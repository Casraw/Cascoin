// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/vote_manipulation_detector.h>
#include <chain.h>
#include <sync.h>
#include <validation.h>
#include <util.h>
#include <algorithm>
#include <cmath>

extern CChain& chainActive;

VoteManipulationDetector::VoteManipulationDetector(CVM::CVMDatabase& database)
    : db(database)
{
    LoadFlaggedAddresses();
}

void VoteManipulationDetector::RecordVote(const uint256& txHash, 
                                         const uint160& validatorAddress,
                                         bool voteAccept, int64_t timestamp,
                                         int16_t scoreDifference)
{
    VoteRecord record(txHash, validatorAddress, voteAccept, timestamp, scoreDifference);
    voteHistory[txHash].push_back(record);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Recorded vote for tx %s from validator %s: %s (diff=%d)\n",
             txHash.ToString(), validatorAddress.ToString(),
             voteAccept ? "ACCEPT" : "REJECT", scoreDifference);
}

void VoteManipulationDetector::RecordReputationChange(const uint160& address,
                                                     int blockHeight,
                                                     int16_t oldScore,
                                                     int16_t newScore,
                                                     const std::string& reason)
{
    ReputationChange change(address, blockHeight, oldScore, newScore, reason);
    reputationHistory[address].push_back(change);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Recorded reputation change for %s: %d -> %d (%s)\n",
             address.ToString(), oldScore, newScore, reason);
}

ManipulationDetection VoteManipulationDetector::DetectCoordinatedVoting(const uint256& txHash)
{
    ManipulationDetection result;
    
    auto it = voteHistory.find(txHash);
    if (it == voteHistory.end() || it->second.size() < MIN_VOTES_FOR_ANALYSIS) {
        return result; // Not enough data
    }
    
    const auto& votes = it->second;
    
    // Count identical voting patterns
    std::map<bool, std::vector<uint160>> voteGroups;
    for (const auto& vote : votes) {
        voteGroups[vote.voteAccept].push_back(vote.validatorAddress);
    }
    
    // Check if one group is suspiciously large
    for (const auto& group : voteGroups) {
        double groupRatio = static_cast<double>(group.second.size()) / votes.size();
        
        if (groupRatio >= SUSPICIOUS_CORRELATION_THRESHOLD && group.second.size() >= 5) {
            result.type = ManipulationDetection::COORDINATED_VOTING;
            result.suspiciousAddresses = group.second;
            result.suspiciousTxs.push_back(txHash);
            result.confidence = groupRatio;
            result.description = strprintf("Coordinated voting detected: %d/%d validators voted identically",
                                         group.second.size(), votes.size());
            result.escalateToDAO = (groupRatio >= 0.95);
            
            LogPrintf("VoteManipulationDetector: Coordinated voting detected for tx %s: %d/%d validators (%.1f%%)\n",
                     txHash.ToString(), group.second.size(), votes.size(), groupRatio * 100);
            
            // Flag suspicious validators
            for (const auto& addr : group.second) {
                FlagAddress(addr);
            }
            
            break;
        }
    }
    
    return result;
}

ManipulationDetection VoteManipulationDetector::AnalyzeVoteTimingCorrelation(const uint256& txHash)
{
    ManipulationDetection result;
    
    auto it = voteHistory.find(txHash);
    if (it == voteHistory.end() || it->second.size() < MIN_VOTES_FOR_ANALYSIS) {
        return result;
    }
    
    const auto& votes = it->second;
    
    // Sort votes by timestamp
    std::vector<VoteRecord> sortedVotes = votes;
    std::sort(sortedVotes.begin(), sortedVotes.end(),
             [](const VoteRecord& a, const VoteRecord& b) {
                 return a.timestamp < b.timestamp;
             });
    
    // Detect clusters of votes within timing window
    std::vector<std::vector<uint160>> timingClusters;
    std::vector<uint160> currentCluster;
    int64_t clusterStartTime = 0;
    
    for (const auto& vote : sortedVotes) {
        if (currentCluster.empty()) {
            currentCluster.push_back(vote.validatorAddress);
            clusterStartTime = vote.timestamp;
        } else if (vote.timestamp - clusterStartTime <= VOTE_TIMING_WINDOW_MS) {
            currentCluster.push_back(vote.validatorAddress);
        } else {
            if (currentCluster.size() >= 5) {
                timingClusters.push_back(currentCluster);
            }
            currentCluster.clear();
            currentCluster.push_back(vote.validatorAddress);
            clusterStartTime = vote.timestamp;
        }
    }
    
    if (currentCluster.size() >= 5) {
        timingClusters.push_back(currentCluster);
    }
    
    // Analyze largest cluster
    if (!timingClusters.empty()) {
        auto largestCluster = *std::max_element(timingClusters.begin(), timingClusters.end(),
                                               [](const auto& a, const auto& b) {
                                                   return a.size() < b.size();
                                               });
        
        double clusterRatio = static_cast<double>(largestCluster.size()) / votes.size();
        
        if (clusterRatio >= 0.5) { // 50% of votes in same timing window
            result.type = ManipulationDetection::TIMING_CORRELATION;
            result.suspiciousAddresses = largestCluster;
            result.suspiciousTxs.push_back(txHash);
            result.confidence = clusterRatio;
            result.description = strprintf("Suspicious vote timing: %d validators voted within %dms",
                                         largestCluster.size(), VOTE_TIMING_WINDOW_MS);
            result.escalateToDAO = (clusterRatio >= 0.75);
            
            LogPrintf("VoteManipulationDetector: Timing correlation detected for tx %s: %d validators within %dms\n",
                     txHash.ToString(), largestCluster.size(), VOTE_TIMING_WINDOW_MS);
            
            for (const auto& addr : largestCluster) {
                FlagAddress(addr);
            }
        }
    }
    
    return result;
}

ManipulationDetection VoteManipulationDetector::DetectReputationSpike(const uint160& address)
{
    ManipulationDetection result;
    
    auto it = reputationHistory.find(address);
    if (it == reputationHistory.end() || it->second.size() < 2) {
        return result;
    }
    
    const auto& history = it->second;
    
    // Analyze recent changes (last 1000 blocks)
    int16_t totalChange = 0;
    int blockSpan = 0;
    
    for (size_t i = history.size() - 1; i > 0 && blockSpan < 1000; --i) {
        totalChange += history[i].change;
        blockSpan = history.back().blockHeight - history[i].blockHeight;
    }
    
    if (blockSpan > 0) {
        // Calculate change rate per 1000 blocks
        double changeRate = (static_cast<double>(totalChange) / blockSpan) * 1000;
        
        if (changeRate >= REPUTATION_SPIKE_THRESHOLD) {
            result.type = ManipulationDetection::REPUTATION_SPIKE;
            result.suspiciousAddresses.push_back(address);
            result.confidence = std::min(1.0, changeRate / (REPUTATION_SPIKE_THRESHOLD * 2));
            result.description = strprintf("Suspicious reputation spike: +%d points in %d blocks (%.1f per 1000 blocks)",
                                         totalChange, blockSpan, changeRate);
            result.escalateToDAO = (changeRate >= REPUTATION_SPIKE_THRESHOLD * 2);
            
            LogPrintf("VoteManipulationDetector: Reputation spike detected for %s: +%d in %d blocks\n",
                     address.ToString(), totalChange, blockSpan);
            
            FlagAddress(address);
        }
    }
    
    return result;
}

ManipulationDetection VoteManipulationDetector::DetectValidatorCollusion(const uint160& validator1,
                                                                         const uint160& validator2)
{
    ManipulationDetection result;
    
    double correlation = CalculateValidatorCorrelation(validator1, validator2);
    
    if (correlation >= COLLUSION_AGREEMENT_THRESHOLD) {
        result.type = ManipulationDetection::COLLUSION;
        result.suspiciousAddresses.push_back(validator1);
        result.suspiciousAddresses.push_back(validator2);
        result.confidence = correlation;
        result.description = strprintf("Validator collusion detected: %.1f%% agreement",
                                     correlation * 100);
        result.escalateToDAO = true;
        
        LogPrintf("VoteManipulationDetector: Collusion detected between %s and %s: %.1f%% agreement\n",
                 validator1.ToString(), validator2.ToString(), correlation * 100);
        
        FlagAddress(validator1);
        FlagAddress(validator2);
    }
    
    return result;
}

double VoteManipulationDetector::CalculateValidatorCorrelation(const uint160& validator1,
                                                               const uint160& validator2)
{
    // Check cache first
    auto key = std::make_pair(validator1, validator2);
    auto it = validatorCorrelations.find(key);
    if (it != validatorCorrelations.end()) {
        return it->second;
    }
    
    // Find common transactions both validators voted on
    std::vector<std::pair<bool, bool>> commonVotes; // (validator1_vote, validator2_vote)
    
    for (const auto& entry : voteHistory) {
        const auto& votes = entry.second;
        
        bool found1 = false, found2 = false;
        bool vote1 = false, vote2 = false;
        
        for (const auto& vote : votes) {
            if (vote.validatorAddress == validator1) {
                found1 = true;
                vote1 = vote.voteAccept;
            }
            if (vote.validatorAddress == validator2) {
                found2 = true;
                vote2 = vote.voteAccept;
            }
        }
        
        if (found1 && found2) {
            commonVotes.push_back({vote1, vote2});
        }
    }
    
    if (commonVotes.size() < MIN_VOTES_FOR_ANALYSIS) {
        return 0.0; // Not enough data
    }
    
    // Calculate agreement rate
    int agreements = 0;
    for (const auto& pair : commonVotes) {
        if (pair.first == pair.second) {
            agreements++;
        }
    }
    
    double correlation = static_cast<double>(agreements) / commonVotes.size();
    
    // Cache result
    validatorCorrelations[key] = correlation;
    validatorCorrelations[std::make_pair(validator2, validator1)] = correlation; // Symmetric
    
    return correlation;
}

ManipulationDetection VoteManipulationDetector::AnalyzeTransaction(const uint256& txHash)
{
    // Run all detection mechanisms
    std::vector<ManipulationDetection> detections;
    
    detections.push_back(DetectCoordinatedVoting(txHash));
    detections.push_back(AnalyzeVoteTimingCorrelation(txHash));
    
    // Return most significant detection
    ManipulationDetection result;
    double maxConfidence = 0.0;
    
    for (const auto& detection : detections) {
        if (detection.type != ManipulationDetection::NONE && 
            detection.confidence > maxConfidence) {
            result = detection;
            maxConfidence = detection.confidence;
        }
    }
    
    return result;
}

ManipulationDetection VoteManipulationDetector::AnalyzeAddress(const uint160& address)
{
    // Check for reputation spike
    ManipulationDetection result = DetectReputationSpike(address);
    
    // Check if already flagged
    if (IsAddressFlagged(address) && result.type == ManipulationDetection::NONE) {
        result.type = ManipulationDetection::SUSPICIOUS_PATTERN;
        result.suspiciousAddresses.push_back(address);
        result.confidence = 0.5;
        result.description = "Address previously flagged for suspicious activity";
        result.escalateToDAO = false;
    }
    
    return result;
}

void VoteManipulationDetector::FlagAddress(const uint160& address)
{
    if (flaggedAddresses.insert(address).second) {
        LogPrintf("VoteManipulationDetector: Flagged address %s as suspicious\n",
                 address.ToString());
        SaveFlaggedAddresses();
    }
}

void VoteManipulationDetector::UnflagAddress(const uint160& address)
{
    if (flaggedAddresses.erase(address) > 0) {
        LogPrintf("VoteManipulationDetector: Unflagged address %s\n",
                 address.ToString());
        SaveFlaggedAddresses();
    }
}

bool VoteManipulationDetector::IsAddressFlagged(const uint160& address) const
{
    return flaggedAddresses.find(address) != flaggedAddresses.end();
}

std::vector<VoteRecord> VoteManipulationDetector::GetVoteHistory(const uint256& txHash) const
{
    auto it = voteHistory.find(txHash);
    if (it != voteHistory.end()) {
        return it->second;
    }
    return std::vector<VoteRecord>();
}

std::vector<ReputationChange> VoteManipulationDetector::GetReputationHistory(const uint160& address) const
{
    auto it = reputationHistory.find(address);
    if (it != reputationHistory.end()) {
        return it->second;
    }
    return std::vector<ReputationChange>();
}

void VoteManipulationDetector::PruneVoteHistory(size_t keepCount)
{
    if (voteHistory.size() <= keepCount) {
        return;
    }
    
    // Keep most recent transactions
    std::vector<std::pair<uint256, int64_t>> txTimestamps;
    for (const auto& entry : voteHistory) {
        if (!entry.second.empty()) {
            int64_t latestTime = entry.second.back().timestamp;
            txTimestamps.push_back({entry.first, latestTime});
        }
    }
    
    std::sort(txTimestamps.begin(), txTimestamps.end(),
             [](const auto& a, const auto& b) {
                 return a.second > b.second; // Descending
             });
    
    // Remove old entries
    for (size_t i = keepCount; i < txTimestamps.size(); ++i) {
        voteHistory.erase(txTimestamps[i].first);
    }
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Pruned vote history, kept %d transactions\n",
             keepCount);
}

void VoteManipulationDetector::PruneReputationHistory(int keepBlocks)
{
    int currentHeight = 0;
    {
        LOCK(cs_main);
        currentHeight = chainActive.Height();
    }
    
    for (auto it = reputationHistory.begin(); it != reputationHistory.end(); ) {
        auto& history = it->second;
        
        // Remove old entries
        history.erase(
            std::remove_if(history.begin(), history.end(),
                          [currentHeight, keepBlocks](const ReputationChange& change) {
                              return (currentHeight - change.blockHeight) > keepBlocks;
                          }),
            history.end()
        );
        
        if (history.empty()) {
            it = reputationHistory.erase(it);
        } else {
            ++it;
        }
    }
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Pruned reputation history, kept last %d blocks\n",
             keepBlocks);
}

void VoteManipulationDetector::SaveFlaggedAddresses()
{
    // Serialize flagged addresses to database
    std::vector<uint8_t> data;
    data.reserve(flaggedAddresses.size() * 20);
    
    for (const auto& addr : flaggedAddresses) {
        data.insert(data.end(), addr.begin(), addr.end());
    }
    
    db.WriteGeneric("flagged_addresses", data);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Saved %d flagged addresses to database\n",
             flaggedAddresses.size());
}

void VoteManipulationDetector::LoadFlaggedAddresses()
{
    std::vector<uint8_t> data;
    if (!db.ReadGeneric("flagged_addresses", data)) {
        return; // No data
    }
    
    flaggedAddresses.clear();
    
    for (size_t i = 0; i + 20 <= data.size(); i += 20) {
        uint160 addr;
        std::copy(data.begin() + i, data.begin() + i + 20, addr.begin());
        flaggedAddresses.insert(addr);
    }
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Loaded %d flagged addresses from database\n",
             flaggedAddresses.size());
}
