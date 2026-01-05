// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/security_audit.h>
#include <cvm/access_control_audit.h>
#include <cvm/cvmdb.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>
#include <fstream>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace CVM {

// Global instance
std::unique_ptr<SecurityAuditLogger> g_securityAudit;

// Database keys for security audit
static const char DB_SECURITY_EVENT = 'E';      // Security event: 'E' + eventId -> SecurityEvent
static const char DB_REPUTATION_CHANGE = 'P';   // Reputation change: 'P' + address + timestamp -> ReputationChangeRecord
static const char DB_VALIDATOR_RESPONSE = 'W';  // Validator response: 'W' + txHash + validator -> ValidatorResponseRecord
static const char DB_ACCESS_CONTROL = 'A';      // Access control: 'A' + timestamp -> AccessControlRecord
static const char DB_ANOMALY = 'Y';             // Anomaly: 'Y' + timestamp -> AnomalyDetectionResult

// ========== SecurityEvent Implementation ==========

std::string SecurityEvent::GetTypeString() const {
    switch (type) {
        case SecurityEventType::REPUTATION_SCORE_CHANGE: return "REPUTATION_SCORE_CHANGE";
        case SecurityEventType::REPUTATION_VOTE_CAST: return "REPUTATION_VOTE_CAST";
        case SecurityEventType::REPUTATION_PENALTY_APPLIED: return "REPUTATION_PENALTY_APPLIED";
        case SecurityEventType::REPUTATION_BONUS_APPLIED: return "REPUTATION_BONUS_APPLIED";
        case SecurityEventType::VALIDATOR_RESPONSE_RECEIVED: return "VALIDATOR_RESPONSE_RECEIVED";
        case SecurityEventType::VALIDATOR_CHALLENGE_SENT: return "VALIDATOR_CHALLENGE_SENT";
        case SecurityEventType::VALIDATOR_TIMEOUT: return "VALIDATOR_TIMEOUT";
        case SecurityEventType::VALIDATOR_ACCURACY_UPDATE: return "VALIDATOR_ACCURACY_UPDATE";
        case SecurityEventType::VALIDATOR_ELIGIBILITY_CHANGE: return "VALIDATOR_ELIGIBILITY_CHANGE";
        case SecurityEventType::CONSENSUS_REACHED: return "CONSENSUS_REACHED";
        case SecurityEventType::CONSENSUS_FAILED: return "CONSENSUS_FAILED";
        case SecurityEventType::DAO_DISPUTE_CREATED: return "DAO_DISPUTE_CREATED";
        case SecurityEventType::DAO_DISPUTE_RESOLVED: return "DAO_DISPUTE_RESOLVED";
        case SecurityEventType::FRAUD_ATTEMPT_DETECTED: return "FRAUD_ATTEMPT_DETECTED";
        case SecurityEventType::FRAUD_RECORD_CREATED: return "FRAUD_RECORD_CREATED";
        case SecurityEventType::SYBIL_ATTACK_DETECTED: return "SYBIL_ATTACK_DETECTED";
        case SecurityEventType::TRUST_SCORE_QUERY: return "TRUST_SCORE_QUERY";
        case SecurityEventType::TRUST_SCORE_MODIFICATION: return "TRUST_SCORE_MODIFICATION";
        case SecurityEventType::REPUTATION_GATED_ACCESS: return "REPUTATION_GATED_ACCESS";
        case SecurityEventType::REPUTATION_GATED_DENIED: return "REPUTATION_GATED_DENIED";
        case SecurityEventType::ANOMALY_REPUTATION_SPIKE: return "ANOMALY_REPUTATION_SPIKE";
        case SecurityEventType::ANOMALY_REPUTATION_DROP: return "ANOMALY_REPUTATION_DROP";
        case SecurityEventType::ANOMALY_VALIDATOR_PATTERN: return "ANOMALY_VALIDATOR_PATTERN";
        case SecurityEventType::ANOMALY_VOTE_PATTERN: return "ANOMALY_VOTE_PATTERN";
        case SecurityEventType::ANOMALY_TRUST_GRAPH: return "ANOMALY_TRUST_GRAPH";
        case SecurityEventType::SYSTEM_STARTUP: return "SYSTEM_STARTUP";
        case SecurityEventType::SYSTEM_SHUTDOWN: return "SYSTEM_SHUTDOWN";
        case SecurityEventType::CONFIG_CHANGE: return "CONFIG_CHANGE";
        default: return "UNKNOWN";
    }
}

std::string SecurityEvent::GetSeverityString() const {
    switch (severity) {
        case SecuritySeverity::DEBUG: return "DEBUG";
        case SecuritySeverity::INFO: return "INFO";
        case SecuritySeverity::WARNING: return "WARNING";
        case SecuritySeverity::ERR_SEVERITY: return "ERR_SEVERITY";
        case SecuritySeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string SecurityEvent::ToLogString() const {
    std::string result = tfm::format("[%s] [%s] [Block %d] %s",
                                     GetSeverityString(),
                                     GetTypeString(),
                                     blockHeight,
                                     description);
    
    if (!primaryAddress.IsNull()) {
        result += tfm::format(" | Address: %s", primaryAddress.GetHex().substr(0, 16));
    }
    if (!txHash.IsNull()) {
        result += tfm::format(" | TxHash: %s", txHash.GetHex().substr(0, 16));
    }
    if (delta != 0) {
        result += tfm::format(" | Delta: %.2f", delta);
    }
    
    return result;
}


// ========== SecurityAuditLogger Implementation ==========

SecurityAuditLogger::SecurityAuditLogger(CVMDatabase& db)
    : m_db(db)
    , m_maxEventsInMemory(10000)
    , m_nextEventId(1)
    , m_metricsWindowStart(0)
    , m_reputationAnomalyThreshold(2.5)  // 2.5 standard deviations
    , m_validatorAnomalyThreshold(2.0)   // 2.0 standard deviations
    , m_votingAnomalyThreshold(2.0)      // 2.0 standard deviations
    , m_minLoggingSeverity(SecuritySeverity::INFO)
    , m_fileLoggingEnabled(false)
    , m_currentBlockHeight(0)
{
}

SecurityAuditLogger::~SecurityAuditLogger() {
    Shutdown();
}

bool SecurityAuditLogger::Initialize(int currentBlockHeight) {
    LOCK(m_cs);
    
    m_currentBlockHeight = currentBlockHeight;
    m_metricsWindowStart = GetCurrentTimestamp();
    m_currentMetrics = SecurityMetrics();
    m_currentMetrics.windowStart = m_metricsWindowStart;
    m_currentMetrics.startBlockHeight = currentBlockHeight;
    
    // Log system startup
    SecurityEvent startupEvent;
    startupEvent.eventId = m_nextEventId++;
    startupEvent.type = SecurityEventType::SYSTEM_STARTUP;
    startupEvent.severity = SecuritySeverity::INFO;
    startupEvent.timestamp = GetCurrentTimestamp();
    startupEvent.blockHeight = currentBlockHeight;
    startupEvent.description = "Security audit system initialized";
    
    AddEvent(startupEvent);
    
    LogPrint(BCLog::CVM, "Security audit system initialized at block %d\n", currentBlockHeight);
    
    return true;
}

void SecurityAuditLogger::Shutdown() {
    LOCK(m_cs);
    
    // Log system shutdown
    SecurityEvent shutdownEvent;
    shutdownEvent.eventId = m_nextEventId++;
    shutdownEvent.type = SecurityEventType::SYSTEM_SHUTDOWN;
    shutdownEvent.severity = SecuritySeverity::INFO;
    shutdownEvent.timestamp = GetCurrentTimestamp();
    shutdownEvent.blockHeight = m_currentBlockHeight;
    shutdownEvent.description = "Security audit system shutting down";
    
    AddEvent(shutdownEvent);
    
    LogPrint(BCLog::CVM, "Security audit system shutdown\n");
}

// ========== Reputation Event Logging (24.1) ==========

void SecurityAuditLogger::LogReputationChange(const ReputationChangeRecord& record) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::REPUTATION_SCORE_CHANGE;
    event.severity = (std::abs(record.delta) > 20) ? SecuritySeverity::WARNING : SecuritySeverity::INFO;
    event.timestamp = record.timestamp;
    event.blockHeight = record.blockHeight;
    event.primaryAddress = record.address;
    event.txHash = record.triggerTxHash;
    event.description = tfm::format("Reputation changed: %d -> %d (%+d) - %s",
                                    record.oldScore, record.newScore, record.delta, record.reason);
    event.oldValue = record.oldScore;
    event.newValue = record.newScore;
    event.delta = record.delta;
    
    event.metadata["reason"] = record.reason;
    event.metadata["old_behavior"] = tfm::format("%.2f", record.oldBehavior);
    event.metadata["new_behavior"] = tfm::format("%.2f", record.newBehavior);
    event.metadata["old_wot"] = tfm::format("%.2f", record.oldWoT);
    event.metadata["new_wot"] = tfm::format("%.2f", record.newWoT);
    event.metadata["old_economic"] = tfm::format("%.2f", record.oldEconomic);
    event.metadata["new_economic"] = tfm::format("%.2f", record.newEconomic);
    event.metadata["old_temporal"] = tfm::format("%.2f", record.oldTemporal);
    event.metadata["new_temporal"] = tfm::format("%.2f", record.newTemporal);
    
    AddEvent(event);
    
    // Update reputation history for anomaly detection
    m_reputationHistory[record.address].push_back(record.newScore);
    if (m_reputationHistory[record.address].size() > 100) {
        m_reputationHistory[record.address].erase(m_reputationHistory[record.address].begin());
    }
    
    // Update metrics
    m_currentMetrics.reputationChanges++;
    if (record.delta < 0) {
        m_currentMetrics.reputationPenalties++;
    } else if (record.delta > 0) {
        m_currentMetrics.reputationBonuses++;
    }
    
    // Check for anomaly
    AnomalyDetectionResult anomaly = DetectReputationAnomaly(record.address, record.newScore);
    if (anomaly.isAnomaly) {
        LogAnomaly(anomaly);
    }
    
    // Persist to database
    std::string key = tfm::format("%c%s%lld", DB_REPUTATION_CHANGE,
                                  record.address.GetHex(), record.timestamp);
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
    
    LogPrint(BCLog::CVM, "Security: Reputation change logged for %s: %d -> %d\n",
             record.address.GetHex().substr(0, 16), record.oldScore, record.newScore);
}

void SecurityAuditLogger::LogReputationVote(const uint160& voter, const uint160& target,
                                           int16_t voteValue, const uint256& txHash) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::REPUTATION_VOTE_CAST;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = voter;
    event.secondaryAddress = target;
    event.txHash = txHash;
    event.description = tfm::format("Reputation vote cast: %+d", voteValue);
    event.newValue = voteValue;
    
    event.metadata["voter"] = voter.GetHex();
    event.metadata["target"] = target.GetHex();
    event.metadata["vote_value"] = tfm::format("%d", voteValue);
    
    AddEvent(event);
    
    // Track voting patterns for anomaly detection
    m_votingPatterns[voter].push_back(voteValue);
    if (m_votingPatterns[voter].size() > 50) {
        m_votingPatterns[voter].erase(m_votingPatterns[voter].begin());
    }
    
    LogPrint(BCLog::CVM, "Security: Reputation vote logged: %s -> %s (%+d)\n",
             voter.GetHex().substr(0, 16), target.GetHex().substr(0, 16), voteValue);
}

void SecurityAuditLogger::LogReputationPenalty(const uint160& address, int16_t penalty,
                                              const std::string& reason, const uint256& txHash) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::REPUTATION_PENALTY_APPLIED;
    event.severity = (penalty > 20) ? SecuritySeverity::WARNING : SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = address;
    event.txHash = txHash;
    event.description = tfm::format("Reputation penalty applied: -%d - %s", penalty, reason);
    event.delta = -penalty;
    
    event.metadata["penalty"] = tfm::format("%d", penalty);
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    m_currentMetrics.reputationPenalties++;
    
    LogPrint(BCLog::CVM, "Security: Reputation penalty logged for %s: -%d (%s)\n",
             address.GetHex().substr(0, 16), penalty, reason);
}

void SecurityAuditLogger::LogReputationBonus(const uint160& address, int16_t bonus,
                                            const std::string& reason, const uint256& txHash) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::REPUTATION_BONUS_APPLIED;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = address;
    event.txHash = txHash;
    event.description = tfm::format("Reputation bonus applied: +%d - %s", bonus, reason);
    event.delta = bonus;
    
    event.metadata["bonus"] = tfm::format("%d", bonus);
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    m_currentMetrics.reputationBonuses++;
    
    LogPrint(BCLog::CVM, "Security: Reputation bonus logged for %s: +%d (%s)\n",
             address.GetHex().substr(0, 16), bonus, reason);
}


// ========== Validator Response Logging (24.1) ==========

void SecurityAuditLogger::LogValidatorResponse(const ValidatorResponseRecord& record) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::VALIDATOR_RESPONSE_RECEIVED;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = record.timestamp;
    event.blockHeight = record.blockHeight;
    event.primaryAddress = record.validatorAddress;
    event.txHash = record.txHash;
    
    std::string voteStr;
    switch (record.vote) {
        case ValidationVote::ACCEPT: voteStr = "ACCEPT"; break;
        case ValidationVote::REJECT: voteStr = "REJECT"; break;
        case ValidationVote::ABSTAIN: voteStr = "ABSTAIN"; break;
    }
    
    event.description = tfm::format("Validator response: %s (confidence: %.2f, diff: %d)",
                                    voteStr, record.confidence, record.scoreDifference);
    event.oldValue = record.reportedScore;
    event.newValue = record.calculatedScore;
    event.delta = record.scoreDifference;
    
    event.metadata["vote"] = voteStr;
    event.metadata["confidence"] = tfm::format("%.2f", record.confidence);
    event.metadata["has_wot"] = record.hasWoTConnection ? "true" : "false";
    event.metadata["response_time_ms"] = tfm::format("%lld", record.responseTime);
    event.metadata["reported_score"] = tfm::format("%d", record.reportedScore);
    event.metadata["calculated_score"] = tfm::format("%d", record.calculatedScore);
    
    AddEvent(event);
    
    // Update validator response time history for anomaly detection
    m_validatorResponseTimes[record.validatorAddress].push_back(record.responseTime);
    if (m_validatorResponseTimes[record.validatorAddress].size() > 100) {
        m_validatorResponseTimes[record.validatorAddress].erase(
            m_validatorResponseTimes[record.validatorAddress].begin());
    }
    
    // Update metrics
    m_currentMetrics.totalValidatorResponses++;
    
    // Calculate running average response time
    double totalTime = m_currentMetrics.averageResponseTime * (m_currentMetrics.totalValidatorResponses - 1);
    totalTime += record.responseTime;
    m_currentMetrics.averageResponseTime = totalTime / m_currentMetrics.totalValidatorResponses;
    
    // Persist to database
    std::string key = tfm::format("%c%s%s", DB_VALIDATOR_RESPONSE,
                                  record.txHash.GetHex(), record.validatorAddress.GetHex());
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
    
    // Check for anomaly
    AnomalyDetectionResult anomaly = DetectValidatorAnomaly(record.validatorAddress,
                                                           ValidationResponse());
    if (anomaly.isAnomaly) {
        LogAnomaly(anomaly);
    }
    
    LogPrint(BCLog::CVM, "Security: Validator response logged from %s: %s (%.2f confidence)\n",
             record.validatorAddress.GetHex().substr(0, 16), voteStr, record.confidence);
}

void SecurityAuditLogger::LogValidationChallenge(const uint256& txHash,
                                                const std::vector<uint160>& validators) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::VALIDATOR_CHALLENGE_SENT;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.txHash = txHash;
    event.description = tfm::format("Validation challenge sent to %zu validators", validators.size());
    event.newValue = validators.size();
    
    event.metadata["validator_count"] = tfm::format("%zu", validators.size());
    
    AddEvent(event);
    m_currentMetrics.totalValidations++;
    
    LogPrint(BCLog::CVM, "Security: Validation challenge logged for tx %s to %zu validators\n",
             txHash.GetHex().substr(0, 16), validators.size());
}

void SecurityAuditLogger::LogValidatorTimeout(const uint256& txHash, const uint160& validator) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::VALIDATOR_TIMEOUT;
    event.severity = SecuritySeverity::WARNING;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = validator;
    event.txHash = txHash;
    event.description = "Validator failed to respond within timeout";
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: Validator timeout logged for %s on tx %s\n",
             validator.GetHex().substr(0, 16), txHash.GetHex().substr(0, 16));
}

void SecurityAuditLogger::LogValidatorAccuracyUpdate(const uint160& validator,
                                                    double oldAccuracy, double newAccuracy) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::VALIDATOR_ACCURACY_UPDATE;
    event.severity = (newAccuracy < 0.7) ? SecuritySeverity::WARNING : SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = validator;
    event.description = tfm::format("Validator accuracy updated: %.2f%% -> %.2f%%",
                                    oldAccuracy * 100, newAccuracy * 100);
    event.oldValue = oldAccuracy;
    event.newValue = newAccuracy;
    event.delta = newAccuracy - oldAccuracy;
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: Validator accuracy update for %s: %.2f%% -> %.2f%%\n",
             validator.GetHex().substr(0, 16), oldAccuracy * 100, newAccuracy * 100);
}

void SecurityAuditLogger::LogValidatorEligibilityChange(const uint160& validator,
                                                       bool wasEligible, bool isEligible,
                                                       const std::string& reason) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::VALIDATOR_ELIGIBILITY_CHANGE;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = validator;
    event.description = tfm::format("Validator eligibility changed: %s -> %s - %s",
                                    wasEligible ? "eligible" : "ineligible",
                                    isEligible ? "eligible" : "ineligible",
                                    reason);
    event.oldValue = wasEligible ? 1 : 0;
    event.newValue = isEligible ? 1 : 0;
    
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    
    // Update active validator count
    if (wasEligible && !isEligible) {
        if (m_currentMetrics.activeValidators > 0) {
            m_currentMetrics.activeValidators--;
        }
    } else if (!wasEligible && isEligible) {
        m_currentMetrics.activeValidators++;
    }
    
    LogPrint(BCLog::CVM, "Security: Validator eligibility change for %s: %s (%s)\n",
             validator.GetHex().substr(0, 16), isEligible ? "eligible" : "ineligible", reason);
}

// ========== Consensus Event Logging ==========

void SecurityAuditLogger::LogConsensusReached(const uint256& txHash, bool approved,
                                             int acceptVotes, int rejectVotes, int abstainVotes) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::CONSENSUS_REACHED;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.txHash = txHash;
    event.description = tfm::format("Consensus reached: %s (Accept: %d, Reject: %d, Abstain: %d)",
                                    approved ? "APPROVED" : "REJECTED",
                                    acceptVotes, rejectVotes, abstainVotes);
    event.newValue = approved ? 1 : 0;
    
    event.metadata["approved"] = approved ? "true" : "false";
    event.metadata["accept_votes"] = tfm::format("%d", acceptVotes);
    event.metadata["reject_votes"] = tfm::format("%d", rejectVotes);
    event.metadata["abstain_votes"] = tfm::format("%d", abstainVotes);
    
    AddEvent(event);
    m_currentMetrics.successfulValidations++;
    
    LogPrint(BCLog::CVM, "Security: Consensus reached for tx %s: %s\n",
             txHash.GetHex().substr(0, 16), approved ? "APPROVED" : "REJECTED");
}

void SecurityAuditLogger::LogConsensusFailed(const uint256& txHash, const std::string& reason) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::CONSENSUS_FAILED;
    event.severity = SecuritySeverity::WARNING;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.txHash = txHash;
    event.description = tfm::format("Consensus failed: %s", reason);
    
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    m_currentMetrics.failedValidations++;
    
    LogPrint(BCLog::CVM, "Security: Consensus failed for tx %s: %s\n",
             txHash.GetHex().substr(0, 16), reason);
}

void SecurityAuditLogger::LogDAODisputeCreated(const uint256& disputeId, const uint256& txHash,
                                              const uint160& address) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::DAO_DISPUTE_CREATED;
    event.severity = SecuritySeverity::WARNING;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = address;
    event.txHash = txHash;
    event.description = "DAO dispute created for transaction";
    
    event.metadata["dispute_id"] = disputeId.GetHex();
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: DAO dispute created for tx %s\n",
             txHash.GetHex().substr(0, 16));
}

void SecurityAuditLogger::LogDAODisputeResolved(const uint256& disputeId, bool approved,
                                               const std::string& resolution) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::DAO_DISPUTE_RESOLVED;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.description = tfm::format("DAO dispute resolved: %s - %s",
                                    approved ? "APPROVED" : "REJECTED", resolution);
    event.newValue = approved ? 1 : 0;
    
    event.metadata["dispute_id"] = disputeId.GetHex();
    event.metadata["approved"] = approved ? "true" : "false";
    event.metadata["resolution"] = resolution;
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: DAO dispute %s resolved: %s\n",
             disputeId.GetHex().substr(0, 16), approved ? "APPROVED" : "REJECTED");
}


// ========== Fraud Event Logging ==========

void SecurityAuditLogger::LogFraudAttempt(const uint160& address, const uint256& txHash,
                                         int16_t claimedScore, int16_t actualScore) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::FRAUD_ATTEMPT_DETECTED;
    event.severity = SecuritySeverity::CRITICAL;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = address;
    event.txHash = txHash;
    event.description = tfm::format("Fraud attempt detected: claimed %d, actual %d (diff: %d)",
                                    claimedScore, actualScore, claimedScore - actualScore);
    event.oldValue = actualScore;
    event.newValue = claimedScore;
    event.delta = claimedScore - actualScore;
    
    event.metadata["claimed_score"] = tfm::format("%d", claimedScore);
    event.metadata["actual_score"] = tfm::format("%d", actualScore);
    event.metadata["difference"] = tfm::format("%d", claimedScore - actualScore);
    
    AddEvent(event);
    m_currentMetrics.fraudAttemptsDetected++;
    
    LogPrintf("SECURITY ALERT: Fraud attempt detected from %s - claimed %d, actual %d\n",
              address.GetHex().substr(0, 16), claimedScore, actualScore);
}

void SecurityAuditLogger::LogFraudRecordCreated(const FraudRecord& record) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::FRAUD_RECORD_CREATED;
    event.severity = SecuritySeverity::CRITICAL;
    event.timestamp = record.timestamp;
    event.blockHeight = record.blockHeight;
    event.primaryAddress = record.fraudsterAddress;
    event.txHash = record.txHash;
    event.description = tfm::format("Fraud record created: penalty %d, bond slashed %lld",
                                    record.reputationPenalty, record.bondSlashed);
    event.delta = record.scoreDifference;
    
    event.metadata["reputation_penalty"] = tfm::format("%d", record.reputationPenalty);
    event.metadata["bond_slashed"] = tfm::format("%lld", record.bondSlashed);
    event.metadata["claimed_score"] = tfm::format("%d", record.claimedScore.finalScore);
    event.metadata["actual_score"] = tfm::format("%d", record.actualScore.finalScore);
    
    AddEvent(event);
    m_currentMetrics.fraudRecordsCreated++;
    
    LogPrintf("SECURITY: Fraud record created for %s - penalty %d\n",
              record.fraudsterAddress.GetHex().substr(0, 16), record.reputationPenalty);
}

void SecurityAuditLogger::LogSybilAttackDetected(const std::vector<uint160>& addresses,
                                                double riskScore, const std::string& reason) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::SYBIL_ATTACK_DETECTED;
    event.severity = SecuritySeverity::CRITICAL;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    if (!addresses.empty()) {
        event.primaryAddress = addresses[0];
    }
    event.description = tfm::format("Sybil attack detected: %zu addresses, risk %.2f - %s",
                                    addresses.size(), riskScore, reason);
    event.newValue = riskScore;
    
    event.metadata["address_count"] = tfm::format("%zu", addresses.size());
    event.metadata["risk_score"] = tfm::format("%.2f", riskScore);
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    m_currentMetrics.sybilAttacksDetected++;
    
    LogPrintf("SECURITY ALERT: Sybil attack detected - %zu addresses, risk %.2f\n",
              addresses.size(), riskScore);
}

// ========== Access Control Logging (24.4) ==========

void SecurityAuditLogger::LogTrustScoreQuery(const uint160& requester, const uint160& target,
                                            int16_t score) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::TRUST_SCORE_QUERY;
    event.severity = SecuritySeverity::DEBUG;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = requester;
    event.secondaryAddress = target;
    event.description = tfm::format("Trust score queried: %d", score);
    event.newValue = score;
    
    AddEvent(event);
    m_currentMetrics.accessAttempts++;
    m_currentMetrics.accessGranted++;
    
    LogPrint(BCLog::CVM, "Security: Trust score query from %s for %s: %d\n",
             requester.GetHex().substr(0, 16), target.GetHex().substr(0, 16), score);
}

void SecurityAuditLogger::LogTrustScoreModification(const uint160& modifier, const uint160& target,
                                                   int16_t oldScore, int16_t newScore,
                                                   const std::string& reason) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::TRUST_SCORE_MODIFICATION;
    event.severity = (std::abs(newScore - oldScore) > 20) ? SecuritySeverity::WARNING : SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.primaryAddress = modifier;
    event.secondaryAddress = target;
    event.description = tfm::format("Trust score modified: %d -> %d - %s",
                                    oldScore, newScore, reason);
    event.oldValue = oldScore;
    event.newValue = newScore;
    event.delta = newScore - oldScore;
    
    event.metadata["reason"] = reason;
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: Trust score modification by %s for %s: %d -> %d\n",
             modifier.GetHex().substr(0, 16), target.GetHex().substr(0, 16), oldScore, newScore);
}

void SecurityAuditLogger::LogReputationGatedAccess(const AccessControlRecord& record) {
    LOCK(m_cs);
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = record.accessGranted ? SecurityEventType::REPUTATION_GATED_ACCESS
                                      : SecurityEventType::REPUTATION_GATED_DENIED;
    event.severity = record.accessGranted ? SecuritySeverity::INFO : SecuritySeverity::WARNING;
    event.timestamp = record.timestamp;
    event.blockHeight = record.blockHeight;
    event.primaryAddress = record.requesterAddress;
    event.secondaryAddress = record.targetAddress;
    event.description = tfm::format("Reputation-gated access %s: %s (required: %d, actual: %d)",
                                    record.accessGranted ? "granted" : "denied",
                                    record.operation,
                                    record.requiredReputation, record.actualReputation);
    event.oldValue = record.requiredReputation;
    event.newValue = record.actualReputation;
    
    event.metadata["operation"] = record.operation;
    event.metadata["required_reputation"] = tfm::format("%d", record.requiredReputation);
    event.metadata["actual_reputation"] = tfm::format("%d", record.actualReputation);
    if (!record.accessGranted) {
        event.metadata["denial_reason"] = record.denialReason;
    }
    
    AddEvent(event);
    
    // Update metrics
    m_currentMetrics.accessAttempts++;
    if (record.accessGranted) {
        m_currentMetrics.accessGranted++;
    } else {
        m_currentMetrics.accessDenied++;
    }
    
    // Persist to database
    std::string key = tfm::format("%c%lld", DB_ACCESS_CONTROL, record.timestamp);
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
    
    LogPrint(BCLog::CVM, "Security: Reputation-gated access %s for %s: %s\n",
             record.accessGranted ? "granted" : "denied",
             record.requesterAddress.GetHex().substr(0, 16), record.operation);
}


// ========== Anomaly Detection (24.2) ==========

AnomalyDetectionResult SecurityAuditLogger::DetectReputationAnomaly(const uint160& address,
                                                                   int16_t newScore) {
    AnomalyDetectionResult result;
    result.address = address;
    result.anomalyType = "reputation";
    result.timestamp = GetCurrentTimestamp();
    result.blockHeight = m_currentBlockHeight;
    result.threshold = m_reputationAnomalyThreshold;
    
    auto it = m_reputationHistory.find(address);
    if (it == m_reputationHistory.end() || it->second.size() < 5) {
        // Not enough history for anomaly detection
        result.isAnomaly = false;
        result.anomalyScore = 0;
        result.description = "Insufficient history for anomaly detection";
        return result;
    }
    
    const std::vector<int16_t>& history = it->second;
    
    // Calculate mean and standard deviation
    double sum = std::accumulate(history.begin(), history.end(), 0.0);
    double mean = sum / history.size();
    
    double stddev = CalculateStandardDeviation(history);
    
    if (stddev < 0.1) {
        // Very stable history, any change is potentially anomalous
        stddev = 1.0;
    }
    
    // Calculate z-score
    double zScore = std::abs(newScore - mean) / stddev;
    result.anomalyScore = zScore / m_reputationAnomalyThreshold;  // Normalize to 0-1
    
    if (zScore > m_reputationAnomalyThreshold) {
        result.isAnomaly = true;
        
        if (newScore > mean) {
            result.description = tfm::format("Unusual reputation spike: %d (mean: %.1f, stddev: %.1f, z-score: %.2f)",
                                            newScore, mean, stddev, zScore);
            result.indicators.push_back("Reputation increased significantly above historical average");
        } else {
            result.description = tfm::format("Unusual reputation drop: %d (mean: %.1f, stddev: %.1f, z-score: %.2f)",
                                            newScore, mean, stddev, zScore);
            result.indicators.push_back("Reputation decreased significantly below historical average");
        }
        
        // Check for rapid changes
        if (history.size() >= 2) {
            int16_t lastScore = history.back();
            int16_t delta = newScore - lastScore;
            if (std::abs(delta) > 20) {
                result.indicators.push_back(tfm::format("Rapid change: %+d points in single update", delta));
            }
        }
    } else {
        result.isAnomaly = false;
        result.description = "Reputation change within normal range";
    }
    
    return result;
}

AnomalyDetectionResult SecurityAuditLogger::DetectValidatorAnomaly(const uint160& validator,
                                                                  const ValidationResponse& response) {
    AnomalyDetectionResult result;
    result.address = validator;
    result.anomalyType = "validator";
    result.timestamp = GetCurrentTimestamp();
    result.blockHeight = m_currentBlockHeight;
    result.threshold = m_validatorAnomalyThreshold;
    
    auto it = m_validatorResponseTimes.find(validator);
    if (it == m_validatorResponseTimes.end() || it->second.size() < 10) {
        result.isAnomaly = false;
        result.anomalyScore = 0;
        result.description = "Insufficient history for anomaly detection";
        return result;
    }
    
    const std::vector<double>& responseTimes = it->second;
    
    // Calculate mean and standard deviation of response times
    double sum = std::accumulate(responseTimes.begin(), responseTimes.end(), 0.0);
    double mean = sum / responseTimes.size();
    
    double stddev = CalculateStandardDeviation(responseTimes);
    
    if (stddev < 10.0) {
        stddev = 10.0;  // Minimum stddev of 10ms
    }
    
    // Check for patterns
    result.isAnomaly = false;
    result.anomalyScore = 0;
    
    // Check for consistently slow responses
    int slowCount = 0;
    for (double time : responseTimes) {
        if (time > mean + stddev) {
            slowCount++;
        }
    }
    
    if (slowCount > responseTimes.size() * 0.5) {
        result.isAnomaly = true;
        result.anomalyScore = 0.7;
        result.description = "Validator showing consistently slow response times";
        result.indicators.push_back(tfm::format("%.0f%% of responses above average", 
                                               (double)slowCount / responseTimes.size() * 100));
    }
    
    // Check for erratic response times
    double variance = 0;
    for (double time : responseTimes) {
        variance += (time - mean) * (time - mean);
    }
    variance /= responseTimes.size();
    
    double coefficientOfVariation = std::sqrt(variance) / mean;
    if (coefficientOfVariation > 1.5) {
        result.isAnomaly = true;
        result.anomalyScore = std::max(result.anomalyScore, 0.6);
        result.description = "Validator showing erratic response time patterns";
        result.indicators.push_back(tfm::format("High coefficient of variation: %.2f", coefficientOfVariation));
    }
    
    if (!result.isAnomaly) {
        result.description = "Validator response patterns within normal range";
    }
    
    return result;
}

AnomalyDetectionResult SecurityAuditLogger::DetectVotingAnomaly(const uint160& voter) {
    AnomalyDetectionResult result;
    result.address = voter;
    result.anomalyType = "voting";
    result.timestamp = GetCurrentTimestamp();
    result.blockHeight = m_currentBlockHeight;
    result.threshold = m_votingAnomalyThreshold;
    
    auto it = m_votingPatterns.find(voter);
    if (it == m_votingPatterns.end() || it->second.size() < 10) {
        result.isAnomaly = false;
        result.anomalyScore = 0;
        result.description = "Insufficient voting history for anomaly detection";
        return result;
    }
    
    const std::vector<int>& votes = it->second;
    
    // Check for voting patterns
    result.isAnomaly = false;
    result.anomalyScore = 0;
    
    // Check for all positive or all negative votes
    int positiveCount = 0;
    int negativeCount = 0;
    for (int vote : votes) {
        if (vote > 0) positiveCount++;
        else if (vote < 0) negativeCount++;
    }
    
    double positiveRatio = (double)positiveCount / votes.size();
    double negativeRatio = (double)negativeCount / votes.size();
    
    if (positiveRatio > 0.95 || negativeRatio > 0.95) {
        result.isAnomaly = true;
        result.anomalyScore = 0.8;
        result.description = "Voter showing extreme bias in voting pattern";
        if (positiveRatio > 0.95) {
            result.indicators.push_back(tfm::format("%.0f%% positive votes", positiveRatio * 100));
        } else {
            result.indicators.push_back(tfm::format("%.0f%% negative votes", negativeRatio * 100));
        }
    }
    
    // Check for vote value patterns (always same value)
    std::map<int, int> voteValueCounts;
    for (int vote : votes) {
        voteValueCounts[vote]++;
    }
    
    for (const auto& pair : voteValueCounts) {
        double ratio = (double)pair.second / votes.size();
        if (ratio > 0.8 && votes.size() >= 20) {
            result.isAnomaly = true;
            result.anomalyScore = std::max(result.anomalyScore, 0.7);
            result.description = "Voter showing repetitive voting pattern";
            result.indicators.push_back(tfm::format("%.0f%% of votes are value %d", ratio * 100, pair.first));
        }
    }
    
    if (!result.isAnomaly) {
        result.description = "Voting patterns within normal range";
    }
    
    return result;
}

void SecurityAuditLogger::LogAnomaly(const AnomalyDetectionResult& result) {
    LOCK(m_cs);
    
    SecurityEventType eventType;
    if (result.anomalyType == "reputation") {
        eventType = result.description.find("spike") != std::string::npos
                    ? SecurityEventType::ANOMALY_REPUTATION_SPIKE
                    : SecurityEventType::ANOMALY_REPUTATION_DROP;
    } else if (result.anomalyType == "validator") {
        eventType = SecurityEventType::ANOMALY_VALIDATOR_PATTERN;
    } else if (result.anomalyType == "voting") {
        eventType = SecurityEventType::ANOMALY_VOTE_PATTERN;
    } else {
        eventType = SecurityEventType::ANOMALY_TRUST_GRAPH;
    }
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = eventType;
    event.severity = (result.anomalyScore > 0.8) ? SecuritySeverity::WARNING : SecuritySeverity::INFO;
    event.timestamp = result.timestamp;
    event.blockHeight = result.blockHeight;
    event.primaryAddress = result.address;
    event.description = result.description;
    event.newValue = result.anomalyScore;
    
    event.metadata["anomaly_type"] = result.anomalyType;
    event.metadata["anomaly_score"] = tfm::format("%.2f", result.anomalyScore);
    event.metadata["threshold"] = tfm::format("%.2f", result.threshold);
    
    for (size_t i = 0; i < result.indicators.size(); i++) {
        event.metadata[tfm::format("indicator_%zu", i)] = result.indicators[i];
    }
    
    AddEvent(event);
    
    // Update metrics
    m_currentMetrics.anomaliesDetected++;
    if (result.anomalyType == "reputation") {
        m_currentMetrics.reputationAnomalies++;
    } else if (result.anomalyType == "validator") {
        m_currentMetrics.validatorAnomalies++;
    } else {
        m_currentMetrics.trustGraphAnomalies++;
    }
    
    // Persist to database
    std::string key = tfm::format("%c%lld", DB_ANOMALY, result.timestamp);
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << result;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
    
    LogPrint(BCLog::CVM, "Security: Anomaly detected for %s: %s (score: %.2f)\n",
             result.address.GetHex().substr(0, 16), result.description, result.anomalyScore);
}


// ========== Security Metrics (24.3) ==========

SecurityMetrics SecurityAuditLogger::GetCurrentMetrics() {
    LOCK(m_cs);
    
    m_currentMetrics.windowEnd = GetCurrentTimestamp();
    m_currentMetrics.endBlockHeight = m_currentBlockHeight;
    m_currentMetrics.CalculateRates();
    
    return m_currentMetrics;
}

SecurityMetrics SecurityAuditLogger::GetMetricsForWindow(int64_t startTime, int64_t endTime) {
    LOCK(m_cs);
    
    SecurityMetrics metrics;
    metrics.windowStart = startTime;
    metrics.windowEnd = endTime;
    
    // Iterate through recent events and aggregate
    for (const auto& event : m_recentEvents) {
        if (event.timestamp < startTime || event.timestamp > endTime) {
            continue;
        }
        
        switch (event.type) {
            case SecurityEventType::VALIDATOR_CHALLENGE_SENT:
                metrics.totalValidations++;
                break;
            case SecurityEventType::CONSENSUS_REACHED:
                metrics.successfulValidations++;
                break;
            case SecurityEventType::CONSENSUS_FAILED:
                metrics.failedValidations++;
                break;
            case SecurityEventType::VALIDATOR_RESPONSE_RECEIVED:
                metrics.totalValidatorResponses++;
                break;
            case SecurityEventType::REPUTATION_SCORE_CHANGE:
                metrics.reputationChanges++;
                break;
            case SecurityEventType::REPUTATION_PENALTY_APPLIED:
                metrics.reputationPenalties++;
                break;
            case SecurityEventType::REPUTATION_BONUS_APPLIED:
                metrics.reputationBonuses++;
                break;
            case SecurityEventType::FRAUD_ATTEMPT_DETECTED:
                metrics.fraudAttemptsDetected++;
                break;
            case SecurityEventType::FRAUD_RECORD_CREATED:
                metrics.fraudRecordsCreated++;
                break;
            case SecurityEventType::SYBIL_ATTACK_DETECTED:
                metrics.sybilAttacksDetected++;
                break;
            case SecurityEventType::ANOMALY_REPUTATION_SPIKE:
            case SecurityEventType::ANOMALY_REPUTATION_DROP:
                metrics.anomaliesDetected++;
                metrics.reputationAnomalies++;
                break;
            case SecurityEventType::ANOMALY_VALIDATOR_PATTERN:
                metrics.anomaliesDetected++;
                metrics.validatorAnomalies++;
                break;
            case SecurityEventType::ANOMALY_TRUST_GRAPH:
                metrics.anomaliesDetected++;
                metrics.trustGraphAnomalies++;
                break;
            case SecurityEventType::REPUTATION_GATED_ACCESS:
                metrics.accessAttempts++;
                metrics.accessGranted++;
                break;
            case SecurityEventType::REPUTATION_GATED_DENIED:
                metrics.accessAttempts++;
                metrics.accessDenied++;
                break;
            default:
                break;
        }
    }
    
    metrics.CalculateRates();
    return metrics;
}

SecurityMetrics SecurityAuditLogger::GetMetricsForBlockRange(int startBlock, int endBlock) {
    LOCK(m_cs);
    
    SecurityMetrics metrics;
    metrics.startBlockHeight = startBlock;
    metrics.endBlockHeight = endBlock;
    
    // Iterate through recent events and aggregate
    for (const auto& event : m_recentEvents) {
        if (event.blockHeight < startBlock || event.blockHeight > endBlock) {
            continue;
        }
        
        // Same aggregation logic as GetMetricsForWindow
        switch (event.type) {
            case SecurityEventType::VALIDATOR_CHALLENGE_SENT:
                metrics.totalValidations++;
                break;
            case SecurityEventType::CONSENSUS_REACHED:
                metrics.successfulValidations++;
                break;
            case SecurityEventType::CONSENSUS_FAILED:
                metrics.failedValidations++;
                break;
            case SecurityEventType::VALIDATOR_RESPONSE_RECEIVED:
                metrics.totalValidatorResponses++;
                break;
            case SecurityEventType::REPUTATION_SCORE_CHANGE:
                metrics.reputationChanges++;
                break;
            case SecurityEventType::REPUTATION_PENALTY_APPLIED:
                metrics.reputationPenalties++;
                break;
            case SecurityEventType::REPUTATION_BONUS_APPLIED:
                metrics.reputationBonuses++;
                break;
            case SecurityEventType::FRAUD_ATTEMPT_DETECTED:
                metrics.fraudAttemptsDetected++;
                break;
            case SecurityEventType::FRAUD_RECORD_CREATED:
                metrics.fraudRecordsCreated++;
                break;
            case SecurityEventType::SYBIL_ATTACK_DETECTED:
                metrics.sybilAttacksDetected++;
                break;
            case SecurityEventType::ANOMALY_REPUTATION_SPIKE:
            case SecurityEventType::ANOMALY_REPUTATION_DROP:
                metrics.anomaliesDetected++;
                metrics.reputationAnomalies++;
                break;
            case SecurityEventType::ANOMALY_VALIDATOR_PATTERN:
                metrics.anomaliesDetected++;
                metrics.validatorAnomalies++;
                break;
            case SecurityEventType::ANOMALY_TRUST_GRAPH:
                metrics.anomaliesDetected++;
                metrics.trustGraphAnomalies++;
                break;
            case SecurityEventType::REPUTATION_GATED_ACCESS:
                metrics.accessAttempts++;
                metrics.accessGranted++;
                break;
            case SecurityEventType::REPUTATION_GATED_DENIED:
                metrics.accessAttempts++;
                metrics.accessDenied++;
                break;
            default:
                break;
        }
    }
    
    metrics.CalculateRates();
    return metrics;
}

std::vector<SecurityEvent> SecurityAuditLogger::GetRecentEvents(size_t count) {
    LOCK(m_cs);
    
    std::vector<SecurityEvent> result;
    size_t start = (m_recentEvents.size() > count) ? m_recentEvents.size() - count : 0;
    
    for (size_t i = start; i < m_recentEvents.size(); i++) {
        result.push_back(m_recentEvents[i]);
    }
    
    return result;
}

std::vector<SecurityEvent> SecurityAuditLogger::GetEventsByType(SecurityEventType type,
                                                               size_t count) {
    LOCK(m_cs);
    
    std::vector<SecurityEvent> result;
    
    for (auto it = m_recentEvents.rbegin(); it != m_recentEvents.rend() && result.size() < count; ++it) {
        if (it->type == type) {
            result.push_back(*it);
        }
    }
    
    return result;
}

std::vector<SecurityEvent> SecurityAuditLogger::GetEventsForAddress(const uint160& address,
                                                                   size_t count) {
    LOCK(m_cs);
    
    std::vector<SecurityEvent> result;
    
    for (auto it = m_recentEvents.rbegin(); it != m_recentEvents.rend() && result.size() < count; ++it) {
        if (it->primaryAddress == address || it->secondaryAddress == address) {
            result.push_back(*it);
        }
    }
    
    return result;
}

// ========== Configuration ==========

void SecurityAuditLogger::SetAnomalyThresholds(double reputationThreshold,
                                              double validatorThreshold,
                                              double votingThreshold) {
    LOCK(m_cs);
    
    m_reputationAnomalyThreshold = reputationThreshold;
    m_validatorAnomalyThreshold = validatorThreshold;
    m_votingAnomalyThreshold = votingThreshold;
    
    SecurityEvent event;
    event.eventId = m_nextEventId++;
    event.type = SecurityEventType::CONFIG_CHANGE;
    event.severity = SecuritySeverity::INFO;
    event.timestamp = GetCurrentTimestamp();
    event.blockHeight = m_currentBlockHeight;
    event.description = tfm::format("Anomaly thresholds updated: reputation=%.2f, validator=%.2f, voting=%.2f",
                                    reputationThreshold, validatorThreshold, votingThreshold);
    
    AddEvent(event);
    
    LogPrint(BCLog::CVM, "Security: Anomaly thresholds updated\n");
}

void SecurityAuditLogger::SetLoggingLevel(SecuritySeverity minSeverity) {
    LOCK(m_cs);
    m_minLoggingSeverity = minSeverity;
    
    LogPrint(BCLog::CVM, "Security: Logging level set to %d\n", static_cast<int>(minSeverity));
}

void SecurityAuditLogger::SetFileLogging(bool enabled, const std::string& logPath) {
    LOCK(m_cs);
    m_fileLoggingEnabled = enabled;
    if (!logPath.empty()) {
        m_logFilePath = logPath;
    }
    
    LogPrint(BCLog::CVM, "Security: File logging %s\n", enabled ? "enabled" : "disabled");
}

void SecurityAuditLogger::SetMaxEventsInMemory(size_t maxEvents) {
    LOCK(m_cs);
    m_maxEventsInMemory = maxEvents;
    
    // Trim if necessary
    while (m_recentEvents.size() > m_maxEventsInMemory) {
        m_recentEvents.pop_front();
    }
}

// ========== Internal Methods ==========

void SecurityAuditLogger::AddEvent(const SecurityEvent& event) {
    // Check severity filter
    if (event.severity < m_minLoggingSeverity) {
        return;
    }
    
    // Add to in-memory queue
    m_recentEvents.push_back(event);
    
    // Trim if necessary
    while (m_recentEvents.size() > m_maxEventsInMemory) {
        m_recentEvents.pop_front();
    }
    
    // Update metrics
    UpdateMetrics(event);
    
    // Persist to database
    PersistEvent(event);
    
    // Write to log file if enabled
    if (m_fileLoggingEnabled) {
        WriteToLogFile(event);
    }
}

void SecurityAuditLogger::PersistEvent(const SecurityEvent& event) {
    std::string key = tfm::format("%c%020llu", DB_SECURITY_EVENT, event.eventId);
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << event;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
}

void SecurityAuditLogger::UpdateMetrics(const SecurityEvent& event) {
    // Metrics are updated in the specific logging methods
    // This method can be used for additional cross-cutting metrics
}

double SecurityAuditLogger::CalculateStandardDeviation(const std::vector<int16_t>& values) {
    if (values.size() < 2) return 0;
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    
    double variance = 0;
    for (int16_t val : values) {
        variance += (val - mean) * (val - mean);
    }
    variance /= values.size();
    
    return std::sqrt(variance);
}

double SecurityAuditLogger::CalculateStandardDeviation(const std::vector<double>& values) {
    if (values.size() < 2) return 0;
    
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    
    double variance = 0;
    for (double val : values) {
        variance += (val - mean) * (val - mean);
    }
    variance /= values.size();
    
    return std::sqrt(variance);
}

int64_t SecurityAuditLogger::GetCurrentTimestamp() {
    return GetTimeMillis();
}

void SecurityAuditLogger::WriteToLogFile(const SecurityEvent& event) {
    if (m_logFilePath.empty()) {
        return;
    }
    
    try {
        std::ofstream logFile(m_logFilePath, std::ios::app);
        if (logFile.is_open()) {
            logFile << event.ToLogString() << "\n";
            logFile.close();
        }
    } catch (const std::exception& e) {
        LogPrintf("Security: Failed to write to log file: %s\n", e.what());
    }
}

// ========== Global Functions ==========

bool InitSecurityAudit(CVMDatabase& db, int currentBlockHeight) {
    g_securityAudit = std::make_unique<SecurityAuditLogger>(db);
    bool success = g_securityAudit->Initialize(currentBlockHeight);
    
    // Also initialize access control auditor
    if (success) {
        success = InitAccessControlAuditor(db, g_securityAudit.get(), currentBlockHeight);
    }
    
    return success;
}

void ShutdownSecurityAudit() {
    // Shutdown access control auditor first
    ShutdownAccessControlAuditor();
    
    if (g_securityAudit) {
        g_securityAudit->Shutdown();
        g_securityAudit.reset();
    }
}

} // namespace CVM
