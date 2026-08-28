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
                                         const CVM::TrustNodeId& validatorAddress,
                                         bool voteAccept, int64_t timestamp,
                                         int16_t scoreDifference)
{
    VoteRecord record(txHash, validatorAddress, voteAccept, timestamp, scoreDifference);
    voteHistory[txHash].push_back(record);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Recorded vote for tx %s from validator %s: %s (diff=%d)\n",
             txHash.ToString(), validatorAddress.ToKeyString(),
             voteAccept ? "ACCEPT" : "REJECT", scoreDifference);
}

void VoteManipulationDetector::RecordReputationChange(const CVM::TrustNodeId& address,
                                                     int blockHeight,
                                                     int16_t oldScore,
                                                     int16_t newScore,
                                                     const std::string& reason)
{
    ReputationChange change(address, blockHeight, oldScore, newScore, reason);
    reputationHistory[address].push_back(change);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Recorded reputation change for %s: %d -> %d (%s)\n",
             address.ToKeyString(), oldScore, newScore, reason);
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
    std::map<bool, std::vector<CVM::TrustNodeId>> voteGroups;
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
    std::vector<std::vector<CVM::TrustNodeId>> timingClusters;
    std::vector<CVM::TrustNodeId> currentCluster;
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

ManipulationDetection VoteManipulationDetector::DetectReputationSpike(const CVM::TrustNodeId& address)
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
                     address.ToKeyString(), totalChange, blockSpan);
            
            FlagAddress(address);
        }
    }
    
    return result;
}

ManipulationDetection VoteManipulationDetector::DetectValidatorCollusion(const CVM::TrustNodeId& validator1,
                                                                         const CVM::TrustNodeId& validator2)
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
                 validator1.ToKeyString(), validator2.ToKeyString(), correlation * 100);
        
        FlagAddress(validator1);
        FlagAddress(validator2);
    }
    
    return result;
}

double VoteManipulationDetector::CalculateValidatorCorrelation(const CVM::TrustNodeId& validator1,
                                                               const CVM::TrustNodeId& validator2)
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

ManipulationDetection VoteManipulationDetector::AnalyzeAddress(const CVM::TrustNodeId& address)
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

void VoteManipulationDetector::FlagAddress(const CVM::TrustNodeId& address)
{
    if (flaggedAddresses.insert(address).second) {
        LogPrintf("VoteManipulationDetector: Flagged address %s as suspicious\n",
                 address.ToKeyString());
        SaveFlaggedAddresses();
    }
}

void VoteManipulationDetector::UnflagAddress(const CVM::TrustNodeId& address)
{
    if (flaggedAddresses.erase(address) > 0) {
        LogPrintf("VoteManipulationDetector: Unflagged address %s\n",
                 address.ToKeyString());
        SaveFlaggedAddresses();
    }
}

bool VoteManipulationDetector::IsAddressFlagged(const CVM::TrustNodeId& address) const
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

std::vector<ReputationChange> VoteManipulationDetector::GetReputationHistory(const CVM::TrustNodeId& address) const
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

// Persisted flag-set keys.
//
// The legacy key held a bare concatenation of 20-byte uint160 values, which
// cannot represent a P2WSH or quantum identity. The current key stores canonical
// 33-byte TNI33 records (1 type byte + 32 data bytes) so every supported
// destination round-trips without truncation.
//
// A separate key is used rather than a version prefix because a byte length can
// be an exact multiple of both 20 and 33 (e.g. 660), so the two layouts are not
// reliably distinguishable from the payload alone.
static const char* FLAGGED_ADDRESSES_KEY_LEGACY = "flagged_addresses";
static const char* FLAGGED_ADDRESSES_KEY = "flagged_addresses_v2";

// TNI33: 1 type byte + 32 data bytes.
static constexpr size_t TNI33_RECORD_SIZE = 1 + 32;

void VoteManipulationDetector::SaveFlaggedAddresses()
{
    std::vector<uint8_t> data;
    data.reserve(flaggedAddresses.size() * TNI33_RECORD_SIZE);
    
    for (const auto& node : flaggedAddresses) {
        data.push_back(node.type);
        data.insert(data.end(), node.data.begin(), node.data.end());
    }
    
    db.WriteGeneric(FLAGGED_ADDRESSES_KEY, data);
    
    LogPrint(BCLog::CVM, "VoteManipulationDetector: Saved %d flagged addresses to database\n",
             flaggedAddresses.size());
}

void VoteManipulationDetector::LoadFlaggedAddresses()
{
    std::vector<uint8_t> data;
    
    if (db.ReadGeneric(FLAGGED_ADDRESSES_KEY, data)) {
        flaggedAddresses.clear();
        
        size_t rejected = 0;
        for (size_t i = 0; i + TNI33_RECORD_SIZE <= data.size(); i += TNI33_RECORD_SIZE) {
            CVM::TrustNodeId node;
            node.type = data[i];
            std::copy(data.begin() + i + 1, data.begin() + i + TNI33_RECORD_SIZE,
                      node.data.begin());
            
            // Only accept strictly canonical identities so a corrupt or
            // hand-edited record cannot inject an unusable node.
            std::string err;
            if (!CVM::ValidateCanonicalTrustNode(node, err)) {
                ++rejected;
                continue;
            }
            flaggedAddresses.insert(node);
        }
        
        if (rejected > 0) {
            LogPrintf("VoteManipulationDetector: Skipped %d noncanonical flagged-address records\n",
                      rejected);
        }
        
        LogPrint(BCLog::CVM, "VoteManipulationDetector: Loaded %d flagged addresses from database\n",
                 flaggedAddresses.size());
        return;
    }
    
    // Fall back to the legacy 20-byte layout written by earlier builds. Those
    // records were all uint160-shaped, so they migrate to P2PKH TrustNodeIds.
    if (!db.ReadGeneric(FLAGGED_ADDRESSES_KEY_LEGACY, data)) {
        return; // No data in either layout
    }
    
    flaggedAddresses.clear();
    
    for (size_t i = 0; i + 20 <= data.size(); i += 20) {
        uint160 addr;
        std::copy(data.begin() + i, data.begin() + i + 20, addr.begin());
        flaggedAddresses.insert(CVM::TrustNodeId::FromLegacyUint160(addr));
    }
    
    LogPrintf("VoteManipulationDetector: Migrated %d flagged addresses from the legacy "
              "20-byte layout to TNI33\n", flaggedAddresses.size());
    
    // Persist in the current layout so the migration happens once.
    SaveFlaggedAddresses();
}
