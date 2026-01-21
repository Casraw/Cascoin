// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_REWARD_DISTRIBUTOR_H
#define CASCOIN_CVM_REWARD_DISTRIBUTOR_H

#include <cvm/reward_types.h>
#include <cvm/trustgraph.h>
#include <uint256.h>
#include <amount.h>
#include <map>
#include <vector>
#include <string>

namespace CVM {

// Forward declarations
class CVMDatabase;

/**
 * RewardDistributor Class
 * 
 * Responsible for calculating and distributing rewards after DAO dispute resolution.
 * 
 * For successful challenges (slash decision):
 * - Challenger gets bond return (100%) + bounty (configurable % of slashed bond)
 * - DAO voters on winning side get proportional rewards (configurable % of slashed bond)
 * - Remainder is burned for deflation (configurable %)
 * 
 * For failed challenges (keep decision):
 * - Challenger forfeits bond
 * - Wrongly accused voter gets compensation (configurable % of forfeited bond)
 * - Remainder is burned for deflation
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 2.1, 2.2, 2.3, 3.1, 3.3, 3.4, 3.5
 */
class RewardDistributor {
public:
    explicit RewardDistributor(CVMDatabase& db, const WoTConfig& config);
    virtual ~RewardDistributor() = default;
    
    /**
     * Distribute rewards after a successful slash decision
     * 
     * @param dispute The resolved dispute
     * @param slashedBond Amount slashed from malicious voter
     * @return true if distribution successful
     * 
     * Distribution breakdown:
     * - Challenger bond return: 100% of original challenge bond
     * - Challenger bounty: challengerRewardPercent% of slashed bond
     * - DAO voter rewards: daoVoterRewardPercent% of slashed bond (proportional to stakes)
     * - Burn: burnPercent% of slashed bond + any rounding remainder
     * 
     * Edge cases:
     * - No voters on winning side: voter portion goes to challenger
     * - All voters on losing side: voter portion is burned
     * 
     * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 5.1, 5.2, 5.4, 5.5, 6.1, 6.2, 6.4
     */
    bool DistributeSlashRewards(const DAODispute& dispute, CAmount slashedBond);
    
    /**
     * Distribute rewards after a failed challenge (no slash)
     * 
     * @param dispute The resolved dispute
     * @param originalVoter Address of the wrongly accused voter
     * @return true if distribution successful
     * 
     * Distribution breakdown:
     * - Challenger bond: forfeited (no return)
     * - Wrongly accused compensation: wronglyAccusedRewardPercent% of forfeited bond
     * - Burn: failedChallengeBurnPercent% of forfeited bond
     * 
     * Edge case:
     * - Invalid voter address: entire bond is burned
     * 
     * Requirements: 2.1, 2.2, 2.3, 2.4, 6.1, 6.2
     */
    bool DistributeFailedChallengeRewards(const DAODispute& dispute, 
                                          const uint160& originalVoter);
    
    /**
     * Get all pending rewards for an address
     * 
     * @param recipient Address to query
     * @return Vector of pending (unclaimed) rewards
     * 
     * Requirements: 3.5
     */
    std::vector<PendingReward> GetPendingRewards(const uint160& recipient) const;
    
    /**
     * Get all claimed rewards for an address (claim history)
     * 
     * @param recipient Address to query
     * @return Vector of claimed rewards
     * 
     * Requirements: 7.1, 7.2 (Dashboard claim history)
     */
    std::vector<PendingReward> GetClaimedRewards(const uint160& recipient) const;
    
    /**
     * Get all rewards (both pending and claimed) for an address
     * 
     * @param recipient Address to query
     * @return Vector of all rewards
     */
    std::vector<PendingReward> GetAllRewards(const uint160& recipient) const;
    
    /**
     * Claim a pending reward
     * 
     * @param rewardId Unique reward identifier
     * @param recipient Address claiming the reward
     * @return Amount claimed, or 0 if claim failed
     * 
     * Verifies:
     * - Reward exists
     * - Recipient matches
     * - Not already claimed
     * 
     * Requirements: 3.3, 3.4, 6.3
     */
    CAmount ClaimReward(const uint256& rewardId, const uint160& recipient);
    
    /**
     * Get reward distribution details for a dispute
     * 
     * @param disputeId Dispute to query
     * @return RewardDistribution record (empty if not found)
     */
    RewardDistribution GetRewardDistribution(const uint256& disputeId) const;
    
    /**
     * Check if a reward exists
     * 
     * @param rewardId Reward ID to check
     * @return true if reward exists
     */
    bool RewardExists(const uint256& rewardId) const;
    
    /**
     * Get a specific pending reward
     * 
     * @param rewardId Reward ID
     * @param reward Output reward
     * @return true if found
     */
    bool GetReward(const uint256& rewardId, PendingReward& reward) const;
    
    /**
     * Get current timestamp for reward creation
     * Virtual for testing
     */
    virtual uint32_t GetCurrentTimestamp() const;

private:
    CVMDatabase& database;
    const WoTConfig& config;
    
    /**
     * Calculate DAO voter rewards proportionally based on stakes
     * 
     * @param dispute The dispute with voter stakes
     * @param totalVoterPool Total amount to distribute to voters
     * @param winningSide true if calculating for slash voters, false for keep voters
     * @return Map of voter address to reward amount
     * 
     * Uses integer arithmetic to avoid rounding errors.
     * Any remainder from rounding is returned separately.
     */
    std::pair<std::map<uint160, CAmount>, CAmount> CalculateVoterRewards(
        const DAODispute& dispute,
        CAmount totalVoterPool,
        bool winningSide
    ) const;
    
    /**
     * Store a pending reward in the database
     * 
     * @param reward Reward to store
     * @return true if successful
     */
    bool StorePendingReward(const PendingReward& reward);
    
    /**
     * Update an existing reward in the database
     * 
     * @param reward Reward to update
     * @return true if successful
     */
    bool UpdateReward(const PendingReward& reward);
    
    /**
     * Store a reward distribution record
     * 
     * @param distribution Distribution to store
     * @return true if successful
     */
    bool StoreRewardDistribution(const RewardDistribution& distribution);
    
    /**
     * Emit a reward event for logging/transparency
     * 
     * @param eventType Type of event (e.g., "RewardDistributed", "BondBurned", "RewardClaimed")
     * @param disputeId Related dispute
     * @param recipient Recipient address (if applicable)
     * @param amount Amount involved
     */
    void EmitRewardEvent(const std::string& eventType,
                         const uint256& disputeId,
                         const uint160& recipient,
                         CAmount amount);
    
    /**
     * Add reward to recipient index for efficient querying
     * 
     * @param recipient Recipient address
     * @param rewardId Reward ID to add
     * @return true if successful
     */
    bool AddToRecipientIndex(const uint160& recipient, const uint256& rewardId);
    
    /**
     * Get total stake on a specific side of the dispute
     * 
     * @param dispute The dispute
     * @param side true for slash side, false for keep side
     * @return Total stake on that side
     */
    CAmount GetTotalStakeOnSide(const DAODispute& dispute, bool side) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_REWARD_DISTRIBUTOR_H
