// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_FEE_DISTRIBUTOR_H
#define CASCOIN_L2_FEE_DISTRIBUTOR_H

/**
 * @file fee_distributor.h
 * @brief Gas Fee Distribution for Cascoin Layer 2
 * 
 * This file implements the gas fee distribution mechanism for L2,
 * distributing fees according to the 70/20/10 split:
 * - 70% to the active sequencer (block producer)
 * - 20% to other sequencers (shared pool)
 * - 10% burned (deflationary mechanism)
 * 
 * Key features:
 * - 70/20/10 fee split implementation
 * - Sequencer reward tracking
 * - Fee burning mechanism
 * - Uptime bonus rewards
 * - Penalty for missed blocks
 * 
 * Requirements: 18.2, 38.2
 */

#include <l2/l2_common.h>
#include <l2/l2_transaction.h>
#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>
#include <deque>
#include <optional>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Percentage of fees going to block producer (Requirement 18.2) */
static constexpr uint32_t FEE_BLOCK_PRODUCER_PERCENT = 70;

/** Percentage of fees going to other sequencers (Requirement 18.2) */
static constexpr uint32_t FEE_OTHER_SEQUENCERS_PERCENT = 20;

/** Percentage of fees burned (Requirement 18.2) */
static constexpr uint32_t FEE_BURN_PERCENT = 10;

/** Uptime threshold for bonus rewards (99.9%) (Requirement 38.3) */
static constexpr uint32_t UPTIME_BONUS_THRESHOLD_PERMILLE = 999;

/** Uptime bonus percentage (10% extra) */
static constexpr uint32_t UPTIME_BONUS_PERCENT = 10;

/** Penalty duration for missed blocks in blocks (24 hours at 2s blocks) */
static constexpr uint64_t MISSED_BLOCK_PENALTY_DURATION = 43200;

/** Penalty reduction percentage for missed blocks (Requirement 38.4) */
static constexpr uint32_t MISSED_BLOCK_PENALTY_PERCENT = 50;

/** Number of blocks to track for uptime calculation */
static constexpr uint64_t UPTIME_TRACKING_WINDOW = 100000;

/** Minimum blocks produced to qualify for uptime bonus */
static constexpr uint64_t MIN_BLOCKS_FOR_UPTIME_BONUS = 1000;

/** Minimum transaction fee in satoshis (0.00001 L2-Token) - Requirement 6.6 */
static constexpr CAmount MIN_TRANSACTION_FEE = 1000;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Reward information for a single sequencer
 * 
 * Tracks accumulated rewards and performance metrics for a sequencer.
 */
struct SequencerRewardInfo {
    /** Sequencer address */
    uint160 sequencerAddress;
    
    /** Total rewards earned (unclaimed) */
    CAmount totalRewards;
    
    /** Rewards from block production (70% share) */
    CAmount blockProductionRewards;
    
    /** Rewards from shared pool (20% share) */
    CAmount sharedPoolRewards;
    
    /** Bonus rewards from uptime */
    CAmount uptimeBonus;
    
    /** Total rewards claimed */
    CAmount claimedRewards;
    
    /** Number of blocks produced */
    uint64_t blocksProduced;
    
    /** Number of blocks missed (when was leader) */
    uint64_t blocksMissed;
    
    /** Last block produced */
    uint64_t lastBlockProduced;
    
    /** Last block where sequencer was active */
    uint64_t lastActiveBlock;
    
    /** Penalty expiration block (0 if no penalty) */
    uint64_t penaltyExpiresBlock;
    
    /** Reputation score (cached) */
    uint32_t reputationScore;
    
    /** Stake amount (cached) */
    CAmount stakeAmount;

    /** Default constructor */
    SequencerRewardInfo()
        : totalRewards(0)
        , blockProductionRewards(0)
        , sharedPoolRewards(0)
        , uptimeBonus(0)
        , claimedRewards(0)
        , blocksProduced(0)
        , blocksMissed(0)
        , lastBlockProduced(0)
        , lastActiveBlock(0)
        , penaltyExpiresBlock(0)
        , reputationScore(0)
        , stakeAmount(0)
    {}

    /** Constructor with address */
    explicit SequencerRewardInfo(const uint160& addr)
        : sequencerAddress(addr)
        , totalRewards(0)
        , blockProductionRewards(0)
        , sharedPoolRewards(0)
        , uptimeBonus(0)
        , claimedRewards(0)
        , blocksProduced(0)
        , blocksMissed(0)
        , lastBlockProduced(0)
        , lastActiveBlock(0)
        , penaltyExpiresBlock(0)
        , reputationScore(0)
        , stakeAmount(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(totalRewards);
        READWRITE(blockProductionRewards);
        READWRITE(sharedPoolRewards);
        READWRITE(uptimeBonus);
        READWRITE(claimedRewards);
        READWRITE(blocksProduced);
        READWRITE(blocksMissed);
        READWRITE(lastBlockProduced);
        READWRITE(lastActiveBlock);
        READWRITE(penaltyExpiresBlock);
        READWRITE(reputationScore);
        READWRITE(stakeAmount);
    }

    /**
     * @brief Get unclaimed rewards
     * @return Amount of unclaimed rewards
     */
    CAmount GetUnclaimedRewards() const {
        return totalRewards - claimedRewards;
    }

    /**
     * @brief Calculate uptime percentage (permille)
     * @return Uptime in permille (0-1000)
     */
    uint32_t GetUptimePermille() const {
        uint64_t totalExpected = blocksProduced + blocksMissed;
        if (totalExpected == 0) return 1000;  // Perfect if no blocks expected
        return static_cast<uint32_t>((blocksProduced * 1000) / totalExpected);
    }

    /**
     * @brief Check if sequencer qualifies for uptime bonus
     * @return true if uptime >= 99.9% and enough blocks produced
     */
    bool QualifiesForUptimeBonus() const {
        return GetUptimePermille() >= UPTIME_BONUS_THRESHOLD_PERMILLE &&
               blocksProduced >= MIN_BLOCKS_FOR_UPTIME_BONUS;
    }

    /**
     * @brief Check if sequencer is currently penalized
     * @param currentBlock Current block number
     * @return true if under penalty
     */
    bool IsPenalized(uint64_t currentBlock) const {
        return penaltyExpiresBlock > currentBlock;
    }
};

/**
 * @brief Fee distribution result for a single block
 */
struct FeeDistributionResult {
    /** Block number */
    uint64_t blockNumber;
    
    /** Total fees collected */
    CAmount totalFees;
    
    /** Amount to block producer */
    CAmount blockProducerAmount;
    
    /** Amount to shared pool */
    CAmount sharedPoolAmount;
    
    /** Amount burned */
    CAmount burnedAmount;
    
    /** Block producer address */
    uint160 blockProducer;
    
    /** Number of sequencers sharing the pool */
    uint32_t sequencerCount;
    
    /** Per-sequencer share from pool */
    CAmount perSequencerShare;
    
    /** Uptime bonus applied */
    CAmount uptimeBonusApplied;
    
    /** Whether penalty was applied */
    bool penaltyApplied;

    FeeDistributionResult()
        : blockNumber(0)
        , totalFees(0)
        , blockProducerAmount(0)
        , sharedPoolAmount(0)
        , burnedAmount(0)
        , sequencerCount(0)
        , perSequencerShare(0)
        , uptimeBonusApplied(0)
        , penaltyApplied(false)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(totalFees);
        READWRITE(blockProducerAmount);
        READWRITE(sharedPoolAmount);
        READWRITE(burnedAmount);
        READWRITE(blockProducer);
        READWRITE(sequencerCount);
        READWRITE(perSequencerShare);
        READWRITE(uptimeBonusApplied);
        READWRITE(penaltyApplied);
    }

    /**
     * @brief Verify the distribution is correct (sums to total)
     * @return true if distribution is valid
     */
    bool IsValid() const {
        CAmount distributed = blockProducerAmount + sharedPoolAmount + burnedAmount;
        // Allow for rounding differences of up to 2 satoshis
        return distributed >= totalFees - 2 && distributed <= totalFees;
    }
};

/**
 * @brief Summary of total burned fees
 */
struct BurnSummary {
    /** Total fees burned since genesis */
    CAmount totalBurned;
    
    /** Fees burned in last 24 hours */
    CAmount burned24h;
    
    /** Fees burned in last 7 days */
    CAmount burned7d;
    
    /** Last block with burn */
    uint64_t lastBurnBlock;

    BurnSummary()
        : totalBurned(0)
        , burned24h(0)
        , burned7d(0)
        , lastBurnBlock(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(totalBurned);
        READWRITE(burned24h);
        READWRITE(burned7d);
        READWRITE(lastBurnBlock);
    }
};

/**
 * @brief Fee distribution for a single block (Burn-and-Mint model)
 * 
 * Tracks fee distribution for a block where sequencer rewards come
 * exclusively from transaction fees (no minting).
 * 
 * Requirements: 6.1, 6.2, 6.3, 6.4
 */
struct BlockFeeDistribution {
    /** Block number */
    uint64_t blockNumber;
    
    /** Sequencer address (block producer) */
    uint160 sequencerAddress;
    
    /** Total fees collected in this block */
    CAmount totalFees;
    
    /** Number of transactions in the block */
    uint32_t transactionCount;
    
    /** Timestamp when fees were distributed */
    uint64_t timestamp;

    BlockFeeDistribution()
        : blockNumber(0)
        , totalFees(0)
        , transactionCount(0)
        , timestamp(0)
    {}

    BlockFeeDistribution(uint64_t block, const uint160& sequencer, CAmount fees, uint32_t txCount, uint64_t ts)
        : blockNumber(block)
        , sequencerAddress(sequencer)
        , totalFees(fees)
        , transactionCount(txCount)
        , timestamp(ts)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(sequencerAddress);
        READWRITE(totalFees);
        READWRITE(transactionCount);
        READWRITE(timestamp);
    }
};

// ============================================================================
// Fee Distributor Class
// ============================================================================

/**
 * @brief L2 Fee Distributor
 * 
 * Manages the distribution of gas fees collected from L2 transactions.
 * Implements the 70/20/10 split between block producer, other sequencers,
 * and burning.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 18.2, 38.2
 */
class FeeDistributor {
public:
    /**
     * @brief Construct a new Fee Distributor
     */
    FeeDistributor();

    // =========================================================================
    // Fee Distribution (Requirements 18.2, 38.2)
    // =========================================================================

    /**
     * @brief Distribute fees for a block
     * @param blockNumber Block number
     * @param totalFees Total fees collected in the block
     * @param blockProducer Address of the block producer
     * @param activeSequencers List of active sequencer addresses
     * @return FeeDistributionResult with distribution details
     * 
     * Requirement 18.2: Distribute L2 fees: 70% to block producer, 20% to
     * other sequencers, 10% burned
     */
    FeeDistributionResult DistributeFees(
        uint64_t blockNumber,
        CAmount totalFees,
        const uint160& blockProducer,
        const std::vector<uint160>& activeSequencers
    );

    /**
     * @brief Calculate fee distribution without applying it
     * @param totalFees Total fees to distribute
     * @param blockProducer Block producer address
     * @param sequencerCount Number of active sequencers
     * @param currentBlock Current block number
     * @return FeeDistributionResult with calculated amounts
     */
    FeeDistributionResult CalculateDistribution(
        CAmount totalFees,
        const uint160& blockProducer,
        uint32_t sequencerCount,
        uint64_t currentBlock
    ) const;

    // =========================================================================
    // Sequencer Reward Tracking (Requirement 38.1, 38.2)
    // =========================================================================

    /**
     * @brief Get reward info for a sequencer
     * @param sequencer Sequencer address
     * @return SequencerRewardInfo (empty if not found)
     */
    std::optional<SequencerRewardInfo> GetSequencerRewards(const uint160& sequencer) const;

    /**
     * @brief Get total unclaimed rewards for a sequencer
     * @param sequencer Sequencer address
     * @return Unclaimed reward amount
     */
    CAmount GetUnclaimedRewards(const uint160& sequencer) const;

    /**
     * @brief Claim rewards for a sequencer
     * @param sequencer Sequencer address
     * @param amount Amount to claim (0 = claim all)
     * @return Amount actually claimed
     * 
     * Requirement 38.5: Enable automatic reward claiming
     */
    CAmount ClaimRewards(const uint160& sequencer, CAmount amount = 0);

    /**
     * @brief Get all sequencers with unclaimed rewards
     * @return Map of sequencer address to unclaimed amount
     */
    std::map<uint160, CAmount> GetAllUnclaimedRewards() const;

    // =========================================================================
    // Block Production Tracking (Requirements 38.3, 38.4)
    // =========================================================================

    /**
     * @brief Record a block produced by a sequencer
     * @param sequencer Sequencer address
     * @param blockNumber Block number
     * 
     * Updates block production stats for uptime calculation.
     */
    void RecordBlockProduced(const uint160& sequencer, uint64_t blockNumber);

    /**
     * @brief Record a missed block by a sequencer
     * @param sequencer Sequencer address
     * @param blockNumber Block number
     * 
     * Requirement 38.4: Penalize sequencers for missed blocks
     */
    void RecordMissedBlock(const uint160& sequencer, uint64_t blockNumber);

    /**
     * @brief Get uptime percentage for a sequencer
     * @param sequencer Sequencer address
     * @return Uptime in permille (0-1000)
     */
    uint32_t GetUptimePermille(const uint160& sequencer) const;

    /**
     * @brief Check if sequencer qualifies for uptime bonus
     * @param sequencer Sequencer address
     * @return true if qualifies for bonus
     * 
     * Requirement 38.3: Provide bonus rewards for consistent uptime (>99.9%)
     */
    bool QualifiesForUptimeBonus(const uint160& sequencer) const;

    // =========================================================================
    // Fee Burning
    // =========================================================================

    /**
     * @brief Get total fees burned
     * @return Total burned amount
     */
    CAmount GetTotalBurned() const;

    /**
     * @brief Get burn summary
     * @return BurnSummary with burn statistics
     */
    BurnSummary GetBurnSummary() const;

    // =========================================================================
    // Burn-and-Mint Fee Distribution (Requirements 6.1-6.6)
    // =========================================================================

    /**
     * @brief Distribute block fees to the block producer
     * 
     * In the burn-and-mint model, sequencer rewards come ONLY from
     * transaction fees. No new tokens are minted as block rewards.
     * 
     * @param blockNumber Block number
     * @param sequencer Block producer address
     * @param transactions Transactions in the block
     * @return true if distribution succeeded
     * 
     * Requirements: 6.1, 6.2, 6.3, 6.4
     */
    bool DistributeBlockFees(
        uint64_t blockNumber,
        const uint160& sequencer,
        const std::vector<L2Transaction>& transactions
    );

    /**
     * @brief Calculate total fees in a block
     * 
     * Sums up all transaction fees (gasUsed * gasPrice) for transactions
     * in the block.
     * 
     * @param transactions Transactions in the block
     * @return Total fees in satoshis
     * 
     * Requirement: 6.3
     */
    CAmount CalculateBlockFees(const std::vector<L2Transaction>& transactions) const;

    /**
     * @brief Get fee history for a sequencer
     * 
     * Returns the fee distribution history for a specific sequencer
     * within a block range.
     * 
     * @param sequencer Sequencer address
     * @param fromBlock Start block (inclusive)
     * @param toBlock End block (inclusive)
     * @return Vector of BlockFeeDistribution records
     * 
     * Requirement: 6.4
     */
    std::vector<BlockFeeDistribution> GetFeeHistory(
        const uint160& sequencer,
        uint64_t fromBlock,
        uint64_t toBlock
    ) const;

    /**
     * @brief Get total fees earned by a sequencer
     * 
     * Returns the cumulative fees earned by a sequencer across all blocks.
     * 
     * @param sequencer Sequencer address
     * @return Total fees earned in satoshis
     * 
     * Requirement: 6.4
     */
    CAmount GetTotalFeesEarned(const uint160& sequencer) const;

    /**
     * @brief Validate transaction fee meets minimum requirement
     * 
     * Checks if a transaction's fee meets the minimum fee requirement
     * to prevent spam.
     * 
     * @param tx Transaction to validate
     * @return true if fee is sufficient
     * 
     * Requirement: 6.6
     */
    static bool ValidateMinimumFee(const L2Transaction& tx);

    /**
     * @brief Get the minimum transaction fee
     * @return Minimum fee in satoshis
     */
    static CAmount GetMinTransactionFee() {
        return MIN_TRANSACTION_FEE;
    }

    // =========================================================================
    // Sequencer Management
    // =========================================================================

    /**
     * @brief Register a new sequencer
     * @param sequencer Sequencer address
     * @param reputation Initial reputation score
     * @param stake Initial stake amount
     */
    void RegisterSequencer(const uint160& sequencer, uint32_t reputation, CAmount stake);

    /**
     * @brief Update sequencer reputation
     * @param sequencer Sequencer address
     * @param reputation New reputation score
     */
    void UpdateSequencerReputation(const uint160& sequencer, uint32_t reputation);

    /**
     * @brief Update sequencer stake
     * @param sequencer Sequencer address
     * @param stake New stake amount
     */
    void UpdateSequencerStake(const uint160& sequencer, CAmount stake);

    /**
     * @brief Remove a sequencer
     * @param sequencer Sequencer address
     */
    void RemoveSequencer(const uint160& sequencer);

    /**
     * @brief Get number of registered sequencers
     * @return Sequencer count
     */
    size_t GetSequencerCount() const;

    /**
     * @brief Get all registered sequencer addresses
     * @return Vector of sequencer addresses
     */
    std::vector<uint160> GetAllSequencers() const;

    // =========================================================================
    // Penalty Management
    // =========================================================================

    /**
     * @brief Check if sequencer is penalized
     * @param sequencer Sequencer address
     * @param currentBlock Current block number
     * @return true if under penalty
     */
    bool IsPenalized(const uint160& sequencer, uint64_t currentBlock) const;

    /**
     * @brief Get penalty expiration block
     * @param sequencer Sequencer address
     * @return Block when penalty expires (0 if no penalty)
     */
    uint64_t GetPenaltyExpiration(const uint160& sequencer) const;

    /**
     * @brief Apply penalty to a sequencer
     * @param sequencer Sequencer address
     * @param currentBlock Current block number
     * @param durationBlocks Penalty duration in blocks
     */
    void ApplyPenalty(const uint160& sequencer, uint64_t currentBlock, uint64_t durationBlocks);

    // =========================================================================
    // Distribution History
    // =========================================================================

    /**
     * @brief Get recent distribution results
     * @param count Number of results to return
     * @return Vector of recent FeeDistributionResult
     */
    std::vector<FeeDistributionResult> GetRecentDistributions(size_t count) const;

    /**
     * @brief Get distribution for a specific block
     * @param blockNumber Block number
     * @return FeeDistributionResult (empty if not found)
     */
    std::optional<FeeDistributionResult> GetDistribution(uint64_t blockNumber) const;

    // =========================================================================
    // Utility Methods
    // =========================================================================

    /**
     * @brief Clear all data (for testing)
     */
    void Clear();

    /**
     * @brief Calculate the 70/20/10 split
     * @param totalFees Total fees to split
     * @param blockProducerAmount Output: 70% amount
     * @param sharedPoolAmount Output: 20% amount
     * @param burnAmount Output: 10% amount
     */
    static void CalculateSplit(
        CAmount totalFees,
        CAmount& blockProducerAmount,
        CAmount& sharedPoolAmount,
        CAmount& burnAmount
    );

    /**
     * @brief Calculate uptime bonus
     * @param baseReward Base reward amount
     * @param uptimePermille Uptime in permille
     * @return Bonus amount (0 if not qualified)
     */
    static CAmount CalculateUptimeBonus(CAmount baseReward, uint32_t uptimePermille);

    /**
     * @brief Calculate penalty reduction
     * @param baseReward Base reward amount
     * @return Reduced reward amount
     */
    static CAmount ApplyPenaltyReduction(CAmount baseReward);

private:
    /** Sequencer reward tracking */
    std::map<uint160, SequencerRewardInfo> sequencerRewards_;

    /** Distribution history (recent blocks) */
    std::deque<FeeDistributionResult> distributionHistory_;

    /** Burn tracking */
    BurnSummary burnSummary_;

    /** Burn history for time-based calculations */
    std::deque<std::pair<uint64_t, CAmount>> burnHistory_;

    /** Block fee distribution history (for burn-and-mint model) */
    std::deque<BlockFeeDistribution> blockFeeHistory_;

    /** Total fees earned per sequencer (for burn-and-mint model) */
    std::map<uint160, CAmount> totalFeesEarned_;

    /** Maximum distribution history to keep */
    static constexpr size_t MAX_DISTRIBUTION_HISTORY = 1000;

    /** Maximum block fee history entries */
    static constexpr size_t MAX_BLOCK_FEE_HISTORY = 10000;

    /** Maximum burn history entries */
    static constexpr size_t MAX_BURN_HISTORY = 10000;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_distributor_;

    /**
     * @brief Ensure sequencer reward info exists
     * @param sequencer Sequencer address
     * @return Reference to reward info
     */
    SequencerRewardInfo& EnsureSequencerInfo(const uint160& sequencer);

    /**
     * @brief Update burn summary with new burn
     * @param amount Amount burned
     * @param blockNumber Block number
     */
    void RecordBurn(CAmount amount, uint64_t blockNumber);

    /**
     * @brief Clean up old history entries
     */
    void CleanupHistory();
};

} // namespace l2

#endif // CASCOIN_L2_FEE_DISTRIBUTOR_H
