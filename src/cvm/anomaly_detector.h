// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ANOMALY_DETECTOR_H
#define CASCOIN_CVM_ANOMALY_DETECTOR_H

#include <uint256.h>
#include <serialize.h>
#include <sync.h>
#include <cvm/hat_consensus.h>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <memory>

namespace CVM {

// Forward declarations
class CVMDatabase;
class SecurityAuditLogger;

/**
 * Anomaly Type
 */
enum class AnomalyType {
    REPUTATION_SPIKE,           // Sudden reputation increase
    REPUTATION_DROP,            // Sudden reputation decrease
    REPUTATION_OSCILLATION,     // Rapid back-and-forth changes
    VALIDATOR_SLOW_RESPONSE,    // Consistently slow responses
    VALIDATOR_ERRATIC_TIMING,   // Erratic response times
    VALIDATOR_BIAS,             // Biased voting pattern
    VOTE_MANIPULATION,          // Coordinated voting
    VOTE_EXTREME_BIAS,          // Always positive/negative votes
    TRUST_GRAPH_MANIPULATION,   // Artificial trust path creation
    SYBIL_CLUSTER,              // Sybil attack cluster detected
    COORDINATED_ATTACK          // Multiple anomalies suggesting attack
};

/**
 * Anomaly Alert
 * 
 * Generated when an anomaly is detected.
 */
struct AnomalyAlert {
    uint64_t alertId;
    AnomalyType type;
    uint160 primaryAddress;
    std::vector<uint160> relatedAddresses;
    double severity;                    // 0.0-1.0
    double confidence;                  // 0.0-1.0
    std::string description;
    std::vector<std::string> evidence;
    int64_t timestamp;
    int blockHeight;
    bool acknowledged;
    bool resolved;
    
    AnomalyAlert() : alertId(0), type(AnomalyType::REPUTATION_SPIKE),
                    severity(0), confidence(0), timestamp(0),
                    blockHeight(0), acknowledged(false), resolved(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(alertId);
        uint8_t typeVal = static_cast<uint8_t>(type);
        READWRITE(typeVal);
        type = static_cast<AnomalyType>(typeVal);
        READWRITE(primaryAddress);
        READWRITE(relatedAddresses);
        READWRITE(severity);
        READWRITE(confidence);
        READWRITE(description);
        READWRITE(evidence);
        READWRITE(timestamp);
        READWRITE(blockHeight);
        READWRITE(acknowledged);
        READWRITE(resolved);
    }
};

/**
 * Reputation History Entry
 */
struct ReputationHistoryEntry {
    int16_t score;
    int64_t timestamp;
    int blockHeight;
    std::string reason;
    
    ReputationHistoryEntry() : score(0), timestamp(0), blockHeight(0) {}
};

/**
 * Validator Behavior Profile
 */
struct ValidatorBehaviorProfile {
    uint160 validatorAddress;
    std::deque<double> responseTimes;       // Recent response times
    std::deque<int> voteHistory;            // Recent votes (1=accept, -1=reject, 0=abstain)
    std::deque<double> confidenceHistory;   // Recent confidence levels
    double averageResponseTime;
    double responseTimeStdDev;
    double acceptRate;
    double rejectRate;
    double abstainRate;
    int64_t lastActivityTime;
    
    ValidatorBehaviorProfile() : averageResponseTime(0), responseTimeStdDev(0),
                                acceptRate(0), rejectRate(0), abstainRate(0),
                                lastActivityTime(0) {}
    
    void UpdateStats();
};

/**
 * Anomaly Detector
 * 
 * Advanced anomaly detection for the CVM security system.
 * Implements requirements 10.3 and 10.4:
 * - Monitor for unusual reputation score changes
 * - Detect abnormal validator response patterns
 */
class AnomalyDetector {
public:
    explicit AnomalyDetector(CVMDatabase& db, SecurityAuditLogger* auditLogger = nullptr);
    ~AnomalyDetector();
    
    /**
     * Initialize the anomaly detector
     */
    bool Initialize(int currentBlockHeight);
    
    /**
     * Update current block height
     */
    void SetBlockHeight(int height) { m_currentBlockHeight = height; }
    
    // ========== Reputation Anomaly Detection ==========
    
    /**
     * Record a reputation score for an address
     */
    void RecordReputationScore(const uint160& address, int16_t score,
                              const std::string& reason);
    
    /**
     * Analyze reputation changes for anomalies
     */
    std::vector<AnomalyAlert> AnalyzeReputationChanges(const uint160& address);
    
    /**
     * Detect reputation spike anomaly
     */
    bool DetectReputationSpike(const uint160& address, int16_t newScore,
                              AnomalyAlert& alert);
    
    /**
     * Detect reputation drop anomaly
     */
    bool DetectReputationDrop(const uint160& address, int16_t newScore,
                             AnomalyAlert& alert);
    
    /**
     * Detect reputation oscillation (rapid changes)
     */
    bool DetectReputationOscillation(const uint160& address, AnomalyAlert& alert);
    
    // ========== Validator Anomaly Detection ==========
    
    /**
     * Record a validator response
     */
    void RecordValidatorResponse(const uint160& validator, const uint256& txHash,
                                ValidationVote vote, double confidence,
                                double responseTime);
    
    /**
     * Analyze validator behavior for anomalies
     */
    std::vector<AnomalyAlert> AnalyzeValidatorBehavior(const uint160& validator);
    
    /**
     * Detect slow response pattern
     */
    bool DetectSlowResponsePattern(const uint160& validator, AnomalyAlert& alert);
    
    /**
     * Detect erratic timing pattern
     */
    bool DetectErraticTimingPattern(const uint160& validator, AnomalyAlert& alert);
    
    /**
     * Detect voting bias
     */
    bool DetectVotingBias(const uint160& validator, AnomalyAlert& alert);
    
    // ========== Coordinated Attack Detection ==========
    
    /**
     * Detect coordinated voting patterns across validators
     */
    bool DetectCoordinatedVoting(const std::vector<ValidationResponse>& responses,
                                AnomalyAlert& alert);
    
    /**
     * Detect Sybil cluster based on behavior similarity
     */
    bool DetectSybilCluster(const std::vector<uint160>& addresses,
                           AnomalyAlert& alert);
    
    /**
     * Analyze transaction for coordinated attack
     */
    std::vector<AnomalyAlert> AnalyzeTransactionForAttack(const uint256& txHash,
                                                         const std::vector<ValidationResponse>& responses);
    
    // ========== Alert Management ==========
    
    /**
     * Get all active alerts
     */
    std::vector<AnomalyAlert> GetActiveAlerts();
    
    /**
     * Get alerts for an address
     */
    std::vector<AnomalyAlert> GetAlertsForAddress(const uint160& address);
    
    /**
     * Acknowledge an alert
     */
    bool AcknowledgeAlert(uint64_t alertId);
    
    /**
     * Resolve an alert
     */
    bool ResolveAlert(uint64_t alertId, const std::string& resolution);
    
    /**
     * Get alert by ID
     */
    AnomalyAlert GetAlert(uint64_t alertId);
    
    // ========== Configuration ==========
    
    /**
     * Set detection thresholds
     */
    void SetThresholds(double reputationZScore, double validatorZScore,
                      double coordinationThreshold);
    
    /**
     * Set history window size
     */
    void SetHistoryWindowSize(size_t reputationWindow, size_t validatorWindow);
    
    /**
     * Enable/disable specific detection types
     */
    void EnableDetection(AnomalyType type, bool enabled);
    
private:
    CVMDatabase& m_db;
    SecurityAuditLogger* m_auditLogger;
    mutable CCriticalSection m_cs;
    
    // State
    int m_currentBlockHeight;
    std::atomic<uint64_t> m_nextAlertId;
    
    // History tracking
    std::map<uint160, std::deque<ReputationHistoryEntry>> m_reputationHistory;
    std::map<uint160, ValidatorBehaviorProfile> m_validatorProfiles;
    std::deque<AnomalyAlert> m_activeAlerts;
    
    // Configuration
    size_t m_reputationHistoryWindow;
    size_t m_validatorHistoryWindow;
    double m_reputationZScoreThreshold;
    double m_validatorZScoreThreshold;
    double m_coordinationThreshold;
    std::map<AnomalyType, bool> m_enabledDetections;
    
    // Internal methods
    void CreateAlert(const AnomalyAlert& alert);
    double CalculateMean(const std::deque<double>& values);
    double CalculateStdDev(const std::deque<double>& values, double mean);
    double CalculateZScore(double value, double mean, double stdDev);
    int64_t GetCurrentTimestamp();
    void PersistAlert(const AnomalyAlert& alert);
    void LoadActiveAlerts();
};

/**
 * Global anomaly detector instance
 */
extern std::unique_ptr<AnomalyDetector> g_anomalyDetector;

/**
 * Initialize anomaly detector
 */
bool InitAnomalyDetector(CVMDatabase& db, SecurityAuditLogger* auditLogger, int currentBlockHeight);

/**
 * Shutdown anomaly detector
 */
void ShutdownAnomalyDetector();

} // namespace CVM

#endif // CASCOIN_CVM_ANOMALY_DETECTOR_H
