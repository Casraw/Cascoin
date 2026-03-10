// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/anomaly_detector.h>
#include <cvm/cvmdb.h>
#include <cvm/security_audit.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>
#include <cmath>
#include <numeric>
#include <algorithm>

namespace CVM {

// Global instance
std::unique_ptr<AnomalyDetector> g_anomalyDetector;

// Database keys
static const char DB_ANOMALY_ALERT = 'Z';  // Anomaly alert: 'Z' + alertId -> AnomalyAlert

// ========== ValidatorBehaviorProfile Implementation ==========

void ValidatorBehaviorProfile::UpdateStats() {
    // Calculate response time statistics
    if (!responseTimes.empty()) {
        double sum = std::accumulate(responseTimes.begin(), responseTimes.end(), 0.0);
        averageResponseTime = sum / responseTimes.size();
        
        double variance = 0;
        for (double time : responseTimes) {
            variance += (time - averageResponseTime) * (time - averageResponseTime);
        }
        responseTimeStdDev = std::sqrt(variance / responseTimes.size());
    }
    
    // Calculate vote statistics
    if (!voteHistory.empty()) {
        int accepts = 0, rejects = 0, abstains = 0;
        for (int vote : voteHistory) {
            if (vote > 0) accepts++;
            else if (vote < 0) rejects++;
            else abstains++;
        }
        
        double total = voteHistory.size();
        acceptRate = accepts / total;
        rejectRate = rejects / total;
        abstainRate = abstains / total;
    }
}

// ========== AnomalyDetector Implementation ==========

AnomalyDetector::AnomalyDetector(CVMDatabase& db, SecurityAuditLogger* auditLogger)
    : m_db(db)
    , m_auditLogger(auditLogger)
    , m_currentBlockHeight(0)
    , m_nextAlertId(1)
    , m_reputationHistoryWindow(100)
    , m_validatorHistoryWindow(100)
    , m_reputationZScoreThreshold(2.5)
    , m_validatorZScoreThreshold(2.0)
    , m_coordinationThreshold(0.8)
{
    // Enable all detection types by default
    m_enabledDetections[AnomalyType::REPUTATION_SPIKE] = true;
    m_enabledDetections[AnomalyType::REPUTATION_DROP] = true;
    m_enabledDetections[AnomalyType::REPUTATION_OSCILLATION] = true;
    m_enabledDetections[AnomalyType::VALIDATOR_SLOW_RESPONSE] = true;
    m_enabledDetections[AnomalyType::VALIDATOR_ERRATIC_TIMING] = true;
    m_enabledDetections[AnomalyType::VALIDATOR_BIAS] = true;
    m_enabledDetections[AnomalyType::VOTE_MANIPULATION] = true;
    m_enabledDetections[AnomalyType::VOTE_EXTREME_BIAS] = true;
    m_enabledDetections[AnomalyType::TRUST_GRAPH_MANIPULATION] = true;
    m_enabledDetections[AnomalyType::SYBIL_CLUSTER] = true;
    m_enabledDetections[AnomalyType::COORDINATED_ATTACK] = true;
}

AnomalyDetector::~AnomalyDetector() {
}

bool AnomalyDetector::Initialize(int currentBlockHeight) {
    LOCK(m_cs);
    
    m_currentBlockHeight = currentBlockHeight;
    LoadActiveAlerts();
    
    LogPrint(BCLog::CVM, "Anomaly detector initialized at block %d\n", currentBlockHeight);
    return true;
}

// ========== Reputation Anomaly Detection ==========

void AnomalyDetector::RecordReputationScore(const uint160& address, int16_t score,
                                           const std::string& reason) {
    LOCK(m_cs);
    
    ReputationHistoryEntry entry;
    entry.score = score;
    entry.timestamp = GetCurrentTimestamp();
    entry.blockHeight = m_currentBlockHeight;
    entry.reason = reason;
    
    auto& history = m_reputationHistory[address];
    history.push_back(entry);
    
    // Trim history
    while (history.size() > m_reputationHistoryWindow) {
        history.pop_front();
    }
    
    // Check for anomalies
    AnomalyAlert alert;
    
    if (m_enabledDetections[AnomalyType::REPUTATION_SPIKE] && 
        DetectReputationSpike(address, score, alert)) {
        CreateAlert(alert);
    }
    
    if (m_enabledDetections[AnomalyType::REPUTATION_DROP] && 
        DetectReputationDrop(address, score, alert)) {
        CreateAlert(alert);
    }
    
    if (m_enabledDetections[AnomalyType::REPUTATION_OSCILLATION] && 
        DetectReputationOscillation(address, alert)) {
        CreateAlert(alert);
    }
}

std::vector<AnomalyAlert> AnomalyDetector::AnalyzeReputationChanges(const uint160& address) {
    LOCK(m_cs);
    
    std::vector<AnomalyAlert> alerts;
    
    auto it = m_reputationHistory.find(address);
    if (it == m_reputationHistory.end() || it->second.size() < 5) {
        return alerts;  // Not enough history
    }
    
    AnomalyAlert alert;
    
    if (m_enabledDetections[AnomalyType::REPUTATION_SPIKE]) {
        int16_t latestScore = it->second.back().score;
        if (DetectReputationSpike(address, latestScore, alert)) {
            alerts.push_back(alert);
        }
    }
    
    if (m_enabledDetections[AnomalyType::REPUTATION_DROP]) {
        int16_t latestScore = it->second.back().score;
        if (DetectReputationDrop(address, latestScore, alert)) {
            alerts.push_back(alert);
        }
    }
    
    if (m_enabledDetections[AnomalyType::REPUTATION_OSCILLATION]) {
        if (DetectReputationOscillation(address, alert)) {
            alerts.push_back(alert);
        }
    }
    
    return alerts;
}

bool AnomalyDetector::DetectReputationSpike(const uint160& address, int16_t newScore,
                                           AnomalyAlert& alert) {
    auto it = m_reputationHistory.find(address);
    if (it == m_reputationHistory.end() || it->second.size() < 5) {
        return false;
    }
    
    const auto& history = it->second;
    
    // Calculate mean and stddev of historical scores
    std::deque<double> scores;
    for (const auto& entry : history) {
        scores.push_back(entry.score);
    }
    
    double mean = CalculateMean(scores);
    double stdDev = CalculateStdDev(scores, mean);
    
    if (stdDev < 1.0) stdDev = 1.0;  // Minimum stddev
    
    double zScore = CalculateZScore(newScore, mean, stdDev);
    
    // Check for positive spike
    if (zScore > m_reputationZScoreThreshold) {
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::REPUTATION_SPIKE;
        alert.primaryAddress = address;
        alert.severity = std::min(1.0, zScore / (m_reputationZScoreThreshold * 2));
        alert.confidence = std::min(1.0, (double)history.size() / m_reputationHistoryWindow);
        alert.description = tfm::format("Unusual reputation spike detected: %d (mean: %.1f, z-score: %.2f)",
                                       newScore, mean, zScore);
        alert.evidence.push_back(tfm::format("New score: %d", newScore));
        alert.evidence.push_back(tfm::format("Historical mean: %.1f", mean));
        alert.evidence.push_back(tfm::format("Standard deviation: %.1f", stdDev));
        alert.evidence.push_back(tfm::format("Z-score: %.2f (threshold: %.2f)", zScore, m_reputationZScoreThreshold));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

bool AnomalyDetector::DetectReputationDrop(const uint160& address, int16_t newScore,
                                          AnomalyAlert& alert) {
    auto it = m_reputationHistory.find(address);
    if (it == m_reputationHistory.end() || it->second.size() < 5) {
        return false;
    }
    
    const auto& history = it->second;
    
    // Calculate mean and stddev
    std::deque<double> scores;
    for (const auto& entry : history) {
        scores.push_back(entry.score);
    }
    
    double mean = CalculateMean(scores);
    double stdDev = CalculateStdDev(scores, mean);
    
    if (stdDev < 1.0) stdDev = 1.0;
    
    double zScore = CalculateZScore(newScore, mean, stdDev);
    
    // Check for negative spike (drop)
    if (zScore < -m_reputationZScoreThreshold) {
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::REPUTATION_DROP;
        alert.primaryAddress = address;
        alert.severity = std::min(1.0, std::abs(zScore) / (m_reputationZScoreThreshold * 2));
        alert.confidence = std::min(1.0, (double)history.size() / m_reputationHistoryWindow);
        alert.description = tfm::format("Unusual reputation drop detected: %d (mean: %.1f, z-score: %.2f)",
                                       newScore, mean, zScore);
        alert.evidence.push_back(tfm::format("New score: %d", newScore));
        alert.evidence.push_back(tfm::format("Historical mean: %.1f", mean));
        alert.evidence.push_back(tfm::format("Standard deviation: %.1f", stdDev));
        alert.evidence.push_back(tfm::format("Z-score: %.2f (threshold: %.2f)", zScore, -m_reputationZScoreThreshold));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

bool AnomalyDetector::DetectReputationOscillation(const uint160& address, AnomalyAlert& alert) {
    auto it = m_reputationHistory.find(address);
    if (it == m_reputationHistory.end() || it->second.size() < 10) {
        return false;
    }
    
    const auto& history = it->second;
    
    // Count direction changes
    int directionChanges = 0;
    int lastDirection = 0;
    
    for (size_t i = 1; i < history.size(); i++) {
        int delta = history[i].score - history[i-1].score;
        int direction = (delta > 0) ? 1 : ((delta < 0) ? -1 : 0);
        
        if (direction != 0 && lastDirection != 0 && direction != lastDirection) {
            directionChanges++;
        }
        
        if (direction != 0) {
            lastDirection = direction;
        }
    }
    
    // Calculate oscillation rate
    double oscillationRate = (double)directionChanges / (history.size() - 1);
    
    // High oscillation rate indicates manipulation
    if (oscillationRate > 0.7) {
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::REPUTATION_OSCILLATION;
        alert.primaryAddress = address;
        alert.severity = std::min(1.0, oscillationRate);
        alert.confidence = std::min(1.0, (double)history.size() / m_reputationHistoryWindow);
        alert.description = tfm::format("Reputation oscillation detected: %.0f%% direction changes",
                                       oscillationRate * 100);
        alert.evidence.push_back(tfm::format("Direction changes: %d", directionChanges));
        alert.evidence.push_back(tfm::format("History length: %zu", history.size()));
        alert.evidence.push_back(tfm::format("Oscillation rate: %.2f", oscillationRate));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}


// ========== Validator Anomaly Detection ==========

void AnomalyDetector::RecordValidatorResponse(const uint160& validator, const uint256& txHash,
                                             ValidationVote vote, double confidence,
                                             double responseTime) {
    LOCK(m_cs);
    
    auto& profile = m_validatorProfiles[validator];
    profile.validatorAddress = validator;
    profile.lastActivityTime = GetCurrentTimestamp();
    
    // Record response time
    profile.responseTimes.push_back(responseTime);
    while (profile.responseTimes.size() > m_validatorHistoryWindow) {
        profile.responseTimes.pop_front();
    }
    
    // Record vote
    int voteValue = (vote == ValidationVote::ACCEPT) ? 1 : 
                   ((vote == ValidationVote::REJECT) ? -1 : 0);
    profile.voteHistory.push_back(voteValue);
    while (profile.voteHistory.size() > m_validatorHistoryWindow) {
        profile.voteHistory.pop_front();
    }
    
    // Record confidence
    profile.confidenceHistory.push_back(confidence);
    while (profile.confidenceHistory.size() > m_validatorHistoryWindow) {
        profile.confidenceHistory.pop_front();
    }
    
    // Update statistics
    profile.UpdateStats();
    
    // Check for anomalies
    AnomalyAlert alert;
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_SLOW_RESPONSE] && 
        DetectSlowResponsePattern(validator, alert)) {
        CreateAlert(alert);
    }
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_ERRATIC_TIMING] && 
        DetectErraticTimingPattern(validator, alert)) {
        CreateAlert(alert);
    }
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_BIAS] && 
        DetectVotingBias(validator, alert)) {
        CreateAlert(alert);
    }
}

std::vector<AnomalyAlert> AnomalyDetector::AnalyzeValidatorBehavior(const uint160& validator) {
    LOCK(m_cs);
    
    std::vector<AnomalyAlert> alerts;
    
    auto it = m_validatorProfiles.find(validator);
    if (it == m_validatorProfiles.end() || it->second.responseTimes.size() < 10) {
        return alerts;
    }
    
    AnomalyAlert alert;
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_SLOW_RESPONSE] && 
        DetectSlowResponsePattern(validator, alert)) {
        alerts.push_back(alert);
    }
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_ERRATIC_TIMING] && 
        DetectErraticTimingPattern(validator, alert)) {
        alerts.push_back(alert);
    }
    
    if (m_enabledDetections[AnomalyType::VALIDATOR_BIAS] && 
        DetectVotingBias(validator, alert)) {
        alerts.push_back(alert);
    }
    
    return alerts;
}

bool AnomalyDetector::DetectSlowResponsePattern(const uint160& validator, AnomalyAlert& alert) {
    auto it = m_validatorProfiles.find(validator);
    if (it == m_validatorProfiles.end() || it->second.responseTimes.size() < 10) {
        return false;
    }
    
    const auto& profile = it->second;
    
    // Count responses above threshold (e.g., 5 seconds)
    const double slowThreshold = 5000.0;  // 5 seconds in ms
    int slowCount = 0;
    
    for (double time : profile.responseTimes) {
        if (time > slowThreshold) {
            slowCount++;
        }
    }
    
    double slowRate = (double)slowCount / profile.responseTimes.size();
    
    if (slowRate > 0.5) {  // More than 50% slow responses
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::VALIDATOR_SLOW_RESPONSE;
        alert.primaryAddress = validator;
        alert.severity = std::min(1.0, slowRate);
        alert.confidence = std::min(1.0, (double)profile.responseTimes.size() / m_validatorHistoryWindow);
        alert.description = tfm::format("Validator showing consistently slow responses: %.0f%% above %.0fms",
                                       slowRate * 100, slowThreshold);
        alert.evidence.push_back(tfm::format("Slow responses: %d/%zu", slowCount, profile.responseTimes.size()));
        alert.evidence.push_back(tfm::format("Average response time: %.0fms", profile.averageResponseTime));
        alert.evidence.push_back(tfm::format("Slow threshold: %.0fms", slowThreshold));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

bool AnomalyDetector::DetectErraticTimingPattern(const uint160& validator, AnomalyAlert& alert) {
    auto it = m_validatorProfiles.find(validator);
    if (it == m_validatorProfiles.end() || it->second.responseTimes.size() < 10) {
        return false;
    }
    
    const auto& profile = it->second;
    
    // Calculate coefficient of variation
    if (profile.averageResponseTime < 1.0) {
        return false;  // Avoid division by zero
    }
    
    double coefficientOfVariation = profile.responseTimeStdDev / profile.averageResponseTime;
    
    if (coefficientOfVariation > 1.5) {  // High variability
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::VALIDATOR_ERRATIC_TIMING;
        alert.primaryAddress = validator;
        alert.severity = std::min(1.0, coefficientOfVariation / 3.0);
        alert.confidence = std::min(1.0, (double)profile.responseTimes.size() / m_validatorHistoryWindow);
        alert.description = tfm::format("Validator showing erratic response timing: CV=%.2f",
                                       coefficientOfVariation);
        alert.evidence.push_back(tfm::format("Average response time: %.0fms", profile.averageResponseTime));
        alert.evidence.push_back(tfm::format("Standard deviation: %.0fms", profile.responseTimeStdDev));
        alert.evidence.push_back(tfm::format("Coefficient of variation: %.2f", coefficientOfVariation));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

bool AnomalyDetector::DetectVotingBias(const uint160& validator, AnomalyAlert& alert) {
    auto it = m_validatorProfiles.find(validator);
    if (it == m_validatorProfiles.end() || it->second.voteHistory.size() < 20) {
        return false;
    }
    
    const auto& profile = it->second;
    
    // Check for extreme bias
    if (profile.acceptRate > 0.95 || profile.rejectRate > 0.95) {
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::VALIDATOR_BIAS;
        alert.primaryAddress = validator;
        alert.severity = std::max(profile.acceptRate, profile.rejectRate);
        alert.confidence = std::min(1.0, (double)profile.voteHistory.size() / m_validatorHistoryWindow);
        
        if (profile.acceptRate > 0.95) {
            alert.description = tfm::format("Validator showing extreme accept bias: %.0f%% accepts",
                                           profile.acceptRate * 100);
        } else {
            alert.description = tfm::format("Validator showing extreme reject bias: %.0f%% rejects",
                                           profile.rejectRate * 100);
        }
        
        alert.evidence.push_back(tfm::format("Accept rate: %.0f%%", profile.acceptRate * 100));
        alert.evidence.push_back(tfm::format("Reject rate: %.0f%%", profile.rejectRate * 100));
        alert.evidence.push_back(tfm::format("Abstain rate: %.0f%%", profile.abstainRate * 100));
        alert.evidence.push_back(tfm::format("Vote history size: %zu", profile.voteHistory.size()));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

// ========== Coordinated Attack Detection ==========

bool AnomalyDetector::DetectCoordinatedVoting(const std::vector<ValidationResponse>& responses,
                                             AnomalyAlert& alert) {
    if (responses.size() < 5) {
        return false;
    }
    
    // Check for identical voting patterns
    std::map<ValidationVote, int> voteCounts;
    for (const auto& response : responses) {
        voteCounts[response.vote]++;
    }
    
    // Check if all validators voted the same way
    for (const auto& pair : voteCounts) {
        double ratio = (double)pair.second / responses.size();
        if (ratio > m_coordinationThreshold && responses.size() >= 10) {
            // Check for timing correlation
            std::vector<int64_t> timestamps;
            for (const auto& response : responses) {
                timestamps.push_back(response.timestamp);
            }
            
            // Sort timestamps
            std::sort(timestamps.begin(), timestamps.end());
            
            // Calculate time spread
            int64_t timeSpread = timestamps.back() - timestamps.front();
            
            // If all responses came within 1 second, suspicious
            if (timeSpread < 1000) {
                alert.alertId = m_nextAlertId++;
                alert.type = AnomalyType::VOTE_MANIPULATION;
                alert.severity = ratio;
                alert.confidence = std::min(1.0, (double)responses.size() / 20.0);
                alert.description = tfm::format("Coordinated voting detected: %.0f%% identical votes within %lldms",
                                               ratio * 100, timeSpread);
                
                for (const auto& response : responses) {
                    alert.relatedAddresses.push_back(response.validatorAddress);
                }
                
                alert.evidence.push_back(tfm::format("Identical vote ratio: %.0f%%", ratio * 100));
                alert.evidence.push_back(tfm::format("Time spread: %lldms", timeSpread));
                alert.evidence.push_back(tfm::format("Validator count: %zu", responses.size()));
                alert.timestamp = GetCurrentTimestamp();
                alert.blockHeight = m_currentBlockHeight;
                
                return true;
            }
        }
    }
    
    return false;
}

bool AnomalyDetector::DetectSybilCluster(const std::vector<uint160>& addresses,
                                        AnomalyAlert& alert) {
    if (addresses.size() < 3) {
        return false;
    }
    
    // Check for behavior similarity across addresses
    std::vector<ValidatorBehaviorProfile*> profiles;
    for (const auto& addr : addresses) {
        auto it = m_validatorProfiles.find(addr);
        if (it != m_validatorProfiles.end() && it->second.voteHistory.size() >= 10) {
            profiles.push_back(&it->second);
        }
    }
    
    if (profiles.size() < 3) {
        return false;
    }
    
    // Calculate pairwise similarity
    int similarPairs = 0;
    int totalPairs = 0;
    
    for (size_t i = 0; i < profiles.size(); i++) {
        for (size_t j = i + 1; j < profiles.size(); j++) {
            totalPairs++;
            
            // Compare accept rates
            double acceptDiff = std::abs(profiles[i]->acceptRate - profiles[j]->acceptRate);
            double rejectDiff = std::abs(profiles[i]->rejectRate - profiles[j]->rejectRate);
            
            if (acceptDiff < 0.1 && rejectDiff < 0.1) {
                similarPairs++;
            }
        }
    }
    
    double similarityRate = (double)similarPairs / totalPairs;
    
    if (similarityRate > 0.8) {
        alert.alertId = m_nextAlertId++;
        alert.type = AnomalyType::SYBIL_CLUSTER;
        alert.primaryAddress = addresses[0];
        alert.relatedAddresses = addresses;
        alert.severity = similarityRate;
        alert.confidence = std::min(1.0, (double)profiles.size() / 10.0);
        alert.description = tfm::format("Potential Sybil cluster detected: %zu addresses with %.0f%% behavior similarity",
                                       addresses.size(), similarityRate * 100);
        alert.evidence.push_back(tfm::format("Cluster size: %zu", addresses.size()));
        alert.evidence.push_back(tfm::format("Similar pairs: %d/%d", similarPairs, totalPairs));
        alert.evidence.push_back(tfm::format("Similarity rate: %.0f%%", similarityRate * 100));
        alert.timestamp = GetCurrentTimestamp();
        alert.blockHeight = m_currentBlockHeight;
        
        return true;
    }
    
    return false;
}

std::vector<AnomalyAlert> AnomalyDetector::AnalyzeTransactionForAttack(
    const uint256& txHash,
    const std::vector<ValidationResponse>& responses) {
    
    LOCK(m_cs);
    
    std::vector<AnomalyAlert> alerts;
    AnomalyAlert alert;
    
    if (m_enabledDetections[AnomalyType::VOTE_MANIPULATION] && 
        DetectCoordinatedVoting(responses, alert)) {
        alert.description = tfm::format("Transaction %s: %s", 
                                       txHash.GetHex().substr(0, 16), alert.description);
        alerts.push_back(alert);
    }
    
    // Extract validator addresses
    std::vector<uint160> validators;
    for (const auto& response : responses) {
        validators.push_back(response.validatorAddress);
    }
    
    if (m_enabledDetections[AnomalyType::SYBIL_CLUSTER] && 
        DetectSybilCluster(validators, alert)) {
        alert.description = tfm::format("Transaction %s: %s", 
                                       txHash.GetHex().substr(0, 16), alert.description);
        alerts.push_back(alert);
    }
    
    return alerts;
}


// ========== Alert Management ==========

std::vector<AnomalyAlert> AnomalyDetector::GetActiveAlerts() {
    LOCK(m_cs);
    
    std::vector<AnomalyAlert> result;
    for (const auto& alert : m_activeAlerts) {
        if (!alert.resolved) {
            result.push_back(alert);
        }
    }
    
    return result;
}

std::vector<AnomalyAlert> AnomalyDetector::GetAlertsForAddress(const uint160& address) {
    LOCK(m_cs);
    
    std::vector<AnomalyAlert> result;
    for (const auto& alert : m_activeAlerts) {
        if (alert.primaryAddress == address) {
            result.push_back(alert);
            continue;
        }
        
        for (const auto& related : alert.relatedAddresses) {
            if (related == address) {
                result.push_back(alert);
                break;
            }
        }
    }
    
    return result;
}

bool AnomalyDetector::AcknowledgeAlert(uint64_t alertId) {
    LOCK(m_cs);
    
    for (auto& alert : m_activeAlerts) {
        if (alert.alertId == alertId) {
            alert.acknowledged = true;
            PersistAlert(alert);
            return true;
        }
    }
    
    return false;
}

bool AnomalyDetector::ResolveAlert(uint64_t alertId, const std::string& resolution) {
    LOCK(m_cs);
    
    for (auto& alert : m_activeAlerts) {
        if (alert.alertId == alertId) {
            alert.resolved = true;
            alert.evidence.push_back(tfm::format("Resolution: %s", resolution));
            PersistAlert(alert);
            return true;
        }
    }
    
    return false;
}

AnomalyAlert AnomalyDetector::GetAlert(uint64_t alertId) {
    LOCK(m_cs);
    
    for (const auto& alert : m_activeAlerts) {
        if (alert.alertId == alertId) {
            return alert;
        }
    }
    
    return AnomalyAlert();
}

// ========== Configuration ==========

void AnomalyDetector::SetThresholds(double reputationZScore, double validatorZScore,
                                   double coordinationThreshold) {
    LOCK(m_cs);
    
    m_reputationZScoreThreshold = reputationZScore;
    m_validatorZScoreThreshold = validatorZScore;
    m_coordinationThreshold = coordinationThreshold;
    
    LogPrint(BCLog::CVM, "Anomaly detector thresholds updated: reputation=%.2f, validator=%.2f, coordination=%.2f\n",
             reputationZScore, validatorZScore, coordinationThreshold);
}

void AnomalyDetector::SetHistoryWindowSize(size_t reputationWindow, size_t validatorWindow) {
    LOCK(m_cs);
    
    m_reputationHistoryWindow = reputationWindow;
    m_validatorHistoryWindow = validatorWindow;
    
    LogPrint(BCLog::CVM, "Anomaly detector history windows updated: reputation=%zu, validator=%zu\n",
             reputationWindow, validatorWindow);
}

void AnomalyDetector::EnableDetection(AnomalyType type, bool enabled) {
    LOCK(m_cs);
    m_enabledDetections[type] = enabled;
}

// ========== Internal Methods ==========

void AnomalyDetector::CreateAlert(const AnomalyAlert& alert) {
    m_activeAlerts.push_back(alert);
    
    // Trim old alerts (keep last 1000)
    while (m_activeAlerts.size() > 1000) {
        m_activeAlerts.pop_front();
    }
    
    // Persist to database
    PersistAlert(alert);
    
    // Log to security audit if available
    if (m_auditLogger) {
        AnomalyDetectionResult result;
        result.address = alert.primaryAddress;
        result.anomalyType = tfm::format("%d", static_cast<int>(alert.type));
        result.anomalyScore = alert.severity;
        result.threshold = m_reputationZScoreThreshold;
        result.isAnomaly = true;
        result.description = alert.description;
        result.indicators = alert.evidence;
        result.timestamp = alert.timestamp;
        result.blockHeight = alert.blockHeight;
        
        m_auditLogger->LogAnomaly(result);
    }
    
    LogPrint(BCLog::CVM, "Anomaly alert created: %s (severity: %.2f)\n",
             alert.description, alert.severity);
}

double AnomalyDetector::CalculateMean(const std::deque<double>& values) {
    if (values.empty()) return 0;
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / values.size();
}

double AnomalyDetector::CalculateStdDev(const std::deque<double>& values, double mean) {
    if (values.size() < 2) return 0;
    
    double variance = 0;
    for (double val : values) {
        variance += (val - mean) * (val - mean);
    }
    variance /= values.size();
    
    return std::sqrt(variance);
}

double AnomalyDetector::CalculateZScore(double value, double mean, double stdDev) {
    if (stdDev < 0.001) return 0;
    return (value - mean) / stdDev;
}

int64_t AnomalyDetector::GetCurrentTimestamp() {
    return GetTimeMillis();
}

void AnomalyDetector::PersistAlert(const AnomalyAlert& alert) {
    std::string key = tfm::format("%c%020llu", DB_ANOMALY_ALERT, alert.alertId);
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << alert;
    m_db.WriteGeneric(key, std::vector<uint8_t>(ss.begin(), ss.end()));
}

void AnomalyDetector::LoadActiveAlerts() {
    // Load alerts from database
    std::vector<std::string> keys = m_db.ListKeysWithPrefix(std::string(1, DB_ANOMALY_ALERT));
    
    for (const auto& key : keys) {
        std::vector<uint8_t> data;
        if (m_db.ReadGeneric(key, data)) {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            AnomalyAlert alert;
            ss >> alert;
            
            if (!alert.resolved) {
                m_activeAlerts.push_back(alert);
                
                // Update next alert ID
                if (alert.alertId >= m_nextAlertId) {
                    m_nextAlertId = alert.alertId + 1;
                }
            }
        }
    }
    
    LogPrint(BCLog::CVM, "Loaded %zu active anomaly alerts\n", m_activeAlerts.size());
}

// ========== Global Functions ==========

bool InitAnomalyDetector(CVMDatabase& db, SecurityAuditLogger* auditLogger, int currentBlockHeight) {
    g_anomalyDetector = std::make_unique<AnomalyDetector>(db, auditLogger);
    return g_anomalyDetector->Initialize(currentBlockHeight);
}

void ShutdownAnomalyDetector() {
    g_anomalyDetector.reset();
}

} // namespace CVM
