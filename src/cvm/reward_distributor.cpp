// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reward_distributor.h>
#include <cvm/cvmdb.h>
#include <streams.h>
#include <util.h>

namespace CVM {

// Database key prefixes for reward storage
static const std::string DB_REWARD_PREFIX = "reward_";
static const std::string DB_REWARDS_BY_RECIPIENT_PREFIX = "rewards_recipient_";
static const std::string DB_DISTRIBUTION_PREFIX = "distribution_";
static const std::string DB_REWARD_EVENT_PREFIX = "event_reward_";

RewardDistributor::RewardDistributor(CVMDatabase& db, const WoTConfig& cfg)
    : database(db)
    , config(cfg)
{
}

uint32_t RewardDistributor::GetCurrentTimestamp() const
{
    return static_cast<uint32_t>(GetTime());
}

CAmount RewardDistributor::GetTotalStakeOnSide(const DAODispute& dispute, bool side) const
{
    CAmount total = 0;
    for (const auto& [voter, vote] : dispute.daoVotes) {
        if (vote == side) {
            auto stakeIt = dispute.daoStakes.find(voter);
            if (stakeIt != dispute.daoStakes.end()) {
                total += stakeIt->second;
            }
        }
    }
    return total;
}

std::pair<std::map<uint160, CAmount>, CAmount> RewardDistributor::CalculateVoterRewards(
    const DAODispute& dispute,
    CAmount totalVoterPool,
    bool winningSide
) const
{
    std::map<uint160, CAmount> rewards;
    CAmount remainder = 0;
    
    if (totalVoterPool <= 0) {
        return {rewards, remainder};
    }
    
    // Get total stake on winning side
    CAmount totalWinningStake = GetTotalStakeOnSide(dispute, winningSide);
    
    if (totalWinningStake <= 0) {
        // No voters on winning side - return full pool as remainder
        return {rewards, totalVoterPool};
    }
    
    // Calculate proportional rewards using integer arithmetic
    CAmount distributed = 0;
    for (const auto& [voter, vote] : dispute.daoVotes) {
        if (vote == winningSide) {
            auto stakeIt = dispute.daoStakes.find(voter);
            if (stakeIt != dispute.daoStakes.end() && stakeIt->second > 0) {
                // reward = (voterStake * totalPool) / totalWinningStake
                // Use 128-bit arithmetic to avoid overflow
                __int128 numerator = static_cast<__int128>(stakeIt->second) * totalVoterPool;
                CAmount voterReward = static_cast<CAmount>(numerator / totalWinningStake);
                
                if (voterReward > 0) {
                    rewards[voter] = voterReward;
                    distributed += voterReward;
                }
            }
        }
    }
    
    // Calculate remainder from rounding
    remainder = totalVoterPool - distributed;
    
    return {rewards, remainder};
}


bool RewardDistributor::DistributeSlashRewards(const DAODispute& dispute, CAmount slashedBond)
{
    // Validate inputs
    if (dispute.disputeId.IsNull() || dispute.challenger.IsNull()) {
        return false;
    }
    
    if (!dispute.resolved || !dispute.slashDecision) {
        return false;  // Must be resolved with slash decision
    }
    
    uint32_t timestamp = GetCurrentTimestamp();
    
    // Calculate distribution amounts
    // Challenger bond return: 100% of original challenge bond
    CAmount challengerBondReturn = dispute.challengeBond;
    
    // Calculate percentages of slashed bond
    // Use integer arithmetic: amount = (slashedBond * percent) / 100
    CAmount challengerBounty = (slashedBond * config.challengerRewardPercent) / 100;
    CAmount voterPoolBase = (slashedBond * config.daoVoterRewardPercent) / 100;
    CAmount burnBase = (slashedBond * config.burnPercent) / 100;
    
    // Handle rounding remainder - add to burn
    CAmount calculatedTotal = challengerBounty + voterPoolBase + burnBase;
    CAmount roundingRemainder = slashedBond - calculatedTotal;
    CAmount burnAmount = burnBase + roundingRemainder;
    
    // Calculate voter rewards
    auto [voterRewards, voterRemainder] = CalculateVoterRewards(
        dispute, voterPoolBase, true /* winningSide = slash */);
    
    CAmount totalVoterRewards = 0;
    for (const auto& [voter, amount] : voterRewards) {
        totalVoterRewards += amount;
    }
    
    // Handle edge cases for voter rewards
    if (voterRewards.empty()) {
        // No voters on winning side - give voter portion to challenger
        challengerBounty += voterPoolBase;
        // voterRemainder is already voterPoolBase in this case, don't add to burn
    } else {
        // Add voter rounding remainder to burn
        burnAmount += voterRemainder;
    }
    
    // Create pending rewards
    // 1. Challenger bond return
    if (challengerBondReturn > 0) {
        uint256 bondReturnId = PendingReward::GenerateRewardId(
            dispute.disputeId, dispute.challenger, RewardType::CHALLENGER_BOND_RETURN);
        
        PendingReward bondReturn(
            bondReturnId,
            dispute.disputeId,
            dispute.challenger,
            challengerBondReturn,
            RewardType::CHALLENGER_BOND_RETURN,
            timestamp
        );
        
        if (!StorePendingReward(bondReturn)) {
            return false;
        }
        
        EmitRewardEvent("RewardDistributed", dispute.disputeId, 
                        dispute.challenger, challengerBondReturn);
    }
    
    // 2. Challenger bounty
    if (challengerBounty > 0) {
        uint256 bountyId = PendingReward::GenerateRewardId(
            dispute.disputeId, dispute.challenger, RewardType::CHALLENGER_BOUNTY);
        
        PendingReward bounty(
            bountyId,
            dispute.disputeId,
            dispute.challenger,
            challengerBounty,
            RewardType::CHALLENGER_BOUNTY,
            timestamp
        );
        
        if (!StorePendingReward(bounty)) {
            return false;
        }
        
        EmitRewardEvent("RewardDistributed", dispute.disputeId,
                        dispute.challenger, challengerBounty);
    }
    
    // 3. DAO voter rewards
    for (const auto& [voter, amount] : voterRewards) {
        uint256 voterRewardId = PendingReward::GenerateRewardId(
            dispute.disputeId, voter, RewardType::DAO_VOTER_REWARD);
        
        PendingReward voterReward(
            voterRewardId,
            dispute.disputeId,
            voter,
            amount,
            RewardType::DAO_VOTER_REWARD,
            timestamp
        );
        
        if (!StorePendingReward(voterReward)) {
            return false;
        }
        
        EmitRewardEvent("RewardDistributed", dispute.disputeId, voter, amount);
    }
    
    // 4. Emit burn event
    if (burnAmount > 0) {
        uint160 nullAddr;  // Null address for burn
        EmitRewardEvent("BondBurned", dispute.disputeId, nullAddr, burnAmount);
    }
    
    // Store reward distribution record
    RewardDistribution distribution;
    distribution.disputeId = dispute.disputeId;
    distribution.slashDecision = true;
    distribution.totalSlashedBond = slashedBond;
    distribution.challengerBondReturn = challengerBondReturn;
    distribution.challengerBounty = challengerBounty;
    distribution.totalDaoVoterRewards = totalVoterRewards;
    distribution.burnedAmount = burnAmount;
    distribution.voterRewards = voterRewards;
    distribution.distributedTime = timestamp;
    
    return StoreRewardDistribution(distribution);
}


bool RewardDistributor::DistributeFailedChallengeRewards(const DAODispute& dispute,
                                                         const uint160& originalVoter)
{
    // Validate inputs
    if (dispute.disputeId.IsNull()) {
        return false;
    }
    
    if (!dispute.resolved || dispute.slashDecision) {
        return false;  // Must be resolved with keep decision (no slash)
    }
    
    uint32_t timestamp = GetCurrentTimestamp();
    
    // Challenger bond is forfeited (no return)
    CAmount forfeitedBond = dispute.challengeBond;
    
    // Calculate distribution
    CAmount wronglyAccusedCompensation = 0;
    CAmount burnAmount = 0;
    
    // Check if original voter address is valid
    if (originalVoter.IsNull()) {
        // Invalid voter address - burn entire bond
        burnAmount = forfeitedBond;
    } else {
        // Calculate percentages
        wronglyAccusedCompensation = (forfeitedBond * config.wronglyAccusedRewardPercent) / 100;
        CAmount burnBase = (forfeitedBond * config.failedChallengeBurnPercent) / 100;
        
        // Handle rounding remainder - add to burn
        CAmount calculatedTotal = wronglyAccusedCompensation + burnBase;
        CAmount roundingRemainder = forfeitedBond - calculatedTotal;
        burnAmount = burnBase + roundingRemainder;
    }
    
    // Create pending reward for wrongly accused voter
    if (wronglyAccusedCompensation > 0 && !originalVoter.IsNull()) {
        uint256 compensationId = PendingReward::GenerateRewardId(
            dispute.disputeId, originalVoter, RewardType::WRONGLY_ACCUSED_COMPENSATION);
        
        PendingReward compensation(
            compensationId,
            dispute.disputeId,
            originalVoter,
            wronglyAccusedCompensation,
            RewardType::WRONGLY_ACCUSED_COMPENSATION,
            timestamp
        );
        
        if (!StorePendingReward(compensation)) {
            return false;
        }
        
        EmitRewardEvent("RewardDistributed", dispute.disputeId,
                        originalVoter, wronglyAccusedCompensation);
    }
    
    // Emit burn event
    if (burnAmount > 0) {
        uint160 nullAddr;  // Null address for burn
        EmitRewardEvent("BondBurned", dispute.disputeId, nullAddr, burnAmount);
    }
    
    // Store reward distribution record
    RewardDistribution distribution;
    distribution.disputeId = dispute.disputeId;
    distribution.slashDecision = false;
    distribution.totalSlashedBond = forfeitedBond;  // Forfeited challenger bond
    distribution.challengerBondReturn = 0;  // No return for failed challenge
    distribution.challengerBounty = 0;
    distribution.totalDaoVoterRewards = wronglyAccusedCompensation;
    distribution.burnedAmount = burnAmount;
    
    if (!originalVoter.IsNull() && wronglyAccusedCompensation > 0) {
        distribution.voterRewards[originalVoter] = wronglyAccusedCompensation;
    }
    
    distribution.distributedTime = timestamp;
    
    return StoreRewardDistribution(distribution);
}


std::vector<PendingReward> RewardDistributor::GetPendingRewards(const uint160& recipient) const
{
    std::vector<PendingReward> result;
    
    if (recipient.IsNull()) {
        return result;
    }
    
    // Get list of reward IDs for this recipient
    std::string indexKey = DB_REWARDS_BY_RECIPIENT_PREFIX + recipient.GetHex();
    std::vector<uint8_t> indexData;
    
    if (!database.ReadGeneric(indexKey, indexData)) {
        return result;  // No rewards for this recipient
    }
    
    // Deserialize list of reward IDs
    CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
    std::vector<uint256> rewardIds;
    ss >> rewardIds;
    
    // Load each reward and filter for unclaimed
    for (const auto& rewardId : rewardIds) {
        PendingReward reward;
        if (GetReward(rewardId, reward)) {
            // Only return unclaimed rewards
            if (!reward.claimed) {
                result.push_back(reward);
            }
        }
    }
    
    return result;
}

std::vector<PendingReward> RewardDistributor::GetClaimedRewards(const uint160& recipient) const
{
    std::vector<PendingReward> result;
    
    if (recipient.IsNull()) {
        return result;
    }
    
    // Get list of reward IDs for this recipient
    std::string indexKey = DB_REWARDS_BY_RECIPIENT_PREFIX + recipient.GetHex();
    std::vector<uint8_t> indexData;
    
    if (!database.ReadGeneric(indexKey, indexData)) {
        return result;  // No rewards for this recipient
    }
    
    // Deserialize list of reward IDs
    CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
    std::vector<uint256> rewardIds;
    ss >> rewardIds;
    
    // Load each reward and filter for claimed
    for (const auto& rewardId : rewardIds) {
        PendingReward reward;
        if (GetReward(rewardId, reward)) {
            // Only return claimed rewards
            if (reward.claimed) {
                result.push_back(reward);
            }
        }
    }
    
    return result;
}

std::vector<PendingReward> RewardDistributor::GetAllRewards(const uint160& recipient) const
{
    std::vector<PendingReward> result;
    
    if (recipient.IsNull()) {
        return result;
    }
    
    // Get list of reward IDs for this recipient
    std::string indexKey = DB_REWARDS_BY_RECIPIENT_PREFIX + recipient.GetHex();
    std::vector<uint8_t> indexData;
    
    if (!database.ReadGeneric(indexKey, indexData)) {
        return result;  // No rewards for this recipient
    }
    
    // Deserialize list of reward IDs
    CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
    std::vector<uint256> rewardIds;
    ss >> rewardIds;
    
    // Load all rewards
    for (const auto& rewardId : rewardIds) {
        PendingReward reward;
        if (GetReward(rewardId, reward)) {
            result.push_back(reward);
        }
    }
    
    return result;
}

CAmount RewardDistributor::ClaimReward(const uint256& rewardId, const uint160& recipient)
{
    // Validate inputs
    if (rewardId.IsNull() || recipient.IsNull()) {
        return 0;
    }
    
    // Get the reward
    PendingReward reward;
    if (!GetReward(rewardId, reward)) {
        return 0;  // Reward not found
    }
    
    // Verify recipient matches
    if (reward.recipient != recipient) {
        return 0;  // Not authorized
    }
    
    // Check if already claimed (prevent double-claiming)
    if (reward.claimed) {
        return 0;  // Already claimed
    }
    
    // Mark as claimed
    reward.claimed = true;
    reward.claimedTime = GetCurrentTimestamp();
    // Note: claimTxHash would be set by the caller after creating the transaction
    
    // Update in database
    if (!UpdateReward(reward)) {
        return 0;
    }
    
    // Emit claim event
    EmitRewardEvent("RewardClaimed", reward.disputeId, recipient, reward.amount);
    
    return reward.amount;
}

bool RewardDistributor::RewardExists(const uint256& rewardId) const
{
    std::string key = DB_REWARD_PREFIX + rewardId.GetHex();
    return database.ExistsGeneric(key);
}

bool RewardDistributor::GetReward(const uint256& rewardId, PendingReward& reward) const
{
    std::string key = DB_REWARD_PREFIX + rewardId.GetHex();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        return false;
    }
    
    CDataStream ss(data, SER_DISK, CLIENT_VERSION);
    ss >> reward;
    
    return true;
}

RewardDistribution RewardDistributor::GetRewardDistribution(const uint256& disputeId) const
{
    RewardDistribution distribution;
    
    if (disputeId.IsNull()) {
        return distribution;
    }
    
    std::string key = DB_DISTRIBUTION_PREFIX + disputeId.GetHex();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        return distribution;  // Return empty distribution
    }
    
    CDataStream ss(data, SER_DISK, CLIENT_VERSION);
    ss >> distribution;
    
    return distribution;
}

bool RewardDistributor::StorePendingReward(const PendingReward& reward)
{
    if (!reward.IsValid()) {
        return false;
    }
    
    // Store the reward
    std::string key = DB_REWARD_PREFIX + reward.rewardId.GetHex();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << reward;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    if (!database.WriteGeneric(key, data)) {
        return false;
    }
    
    // Add to recipient index
    return AddToRecipientIndex(reward.recipient, reward.rewardId);
}

bool RewardDistributor::UpdateReward(const PendingReward& reward)
{
    std::string key = DB_REWARD_PREFIX + reward.rewardId.GetHex();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << reward;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    return database.WriteGeneric(key, data);
}

bool RewardDistributor::StoreRewardDistribution(const RewardDistribution& distribution)
{
    if (!distribution.IsValid()) {
        return false;
    }
    
    std::string key = DB_DISTRIBUTION_PREFIX + distribution.disputeId.GetHex();
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << distribution;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    return database.WriteGeneric(key, data);
}

bool RewardDistributor::AddToRecipientIndex(const uint160& recipient, const uint256& rewardId)
{
    std::string indexKey = DB_REWARDS_BY_RECIPIENT_PREFIX + recipient.GetHex();
    std::vector<uint8_t> indexData;
    std::vector<uint256> rewardIds;
    
    // Load existing index if present
    if (database.ReadGeneric(indexKey, indexData)) {
        CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
        ss >> rewardIds;
    }
    
    // Check if already in list
    for (const auto& id : rewardIds) {
        if (id == rewardId) {
            return true;  // Already indexed
        }
    }
    
    // Add to list
    rewardIds.push_back(rewardId);
    
    // Write back
    CDataStream newSs(SER_DISK, CLIENT_VERSION);
    newSs << rewardIds;
    std::vector<uint8_t> newData(newSs.begin(), newSs.end());
    
    return database.WriteGeneric(indexKey, newData);
}

void RewardDistributor::EmitRewardEvent(const std::string& eventType,
                                         const uint256& disputeId,
                                         const uint160& recipient,
                                         CAmount amount)
{
    // Create event key with timestamp for ordering
    uint32_t timestamp = GetCurrentTimestamp();
    std::string eventKey = DB_REWARD_EVENT_PREFIX + 
                           std::to_string(timestamp) + "_" +
                           disputeId.GetHex().substr(0, 16);
    
    // Create event data
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << eventType;
    ss << disputeId;
    ss << recipient;
    ss << amount;
    ss << timestamp;
    
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    // Store event (best effort - don't fail if event storage fails)
    database.WriteGeneric(eventKey, data);
    
    // Log the event
    LogPrint(BCLog::CVM, "RewardEvent: type=%s dispute=%s recipient=%s amount=%lld\n",
             eventType, disputeId.GetHex(), recipient.GetHex(), amount);
}

} // namespace CVM
