// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_reputation.cpp
 * @brief Implementation of L2 Reputation Manager
 * 
 * Requirements: 6.1, 6.2, 10.1, 10.2, 10.3, 10.4, 10.5, 18.5
 */

#include <l2/l2_reputation.h>
#include <util.h>

#include <algorithm>
#include <cmath>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

L2ReputationManager::L2ReputationManager(uint64_t chainId)
    : chainId_(chainId)
{
}

// ============================================================================
// L1 Reputation Import (Requirement 10.1)
// ============================================================================

bool L2ReputationManager::ImportL1Reputation(const uint160& address, uint32_t l1HatScore, uint64_t l1BlockNumber)
{
    LOCK(cs_reputation_);
    
    // Validate score range
    if (l1HatScore > 100) {
        return false;
    }
    
    L2ReputationData& data = EnsureReputationData(address);
    
    // Update L1 score
    data.l1HatScore = l1HatScore;
    data.lastL1Sync = l1BlockNumber;
    
    // If this is first time seeing this address, set firstSeenOnL2
    if (data.firstSeenOnL2 == 0) {
        data.firstSeenOnL2 = l1BlockNumber;
    }
    
    // Recalculate aggregated score
    RecalculateAggregatedScore(address);
    
    return true;
}

bool L2ReputationManager::HasL1Reputation(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    if (auto it = reputationCache_.find(address); it == reputationCache_.end()) {
        return false;
    } else {
        return it->second.l1HatScore > 0 || it->second.lastL1Sync > 0;
    }
}

uint32_t L2ReputationManager::GetL1HatScore(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return 0;
    }
    
    return it->second.l1HatScore;
}

// ============================================================================
// Aggregated Reputation (Requirement 10.3)
// ============================================================================

uint32_t L2ReputationManager::GetAggregatedReputation(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return 0;
    }
    
    return it->second.aggregatedScore;
}

L2ReputationData L2ReputationManager::GetReputationData(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return L2ReputationData();
    }
    
    return it->second;
}

bool L2ReputationManager::HasReputationData(const uint160& address) const
{
    LOCK(cs_reputation_);
    return reputationCache_.find(address) != reputationCache_.end();
}

// ============================================================================
// L2 Reputation Updates (Requirement 10.2)
// ============================================================================

void L2ReputationManager::UpdateL2Reputation(const uint160& address, const L2Activity& activity)
{
    LOCK(cs_reputation_);
    
    L2ReputationData& data = EnsureReputationData(address);
    
    // Update activity metrics
    data.l2TransactionCount++;
    data.lastL2Activity = activity.blockNumber;
    
    if (activity.success) {
        data.l2VolumeTraded += activity.value;
        
        if (activity.type == L2Activity::Type::CONTRACT_CALL ||
            activity.type == L2Activity::Type::CONTRACT_DEPLOY) {
            data.successfulContractCalls++;
        }
    } else {
        data.failedTransactions++;
    }
    
    // Recalculate scores
    data.l2BehaviorScore = CalculateBehaviorScore(data);
    data.l2EconomicScore = CalculateEconomicScore(data);
    
    RecalculateAggregatedScore(address);
}

void L2ReputationManager::RecordTransaction(const uint160& address, CAmount value, 
                                            uint64_t gasUsed, uint64_t blockNumber)
{
    L2Activity activity(L2Activity::Type::TRANSACTION, value, gasUsed, blockNumber, true);
    UpdateL2Reputation(address, activity);
}

void L2ReputationManager::RecordFailedTransaction(const uint160& address, uint64_t blockNumber)
{
    L2Activity activity(L2Activity::Type::FAILED_TX, 0, 0, blockNumber, false);
    UpdateL2Reputation(address, activity);
}

void L2ReputationManager::RecordContractCall(const uint160& address, CAmount value,
                                             uint64_t gasUsed, uint64_t blockNumber, bool success)
{
    L2Activity activity(L2Activity::Type::CONTRACT_CALL, value, gasUsed, blockNumber, success);
    UpdateL2Reputation(address, activity);
}

// ============================================================================
// L1 Sync (Requirement 10.4)
// ============================================================================

ReputationSyncRequest L2ReputationManager::SyncToL1(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    ReputationSyncRequest request;
    request.address = address;
    request.chainId = chainId_;
    
    if (auto it = reputationCache_.find(address); it != reputationCache_.end()) {
        const L2ReputationData& data = it->second;
        request.l2AggregatedScore = data.aggregatedScore;
        request.l2TransactionCount = data.l2TransactionCount;
        request.l2VolumeTraded = data.l2VolumeTraded;
        request.l2BlockNumber = data.lastL2Activity;
    }
    
    request.timestamp = GetTime();
    
    return request;
}

bool L2ReputationManager::NeedsL1Sync(const uint160& address, uint64_t /* currentL2Block */) const
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return false;
    }
    
    const L2ReputationData& data = it->second;
    
    // Need sync if enough blocks have passed since last sync
    if (data.lastL2Activity > data.lastL1Sync + L1_REPUTATION_SYNC_INTERVAL) {
        return true;
    }
    
    // Need sync if significant activity has occurred
    if (data.l2TransactionCount >= MIN_L2_TRANSACTIONS_FOR_REPUTATION && 
        data.lastL1Sync == 0) {
        return true;
    }
    
    return false;
}

std::vector<uint160> L2ReputationManager::GetAddressesNeedingSync(uint64_t currentL2Block) const
{
    LOCK(cs_reputation_);
    
    std::vector<uint160> addresses;
    
    for (const auto& [addr, data] : reputationCache_) {
        if (NeedsL1Sync(addr, currentL2Block)) {
            addresses.push_back(addr);
        }
    }
    
    return addresses;
}

// ============================================================================
// Reputation Benefits (Requirements 6.1, 6.2, 18.5)
// ============================================================================

ReputationBenefits L2ReputationManager::GetBenefits(const uint160& address) const
{
    uint32_t score = GetAggregatedReputation(address);
    return CalculateBenefits(score);
}

bool L2ReputationManager::QualifiesForFastWithdrawal(const uint160& address) const
{
    uint32_t score = GetAggregatedReputation(address);
    return score >= REPUTATION_FAST_WITHDRAWAL_THRESHOLD;
}

uint32_t L2ReputationManager::GetGasDiscount(const uint160& address) const
{
    uint32_t score = GetAggregatedReputation(address);
    
    if (score < REPUTATION_GAS_DISCOUNT_THRESHOLD) {
        return 0;
    }
    
    // Linear scaling from threshold to 100
    // At threshold (70): 0% discount
    // At 100: 50% discount
    uint32_t range = 100 - REPUTATION_GAS_DISCOUNT_THRESHOLD;
    uint32_t scoreAboveThreshold = score - REPUTATION_GAS_DISCOUNT_THRESHOLD;
    
    return (scoreAboveThreshold * MAX_GAS_DISCOUNT_PERCENT) / range;
}

uint32_t L2ReputationManager::GetRateLimitMultiplier(const uint160& address) const
{
    uint32_t score = GetAggregatedReputation(address);
    
    // Base multiplier is 1
    // Score 0-49: multiplier 1
    // Score 50-69: multiplier 2
    // Score 70-79: multiplier 5
    // Score 80-89: multiplier 7
    // Score 90-100: multiplier 10
    
    if (score < 50) return 1;
    if (score < 70) return 2;
    if (score < 80) return 5;
    if (score < 90) return 7;
    return 10;
}

bool L2ReputationManager::HasInstantSoftFinality(const uint160& address) const
{
    uint32_t score = GetAggregatedReputation(address);
    return score > 80;  // Requirement 6.1: reputation >80
}

// ============================================================================
// Anti-Gaming (Requirement 10.5)
// ============================================================================

bool L2ReputationManager::DetectReputationGaming(const uint160& address) const
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return false;
    }
    
    const L2ReputationData& data = it->second;
    
    // Check for suspicious patterns:
    
    // 1. Very high transaction count with very low volume (wash trading)
    if (data.l2TransactionCount > 100 && data.l2VolumeTraded < 1 * COIN) {
        return true;
    }
    
    // 2. High failure rate (possible spam/attack)
    if (data.l2TransactionCount > 20) {
        uint32_t successRate = data.GetSuccessRate();
        if (successRate < 50) {
            return true;
        }
    }
    
    // 3. Large discrepancy between L1 and L2 scores (possible gaming)
    if (data.l1HatScore > 0 && data.l2TransactionCount > MIN_L2_TRANSACTIONS_FOR_REPUTATION) {
        int32_t scoreDiff = static_cast<int32_t>(data.l2BehaviorScore) - 
                           static_cast<int32_t>(data.l1HatScore);
        // If L2 score is much higher than L1 score, suspicious
        if (scoreDiff > 30) {
            return true;
        }
    }
    
    // 4. Already flagged
    if (data.flaggedForReview) {
        return true;
    }
    
    return false;
}

void L2ReputationManager::FlagForReview(const uint160& address, const std::string& /* reason */)
{
    LOCK(cs_reputation_);
    
    L2ReputationData& data = EnsureReputationData(address);
    data.flaggedForReview = true;
    
    // Recalculate score (will be capped at 50)
    RecalculateAggregatedScore(address);
}

void L2ReputationManager::ClearFlag(const uint160& address)
{
    LOCK(cs_reputation_);
    
    auto it = reputationCache_.find(address);
    if (it != reputationCache_.end()) {
        it->second.flaggedForReview = false;
    }
}

// ============================================================================
// Utility Methods
// ============================================================================

size_t L2ReputationManager::GetAddressCount() const
{
    LOCK(cs_reputation_);
    return reputationCache_.size();
}

void L2ReputationManager::Clear()
{
    LOCK(cs_reputation_);
    reputationCache_.clear();
}

ReputationBenefits L2ReputationManager::CalculateBenefits(uint32_t score)
{
    ReputationBenefits benefits;
    
    // Gas discount (Requirement 18.5)
    if (score >= REPUTATION_GAS_DISCOUNT_THRESHOLD) {
        uint32_t range = 100 - REPUTATION_GAS_DISCOUNT_THRESHOLD;
        uint32_t scoreAboveThreshold = score - REPUTATION_GAS_DISCOUNT_THRESHOLD;
        benefits.gasDiscountPercent = (scoreAboveThreshold * MAX_GAS_DISCOUNT_PERCENT) / range;
    }
    
    // Challenge period (Requirement 6.2)
    if (score >= REPUTATION_FAST_WITHDRAWAL_THRESHOLD) {
        benefits.challengePeriodSeconds = 1 * 24 * 60 * 60;  // 1 day for high rep
        benefits.qualifiesForFastWithdrawal = true;
    } else if (score >= 60) {
        benefits.challengePeriodSeconds = 3 * 24 * 60 * 60;  // 3 days for medium rep
    } else {
        benefits.challengePeriodSeconds = 7 * 24 * 60 * 60;  // 7 days default
    }
    
    // Rate limit multiplier
    if (score < 50) {
        benefits.rateLimitMultiplier = 1;
    } else if (score < 70) {
        benefits.rateLimitMultiplier = 2;
    } else if (score < 80) {
        benefits.rateLimitMultiplier = 5;
    } else if (score < 90) {
        benefits.rateLimitMultiplier = 7;
    } else {
        benefits.rateLimitMultiplier = 10;
    }
    
    // Instant soft-finality (Requirement 6.1)
    benefits.instantSoftFinality = (score > 80);
    
    // Priority level (0-10)
    benefits.priorityLevel = score / 10;
    
    // Max withdrawal without verification
    if (score >= 90) {
        benefits.maxWithdrawalWithoutVerification = 100000 * COIN;  // 100,000 CAS
    } else if (score >= 80) {
        benefits.maxWithdrawalWithoutVerification = 50000 * COIN;   // 50,000 CAS
    } else if (score >= 70) {
        benefits.maxWithdrawalWithoutVerification = 25000 * COIN;   // 25,000 CAS
    } else {
        benefits.maxWithdrawalWithoutVerification = 10000 * COIN;   // 10,000 CAS default
    }
    
    return benefits;
}

uint32_t L2ReputationManager::CalculateAggregatedScore(uint32_t l1Score, uint32_t l2Behavior, uint32_t l2Economic)
{
    // Weighted average: 60% L1, 25% L2 behavior, 15% L2 economic
    // This is a deterministic function per Requirement 10.3
    
    uint32_t weighted = (l1Score * L1_REPUTATION_WEIGHT +
                        l2Behavior * L2_BEHAVIOR_WEIGHT +
                        l2Economic * L2_ECONOMIC_WEIGHT) / 100;
    
    // Clamp to 0-100
    return std::min(weighted, 100u);
}

// ============================================================================
// Private Methods
// ============================================================================

uint32_t L2ReputationManager::CalculateBehaviorScore(const L2ReputationData& data)
{
    // Behavior score based on:
    // - Transaction count (activity level)
    // - Success rate
    // - Contract interactions
    
    if (data.l2TransactionCount < MIN_L2_TRANSACTIONS_FOR_REPUTATION) {
        return 0;  // Not enough activity to calculate
    }
    
    // Base score from transaction count (max 40 points)
    uint32_t activityScore = 0;
    if (data.l2TransactionCount >= 1000) {
        activityScore = 40;
    } else if (data.l2TransactionCount >= 100) {
        activityScore = 30;
    } else if (data.l2TransactionCount >= 50) {
        activityScore = 20;
    } else if (data.l2TransactionCount >= MIN_L2_TRANSACTIONS_FOR_REPUTATION) {
        activityScore = 10;
    }
    
    // Success rate score (max 40 points)
    uint32_t successRate = data.GetSuccessRate();
    uint32_t successScore = (successRate * 40) / 100;
    
    // Contract interaction score (max 20 points)
    uint32_t contractScore = 0;
    if (data.successfulContractCalls >= 100) {
        contractScore = 20;
    } else if (data.successfulContractCalls >= 50) {
        contractScore = 15;
    } else if (data.successfulContractCalls >= 10) {
        contractScore = 10;
    } else if (data.successfulContractCalls >= 1) {
        contractScore = 5;
    }
    
    return std::min(activityScore + successScore + contractScore, 100u);
}

uint32_t L2ReputationManager::CalculateEconomicScore(const L2ReputationData& data)
{
    // Economic score based on volume traded
    // Logarithmic scaling to prevent whales from dominating
    
    if (data.l2VolumeTraded == 0) {
        return 0;
    }
    
    // Convert to CAS units
    double volumeInCas = static_cast<double>(data.l2VolumeTraded) / COIN;
    
    // Logarithmic scaling
    // 1 CAS = ~10 points
    // 10 CAS = ~20 points
    // 100 CAS = ~30 points
    // 1000 CAS = ~40 points
    // 10000 CAS = ~50 points
    // 100000+ CAS = ~60-100 points
    
    double score = 10.0 * std::log10(volumeInCas + 1.0);
    
    // Additional bonus for high volume
    if (volumeInCas >= 100000) {
        score += 40;
    } else if (volumeInCas >= 10000) {
        score += 30;
    } else if (volumeInCas >= 1000) {
        score += 20;
    }
    
    return std::min(static_cast<uint32_t>(score), 100u);
}

void L2ReputationManager::RecalculateAggregatedScore(const uint160& address)
{
    // Note: Must be called with cs_reputation_ held
    
    auto it = reputationCache_.find(address);
    if (it == reputationCache_.end()) {
        return;
    }
    
    L2ReputationData& data = it->second;
    
    // Calculate the base score first
    uint32_t baseScore;
    
    // If no significant L2 activity, use only L1 score
    // This prevents new users from being penalized for not having L2 history
    if (data.l2TransactionCount < MIN_L2_TRANSACTIONS_FOR_REPUTATION) {
        baseScore = data.l1HatScore;
    } else {
        baseScore = CalculateAggregatedScore(
            data.l1HatScore,
            data.l2BehaviorScore,
            data.l2EconomicScore
        );
    }
    
    // If flagged for review, cap the score at 50
    if (data.flaggedForReview) {
        data.aggregatedScore = std::min(baseScore, 50u);
    } else {
        data.aggregatedScore = baseScore;
    }
}

L2ReputationData& L2ReputationManager::EnsureReputationData(const uint160& address)
{
    // Note: Must be called with cs_reputation_ held
    
    if (auto it = reputationCache_.find(address); it == reputationCache_.end()) {
        return reputationCache_[address];  // Default constructs if not present
    } else {
        return it->second;
    }
}

} // namespace l2
