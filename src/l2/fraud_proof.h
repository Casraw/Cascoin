// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_FRAUD_PROOF_H
#define CASCOIN_L2_FRAUD_PROOF_H

/**
 * @file fraud_proof.h
 * @brief Fraud Proof System for Cascoin L2
 * 
 * This file implements the fraud proof system that enables any node to
 * challenge invalid L2 state transitions. The system supports both
 * single-round fraud proofs and interactive fraud proofs for complex disputes.
 * 
 * Key features:
 * - Multiple fraud proof types (invalid state, invalid tx, double spend, etc.)
 * - Interactive fraud proofs with binary search for complex disputes
 * - Slashing mechanism for fraudulent sequencers
 * - Reward distribution for successful challengers
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <l2/l2_common.h>
#include <l2/sparse_merkle_tree.h>
#include <uint256.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>
#include <optional>
#include <string>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Challenge bond required to submit a fraud proof: 10 CAS */
static constexpr CAmount FRAUD_PROOF_CHALLENGE_BOND = 10 * COIN;

/** Minimum slashing amount for valid fraud proof: 50 CAS */
static constexpr CAmount MIN_SLASHING_AMOUNT = 50 * COIN;

/** Maximum slashing percentage of sequencer stake: 100% */
static constexpr uint32_t MAX_SLASHING_PERCENT = 100;

/** Challenger reward percentage from slashed stake: 50% */
static constexpr uint32_t CHALLENGER_REWARD_PERCENT = 50;

/** Maximum interactive proof steps before timeout */
static constexpr uint32_t MAX_INTERACTIVE_STEPS = 256;

/** Interactive proof step timeout in seconds: 1 hour */
static constexpr uint64_t INTERACTIVE_STEP_TIMEOUT = 3600;

/** Maximum execution trace size in bytes: 1MB */
static constexpr size_t MAX_EXECUTION_TRACE_SIZE = 1024 * 1024;

/** Maximum state proof size in bytes: 100KB */
static constexpr size_t MAX_STATE_PROOF_SIZE = 100 * 1024;

/** Maximum transactions in a fraud proof: 100 */
static constexpr size_t MAX_FRAUD_PROOF_TRANSACTIONS = 100;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Execution result from re-executing a transaction
 */
struct ExecutionResult {
    bool success;
    std::string error;
    uint64_t gasUsed;
    uint256 postStateRoot;
    std::vector<std::vector<unsigned char>> logs;

    ExecutionResult() : success(false), gasUsed(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(success);
        READWRITE(error);
        READWRITE(gasUsed);
        READWRITE(postStateRoot);
        READWRITE(logs);
    }

    static ExecutionResult Success(uint64_t gas, const uint256& root) {
        ExecutionResult result;
        result.success = true;
        result.gasUsed = gas;
        result.postStateRoot = root;
        return result;
    }

    static ExecutionResult Failure(const std::string& err) {
        ExecutionResult result;
        result.success = false;
        result.error = err;
        return result;
    }
};

/**
 * @brief Fraud proof submission
 * Requirements: 5.1, 5.6
 */
struct FraudProof {
    FraudProofType type = FraudProofType::INVALID_STATE_TRANSITION;
    uint256 disputedStateRoot;
    uint64_t disputedBlockNumber = 0;
    uint256 previousStateRoot;
    uint64_t l2ChainId = DEFAULT_L2_CHAIN_ID;
    std::vector<CMutableTransaction> relevantTransactions;
    std::vector<unsigned char> stateProof;
    std::vector<unsigned char> executionTrace;
    uint160 challengerAddress;
    CAmount challengeBond = 0;
    std::vector<unsigned char> challengerSignature;
    uint64_t submittedAt = 0;
    uint160 sequencerAddress;

    FraudProof() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint8_t typeByte = static_cast<uint8_t>(type);
        READWRITE(typeByte);
        if (ser_action.ForRead()) {
            type = static_cast<FraudProofType>(typeByte);
        }
        READWRITE(disputedStateRoot);
        READWRITE(disputedBlockNumber);
        READWRITE(previousStateRoot);
        READWRITE(l2ChainId);
        READWRITE(relevantTransactions);
        READWRITE(stateProof);
        READWRITE(executionTrace);
        READWRITE(challengerAddress);
        READWRITE(challengeBond);
        READWRITE(challengerSignature);
        READWRITE(submittedAt);
        READWRITE(sequencerAddress);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << static_cast<uint8_t>(type);
        ss << disputedStateRoot;
        ss << disputedBlockNumber;
        ss << previousStateRoot;
        ss << l2ChainId;
        ss << challengerAddress;
        ss << submittedAt;
        ss << sequencerAddress;
        return ss.GetHash();
    }

    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << static_cast<uint8_t>(type);
        ss << disputedStateRoot;
        ss << disputedBlockNumber;
        ss << previousStateRoot;
        ss << l2ChainId;
        ss << challengerAddress;
        ss << challengeBond;
        ss << submittedAt;
        ss << sequencerAddress;
        return ss.GetHash();
    }

    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool ValidateStructure() const {
        if (disputedStateRoot.IsNull()) return false;
        if (previousStateRoot.IsNull()) return false;
        if (challengerAddress.IsNull()) return false;
        if (sequencerAddress.IsNull()) return false;
        if (challengeBond < FRAUD_PROOF_CHALLENGE_BOND) return false;
        if (stateProof.size() > MAX_STATE_PROOF_SIZE) return false;
        if (executionTrace.size() > MAX_EXECUTION_TRACE_SIZE) return false;
        if (relevantTransactions.size() > MAX_FRAUD_PROOF_TRANSACTIONS) return false;
        return true;
    }

    bool operator==(const FraudProof& other) const {
        return type == other.type &&
               disputedStateRoot == other.disputedStateRoot &&
               disputedBlockNumber == other.disputedBlockNumber &&
               previousStateRoot == other.previousStateRoot &&
               challengerAddress == other.challengerAddress &&
               sequencerAddress == other.sequencerAddress;
    }
};

/**
 * @brief Interactive fraud proof step
 * Requirement: 5.6
 */
struct InteractiveFraudProofStep {
    uint64_t stepNumber;
    uint256 preStateRoot;
    uint256 postStateRoot;
    std::vector<unsigned char> instruction;
    std::vector<unsigned char> witness;
    uint64_t gasUsed;
    uint64_t submittedAt;
    uint160 submitter;
    std::vector<unsigned char> signature;

    InteractiveFraudProofStep()
        : stepNumber(0), gasUsed(0), submittedAt(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(stepNumber);
        READWRITE(preStateRoot);
        READWRITE(postStateRoot);
        READWRITE(instruction);
        READWRITE(witness);
        READWRITE(gasUsed);
        READWRITE(submittedAt);
        READWRITE(submitter);
        READWRITE(signature);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << stepNumber << preStateRoot << postStateRoot << instruction << gasUsed;
        return ss.GetHash();
    }

    bool ValidateStructure() const {
        if (preStateRoot.IsNull()) return false;
        if (postStateRoot.IsNull()) return false;
        if (instruction.empty()) return false;
        if (submitter.IsNull()) return false;
        return true;
    }

    bool operator==(const InteractiveFraudProofStep& other) const {
        return stepNumber == other.stepNumber &&
               preStateRoot == other.preStateRoot &&
               postStateRoot == other.postStateRoot &&
               instruction == other.instruction;
    }
};

/** Interactive proof session state */
enum class InteractiveProofState : uint8_t {
    INITIATED = 0,
    CHALLENGER_TURN = 1,
    SEQUENCER_TURN = 2,
    RESOLVED = 3,
    TIMEOUT = 4,
    CANCELLED = 5
};

inline std::ostream& operator<<(std::ostream& os, InteractiveProofState state) {
    switch (state) {
        case InteractiveProofState::INITIATED: return os << "INITIATED";
        case InteractiveProofState::CHALLENGER_TURN: return os << "CHALLENGER_TURN";
        case InteractiveProofState::SEQUENCER_TURN: return os << "SEQUENCER_TURN";
        case InteractiveProofState::RESOLVED: return os << "RESOLVED";
        case InteractiveProofState::TIMEOUT: return os << "TIMEOUT";
        case InteractiveProofState::CANCELLED: return os << "CANCELLED";
        default: return os << "UNKNOWN";
    }
}


/** Interactive fraud proof session */
struct InteractiveProofSession {
    uint256 sessionId;
    uint256 fraudProofHash;
    uint160 challenger;
    uint160 sequencer;
    InteractiveProofState state;
    std::vector<InteractiveFraudProofStep> steps;
    uint64_t searchLower;
    uint64_t searchUpper;
    uint64_t totalSteps;
    uint64_t createdAt;
    uint64_t lastActivityAt;
    uint64_t stepDeadline;
    uint160 winner;
    uint64_t invalidStepNumber;
    uint64_t l2ChainId;

    InteractiveProofSession()
        : state(InteractiveProofState::INITIATED)
        , searchLower(0), searchUpper(0), totalSteps(0)
        , createdAt(0), lastActivityAt(0), stepDeadline(0)
        , invalidStepNumber(0), l2ChainId(DEFAULT_L2_CHAIN_ID) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sessionId);
        READWRITE(fraudProofHash);
        READWRITE(challenger);
        READWRITE(sequencer);
        uint8_t stateByte = static_cast<uint8_t>(state);
        READWRITE(stateByte);
        if (ser_action.ForRead()) {
            state = static_cast<InteractiveProofState>(stateByte);
        }
        READWRITE(steps);
        READWRITE(searchLower);
        READWRITE(searchUpper);
        READWRITE(totalSteps);
        READWRITE(createdAt);
        READWRITE(lastActivityAt);
        READWRITE(stepDeadline);
        READWRITE(winner);
        READWRITE(invalidStepNumber);
        READWRITE(l2ChainId);
    }

    bool IsTimedOut(uint64_t currentTime) const {
        return currentTime > stepDeadline && 
               state != InteractiveProofState::RESOLVED &&
               state != InteractiveProofState::CANCELLED;
    }

    bool IsChallengerTurn() const { return state == InteractiveProofState::CHALLENGER_TURN; }
    bool IsSequencerTurn() const { return state == InteractiveProofState::SEQUENCER_TURN; }
    bool IsResolved() const {
        return state == InteractiveProofState::RESOLVED ||
               state == InteractiveProofState::TIMEOUT ||
               state == InteractiveProofState::CANCELLED;
    }
    uint64_t GetMidpoint() const { return searchLower + (searchUpper - searchLower) / 2; }
    bool HasConverged() const { return searchUpper - searchLower <= 1; }
};

/** Fraud proof result */
enum class FraudProofResult : uint8_t {
    PENDING = 0,
    VALID = 1,
    INVALID = 2,
    EXPIRED = 3,
    INSUFFICIENT_BOND = 4
};

inline std::ostream& operator<<(std::ostream& os, FraudProofResult result) {
    switch (result) {
        case FraudProofResult::PENDING: return os << "PENDING";
        case FraudProofResult::VALID: return os << "VALID";
        case FraudProofResult::INVALID: return os << "INVALID";
        case FraudProofResult::EXPIRED: return os << "EXPIRED";
        case FraudProofResult::INSUFFICIENT_BOND: return os << "INSUFFICIENT_BOND";
        default: return os << "UNKNOWN";
    }
}

/** Slashing record for a sequencer */
struct SlashingRecord {
    uint160 sequencerAddress;
    CAmount slashedAmount;
    uint256 fraudProofHash;
    uint160 challenger;
    CAmount challengerReward;
    uint64_t slashedAt;
    uint64_t blockNumber;
    int32_t reputationPenalty;

    SlashingRecord()
        : slashedAmount(0), challengerReward(0), slashedAt(0)
        , blockNumber(0), reputationPenalty(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(slashedAmount);
        READWRITE(fraudProofHash);
        READWRITE(challenger);
        READWRITE(challengerReward);
        READWRITE(slashedAt);
        READWRITE(blockNumber);
        READWRITE(reputationPenalty);
    }
};

/** Fraud proof verification result */
struct FraudProofVerificationResult {
    bool verified;
    FraudProofResult result;
    std::string error;
    uint256 expectedStateRoot;
    uint256 actualStateRoot;
    uint64_t gasUsed;

    FraudProofVerificationResult()
        : verified(false), result(FraudProofResult::PENDING), gasUsed(0) {}

    static FraudProofVerificationResult Valid(const uint256& expected, const uint256& actual) {
        FraudProofVerificationResult r;
        r.verified = true;
        r.result = FraudProofResult::VALID;
        r.expectedStateRoot = expected;
        r.actualStateRoot = actual;
        return r;
    }

    static FraudProofVerificationResult Invalid(const std::string& err) {
        FraudProofVerificationResult r;
        r.verified = true;
        r.result = FraudProofResult::INVALID;
        r.error = err;
        return r;
    }
};

// ============================================================================
// Fraud Proof System Class
// ============================================================================

/**
 * @brief Fraud Proof System
 * 
 * Manages fraud proof submission, verification, and resolution.
 * Thread-safe for concurrent access.
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */
class FraudProofSystem {
public:
    explicit FraudProofSystem(uint64_t chainId);

    // Single-Round Fraud Proofs (Requirements 5.1, 5.2)
    bool SubmitFraudProof(const FraudProof& proof, uint64_t currentTime);
    FraudProofVerificationResult VerifyFraudProof(const FraudProof& proof);
    ExecutionResult ReExecuteOnL1(const CMutableTransaction& tx, const uint256& preStateRoot);
    std::optional<FraudProof> GetFraudProof(const uint256& proofHash) const;
    FraudProofResult GetFraudProofResult(const uint256& proofHash) const;
    bool IsStateRootFinalized(const uint256& stateRoot, uint64_t currentTime) const;
    uint64_t GetChallengeDeadline(const uint256& stateRoot) const;
    void RegisterStateRoot(const uint256& stateRoot, uint64_t blockNumber, uint64_t deadline);

    // Interactive Fraud Proofs (Requirement 5.6)
    uint256 StartInteractiveProof(const uint256& disputedStateRoot, const uint160& challenger,
                                   const uint160& sequencer, uint64_t totalSteps, uint64_t currentTime);
    bool SubmitInteractiveStep(const uint256& sessionId, const InteractiveFraudProofStep& step,
                               uint64_t currentTime);
    FraudProofResult ResolveInteractiveProof(const uint256& sessionId, uint64_t currentTime);
    std::optional<InteractiveProofSession> GetInteractiveSession(const uint256& sessionId) const;
    size_t ProcessTimeouts(uint64_t currentTime);

    // Slashing and Rewards (Requirements 5.4, 5.5)
    SlashingRecord SlashSequencer(const uint160& sequencer, const FraudProof& proof, uint64_t currentTime);
    CAmount RewardChallenger(const uint160& challenger, CAmount slashedAmount);
    std::vector<SlashingRecord> GetSlashingRecords(const uint160& sequencer) const;
    CAmount GetTotalSlashed(const uint160& sequencer) const;

    // Utility Methods
    uint64_t GetChainId() const { return chainId_; }
    size_t GetActiveFraudProofCount() const;
    size_t GetActiveSessionCount() const;
    void Clear();
    void SetSequencerStake(const uint160& sequencer, CAmount stake);
    CAmount GetSequencerStake(const uint160& sequencer) const;

private:
    uint64_t chainId_;
    std::map<uint256, FraudProof> activeProofs_;
    std::map<uint256, FraudProofResult> proofResults_;
    std::map<uint256, InteractiveProofSession> interactiveSessions_;
    std::map<uint256, uint64_t> stateRootDeadlines_;
    std::map<uint256, uint64_t> stateRootBlocks_;
    std::map<uint160, std::vector<SlashingRecord>> slashingRecords_;
    std::map<uint160, CAmount> sequencerStakes_;
    std::map<uint160, CAmount> challengerRewards_;
    mutable CCriticalSection cs_fraudproof_;
    uint64_t nextSessionId_;

    uint256 GenerateSessionId(const uint160& challenger, const uint160& sequencer, uint64_t timestamp) const;
    uint64_t BinarySearchInvalidStep(const InteractiveProofSession& session) const;
    bool VerifyExecutionStep(const InteractiveFraudProofStep& step) const;
    CAmount CalculateSlashingAmount(FraudProofType type, CAmount sequencerStake) const;
    int32_t CalculateReputationPenalty(FraudProofType type) const;
};

} // namespace l2

#endif // CASCOIN_L2_FRAUD_PROOF_H
