// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUSTGRAPH_H
#define CASCOIN_CVM_TRUSTGRAPH_H

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <set>

namespace CVM {

class CVMDatabase;

/**
 * Trust Edge - Represents a trust relationship from one address to another
 * 
 * In Web-of-Trust, each user maintains their own trust graph.
 * An edge from A to B means: "A trusts B with weight X"
 */
struct TrustEdge {
    uint160 fromAddress;      // Who is trusting
    uint160 toAddress;        // Who is trusted
    int16_t trustWeight;      // Trust weight (-100 to +100)
    uint32_t timestamp;       // When trust was established
    CAmount bondAmount;       // Amount of CAS bonded/staked
    uint256 bondTxHash;       // Transaction hash of bond
    bool slashed;             // Was this bond slashed?
    std::string reason;       // Human-readable reason
    
    TrustEdge() : trustWeight(0), timestamp(0), bondAmount(0), slashed(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(fromAddress);
        READWRITE(toAddress);
        READWRITE(trustWeight);
        READWRITE(timestamp);
        READWRITE(bondAmount);
        READWRITE(bondTxHash);
        READWRITE(slashed);
        READWRITE(reason);
    }
};

/**
 * Trust Path - Represents a path through the trust graph
 * 
 * Used for calculating reputation weighted by trust relationships.
 * Example: If A trusts B (80%) and B trusts C (90%), 
 * then A's view of C's reputation is weighted by 0.8 * 0.9 = 0.72
 */
struct TrustPath {
    std::vector<uint160> addresses;  // Path of addresses
    std::vector<int16_t> weights;    // Trust weight at each hop
    double totalWeight;              // Combined weight (product of all weights)
    
    TrustPath() : totalWeight(1.0) {}
    
    void AddHop(const uint160& addr, int16_t weight) {
        addresses.push_back(addr);
        weights.push_back(weight);
        totalWeight *= (weight / 100.0); // Normalize to [0, 1]
    }
    
    size_t Length() const { return addresses.size(); }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(addresses);
        READWRITE(weights);
        READWRITE(totalWeight);
    }
};

/**
 * Reputation Vote with Bonding
 * 
 * To vote, user must stake CAS. If vote is deemed malicious by DAO,
 * the bond is slashed.
 */
struct BondedVote {
    uint160 voter;               // Who is voting
    uint160 target;              // Who is being voted on
    int16_t voteValue;          // Vote value (-100 to +100)
    CAmount bondAmount;         // Amount staked
    uint256 bondTxHash;         // Transaction with bond
    uint32_t timestamp;         // When vote was cast
    bool slashed;               // Was bond slashed?
    uint256 slashTxHash;        // Transaction that slashed (if any)
    std::string reason;         // Reason for vote
    
    BondedVote() : voteValue(0), bondAmount(0), timestamp(0), slashed(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(voter);
        READWRITE(target);
        READWRITE(voteValue);
        READWRITE(bondAmount);
        READWRITE(bondTxHash);
        READWRITE(timestamp);
        READWRITE(slashed);
        READWRITE(slashTxHash);
        READWRITE(reason);
    }
};

/**
 * DAO Dispute
 * 
 * When a vote is challenged, a DAO panel can arbitrate.
 * DAO members stake their own CAS to vote on the dispute.
 * 
 * Extended with commit-reveal voting support and reward tracking.
 * Requirements: 8.2, 8.3, 9.3
 */
struct DAODispute {
    uint256 disputeId;           // Unique dispute ID
    uint256 originalVoteTx;      // The vote being disputed
    uint160 challenger;          // Who challenged the vote
    CAmount challengeBond;       // Bond staked by challenger
    std::string challengeReason; // Why is it being challenged
    uint32_t createdTime;        // When dispute was created
    
    // DAO Votes
    std::map<uint160, bool> daoVotes;  // DAO member -> support (true=slash, false=keep)
    std::map<uint160, CAmount> daoStakes; // Amount staked by each DAO member
    
    // Resolution
    bool resolved;               // Has dispute been resolved?
    bool slashDecision;          // Final decision: slash or not?
    uint32_t resolvedTime;       // When resolved
    
    // Commit-reveal phase tracking (Requirements: 8.2, 8.3)
    uint32_t commitPhaseStart;   // Block height when commit phase started
    uint32_t revealPhaseStart;   // Block height when reveal phase started (0 if not started)
    bool useCommitReveal;        // Whether this dispute uses commit-reveal voting
    
    // Reward tracking (Requirement: 9.3)
    bool rewardsDistributed;     // Have rewards been distributed?
    uint256 rewardDistributionId; // ID of reward distribution record
    
    DAODispute() 
        : challengeBond(0)
        , createdTime(0)
        , resolved(false)
        , slashDecision(false)
        , resolvedTime(0)
        , commitPhaseStart(0)
        , revealPhaseStart(0)
        , useCommitReveal(false)
        , rewardsDistributed(false)
    {}
    
    /**
     * Check if this dispute is in commit phase
     * @param currentHeight Current block height
     * @param commitDuration Duration of commit phase in blocks
     * @return true if in commit phase
     */
    bool IsInCommitPhase(uint32_t currentHeight, uint32_t commitDuration) const {
        if (!useCommitReveal) return false;
        uint32_t commitEnd = commitPhaseStart + commitDuration;
        return currentHeight >= commitPhaseStart && currentHeight < commitEnd;
    }
    
    /**
     * Check if this dispute is in reveal phase
     * @param currentHeight Current block height
     * @param commitDuration Duration of commit phase in blocks
     * @param revealDuration Duration of reveal phase in blocks
     * @return true if in reveal phase
     */
    bool IsInRevealPhase(uint32_t currentHeight, uint32_t commitDuration, uint32_t revealDuration) const {
        if (!useCommitReveal) return false;
        uint32_t commitEnd = commitPhaseStart + commitDuration;
        uint32_t revealEnd = commitEnd + revealDuration;
        return currentHeight >= commitEnd && currentHeight < revealEnd;
    }
    
    /**
     * Check if both phases have completed
     * @param currentHeight Current block height
     * @param commitDuration Duration of commit phase in blocks
     * @param revealDuration Duration of reveal phase in blocks
     * @return true if both phases are complete
     */
    bool ArePhasesComplete(uint32_t currentHeight, uint32_t commitDuration, uint32_t revealDuration) const {
        if (!useCommitReveal) return true;  // Legacy disputes don't have phases
        uint32_t revealEnd = commitPhaseStart + commitDuration + revealDuration;
        return currentHeight >= revealEnd;
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(disputeId);
        READWRITE(originalVoteTx);
        READWRITE(challenger);
        READWRITE(challengeBond);
        READWRITE(challengeReason);
        READWRITE(createdTime);
        READWRITE(daoVotes);
        READWRITE(daoStakes);
        READWRITE(resolved);
        READWRITE(slashDecision);
        READWRITE(resolvedTime);
        // Extended fields for commit-reveal and rewards
        READWRITE(commitPhaseStart);
        READWRITE(revealPhaseStart);
        READWRITE(useCommitReveal);
        READWRITE(rewardsDistributed);
        READWRITE(rewardDistributionId);
    }
};

// Forward declarations for reward system integration
class RewardDistributor;
class CommitRevealManager;

/**
 * Web-of-Trust Reputation System
 * 
 * Unlike the simple global score system, this implements a personalized
 * Web-of-Trust where each user's view of reputation is based on their
 * own trust graph and connections.
 * 
 * Extended with reward distribution and commit-reveal voting support.
 * Requirements: 9.1, 9.3, 9.4, 9.5
 */
class TrustGraph {
public:
    explicit TrustGraph(CVMDatabase& db);
    ~TrustGraph();
    
    /**
     * Add or update a trust edge
     * 
     * @param from Address establishing trust
     * @param to Address being trusted
     * @param weight Trust weight (-100 to +100)
     * @param bondAmount Amount of CAS bonded
     * @param bondTx Transaction with bond
     * @param reason Human-readable reason
     * @return true if successful
     */
    bool AddTrustEdge(
        const uint160& from,
        const uint160& to,
        int16_t weight,
        CAmount bondAmount,
        const uint256& bondTx,
        const std::string& reason
    );
    
    /**
     * Get trust edge between two addresses
     * 
     * @param from Truster address
     * @param to Trusted address
     * @param edge Output trust edge
     * @return true if edge exists
     */
    bool GetTrustEdge(const uint160& from, const uint160& to, TrustEdge& edge) const;
    
    /**
     * Get all outgoing trust edges from an address
     * 
     * @param from Address
     * @return Vector of trust edges
     */
    std::vector<TrustEdge> GetOutgoingTrust(const uint160& from) const;
    
    /**
     * Get all incoming trust edges to an address
     * 
     * @param to Address
     * @return Vector of trust edges
     */
    std::vector<TrustEdge> GetIncomingTrust(const uint160& to) const;
    
    /**
     * Calculate reputation from a specific viewer's perspective
     * 
     * This is the core of Web-of-Trust: reputation is NOT global,
     * but personalized based on the viewer's trust graph.
     * 
     * Algorithm:
     * 1. Find all paths from viewer to target (max depth N)
     * 2. Weight each path by product of trust weights
     * 3. Aggregate all weighted paths
     * 
     * @param viewer Who is viewing the reputation
     * @param target Whose reputation is being calculated
     * @param maxDepth Maximum depth to traverse (default: 3)
     * @return Weighted reputation score
     */
    double GetWeightedReputation(
        const uint160& viewer,
        const uint160& target,
        int maxDepth = 3
    ) const;
    
    /**
     * Find trust paths between two addresses
     * 
     * @param from Source address
     * @param to Target address
     * @param maxDepth Maximum path length
     * @return Vector of trust paths
     */
    std::vector<TrustPath> FindTrustPaths(
        const uint160& from,
        const uint160& to,
        int maxDepth = 3
    ) const;
    
    /**
     * Record a bonded vote
     * 
     * @param vote Bonded vote to record
     * @return true if successful
     */
    bool RecordBondedVote(const BondedVote& vote);
    
    /**
     * Get all votes for a target address
     * 
     * @param target Address
     * @return Vector of bonded votes
     */
    std::vector<BondedVote> GetVotesForAddress(const uint160& target) const;
    
    /**
     * Slash a bonded vote (DAO decision)
     * 
     * @param voteTxHash Transaction hash of original vote
     * @param slashTxHash Transaction hash of slash
     * @return true if successful
     */
    bool SlashVote(const uint256& voteTxHash, const uint256& slashTxHash);
    
    /**
     * Create a DAO dispute
     * 
     * @param dispute Dispute details
     * @return true if successful
     */
    bool CreateDispute(const DAODispute& dispute);
    
    /**
     * Get dispute by ID
     * 
     * @param disputeId Dispute ID
     * @param dispute Output dispute
     * @return true if found
     */
    bool GetDispute(const uint256& disputeId, DAODispute& dispute) const;
    
    /**
     * DAO member votes on dispute
     * 
     * @param disputeId Dispute ID
     * @param daoMember DAO member address
     * @param support true=slash, false=keep
     * @param stake Amount staked by DAO member
     * @return true if successful
     */
    bool VoteOnDispute(
        const uint256& disputeId,
        const uint160& daoMember,
        bool support,
        CAmount stake
    );
    
    /**
     * Resolve a dispute
     * 
     * Extended to integrate with RewardDistributor for automatic reward distribution.
     * Calls DistributeSlashRewards() or DistributeFailedChallengeRewards() based on outcome.
     * 
     * @param disputeId Dispute ID
     * @return true if successful
     * 
     * Requirements: 9.1, 9.3, 9.4, 9.5
     */
    bool ResolveDispute(const uint256& disputeId);
    
    /**
     * Get the original vote being disputed
     * 
     * @param disputeId Dispute ID
     * @param vote Output bonded vote
     * @return true if found
     * 
     * Requirement: 9.1
     */
    bool GetDisputedVote(const uint256& disputeId, BondedVote& vote) const;
    
    /**
     * Get statistics about the trust graph
     * 
     * @return Map with statistics
     */
    std::map<std::string, uint64_t> GetGraphStats() const;
    
    /**
     * Check if address is a DAO member
     * 
     * DAO membership requirements:
     * 1. Minimum reputation score (70+)
     * 2. Minimum stake (100 CAS bonded)
     * 3. Active participation (voted in last 10,000 blocks)
     * 
     * @param address Address to check
     * @return true if address is a DAO member
     */
    bool IsDAOMember(const uint160& address) const;
    
    /**
     * Get the RewardDistributor instance
     * 
     * @return Pointer to RewardDistributor (may be null if not initialized)
     */
    RewardDistributor* GetRewardDistributor() const { return rewardDistributor.get(); }
    
    /**
     * Get the CommitRevealManager instance
     * 
     * @return Pointer to CommitRevealManager (may be null if not initialized)
     */
    CommitRevealManager* GetCommitRevealManager() const { return commitRevealManager.get(); }

private:
    CVMDatabase& database;
    
    // Reward system integration (Requirements: 9.1, 9.3)
    std::unique_ptr<RewardDistributor> rewardDistributor;
    std::unique_ptr<CommitRevealManager> commitRevealManager;
    
    /**
     * Recursive helper for finding trust paths
     */
    void FindPathsRecursive(
        const uint160& current,
        const uint160& target,
        int remainingDepth,
        TrustPath& currentPath,
        std::set<uint160>& visited,
        std::vector<TrustPath>& results
    ) const;
    
    /**
     * Calculate bond requirement based on vote value
     * 
     * Larger absolute vote values require larger bonds to prevent spam.
     */
    CAmount CalculateRequiredBond(int16_t voteValue) const;
    
    /**
     * Update dispute in database
     * 
     * @param dispute Dispute to update
     * @return true if successful
     */
    bool UpdateDispute(const DAODispute& dispute);
};

/**
 * Configuration for Web-of-Trust system
 */
struct WoTConfig {
    CAmount minBondAmount;          // Minimum bond for any vote (e.g., 1 CAS)
    CAmount bondPerVotePoint;       // Additional bond per vote point (e.g., 0.01 CAS per point)
    int maxTrustPathDepth;          // Maximum depth for trust path search
    int minDAOVotesForResolution;   // Minimum DAO votes needed to resolve dispute
    double daoQuorumPercentage;     // Percentage of DAO stake needed for quorum
    uint32_t disputeTimeoutBlocks;  // Blocks before dispute auto-resolves
    
    // Reward distribution percentages for successful challenge (must sum to 100)
    uint8_t challengerRewardPercent;        // Percentage of slashed bond to challenger (default: 50)
    uint8_t daoVoterRewardPercent;          // Percentage of slashed bond to DAO voters (default: 30)
    uint8_t burnPercent;                    // Percentage of slashed bond to burn (default: 20)
    
    // Failed challenge distribution percentages (must sum to 100)
    uint8_t wronglyAccusedRewardPercent;    // Percentage of forfeited bond to wrongly accused (default: 70)
    uint8_t failedChallengeBurnPercent;     // Percentage of forfeited bond to burn (default: 30)
    
    // Commit-reveal voting timing (in blocks)
    uint32_t commitPhaseDuration;           // Duration of commit phase (default: 720 blocks ~12 hours)
    uint32_t revealPhaseDuration;           // Duration of reveal phase (default: 720 blocks ~12 hours)
    
    // Feature flags
    bool enableCommitReveal;                // Enable commit-reveal voting for new disputes (default: true)
    
    WoTConfig() :
        minBondAmount(COIN),              // 1 CAS minimum (COIN = 10000000 in Cascoin!)
        bondPerVotePoint(COIN / 100),     // 0.01 CAS per point (= 100000 in Cascoin)
        maxTrustPathDepth(3),
        minDAOVotesForResolution(5),
        daoQuorumPercentage(0.51),
        disputeTimeoutBlocks(1440),       // ~1 day
        challengerRewardPercent(50),
        daoVoterRewardPercent(30),
        burnPercent(20),
        wronglyAccusedRewardPercent(70),
        failedChallengeBurnPercent(30),
        commitPhaseDuration(720),         // ~12 hours at 1 block/minute
        revealPhaseDuration(720),         // ~12 hours at 1 block/minute
        enableCommitReveal(true)
    {}
    
    /**
     * Validate that reward percentages sum to 100
     * 
     * @return true if both percentage sets are valid:
     *         - challengerRewardPercent + daoVoterRewardPercent + burnPercent == 100
     *         - wronglyAccusedRewardPercent + failedChallengeBurnPercent == 100
     */
    bool ValidateRewardPercentages() const {
        return (challengerRewardPercent + daoVoterRewardPercent + burnPercent) == 100 &&
               (wronglyAccusedRewardPercent + failedChallengeBurnPercent) == 100;
    }
};

// Global Web-of-Trust configuration
extern WoTConfig g_wotConfig;

} // namespace CVM

#endif // CASCOIN_CVM_TRUSTGRAPH_H

