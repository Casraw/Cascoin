// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_REPUTATION_H
#define CASCOIN_CVM_REPUTATION_H

#include <uint256.h>
#include <serialize.h>
#include <primitives/transaction.h>
#include <cvm/trustnodeid.h>
#include <vector>
#include <string>

namespace CVM {

/**
 * Anti-Scam Reputation System (ASRS)
 * 
 * Stores per-address reputation scores on-chain.
 * Scores are modified through:
 * - DAO voting transactions
 * - On-chain behavior patterns
 * 
 * Never blocks transactions, only provides scoring and warnings.
 *
 * User identities (the scored address and the vote target) are wide
 * TrustNodeId values: they represent every supported destination type
 * (P2PKH/P2SH/P2WPKH/P2WSH/quantum) without truncation and never collide when
 * they differ by type. Transaction-pattern data remains keyed by uint256
 * transaction hashes.
 */

/**
 * Reputation score for an address
 */
class ReputationScore {
public:
    TrustNodeId address;       // Address being scored (wide user identity)
    int64_t score;             // Reputation score (-10000 to +10000)
    uint64_t voteCount;        // Number of votes received
    int64_t lastUpdated;       // Timestamp of last update
    std::string category;      // Category: "exchange", "mixer", "scam", "normal"
    
    // Behavior metrics
    uint64_t totalTransactions;
    uint64_t totalVolume;
    uint64_t suspiciousPatterns; // Count of suspicious behaviors
    
    ReputationScore() : score(0), voteCount(0), lastUpdated(0), 
                       totalTransactions(0), totalVolume(0), suspiciousPatterns(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(score);
        READWRITE(voteCount);
        READWRITE(lastUpdated);
        READWRITE(category);
        READWRITE(totalTransactions);
        READWRITE(totalVolume);
        READWRITE(suspiciousPatterns);
    }
    
    /**
     * Get human-readable reputation level
     */
    std::string GetReputationLevel() const;
    
    /**
     * Check if address should trigger warnings
     */
    bool ShouldWarn() const { return score < -5000; }
};

/**
 * Reputation vote transaction data
 */
class ReputationVoteTx {
public:
    TrustNodeId targetAddress; // Address being voted on (wide user identity)
    int64_t voteValue;         // Vote value (-100 to +100)
    std::string reason;        // Reason for vote
    std::vector<uint8_t> proof; // Optional proof/evidence
    
    ReputationVoteTx() : voteValue(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(targetAddress);
        READWRITE(voteValue);
        READWRITE(reason);
        READWRITE(proof);
    }
    
    /**
     * Serialize to vector for embedding in transaction
     */
    std::vector<uint8_t> Serialize() const;
    
    /**
     * Deserialize from transaction data
     */
    bool Deserialize(const std::vector<uint8_t>& data);
    
    /**
     * Validate vote transaction
     */
    bool IsValid(std::string& error) const;
};

/**
 * Parse reputation vote from transaction
 */
bool ParseReputationVoteTx(const CTransaction& tx, ReputationVoteTx& voteTx);

/**
 * Check if transaction is a reputation vote
 */
bool IsReputationVoteTransaction(const CTransaction& tx);

// Forward declaration
class CVMDatabase;

/**
 * Reputation system manager
 */
class ReputationSystem {
public:
    ReputationSystem(CVMDatabase& db);
    
    /**
     * Get reputation score for a wide user identity.
     */
    bool GetReputation(const TrustNodeId& address, ReputationScore& score);
    
    /**
     * Update reputation score for a wide user identity.
     */
    bool UpdateReputation(const TrustNodeId& address, const ReputationScore& score);
    
    /**
     * Apply a reputation vote from a resolved voter identity.
     */
    bool ApplyVote(const TrustNodeId& voterAddress, const ReputationVoteTx& vote, int64_t timestamp);
    
    /**
     * Update reputation based on transaction behavior.
     */
    void UpdateBehaviorScore(const TrustNodeId& address, const CTransaction& tx, int blockHeight);
    
    /**
     * Calculate voting power for an identity.
     * Based on: coin age, stake, own reputation
     */
    int64_t GetVotingPower(const TrustNodeId& address);

    //! Thin uint160 wrappers (legacy P2PKH callers). Each wraps the bare
    //! uint160 as TrustNodeId{P2PKH, zero-extended} and forwards to the
    //! TrustNodeId overload above. Wave 8 removes the remaining uint160
    //! bridging at the RPC/block-processing sites.
    bool GetReputation(const uint160& address, ReputationScore& score);
    bool UpdateReputation(const uint160& address, const ReputationScore& score);
    bool ApplyVote(const uint160& voterAddress, const ReputationVoteTx& vote, int64_t timestamp);
    void UpdateBehaviorScore(const uint160& address, const CTransaction& tx, int blockHeight);
    int64_t GetVotingPower(const uint160& address);
    
    /**
     * Get list of identities with poor reputation.
     *
     * Enumerates the maintained reputation index (clause 2.49) and returns the
     * wide identities at or below the threshold. Malformed/noncanonical index
     * key segments are skipped, not reinterpreted.
     */
    std::vector<TrustNodeId> GetLowReputationAddresses(int64_t threshold = -5000);
    
    /**
     * Analyze transaction for suspicious patterns
     */
    bool DetectSuspiciousPattern(const CTransaction& tx, std::string& reason);
    
private:
    CVMDatabase& database;
    
    /**
     * Calculate score change from vote
     */
    int64_t CalculateScoreChange(int64_t voteValue, int64_t votingPower);
    
    /**
     * Apply decay to old scores (time-based reputation decay)
     */
    void ApplyDecay(ReputationScore& score, int64_t currentTime);
};

/**
 * Suspicious pattern detection
 */
class PatternDetector {
public:
    /**
     * Detect rapid-fire transactions (possible spam).
     *
     * Consults the per-identity transaction-history index maintained via
     * RecordAddressActivity() and returns true when the identity's activity
     * matches the rapid-fire pattern (many transactions within a small block
     * window) at or before the supplied block height. (Bugfix 2.14)
     */
    static bool DetectRapidFire(const TrustNodeId& address, int blockHeight, CVMDatabase& db);
    
    /**
     * Detect mixer-like behavior
     */
    static bool DetectMixerPattern(const CTransaction& tx);
    
    /**
     * Detect dusting attack
     */
    static bool DetectDusting(const CTransaction& tx);
    
    /**
     * Detect address reuse patterns that indicate exchange.
     *
     * Consults the identity's committed reputation record and returns true when
     * its transaction volume / count matches the exchange pattern. (Bugfix 2.48)
     */
    static bool DetectExchangePattern(const TrustNodeId& address, CVMDatabase& db);

    /**
     * Record an identity's on-chain activity (block height) into the
     * transaction-history index consulted by DetectRapidFire().
     */
    static void RecordAddressActivity(const TrustNodeId& address, int blockHeight, CVMDatabase& db);

    //! Thin uint160 wrappers (legacy P2PKH callers); Wave 8 removes the
    //! remaining uint160 bridging at the call sites.
    static bool DetectRapidFire(const uint160& address, int blockHeight, CVMDatabase& db);
    static bool DetectExchangePattern(const uint160& address, CVMDatabase& db);
    static void RecordAddressActivity(const uint160& address, int blockHeight, CVMDatabase& db);
};

} // namespace CVM

#endif // CASCOIN_CVM_REPUTATION_H

