// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_VOTE_MANIPULATION_DETECTOR_H
#define CASCOIN_CVM_VOTE_MANIPULATION_DETECTOR_H

#include <uint256.h>
#include <primitives/transaction.h>
#include <cvm/cvmdb.h>
#include <map>
#include <vector>
#include <set>

/**
 * Vote Manipulation Detector
 * 
 * Detects coordinated voting patterns and reputation manipulation attempts
 * in the HAT v2 consensus system and trust graph voting.
 * 
 * Detection Mechanisms:
 * 1. Coordinated voting patterns (validators voting identically across multiple transactions)
 * 2. Vote timing correlation (validators responding within suspicious time windows)
 * 3. Sudden reputation spikes (addresses gaining reputation too quickly)
 * 4. Suspicious voting behavior (validators always agreeing/disagreeing)
 * 5. Automated DAO escalation for investigation
 */

// Vote timing window for correlation analysis (milliseconds)
static constexpr int64_t VOTE_TIMING_WINDOW_MS = 1000; // 1 second

// Minimum votes to analyze for patterns
static constexpr size_t MIN_VOTES_FOR_ANALYSIS = 10;

// Correlation threshold for suspicious behavior (0.0-1.0)
static constexpr double SUSPICIOUS_CORRELATION_THRESHOLD = 0.85;

// Reputation spike threshold (points per 1000 blocks)
static constexpr int16_t REPUTATION_SPIKE_THRESHOLD = 20;

// Agreement threshold for collusion detection (0.0-1.0)
static constexpr double COLLUSION_AGREEMENT_THRESHOLD = 0.95;

/**
 * Vote record for pattern analysis
 */
struct VoteRecord {
    uint256 txHash;           // Transaction being validated
    uint160 validatorAddress; // Validator who voted
    bool voteAccept;          // true = ACCEPT, false = REJECT
    int64_t timestamp;        // Vote timestamp (milliseconds)
    int16_t scoreDifference;  // Difference between claimed and calculated score
    
    VoteRecord() : voteAccept(false), timestamp(0), scoreDifference(0) {}
    
    VoteRecord(const uint256& tx, const uint160& validator, bool accept, 
               int64_t time, int16_t diff)
        : txHash(tx), validatorAddress(validator), voteAccept(accept),
          timestamp(time), scoreDifference(diff) {}
};

/**
 * Reputation change record for spike detection
 */
struct ReputationChange {
    uint160 address;
    int blockHeight;
    int16_t oldScore;
    int16_t newScore;
    int16_t change;
    std::string reason;
    
    ReputationChange() : blockHeight(0), oldScore(0), newScore(0), change(0) {}
    
    ReputationChange(const uint160& addr, int height, int16_t old_score, 
                    int16_t new_score, const std::string& rsn)
        : address(addr), blockHeight(height), oldScore(old_score),
          newScore(new_score), change(new_score - old_score), reason(rsn) {}
};

/**
 * Manipulation detection result
 */
struct ManipulationDetection {
    enum Type {
        NONE,
        COORDINATED_VOTING,
        TIMING_CORRELATION,
        REPUTATION_SPIKE,
        COLLUSION,
        SUSPICIOUS_PATTERN
    };
    
    Type type;
    std::vector<uint160> suspiciousAddresses;
    std::vector<uint256> suspiciousTxs;
    double confidence;        // 0.0-1.0
    std::string description;
    bool escalateToDAO;
    
    ManipulationDetection() : type(NONE), confidence(0.0), escalateToDAO(false) {}
};

/**
 * Vote Manipulation Detector
 * 
 * Analyzes voting patterns and reputation changes to detect manipulation attempts.
 */
class VoteManipulationDetector {
private:
    CVM::CVMDatabase& db;
    
    // Vote history for pattern analysis
    std::map<uint256, std::vector<VoteRecord>> voteHistory;
    
    // Reputation change history
    std::map<uint160, std::vector<ReputationChange>> reputationHistory;
    
    // Validator pair correlation scores
    std::map<std::pair<uint160, uint160>, double> validatorCorrelations;
    
    // Flagged addresses
    std::set<uint160> flaggedAddresses;
    
public:
    explicit VoteManipulationDetector(CVM::CVMDatabase& database);
    
    /**
     * Record a validator vote for pattern analysis
     */
    void RecordVote(const uint256& txHash, const uint160& validatorAddress,
                   bool voteAccept, int64_t timestamp, int16_t scoreDifference);
    
    /**
     * Record a reputation change for spike detection
     */
    void RecordReputationChange(const uint160& address, int blockHeight,
                               int16_t oldScore, int16_t newScore,
                               const std::string& reason);
    
    /**
     * Detect coordinated voting patterns
     * Returns detection result if suspicious pattern found
     */
    ManipulationDetection DetectCoordinatedVoting(const uint256& txHash);
    
    /**
     * Analyze vote timing correlation between validators
     * Returns detection result if suspicious correlation found
     */
    ManipulationDetection AnalyzeVoteTimingCorrelation(const uint256& txHash);
    
    /**
     * Detect sudden reputation spikes
     * Returns detection result if suspicious spike found
     */
    ManipulationDetection DetectReputationSpike(const uint160& address);
    
    /**
     * Detect validator collusion (always agreeing/disagreeing)
     * Returns detection result if collusion detected
     */
    ManipulationDetection DetectValidatorCollusion(const uint160& validator1,
                                                   const uint160& validator2);
    
    /**
     * Analyze all patterns for a transaction
     * Returns most significant detection result
     */
    ManipulationDetection AnalyzeTransaction(const uint256& txHash);
    
    /**
     * Analyze all patterns for an address
     * Returns most significant detection result
     */
    ManipulationDetection AnalyzeAddress(const uint160& address);
    
    /**
     * Get flagged addresses
     */
    std::set<uint160> GetFlaggedAddresses() const { return flaggedAddresses; }
    
    /**
     * Flag an address as suspicious
     */
    void FlagAddress(const uint160& address);
    
    /**
     * Unflag an address (after DAO investigation)
     */
    void UnflagAddress(const uint160& address);
    
    /**
     * Check if address is flagged
     */
    bool IsAddressFlagged(const uint160& address) const;
    
    /**
     * Calculate correlation between two validators
     * Returns correlation coefficient (0.0-1.0)
     */
    double CalculateValidatorCorrelation(const uint160& validator1,
                                        const uint160& validator2);
    
    /**
     * Get vote history for a transaction
     */
    std::vector<VoteRecord> GetVoteHistory(const uint256& txHash) const;
    
    /**
     * Get reputation change history for an address
     */
    std::vector<ReputationChange> GetReputationHistory(const uint160& address) const;
    
    /**
     * Clear old vote history (keep last N transactions)
     */
    void PruneVoteHistory(size_t keepCount = 1000);
    
    /**
     * Clear old reputation history (keep last N blocks)
     */
    void PruneReputationHistory(int keepBlocks = 10000);
    
    /**
     * Persist flagged addresses to database
     */
    void SaveFlaggedAddresses();
    
    /**
     * Load flagged addresses from database
     */
    void LoadFlaggedAddresses();
};

#endif // CASCOIN_CVM_VOTE_MANIPULATION_DETECTOR_H
