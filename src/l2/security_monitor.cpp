// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file security_monitor.cpp
 * @brief Implementation of Security Monitoring System for Cascoin L2
 * 
 * Requirements: 33.1, 33.2, 33.5, 33.6, 36.6
 */

#include <l2/security_monitor.h>
#include <util.h>
#include <utiltime.h>

#include <algorithm>
#include <numeric>
#include <cmath>

namespace l2 {

// ============================================================================
// Global Instance
// ============================================================================

static std::unique_ptr<SecurityMonitor> g_security_monitor;
static CCriticalSection cs_security_monitor_init;

SecurityMonitor& GetSecurityMonitor() {
    LOCK(cs_security_monitor_init);
    if (!g_security_monitor) {
        throw std::runtime_error("Security monitor not initialized");
    }
    return *g_security_monitor;
}

void InitSecurityMonitor(uint64_t chainId) {
    LOCK(cs_security_monitor_init);
    g_security_monitor = std::make_unique<SecurityMonitor>(chainId);
}

bool IsSecurityMonitorInitialized() {
    LOCK(cs_security_monitor_init);
    return g_security_monitor != nullptr;
}

// ============================================================================
// Constructor
// ============================================================================

SecurityMonitor::SecurityMonitor(uint64_t chainId)
    : chainId_(chainId)
    , currentTVL_(0)
    , volumeSpikeThreshold_(TX_VOLUME_SPIKE_THRESHOLD)
    , valueSpikeThreshold_(TX_VALUE_SPIKE_THRESHOLD)
    , circuitBreakerThreshold_(CIRCUIT_BREAKER_TVL_THRESHOLD)
    , nextAlertId_(1)
    , nextAuditId_(1)
{
    circuitBreakerStatus_.state = CircuitBreakerState::NORMAL;
    circuitBreakerStatus_.lastStateChange = GetTime();
}

// ============================================================================
// Anomaly Detection
// ============================================================================

void SecurityMonitor::RecordTransaction(
    const uint256& txHash,
    const uint160& sender,
    const uint160& receiver,
    CAmount value,
    uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    transactionRecords_.push_back(std::make_tuple(txHash, sender, receiver, value, timestamp));
    
    // Prune old records
    PruneTransactionRecords(timestamp);
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["value"] = std::to_string(value);
    LogAudit(SecurityEventCategory::TRANSACTION_ANOMALY, "transaction_recorded",
             sender.ToString(), receiver.ToString(), "", metadata, txHash, true);
}

bool SecurityMonitor::DetectVolumeAnomaly(uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    // Get current hour's transaction count
    uint64_t currentHourCount = 0;
    uint64_t hourStart = currentTime - ANOMALY_DETECTION_WINDOW;
    
    for (const auto& record : transactionRecords_) {
        if (std::get<4>(record) >= hourStart) {
            currentHourCount++;
        }
    }
    
    // Calculate average from historical data
    uint64_t avgCount = CalculateAverageTransactionCount(ANOMALY_DETECTION_WINDOW * 24, currentTime);
    
    if (avgCount > 0 && currentHourCount > avgCount * volumeSpikeThreshold_) {
        RecordAnomaly(SecurityEventCategory::TRANSACTION_ANOMALY, currentTime);
        
        CreateAlert(AlertType::WARNING, SecurityEventCategory::TRANSACTION_ANOMALY,
                   "Transaction volume spike detected",
                   "Current: " + std::to_string(currentHourCount) + 
                   ", Average: " + std::to_string(avgCount));
        return true;
    }
    
    return false;
}

bool SecurityMonitor::DetectValueAnomaly(uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    // Get current hour's total value
    CAmount currentHourValue = 0;
    uint64_t hourStart = currentTime - ANOMALY_DETECTION_WINDOW;
    
    for (const auto& record : transactionRecords_) {
        if (std::get<4>(record) >= hourStart) {
            currentHourValue += std::get<3>(record);
        }
    }
    
    // Calculate average from historical data
    CAmount avgValue = CalculateAverageTransactionValue(ANOMALY_DETECTION_WINDOW * 24, currentTime);
    
    if (avgValue > 0 && currentHourValue > avgValue * valueSpikeThreshold_) {
        RecordAnomaly(SecurityEventCategory::TRANSACTION_ANOMALY, currentTime);
        
        CreateAlert(AlertType::WARNING, SecurityEventCategory::TRANSACTION_ANOMALY,
                   "Transaction value spike detected",
                   "Current: " + std::to_string(currentHourValue) + 
                   ", Average: " + std::to_string(avgValue));
        return true;
    }
    
    return false;
}

bool SecurityMonitor::DetectFrequencyAnomaly(const uint160& address, uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    // Count transactions from this address in last hour
    uint64_t addressTxCount = 0;
    uint64_t hourStart = currentTime - ANOMALY_DETECTION_WINDOW;
    
    for (const auto& record : transactionRecords_) {
        if (std::get<4>(record) >= hourStart && std::get<1>(record) == address) {
            addressTxCount++;
        }
    }
    
    // Threshold: more than 100 transactions per hour from single address
    if (addressTxCount > 100) {
        RecordAnomaly(SecurityEventCategory::TRANSACTION_ANOMALY, currentTime);
        
        CreateAlert(AlertType::WARNING, SecurityEventCategory::TRANSACTION_ANOMALY,
                   "High frequency transactions from single address",
                   "Address: " + address.ToString() + ", Count: " + std::to_string(addressTxCount),
                   {address});
        return true;
    }
    
    return false;
}

TransactionStats SecurityMonitor::GetTransactionStats(uint64_t windowSeconds, uint64_t currentTime) const
{
    LOCK(cs_monitor_);
    
    TransactionStats stats;
    stats.windowStart = currentTime - windowSeconds;
    stats.windowEnd = currentTime;
    
    std::set<uint160> senders, receivers;
    CAmount totalValue = 0;
    CAmount maxValue = 0;
    
    for (const auto& record : transactionRecords_) {
        uint64_t timestamp = std::get<4>(record);
        if (timestamp >= stats.windowStart && timestamp <= stats.windowEnd) {
            stats.transactionCount++;
            CAmount value = std::get<3>(record);
            totalValue += value;
            if (value > maxValue) maxValue = value;
            senders.insert(std::get<1>(record));
            receivers.insert(std::get<2>(record));
        }
    }
    
    stats.totalValue = totalValue;
    stats.maxValue = maxValue;
    stats.avgValue = stats.transactionCount > 0 ? totalValue / stats.transactionCount : 0;
    stats.uniqueSenders = senders.size();
    stats.uniqueReceivers = receivers.size();
    
    return stats;
}

// ============================================================================
// Sequencer Monitoring
// ============================================================================

void SecurityMonitor::RecordSequencerAction(
    const uint160& sequencer,
    const std::string& action,
    uint64_t timestamp,
    bool success)
{
    LOCK(cs_monitor_);
    
    // Initialize metrics if needed
    if (sequencerMetrics_.find(sequencer) == sequencerMetrics_.end()) {
        SequencerMetrics metrics;
        metrics.sequencerAddress = sequencer;
        metrics.lastActivityTimestamp = timestamp;
        sequencerMetrics_[sequencer] = metrics;
    }
    
    sequencerMetrics_[sequencer].lastActivityTimestamp = timestamp;
    
    // Log audit entry
    LogAudit(SecurityEventCategory::SEQUENCER_BEHAVIOR, action,
             sequencer.ToString(), "", "", {}, uint256(), success);
}

void SecurityMonitor::RecordBlockProposal(
    const uint160& sequencer,
    const uint256& blockHash,
    uint64_t timestamp,
    bool accepted)
{
    LOCK(cs_monitor_);
    
    if (sequencerMetrics_.find(sequencer) == sequencerMetrics_.end()) {
        SequencerMetrics metrics;
        metrics.sequencerAddress = sequencer;
        sequencerMetrics_[sequencer] = metrics;
    }
    
    sequencerMetrics_[sequencer].blocksProposed++;
    sequencerMetrics_[sequencer].lastActivityTimestamp = timestamp;
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["accepted"] = accepted ? "true" : "false";
    LogAudit(SecurityEventCategory::SEQUENCER_BEHAVIOR, "block_proposal",
             sequencer.ToString(), blockHash.ToString(), "", metadata, uint256(), accepted);
}

void SecurityMonitor::RecordMissedBlock(
    const uint160& sequencer,
    uint64_t slotNumber,
    uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    if (sequencerMetrics_.find(sequencer) == sequencerMetrics_.end()) {
        SequencerMetrics metrics;
        metrics.sequencerAddress = sequencer;
        sequencerMetrics_[sequencer] = metrics;
    }
    
    sequencerMetrics_[sequencer].blocksMissed++;
    
    // Update uptime
    auto& metrics = sequencerMetrics_[sequencer];
    uint64_t totalBlocks = metrics.blocksProposed + metrics.blocksMissed;
    if (totalBlocks > 0) {
        metrics.uptimePercent = (static_cast<double>(metrics.blocksProposed) / totalBlocks) * 100.0;
    }
    
    // Create alert if too many missed blocks
    if (metrics.blocksMissed > 10 && metrics.uptimePercent < 90.0) {
        CreateAlert(AlertType::WARNING, SecurityEventCategory::SEQUENCER_BEHAVIOR,
                   "Sequencer missing blocks",
                   "Sequencer: " + sequencer.ToString() + 
                   ", Missed: " + std::to_string(metrics.blocksMissed) +
                   ", Uptime: " + std::to_string(metrics.uptimePercent) + "%",
                   {sequencer});
    }
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["slot"] = std::to_string(slotNumber);
    LogAudit(SecurityEventCategory::SEQUENCER_BEHAVIOR, "missed_block",
             sequencer.ToString(), "", "", metadata, uint256(), false);
}

void SecurityMonitor::RecordSequencerVote(
    const uint160& sequencer,
    const uint256& blockHash,
    VoteType vote,
    uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    if (sequencerMetrics_.find(sequencer) == sequencerMetrics_.end()) {
        SequencerMetrics metrics;
        metrics.sequencerAddress = sequencer;
        sequencerMetrics_[sequencer] = metrics;
    }
    
    auto& metrics = sequencerMetrics_[sequencer];
    switch (vote) {
        case VoteType::ACCEPT:
            metrics.votesAccept++;
            break;
        case VoteType::REJECT:
            metrics.votesReject++;
            break;
        case VoteType::ABSTAIN:
            metrics.votesAbstain++;
            break;
    }
    metrics.lastActivityTimestamp = timestamp;
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["vote"] = (vote == VoteType::ACCEPT) ? "accept" : 
                       (vote == VoteType::REJECT) ? "reject" : "abstain";
    LogAudit(SecurityEventCategory::SEQUENCER_BEHAVIOR, "vote",
             sequencer.ToString(), blockHash.ToString(), "", metadata);
}

SequencerMetrics SecurityMonitor::GetSequencerMetrics(const uint160& sequencer) const
{
    LOCK(cs_monitor_);
    
    auto it = sequencerMetrics_.find(sequencer);
    if (it != sequencerMetrics_.end()) {
        return it->second;
    }
    
    SequencerMetrics empty;
    empty.sequencerAddress = sequencer;
    return empty;
}

std::map<uint160, SequencerMetrics> SecurityMonitor::GetAllSequencerMetrics() const
{
    LOCK(cs_monitor_);
    return sequencerMetrics_;
}

// ============================================================================
// Bridge Monitoring
// ============================================================================

void SecurityMonitor::RecordBridgeBalance(
    CAmount expectedBalance,
    CAmount actualBalance,
    uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    bridgeBalanceRecords_.push_back(std::make_tuple(expectedBalance, actualBalance, timestamp));
    
    // Keep only recent records
    while (bridgeBalanceRecords_.size() > 1000) {
        bridgeBalanceRecords_.pop_front();
    }
    
    // Check for discrepancy
    CAmount discrepancy = actualBalance - expectedBalance;
    double discrepancyPercent = expectedBalance > 0 ? 
        std::abs(static_cast<double>(discrepancy)) / expectedBalance : 0.0;
    
    if (discrepancyPercent > BRIDGE_BALANCE_DISCREPANCY_THRESHOLD) {
        RecordAnomaly(SecurityEventCategory::BRIDGE_DISCREPANCY, timestamp);
        
        CreateAlert(AlertType::CRITICAL, SecurityEventCategory::BRIDGE_DISCREPANCY,
                   "Bridge balance discrepancy detected",
                   "Expected: " + std::to_string(expectedBalance) + 
                   ", Actual: " + std::to_string(actualBalance) +
                   ", Discrepancy: " + std::to_string(discrepancy));
    }
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["expected"] = std::to_string(expectedBalance);
    metadata["actual"] = std::to_string(actualBalance);
    metadata["discrepancy"] = std::to_string(discrepancy);
    LogAudit(SecurityEventCategory::BRIDGE_DISCREPANCY, "balance_check",
             "bridge", "", "", metadata);
}

bool SecurityMonitor::HasBridgeDiscrepancy() const
{
    LOCK(cs_monitor_);
    
    if (bridgeBalanceRecords_.empty()) {
        return false;
    }
    
    const auto& latest = bridgeBalanceRecords_.back();
    CAmount expected = std::get<0>(latest);
    CAmount actual = std::get<1>(latest);
    
    if (expected == 0) return false;
    
    double discrepancyPercent = std::abs(static_cast<double>(actual - expected)) / expected;
    return discrepancyPercent > BRIDGE_BALANCE_DISCREPANCY_THRESHOLD;
}

CAmount SecurityMonitor::GetBridgeDiscrepancy() const
{
    LOCK(cs_monitor_);
    
    if (bridgeBalanceRecords_.empty()) {
        return 0;
    }
    
    const auto& latest = bridgeBalanceRecords_.back();
    return std::get<1>(latest) - std::get<0>(latest);
}

// ============================================================================
// Reputation Monitoring
// ============================================================================

void SecurityMonitor::RecordReputationChange(
    const uint160& address,
    uint32_t oldScore,
    uint32_t newScore,
    uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    reputationHistory_[address].push_back(std::make_pair(newScore, timestamp));
    
    // Keep only recent history
    while (reputationHistory_[address].size() > 100) {
        reputationHistory_[address].erase(reputationHistory_[address].begin());
    }
    
    // Check for significant drop
    if (oldScore > newScore && (oldScore - newScore) >= REPUTATION_DROP_THRESHOLD) {
        RecordAnomaly(SecurityEventCategory::REPUTATION_CHANGE, timestamp);
        
        CreateAlert(AlertType::WARNING, SecurityEventCategory::REPUTATION_CHANGE,
                   "Significant reputation drop detected",
                   "Address: " + address.ToString() + 
                   ", Old: " + std::to_string(oldScore) +
                   ", New: " + std::to_string(newScore),
                   {address});
    }
    
    // Update sequencer metrics if applicable
    auto it = sequencerMetrics_.find(address);
    if (it != sequencerMetrics_.end()) {
        it->second.previousReputationScore = oldScore;
        it->second.reputationScore = newScore;
    }
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["old_score"] = std::to_string(oldScore);
    metadata["new_score"] = std::to_string(newScore);
    metadata["change"] = std::to_string(static_cast<int32_t>(newScore) - static_cast<int32_t>(oldScore));
    LogAudit(SecurityEventCategory::REPUTATION_CHANGE, "reputation_change",
             address.ToString(), "", "", metadata);
}

bool SecurityMonitor::HasSignificantReputationDrop(const uint160& address) const
{
    LOCK(cs_monitor_);
    
    auto it = reputationHistory_.find(address);
    if (it == reputationHistory_.end() || it->second.size() < 2) {
        return false;
    }
    
    const auto& history = it->second;
    uint32_t latest = history.back().first;
    uint32_t previous = history[history.size() - 2].first;
    
    return previous > latest && (previous - latest) >= REPUTATION_DROP_THRESHOLD;
}

// ============================================================================
// Alert System
// ============================================================================

SecurityAlert SecurityMonitor::CreateAlert(
    AlertType type,
    SecurityEventCategory category,
    const std::string& message,
    const std::string& details,
    const std::vector<uint160>& involvedAddresses,
    const std::vector<uint256>& relatedTxHashes)
{
    LOCK(cs_monitor_);
    
    SecurityAlert alert;
    alert.alertId = GenerateAlertId();
    alert.type = type;
    alert.category = category;
    alert.message = message;
    alert.details = details;
    alert.timestamp = GetTime();
    alert.involvedAddresses = involvedAddresses;
    alert.relatedTxHashes = relatedTxHashes;
    alert.acknowledged = false;
    alert.resolved = false;
    
    alerts_[alert.alertId] = alert;
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["type"] = std::to_string(static_cast<uint8_t>(type));
    metadata["category"] = std::to_string(static_cast<uint8_t>(category));
    LogAudit(category, "alert_created", "system", alert.alertId.ToString(), message, metadata);
    
    // Notify callbacks
    NotifyAlertCallbacks(alert);
    
    // Auto-trigger circuit breaker for emergency alerts
    if (type == AlertType::EMERGENCY) {
        TriggerCircuitBreaker("Emergency alert: " + message, alert.timestamp);
    }
    
    return alert;
}

std::vector<SecurityAlert> SecurityMonitor::GetActiveAlerts() const
{
    LOCK(cs_monitor_);
    
    std::vector<SecurityAlert> active;
    for (const auto& pair : alerts_) {
        if (!pair.second.resolved) {
            active.push_back(pair.second);
        }
    }
    return active;
}

std::vector<SecurityAlert> SecurityMonitor::GetAlertsByType(AlertType type) const
{
    LOCK(cs_monitor_);
    
    std::vector<SecurityAlert> result;
    for (const auto& pair : alerts_) {
        if (pair.second.type == type) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<SecurityAlert> SecurityMonitor::GetAlertsByCategory(SecurityEventCategory category) const
{
    LOCK(cs_monitor_);
    
    std::vector<SecurityAlert> result;
    for (const auto& pair : alerts_) {
        if (pair.second.category == category) {
            result.push_back(pair.second);
        }
    }
    return result;
}

bool SecurityMonitor::AcknowledgeAlert(const uint256& alertId)
{
    LOCK(cs_monitor_);
    
    auto it = alerts_.find(alertId);
    if (it == alerts_.end()) {
        return false;
    }
    
    it->second.acknowledged = true;
    
    LogAudit(it->second.category, "alert_acknowledged", "operator", 
             alertId.ToString(), "", {});
    
    return true;
}

bool SecurityMonitor::ResolveAlert(const uint256& alertId, const std::string& resolution)
{
    LOCK(cs_monitor_);
    
    auto it = alerts_.find(alertId);
    if (it == alerts_.end()) {
        return false;
    }
    
    it->second.resolved = true;
    it->second.acknowledged = true;
    
    std::map<std::string, std::string> metadata;
    metadata["resolution"] = resolution;
    LogAudit(it->second.category, "alert_resolved", "operator", 
             alertId.ToString(), resolution, metadata);
    
    return true;
}

std::map<AlertType, uint64_t> SecurityMonitor::GetAlertCounts() const
{
    LOCK(cs_monitor_);
    
    std::map<AlertType, uint64_t> counts;
    counts[AlertType::INFO] = 0;
    counts[AlertType::WARNING] = 0;
    counts[AlertType::CRITICAL] = 0;
    counts[AlertType::EMERGENCY] = 0;
    
    for (const auto& pair : alerts_) {
        if (!pair.second.resolved) {
            counts[pair.second.type]++;
        }
    }
    
    return counts;
}

void SecurityMonitor::RegisterAlertCallback(AlertCallback callback)
{
    LOCK(cs_monitor_);
    alertCallbacks_.push_back(callback);
}

// ============================================================================
// Audit Logging
// ============================================================================

AuditLogEntry SecurityMonitor::LogAudit(
    SecurityEventCategory category,
    const std::string& action,
    const std::string& actor,
    const std::string& target,
    const std::string& details,
    const std::map<std::string, std::string>& metadata,
    const uint256& relatedTxHash,
    bool success)
{
    LOCK(cs_monitor_);
    
    AuditLogEntry entry;
    entry.entryId = GenerateAuditId();
    entry.timestamp = GetTime();
    entry.category = category;
    entry.action = action;
    entry.actor = actor;
    entry.target = target;
    entry.details = details;
    entry.metadata = metadata;
    entry.relatedTxHash = relatedTxHash;
    entry.success = success;
    
    auditLog_.push_back(entry);
    
    // Enforce maximum entries
    while (auditLog_.size() > MAX_AUDIT_LOG_ENTRIES) {
        auditLog_.pop_front();
    }
    
    return entry;
}

std::vector<AuditLogEntry> SecurityMonitor::GetAuditLog(uint64_t startTime, uint64_t endTime) const
{
    LOCK(cs_monitor_);
    
    std::vector<AuditLogEntry> result;
    for (const auto& entry : auditLog_) {
        if (entry.timestamp >= startTime && entry.timestamp <= endTime) {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<AuditLogEntry> SecurityMonitor::GetAuditLogByCategory(
    SecurityEventCategory category,
    size_t limit) const
{
    LOCK(cs_monitor_);
    
    std::vector<AuditLogEntry> result;
    for (auto it = auditLog_.rbegin(); it != auditLog_.rend() && result.size() < limit; ++it) {
        if (it->category == category) {
            result.push_back(*it);
        }
    }
    return result;
}

std::vector<AuditLogEntry> SecurityMonitor::GetAuditLogByActor(
    const std::string& actor,
    size_t limit) const
{
    LOCK(cs_monitor_);
    
    std::vector<AuditLogEntry> result;
    for (auto it = auditLog_.rbegin(); it != auditLog_.rend() && result.size() < limit; ++it) {
        if (it->actor == actor) {
            result.push_back(*it);
        }
    }
    return result;
}

size_t SecurityMonitor::GetAuditLogCount() const
{
    LOCK(cs_monitor_);
    return auditLog_.size();
}

size_t SecurityMonitor::PruneAuditLog(uint64_t currentTime, uint64_t retentionSeconds)
{
    LOCK(cs_monitor_);
    
    uint64_t cutoffTime = currentTime - retentionSeconds;
    size_t pruned = 0;
    
    while (!auditLog_.empty() && auditLog_.front().timestamp < cutoffTime) {
        auditLog_.pop_front();
        pruned++;
    }
    
    return pruned;
}

// ============================================================================
// Circuit Breaker
// ============================================================================

void SecurityMonitor::RecordWithdrawal(CAmount amount, uint64_t timestamp)
{
    LOCK(cs_monitor_);
    
    withdrawalRecords_.push_back(std::make_pair(amount, timestamp));
    PruneWithdrawalRecords(timestamp);
    
    // Check if circuit breaker should trigger
    if (ShouldTriggerCircuitBreaker(timestamp)) {
        TriggerCircuitBreaker("Daily withdrawal volume exceeded 10% of TVL", timestamp);
    }
}

void SecurityMonitor::UpdateTVL(CAmount tvl, uint64_t timestamp)
{
    LOCK(cs_monitor_);
    currentTVL_ = tvl;
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["tvl"] = std::to_string(tvl);
    LogAudit(SecurityEventCategory::SYSTEM_ERROR, "tvl_update", "system", "", "", metadata);
}

bool SecurityMonitor::ShouldTriggerCircuitBreaker(uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    if (circuitBreakerStatus_.IsTriggered()) {
        return false;  // Already triggered
    }
    
    if (currentTVL_ == 0) {
        return false;  // No TVL to protect
    }
    
    CAmount dailyVolume = GetDailyWithdrawalVolume(currentTime);
    double ratio = static_cast<double>(dailyVolume) / currentTVL_;
    
    return ratio >= circuitBreakerThreshold_;
}

void SecurityMonitor::TriggerCircuitBreaker(const std::string& reason, uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    if (circuitBreakerStatus_.IsTriggered()) {
        return;  // Already triggered
    }
    
    circuitBreakerStatus_.state = CircuitBreakerState::TRIGGERED;
    circuitBreakerStatus_.triggeredAt = currentTime;
    circuitBreakerStatus_.lastStateChange = currentTime;
    circuitBreakerStatus_.triggerReason = reason;
    circuitBreakerStatus_.tvlAtTrigger = currentTVL_;
    circuitBreakerStatus_.withdrawalVolumeAtTrigger = GetDailyWithdrawalVolume(currentTime);
    circuitBreakerStatus_.cooldownEndsAt = currentTime + CIRCUIT_BREAKER_COOLDOWN;
    
    // Create emergency alert
    CreateAlert(AlertType::EMERGENCY, SecurityEventCategory::CIRCUIT_BREAKER,
               "Circuit breaker triggered",
               reason);
    
    // Log audit entry
    std::map<std::string, std::string> metadata;
    metadata["reason"] = reason;
    metadata["tvl"] = std::to_string(currentTVL_);
    metadata["withdrawal_volume"] = std::to_string(circuitBreakerStatus_.withdrawalVolumeAtTrigger);
    LogAudit(SecurityEventCategory::CIRCUIT_BREAKER, "triggered", "system", "", reason, metadata);
    
    // Notify callbacks
    NotifyCircuitBreakerCallbacks(CircuitBreakerState::TRIGGERED, reason);
}

bool SecurityMonitor::ResetCircuitBreaker(uint64_t currentTime)
{
    LOCK(cs_monitor_);
    
    if (!circuitBreakerStatus_.IsTriggered()) {
        return false;  // Not triggered
    }
    
    if (circuitBreakerStatus_.IsInCooldown(currentTime)) {
        return false;  // Still in cooldown
    }
    
    circuitBreakerStatus_.state = CircuitBreakerState::RECOVERY;
    circuitBreakerStatus_.lastStateChange = currentTime;
    
    // Log audit entry
    LogAudit(SecurityEventCategory::CIRCUIT_BREAKER, "reset", "operator", "", "Manual reset");
    
    // Notify callbacks
    NotifyCircuitBreakerCallbacks(CircuitBreakerState::RECOVERY, "Manual reset");
    
    // After recovery, set to normal
    circuitBreakerStatus_.state = CircuitBreakerState::NORMAL;
    NotifyCircuitBreakerCallbacks(CircuitBreakerState::NORMAL, "Recovery complete");
    
    return true;
}

CircuitBreakerStatus SecurityMonitor::GetCircuitBreakerStatus() const
{
    LOCK(cs_monitor_);
    return circuitBreakerStatus_;
}

bool SecurityMonitor::IsCircuitBreakerTriggered() const
{
    LOCK(cs_monitor_);
    return circuitBreakerStatus_.IsTriggered();
}

void SecurityMonitor::RegisterCircuitBreakerCallback(CircuitBreakerCallback callback)
{
    LOCK(cs_monitor_);
    circuitBreakerCallbacks_.push_back(callback);
}

CAmount SecurityMonitor::GetDailyWithdrawalVolume(uint64_t currentTime) const
{
    LOCK(cs_monitor_);
    
    uint64_t dayStart = currentTime - (24 * 60 * 60);
    CAmount total = 0;
    
    for (const auto& record : withdrawalRecords_) {
        if (record.second >= dayStart) {
            total += record.first;
        }
    }
    
    return total;
}

// ============================================================================
// Dashboard and Metrics
// ============================================================================

SecurityDashboardMetrics SecurityMonitor::GetDashboardMetrics(uint64_t currentTime) const
{
    LOCK(cs_monitor_);
    
    SecurityDashboardMetrics metrics;
    metrics.timestamp = currentTime;
    
    // Count alerts
    for (const auto& pair : alerts_) {
        if (!pair.second.resolved) {
            metrics.activeAlerts++;
            if (pair.second.type == AlertType::CRITICAL || 
                pair.second.type == AlertType::EMERGENCY) {
                metrics.criticalAlerts++;
            }
        }
    }
    
    metrics.totalAuditEntries = auditLog_.size();
    metrics.circuitBreakerState = circuitBreakerStatus_.state;
    metrics.totalValueLocked = currentTVL_;
    metrics.dailyWithdrawalVolume = GetDailyWithdrawalVolume(currentTime);
    
    if (currentTVL_ > 0) {
        metrics.withdrawalToTvlRatio = static_cast<double>(metrics.dailyWithdrawalVolume) / currentTVL_;
    }
    
    // Sequencer metrics
    metrics.activeSequencers = sequencerMetrics_.size();
    double totalUptime = 0.0;
    for (const auto& pair : sequencerMetrics_) {
        totalUptime += pair.second.uptimePercent;
    }
    if (metrics.activeSequencers > 0) {
        metrics.avgSequencerUptime = totalUptime / metrics.activeSequencers;
    }
    
    metrics.anomaliesDetected24h = GetAnomaliesDetected24h(currentTime);
    
    return metrics;
}

uint64_t SecurityMonitor::GetAnomaliesDetected24h(uint64_t currentTime) const
{
    LOCK(cs_monitor_);
    
    uint64_t dayStart = currentTime - (24 * 60 * 60);
    uint64_t count = 0;
    
    for (const auto& record : anomalyRecords_) {
        if (record.second >= dayStart) {
            count++;
        }
    }
    
    return count;
}

// ============================================================================
// Configuration
// ============================================================================

void SecurityMonitor::SetVolumeSpikeTreshold(double threshold)
{
    LOCK(cs_monitor_);
    volumeSpikeThreshold_ = threshold;
}

void SecurityMonitor::SetValueSpikeThreshold(double threshold)
{
    LOCK(cs_monitor_);
    valueSpikeThreshold_ = threshold;
}

void SecurityMonitor::SetCircuitBreakerThreshold(double threshold)
{
    LOCK(cs_monitor_);
    circuitBreakerThreshold_ = threshold;
}

void SecurityMonitor::Clear()
{
    LOCK(cs_monitor_);
    
    alerts_.clear();
    auditLog_.clear();
    transactionRecords_.clear();
    sequencerMetrics_.clear();
    reputationHistory_.clear();
    bridgeBalanceRecords_.clear();
    withdrawalRecords_.clear();
    anomalyRecords_.clear();
    alertCallbacks_.clear();
    circuitBreakerCallbacks_.clear();
    
    currentTVL_ = 0;
    circuitBreakerStatus_ = CircuitBreakerStatus();
    nextAlertId_ = 1;
    nextAuditId_ = 1;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint256 SecurityMonitor::GenerateAlertId()
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << chainId_ << nextAlertId_++ << GetTime();
    return ss.GetHash();
}

uint256 SecurityMonitor::GenerateAuditId()
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << chainId_ << nextAuditId_++ << GetTime();
    return ss.GetHash();
}

void SecurityMonitor::NotifyAlertCallbacks(const SecurityAlert& alert)
{
    for (const auto& callback : alertCallbacks_) {
        try {
            callback(alert);
        } catch (const std::exception& e) {
            // Log error but don't propagate
        }
    }
}

void SecurityMonitor::NotifyCircuitBreakerCallbacks(CircuitBreakerState state, const std::string& reason)
{
    for (const auto& callback : circuitBreakerCallbacks_) {
        try {
            callback(state, reason);
        } catch (const std::exception& e) {
            // Log error but don't propagate
        }
    }
}

void SecurityMonitor::PruneTransactionRecords(uint64_t currentTime)
{
    // Keep records for 24 hours
    uint64_t cutoff = currentTime - (24 * 60 * 60);
    
    while (!transactionRecords_.empty() && std::get<4>(transactionRecords_.front()) < cutoff) {
        transactionRecords_.pop_front();
    }
}

void SecurityMonitor::PruneWithdrawalRecords(uint64_t currentTime)
{
    // Keep records for 24 hours
    uint64_t cutoff = currentTime - (24 * 60 * 60);
    
    while (!withdrawalRecords_.empty() && withdrawalRecords_.front().second < cutoff) {
        withdrawalRecords_.pop_front();
    }
}

void SecurityMonitor::RecordAnomaly(SecurityEventCategory category, uint64_t timestamp)
{
    anomalyRecords_.push_back(std::make_pair(category, timestamp));
    
    // Keep only 24 hours of records
    uint64_t cutoff = timestamp - (24 * 60 * 60);
    while (!anomalyRecords_.empty() && anomalyRecords_.front().second < cutoff) {
        anomalyRecords_.pop_front();
    }
}

CAmount SecurityMonitor::CalculateAverageTransactionValue(uint64_t windowSeconds, uint64_t currentTime) const
{
    uint64_t windowStart = currentTime - windowSeconds;
    CAmount total = 0;
    uint64_t count = 0;
    
    for (const auto& record : transactionRecords_) {
        if (std::get<4>(record) >= windowStart) {
            total += std::get<3>(record);
            count++;
        }
    }
    
    return count > 0 ? total / count : 0;
}

uint64_t SecurityMonitor::CalculateAverageTransactionCount(uint64_t windowSeconds, uint64_t currentTime) const
{
    uint64_t windowStart = currentTime - windowSeconds;
    uint64_t count = 0;
    
    for (const auto& record : transactionRecords_) {
        if (std::get<4>(record) >= windowStart) {
            count++;
        }
    }
    
    // Calculate hourly average
    uint64_t hours = windowSeconds / ANOMALY_DETECTION_WINDOW;
    return hours > 0 ? count / hours : count;
}

} // namespace l2
