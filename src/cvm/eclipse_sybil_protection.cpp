// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/eclipse_sybil_protection.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/walletcluster.h>
#include <util.h>
#include <algorithm>
#include <cmath>

namespace CVM {

// Helper function to create database key
static std::string MakeDBKey(char prefix, const uint160& addr) {
    return std::string(1, prefix) + addr.ToString();
}

// Helper function to write serializable data to database
template<typename T>
static bool WriteToDatabase(CVMDatabase& db, const std::string& key, const T& value) {
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << value;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    return db.WriteGeneric(key, data);
}

// Helper function to read serializable data from database
template<typename T>
static bool ReadFromDatabase(CVMDatabase& db, const std::string& key, T& value) {
    std::vector<uint8_t> data;
    if (!db.ReadGeneric(key, data)) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> value;
        return true;
    } catch (...) {
        return false;
    }
}

EclipseSybilProtection::EclipseSybilProtection(CVMDatabase& db, TrustGraph& trustGraph)
    : m_db(db), m_trustGraph(trustGraph)
{
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Initialized\n");
}

bool EclipseSybilProtection::IsValidatorEligible(const uint160& validatorAddr, int currentHeight)
{
    // Check validator history (minimum 10,000 blocks)
    if (!CheckValidatorHistory(validatorAddr, currentHeight)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s failed history check\n",
                validatorAddr.ToString());
        return false;
    }
    
    // Check validation history (minimum 50 validations with 85%+ accuracy)
    if (!CheckValidationHistory(validatorAddr)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s failed validation history check\n",
                validatorAddr.ToString());
        return false;
    }
    
    // Check stake age (minimum 1000 blocks)
    if (!CheckStakeAge(validatorAddr)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s failed stake age check\n",
                validatorAddr.ToString());
        return false;
    }
    
    // Check stake source diversity (minimum 3 sources)
    if (!CheckStakeSourceDiversity(validatorAddr)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s failed stake source diversity check\n",
                validatorAddr.ToString());
        return false;
    }
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s is eligible\n",
            validatorAddr.ToString());
    return true;
}

SybilDetectionResult EclipseSybilProtection::DetectSybilNetwork(
    const std::vector<uint160>& validators,
    int currentHeight)
{
    SybilDetectionResult result;
    std::vector<std::string> reasons;
    
    // Check network topology diversity
    if (!CheckTopologyDiversity(validators)) {
        result.hasTopologyCollusion = true;
        reasons.push_back("Validators from same IP subnet");
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Topology collusion detected\n");
    }
    
    // Check peer connection diversity
    if (!CheckPeerDiversity(validators)) {
        result.hasPeerCollusion = true;
        reasons.push_back("High peer overlap between validators");
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Peer collusion detected\n");
    }
    
    // Check stake concentration
    if (!CheckStakeConcentration(validators)) {
        result.hasStakeCollusion = true;
        reasons.push_back("Stake concentration exceeds 20%");
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Stake collusion detected\n");
    }
    
    // Check coordinated behavior
    if (DetectCoordinatedBehavior(validators)) {
        result.hasBehavioralCollusion = true;
        reasons.push_back("Coordinated behavioral patterns detected");
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Behavioral collusion detected\n");
    }
    
    // Check WoT group isolation
    // If all validators are from same WoT group, it's suspicious
    size_t wotValidators = 0;
    for (const auto& validator : validators) {
        if (HasWoTConnection(validator)) {
            wotValidators++;
        }
    }
    double wotRatio = static_cast<double>(wotValidators) / validators.size();
    if (wotRatio > 0.90 || wotRatio < 0.10) {
        result.hasWoTCollusion = true;
        reasons.push_back("WoT group isolation detected");
        LogPrint(BCLog::CVM, "EclipseSybilProtection: WoT collusion detected (ratio: %.2f)\n", wotRatio);
    }
    
    // Calculate confidence based on number of indicators
    int collusionIndicators = 0;
    if (result.hasTopologyCollusion) collusionIndicators++;
    if (result.hasPeerCollusion) collusionIndicators++;
    if (result.hasStakeCollusion) collusionIndicators++;
    if (result.hasBehavioralCollusion) collusionIndicators++;
    if (result.hasWoTCollusion) collusionIndicators++;
    
    result.confidence = static_cast<double>(collusionIndicators) / 5.0;
    
    // Determine if this is a Sybil network
    // Require at least 2 indicators with 40%+ confidence
    result.isSybilNetwork = (collusionIndicators >= 2 && result.confidence >= 0.40);
    
    if (result.isSybilNetwork) {
        result.suspiciousValidators = validators;
        result.reason = "Sybil network detected: ";
        for (size_t i = 0; i < reasons.size(); i++) {
            result.reason += reasons[i];
            if (i < reasons.size() - 1) result.reason += ", ";
        }
        
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Sybil network detected with %.0f%% confidence: %s\n",
                result.confidence * 100, result.reason);
    }
    
    return result;
}

bool EclipseSybilProtection::ValidateValidatorSetDiversity(
    const std::vector<uint160>& validators,
    int currentHeight)
{
    // Check topology diversity
    if (!CheckTopologyDiversity(validators)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator set failed topology diversity check\n");
        return false;
    }
    
    // Check peer diversity
    if (!CheckPeerDiversity(validators)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator set failed peer diversity check\n");
        return false;
    }
    
    // Check stake concentration
    if (!CheckStakeConcentration(validators)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator set failed stake concentration check\n");
        return false;
    }
    
    // Check cross-validation requirements (40% non-WoT)
    if (!CheckCrossValidationRequirements(validators)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator set failed cross-validation requirements\n");
        return false;
    }
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator set passed all diversity checks\n");
    return true;
}

void EclipseSybilProtection::UpdateValidatorNetworkInfo(
    const uint160& validatorAddr,
    const CNetAddr& ipAddr,
    const std::set<uint160>& peers,
    int currentHeight)
{
    ValidatorNetworkInfo info;
    std::string key = MakeDBKey(DB_VALIDATOR_NETWORK, validatorAddr);
    
    // Read existing info or create new
    if (!ReadFromDatabase(m_db, key, info)) {
        info.address = validatorAddr;
        info.firstSeen = currentHeight;
    }
    
    // Update info
    info.ipAddress = ipAddr;
    info.connectedPeers = peers;
    
    // Write back to database
    WriteToDatabase(m_db, key, info);
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Updated network info for validator %s\n",
            validatorAddr.ToString());
}

void EclipseSybilProtection::UpdateValidatorStakeInfo(
    const uint160& validatorAddr,
    const ValidatorStakeInfo& stakeInfo)
{
    std::string key = MakeDBKey(DB_VALIDATOR_STAKE, validatorAddr);
    WriteToDatabase(m_db, key, stakeInfo);
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Updated stake info for validator %s (stake: %d, sources: %zu)\n",
            validatorAddr.ToString(), stakeInfo.totalStake, stakeInfo.GetStakeSourceCount());
}

void EclipseSybilProtection::RecordValidationResult(const uint160& validatorAddr, bool wasAccurate)
{
    ValidatorNetworkInfo info;
    std::string key = MakeDBKey(DB_VALIDATOR_NETWORK, validatorAddr);
    
    if (ReadFromDatabase(m_db, key, info)) {
        info.validationCount++;
        if (wasAccurate) {
            info.accurateValidations++;
        }
        WriteToDatabase(m_db, key, info);
        
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Recorded validation result for %s (accurate: %d, total: %d, accuracy: %.2f%%)\n",
                validatorAddr.ToString(), info.accurateValidations, info.validationCount,
                info.GetAccuracy() * 100);
    }
}

void EclipseSybilProtection::RecordValidationTimestamp(
    const uint160& validatorAddr,
    const uint256& taskHash,
    int64_t timestamp)
{
    ValidatorNetworkInfo info;
    std::string key = MakeDBKey(DB_VALIDATOR_NETWORK, validatorAddr);
    
    // Read existing info or create new
    if (!ReadFromDatabase(m_db, key, info)) {
        info.address = validatorAddr;
        info.firstSeen = 0;  // Will be set properly when UpdateValidatorNetworkInfo is called
    }
    
    // Create and add the validation timestamp
    ValidationTimestamp ts(taskHash, timestamp, validatorAddr);
    info.AddValidationTimestamp(ts);
    
    // Write back to database
    WriteToDatabase(m_db, key, info);
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Recorded validation timestamp for %s, task %s at %ld (total timestamps: %zu)\n",
            validatorAddr.ToString(), taskHash.ToString().substr(0, 16), timestamp, 
            info.validationTimestamps.size());
}

CoordinatedAttackResult EclipseSybilProtection::DetectCoordinatedAttack(
    const uint256& taskHash,
    const std::vector<ValidationTimestamp>& timestamps)
{
    CoordinatedAttackResult result;
    
    if (timestamps.size() < MIN_COORDINATED_VALIDATORS) {
        // Not enough validators to detect coordination
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Not enough timestamps (%zu) to detect coordination (need %zu)\n",
                timestamps.size(), MIN_COORDINATED_VALIDATORS);
        return result;
    }
    
    // Sort timestamps by time
    std::vector<ValidationTimestamp> sortedTimestamps = timestamps;
    std::sort(sortedTimestamps.begin(), sortedTimestamps.end(),
        [](const ValidationTimestamp& a, const ValidationTimestamp& b) {
            return a.timestamp < b.timestamp;
        });
    
    // Find the largest cluster of responses within COORDINATION_WINDOW_MS (1 second)
    size_t maxClusterSize = 0;
    size_t maxClusterStart = 0;
    
    for (size_t i = 0; i < sortedTimestamps.size(); i++) {
        size_t clusterSize = 1;
        int64_t windowStart = sortedTimestamps[i].timestamp;
        
        // Count how many responses fall within 1 second of this timestamp
        for (size_t j = i + 1; j < sortedTimestamps.size(); j++) {
            if (sortedTimestamps[j].timestamp - windowStart <= COORDINATION_WINDOW_MS) {
                clusterSize++;
            } else {
                break;  // Timestamps are sorted, so no more will be in window
            }
        }
        
        if (clusterSize > maxClusterSize) {
            maxClusterSize = clusterSize;
            maxClusterStart = i;
        }
    }
    
    result.clusteredResponses = maxClusterSize;
    
    // Check if we have a coordinated attack (5+ validators within 1 second)
    if (maxClusterSize >= MIN_COORDINATED_VALIDATORS) {
        result.isCoordinatedAttack = true;
        
        // Collect suspicious validators from the cluster
        result.minTimestamp = sortedTimestamps[maxClusterStart].timestamp;
        result.maxTimestamp = sortedTimestamps[maxClusterStart].timestamp;
        
        for (size_t i = maxClusterStart; i < sortedTimestamps.size(); i++) {
            if (sortedTimestamps[i].timestamp - result.minTimestamp <= COORDINATION_WINDOW_MS) {
                result.suspiciousValidators.push_back(sortedTimestamps[i].validatorAddress);
                if (sortedTimestamps[i].timestamp > result.maxTimestamp) {
                    result.maxTimestamp = sortedTimestamps[i].timestamp;
                }
            } else {
                break;
            }
        }
        
        // Calculate confidence based on cluster density
        // Higher confidence if more validators respond in shorter time
        int64_t timeSpan = result.maxTimestamp - result.minTimestamp;
        if (timeSpan == 0) {
            // All responses at exact same time - very suspicious
            result.confidence = 1.0;
        } else {
            // Confidence increases with more validators and shorter time span
            double densityFactor = static_cast<double>(maxClusterSize) / MIN_COORDINATED_VALIDATORS;
            double timeFactor = 1.0 - (static_cast<double>(timeSpan) / COORDINATION_WINDOW_MS);
            result.confidence = std::min(1.0, (densityFactor * 0.6 + timeFactor * 0.4));
        }
        
        result.reason = "Coordinated attack detected: " + std::to_string(maxClusterSize) + 
                       " validators responded within " + std::to_string(result.maxTimestamp - result.minTimestamp) + 
                       "ms window";
        
        LogPrint(BCLog::CVM, "EclipseSybilProtection: %s (confidence: %.0f%%, task: %s)\n",
                result.reason, result.confidence * 100, taskHash.ToString().substr(0, 16));
    } else {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: No coordinated attack detected for task %s (max cluster: %zu)\n",
                taskHash.ToString().substr(0, 16), maxClusterSize);
    }
    
    return result;
}

std::vector<ValidationTimestamp> EclipseSybilProtection::CollectTaskTimestamps(
    const uint256& taskHash,
    const std::vector<uint160>& validators)
{
    std::vector<ValidationTimestamp> allTimestamps;
    
    for (const auto& validator : validators) {
        ValidatorNetworkInfo info;
        if (GetValidatorNetworkInfo(validator, info)) {
            // Get timestamps for this specific task
            std::vector<ValidationTimestamp> validatorTimestamps = info.GetTimestampsForTask(taskHash);
            allTimestamps.insert(allTimestamps.end(), validatorTimestamps.begin(), validatorTimestamps.end());
        }
    }
    
    LogPrint(BCLog::CVM, "EclipseSybilProtection: Collected %zu timestamps for task %s from %zu validators\n",
            allTimestamps.size(), taskHash.ToString().substr(0, 16), validators.size());
    
    return allTimestamps;
}

bool EclipseSybilProtection::GetValidatorNetworkInfo(const uint160& validatorAddr, ValidatorNetworkInfo& info)
{
    std::string key = MakeDBKey(DB_VALIDATOR_NETWORK, validatorAddr);
    return ReadFromDatabase(m_db, key, info);
}

bool EclipseSybilProtection::GetValidatorStakeInfo(const uint160& validatorAddr, ValidatorStakeInfo& info)
{
    std::string key = MakeDBKey(DB_VALIDATOR_STAKE, validatorAddr);
    return ReadFromDatabase(m_db, key, info);
}

// Private helper methods

bool EclipseSybilProtection::CheckTopologyDiversity(const std::vector<uint160>& validators)
{
    if (validators.size() < 2) return true;
    
    std::map<uint32_t, int> subnetCounts;
    
    for (const auto& validator : validators) {
        ValidatorNetworkInfo info;
        if (GetValidatorNetworkInfo(validator, info)) {
            uint32_t subnet = GetIPSubnet(info.ipAddress);
            subnetCounts[subnet]++;
        }
    }
    
    // Check if any subnet has more than 50% of validators
    for (const auto& pair : subnetCounts) {
        double ratio = static_cast<double>(pair.second) / validators.size();
        if (ratio > 0.50) {
            LogPrint(BCLog::CVM, "EclipseSybilProtection: Subnet concentration %.0f%% exceeds 50%%\n",
                    ratio * 100);
            return false;
        }
    }
    
    return true;
}

bool EclipseSybilProtection::CheckPeerDiversity(const std::vector<uint160>& validators)
{
    if (validators.size() < 2) return true;
    
    // Check pairwise peer overlap
    for (size_t i = 0; i < validators.size(); i++) {
        for (size_t j = i + 1; j < validators.size(); j++) {
            ValidatorNetworkInfo info1, info2;
            if (GetValidatorNetworkInfo(validators[i], info1) &&
                GetValidatorNetworkInfo(validators[j], info2)) {
                
                double overlap = CalculatePeerOverlap(info1.connectedPeers, info2.connectedPeers);
                if (overlap > MAX_PEER_OVERLAP) {
                    LogPrint(BCLog::CVM, "EclipseSybilProtection: Peer overlap %.0f%% between %s and %s exceeds 50%%\n",
                            overlap * 100, validators[i].ToString(), validators[j].ToString());
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool EclipseSybilProtection::CheckValidatorHistory(const uint160& validatorAddr, int currentHeight)
{
    ValidatorNetworkInfo info;
    if (!GetValidatorNetworkInfo(validatorAddr, info)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: No network info for validator %s\n",
                validatorAddr.ToString());
        return false;
    }
    
    uint64_t blocksSinceFirstSeen = currentHeight - info.firstSeen;
    if (blocksSinceFirstSeen < MIN_VALIDATOR_HISTORY_BLOCKS) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s only has %lu blocks history (need %u)\n",
                validatorAddr.ToString(), blocksSinceFirstSeen, MIN_VALIDATOR_HISTORY_BLOCKS);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::CheckValidationHistory(const uint160& validatorAddr)
{
    ValidatorNetworkInfo info;
    if (!GetValidatorNetworkInfo(validatorAddr, info)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: No network info for validator %s\n",
                validatorAddr.ToString());
        return false;
    }
    
    // Check minimum validation count
    if (info.validationCount < MIN_VALIDATION_COUNT) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s only has %u validations (need %u)\n",
                validatorAddr.ToString(), info.validationCount, MIN_VALIDATION_COUNT);
        return false;
    }
    
    // Check minimum accuracy
    double accuracy = info.GetAccuracy();
    if (accuracy < MIN_VALIDATION_ACCURACY) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s only has %.2f%% accuracy (need %.0f%%)\n",
                validatorAddr.ToString(), accuracy * 100, MIN_VALIDATION_ACCURACY * 100);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::CheckStakeConcentration(const std::vector<uint160>& validators)
{
    if (validators.empty()) return true;
    
    // Calculate total stake
    CAmount totalStake = 0;
    std::map<uint160, CAmount> validatorStakes;
    
    for (const auto& validator : validators) {
        ValidatorStakeInfo info;
        if (GetValidatorStakeInfo(validator, info)) {
            validatorStakes[validator] = info.totalStake;
            totalStake += info.totalStake;
        }
    }
    
    if (totalStake == 0) return true;
    
    // Check if any validator controls >20% of stake
    for (const auto& pair : validatorStakes) {
        double ratio = static_cast<double>(pair.second) / totalStake;
        if (ratio > MAX_STAKE_CONCENTRATION) {
            LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s controls %.0f%% of stake (max 20%%)\n",
                    pair.first.ToString(), ratio * 100);
            return false;
        }
    }
    
    // Also check for wallet clustering (same entity controlling multiple validators)
    // Use WalletClusterer from walletcluster.h
    WalletClusterer clusterer(m_db);
    std::map<uint160, std::vector<uint160>> clusterMap;
    
    for (const auto& validator : validators) {
        uint160 clusterId = clusterer.GetClusterForAddress(validator);
        if (!clusterId.IsNull()) {
            clusterMap[clusterId].push_back(validator);
        }
    }
    
    // Check if any cluster controls >20% of stake
    for (const auto& pair : clusterMap) {
        CAmount clusterStake = 0;
        for (const auto& validator : pair.second) {
            auto it = validatorStakes.find(validator);
            if (it != validatorStakes.end()) {
                clusterStake += it->second;
            }
        }
        
        double ratio = static_cast<double>(clusterStake) / totalStake;
        if (ratio > MAX_STAKE_CONCENTRATION) {
            LogPrint(BCLog::CVM, "EclipseSybilProtection: Wallet cluster controls %.0f%% of stake (max 20%%)\n",
                    ratio * 100);
            return false;
        }
    }
    
    return true;
}

bool EclipseSybilProtection::CheckCrossValidationRequirements(const std::vector<uint160>& validators)
{
    if (validators.empty()) return true;
    
    size_t nonWoTValidators = 0;
    for (const auto& validator : validators) {
        if (!HasWoTConnection(validator)) {
            nonWoTValidators++;
        }
    }
    
    double nonWoTRatio = static_cast<double>(nonWoTValidators) / validators.size();
    if (nonWoTRatio < MIN_NON_WOT_VALIDATORS) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Only %.0f%% non-WoT validators (need %.0f%%)\n",
                nonWoTRatio * 100, MIN_NON_WOT_VALIDATORS * 100);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::CheckCrossGroupAgreement(
    const std::vector<uint160>& validators,
    const std::map<uint160, int>& votes)
{
    if (validators.empty() || votes.empty()) return true;
    
    // Separate validators into WoT and non-WoT groups
    std::vector<int> wotVotes;
    std::vector<int> nonWotVotes;
    
    for (const auto& validator : validators) {
        auto it = votes.find(validator);
        if (it != votes.end()) {
            if (HasWoTConnection(validator)) {
                wotVotes.push_back(it->second);
            } else {
                nonWotVotes.push_back(it->second);
            }
        }
    }
    
    if (wotVotes.empty() || nonWotVotes.empty()) return true;
    
    // Calculate average votes for each group
    double wotAvg = 0;
    for (int vote : wotVotes) wotAvg += vote;
    wotAvg /= wotVotes.size();
    
    double nonWotAvg = 0;
    for (int vote : nonWotVotes) nonWotAvg += vote;
    nonWotAvg /= nonWotVotes.size();
    
    // Check if disagreement exceeds threshold
    double disagreement = std::abs(wotAvg - nonWotAvg) / 100.0;  // Normalize to 0-1
    if (disagreement > MAX_CROSS_GROUP_DISAGREEMENT) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Cross-group disagreement %.0f%% exceeds 60%%\n",
                disagreement * 100);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::CheckStakeAge(const uint160& validatorAddr)
{
    ValidatorStakeInfo info;
    if (!GetValidatorStakeInfo(validatorAddr, info)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: No stake info for validator %s\n",
                validatorAddr.ToString());
        return false;
    }
    
    if (info.oldestStakeAge < MIN_STAKE_AGE_BLOCKS) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s stake only aged %lu blocks (need %lu)\n",
                validatorAddr.ToString(), info.oldestStakeAge, MIN_STAKE_AGE_BLOCKS);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::CheckStakeSourceDiversity(const uint160& validatorAddr)
{
    ValidatorStakeInfo info;
    if (!GetValidatorStakeInfo(validatorAddr, info)) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: No stake info for validator %s\n",
                validatorAddr.ToString());
        return false;
    }
    
    if (info.GetStakeSourceCount() < MIN_STAKE_SOURCES) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Validator %s only has %zu stake sources (need %zu)\n",
                validatorAddr.ToString(), info.GetStakeSourceCount(), MIN_STAKE_SOURCES);
        return false;
    }
    
    return true;
}

bool EclipseSybilProtection::DetectCoordinatedBehavior(const std::vector<uint160>& validators)
{
    if (validators.size() < 2) return false;
    
    // Collect all recent validation timestamps from validators
    std::map<uint256, std::vector<ValidationTimestamp>> taskTimestamps;
    
    for (const auto& validator : validators) {
        ValidatorNetworkInfo info;
        if (GetValidatorNetworkInfo(validator, info)) {
            // Group timestamps by task hash
            for (const auto& ts : info.validationTimestamps) {
                taskTimestamps[ts.taskHash].push_back(ts);
            }
        }
    }
    
    // Check each task for coordinated timing patterns
    for (const auto& pair : taskTimestamps) {
        const uint256& taskHash = pair.first;
        const std::vector<ValidationTimestamp>& timestamps = pair.second;
        
        // Use DetectCoordinatedAttack to analyze timing patterns
        CoordinatedAttackResult attackResult = DetectCoordinatedAttack(taskHash, timestamps);
        if (attackResult.isCoordinatedAttack) {
            LogPrint(BCLog::CVM, "EclipseSybilProtection: Coordinated behavior detected via timing analysis for task %s\n",
                    taskHash.ToString().substr(0, 16));
            return true;
        }
    }
    
    // Also check if validators have suspiciously similar validation counts (original check)
    std::vector<uint32_t> validationCounts;
    for (const auto& validator : validators) {
        ValidatorNetworkInfo info;
        if (GetValidatorNetworkInfo(validator, info)) {
            validationCounts.push_back(info.validationCount);
        }
    }
    
    if (validationCounts.size() < 2) return false;
    
    // Calculate standard deviation
    double mean = 0;
    for (uint32_t count : validationCounts) mean += count;
    mean /= validationCounts.size();
    
    double variance = 0;
    for (uint32_t count : validationCounts) {
        double diff = count - mean;
        variance += diff * diff;
    }
    variance /= validationCounts.size();
    double stddev = std::sqrt(variance);
    
    // If standard deviation is very low (< 10% of mean), it's suspicious
    double coefficientOfVariation = (mean > 0) ? (stddev / mean) : 0;
    if (coefficientOfVariation < 0.10) {
        LogPrint(BCLog::CVM, "EclipseSybilProtection: Suspiciously similar validation counts (CV: %.2f%%)\n",
                coefficientOfVariation * 100);
        return true;
    }
    
    return false;
}

double EclipseSybilProtection::CalculatePeerOverlap(
    const std::set<uint160>& peers1,
    const std::set<uint160>& peers2)
{
    if (peers1.empty() || peers2.empty()) return 0.0;
    
    // Count common peers
    size_t commonPeers = 0;
    for (const auto& peer : peers1) {
        if (peers2.count(peer) > 0) {
            commonPeers++;
        }
    }
    
    // Calculate overlap as ratio of common peers to smaller set
    size_t smallerSetSize = std::min(peers1.size(), peers2.size());
    return static_cast<double>(commonPeers) / smallerSetSize;
}

uint32_t EclipseSybilProtection::GetIPSubnet(const CNetAddr& ipAddr)
{
    // Get first 16 bits of IP address for /16 subnet
    std::vector<unsigned char> ip = ipAddr.GetGroup();
    if (ip.size() >= 2) {
        return (static_cast<uint32_t>(ip[0]) << 8) | ip[1];
    }
    return 0;
}

bool EclipseSybilProtection::HasWoTConnection(const uint160& validatorAddr)
{
    // Check if validator has any trust relationships in the trust graph
    std::vector<TrustEdge> outgoing = m_trustGraph.GetOutgoingTrust(validatorAddr);
    std::vector<TrustEdge> incoming = m_trustGraph.GetIncomingTrust(validatorAddr);
    return !outgoing.empty() || !incoming.empty();
}

} // namespace CVM
