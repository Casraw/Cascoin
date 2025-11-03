// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_REPUTATION_H
#define CASCOIN_CVM_REPUTATION_H

#include <uint256.h>
#include <serialize.h>
#include <primitives/transaction.h>
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
 */

/**
 * Reputation score for an address
 */
class ReputationScore {
public:
    uint160 address;           // Address being scored
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
    uint160 targetAddress;     // Address being voted on
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
     * Get reputation score for address
     */
    bool GetReputation(const uint160& address, ReputationScore& score);
    
    /**
     * Apply a reputation vote
     */
    bool ApplyVote(const uint160& voterAddress, const ReputationVoteTx& vote, int64_t timestamp);
    
    /**
     * Update reputation based on transaction behavior
     */
    void UpdateBehaviorScore(const uint160& address, const CTransaction& tx, int blockHeight);
    
    /**
     * Calculate voting power for an address
     * Based on: coin age, stake, own reputation
     */
    int64_t GetVotingPower(const uint160& address);
    
    /**
     * Get list of addresses with poor reputation
     */
    std::vector<uint160> GetLowReputationAddresses(int64_t threshold = -5000);
    
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
     * Detect rapid-fire transactions (possible spam)
     */
    static bool DetectRapidFire(const uint160& address, int blockHeight);
    
    /**
     * Detect mixer-like behavior
     */
    static bool DetectMixerPattern(const CTransaction& tx);
    
    /**
     * Detect dusting attack
     */
    static bool DetectDusting(const CTransaction& tx);
    
    /**
     * Detect address reuse patterns that indicate exchange
     */
    static bool DetectExchangePattern(const uint160& address);
};

} // namespace CVM

#endif // CASCOIN_CVM_REPUTATION_H

