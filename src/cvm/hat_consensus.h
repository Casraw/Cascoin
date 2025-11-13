// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_HAT_CONSENSUS_H
#define CASCOIN_CVM_HAT_CONSENSUS_H

#include <uint256.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <key.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <vector>
#include <map>
#include <set>

namespace CVM {

// Forward declarations
class CVMDatabase;
class SecureHAT;
class TrustGraph;

/**
 * HAT v2 Score with Component Breakdown
 * 
 * Represents a complete HAT v2 trust score with all components.
 * Used for consensus validation where validators compare their
 * calculated scores with sender-declared scores.
 */
struct HATv2Score {
    uint160 address;                    // Address being scored
    int16_t finalScore;                 // Final trust score (0-100)
    uint64_t timestamp;                 // When score was calculated
    
    // Component scores (for verification)
    double behaviorScore;               // 40% weight
    double wotScore;                    // 30% weight
    double economicScore;               // 20% weight
    double temporalScore;               // 10% weight
    
    // Metadata
    bool hasWoTConnection;              // Does validator have WoT path to address?
    uint32_t wotPathCount;              // Number of WoT paths found
    double wotPathStrength;             // Average path strength
    
    HATv2Score() : finalScore(0), timestamp(0), behaviorScore(0), wotScore(0),
                   economicScore(0), temporalScore(0), hasWoTConnection(false),
                   wotPathCount(0), wotPathStrength(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(finalScore);
        READWRITE(timestamp);
        READWRITE(behaviorScore);
        READWRITE(wotScore);
        READWRITE(economicScore);
        READWRITE(temporalScore);
        READWRITE(hasWoTConnection);
        READWRITE(wotPathCount);
        READWRITE(wotPathStrength);
    }
};

/**
 * Validation Request
 * 
 * Sent to randomly selected validators to verify a transaction's
 * self-reported reputation score.
 */
struct ValidationRequest {
    uint256 txHash;                     // Transaction being validated
    uint160 senderAddress;              // Sender's address
    HATv2Score selfReportedScore;       // Score claimed by sender
    uint256 challengeNonce;             // Cryptographic nonce for replay protection
    uint64_t timestamp;                 // Request timestamp
    int blockHeight;                    // Current block height
    
    ValidationRequest() : timestamp(0), blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(senderAddress);
        READWRITE(selfReportedScore);
        READWRITE(challengeNonce);
        READWRITE(timestamp);
        READWRITE(blockHeight);
    }
    
    /**
     * Generate challenge nonce
     */
    static uint256 GenerateChallengeNonce(const uint256& txHash, int blockHeight);
};

/**
 * Validation Vote
 * 
 * Validator's decision on whether to accept or reject the self-reported score.
 */
enum class ValidationVote {
    ACCEPT,         // Score is accurate (within tolerance)
    REJECT,         // Score is fraudulent (exceeds tolerance)
    ABSTAIN         // Cannot verify (no WoT connection, insufficient data)
};

/**
 * Component Status
 * 
 * For validators without WoT connection, they can verify non-WoT components.
 */
struct ComponentStatus {
    bool behaviorVerified;
    bool economicVerified;
    bool temporalVerified;
    bool wotVerified;
    
    double behaviorDifference;
    double economicDifference;
    double temporalDifference;
    
    ComponentStatus() : behaviorVerified(false), economicVerified(false),
                       temporalVerified(false), wotVerified(false),
                       behaviorDifference(0), economicDifference(0),
                       temporalDifference(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(behaviorVerified);
        READWRITE(economicVerified);
        READWRITE(temporalVerified);
        READWRITE(wotVerified);
        READWRITE(behaviorDifference);
        READWRITE(economicDifference);
        READWRITE(temporalDifference);
    }
};

/**
 * Validation Response
 * 
 * Validator's response to a validation request.
 */
struct ValidationResponse {
    uint256 txHash;                     // Transaction being validated
    uint160 validatorAddress;           // Validator's address
    HATv2Score calculatedScore;         // Score calculated by validator
    ValidationVote vote;                // Accept/Reject/Abstain
    double voteConfidence;              // Confidence level (0.0-1.0)
    
    // WoT connectivity info
    bool hasWoTConnection;              // Does validator have WoT path?
    std::vector<TrustPath> relevantPaths; // Trust paths used (if any)
    uint256 trustGraphHash;             // Hash of validator's trust graph state
    
    // Component verification (for non-WoT validators)
    ComponentStatus componentStatus;
    HATv2Score verifiedComponents;      // Non-WoT components only
    
    // Cryptographic proof
    std::vector<uint8_t> validatorPubKey; // Validator's public key (for signature verification)
    std::vector<uint8_t> signature;     // Validator's signature
    uint256 challengeNonce;             // Nonce from request (replay protection)
    uint64_t timestamp;                 // Response timestamp
    
    ValidationResponse() : vote(ValidationVote::ABSTAIN), voteConfidence(0),
                          hasWoTConnection(false), timestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(validatorAddress);
        READWRITE(calculatedScore);
        READWRITE((int&)vote);
        READWRITE(voteConfidence);
        READWRITE(hasWoTConnection);
        READWRITE(relevantPaths);
        READWRITE(trustGraphHash);
        READWRITE(componentStatus);
        READWRITE(verifiedComponents);
        READWRITE(validatorPubKey);
        READWRITE(signature);
        READWRITE(challengeNonce);
        READWRITE(timestamp);
    }
    
    /**
     * Sign the response
     */
    bool Sign(const CKey& validatorKey);
    
    /**
     * Verify signature
     */
    bool VerifySignature() const;
};

/**
 * Consensus Result
 * 
 * Aggregated result from all validator responses.
 */
struct ConsensusResult {
    uint256 txHash;
    bool consensusReached;              // Did validators reach consensus?
    bool approved;                      // Was transaction approved?
    bool requiresDAOReview;             // Should DAO arbitrate?
    
    // Vote tallies
    uint32_t acceptVotes;
    uint32_t rejectVotes;
    uint32_t abstainVotes;
    
    // Weighted tallies (WoT validators count more)
    double weightedAccept;
    double weightedReject;
    double weightedAbstain;
    
    // Validator responses
    std::vector<ValidationResponse> responses;
    
    // Consensus threshold
    double consensusThreshold;          // Required agreement (default: 0.70)
    double wotCoverageThreshold;        // Required WoT coverage (default: 0.30)
    
    ConsensusResult() : consensusReached(false), approved(false),
                       requiresDAOReview(false), acceptVotes(0), rejectVotes(0),
                       abstainVotes(0), weightedAccept(0), weightedReject(0),
                       weightedAbstain(0), consensusThreshold(0.70),
                       wotCoverageThreshold(0.30) {}
};

/**
 * Dispute Case
 * 
 * Created when validators cannot reach consensus.
 * Escalated to DAO for arbitration.
 */
struct DisputeCase {
    uint256 disputeId;
    uint256 txHash;
    uint160 senderAddress;
    HATv2Score selfReportedScore;
    
    // Validator responses
    std::vector<ValidationResponse> validatorResponses;
    
    // Evidence
    std::vector<uint8_t> evidenceData;
    std::string disputeReason;
    
    // DAO resolution
    bool resolved;
    bool approved;                      // DAO decision
    uint64_t resolutionTimestamp;
    
    DisputeCase() : resolved(false), approved(false), resolutionTimestamp(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(disputeId);
        READWRITE(txHash);
        READWRITE(senderAddress);
        READWRITE(selfReportedScore);
        READWRITE(validatorResponses);
        READWRITE(evidenceData);
        READWRITE(disputeReason);
        READWRITE(resolved);
        READWRITE(approved);
        READWRITE(resolutionTimestamp);
    }
};

/**
 * Fraud Record
 * 
 * Permanent on-chain record of reputation fraud attempts.
 */
struct FraudRecord {
    uint256 txHash;
    uint160 fraudsterAddress;
    HATv2Score claimedScore;
    HATv2Score actualScore;
    int16_t scoreDifference;
    uint64_t timestamp;
    int blockHeight;
    
    // Penalties
    int16_t reputationPenalty;          // Reputation points deducted
    CAmount bondSlashed;                // Bond amount slashed (if any)
    
    FraudRecord() : scoreDifference(0), timestamp(0), blockHeight(0),
                   reputationPenalty(0), bondSlashed(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(fraudsterAddress);
        READWRITE(claimedScore);
        READWRITE(actualScore);
        READWRITE(scoreDifference);
        READWRITE(timestamp);
        READWRITE(blockHeight);
        READWRITE(reputationPenalty);
        READWRITE(bondSlashed);
    }
};

/**
 * Validator Stats
 * 
 * Tracks validator performance and accountability.
 */
struct ValidatorStats {
    uint160 validatorAddress;
    uint64_t totalValidations;
    uint64_t accurateValidations;
    uint64_t inaccurateValidations;
    uint64_t abstentions;
    double accuracyRate;
    int16_t validatorReputation;
    int64_t lastActivityTime;
    
    ValidatorStats() : totalValidations(0), accurateValidations(0),
                      inaccurateValidations(0), abstentions(0),
                      accuracyRate(0), validatorReputation(50), lastActivityTime(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(validatorAddress);
        READWRITE(totalValidations);
        READWRITE(accurateValidations);
        READWRITE(inaccurateValidations);
        READWRITE(abstentions);
        READWRITE(accuracyRate);
        READWRITE(validatorReputation);
        READWRITE(lastActivityTime);
    }
    
    void UpdateAccuracy() {
        if (totalValidations > 0) {
            accuracyRate = (double)accurateValidations / totalValidations;
        }
    }
};

/**
 * Transaction State
 * 
 * Tracks validation state of transactions in mempool.
 */
enum class TransactionState {
    PENDING_VALIDATION,     // Awaiting validator responses
    VALIDATED,              // Consensus reached, approved
    DISPUTED,               // No consensus, escalated to DAO
    REJECTED                // Consensus reached, rejected
};

/**
 * HAT Consensus Validator
 * 
 * Implements distributed reputation verification system where:
 * 1. Senders declare their HAT v2 score in transactions
 * 2. Random validators verify the declared score
 * 3. Validators vote based on their own HAT v2 calculation
 * 4. Consensus is reached if 70%+ validators agree
 * 5. Disputed transactions escalate to DAO
 * 6. Fraud attempts are recorded on-chain
 */
class HATConsensusValidator {
public:
    explicit HATConsensusValidator(CVMDatabase& db, SecureHAT& hat, TrustGraph& graph);
    
    /**
     * Initiate validation for a transaction
     * 
     * @param tx Transaction to validate
     * @param selfReportedScore Sender's claimed HAT v2 score
     * @return Validation request to send to validators
     */
    ValidationRequest InitiateValidation(
        const CTransaction& tx,
        const HATv2Score& selfReportedScore
    );
    
    /**
     * Select random validators for a transaction
     * 
     * Uses deterministic randomness based on tx hash + block height.
     * Minimum 10 validators selected.
     * 
     * @param txHash Transaction hash
     * @param blockHeight Current block height
     * @return Vector of validator addresses
     */
    std::vector<uint160> SelectRandomValidators(
        const uint256& txHash,
        int blockHeight
    );
    
    /**
     * Send validation challenge to a validator
     * 
     * @param validator Validator address
     * @param request Validation request
     * @return true if challenge sent successfully
     */
    bool SendValidationChallenge(
        const uint160& validator,
        const ValidationRequest& request
    );
    
    /**
     * Process validator response
     * 
     * @param response Validator's response
     * @return true if response is valid
     */
    bool ProcessValidatorResponse(const ValidationResponse& response);
    
    /**
     * Determine consensus from validator responses
     * 
     * @param responses All validator responses
     * @return Consensus result
     */
    ConsensusResult DetermineConsensus(
        const std::vector<ValidationResponse>& responses
    );
    
    /**
     * Create dispute case for DAO review
     * 
     * @param tx Transaction
     * @param responses Validator responses
     * @return Dispute case
     */
    DisputeCase CreateDisputeCase(
        const CTransaction& tx,
        const std::vector<ValidationResponse>& responses
    );
    
    /**
     * Escalate dispute to DAO
     * 
     * @param dispute Dispute case
     * @return true if escalation successful
     */
    bool EscalateToDAO(const DisputeCase& dispute);
    
    /**
     * Process DAO resolution
     * 
     * @param disputeId Dispute ID
     * @param resolution DAO resolution
     * @return true if processing successful
     */
    bool ProcessDAOResolution(
        const uint256& disputeId,
        const DAODispute& resolution
    );
    
    /**
     * Record fraud attempt on-chain
     * 
     * @param fraudsterAddress Address that attempted fraud
     * @param tx Transaction
     * @param claimedScore Self-reported score from transaction
     * @param actualScore Actual calculated score
     * @return true if recorded successfully
     */
    bool RecordFraudAttempt(
        const uint160& fraudsterAddress,
        const CTransaction& tx,
        const HATv2Score& claimedScore,
        const HATv2Score& actualScore
    );
    
    /**
     * Update validator reputation
     * 
     * @param validator Validator address
     * @param accurate Was validation accurate?
     * @return true if updated successfully
     */
    bool UpdateValidatorReputation(
        const uint160& validator,
        bool accurate
    );
    
    /**
     * Get transaction validation state
     * 
     * @param txHash Transaction hash
     * @return Transaction state
     */
    TransactionState GetTransactionState(const uint256& txHash);
    
    /**
     * Update transaction state in mempool
     * 
     * @param txHash Transaction hash
     * @param state New state
     * @return true if updated successfully
     */
    bool UpdateMempoolState(
        const uint256& txHash,
        TransactionState state
    );
    
    /**
     * Get dispute case
     * 
     * @param disputeId Dispute ID
     * @return Dispute case
     */
    DisputeCase GetDisputeCase(const uint256& disputeId);
    
    /**
     * Check if address has WoT connection to another address
     * 
     * @param validator Validator address
     * @param target Target address
     * @return true if WoT connection exists
     */
    bool HasWoTConnection(
        const uint160& validator,
        const uint160& target
    );
    
    /**
     * Calculate vote confidence for a validator
     * 
     * Based on WoT connectivity and validator reputation.
     * 
     * @param validator Validator address
     * @param target Target address
     * @return Confidence level (0.0-1.0)
     */
    double CalculateVoteConfidence(
        const uint160& validator,
        const uint160& target
    );
    
    /**
     * Calculate validator's vote
     * 
     * @param selfReported Sender's claimed score
     * @param calculated Validator's calculated score
     * @param hasWoT Does validator have WoT connection?
     * @return Validation vote
     */
    ValidationVote CalculateValidatorVote(
        const HATv2Score& selfReported,
        const HATv2Score& calculated,
        bool hasWoT
    );
    
    /**
     * Calculate non-WoT components only
     * 
     * For validators without WoT connection.
     * 
     * @param address Address to evaluate
     * @return HAT v2 score with only non-WoT components
     */
    HATv2Score CalculateNonWoTComponents(const uint160& address);
    
    /**
     * Get validator stats
     * 
     * @param validator Validator address
     * @return Validator statistics
     */
    ValidatorStats GetValidatorStats(const uint160& validator);
    
private:
    CVMDatabase& database;
    SecureHAT& secureHAT;
    TrustGraph& trustGraph;
    
public:
    // Consensus parameters
    static constexpr int MIN_VALIDATORS = 10;
    static constexpr double CONSENSUS_THRESHOLD = 0.70;
    static constexpr double WOT_COVERAGE_THRESHOLD = 0.30;
    static constexpr int16_t SCORE_TOLERANCE = 5;  // ±5 points
    static constexpr double WOT_VOTE_WEIGHT = 1.0;
    static constexpr double NON_WOT_VOTE_WEIGHT = 0.5;
    static constexpr uint32_t VALIDATION_TIMEOUT = 30;  // 30 seconds
    
private:
    
    /**
     * Check if address is eligible to be a validator
     */
    bool IsEligibleValidator(const uint160& address);
    
    /**
     * Generate deterministic random seed
     */
    uint256 GenerateRandomSeed(const uint256& txHash, int blockHeight);
    
    /**
     * Compare HAT v2 scores
     */
    bool ScoresMatch(const HATv2Score& score1, const HATv2Score& score2, int16_t tolerance);
    
    /**
     * Calculate weighted vote tally
     */
    void CalculateWeightedVotes(
        const std::vector<ValidationResponse>& responses,
        double& weightedAccept,
        double& weightedReject,
        double& weightedAbstain
    );
    
    /**
     * Check WoT coverage requirement
     */
    bool MeetsWoTCoverage(const std::vector<ValidationResponse>& responses);
};

} // namespace CVM

#endif // CASCOIN_CVM_HAT_CONSENSUS_H
