// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file collusion_detector.cpp
 * @brief Implementation of Anti-Collusion Detection System for Cascoin L2
 * 
 * Requirements: 22.1, 22.2, 22.4
 */

#include <l2/collusion_detector.h>
#include <l2/sequencer_discovery.h>
#include <hash.h>
#include <util.h>

#include <cmath>
#include <algorithm>
#include <numeric>

namespace l2 {

// Global instance
static std::unique_ptr<CollusionDetector> g_collusion_detector;
static bool g_collusion_detector_initialized = false;

CollusionDetector::CollusionDetector(uint64_t chainId)
    : chainId_(chainId)
    , timingCorrelationThreshold_(0.8)
    , votingCorrelationThreshold_(0.9)
    , stakeConcentrationLimit_(0.2)
{
}

// ============================================================================
// Timing Correlation Detection
// ============================================================================

void CollusionDetector::RecordSequencerAction(const SequencerAction& action)
{
    LOCK(cs_collusion_);
    
    auto& actions = sequencerActions_[action.sequencerAddress];
    actions.push_back(action);
    
    // Prune if too many actions
    while (actions.size() > MAX_ACTIONS_PER_SEQUENCER) {
        actions.pop_front();
    }
    
    // Invalidate timing correlation cache for this sequencer
    for (auto it = timingCorrelationCache_.begin(); it != timingCorrelationCache_.end(); ) {
        if (it->first.first == action.sequencerAddress || 
            it->first.second == action.sequencerAddress) {
            it = timingCorrelationCache_.erase(it);
        } else {
            ++it;
        }
    }
}

TimingCorrelationStats CollusionDetector::AnalyzeTimingCorrelation(
    const uint160& seq1,
    const uint160& seq2) const
{
    LOCK(cs_collusion_);
    
    auto key = MakeOrderedPair(seq1, seq2);
    
    // Check cache
    auto cacheIt = timingCorrelationCache_.find(key);
    if (cacheIt != timingCorrelationCache_.end()) {
        return cacheIt->second;
    }
    
    TimingCorrelationStats stats;
    stats.sequencer1 = key.first;
    stats.sequencer2 = key.second;
    stats.lastUpdated = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    auto it1 = sequencerActions_.find(seq1);
    auto it2 = sequencerActions_.find(seq2);
    
    if (it1 == sequencerActions_.end() || it2 == sequencerActions_.end()) {
        return stats;
    }
    
    const auto& actions1 = it1->second;
    const auto& actions2 = it2->second;
    
    if (actions1.size() < MIN_SAMPLES_FOR_CORRELATION || 
        actions2.size() < MIN_SAMPLES_FOR_CORRELATION) {
        return stats;
    }
    
    stats.correlationScore = CalculateTimingCorrelationScore(actions1, actions2);
    stats.sampleCount = std::min(actions1.size(), actions2.size());
    
    // Calculate average and std dev of time deltas
    std::vector<double> timeDeltas;
    for (const auto& a1 : actions1) {
        for (const auto& a2 : actions2) {
            if (a1.blockHash == a2.blockHash) {
                double delta = std::abs(static_cast<double>(a1.timestamp) - 
                                       static_cast<double>(a2.timestamp));
                timeDeltas.push_back(delta);
            }
        }
    }
    
    if (!timeDeltas.empty()) {
        double sum = std::accumulate(timeDeltas.begin(), timeDeltas.end(), 0.0);
        stats.avgTimeDelta = sum / timeDeltas.size();
        
        double sqSum = 0.0;
        for (double d : timeDeltas) {
            sqSum += (d - stats.avgTimeDelta) * (d - stats.avgTimeDelta);
        }
        stats.stdDevTimeDelta = std::sqrt(sqSum / timeDeltas.size());
    }
    
    // Cache the result
    timingCorrelationCache_[key] = stats;
    
    return stats;
}

std::vector<std::pair<uint160, uint160>> CollusionDetector::DetectTimingCorrelation() const
{
    LOCK(cs_collusion_);
    
    std::vector<std::pair<uint160, uint160>> correlatedPairs;
    
    // Get all sequencer addresses
    std::vector<uint160> sequencers;
    for (const auto& pair : sequencerActions_) {
        sequencers.push_back(pair.first);
    }
    
    // Check all pairs
    for (size_t i = 0; i < sequencers.size(); ++i) {
        for (size_t j = i + 1; j < sequencers.size(); ++j) {
            TimingCorrelationStats stats = AnalyzeTimingCorrelation(
                sequencers[i], sequencers[j]);
            
            if (stats.correlationScore >= timingCorrelationThreshold_) {
                correlatedPairs.emplace_back(sequencers[i], sequencers[j]);
            }
        }
    }
    
    return correlatedPairs;
}

double CollusionDetector::CalculateTimingCorrelationScore(
    const std::deque<SequencerAction>& actions1,
    const std::deque<SequencerAction>& actions2) const
{
    // Find actions on the same blocks
    std::vector<std::pair<uint64_t, uint64_t>> matchedTimestamps;
    
    for (const auto& a1 : actions1) {
        for (const auto& a2 : actions2) {
            if (a1.blockHash == a2.blockHash) {
                matchedTimestamps.emplace_back(a1.timestamp, a2.timestamp);
            }
        }
    }
    
    if (matchedTimestamps.size() < MIN_SAMPLES_FOR_CORRELATION) {
        return 0.0;
    }
    
    // Calculate Pearson correlation coefficient
    double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    size_t n = matchedTimestamps.size();
    
    for (const auto& pair : matchedTimestamps) {
        double x = static_cast<double>(pair.first);
        double y = static_cast<double>(pair.second);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        sumY2 += y * y;
    }
    
    double numerator = n * sumXY - sumX * sumY;
    double denominator = std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
    
    if (denominator == 0) {
        return 0.0;
    }
    
    return numerator / denominator;
}

// ============================================================================
// Voting Pattern Analysis
// ============================================================================

void CollusionDetector::RecordVote(const uint256& blockHash, const uint160& voter, VoteType vote)
{
    LOCK(cs_collusion_);
    
    votingRecords_[blockHash][voter] = vote;
    
    // Prune old records if needed
    if (votingRecords_.size() > MAX_VOTING_RECORDS) {
        PruneOldVotingRecords();
    }
    
    // Invalidate voting pattern cache for this voter
    for (auto it = votingPatternCache_.begin(); it != votingPatternCache_.end(); ) {
        if (it->first.first == voter || it->first.second == voter) {
            it = votingPatternCache_.erase(it);
        } else {
            ++it;
        }
    }
}

VotingPatternStats CollusionDetector::AnalyzeVotingPattern(
    const uint160& seq1,
    const uint160& seq2) const
{
    LOCK(cs_collusion_);
    
    auto key = MakeOrderedPair(seq1, seq2);
    
    // Check cache
    auto cacheIt = votingPatternCache_.find(key);
    if (cacheIt != votingPatternCache_.end()) {
        return cacheIt->second;
    }
    
    VotingPatternStats stats;
    stats.sequencer1 = key.first;
    stats.sequencer2 = key.second;
    stats.lastUpdated = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Analyze voting records
    for (const auto& blockVotes : votingRecords_) {
        const auto& votes = blockVotes.second;
        
        auto it1 = votes.find(seq1);
        auto it2 = votes.find(seq2);
        
        if (it1 != votes.end() && it2 != votes.end()) {
            stats.totalVotesCounted++;
            
            if (it1->second == it2->second) {
                stats.matchingVotes++;
            } else {
                stats.opposingVotes++;
            }
        }
    }
    
    stats.UpdateCorrelation();
    
    // Cache the result
    votingPatternCache_[key] = stats;
    
    return stats;
}

std::vector<std::pair<uint160, uint160>> CollusionDetector::DetectVotingPatternCollusion() const
{
    LOCK(cs_collusion_);
    
    std::vector<std::pair<uint160, uint160>> colludingPairs;
    
    // Get all voters
    std::set<uint160> voters;
    for (const auto& blockVotes : votingRecords_) {
        for (const auto& vote : blockVotes.second) {
            voters.insert(vote.first);
        }
    }
    
    std::vector<uint160> voterList(voters.begin(), voters.end());
    
    // Check all pairs
    for (size_t i = 0; i < voterList.size(); ++i) {
        for (size_t j = i + 1; j < voterList.size(); ++j) {
            VotingPatternStats stats = AnalyzeVotingPattern(voterList[i], voterList[j]);
            
            if (stats.totalVotesCounted >= MIN_SAMPLES_FOR_CORRELATION &&
                stats.correlationScore >= votingCorrelationThreshold_) {
                colludingPairs.emplace_back(voterList[i], voterList[j]);
            }
        }
    }
    
    return colludingPairs;
}

double CollusionDetector::CalculateVotingCorrelationScore(
    const uint160& seq1,
    const uint160& seq2) const
{
    VotingPatternStats stats = AnalyzeVotingPattern(seq1, seq2);
    return stats.correlationScore;
}

// ============================================================================
// Wallet Cluster Integration
// ============================================================================

bool CollusionDetector::AreInSameWalletCluster(const uint160& seq1, const uint160& seq2) const
{
    LOCK(cs_collusion_);
    
    // Check test data first
    if (!testWalletClusters_.empty()) {
        auto it1 = testWalletClusters_.find(seq1);
        auto it2 = testWalletClusters_.find(seq2);
        
        if (it1 != testWalletClusters_.end() && it2 != testWalletClusters_.end()) {
            return it1->second == it2->second;
        }
        return false;
    }
    
    // In production, would call CVM::WalletClusterer
    // For now, return false (different clusters)
    return false;
}

uint160 CollusionDetector::GetWalletCluster(const uint160& sequencer) const
{
    LOCK(cs_collusion_);
    
    // Check test data first
    if (!testWalletClusters_.empty()) {
        auto it = testWalletClusters_.find(sequencer);
        if (it != testWalletClusters_.end()) {
            return it->second;
        }
    }
    
    // Default: each address is its own cluster
    return sequencer;
}

std::map<uint160, std::vector<uint160>> CollusionDetector::DetectWalletClusterViolations() const
{
    LOCK(cs_collusion_);
    
    std::map<uint160, std::vector<uint160>> violations;
    
    // Get all sequencers
    std::vector<uint160> sequencers;
    for (const auto& pair : sequencerActions_) {
        sequencers.push_back(pair.first);
    }
    
    // Group by cluster
    std::map<uint160, std::vector<uint160>> clusterMembers;
    for (const auto& seq : sequencers) {
        uint160 cluster = GetWalletCluster(seq);
        clusterMembers[cluster].push_back(seq);
    }
    
    // Find clusters with multiple sequencers
    for (const auto& pair : clusterMembers) {
        if (pair.second.size() > 1) {
            violations[pair.first] = pair.second;
        }
    }
    
    return violations;
}

bool CollusionDetector::ValidateNewSequencerCluster(
    const uint160& newSequencer,
    const std::vector<uint160>& existingSequencers) const
{
    uint160 newCluster = GetWalletCluster(newSequencer);
    
    for (const auto& existing : existingSequencers) {
        if (GetWalletCluster(existing) == newCluster) {
            return false;  // Same cluster as existing sequencer
        }
    }
    
    return true;
}

// ============================================================================
// Stake Concentration Monitoring
// ============================================================================

double CollusionDetector::CalculateStakeConcentration(const uint160& sequencer) const
{
    LOCK(cs_collusion_);
    
    CAmount totalStake = GetTotalSequencerStake();
    if (totalStake == 0) {
        return 0.0;
    }
    
    uint160 cluster = GetWalletCluster(sequencer);
    CAmount clusterStake = 0;
    
    // Sum stake of all addresses in the same cluster
    for (const auto& pair : sequencerActions_) {
        if (GetWalletCluster(pair.first) == cluster) {
            clusterStake += GetSequencerStake(pair.first);
        }
    }
    
    return static_cast<double>(clusterStake) / static_cast<double>(totalStake);
}

bool CollusionDetector::ExceedsStakeConcentrationLimit(const uint160& sequencer) const
{
    return CalculateStakeConcentration(sequencer) > stakeConcentrationLimit_;
}

std::map<uint160, double> CollusionDetector::GetStakeConcentrationViolations() const
{
    LOCK(cs_collusion_);
    
    std::map<uint160, double> violations;
    std::set<uint160> checkedClusters;
    
    for (const auto& pair : sequencerActions_) {
        uint160 cluster = GetWalletCluster(pair.first);
        
        if (checkedClusters.find(cluster) != checkedClusters.end()) {
            continue;
        }
        checkedClusters.insert(cluster);
        
        double concentration = CalculateStakeConcentration(pair.first);
        if (concentration > stakeConcentrationLimit_) {
            violations[cluster] = concentration;
        }
    }
    
    return violations;
}


// ============================================================================
// Comprehensive Collusion Detection
// ============================================================================

std::vector<CollusionDetectionResult> CollusionDetector::RunFullDetection()
{
    LOCK(cs_collusion_);
    
    std::vector<CollusionDetectionResult> results;
    
    // Get all sequencers
    std::vector<uint160> sequencers;
    for (const auto& pair : sequencerActions_) {
        sequencers.push_back(pair.first);
    }
    
    // Check all pairs
    for (size_t i = 0; i < sequencers.size(); ++i) {
        for (size_t j = i + 1; j < sequencers.size(); ++j) {
            CollusionDetectionResult result = AnalyzeSequencerPair(
                sequencers[i], sequencers[j]);
            
            if (result.IsCollusionDetected()) {
                results.push_back(result);
                NotifyAlertCallbacks(result);
            }
        }
    }
    
    // Check stake concentration violations
    auto stakeViolations = GetStakeConcentrationViolations();
    for (const auto& violation : stakeViolations) {
        CollusionDetectionResult result;
        result.type = CollusionType::STAKE_CONCENTRATION;
        result.severity = violation.second > 0.3 ? CollusionSeverity::HIGH : CollusionSeverity::MEDIUM;
        result.stakeConcentration = violation.second;
        result.confidenceScore = violation.second;  // Higher concentration = higher confidence
        result.description = "Stake concentration exceeds limit";
        result.detectionTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // Find all sequencers in this cluster
        for (const auto& seq : sequencers) {
            if (GetWalletCluster(seq) == violation.first) {
                result.involvedSequencers.push_back(seq);
            }
        }
        
        result.evidenceHash = GenerateEvidenceHash(result);
        results.push_back(result);
        NotifyAlertCallbacks(result);
    }
    
    // Store detected collusions
    detectedCollusions_ = results;
    
    return results;
}

CollusionDetectionResult CollusionDetector::AnalyzeSequencerPair(
    const uint160& seq1,
    const uint160& seq2)
{
    CollusionDetectionResult result;
    result.involvedSequencers.push_back(seq1);
    result.involvedSequencers.push_back(seq2);
    result.detectionTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Check timing correlation
    TimingCorrelationStats timingStats = AnalyzeTimingCorrelation(seq1, seq2);
    result.timingCorrelation = timingStats.correlationScore;
    
    // Check voting pattern
    VotingPatternStats votingStats = AnalyzeVotingPattern(seq1, seq2);
    result.votingCorrelation = votingStats.correlationScore;
    
    // Check wallet cluster
    result.sameWalletCluster = AreInSameWalletCluster(seq1, seq2);
    
    // Check stake concentration (use max of both)
    double stake1 = CalculateStakeConcentration(seq1);
    double stake2 = CalculateStakeConcentration(seq2);
    result.stakeConcentration = std::max(stake1, stake2);
    
    // Determine collusion type and severity
    bool hasTimingCollusion = result.timingCorrelation >= timingCorrelationThreshold_;
    bool hasVotingCollusion = votingStats.totalVotesCounted >= MIN_SAMPLES_FOR_CORRELATION &&
                             result.votingCorrelation >= votingCorrelationThreshold_;
    bool hasClusterViolation = result.sameWalletCluster;
    bool hasStakeViolation = result.stakeConcentration > stakeConcentrationLimit_;
    
    int indicators = (hasTimingCollusion ? 1 : 0) + 
                    (hasVotingCollusion ? 1 : 0) + 
                    (hasClusterViolation ? 1 : 0) +
                    (hasStakeViolation ? 1 : 0);
    
    if (indicators == 0) {
        result.type = CollusionType::NONE;
        result.confidenceScore = 0.0;
        return result;
    }
    
    // Determine type
    if (indicators >= 2) {
        result.type = CollusionType::COMBINED;
    } else if (hasClusterViolation) {
        result.type = CollusionType::WALLET_CLUSTER;
    } else if (hasVotingCollusion) {
        result.type = CollusionType::VOTING_PATTERN;
    } else if (hasTimingCollusion) {
        result.type = CollusionType::TIMING_CORRELATION;
    } else {
        result.type = CollusionType::STAKE_CONCENTRATION;
    }
    
    // Determine severity
    result.severity = DetermineSeverity(
        result.timingCorrelation,
        result.votingCorrelation,
        result.sameWalletCluster,
        result.stakeConcentration);
    
    // Calculate confidence score
    result.confidenceScore = static_cast<double>(indicators) / 4.0;
    if (result.sameWalletCluster) {
        result.confidenceScore = std::max(result.confidenceScore, 0.9);  // Wallet cluster is strong evidence
    }
    
    // Generate description
    std::string desc = "Collusion detected: ";
    if (hasTimingCollusion) desc += "timing correlation (" + std::to_string(result.timingCorrelation) + "), ";
    if (hasVotingCollusion) desc += "voting pattern (" + std::to_string(result.votingCorrelation) + "), ";
    if (hasClusterViolation) desc += "same wallet cluster, ";
    if (hasStakeViolation) desc += "stake concentration (" + std::to_string(result.stakeConcentration * 100) + "%), ";
    result.description = desc.substr(0, desc.length() - 2);  // Remove trailing ", "
    
    result.evidenceHash = GenerateEvidenceHash(result);
    
    return result;
}

double CollusionDetector::GetCollusionRiskScore(const uint160& sequencer) const
{
    LOCK(cs_collusion_);
    
    double maxRisk = 0.0;
    
    // Check against all other sequencers
    for (const auto& pair : sequencerActions_) {
        if (pair.first == sequencer) continue;
        
        // Timing correlation
        TimingCorrelationStats timingStats = AnalyzeTimingCorrelation(sequencer, pair.first);
        maxRisk = std::max(maxRisk, timingStats.correlationScore);
        
        // Voting correlation
        VotingPatternStats votingStats = AnalyzeVotingPattern(sequencer, pair.first);
        if (votingStats.totalVotesCounted >= MIN_SAMPLES_FOR_CORRELATION) {
            maxRisk = std::max(maxRisk, votingStats.correlationScore);
        }
        
        // Wallet cluster
        if (AreInSameWalletCluster(sequencer, pair.first)) {
            maxRisk = std::max(maxRisk, 0.95);
        }
    }
    
    // Stake concentration
    double stakeConc = CalculateStakeConcentration(sequencer);
    if (stakeConc > stakeConcentrationLimit_) {
        maxRisk = std::max(maxRisk, stakeConc);
    }
    
    return maxRisk;
}

// ============================================================================
// Whistleblower System
// ============================================================================

bool CollusionDetector::SubmitWhistleblowerReport(const WhistleblowerReport& report)
{
    LOCK(cs_collusion_);
    
    // Validate report
    if (report.accusedSequencers.empty()) {
        return false;
    }
    
    if (report.bondAmount < WHISTLEBLOWER_BOND) {
        return false;
    }
    
    // Store report
    uint256 reportId = report.GetSigningHash();
    whistleblowerReports_[reportId] = report;
    
    return true;
}

bool CollusionDetector::ValidateWhistleblowerReport(const uint256& reportId)
{
    LOCK(cs_collusion_);
    
    auto it = whistleblowerReports_.find(reportId);
    if (it == whistleblowerReports_.end()) {
        return false;
    }
    
    WhistleblowerReport& report = it->second;
    
    // Run detection on accused sequencers
    bool collusionFound = false;
    
    for (size_t i = 0; i < report.accusedSequencers.size(); ++i) {
        for (size_t j = i + 1; j < report.accusedSequencers.size(); ++j) {
            CollusionDetectionResult result = AnalyzeSequencerPair(
                report.accusedSequencers[i],
                report.accusedSequencers[j]);
            
            if (result.IsCollusionDetected()) {
                collusionFound = true;
                break;
            }
        }
        if (collusionFound) break;
    }
    
    report.isValidated = collusionFound;
    return collusionFound;
}

std::vector<WhistleblowerReport> CollusionDetector::GetPendingReports() const
{
    LOCK(cs_collusion_);
    
    std::vector<WhistleblowerReport> pending;
    for (const auto& pair : whistleblowerReports_) {
        if (!pair.second.isValidated && !pair.second.isRewarded) {
            pending.push_back(pair.second);
        }
    }
    return pending;
}

CAmount CollusionDetector::ProcessWhistleblowerReward(const uint256& reportId)
{
    LOCK(cs_collusion_);
    
    auto it = whistleblowerReports_.find(reportId);
    if (it == whistleblowerReports_.end()) {
        return 0;
    }
    
    WhistleblowerReport& report = it->second;
    
    if (!report.isValidated || report.isRewarded) {
        return 0;
    }
    
    // Calculate reward (10% of slashed amount)
    CAmount slashedAmount = 0;
    for (const auto& seq : report.accusedSequencers) {
        slashedAmount += GetSequencerStake(seq);
    }
    
    CAmount reward = static_cast<CAmount>(slashedAmount * WHISTLEBLOWER_REWARD_PERCENT);
    reward += report.bondAmount;  // Return bond
    
    report.isRewarded = true;
    
    // Notify callbacks
    for (const auto& callback : rewardCallbacks_) {
        callback(report.reporterAddress, reward);
    }
    
    return reward;
}

// ============================================================================
// Slashing and Penalties
// ============================================================================

bool CollusionDetector::SlashColludingSequencers(const CollusionDetectionResult& result)
{
    if (!result.IsCollusionDetected()) {
        return false;
    }
    
    CAmount slashAmount = GetSlashingAmount(result.type, result.severity);
    
    // In production, would call sequencer staking contract to slash
    // For now, just return success
    return slashAmount > 0;
}

CAmount CollusionDetector::GetSlashingAmount(CollusionType type, CollusionSeverity severity) const
{
    // Base amounts by type
    CAmount baseAmount = 0;
    switch (type) {
        case CollusionType::TIMING_CORRELATION:
            baseAmount = 10 * COIN;
            break;
        case CollusionType::VOTING_PATTERN:
            baseAmount = 20 * COIN;
            break;
        case CollusionType::WALLET_CLUSTER:
            baseAmount = 50 * COIN;
            break;
        case CollusionType::STAKE_CONCENTRATION:
            baseAmount = 30 * COIN;
            break;
        case CollusionType::COMBINED:
            baseAmount = 100 * COIN;
            break;
        default:
            return 0;
    }
    
    // Multiply by severity
    double multiplier = 1.0;
    switch (severity) {
        case CollusionSeverity::LOW:
            multiplier = 0.5;
            break;
        case CollusionSeverity::MEDIUM:
            multiplier = 1.0;
            break;
        case CollusionSeverity::HIGH:
            multiplier = 2.0;
            break;
        case CollusionSeverity::CRITICAL:
            multiplier = 5.0;
            break;
    }
    
    return static_cast<CAmount>(baseAmount * multiplier);
}

// ============================================================================
// Configuration and Callbacks
// ============================================================================

void CollusionDetector::RegisterAlertCallback(CollusionAlertCallback callback)
{
    LOCK(cs_collusion_);
    alertCallbacks_.push_back(callback);
}

void CollusionDetector::RegisterRewardCallback(WhistleblowerRewardCallback callback)
{
    LOCK(cs_collusion_);
    rewardCallbacks_.push_back(callback);
}

void CollusionDetector::SetTimingCorrelationThreshold(double threshold)
{
    LOCK(cs_collusion_);
    timingCorrelationThreshold_ = std::max(0.0, std::min(1.0, threshold));
}

void CollusionDetector::SetVotingCorrelationThreshold(double threshold)
{
    LOCK(cs_collusion_);
    votingCorrelationThreshold_ = std::max(-1.0, std::min(1.0, threshold));
}

void CollusionDetector::SetStakeConcentrationLimit(double limit)
{
    LOCK(cs_collusion_);
    stakeConcentrationLimit_ = std::max(0.0, std::min(1.0, limit));
}

void CollusionDetector::Clear()
{
    LOCK(cs_collusion_);
    sequencerActions_.clear();
    votingRecords_.clear();
    timingCorrelationCache_.clear();
    votingPatternCache_.clear();
    whistleblowerReports_.clear();
    detectedCollusions_.clear();
    testSequencerStakes_.clear();
    testWalletClusters_.clear();
}

void CollusionDetector::SetTestSequencerStake(const uint160& address, CAmount stake)
{
    LOCK(cs_collusion_);
    testSequencerStakes_[address] = stake;
}

void CollusionDetector::SetTestWalletCluster(const uint160& address, const uint160& clusterId)
{
    LOCK(cs_collusion_);
    testWalletClusters_[address] = clusterId;
}

void CollusionDetector::ClearTestData()
{
    LOCK(cs_collusion_);
    testSequencerStakes_.clear();
    testWalletClusters_.clear();
}

// ============================================================================
// Private Helper Methods
// ============================================================================

CollusionSeverity CollusionDetector::DetermineSeverity(
    double timingCorr,
    double votingCorr,
    bool sameCluster,
    double stakeConc) const
{
    int criticalIndicators = 0;
    int highIndicators = 0;
    int mediumIndicators = 0;
    
    // Timing correlation
    if (timingCorr >= 0.95) criticalIndicators++;
    else if (timingCorr >= 0.9) highIndicators++;
    else if (timingCorr >= timingCorrelationThreshold_) mediumIndicators++;
    
    // Voting correlation
    if (votingCorr >= 0.98) criticalIndicators++;
    else if (votingCorr >= 0.95) highIndicators++;
    else if (votingCorr >= votingCorrelationThreshold_) mediumIndicators++;
    
    // Wallet cluster (always high severity)
    if (sameCluster) highIndicators++;
    
    // Stake concentration
    if (stakeConc >= 0.4) criticalIndicators++;
    else if (stakeConc >= 0.3) highIndicators++;
    else if (stakeConc > stakeConcentrationLimit_) mediumIndicators++;
    
    if (criticalIndicators >= 2 || (criticalIndicators >= 1 && highIndicators >= 1)) {
        return CollusionSeverity::CRITICAL;
    }
    if (highIndicators >= 2 || (highIndicators >= 1 && mediumIndicators >= 1)) {
        return CollusionSeverity::HIGH;
    }
    if (mediumIndicators >= 2 || highIndicators >= 1) {
        return CollusionSeverity::MEDIUM;
    }
    return CollusionSeverity::LOW;
}

uint256 CollusionDetector::GenerateEvidenceHash(const CollusionDetectionResult& result) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << static_cast<uint8_t>(result.type);
    ss << static_cast<uint8_t>(result.severity);
    for (const auto& seq : result.involvedSequencers) {
        ss << seq;
    }
    ss << result.detectionTimestamp;
    // Serialize doubles as integers
    ss << static_cast<uint64_t>(result.timingCorrelation * 1000000);
    ss << static_cast<uint64_t>(result.votingCorrelation * 1000000);
    ss << result.sameWalletCluster;
    ss << static_cast<uint64_t>(result.stakeConcentration * 1000000);
    return ss.GetHash();
}

void CollusionDetector::NotifyAlertCallbacks(const CollusionDetectionResult& result)
{
    for (const auto& callback : alertCallbacks_) {
        callback(result);
    }
}

void CollusionDetector::PruneOldActions()
{
    // Keep only recent actions
    for (auto& pair : sequencerActions_) {
        while (pair.second.size() > MAX_ACTIONS_PER_SEQUENCER) {
            pair.second.pop_front();
        }
    }
}

void CollusionDetector::PruneOldVotingRecords()
{
    // Remove oldest records until under limit
    while (votingRecords_.size() > MAX_VOTING_RECORDS) {
        votingRecords_.erase(votingRecords_.begin());
    }
}

CAmount CollusionDetector::GetSequencerStake(const uint160& address) const
{
    // Check test data first
    auto it = testSequencerStakes_.find(address);
    if (it != testSequencerStakes_.end()) {
        return it->second;
    }
    
    // In production, would query SequencerDiscovery
    // For now, return default stake
    return 100 * COIN;
}

CAmount CollusionDetector::GetTotalSequencerStake() const
{
    CAmount total = 0;
    
    if (!testSequencerStakes_.empty()) {
        for (const auto& pair : testSequencerStakes_) {
            total += pair.second;
        }
        return total;
    }
    
    // Sum stake of all known sequencers
    for (const auto& pair : sequencerActions_) {
        total += GetSequencerStake(pair.first);
    }
    
    return total;
}

std::pair<uint160, uint160> CollusionDetector::MakeOrderedPair(
    const uint160& a,
    const uint160& b) const
{
    if (a < b) {
        return std::make_pair(a, b);
    }
    return std::make_pair(b, a);
}

// ============================================================================
// Global Instance Management
// ============================================================================

CollusionDetector& GetCollusionDetector()
{
    assert(g_collusion_detector_initialized);
    return *g_collusion_detector;
}

void InitCollusionDetector(uint64_t chainId)
{
    g_collusion_detector = std::make_unique<CollusionDetector>(chainId);
    g_collusion_detector_initialized = true;
}

bool IsCollusionDetectorInitialized()
{
    return g_collusion_detector_initialized;
}

} // namespace l2
