// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_REWARD_TYPES_H
#define CASCOIN_CVM_REWARD_TYPES_H

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <map>
#include <vector>

namespace CVM {

/**
 * Reward Type Enumeration
 * 
 * Defines the different types of rewards that can be distributed
 * through the Challenger Reward System.
 */
enum class RewardType : uint8_t {
    CHALLENGER_BOND_RETURN = 0,       // Original bond returned to challenger
    CHALLENGER_BOUNTY = 1,            // Bounty from slashed bond
    DAO_VOTER_REWARD = 2,             // Reward for voting on winning side
    WRONGLY_ACCUSED_COMPENSATION = 3  // Compensation for false accusation
};

/**
 * Convert RewardType to human-readable string
 */
inline std::string RewardTypeToString(RewardType type) {
    switch (type) {
        case RewardType::CHALLENGER_BOND_RETURN:
            return "CHALLENGER_BOND_RETURN";
        case RewardType::CHALLENGER_BOUNTY:
            return "CHALLENGER_BOUNTY";
        case RewardType::DAO_VOTER_REWARD:
            return "DAO_VOTER_REWARD";
        case RewardType::WRONGLY_ACCUSED_COMPENSATION:
            return "WRONGLY_ACCUSED_COMPENSATION";
        default:
            return "UNKNOWN";
    }
}

/**
 * PendingReward Structure
 * 
 * Represents a reward that can be claimed by a recipient.
 * Rewards are stored in the database rather than auto-sent,
 * allowing recipients to claim when convenient.
 * 
 * Requirements: 3.1, 3.2, 6.4
 */
struct PendingReward {
    uint256 rewardId;                                       // Unique identifier (hash of disputeId + recipient + type)
    uint256 disputeId;                                      // Source dispute
    uint160 recipient;                                      // Who can claim this reward
    CAmount amount{0};                                      // Amount in satoshis
    RewardType type{RewardType::CHALLENGER_BOND_RETURN};    // Type of reward
    uint32_t createdTime{0};                                // When reward was created (block timestamp)
    bool claimed{false};                                    // Has it been claimed?
    uint256 claimTxHash;                                    // Transaction that claimed (if any)
    uint32_t claimedTime{0};                                // When claimed (0 if not claimed)
    
    PendingReward() = default;
    
    PendingReward(const uint256& rewardId_, const uint256& disputeId_, 
                  const uint160& recipient_, CAmount amount_, RewardType type_,
                  uint32_t createdTime_)
        : rewardId(rewardId_)
        , disputeId(disputeId_)
        , recipient(recipient_)
        , amount(amount_)
        , type(type_)
        , createdTime(createdTime_)
    {}
    
    /**
     * Generate a unique reward ID from dispute, recipient, and type
     */
    static uint256 GenerateRewardId(const uint256& disputeId, 
                                    const uint160& recipient, 
                                    RewardType type);
    
    /**
     * Check if this reward is valid (non-zero amount, valid recipient)
     */
    bool IsValid() const {
        return amount > 0 && !recipient.IsNull();
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(rewardId);
        READWRITE(disputeId);
        READWRITE(recipient);
        READWRITE(amount);
        
        // Serialize enum as uint8_t
        auto typeInt = static_cast<uint8_t>(type);
        READWRITE(typeInt);
        if (ser_action.ForRead()) {
            type = static_cast<RewardType>(typeInt);
        }
        
        READWRITE(createdTime);
        READWRITE(claimed);
        READWRITE(claimTxHash);
        READWRITE(claimedTime);
    }
};

/**
 * RewardDistribution Structure
 * 
 * Complete record of reward distribution for a dispute.
 * Stores the full breakdown of how funds were distributed
 * after a dispute resolution.
 * 
 * Requirements: 3.1, 3.2, 6.4
 */
struct RewardDistribution {
    uint256 disputeId;                       // Dispute this distribution is for
    bool slashDecision{false};               // Was it a slash or keep decision?
    CAmount totalSlashedBond{0};             // Total bond that was slashed (or forfeited)
    CAmount challengerBondReturn{0};         // Bond returned to challenger
    CAmount challengerBounty{0};             // Bounty paid to challenger
    CAmount totalDaoVoterRewards{0};         // Total paid to DAO voters
    CAmount burnedAmount{0};                 // Amount burned
    std::map<uint160, CAmount> voterRewards; // Individual voter rewards
    uint32_t distributedTime{0};             // When distribution occurred
    
    RewardDistribution() = default;
    
    /**
     * Calculate total distributed amount
     * Should equal challengerBondReturn + totalSlashedBond for conservation
     */
    CAmount TotalDistributed() const {
        return challengerBondReturn + challengerBounty + totalDaoVoterRewards + burnedAmount;
    }
    
    /**
     * Verify conservation of funds
     * For slash: total out = challenger bond + slashed bond
     * For keep: total out = challenger bond (forfeited)
     */
    bool VerifyConservation(CAmount challengerBond) const {
        auto totalIn = slashDecision ? (challengerBond + totalSlashedBond) : challengerBond;
        auto totalOut = TotalDistributed();
        return totalIn == totalOut;
    }
    
    /**
     * Check if distribution is valid
     */
    bool IsValid() const {
        return !disputeId.IsNull() && distributedTime > 0;
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(disputeId);
        READWRITE(slashDecision);
        READWRITE(totalSlashedBond);
        READWRITE(challengerBondReturn);
        READWRITE(challengerBounty);
        READWRITE(totalDaoVoterRewards);
        READWRITE(burnedAmount);
        READWRITE(voterRewards);
        READWRITE(distributedTime);
    }
};

} // namespace CVM

#endif // CASCOIN_CVM_REWARD_TYPES_H
