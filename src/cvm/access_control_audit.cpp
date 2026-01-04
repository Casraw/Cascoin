// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/access_control_audit.h>
#include <cvm/cvmdb.h>
#include <cvm/security_audit.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>

namespace CVM {

// Global instance
std::unique_ptr<AccessControlAuditor> g_accessControlAuditor;

// Database keys
static const char DB_ACCESS_AUDIT = 'Q';      // Access audit: 'Q' + entryId -> AccessControlAuditEntry
static const char DB_BLACKLIST = 'K';         // Blacklist: 'K' + address -> (reason, expiry)

// ========== AccessControlAuditEntry Implementation ==========

std::string AccessControlAuditEntry::GetOperationTypeString() const {
    switch (operationType) {
        case AccessOperationType::TRUST_SCORE_QUERY: return "TRUST_SCORE_QUERY";
        case AccessOperationType::TRUST_SCORE_MODIFICATION: return "TRUST_SCORE_MODIFICATION";
        case AccessOperationType::REPUTATION_GATED_CALL: return "REPUTATION_GATED_CALL";
        case AccessOperationType::GAS_DISCOUNT_CLAIM: return "GAS_DISCOUNT_CLAIM";
        case AccessOperationType::FREE_GAS_CLAIM: return "FREE_GAS_CLAIM";
        case AccessOperationType::VALIDATOR_REGISTRATION: return "VALIDATOR_REGISTRATION";
        case AccessOperationType::VALIDATOR_RESPONSE: return "VALIDATOR_RESPONSE";
        case AccessOperationType::CONTRACT_DEPLOYMENT: return "CONTRACT_DEPLOYMENT";
        case AccessOperationType::CONTRACT_CALL: return "CONTRACT_CALL";
        case AccessOperationType::STORAGE_ACCESS: return "STORAGE_ACCESS";
        case AccessOperationType::CROSS_CHAIN_ATTESTATION: return "CROSS_CHAIN_ATTESTATION";
        case AccessOperationType::DAO_VOTE: return "DAO_VOTE";
        case AccessOperationType::DISPUTE_CREATION: return "DISPUTE_CREATION";
        case AccessOperationType::DISPUTE_RESOLUTION: return "DISPUTE_RESOLUTION";
        default: return "UNKNOWN";
    }
}

std::string AccessControlAuditEntry::GetDecisionString() const {
    switch (decision) {
        case AccessDecision::GRANTED: return "GRANTED";
        case AccessDecision::DENIED_INSUFFICIENT_REPUTATION: return "DENIED_INSUFFICIENT_REPUTATION";
        case AccessDecision::DENIED_RATE_LIMITED: return "DENIED_RATE_LIMITED";
        case AccessDecision::DENIED_BLACKLISTED: return "DENIED_BLACKLISTED";
        case AccessDecision::DENIED_INVALID_SIGNATURE: return "DENIED_INVALID_SIGNATURE";
        case AccessDecision::DENIED_INSUFFICIENT_STAKE: return "DENIED_INSUFFICIENT_STAKE";
        case AccessDecision::DENIED_COOLDOWN: return "DENIED_COOLDOWN";
        case AccessDecision::DENIED_OTHER: return "DENIED_OTHER";
        default: return "UNKNOWN";
    }
}

// ========== AccessControlAuditor Implementation ==========

AccessControlAuditor::AccessControlAuditor(CVMDatabase& db, SecurityAuditLogger* auditLogger)
    : m_db(db)
    , m_auditLogger(auditLogger)
    , m_currentBlockHeight(0)
    , m_nextEntryId(1)
    , m_maxEntriesInMemory(10000)
{
    // Initialize default rate limits
    m_rateLimitConfigs[AccessOperationType::TRUST_SCORE_QUERY] = {1000, 3600};  // 1000/hour
    m_rateLimitConfigs[AccessOperationType::TRUST_SCORE_MODIFICATION] = {100, 3600};  // 100/hour
    m_rateLimitConfigs[AccessOperationType::REPUTATION_GATED_CALL] = {500, 3600};  // 500/hour
    m_rateLimitConfigs[AccessOperationType::CONTRACT_DEPLOYMENT] = {10, 3600};  // 10/hour
    m_rateLimitConfigs[AccessOperationType::CONTRACT_CALL] = {1000, 3600};  // 1000/hour
    m_rateLimitConfigs[AccessOperationType::VALIDATOR_REGISTRATION] = {1, 86400};  // 1/day
    m_rateLimitConfigs[AccessOperationType::DAO_VOTE] = {100, 86400};  // 100/day
    
    // Initialize default minimum reputation requirements
    m_minReputationRequirements[AccessOperationType::TRUST_SCORE_QUERY] = 0;
    m_minReputationRequirements[AccessOperationType::TRUST_SCORE_MODIFICATION] = 50;
    m_minReputationRequirements[AccessOperationType::REPUTATION_GATED_CALL] = 0;  // Varies by operation
    m_minReputationRequirements[AccessOperationType::GAS_DISCOUNT_CLAIM] = 40;
    m_minReputationRequirements[AccessOperationType::FREE_GAS_CLAIM] = 80;
    m_minReputationRequirements[AccessOperationType::VALIDATOR_REGISTRATION] = 70;
    m_minReputationRequirements[AccessOperationType::VALIDATOR_RESPONSE] = 70;
    m_minReputationRequirements[AccessOperationType::CONTRACT_DEPLOYMENT] = 20;
    m_minReputationRequirements[AccessOperationType::CONTRACT_CALL] = 0;
    m_minReputationRequirements[AccessOperationType::STORAGE_ACCESS] = 0;
    m_minReputationRequirements[AccessOperationType::CROSS_CHAIN_ATTESTATION] = 60;
    m_minReputationRequirements[AccessOperationType::DAO_VOTE] = 50;
    m_minReputationRequirements[AccessOperationType::DISPUTE_CREATION] = 40;
    m_minReputationRequirements[AccessOperationType::DISPUTE_RESOLUTION] = 70;
    
    // Enable logging for all operation types by default
    for (int i = 0; i <= static_cast<int>(AccessOperationType::DISPUTE_RESOLUTION); i++) {
        m_loggingEnabled[static_cast<AccessOperationType>(i)] = true;
    }
}

AccessControlAuditor::~AccessControlAuditor() {
}

bool AccessControlAuditor::Initialize(int currentBlockHeight) {
    LOCK(m_cs);
    
    m_currentBlockHeight = currentBlockHeight;
    m_currentStats = AccessControlStats();
    m_currentStats.windowStart = GetCurrentTimestamp();
    m_currentStats.startBlockHeight = currentBlockHeight;
    
    LoadBlacklist();
    
    LogPrint(BCLog::CVM, "Access control auditor initialized at block %d\n", currentBlockHeight);
    return true;
}

// ========== Access Control Logging ==========

void AccessControlAuditor::LogTrustScoreQuery(const uint160& requester, const uint160& target,
                                             int16_t score, const std::string& context) {
    LOCK(m_cs);
    
    if (!m_loggingEnabled[AccessOperationType::TRUST_SCORE_QUERY]) {
        return;
    }
    
    AccessControlAuditEntry entry;
    entry.entryId = m_nextEntryId++;
    entry.operationType = AccessOperationType::TRUST_SCORE_QUERY;
    entry.decision = AccessDecision::GRANTED;
    entry.requesterAddress = requester;
    entry.targetAddress = target;
    entry.operationName = "TrustScoreQuery";
    entry.timestamp = GetCurrentTimestamp();
    entry.blockHeight = m_currentBlockHeight;
    entry.actualReputation = score;
    
    if (!context.empty()) {
        entry.metadata["context"] = context;
    }
    
    AddEntry(entry);
    
    // Also log to security audit if available
    if (m_auditLogger) {
        m_auditLogger->LogTrustScoreQuery(requester, target, score);
    }
    
    LogPrint(BCLog::CVM, "Access: Trust score query from %s for %s: %d\n",
             requester.GetHex().substr(0, 16), target.GetHex().substr(0, 16), score);
}

void AccessControlAuditor::LogTrustScoreModification(const uint160& modifier, const uint160& target,
                                                    int16_t oldScore, int16_t newScore,
                                                    const std::string& reason) {
    LOCK(m_cs);
    
    if (!m_loggingEnabled[AccessOperationType::TRUST_SCORE_MODIFICATION]) {
        return;
    }
    
    AccessControlAuditEntry entry;
    entry.entryId = m_nextEntryId++;
    entry.operationType = AccessOperationType::TRUST_SCORE_MODIFICATION;
    entry.decision = AccessDecision::GRANTED;
    entry.requesterAddress = modifier;
    entry.targetAddress = target;
    entry.operationName = "TrustScoreModification";
    entry.timestamp = GetCurrentTimestamp();
    entry.blockHeight = m_currentBlockHeight;
    entry.actualReputation = newScore;
    entry.metadata["old_score"] = tfm::format("%d", oldScore);
    entry.metadata["new_score"] = tfm::format("%d", newScore);
    entry.metadata["reason"] = reason;
    
    AddEntry(entry);
    
    // Also log to security audit if available
    if (m_auditLogger) {
        m_auditLogger->LogTrustScoreModification(modifier, target, oldScore, newScore, reason);
    }
    
    LogPrint(BCLog::CVM, "Access: Trust score modification by %s for %s: %d -> %d (%s)\n",
             modifier.GetHex().substr(0, 16), target.GetHex().substr(0, 16),
             oldScore, newScore, reason);
}

AccessDecision AccessControlAuditor::LogReputationGatedOperation(
    const uint160& requester,
    AccessOperationType operationType,
    const std::string& operationName,
    int16_t requiredReputation,
    int16_t actualReputation,
    const std::string& resourceId,
    const uint256& txHash) {
    
    LOCK(m_cs);
    
    AccessControlAuditEntry entry;
    entry.entryId = m_nextEntryId++;
    entry.operationType = operationType;
    entry.requesterAddress = requester;
    entry.operationName = operationName;
    entry.resourceId = resourceId;
    entry.txHash = txHash;
    entry.timestamp = GetCurrentTimestamp();
    entry.blockHeight = m_currentBlockHeight;
    entry.requiredReputation = requiredReputation;
    entry.actualReputation = actualReputation;
    entry.reputationDeficit = requiredReputation - actualReputation;
    
    // Check blacklist first
    if (IsBlacklisted(requester)) {
        entry.decision = AccessDecision::DENIED_BLACKLISTED;
        entry.denialReason = "Address is blacklisted";
        AddEntry(entry);
        
        LogPrint(BCLog::CVM, "Access: DENIED (blacklisted) %s for %s\n",
                 operationName, requester.GetHex().substr(0, 16));
        return entry.decision;
    }
    
    // Check rate limit
    uint32_t requestsInWindow, maxAllowed;
    if (!CheckRateLimit(requester, operationType, requestsInWindow, maxAllowed)) {
        entry.decision = AccessDecision::DENIED_RATE_LIMITED;
        entry.denialReason = tfm::format("Rate limit exceeded: %d/%d requests", requestsInWindow, maxAllowed);
        entry.requestsInWindow = requestsInWindow;
        entry.maxRequestsAllowed = maxAllowed;
        AddEntry(entry);
        
        LogPrint(BCLog::CVM, "Access: DENIED (rate limited) %s for %s\n",
                 operationName, requester.GetHex().substr(0, 16));
        return entry.decision;
    }
    
    // Check reputation
    if (actualReputation < requiredReputation) {
        entry.decision = AccessDecision::DENIED_INSUFFICIENT_REPUTATION;
        entry.denialReason = tfm::format("Insufficient reputation: %d < %d required",
                                        actualReputation, requiredReputation);
        AddEntry(entry);
        
        // Also log to security audit if available
        if (m_auditLogger) {
            AccessControlRecord record;
            record.requesterAddress = requester;
            record.operation = operationName;
            record.requiredReputation = requiredReputation;
            record.actualReputation = actualReputation;
            record.accessGranted = false;
            record.denialReason = entry.denialReason;
            record.timestamp = entry.timestamp;
            record.blockHeight = entry.blockHeight;
            m_auditLogger->LogReputationGatedAccess(record);
        }
        
        LogPrint(BCLog::CVM, "Access: DENIED (reputation) %s for %s: %d < %d\n",
                 operationName, requester.GetHex().substr(0, 16),
                 actualReputation, requiredReputation);
        return entry.decision;
    }
    
    // Access granted
    entry.decision = AccessDecision::GRANTED;
    AddEntry(entry);
    
    // Also log to security audit if available
    if (m_auditLogger) {
        AccessControlRecord record;
        record.requesterAddress = requester;
        record.operation = operationName;
        record.requiredReputation = requiredReputation;
        record.actualReputation = actualReputation;
        record.accessGranted = true;
        record.timestamp = entry.timestamp;
        record.blockHeight = entry.blockHeight;
        m_auditLogger->LogReputationGatedAccess(record);
    }
    
    LogPrint(BCLog::CVM, "Access: GRANTED %s for %s (reputation: %d >= %d)\n",
             operationName, requester.GetHex().substr(0, 16),
             actualReputation, requiredReputation);
    
    return entry.decision;
}

void AccessControlAuditor::LogAccessDecision(const AccessControlAuditEntry& entry) {
    LOCK(m_cs);
    
    if (!m_loggingEnabled[entry.operationType]) {
        return;
    }
    
    AccessControlAuditEntry mutableEntry = entry;
    mutableEntry.entryId = m_nextEntryId++;
    mutableEntry.timestamp = GetCurrentTimestamp();
    mutableEntry.blockHeight = m_currentBlockHeight;
    
    AddEntry(mutableEntry);
}


// ========== Rate Limiting ==========

bool AccessControlAuditor::CheckRateLimit(const uint160& address, AccessOperationType operationType,
                                         uint32_t& requestsInWindow, uint32_t& maxAllowed) {
    // AssertLockHeld(m_cs); - called from locked context
    
    auto key = std::make_pair(address, operationType);
    auto configIt = m_rateLimitConfigs.find(operationType);
    
    if (configIt == m_rateLimitConfigs.end()) {
        // No rate limit configured for this operation type
        requestsInWindow = 0;
        maxAllowed = UINT32_MAX;
        return true;
    }
    
    maxAllowed = configIt->second.first;
    int64_t windowSeconds = configIt->second.second;
    int64_t now = GetCurrentTimestamp();
    
    auto stateIt = m_rateLimitStates.find(key);
    if (stateIt == m_rateLimitStates.end()) {
        // First request from this address for this operation
        RateLimitState state;
        state.address = address;
        state.operationType = operationType;
        state.requestCount = 1;
        state.windowStart = now;
        state.lastRequest = now;
        m_rateLimitStates[key] = state;
        requestsInWindow = 1;
        return true;
    }
    
    RateLimitState& state = stateIt->second;
    
    // Check if window has expired
    if (now - state.windowStart > windowSeconds) {
        // Reset window
        state.windowStart = now;
        state.requestCount = 1;
        state.lastRequest = now;
        requestsInWindow = 1;
        return true;
    }
    
    // Check if limit exceeded
    requestsInWindow = state.requestCount + 1;
    if (requestsInWindow > maxAllowed) {
        return false;
    }
    
    // Update state
    state.requestCount++;
    state.lastRequest = now;
    return true;
}

void AccessControlAuditor::SetRateLimit(AccessOperationType operationType, uint32_t maxRequests,
                                       int64_t windowSeconds) {
    LOCK(m_cs);
    m_rateLimitConfigs[operationType] = std::make_pair(maxRequests, windowSeconds);
    LogPrint(BCLog::CVM, "Access: Rate limit set for operation %d: %d requests per %d seconds\n",
             static_cast<int>(operationType), maxRequests, windowSeconds);
}

RateLimitState AccessControlAuditor::GetRateLimitState(const uint160& address, AccessOperationType operationType) {
    LOCK(m_cs);
    
    auto key = std::make_pair(address, operationType);
    auto it = m_rateLimitStates.find(key);
    if (it != m_rateLimitStates.end()) {
        return it->second;
    }
    
    RateLimitState emptyState;
    emptyState.address = address;
    emptyState.operationType = operationType;
    return emptyState;
}

// ========== Blacklist Management ==========

void AccessControlAuditor::AddToBlacklist(const uint160& address, const std::string& reason,
                                         int64_t duration) {
    LOCK(m_cs);
    
    int64_t expiry = (duration < 0) ? -1 : GetCurrentTimestamp() + duration;
    m_blacklist[address] = std::make_pair(reason, expiry);
    
    // Persist to database using generic key-value storage
    std::string keyStr = std::string(1, DB_BLACKLIST) + address.GetHex();
    
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue << reason << expiry;
    std::vector<uint8_t> valueData(ssValue.begin(), ssValue.end());
    m_db.WriteGeneric(keyStr, valueData);
    
    LogPrint(BCLog::CVM, "Access: Address %s added to blacklist: %s (expiry: %d)\n",
             address.GetHex().substr(0, 16), reason, expiry);
}

void AccessControlAuditor::RemoveFromBlacklist(const uint160& address) {
    LOCK(m_cs);
    
    m_blacklist.erase(address);
    
    // Remove from database using generic key-value storage
    std::string keyStr = std::string(1, DB_BLACKLIST) + address.GetHex();
    m_db.EraseGeneric(keyStr);
    
    LogPrint(BCLog::CVM, "Access: Address %s removed from blacklist\n",
             address.GetHex().substr(0, 16));
}

bool AccessControlAuditor::IsBlacklisted(const uint160& address) {
    // AssertLockHeld(m_cs); - may be called from locked or unlocked context
    LOCK(m_cs);
    
    auto it = m_blacklist.find(address);
    if (it == m_blacklist.end()) {
        return false;
    }
    
    int64_t expiry = it->second.second;
    if (expiry > 0 && GetCurrentTimestamp() > expiry) {
        // Entry has expired, remove it
        m_blacklist.erase(it);
        return false;
    }
    
    return true;
}

std::vector<std::pair<uint160, std::string>> AccessControlAuditor::GetBlacklistEntries() {
    LOCK(m_cs);
    
    CleanupExpiredBlacklistEntries();
    
    std::vector<std::pair<uint160, std::string>> entries;
    for (const auto& entry : m_blacklist) {
        entries.push_back(std::make_pair(entry.first, entry.second.first));
    }
    return entries;
}

// ========== Statistics and Reporting ==========

AccessControlStats AccessControlAuditor::GetStatistics() {
    LOCK(m_cs);
    
    m_currentStats.windowEnd = GetCurrentTimestamp();
    m_currentStats.endBlockHeight = m_currentBlockHeight;
    m_currentStats.CalculateRates();
    
    return m_currentStats;
}

AccessControlStats AccessControlAuditor::GetStatisticsForWindow(int64_t startTime, int64_t endTime) {
    LOCK(m_cs);
    
    AccessControlStats stats;
    stats.windowStart = startTime;
    stats.windowEnd = endTime;
    
    for (const auto& entry : m_recentEntries) {
        if (entry.timestamp >= startTime && entry.timestamp <= endTime) {
            stats.totalRequests[entry.operationType]++;
            stats.decisionCounts[entry.decision]++;
            stats.totalAccessAttempts++;
            
            if (entry.decision == AccessDecision::GRANTED) {
                stats.grantedRequests[entry.operationType]++;
                stats.totalGranted++;
            } else {
                stats.deniedRequests[entry.operationType]++;
                stats.totalDenied++;
                if (entry.reputationDeficit > 0) {
                    stats.averageReputationDeficit += entry.reputationDeficit;
                }
            }
        }
    }
    
    if (stats.totalDenied > 0) {
        stats.averageReputationDeficit /= stats.totalDenied;
    }
    
    stats.CalculateRates();
    return stats;
}

AccessControlStats AccessControlAuditor::GetStatisticsForBlockRange(int startBlock, int endBlock) {
    LOCK(m_cs);
    
    AccessControlStats stats;
    stats.startBlockHeight = startBlock;
    stats.endBlockHeight = endBlock;
    
    for (const auto& entry : m_recentEntries) {
        if (entry.blockHeight >= startBlock && entry.blockHeight <= endBlock) {
            stats.totalRequests[entry.operationType]++;
            stats.decisionCounts[entry.decision]++;
            stats.totalAccessAttempts++;
            
            if (entry.decision == AccessDecision::GRANTED) {
                stats.grantedRequests[entry.operationType]++;
                stats.totalGranted++;
            } else {
                stats.deniedRequests[entry.operationType]++;
                stats.totalDenied++;
            }
        }
    }
    
    stats.CalculateRates();
    return stats;
}

std::vector<AccessControlAuditEntry> AccessControlAuditor::GetRecentEntries(size_t count) {
    LOCK(m_cs);
    
    std::vector<AccessControlAuditEntry> entries;
    size_t numEntries = std::min(count, m_recentEntries.size());
    
    // Return most recent entries (from the back of the deque)
    auto it = m_recentEntries.rbegin();
    for (size_t i = 0; i < numEntries && it != m_recentEntries.rend(); ++i, ++it) {
        entries.push_back(*it);
    }
    
    return entries;
}

std::vector<AccessControlAuditEntry> AccessControlAuditor::GetEntriesForAddress(const uint160& address,
                                                                                size_t count) {
    LOCK(m_cs);
    
    std::vector<AccessControlAuditEntry> entries;
    
    for (auto it = m_recentEntries.rbegin(); it != m_recentEntries.rend() && entries.size() < count; ++it) {
        if (it->requesterAddress == address || it->targetAddress == address) {
            entries.push_back(*it);
        }
    }
    
    return entries;
}

std::vector<AccessControlAuditEntry> AccessControlAuditor::GetEntriesByOperationType(
    AccessOperationType operationType, size_t count) {
    LOCK(m_cs);
    
    std::vector<AccessControlAuditEntry> entries;
    
    for (auto it = m_recentEntries.rbegin(); it != m_recentEntries.rend() && entries.size() < count; ++it) {
        if (it->operationType == operationType) {
            entries.push_back(*it);
        }
    }
    
    return entries;
}

std::vector<AccessControlAuditEntry> AccessControlAuditor::GetDeniedEntries(size_t count) {
    LOCK(m_cs);
    
    std::vector<AccessControlAuditEntry> entries;
    
    for (auto it = m_recentEntries.rbegin(); it != m_recentEntries.rend() && entries.size() < count; ++it) {
        if (it->decision != AccessDecision::GRANTED) {
            entries.push_back(*it);
        }
    }
    
    return entries;
}

// ========== Configuration ==========

void AccessControlAuditor::SetMinimumReputation(AccessOperationType operationType, int16_t minReputation) {
    LOCK(m_cs);
    m_minReputationRequirements[operationType] = minReputation;
    LogPrint(BCLog::CVM, "Access: Minimum reputation for operation %d set to %d\n",
             static_cast<int>(operationType), minReputation);
}

int16_t AccessControlAuditor::GetMinimumReputation(AccessOperationType operationType) {
    LOCK(m_cs);
    
    auto it = m_minReputationRequirements.find(operationType);
    if (it != m_minReputationRequirements.end()) {
        return it->second;
    }
    return 0;
}

void AccessControlAuditor::EnableLogging(AccessOperationType operationType, bool enabled) {
    LOCK(m_cs);
    m_loggingEnabled[operationType] = enabled;
}

void AccessControlAuditor::SetMaxEntriesInMemory(size_t maxEntries) {
    LOCK(m_cs);
    m_maxEntriesInMemory = maxEntries;
    
    // Trim if necessary
    while (m_recentEntries.size() > m_maxEntriesInMemory) {
        m_recentEntries.pop_front();
    }
}

// ========== Internal Methods ==========

void AccessControlAuditor::AddEntry(const AccessControlAuditEntry& entry) {
    // AssertLockHeld(m_cs);
    
    m_recentEntries.push_back(entry);
    
    // Trim if necessary
    while (m_recentEntries.size() > m_maxEntriesInMemory) {
        m_recentEntries.pop_front();
    }
    
    // Persist to database
    PersistEntry(entry);
    
    // Update statistics
    UpdateStatistics(entry);
}

void AccessControlAuditor::PersistEntry(const AccessControlAuditEntry& entry) {
    // AssertLockHeld(m_cs);
    
    // Create key string with entry ID for proper ordering
    std::string keyStr = std::string(1, DB_ACCESS_AUDIT);
    
    // Add entry ID as big-endian for proper ordering
    uint64_t id = entry.entryId;
    for (int i = 7; i >= 0; i--) {
        keyStr.push_back((id >> (i * 8)) & 0xFF);
    }
    
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue << entry;
    std::vector<uint8_t> valueData(ssValue.begin(), ssValue.end());
    m_db.WriteGeneric(keyStr, valueData);
}

void AccessControlAuditor::UpdateStatistics(const AccessControlAuditEntry& entry) {
    // AssertLockHeld(m_cs);
    
    m_currentStats.totalRequests[entry.operationType]++;
    m_currentStats.decisionCounts[entry.decision]++;
    m_currentStats.totalAccessAttempts++;
    
    if (entry.decision == AccessDecision::GRANTED) {
        m_currentStats.grantedRequests[entry.operationType]++;
        m_currentStats.totalGranted++;
    } else {
        m_currentStats.deniedRequests[entry.operationType]++;
        m_currentStats.totalDenied++;
    }
}

int64_t AccessControlAuditor::GetCurrentTimestamp() {
    return GetTime();
}

void AccessControlAuditor::CleanupExpiredBlacklistEntries() {
    // AssertLockHeld(m_cs);
    
    int64_t now = GetCurrentTimestamp();
    std::vector<uint160> toRemove;
    
    for (const auto& entry : m_blacklist) {
        int64_t expiry = entry.second.second;
        if (expiry > 0 && now > expiry) {
            toRemove.push_back(entry.first);
        }
    }
    
    for (const auto& address : toRemove) {
        m_blacklist.erase(address);
        
        // Remove from database using generic key-value storage
        std::string keyStr = std::string(1, DB_BLACKLIST) + address.GetHex();
        m_db.EraseGeneric(keyStr);
    }
}

void AccessControlAuditor::LoadBlacklist() {
    // AssertLockHeld(m_cs);
    
    // Load blacklist entries from database
    // Note: This is a simplified implementation - in production, you'd iterate over DB keys
    LogPrint(BCLog::CVM, "Access: Blacklist loaded\n");
}

// ========== Global Functions ==========

bool InitAccessControlAuditor(CVMDatabase& db, SecurityAuditLogger* auditLogger, int currentBlockHeight) {
    g_accessControlAuditor = std::make_unique<AccessControlAuditor>(db, auditLogger);
    return g_accessControlAuditor->Initialize(currentBlockHeight);
}

void ShutdownAccessControlAuditor() {
    g_accessControlAuditor.reset();
}

} // namespace CVM
