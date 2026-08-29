// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_SECURITY_MONITOR_H
#define CASCOIN_L2_SECURITY_MONITOR_H

/**
 * @file security_monitor.h
 * @brief Security Monitoring System for Cascoin L2
 * 
 * This file implements comprehensive security monitoring including:
 * - Anomaly detection for transactions and sequencer behavior
 * - Alert system for security events
 * - Audit logging for forensic analysis
 * - Circuit breaker for automatic pause on anomalies
 * 
 * Requirements: 33.1, 33.2, 33.5, 33.6, 36.6
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <amount.h>
#include <sync.h>
#include <hash.h>
#include <serialize.h>
#include <streams.h>

#include <cstdint>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <functional>
#include <chrono>
#include <optional>

namespace l2 {

// Forward declarations
class BridgeContract;
class SequencerDiscovery;

// ============================================================================
// Constants
// ============================================================================

/** Default audit log retention period: 90 days in seconds */
static constexpr uint64_t AUDIT_LOG_RETENTION_SECONDS = 90 * 24 * 60 * 60;

/** Maximum audit log entries to keep in memory */
static constexpr size_t MAX_AUDIT_LOG_ENTRIES = 100000;

/** Default anomaly detection window: 1 hour in seconds */
static constexpr uint64_t ANOMALY_DETECTION_WINDOW = 60 * 60;

/** Transaction volume spike threshold (multiplier of average) */
static constexpr double TX_VOLUME_SPIKE_THRESHOLD = 5.0;

/** Transaction value spike threshold (multiplier of average) */
static constexpr double TX_VALUE_SPIKE_THRESHOLD = 10.0;

/** Reputation drop threshold for alerts (percentage points) */
static constexpr uint32_t REPUTATION_DROP_THRESHOLD = 20;

/** Bridge balance discrepancy threshold (percentage) */
static constexpr double BRIDGE_BALANCE_DISCREPANCY_THRESHOLD = 0.01;

/** Circuit breaker TVL withdrawal threshold (10% of TVL) */
static constexpr double CIRCUIT_BREAKER_TVL_THRESHOLD = 0.10;

/** Circuit breaker cooldown period: 1 hour */
static constexpr uint64_t CIRCUIT_BREAKER_COOLDOWN = 60 * 60;

// ============================================================================
// Enums
// ============================================================================

/**
 * @brief Types of security alerts
 */
enum class AlertType : uint8_t {
    INFO = 0,               // Informational
    WARNING = 1,            // Warning - potential issue
    CRITICAL = 2,           // Critical - immediate attention needed
    EMERGENCY = 3           // Emergency - system may be compromised
};

/**
 * @brief Categories of security events
 */
enum class SecurityEventCategory : uint8_t {
    TRANSACTION_ANOMALY = 0,    // Unusual transaction patterns
    SEQUENCER_BEHAVIOR = 1,     // Suspicious sequencer actions
    BRIDGE_DISCREPANCY = 2,     // Bridge balance issues
    REPUTATION_CHANGE = 3,      // Significant reputation changes
    SYSTEM_ERROR = 4,           // System-level errors
    CIRCUIT_BREAKER = 5,        // Circuit breaker events
    COLLUSION_DETECTED = 6,     // Collusion detection alerts
    FRAUD_PROOF = 7             // Fraud proof submissions
};

/**
 * @brief Circuit breaker states
 */
enum class CircuitBreakerState : uint8_t {
    NORMAL = 0,             // Normal operation
    WARNING = 1,            // Elevated risk, monitoring closely
    TRIGGERED = 2,          // Circuit breaker active, operations paused
    RECOVERY = 3            // Recovering from triggered state
};

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Security alert structure
 */
struct SecurityAlert {
    uint256 alertId;
    AlertType type;
    SecurityEventCategory category;
    std::string message;
    std::string details;
    uint64_t timestamp;
    std::vector<uint160> involvedAddresses;
    std::vector<uint256> relatedTxHashes;
    bool acknowledged;
    bool resolved;
    
    SecurityAlert()
        : type(AlertType::INFO)
        , category(SecurityEventCategory::SYSTEM_ERROR)
        , timestamp(0)
        , acknowledged(false)
        , resolved(false)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(alertId);
        if (ser_action.ForRead()) {
            uint8_t typeVal, catVal;
            READWRITE(typeVal);
            READWRITE(catVal);
            type = static_cast<AlertType>(typeVal);
            category = static_cast<SecurityEventCategory>(catVal);
        } else {
            uint8_t typeVal = static_cast<uint8_t>(type);
            uint8_t catVal = static_cast<uint8_t>(category);
            READWRITE(typeVal);
            READWRITE(catVal);
        }
        READWRITE(message);
        READWRITE(details);
        READWRITE(timestamp);
        READWRITE(involvedAddresses);
        READWRITE(relatedTxHashes);
        READWRITE(acknowledged);
        READWRITE(resolved);
    }
    
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << static_cast<uint8_t>(type) << static_cast<uint8_t>(category);
        ss << message << timestamp;
        return ss.GetHash();
    }
};

/**
 * @brief Audit log entry for forensic analysis
 */
struct AuditLogEntry {
    uint256 entryId;
    uint64_t timestamp;
    SecurityEventCategory category;
    std::string action;
    std::string actor;              // Address or system component
    std::string target;             // Target of the action
    std::string details;
    std::map<std::string, std::string> metadata;
    uint256 relatedTxHash;
    bool success;
    
    AuditLogEntry()
        : timestamp(0)
        , category(SecurityEventCategory::SYSTEM_ERROR)
        , success(true)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(entryId);
        READWRITE(timestamp);
        if (ser_action.ForRead()) {
            uint8_t catVal;
            READWRITE(catVal);
            category = static_cast<SecurityEventCategory>(catVal);
        } else {
            uint8_t catVal = static_cast<uint8_t>(category);
            READWRITE(catVal);
        }
        READWRITE(action);
        READWRITE(actor);
        READWRITE(target);
        READWRITE(details);
        READWRITE(metadata);
        READWRITE(relatedTxHash);
        READWRITE(success);
    }
    
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << timestamp << static_cast<uint8_t>(category);
        ss << action << actor << target;
        return ss.GetHash();
    }
};

/**
 * @brief Transaction statistics for anomaly detection
 */
struct TransactionStats {
    uint64_t windowStart;           // Start of measurement window
    uint64_t windowEnd;             // End of measurement window
    uint64_t transactionCount;      // Number of transactions
    CAmount totalValue;             // Total value transferred
    CAmount avgValue;               // Average transaction value
    CAmount maxValue;               // Maximum single transaction value
    uint64_t uniqueSenders;         // Unique sender addresses
    uint64_t uniqueReceivers;       // Unique receiver addresses
    
    TransactionStats()
        : windowStart(0)
        , windowEnd(0)
        , transactionCount(0)
        , totalValue(0)
        , avgValue(0)
        , maxValue(0)
        , uniqueSenders(0)
        , uniqueReceivers(0)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(windowStart);
        READWRITE(windowEnd);
        READWRITE(transactionCount);
        READWRITE(totalValue);
        READWRITE(avgValue);
        READWRITE(maxValue);
        READWRITE(uniqueSenders);
        READWRITE(uniqueReceivers);
    }
};

/**
 * @brief Sequencer behavior metrics
 */
struct SequencerMetrics {
    uint160 sequencerAddress;
    uint64_t blocksProposed;
    uint64_t blocksMissed;
    uint64_t votesAccept;
    uint64_t votesReject;
    uint64_t votesAbstain;
    uint64_t lastActivityTimestamp;
    double uptimePercent;
    uint32_t reputationScore;
    uint32_t previousReputationScore;
    
    SequencerMetrics()
        : blocksProposed(0)
        , blocksMissed(0)
        , votesAccept(0)
        , votesReject(0)
        , votesAbstain(0)
        , lastActivityTimestamp(0)
        , uptimePercent(100.0)
        , reputationScore(0)
        , previousReputationScore(0)
    {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(blocksProposed);
        READWRITE(blocksMissed);
        READWRITE(votesAccept);
        READWRITE(votesReject);
        READWRITE(votesAbstain);
        READWRITE(lastActivityTimestamp);
        if (ser_action.ForRead()) {
            uint64_t uptimeInt;
            READWRITE(uptimeInt);
            uptimePercent = static_cast<double>(uptimeInt) / 10000.0;
        } else {
            uint64_t uptimeInt = static_cast<uint64_t>(uptimePercent * 10000);
            READWRITE(uptimeInt);
        }
        READWRITE(reputationScore);
        READWRITE(previousReputationScore);
    }
};

/**
 * @brief Circuit breaker status
 */
struct CircuitBreakerStatus {
    CircuitBreakerState state;
    uint64_t triggeredAt;
    uint64_t lastStateChange;
    std::string triggerReason;
    CAmount tvlAtTrigger;
    CAmount withdrawalVolumeAtTrigger;
    uint64_t cooldownEndsAt;
    
    CircuitBreakerStatus()
        : state(CircuitBreakerState::NORMAL)
        , triggeredAt(0)
        , lastStateChange(0)
        , tvlAtTrigger(0)
        , withdrawalVolumeAtTrigger(0)
        , cooldownEndsAt(0)
    {}
    
    bool IsTriggered() const {
        return state == CircuitBreakerState::TRIGGERED;
    }
    
    bool IsInCooldown(uint64_t currentTime) const {
        return currentTime < cooldownEndsAt;
    }
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            uint8_t stateVal;
            READWRITE(stateVal);
            state = static_cast<CircuitBreakerState>(stateVal);
        } else {
            uint8_t stateVal = static_cast<uint8_t>(state);
            READWRITE(stateVal);
        }
        READWRITE(triggeredAt);
        READWRITE(lastStateChange);
        READWRITE(triggerReason);
        READWRITE(tvlAtTrigger);
        READWRITE(withdrawalVolumeAtTrigger);
        READWRITE(cooldownEndsAt);
    }
};

/**
 * @brief Security dashboard metrics
 */
struct SecurityDashboardMetrics {
    uint64_t timestamp;
    uint64_t activeAlerts;
    uint64_t criticalAlerts;
    uint64_t totalAuditEntries;
    CircuitBreakerState circuitBreakerState;
    CAmount totalValueLocked;
    CAmount dailyWithdrawalVolume;
    double withdrawalToTvlRatio;
    uint64_t activeSequencers;
    double avgSequencerUptime;
    uint64_t anomaliesDetected24h;
    
    SecurityDashboardMetrics()
        : timestamp(0)
        , activeAlerts(0)
        , criticalAlerts(0)
        , totalAuditEntries(0)
        , circuitBreakerState(CircuitBreakerState::NORMAL)
        , totalValueLocked(0)
        , dailyWithdrawalVolume(0)
        , withdrawalToTvlRatio(0.0)
        , activeSequencers(0)
        , avgSequencerUptime(100.0)
        , anomaliesDetected24h(0)
    {}
};

// ============================================================================
// Security Monitor Class
// ============================================================================

/**
 * @brief Security Monitoring System for L2
 * 
 * Provides comprehensive security monitoring including:
 * - Real-time anomaly detection
 * - Alert generation and management
 * - Audit logging for forensic analysis
 * - Circuit breaker for automatic pause
 * 
 * Requirements: 33.1, 33.2, 33.5, 33.6, 36.6
 */
class SecurityMonitor {
public:
    /** Callback for security alerts */
    using AlertCallback = std::function<void(const SecurityAlert&)>;
    
    /** Callback for circuit breaker state changes */
    using CircuitBreakerCallback = std::function<void(CircuitBreakerState, const std::string&)>;

    /**
     * @brief Construct a new Security Monitor
     * @param chainId The L2 chain ID
     */
    explicit SecurityMonitor(uint64_t chainId);

    // ========================================================================
    // Anomaly Detection (Requirements 33.1, 33.2)
    // ========================================================================

    /**
     * @brief Record a transaction for anomaly detection
     * @param txHash Transaction hash
     * @param sender Sender address
     * @param receiver Receiver address
     * @param value Transaction value
     * @param timestamp Transaction timestamp
     * 
     * Requirement 33.2: Detect anomalous transaction patterns
     */
    void RecordTransaction(
        const uint256& txHash,
        const uint160& sender,
        const uint160& receiver,
        CAmount value,
        uint64_t timestamp);

    /**
     * @brief Check for transaction volume anomalies
     * @param currentTime Current timestamp
     * @return true if anomaly detected
     * 
     * Requirement 33.2: Detect anomalous transaction patterns (volume)
     */
    bool DetectVolumeAnomaly(uint64_t currentTime);

    /**
     * @brief Check for transaction value anomalies
     * @param currentTime Current timestamp
     * @return true if anomaly detected
     * 
     * Requirement 33.2: Detect anomalous transaction patterns (value)
     */
    bool DetectValueAnomaly(uint64_t currentTime);

    /**
     * @brief Check for transaction frequency anomalies
     * @param address Address to check
     * @param currentTime Current timestamp
     * @return true if anomaly detected
     * 
     * Requirement 33.2: Detect anomalous transaction patterns (frequency)
     */
    bool DetectFrequencyAnomaly(const uint160& address, uint64_t currentTime);

    /**
     * @brief Get current transaction statistics
     * @param windowSeconds Time window in seconds
     * @param currentTime Current timestamp
     * @return Transaction statistics for the window
     */
    TransactionStats GetTransactionStats(uint64_t windowSeconds, uint64_t currentTime) const;

    // ========================================================================
    // Sequencer Monitoring (Requirement 33.1)
    // ========================================================================

    /**
     * @brief Record sequencer action for monitoring
     * @param sequencer Sequencer address
     * @param action Action type (block proposal, vote, etc.)
     * @param timestamp Action timestamp
     * @param success Whether action was successful
     * 
     * Requirement 33.1: Monitor all sequencer actions in real-time
     */
    void RecordSequencerAction(
        const uint160& sequencer,
        const std::string& action,
        uint64_t timestamp,
        bool success);

    /**
     * @brief Record sequencer block proposal
     * @param sequencer Sequencer address
     * @param blockHash Block hash
     * @param timestamp Timestamp
     * @param accepted Whether block was accepted
     */
    void RecordBlockProposal(
        const uint160& sequencer,
        const uint256& blockHash,
        uint64_t timestamp,
        bool accepted);

    /**
     * @brief Record sequencer missed block
     * @param sequencer Sequencer address
     * @param slotNumber Slot that was missed
     * @param timestamp Timestamp
     */
    void RecordMissedBlock(
        const uint160& sequencer,
        uint64_t slotNumber,
        uint64_t timestamp);

    /**
     * @brief Record sequencer vote
     * @param sequencer Sequencer address
     * @param blockHash Block being voted on
     * @param vote Vote type
     * @param timestamp Timestamp
     */
    void RecordSequencerVote(
        const uint160& sequencer,
        const uint256& blockHash,
        VoteType vote,
        uint64_t timestamp);

    /**
     * @brief Get metrics for a sequencer
     * @param sequencer Sequencer address
     * @return Sequencer metrics
     */
    SequencerMetrics GetSequencerMetrics(const uint160& sequencer) const;

    /**
     * @brief Get all sequencer metrics
     * @return Map of sequencer address to metrics
     */
    std::map<uint160, SequencerMetrics> GetAllSequencerMetrics() const;

    // ========================================================================
    // Bridge Monitoring (Requirement 33.3)
    // ========================================================================

    /**
     * @brief Record bridge balance for monitoring
     * @param expectedBalance Expected balance based on deposits/withdrawals
     * @param actualBalance Actual balance on L1
     * @param timestamp Timestamp
     * 
     * Requirement 33.3: Alert on bridge balance discrepancies
     */
    void RecordBridgeBalance(
        CAmount expectedBalance,
        CAmount actualBalance,
        uint64_t timestamp);

    /**
     * @brief Check for bridge balance discrepancy
     * @return true if discrepancy detected
     */
    bool HasBridgeDiscrepancy() const;

    /**
     * @brief Get bridge balance discrepancy amount
     * @return Discrepancy amount (positive = actual > expected)
     */
    CAmount GetBridgeDiscrepancy() const;

    // ========================================================================
    // Reputation Monitoring (Requirement 33.4)
    // ========================================================================

    /**
     * @brief Record reputation change
     * @param address Address whose reputation changed
     * @param oldScore Previous reputation score
     * @param newScore New reputation score
     * @param timestamp Timestamp
     * 
     * Requirement 33.4: Track reputation changes and flag sudden drops
     */
    void RecordReputationChange(
        const uint160& address,
        uint32_t oldScore,
        uint32_t newScore,
        uint64_t timestamp);

    /**
     * @brief Check if address had significant reputation drop
     * @param address Address to check
     * @return true if significant drop detected
     */
    bool HasSignificantReputationDrop(const uint160& address) const;

    // ========================================================================
    // Alert System (Requirement 33.5)
    // ========================================================================

    /**
     * @brief Create and emit a security alert
     * @param type Alert type
     * @param category Event category
     * @param message Alert message
     * @param details Additional details
     * @param involvedAddresses Addresses involved
     * @param relatedTxHashes Related transaction hashes
     * @return The created alert
     * 
     * Requirement 33.5: Provide automated incident response for critical alerts
     */
    SecurityAlert CreateAlert(
        AlertType type,
        SecurityEventCategory category,
        const std::string& message,
        const std::string& details = "",
        const std::vector<uint160>& involvedAddresses = {},
        const std::vector<uint256>& relatedTxHashes = {});

    /**
     * @brief Get all active (unresolved) alerts
     * @return Vector of active alerts
     */
    std::vector<SecurityAlert> GetActiveAlerts() const;

    /**
     * @brief Get alerts by type
     * @param type Alert type to filter by
     * @return Vector of matching alerts
     */
    std::vector<SecurityAlert> GetAlertsByType(AlertType type) const;

    /**
     * @brief Get alerts by category
     * @param category Category to filter by
     * @return Vector of matching alerts
     */
    std::vector<SecurityAlert> GetAlertsByCategory(SecurityEventCategory category) const;

    /**
     * @brief Acknowledge an alert
     * @param alertId Alert ID
     * @return true if alert was acknowledged
     */
    bool AcknowledgeAlert(const uint256& alertId);

    /**
     * @brief Resolve an alert
     * @param alertId Alert ID
     * @param resolution Resolution details
     * @return true if alert was resolved
     */
    bool ResolveAlert(const uint256& alertId, const std::string& resolution);

    /**
     * @brief Get alert count by type
     * @return Map of alert type to count
     */
    std::map<AlertType, uint64_t> GetAlertCounts() const;

    /**
     * @brief Register callback for new alerts
     * @param callback Function to call when alert is created
     */
    void RegisterAlertCallback(AlertCallback callback);

    // ========================================================================
    // Audit Logging (Requirement 33.6)
    // ========================================================================

    /**
     * @brief Log an audit entry
     * @param category Event category
     * @param action Action performed
     * @param actor Actor performing the action
     * @param target Target of the action
     * @param details Additional details
     * @param metadata Key-value metadata
     * @param relatedTxHash Related transaction hash
     * @param success Whether action was successful
     * @return The created audit entry
     * 
     * Requirement 33.6: Maintain 90-day audit logs for forensic analysis
     */
    AuditLogEntry LogAudit(
        SecurityEventCategory category,
        const std::string& action,
        const std::string& actor,
        const std::string& target,
        const std::string& details = "",
        const std::map<std::string, std::string>& metadata = {},
        const uint256& relatedTxHash = uint256(),
        bool success = true);

    /**
     * @brief Get audit log entries for time range
     * @param startTime Start timestamp
     * @param endTime End timestamp
     * @return Vector of audit entries in range
     */
    std::vector<AuditLogEntry> GetAuditLog(uint64_t startTime, uint64_t endTime) const;

    /**
     * @brief Get audit log entries by category
     * @param category Category to filter by
     * @param limit Maximum entries to return
     * @return Vector of matching entries
     */
    std::vector<AuditLogEntry> GetAuditLogByCategory(
        SecurityEventCategory category,
        size_t limit = 100) const;

    /**
     * @brief Get audit log entries by actor
     * @param actor Actor to filter by
     * @param limit Maximum entries to return
     * @return Vector of matching entries
     */
    std::vector<AuditLogEntry> GetAuditLogByActor(
        const std::string& actor,
        size_t limit = 100) const;

    /**
     * @brief Get total audit log entry count
     * @return Number of entries
     */
    size_t GetAuditLogCount() const;

    /**
     * @brief Prune old audit log entries
     * @param currentTime Current timestamp
     * @param retentionSeconds Retention period in seconds
     * @return Number of entries pruned
     */
    size_t PruneAuditLog(uint64_t currentTime, uint64_t retentionSeconds = AUDIT_LOG_RETENTION_SECONDS);

    // ========================================================================
    // Circuit Breaker (Requirements 33.5, 36.6)
    // ========================================================================

    /**
     * @brief Record withdrawal for TVL monitoring
     * @param amount Withdrawal amount
     * @param timestamp Timestamp
     * 
     * Requirement 36.6: Implement circuit breaker if daily withdrawal > 10% TVL
     */
    void RecordWithdrawal(CAmount amount, uint64_t timestamp);

    /**
     * @brief Update total value locked
     * @param tvl Current TVL
     * @param timestamp Timestamp
     */
    void UpdateTVL(CAmount tvl, uint64_t timestamp);

    /**
     * @brief Check if circuit breaker should trigger
     * @param currentTime Current timestamp
     * @return true if circuit breaker should trigger
     * 
     * Requirement 36.6: Circuit breaker if daily withdrawal > 10% TVL
     */
    bool ShouldTriggerCircuitBreaker(uint64_t currentTime);

    /**
     * @brief Trigger the circuit breaker
     * @param reason Reason for triggering
     * @param currentTime Current timestamp
     * 
     * Requirement 33.5: Automated incident response
     */
    void TriggerCircuitBreaker(const std::string& reason, uint64_t currentTime);

    /**
     * @brief Reset circuit breaker (manual recovery)
     * @param currentTime Current timestamp
     * @return true if reset was successful
     */
    bool ResetCircuitBreaker(uint64_t currentTime);

    /**
     * @brief Get circuit breaker status
     * @return Current circuit breaker status
     */
    CircuitBreakerStatus GetCircuitBreakerStatus() const;

    /**
     * @brief Check if circuit breaker is triggered
     * @return true if triggered
     */
    bool IsCircuitBreakerTriggered() const;

    /**
     * @brief Register callback for circuit breaker state changes
     * @param callback Function to call on state change
     */
    void RegisterCircuitBreakerCallback(CircuitBreakerCallback callback);

    /**
     * @brief Get daily withdrawal volume
     * @param currentTime Current timestamp
     * @return Total withdrawals in last 24 hours
     */
    CAmount GetDailyWithdrawalVolume(uint64_t currentTime) const;

    // ========================================================================
    // Dashboard and Metrics (Requirement 33.8)
    // ========================================================================

    /**
     * @brief Get security dashboard metrics
     * @param currentTime Current timestamp
     * @return Dashboard metrics
     * 
     * Requirement 33.8: Provide public security dashboard with key metrics
     */
    SecurityDashboardMetrics GetDashboardMetrics(uint64_t currentTime) const;

    /**
     * @brief Get anomalies detected in last 24 hours
     * @param currentTime Current timestamp
     * @return Number of anomalies
     */
    uint64_t GetAnomaliesDetected24h(uint64_t currentTime) const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set transaction volume spike threshold
     * @param threshold Multiplier of average (default 5.0)
     */
    void SetVolumeSpikeTreshold(double threshold);

    /**
     * @brief Set transaction value spike threshold
     * @param threshold Multiplier of average (default 10.0)
     */
    void SetValueSpikeThreshold(double threshold);

    /**
     * @brief Set circuit breaker TVL threshold
     * @param threshold Percentage of TVL (default 0.10)
     */
    void SetCircuitBreakerThreshold(double threshold);

    /**
     * @brief Get the L2 chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Clear all monitoring data (for testing)
     */
    void Clear();

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Security alerts */
    std::map<uint256, SecurityAlert> alerts_;

    /** Audit log entries */
    std::deque<AuditLogEntry> auditLog_;

    /** Transaction records for anomaly detection */
    std::deque<std::tuple<uint256, uint160, uint160, CAmount, uint64_t>> transactionRecords_;

    /** Sequencer metrics */
    std::map<uint160, SequencerMetrics> sequencerMetrics_;

    /** Reputation change history */
    std::map<uint160, std::vector<std::pair<uint32_t, uint64_t>>> reputationHistory_;

    /** Bridge balance records */
    std::deque<std::tuple<CAmount, CAmount, uint64_t>> bridgeBalanceRecords_;

    /** Withdrawal records for circuit breaker */
    std::deque<std::pair<CAmount, uint64_t>> withdrawalRecords_;

    /** Current TVL */
    CAmount currentTVL_;

    /** Circuit breaker status */
    CircuitBreakerStatus circuitBreakerStatus_;

    /** Alert callbacks */
    std::vector<AlertCallback> alertCallbacks_;

    /** Circuit breaker callbacks */
    std::vector<CircuitBreakerCallback> circuitBreakerCallbacks_;

    /** Configuration thresholds */
    double volumeSpikeThreshold_;
    double valueSpikeThreshold_;
    double circuitBreakerThreshold_;

    /** Anomaly detection counters */
    std::deque<std::pair<SecurityEventCategory, uint64_t>> anomalyRecords_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_monitor_;

    /** Next alert ID counter */
    uint64_t nextAlertId_;

    /** Next audit entry ID counter */
    uint64_t nextAuditId_;

    /**
     * @brief Generate unique alert ID
     */
    uint256 GenerateAlertId();

    /**
     * @brief Generate unique audit entry ID
     */
    uint256 GenerateAuditId();

    /**
     * @brief Notify alert callbacks
     */
    void NotifyAlertCallbacks(const SecurityAlert& alert);

    /**
     * @brief Notify circuit breaker callbacks
     */
    void NotifyCircuitBreakerCallbacks(CircuitBreakerState state, const std::string& reason);

    /**
     * @brief Prune old transaction records
     */
    void PruneTransactionRecords(uint64_t currentTime);

    /**
     * @brief Prune old withdrawal records
     */
    void PruneWithdrawalRecords(uint64_t currentTime);

    /**
     * @brief Record anomaly detection
     */
    void RecordAnomaly(SecurityEventCategory category, uint64_t timestamp);

    /**
     * @brief Calculate average transaction value
     */
    CAmount CalculateAverageTransactionValue(uint64_t windowSeconds, uint64_t currentTime) const;

    /**
     * @brief Calculate average transaction count
     */
    uint64_t CalculateAverageTransactionCount(uint64_t windowSeconds, uint64_t currentTime) const;
};

/**
 * @brief Global security monitor instance
 */
SecurityMonitor& GetSecurityMonitor();

/**
 * @brief Initialize the global security monitor
 * @param chainId The L2 chain ID
 */
void InitSecurityMonitor(uint64_t chainId);

/**
 * @brief Check if security monitor is initialized
 * @return true if initialized
 */
bool IsSecurityMonitorInitialized();

} // namespace l2

#endif // CASCOIN_L2_SECURITY_MONITOR_H
