// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_COMMIT_REVEAL_H
#define CASCOIN_CVM_COMMIT_REVEAL_H

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <vector>

namespace CVM {

// Forward declarations
class CVMDatabase;
struct WoTConfig;
struct DAODispute;

/**
 * VoteCommitment Structure
 * 
 * Represents a committed vote in the commit-reveal voting scheme.
 * During the commit phase, voters submit a hash of their vote and a secret nonce.
 * During the reveal phase, they reveal the actual vote and nonce to verify.
 * 
 * This prevents bandwagoning (voters following the majority without independent evaluation)
 * by hiding votes until all commitments are in.
 * 
 * Requirements: 8.1, 8.7
 */
struct VoteCommitment {
    uint256 disputeId;          // Dispute being voted on
    uint160 voter;              // DAO member who committed
    uint256 commitmentHash;     // Hash of (vote || nonce) - SHA256
    CAmount stake{0};           // Amount staked on this vote
    uint32_t commitTime{0};     // Block timestamp when commitment was made
    bool revealed{false};       // Has vote been revealed?
    bool vote{false};           // Actual vote (only valid if revealed): true=slash, false=keep
    uint256 nonce;              // Nonce used (only valid if revealed)
    uint32_t revealTime{0};     // Block timestamp when revealed (0 if not revealed)
    bool forfeited{false};      // Was stake forfeited for non-reveal?
    
    VoteCommitment() = default;
    
    VoteCommitment(const uint256& disputeId_, const uint160& voter_,
                   const uint256& commitmentHash_, CAmount stake_,
                   uint32_t commitTime_)
        : disputeId(disputeId_)
        , voter(voter_)
        , commitmentHash(commitmentHash_)
        , stake(stake_)
        , commitTime(commitTime_)
    {}
    
    /**
     * Check if this commitment is valid (has required fields)
     */
    bool IsValid() const {
        return !disputeId.IsNull() && !voter.IsNull() && 
               !commitmentHash.IsNull() && stake > 0;
    }
    
    /**
     * Check if this commitment can still be revealed
     * (not already revealed and not forfeited)
     */
    bool CanReveal() const {
        return !revealed && !forfeited;
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(disputeId);
        READWRITE(voter);
        READWRITE(commitmentHash);
        READWRITE(stake);
        READWRITE(commitTime);
        READWRITE(revealed);
        READWRITE(vote);
        READWRITE(nonce);
        READWRITE(revealTime);
        READWRITE(forfeited);
    }
};

/**
 * CommitRevealManager Class
 * 
 * Manages the commit-reveal voting scheme for DAO disputes.
 * 
 * The commit-reveal scheme works as follows:
 * 1. Commit Phase: Voters submit hash(vote || nonce) without revealing their vote
 * 2. Reveal Phase: Voters reveal their vote and nonce, system verifies hash matches
 * 3. Resolution: Only revealed votes are counted in the final tally
 * 
 * This prevents:
 * - Bandwagoning: Voters can't see others' votes during commit phase
 * - Vote manipulation: Votes can't be changed after commitment
 * - Free-riding: Non-revealers forfeit their stake
 * 
 * Requirements: 8.1, 8.2, 8.3, 8.4, 8.5, 8.6, 8.7
 */
class CommitRevealManager {
public:
    explicit CommitRevealManager(CVMDatabase& db, const WoTConfig& config);
    virtual ~CommitRevealManager() = default;
    
    /**
     * Submit a vote commitment during the commit phase
     * 
     * @param disputeId Dispute being voted on
     * @param voter DAO member address
     * @param commitmentHash Hash of (vote || nonce) - use CalculateCommitmentHash()
     * @param stake Amount staked on this vote
     * @return true if commitment accepted
     * 
     * Requirements: 8.1, 8.7
     */
    bool SubmitCommitment(const uint256& disputeId,
                          const uint160& voter,
                          const uint256& commitmentHash,
                          CAmount stake);
    
    /**
     * Reveal a vote during the reveal phase
     * 
     * @param disputeId Dispute being voted on
     * @param voter DAO member address
     * @param vote The actual vote (true=slash, false=keep)
     * @param nonce Secret nonce used in commitment
     * @return true if reveal successful and hash matches
     * 
     * Requirements: 8.4
     */
    bool RevealVote(const uint256& disputeId,
                    const uint160& voter,
                    bool vote,
                    const uint256& nonce);
    
    /**
     * Check if dispute is currently in commit phase
     * 
     * @param disputeId Dispute to check
     * @return true if in commit phase
     * 
     * Requirements: 8.2
     */
    bool IsCommitPhase(const uint256& disputeId) const;
    
    /**
     * Check if dispute is currently in reveal phase
     * 
     * @param disputeId Dispute to check
     * @return true if in reveal phase
     * 
     * Requirements: 8.3
     */
    bool IsRevealPhase(const uint256& disputeId) const;
    
    /**
     * Get all commitments for a dispute
     * 
     * @param disputeId Dispute to query
     * @return Vector of all vote commitments
     */
    std::vector<VoteCommitment> GetCommitments(const uint256& disputeId) const;
    
    /**
     * Get a specific commitment
     * 
     * @param disputeId Dispute ID
     * @param voter Voter address
     * @param commitment Output commitment
     * @return true if found
     */
    bool GetCommitment(const uint256& disputeId, const uint160& voter,
                       VoteCommitment& commitment) const;
    
    /**
     * Forfeit stakes of voters who didn't reveal within the reveal phase
     * 
     * Called when dispute resolution begins. Marks all unrevealed commitments
     * as forfeited and returns the total forfeited amount.
     * 
     * @param disputeId Dispute to process
     * @return Total amount forfeited
     * 
     * Requirements: 8.5, 8.6
     */
    CAmount ForfeitUnrevealedStakes(const uint256& disputeId);
    
    /**
     * Calculate commitment hash from vote and nonce
     * 
     * Uses SHA256(vote_byte || nonce_bytes) where:
     * - vote_byte is 0x01 for slash (true) or 0x00 for keep (false)
     * - nonce_bytes is the 32-byte nonce
     * 
     * @param vote The vote (true=slash, false=keep)
     * @param nonce Secret nonce (should be random 256-bit value)
     * @return Commitment hash
     * 
     * Requirements: 8.1, 8.4, 8.7
     */
    static uint256 CalculateCommitmentHash(bool vote, const uint256& nonce);
    
    /**
     * Verify that a revealed vote matches its commitment
     * 
     * @param commitmentHash The original commitment hash
     * @param vote The revealed vote
     * @param nonce The revealed nonce
     * @return true if hash matches
     */
    static bool VerifyCommitment(const uint256& commitmentHash, 
                                  bool vote, const uint256& nonce);
    
    /**
     * Get current block height (for phase calculations)
     * This is a helper that can be overridden for testing
     */
    virtual uint32_t GetCurrentBlockHeight() const;

private:
    CVMDatabase& database;
    const WoTConfig& config;
    
    /**
     * Store a commitment in the database
     */
    bool StoreCommitment(const VoteCommitment& commitment);
    
    /**
     * Update an existing commitment in the database
     */
    bool UpdateCommitment(const VoteCommitment& commitment);
    
    /**
     * Get dispute info for phase calculations
     */
    bool GetDisputeInfo(const uint256& disputeId, uint32_t& commitPhaseStart,
                        uint32_t& revealPhaseStart, bool& useCommitReveal) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_COMMIT_REVEAL_H
