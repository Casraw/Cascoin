// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file fee_distributor.cpp
 * @brief Implementation of Gas Fee Distribution for Cascoin Layer 2
 * 
 * Requirements: 18.2, 38.2
 */

#include <l2/fee_distributor.h>
#include <util.h>

#include <algorithm>
#include <cassert>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

FeeDistributor::FeeDistributor()
{
    // Initialize burn summary
    burnSummary_ = BurnSummary();
}

// ============================================================================
// Fee Distribution (Requirements 18.2, 38.2)
// ============================================================================

FeeDistributionResult FeeDistributor::DistributeFees(
    uint64_t blockNumber,
    CAmount totalFees,
    const uint160& blockProducer,
    const std::vector<uint160>& activeSequencers)
{
    LOCK(cs_distributor_);

    FeeDistributionResult result;
    result.blockNumber = blockNumber;
    result.totalFees = totalFees;
    result.blockProducer = blockProducer;
    result.sequencerCount = static_cast<uint32_t>(activeSequencers.size());

    if (totalFees == 0) {
        return result;
    }

    // Calculate the 70/20/10 split
    CalculateSplit(totalFees, result.blockProducerAmount, result.sharedPoolAmount, result.burnedAmount);

    // Get block producer info
    SequencerRewardInfo& producerInfo = EnsureSequencerInfo(blockProducer);

    // Check for penalty
    if (producerInfo.IsPenalized(blockNumber)) {
        result.penaltyApplied = true;
        result.blockProducerAmount = ApplyPenaltyReduction(result.blockProducerAmount);
    }

    // Check for uptime bonus
    if (producerInfo.QualifiesForUptimeBonus()) {
        CAmount bonus = CalculateUptimeBonus(result.blockProducerAmount, producerInfo.GetUptimePermille());
        result.uptimeBonusApplied = bonus;
        result.blockProducerAmount += bonus;
    }

    // Credit block producer
    producerInfo.totalRewards += result.blockProducerAmount;
    producerInfo.blockProductionRewards += result.blockProducerAmount;
    if (result.uptimeBonusApplied > 0) {
        producerInfo.uptimeBonus += result.uptimeBonusApplied;
    }

    // Distribute shared pool to other sequencers
    if (result.sequencerCount > 1 && result.sharedPoolAmount > 0) {
        // Exclude block producer from shared pool
        uint32_t otherSequencerCount = result.sequencerCount - 1;
        result.perSequencerShare = result.sharedPoolAmount / otherSequencerCount;

        for (const auto& seq : activeSequencers) {
            if (seq != blockProducer) {
                SequencerRewardInfo& seqInfo = EnsureSequencerInfo(seq);
                seqInfo.totalRewards += result.perSequencerShare;
                seqInfo.sharedPoolRewards += result.perSequencerShare;
            }
        }
    } else if (result.sequencerCount == 1) {
        // Only one sequencer, they get the shared pool too
        producerInfo.totalRewards += result.sharedPoolAmount;
        producerInfo.sharedPoolRewards += result.sharedPoolAmount;
        result.perSequencerShare = result.sharedPoolAmount;
    }

    // Record burn
    RecordBurn(result.burnedAmount, blockNumber);

    // Store in history
    distributionHistory_.push_back(result);
    CleanupHistory();

    return result;
}

FeeDistributionResult FeeDistributor::CalculateDistribution(
    CAmount totalFees,
    const uint160& blockProducer,
    uint32_t sequencerCount,
    uint64_t currentBlock) const
{
    LOCK(cs_distributor_);

    FeeDistributionResult result;
    result.totalFees = totalFees;
    result.blockProducer = blockProducer;
    result.sequencerCount = sequencerCount;

    if (totalFees == 0) {
        return result;
    }

    // Calculate the 70/20/10 split
    CalculateSplit(totalFees, result.blockProducerAmount, result.sharedPoolAmount, result.burnedAmount);

    // Check for penalty
    auto it = sequencerRewards_.find(blockProducer);
    if (it != sequencerRewards_.end()) {
        if (it->second.IsPenalized(currentBlock)) {
            result.penaltyApplied = true;
            result.blockProducerAmount = ApplyPenaltyReduction(result.blockProducerAmount);
        }

        // Check for uptime bonus
        if (it->second.QualifiesForUptimeBonus()) {
            CAmount bonus = CalculateUptimeBonus(result.blockProducerAmount, it->second.GetUptimePermille());
            result.uptimeBonusApplied = bonus;
            result.blockProducerAmount += bonus;
        }
    }

    // Calculate per-sequencer share
    if (sequencerCount > 1) {
        result.perSequencerShare = result.sharedPoolAmount / (sequencerCount - 1);
    } else if (sequencerCount == 1) {
        result.perSequencerShare = result.sharedPoolAmount;
    }

    return result;
}

// ============================================================================
// Sequencer Reward Tracking
// ============================================================================

std::optional<SequencerRewardInfo> FeeDistributor::GetSequencerRewards(const uint160& sequencer) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second;
    }
    return std::nullopt;
}

CAmount FeeDistributor::GetUnclaimedRewards(const uint160& sequencer) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second.GetUnclaimedRewards();
    }
    return 0;
}

CAmount FeeDistributor::ClaimRewards(const uint160& sequencer, CAmount amount)
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it == sequencerRewards_.end()) {
        return 0;
    }

    CAmount unclaimed = it->second.GetUnclaimedRewards();
    if (unclaimed == 0) {
        return 0;
    }

    CAmount toClaim = (amount == 0 || amount > unclaimed) ? unclaimed : amount;
    it->second.claimedRewards += toClaim;

    return toClaim;
}

std::map<uint160, CAmount> FeeDistributor::GetAllUnclaimedRewards() const
{
    LOCK(cs_distributor_);

    std::map<uint160, CAmount> result;
    for (const auto& pair : sequencerRewards_) {
        CAmount unclaimed = pair.second.GetUnclaimedRewards();
        if (unclaimed > 0) {
            result[pair.first] = unclaimed;
        }
    }
    return result;
}

// ============================================================================
// Block Production Tracking
// ============================================================================

void FeeDistributor::RecordBlockProduced(const uint160& sequencer, uint64_t blockNumber)
{
    LOCK(cs_distributor_);

    SequencerRewardInfo& info = EnsureSequencerInfo(sequencer);
    info.blocksProduced++;
    info.lastBlockProduced = blockNumber;
    info.lastActiveBlock = blockNumber;
}

void FeeDistributor::RecordMissedBlock(const uint160& sequencer, uint64_t blockNumber)
{
    LOCK(cs_distributor_);

    SequencerRewardInfo& info = EnsureSequencerInfo(sequencer);
    info.blocksMissed++;

    // Apply penalty
    if (info.penaltyExpiresBlock < blockNumber) {
        info.penaltyExpiresBlock = blockNumber + MISSED_BLOCK_PENALTY_DURATION;
    } else {
        // Extend existing penalty
        info.penaltyExpiresBlock += MISSED_BLOCK_PENALTY_DURATION / 2;
    }
}

uint32_t FeeDistributor::GetUptimePermille(const uint160& sequencer) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second.GetUptimePermille();
    }
    return 1000;  // Perfect uptime if not tracked
}

bool FeeDistributor::QualifiesForUptimeBonus(const uint160& sequencer) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second.QualifiesForUptimeBonus();
    }
    return false;
}

// ============================================================================
// Fee Burning
// ============================================================================

CAmount FeeDistributor::GetTotalBurned() const
{
    LOCK(cs_distributor_);
    return burnSummary_.totalBurned;
}

BurnSummary FeeDistributor::GetBurnSummary() const
{
    LOCK(cs_distributor_);
    return burnSummary_;
}

// ============================================================================
// Sequencer Management
// ============================================================================

void FeeDistributor::RegisterSequencer(const uint160& sequencer, uint32_t reputation, CAmount stake)
{
    LOCK(cs_distributor_);

    SequencerRewardInfo& info = EnsureSequencerInfo(sequencer);
    info.reputationScore = reputation;
    info.stakeAmount = stake;
}

void FeeDistributor::UpdateSequencerReputation(const uint160& sequencer, uint32_t reputation)
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        it->second.reputationScore = reputation;
    }
}

void FeeDistributor::UpdateSequencerStake(const uint160& sequencer, CAmount stake)
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        it->second.stakeAmount = stake;
    }
}

void FeeDistributor::RemoveSequencer(const uint160& sequencer)
{
    LOCK(cs_distributor_);
    sequencerRewards_.erase(sequencer);
}

size_t FeeDistributor::GetSequencerCount() const
{
    LOCK(cs_distributor_);
    return sequencerRewards_.size();
}

std::vector<uint160> FeeDistributor::GetAllSequencers() const
{
    LOCK(cs_distributor_);

    std::vector<uint160> result;
    result.reserve(sequencerRewards_.size());
    for (const auto& pair : sequencerRewards_) {
        result.push_back(pair.first);
    }
    return result;
}

// ============================================================================
// Penalty Management
// ============================================================================

bool FeeDistributor::IsPenalized(const uint160& sequencer, uint64_t currentBlock) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second.IsPenalized(currentBlock);
    }
    return false;
}

uint64_t FeeDistributor::GetPenaltyExpiration(const uint160& sequencer) const
{
    LOCK(cs_distributor_);

    auto it = sequencerRewards_.find(sequencer);
    if (it != sequencerRewards_.end()) {
        return it->second.penaltyExpiresBlock;
    }
    return 0;
}

void FeeDistributor::ApplyPenalty(const uint160& sequencer, uint64_t currentBlock, uint64_t durationBlocks)
{
    LOCK(cs_distributor_);

    SequencerRewardInfo& info = EnsureSequencerInfo(sequencer);
    info.penaltyExpiresBlock = currentBlock + durationBlocks;
}

// ============================================================================
// Distribution History
// ============================================================================

std::vector<FeeDistributionResult> FeeDistributor::GetRecentDistributions(size_t count) const
{
    LOCK(cs_distributor_);

    std::vector<FeeDistributionResult> result;
    size_t start = distributionHistory_.size() > count ? distributionHistory_.size() - count : 0;
    for (size_t i = start; i < distributionHistory_.size(); ++i) {
        result.push_back(distributionHistory_[i]);
    }
    return result;
}

std::optional<FeeDistributionResult> FeeDistributor::GetDistribution(uint64_t blockNumber) const
{
    LOCK(cs_distributor_);

    for (const auto& dist : distributionHistory_) {
        if (dist.blockNumber == blockNumber) {
            return dist;
        }
    }
    return std::nullopt;
}

// ============================================================================
// Utility Methods
// ============================================================================

void FeeDistributor::Clear()
{
    LOCK(cs_distributor_);

    sequencerRewards_.clear();
    distributionHistory_.clear();
    burnSummary_ = BurnSummary();
    burnHistory_.clear();
}

void FeeDistributor::CalculateSplit(
    CAmount totalFees,
    CAmount& blockProducerAmount,
    CAmount& sharedPoolAmount,
    CAmount& burnAmount)
{
    // Calculate exact percentages
    blockProducerAmount = (totalFees * FEE_BLOCK_PRODUCER_PERCENT) / 100;
    sharedPoolAmount = (totalFees * FEE_OTHER_SEQUENCERS_PERCENT) / 100;
    burnAmount = (totalFees * FEE_BURN_PERCENT) / 100;

    // Handle rounding - any remainder goes to burn
    CAmount distributed = blockProducerAmount + sharedPoolAmount + burnAmount;
    if (distributed < totalFees) {
        burnAmount += (totalFees - distributed);
    }
}

CAmount FeeDistributor::CalculateUptimeBonus(CAmount baseReward, uint32_t uptimePermille)
{
    if (uptimePermille < UPTIME_BONUS_THRESHOLD_PERMILLE) {
        return 0;
    }

    // 10% bonus for 99.9%+ uptime
    return (baseReward * UPTIME_BONUS_PERCENT) / 100;
}

CAmount FeeDistributor::ApplyPenaltyReduction(CAmount baseReward)
{
    // 50% reduction during penalty period
    return (baseReward * (100 - MISSED_BLOCK_PENALTY_PERCENT)) / 100;
}

// ============================================================================
// Private Methods
// ============================================================================

SequencerRewardInfo& FeeDistributor::EnsureSequencerInfo(const uint160& sequencer)
{
    auto it = sequencerRewards_.find(sequencer);
    if (it == sequencerRewards_.end()) {
        sequencerRewards_[sequencer] = SequencerRewardInfo(sequencer);
        return sequencerRewards_[sequencer];
    }
    return it->second;
}

void FeeDistributor::RecordBurn(CAmount amount, uint64_t blockNumber)
{
    burnSummary_.totalBurned += amount;
    burnSummary_.lastBurnBlock = blockNumber;

    // Add to history
    burnHistory_.push_back({blockNumber, amount});

    // Update 24h and 7d totals (assuming ~2 second blocks)
    // 24h = 43200 blocks, 7d = 302400 blocks
    const uint64_t blocks24h = 43200;
    const uint64_t blocks7d = 302400;

    burnSummary_.burned24h = 0;
    burnSummary_.burned7d = 0;

    for (const auto& entry : burnHistory_) {
        if (blockNumber - entry.first <= blocks24h) {
            burnSummary_.burned24h += entry.second;
        }
        if (blockNumber - entry.first <= blocks7d) {
            burnSummary_.burned7d += entry.second;
        }
    }

    // Cleanup old entries
    while (!burnHistory_.empty() && blockNumber - burnHistory_.front().first > blocks7d) {
        burnHistory_.pop_front();
    }
}

void FeeDistributor::CleanupHistory()
{
    while (distributionHistory_.size() > MAX_DISTRIBUTION_HISTORY) {
        distributionHistory_.pop_front();
    }
}

} // namespace l2
