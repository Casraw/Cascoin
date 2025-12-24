// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_SECURITY_AUDIT_H
#define CASCOIN_CVM_SECURITY_AUDIT_H

#include <uint256.h>
#include <serialize.h>
#include <sync.h>
#include <cvm/hat_consensus.h>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <memory>
#include <atomic>
#include <chrono>

namespace CVM {

// Forward declarations
class CVMDatabase;

/**
 * Security Event Types
 * 
 * Categorizes all security-relevant events in the CVM system.
 */
enum class SecurityEventType {
    // Reputation Events (24.1)
    REPUTATION_SCORE_CHANGE,        // Any reputation score change
    REPUTATION_VOTE_CAST,           // Reputation vote cast
    REPUTATION_PENALTY_APPLIED,     // Penalty applied to address
    REPUTATION_BONUS_APPLIED,       // Bonus applied to address
    
    // Validator Events (24.1)
    VALIDATOR_RESPONSE_RECEIVED,    // Validator response in HAT v2 consensus
    VALIDATOR_CHALLENGE_SENT,       // Validation challenge sent
    VALIDATOR_TIMEOUT,              // Validator failed to respond
    VALIDATOR_ACCURACY_UPDATE,      // Validator accuracy changed
    VALIDATOR_ELIGIBILITY_CHANGE,   // Validator eligibility status changed
    
    // Consensus Events
    CONSENSUS_REACHED,              // HAT v2 consensus reached
    CONSENSUS_FAILED,               // HAT v2 consensus failed
    DAO_DISPUTE_CREATED,            // Dispute escalated to DAO
    DAO_DISPUTE_RESOLVED,           // DAO resolved dispute
    
    // Fraud Events
    FRAUD_ATTEMPT_DETECTED,         // Fraud attempt detected
    FRAUD_RECORD_CREATED,           // Fraud record created on-chain
    SYBIL_ATTACK_DETECTED,          // Sybil attack detected
    
    // Access Control Events (24.4)
    TRUST_SCORE_QUERY,              // Trust score queried
    TRUST_SCORE_MODIFICATION,       // Trust score modified
    REPUTATION_GATED_ACCESS,        // Reputation-gated operation attempted
    REPUTATION_GATED_DENIED,        // Reputation-gated operation denied
    
    // Anomaly Events (24.2)
    ANOMALY_REPUTATION_SPIKE,       // Unusual reputation increase
    ANOMALY_REPUTATION_DROP,        // Unusual reputation decrease
    ANOMALY_VALIDATOR_PATTERN,      // Abnormal validator response pattern
    ANOMALY_VOTE_PATTERN,           // Abnormal voting pattern
    ANOMALY_TRUST_GRAPH,            // Trust graph manipulation detected
    
    // System Events
    SYSTEM_STARTUP,                 // Security audit system started
    SYSTEM_SHUTDOWN,                // Security audit system stopped
    CONFIG_CHANGE                   // Security configuration changed
};

/**
 * Security Event Severity
 */
enum class SecuritySeverity {
    DEBUG,      // Debug information
    INFO,       // Informational
    WARNING,    // Potential issue
    ERROR,      // Error condition
    CRITICAL    // Critical security event
};

/**
 * Security Event
 * 
 * Represents a single security-relevant event in the system.
 */
struct SecurityEvent {
    uint64_t eventId;               // Unique event ID
    SecurityEventType type;         // Event type
    SecuritySeverity severity;      // Event severity
    int64_t timestamp;              // Unix timestamp (milliseconds)
    int blockHeight;                // Block height when event occurred
    
    // Event context
    uint160 primaryAddress;         // Primary address involved
    uint160 secondaryAddress;       // Secondary address (if applicable)
    uint256 txHash;                 // Transaction hash (if applicable)
    
    // Event data
    std::string description;        // Human-readable description
    std::map<std::string, std::string> metadata;  // Additional key-value data
    
    // Numeric values for metrics
    double oldValue;                // Previous value (for changes)
    double newValue;                // New value (for changes)
    double delta;                   // Change amount
    
    SecurityEvent() : eventId(0), type(SecurityEventType::SYSTEM_STARTUP),
                     severity(SecuritySeverity::INFO), timestamp(0),
                     blockHeight(0), oldValue(0), newValue(0), delta(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(eventId);
        uint8_t typeVal = static_cast<uint8_t>(type);
        READWRITE(typeVal);
        type = static_cast<SecurityEventType>(typeVal);
        uint8_t sevVal = static_cast<uint8_t>(severity);
        READWRITE(sevVal);
        severity = static_cast<SecuritySeverity>(sevVal);
        READWRITE(timestamp);
        READWRITE(blockHeight);
        READWRITE(primaryAddress);
        READWRITE(secondaryAddress);
        READWRITE(txHash);
        READWRITE(description);
        READWRITE(metadata);
        READWRITE(oldValue);
        READWRITE(newValue);
        READWRITE(delta);
    }
    
    std::string GetTypeString() const;
    std::string GetSeverityString() const;
    std::string ToLogString() const;
};


/**
 * Reputation Change Record
 * 
 * Detailed record of a reputation score change.
 */
struct ReputationChangeRecord {
    uint160 address;                // Address whose reputation changed
    int16_t oldScore;               // Previous score
    int16_t newScore;               // New score
    int16_t delta;                  // Change amount
    std::string reason;             // Reason for change
    uint256 triggerTxHash;          // Transaction that triggered change
    int64_t timestamp;              // When change occurred
    int blockHeight;                // Block height
    
    // Component breakdown (HAT v2)
    double oldBehavior;
    double newBehavior;
    double oldWoT;
    double newWoT;
    double oldEconomic;
    double newEconomic;
    double oldTemporal;
    double newTemporal;
    
    ReputationChangeRecord() : oldScore(0), newScore(0), delta(0),
                               timestamp(0), blockHeight(0),
                               oldBehavior(0), newBehavior(0),
                               oldWoT(0), newWoT(0),
                               oldEconomic(0), newEconomic(0),
                               oldTemporal(0), newTemporal(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(oldScore);
        READWRITE(newScore);
        READWRITE(delta);
        READWRITE(reason);
        READWRITE(triggerTxHash);
        READWRITE(timestamp);
        READWRITE(blockHeight);
        READWRITE(oldBehavior);
        READWRITE(newBehavior);
        READWRITE(oldWoT);
        READWRITE(newWoT);
        READWRITE(oldEconomic);
        READWRITE(newEconomic);
        READWRITE(oldTemporal);
        READWRITE(newTemporal);
    }
};

/**
 * Validator Response Record
 * 
 * Record of a validator's response in HAT v2 consensus.
 */
struct ValidatorResponseRecord {
    uint256 txHash;                 // Transaction being validated
    uint160 validatorAddress;       // Validator address
    ValidationVote vote;            // Vote cast
    double confidence;              // Vote confidence
    bool hasWoTConnection;          // WoT connectivity
    int16_t calculatedScore;        // Score calculated by validator
    int16_t reportedScore;          // Self-reported score
    int16_t scoreDifference;        // Difference
    int64_t responseTime;           // Response time (ms)
    int64_t timestamp;              // When response received
    int blockHeight;                // Block height
    
    ValidatorResponseRecord() : vote(ValidationVote::ABSTAIN), confidence(0),
                               hasWoTConnection(false), calculatedScore(0),
                               reportedScore(0), scoreDifference(0),
                               responseTime(0), timestamp(0), blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(validatorAddress);
        uint8_t voteVal = static_cast<uint8_t>(vote);
        READWRITE(voteVal);
        vote = static_cast<ValidationVote>(voteVal);
        READWRITE(confidence);
        READWRITE(hasWoTConnection);
        READWRITE(calculatedScore);
        READWRITE(reportedScore);
        READWRITE(scoreDifference);
        READWRITE(responseTime);
        READWRITE(timestamp);
        READWRITE(blockHeight);
    }
};

/**
 * Access Control Record
 * 
 * Record of trust score queries and reputation-gated operations.
 */
struct AccessControlRecord {
    uint160 requesterAddress;       // Who requested access
    uint160 targetAddress;          // Target of query/operation
    std::string operation;          // Operation attempted
    int16_t requiredReputation;     // Required reputation level
    int16_t actualReputation;       // Actual reputation level
    bool accessGranted;             // Was access granted?
    std::string denialReason;       // Reason for denial (if denied)
    int64_t timestamp;              // When access attempted
    int blockHeight;                // Block height
    
    AccessControlRecord() : requiredReputation(0), actualReputation(0),
                           accessGranted(false), timestamp(0), blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(requesterAddress);
        READWRITE(targetAddress);
        READWRITE(operation);
        READWRITE(requiredReputation);
        READWRITE(actualReputation);
        READWRITE(accessGranted);
        READWRITE(denialReason);
        READWRITE(timestamp);
        READWRITE(blockHeight);
    }
};

/**
 * Anomaly Detection Result
 * 
 * Result of anomaly detection analysis.
 */
struct AnomalyDetectionResult {
    uint160 address;                // Address analyzed
    std::string anomalyType;        // Type of anomaly
    double anomalyScore;            // Anomaly score (0.0-1.0)
    double threshold;               // Detection threshold
    bool isAnomaly;                 // Is this an anomaly?
    std::string description;        // Description of anomaly
    std::vector<std::string> indicators;  // Indicators that triggered detection
    int64_t timestamp;              // When detected
    int blockHeight;                // Block height
    
    AnomalyDetectionResult() : anomalyScore(0), threshold(0),
                               isAnomaly(false), timestamp(0), blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(anomalyType);
        READWRITE(anomalyScore);
        READWRITE(threshold);
        READWRITE(isAnomaly);
        READWRITE(description);
        READWRITE(indicators);
        READWRITE(timestamp);
        READWRITE(blockHeight);
    }
};


/**
 * Security Metrics
 * 
 * Aggregated security metrics for dashboard display.
 */
struct SecurityMetrics {
    // Consensus metrics
    uint64_t totalValidations;          // Total validations performed
    uint64_t successfulValidations;     // Successful validations
    uint64_t failedValidations;         // Failed validations
    double validationSuccessRate;       // Success rate
    
    // Validator metrics
    uint64_t activeValidators;          // Currently active validators
    uint64_t totalValidatorResponses;   // Total responses received
    double averageResponseTime;         // Average response time (ms)
    double averageValidatorAccuracy;    // Average validator accuracy
    
    // Reputation metrics
    uint64_t reputationChanges;         // Total reputation changes
    uint64_t reputationPenalties;       // Penalties applied
    uint64_t reputationBonuses;         // Bonuses applied
    double averageReputationChange;     // Average change magnitude
    
    // Fraud metrics
    uint64_t fraudAttemptsDetected;     // Fraud attempts detected
    uint64_t fraudRecordsCreated;       // Fraud records created
    uint64_t sybilAttacksDetected;      // Sybil attacks detected
    
    // Anomaly metrics
    uint64_t anomaliesDetected;         // Total anomalies detected
    uint64_t reputationAnomalies;       // Reputation anomalies
    uint64_t validatorAnomalies;        // Validator anomalies
    uint64_t trustGraphAnomalies;       // Trust graph anomalies
    
    // Access control metrics
    uint64_t accessAttempts;            // Total access attempts
    uint64_t accessGranted;             // Access granted
    uint64_t accessDenied;              // Access denied
    double accessDenialRate;            // Denial rate
    
    // Time window
    int64_t windowStart;                // Metrics window start
    int64_t windowEnd;                  // Metrics window end
    int startBlockHeight;               // Start block height
    int endBlockHeight;                 // End block height
    
    SecurityMetrics() : totalValidations(0), successfulValidations(0),
                       failedValidations(0), validationSuccessRate(0),
                       activeValidators(0), totalValidatorResponses(0),
                       averageResponseTime(0), averageValidatorAccuracy(0),
                       reputationChanges(0), reputationPenalties(0),
                       reputationBonuses(0), averageReputationChange(0),
                       fraudAttemptsDetected(0), fraudRecordsCreated(0),
                       sybilAttacksDetected(0), anomaliesDetected(0),
                       reputationAnomalies(0), validatorAnomalies(0),
                       trustGraphAnomalies(0), accessAttempts(0),
                       accessGranted(0), accessDenied(0), accessDenialRate(0),
                       windowStart(0), windowEnd(0), startBlockHeight(0),
                       endBlockHeight(0) {}
    
    void CalculateRates() {
        if (totalValidations > 0) {
            validationSuccessRate = (double)successfulValidations / totalValidations;
        }
        if (accessAttempts > 0) {
            accessDenialRate = (double)accessDenied / accessAttempts;
        }
    }
};

/**
 * Security Audit Logger
 * 
 * Main class for security monitoring and audit logging.
 * Implements requirements 10.3 and 10.4:
 * - Log all reputation score changes
 * - Record all validator responses in HAT v2 consensus
 * - Monitor for unusual reputation score changes
 * - Detect abnormal validator response patterns
 * - Track consensus validation success/failure rates
 * - Monitor validator participation and response times
 * - Log all trust score queries and modifications
 * - Record all reputation-gated operation attempts
 */
class SecurityAuditLogger {
public:
    explicit SecurityAuditLogger(CVMDatabase& db);
    ~SecurityAuditLogger();
    
    /**
     * Initialize the security audit system
     */
    bool Initialize(int currentBlockHeight);
    
    /**
     * Shutdown the security audit system
     */
    void Shutdown();
    
    // ========== Reputation Event Logging (24.1) ==========
    
    /**
     * Log a reputation score change
     */
    void LogReputationChange(const ReputationChangeRecord& record);
    
    /**
     * Log a reputation vote cast
     */
    void LogReputationVote(const uint160& voter, const uint160& target,
                          int16_t voteValue, const uint256& txHash);
    
    /**
     * Log a reputation penalty
     */
    void LogReputationPenalty(const uint160& address, int16_t penalty,
                             const std::string& reason, const uint256& txHash);
    
    /**
     * Log a reputation bonus
     */
    void LogReputationBonus(const uint160& address, int16_t bonus,
                           const std::string& reason, const uint256& txHash);
    
    // ========== Validator Response Logging (24.1) ==========
    
    /**
     * Log a validator response in HAT v2 consensus
     */
    void LogValidatorResponse(const ValidatorResponseRecord& record);
    
    /**
     * Log a validation challenge sent
     */
    void LogValidationChallenge(const uint256& txHash,
                               const std::vector<uint160>& validators);
    
    /**
     * Log a validator timeout
     */
    void LogValidatorTimeout(const uint256& txHash, const uint160& validator);
    
    /**
     * Log validator accuracy update
     */
    void LogValidatorAccuracyUpdate(const uint160& validator,
                                   double oldAccuracy, double newAccuracy);
    
    /**
     * Log validator eligibility change
     */
    void LogValidatorEligibilityChange(const uint160& validator,
                                      bool wasEligible, bool isEligible,
                                      const std::string& reason);
    
    // ========== Consensus Event Logging ==========
    
    /**
     * Log consensus reached
     */
    void LogConsensusReached(const uint256& txHash, bool approved,
                            int acceptVotes, int rejectVotes, int abstainVotes);
    
    /**
     * Log consensus failed
     */
    void LogConsensusFailed(const uint256& txHash, const std::string& reason);
    
    /**
     * Log DAO dispute created
     */
    void LogDAODisputeCreated(const uint256& disputeId, const uint256& txHash,
                             const uint160& address);
    
    /**
     * Log DAO dispute resolved
     */
    void LogDAODisputeResolved(const uint256& disputeId, bool approved,
                              const std::string& resolution);
    
    // ========== Fraud Event Logging ==========
    
    /**
     * Log fraud attempt detected
     */
    void LogFraudAttempt(const uint160& address, const uint256& txHash,
                        int16_t claimedScore, int16_t actualScore);
    
    /**
     * Log fraud record created
     */
    void LogFraudRecordCreated(const FraudRecord& record);
    
    /**
     * Log Sybil attack detected
     */
    void LogSybilAttackDetected(const std::vector<uint160>& addresses,
                               double riskScore, const std::string& reason);
    
    // ========== Access Control Logging (24.4) ==========
    
    /**
     * Log trust score query
     */
    void LogTrustScoreQuery(const uint160& requester, const uint160& target,
                           int16_t score);
    
    /**
     * Log trust score modification
     */
    void LogTrustScoreModification(const uint160& modifier, const uint160& target,
                                  int16_t oldScore, int16_t newScore,
                                  const std::string& reason);
    
    /**
     * Log reputation-gated operation attempt
     */
    void LogReputationGatedAccess(const AccessControlRecord& record);
    
    // ========== Anomaly Detection (24.2) ==========
    
    /**
     * Detect reputation anomalies for an address
     */
    AnomalyDetectionResult DetectReputationAnomaly(const uint160& address,
                                                   int16_t newScore);
    
    /**
     * Detect validator response anomalies
     */
    AnomalyDetectionResult DetectValidatorAnomaly(const uint160& validator,
                                                  const ValidationResponse& response);
    
    /**
     * Detect voting pattern anomalies
     */
    AnomalyDetectionResult DetectVotingAnomaly(const uint160& voter);
    
    /**
     * Log detected anomaly
     */
    void LogAnomaly(const AnomalyDetectionResult& result);
    
    // ========== Security Metrics (24.3) ==========
    
    /**
     * Get current security metrics
     */
    SecurityMetrics GetCurrentMetrics();
    
    /**
     * Get metrics for a specific time window
     */
    SecurityMetrics GetMetricsForWindow(int64_t startTime, int64_t endTime);
    
    /**
     * Get metrics for a specific block range
     */
    SecurityMetrics GetMetricsForBlockRange(int startBlock, int endBlock);
    
    /**
     * Get recent events
     */
    std::vector<SecurityEvent> GetRecentEvents(size_t count = 100);
    
    /**
     * Get events by type
     */
    std::vector<SecurityEvent> GetEventsByType(SecurityEventType type,
                                               size_t count = 100);
    
    /**
     * Get events for address
     */
    std::vector<SecurityEvent> GetEventsForAddress(const uint160& address,
                                                   size_t count = 100);
    
    // ========== Configuration ==========
    
    /**
     * Set anomaly detection thresholds
     */
    void SetAnomalyThresholds(double reputationThreshold,
                             double validatorThreshold,
                             double votingThreshold);
    
    /**
     * Set logging level
     */
    void SetLoggingLevel(SecuritySeverity minSeverity);
    
    /**
     * Enable/disable file logging
     */
    void SetFileLogging(bool enabled, const std::string& logPath = "");
    
    /**
     * Set maximum events to keep in memory
     */
    void SetMaxEventsInMemory(size_t maxEvents);
    
    /**
     * Update current block height
     */
    void SetBlockHeight(int height) { m_currentBlockHeight = height; }
    
private:
    CVMDatabase& m_db;
    mutable CCriticalSection m_cs;
    
    // Event storage
    std::deque<SecurityEvent> m_recentEvents;
    size_t m_maxEventsInMemory;
    std::atomic<uint64_t> m_nextEventId;
    
    // Metrics tracking
    SecurityMetrics m_currentMetrics;
    int64_t m_metricsWindowStart;
    
    // Anomaly detection state
    std::map<uint160, std::vector<int16_t>> m_reputationHistory;
    std::map<uint160, std::vector<double>> m_validatorResponseTimes;
    std::map<uint160, std::vector<int>> m_votingPatterns;
    
    // Thresholds
    double m_reputationAnomalyThreshold;
    double m_validatorAnomalyThreshold;
    double m_votingAnomalyThreshold;
    
    // Configuration
    SecuritySeverity m_minLoggingSeverity;
    bool m_fileLoggingEnabled;
    std::string m_logFilePath;
    int m_currentBlockHeight;
    
    // Internal methods
    void AddEvent(const SecurityEvent& event);
    void PersistEvent(const SecurityEvent& event);
    void UpdateMetrics(const SecurityEvent& event);
    double CalculateStandardDeviation(const std::vector<int16_t>& values);
    double CalculateStandardDeviation(const std::vector<double>& values);
    int64_t GetCurrentTimestamp();
    void WriteToLogFile(const SecurityEvent& event);
};

/**
 * Global security audit logger instance
 */
extern std::unique_ptr<SecurityAuditLogger> g_securityAudit;

/**
 * Initialize security audit system
 */
bool InitSecurityAudit(CVMDatabase& db, int currentBlockHeight);

/**
 * Shutdown security audit system
 */
void ShutdownSecurityAudit();

} // namespace CVM

#endif // CASCOIN_CVM_SECURITY_AUDIT_H
