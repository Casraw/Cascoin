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
#include <net.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <cvm/eclipse_sybil_protection.h>
#include <cvm/vote_manipulation_detector.h>
#include <cvm/trust_graph_manipulation_detector.h>
#include <vector>
#include <map>
#include <set>

// Forward declarations for P2P networking
class CNode;
class CConnman;

namespace CVM {

// Forward declarations
class CVMDatabase;
class SecureHAT;
class TrustGraph;
class EclipseSybilProtection;

/**
 * Wallet Cluster
 * 
 * Represents a cluster of addresses controlled by the same entity.
 * Used for Sybil attack detection.
 */
struct WalletCluster {
    std::vector<uint160> addresses;     // Addresses in cluster
    double confidence;                  // Confidence level (0.0-1.0)
    
    WalletCluster() : confidence(0) {}
};

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
    
    /**
     * Get hash of response for signing (excludes signature field)
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << txHash;
        ss << validatorAddress;
        ss << calculatedScore;
        ss << static_cast<unsigned char>(vote);
        ss << voteConfidence;
        ss << hasWoTConnection;
        ss << relevantPaths;
        ss << trustGraphHash;
        ss << componentStatus;
        ss << verifiedComponents;
        ss << validatorPubKey;
        ss << challengeNonce;
        ss << timestamp;
        return ss.GetHash();
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(validatorAddress);
        READWRITE(calculatedScore);
        unsigned char voteVal = static_cast<unsigned char>(vote);
        READWRITE(voteVal);
        vote = static_cast<ValidationVote>(voteVal);
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
 * Sybil Alert
 * 
 * Alert for suspicious Sybil attack patterns.
 */
struct SybilAlert {
    uint160 address;
    double riskScore;
    std::string reason;
    int64_t timestamp;
    int blockHeight;
    bool reviewed;
    
    SybilAlert() : riskScore(0), timestamp(0), blockHeight(0), reviewed(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(riskScore);
        READWRITE(reason);
        READWRITE(timestamp);
        READWRITE(blockHeight);
        READWRITE(reviewed);
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
     * Anti-Manipulation: Get wallet cluster for address
     * 
     * @param address Address to check
     * @return Wallet cluster containing address
     */
    WalletCluster GetWalletCluster(const uint160& address);
    
    /**
     * Anti-Manipulation: Count recent fraud records for addresses
     * 
     * @param addresses Addresses to check
     * @param blockWindow Number of blocks to look back
     * @return Count of fraud records
     */
    int CountRecentFraudRecords(const std::vector<uint160>& addresses, int blockWindow);
    
    /**
     * Anti-Manipulation: Validate fraud record before accepting
     * 
     * Checks for:
     * - Minimum score difference threshold
     * - Sybil attack patterns
     * - Wallet clustering
     * - False accusation attempts
     * 
     * @param record Fraud record to validate
     * @return true if fraud record is legitimate
     */
    bool ValidateFraudRecord(const FraudRecord& record);
    
    /**
     * Sybil Attack Detection: Detect coordinated Sybil networks
     * 
     * Analyzes wallet clusters, transaction patterns, and reputation
     * manipulation to identify Sybil attack networks.
     * 
     * @param address Address to analyze
     * @return true if address is part of suspected Sybil network
     */
    bool DetectSybilNetwork(const uint160& address);
    
    /**
     * Sybil Attack Detection: Calculate Sybil risk score
     * 
     * Returns a risk score (0.0-1.0) indicating likelihood of Sybil attack.
     * Higher score = higher risk.
     * 
     * Factors:
     * - Cluster size (large clusters = suspicious)
     * - Cluster creation time (all addresses created recently = suspicious)
     * - Transaction patterns (coordinated activity = suspicious)
     * - Reputation patterns (all addresses have similar scores = suspicious)
     * - Fraud history (multiple fraud attempts in cluster = suspicious)
     * 
     * @param address Address to analyze
     * @return Risk score (0.0-1.0)
     */
    double CalculateSybilRiskScore(const uint160& address);
    
    /**
     * Sybil Attack Detection: Apply reputation penalty for Sybil networks
     * 
     * Applies penalties to all addresses in a detected Sybil network.
     * 
     * @param cluster Wallet cluster identified as Sybil network
     * @return true if penalties applied successfully
     */
    bool ApplySybilPenalty(const WalletCluster& cluster);
    
    /**
     * Sybil Attack Detection: Create alert for suspicious clustering
     * 
     * Creates an alert that can be reviewed by DAO or node operators.
     * 
     * @param address Address with suspicious clustering
     * @param riskScore Calculated risk score
     * @param reason Description of suspicious patterns
     * @return true if alert created successfully
     */
    bool CreateSybilAlert(const uint160& address, double riskScore, const std::string& reason);
    
    /**
     * Sybil Attack Detection: Validate HAT v2 consensus detects coordinated attacks
     * 
     * Tests whether the consensus system properly detects and rejects
     * coordinated Sybil attacks where multiple addresses vote together.
     * 
     * @param responses Validator responses to analyze
     * @return true if coordinated attack detected
     */
    bool DetectCoordinatedSybilAttack(const std::vector<ValidationResponse>& responses);
    
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
    
    /**
     * Eclipse/Sybil Protection: Check if validator is eligible
     * 
     * Validates:
     * - Network topology diversity (different IP subnets)
     * - Peer connection diversity (<50% overlap)
     * - Validator history (10,000+ blocks)
     * - Validation history (50+ validations, 85%+ accuracy)
     * - Stake age (1000+ blocks)
     * - Stake source diversity (3+ sources)
     * 
     * @param validatorAddr Validator address
     * @param currentHeight Current block height
     * @return true if validator is eligible
     */
    bool IsValidatorEligible(const uint160& validatorAddr, int currentHeight);
    
    /**
     * Eclipse/Sybil Protection: Detect Sybil network among validators
     * 
     * Analyzes:
     * - Network topology (same IP subnets)
     * - Peer connections (high overlap)
     * - Stake concentration (>20% control)
     * - Coordinated behavior (similar patterns)
     * - WoT group isolation (all from same group)
     * 
     * @param validators Validator addresses
     * @param currentHeight Current block height
     * @return Detection result with confidence and reasons
     */
    SybilDetectionResult DetectValidatorSybilNetwork(
        const std::vector<uint160>& validators,
        int currentHeight
    );
    
    /**
     * Eclipse/Sybil Protection: Validate validator set diversity
     * 
     * Ensures sufficient diversity across:
     * - Network topology
     * - Peer connections
     * - Stake sources
     * - WoT vs non-WoT validators (40% non-WoT minimum)
     * 
     * @param validators Validator addresses
     * @param currentHeight Current block height
     * @return true if validator set is sufficiently diverse
     */
    bool ValidateValidatorSetDiversity(
        const std::vector<uint160>& validators,
        int currentHeight
    );
    
    /**
     * Eclipse/Sybil Protection: Update validator network info
     * 
     * @param validatorAddr Validator address
     * @param ipAddr IP address
     * @param peers Connected peer addresses
     * @param currentHeight Current block height
     */
    void UpdateValidatorNetworkInfo(
        const uint160& validatorAddr,
        const CNetAddr& ipAddr,
        const std::set<uint160>& peers,
        int currentHeight
    );
    
    /**
     * Eclipse/Sybil Protection: Update validator stake info
     * 
     * @param validatorAddr Validator address
     * @param stakeInfo Stake information
     */
    void UpdateValidatorStakeInfo(
        const uint160& validatorAddr,
        const ValidatorStakeInfo& stakeInfo
    );
    
    /**
     * Eclipse/Sybil Protection: Check cross-group agreement
     * 
     * Ensures WoT and non-WoT validators agree within 60%.
     * 
     * @param validators Validator addresses
     * @param votes Validator votes
     * @return true if agreement within threshold
     */
    bool CheckCrossGroupAgreement(
        const std::vector<uint160>& validators,
        const std::map<uint160, int>& votes
    );
    
    /**
     * Eclipse/Sybil Protection: Escalate to DAO if Sybil network detected
     * 
     * Automatically escalates to DAO when Sybil network is detected
     * with high confidence (>60%).
     * 
     * @param detectionResult Sybil detection result
     * @param tx Transaction being validated
     * @return true if escalation successful
     */
    bool EscalateSybilDetectionToDAO(
        const SybilDetectionResult& detectionResult,
        const CTransaction& tx
    );
    
    /**
     * Get Eclipse/Sybil protection manager
     * 
     * @return Reference to protection manager
     */
    EclipseSybilProtection& GetEclipseSybilProtection() { return *m_eclipseSybilProtection; }
    
    /**
     * Get vote manipulation detector
     * 
     * @return Reference to vote manipulation detector
     */
    VoteManipulationDetector& GetVoteManipulationDetector() { return *m_voteManipulationDetector; }
    
    /**
     * Get trust graph manipulation detector
     * 
     * @return Reference to trust graph manipulation detector
     */
    TrustGraphManipulationDetector& GetTrustGraphManipulationDetector() { return *m_trustGraphManipulationDetector; }
    
    /**
     * Analyze transaction for vote manipulation
     * 
     * @param txHash Transaction hash
     * @return Manipulation detection result
     */
    ManipulationDetection AnalyzeTransactionVoting(const uint256& txHash);
    
    /**
     * Analyze address for reputation manipulation
     * 
     * @param address Address to analyze
     * @return Manipulation detection result
     */
    ManipulationDetection AnalyzeAddressReputation(const uint160& address);
    
    /**
     * Analyze address for trust graph manipulation
     * 
     * @param address Address to analyze
     * @return Trust manipulation detection result
     */
    TrustManipulationResult AnalyzeTrustGraphManipulation(const uint160& address);
    
    /**
     * Process validation request from P2P network
     * 
     * Called when a VALCHALLENGE message is received.
     * Calculates HAT v2 score and sends response.
     * 
     * @param request Validation request
     * @param pfrom Peer that sent the request
     * @param connman Connection manager
     */
    void ProcessValidationRequest(
        const ValidationRequest& request,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Process DAO dispute from P2P network
     * 
     * Called when a DAODISPUTE message is received.
     * Validates and relays dispute to DAO members.
     * 
     * @param dispute Dispute case
     * @param pfrom Peer that sent the dispute
     * @param connman Connection manager
     */
    void ProcessDAODispute(
        const DisputeCase& dispute,
        CNode* pfrom,
        CConnman* connman
    );
    
    /**
     * Process DAO resolution from P2P network
     * 
     * Called when a DAORESOLUTION message is received.
     * Updates transaction state based on DAO decision.
     * 
     * @param disputeId Dispute ID
     * @param approved DAO decision
     * @param resolutionTimestamp Resolution timestamp
     */
    void ProcessDAOResolution(
        const uint256& disputeId,
        bool approved,
        uint64_t resolutionTimestamp
    );
    
    /**
     * Broadcast validation challenge to validators
     * 
     * @param request Validation request
     * @param validators List of validator addresses
     * @param connman Connection manager
     */
    void BroadcastValidationChallenge(
        const ValidationRequest& request,
        const std::vector<uint160>& validators,
        CConnman* connman
    );
    
    /**
     * Send validation response to originator
     * 
     * @param response Validation response
     * @param connman Connection manager
     */
    void SendValidationResponse(
        const ValidationResponse& response,
        CConnman* connman
    );
    
    /**
     * Broadcast DAO dispute to network
     * 
     * @param dispute Dispute case
     * @param connman Connection manager
     */
    void BroadcastDAODispute(
        const DisputeCase& dispute,
        CConnman* connman
    );
    
    /**
     * Broadcast DAO resolution to network
     * 
     * @param disputeId Dispute ID
     * @param approved DAO decision
     * @param resolutionTimestamp Resolution timestamp
     * @param connman Connection manager
     */
    void BroadcastDAOResolution(
        const uint256& disputeId,
        bool approved,
        uint64_t resolutionTimestamp,
        CConnman* connman
    );
    
    /**
     * Notify DAO members of escalated dispute via P2P network
     * 
     * Broadcasts dispute notification to all connected peers.
     * DAO members will self-identify and process the dispute.
     * Uses MSG_DAO_DISPUTE message type.
     * 
     * @param disputeId Dispute ID
     * @param daoMembers List of DAO member addresses
     * @param connman Connection manager
     */
    void NotifyDAOMembers(
        const uint256& disputeId,
        const std::vector<uint160>& daoMembers,
        CConnman* connman
    );
    
    /**
     * Package dispute evidence for DAO review
     * 
     * Serializes validator responses and trust graph snapshot
     * at the time of dispute for DAO arbitration.
     * 
     * @param responses Validator responses
     * @param senderAddress Address of transaction sender
     * @return Serialized evidence data
     */
    std::vector<uint8_t> PackageDisputeEvidence(
        const std::vector<ValidationResponse>& responses,
        const uint160& senderAddress
    );
    
    /**
     * Load transaction evidence for fraud recording
     * 
     * Retrieves transaction from mempool or blockchain
     * for inclusion in fraud attempt records.
     * 
     * @param txHash Transaction hash
     * @param txOut Output transaction reference
     * @return true if transaction found
     */
    bool LoadTransactionEvidence(
        const uint256& txHash,
        CTransactionRef& txOut
    );
    
    /**
     * Get list of DAO members
     * 
     * Queries database for addresses that qualify as DAO members
     * based on reputation, stake, and activity requirements.
     * 
     * @return Vector of DAO member addresses
     */
    std::vector<uint160> GetDAOMemberList();
    
    /**
     * Announce validator address to network
     * 
     * Called when node starts up if it's configured as a validator.
     * Broadcasts validator address to all connected peers.
     * 
     * @param validatorAddress This node's validator address
     * @param validatorKey Private key for signing announcement
     * @param connman Connection manager
     */
    void AnnounceValidatorAddress(
        const uint160& validatorAddress,
        const CKey& validatorKey,
        CConnman* connman
    );
    
    /**
     * Collect validator responses for a transaction
     * 
     * Waits for responses from validators with timeout handling.
     * 
     * @param txHash Transaction hash
     * @param timeoutSeconds Timeout in seconds (default: 30)
     * @return Vector of collected responses
     */
    std::vector<ValidationResponse> CollectValidatorResponses(
        const uint256& txHash,
        uint32_t timeoutSeconds = VALIDATION_TIMEOUT
    );
    
    /**
     * Check if validation session has timed out
     * 
     * @param txHash Transaction hash
     * @return true if session has timed out
     */
    bool HasValidationTimedOut(const uint256& txHash);
    
    /**
     * Handle non-responsive validators
     * 
     * Updates validator reputation for validators that didn't respond.
     * 
     * @param txHash Transaction hash
     * @param selectedValidators List of validators that were selected
     */
    void HandleNonResponsiveValidators(
        const uint256& txHash,
        const std::vector<uint160>& selectedValidators
    );
    
    /**
     * Check if validator is rate-limited
     * 
     * Implements anti-spam measures for validation messages.
     * 
     * @param validatorAddress Validator address
     * @return true if validator is rate-limited
     */
    bool IsValidatorRateLimited(const uint160& validatorAddress);
    
    /**
     * Record validation message from validator
     * 
     * Updates rate limiting counters.
     * 
     * @param validatorAddress Validator address
     */
    void RecordValidationMessage(const uint160& validatorAddress);
    
    /**
     * Register validator peer
     * 
     * Called when a peer announces their validator address.
     * 
     * @param nodeId Peer node ID
     * @param validatorAddress Validator's address
     */
    void RegisterValidatorPeer(
        NodeId nodeId,
        const uint160& validatorAddress
    );
    
    /**
     * Unregister validator peer
     * 
     * Called when a peer disconnects.
     * 
     * @param nodeId Peer node ID
     */
    void UnregisterValidatorPeer(NodeId nodeId);
    
    /**
     * Get validator peer node
     * 
     * Maps validator address to connected peer node.
     * 
     * @param validatorAddress Validator address
     * @param connman Connection manager
     * @return Peer node or nullptr if not found
     */
    CNode* GetValidatorPeer(
        const uint160& validatorAddress,
        CConnman* connman
    );
    
    /**
     * Check if validator is online
     * 
     * @param validatorAddress Validator address
     * @return true if validator peer is connected
     */
    bool IsValidatorOnline(const uint160& validatorAddress);
    
    /**
     * Get all online validators
     * 
     * @return Vector of online validator addresses
     */
    std::vector<uint160> GetOnlineValidators();
    
    /**
     * Load validator peer mappings from database
     * 
     * Called on startup to restore validator mappings.
     * Note: NodeIds are not persisted, only validator addresses.
     * Validators must re-announce after node restart.
     */
    void LoadValidatorPeerMappings();
    
    /**
     * Send validation challenge to specific validator
     * 
     * Sends challenge to a specific validator peer.
     * 
     * @param validatorAddress Validator address
     * @param request Validation request
     * @param connman Connection manager
     * @return true if sent successfully
     */
    bool SendChallengeToValidator(
        const uint160& validatorAddress,
        const ValidationRequest& request,
        CConnman* connman
    );
    
private:
    CVMDatabase& database;
    SecureHAT& secureHAT;
    TrustGraph& trustGraph;
    std::unique_ptr<EclipseSybilProtection> m_eclipseSybilProtection;
    std::unique_ptr<VoteManipulationDetector> m_voteManipulationDetector;
    std::unique_ptr<TrustGraphManipulationDetector> m_trustGraphManipulationDetector;
    
    // Validator peer mapping
    struct ValidatorPeerInfo {
        NodeId nodeId;                  // Peer node ID
        uint160 validatorAddress;       // Validator's address
        uint64_t lastSeen;              // Last activity timestamp
        bool isActive;                  // Is peer currently connected?
    };
    std::map<uint160, ValidatorPeerInfo> validatorPeerMap;  // validator address → peer info
    std::map<NodeId, uint160> nodeToValidatorMap;           // node ID → validator address
    mutable CCriticalSection cs_validatorPeers;             // Protect peer maps
    
    // Rate limiting for anti-spam
    struct ValidatorRateLimit {
        uint64_t lastMessageTime;
        uint32_t messageCount;
        uint64_t windowStart;
    };
    std::map<uint160, ValidatorRateLimit> validatorRateLimits;
    
    // Rate limiting parameters
    static constexpr uint32_t RATE_LIMIT_WINDOW = 60;  // 60 seconds
    static constexpr uint32_t MAX_MESSAGES_PER_WINDOW = 100;  // Max 100 messages per minute
    
public:
    // ============================================================================
    // Consensus Security Parameters
    // ============================================================================
    // These parameters are derived from security analysis (see task-22.1-security-analysis.md)
    //
    // MIN_VALIDATORS: Minimum number of validators required for consensus
    // - Provides Byzantine fault tolerance for up to 30% malicious validators
    // - Statistical significance: 95% confidence with ±31% margin of error
    // - Recommendation: Consider increasing to 15-20 for tighter confidence intervals
    //
    // CONSENSUS_THRESHOLD: Required agreement percentage for consensus
    // - 70% threshold provides BFT guarantee (n >= 3f + 1 where f = 3)
    // - Balances security (higher threshold) vs liveness (lower threshold)
    //
    // WOT_COVERAGE_THRESHOLD: Minimum percentage of validators with WoT connection
    // - Ensures at least 30% of validators can verify WoT component
    // - Prevents acceptance based solely on non-WoT components
    //
    // SCORE_TOLERANCE: Maximum allowed difference between claimed and calculated scores
    // - ±5 points accounts for legitimate measurement variance
    // - Prevents false positives from minor calculation differences
    //
    // VALIDATION_TIMEOUT: Maximum time to wait for validator responses
    // - 30 seconds balances responsiveness vs network latency tolerance
    // ============================================================================
    
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
