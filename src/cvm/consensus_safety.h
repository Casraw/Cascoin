// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CONSENSUS_SAFETY_H
#define CASCOIN_CVM_CONSENSUS_SAFETY_H

#include <uint256.h>
#include <serialize.h>
#include <cvm/securehat.h>
#include <cvm/hat_consensus.h>
#include <cvm/sustainable_gas.h>
#include <cvm/trust_context.h>
#include <cvm/cross_chain_bridge.h>
#include <vector>
#include <map>
#include <set>
#include <mutex>

namespace CVM {

// Forward declarations
class CVMDatabase;
class SecureHAT;
class TrustGraph;

/**
 * Deterministic Execution Result
 * 
 * Result of deterministic execution validation.
 */
struct DeterministicExecutionResult {
    bool isDeterministic;               // Whether execution is deterministic
    uint256 executionHash;              // Hash of execution result
    std::string failureReason;          // Reason for non-determinism (if any)
    
    // Component hashes for debugging
    uint256 behaviorHash;               // Hash of behavior component
    uint256 wotHash;                    // Hash of WoT component
    uint256 economicHash;               // Hash of economic component
    uint256 temporalHash;               // Hash of temporal component
    
    DeterministicExecutionResult() : isDeterministic(true) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isDeterministic);
        READWRITE(executionHash);
        READWRITE(failureReason);
        READWRITE(behaviorHash);
        READWRITE(wotHash);
        READWRITE(economicHash);
        READWRITE(temporalHash);
    }
};

/**
 * Validator Selection Result
 * 
 * Result of validator selection validation.
 */
struct ValidatorSelectionResult {
    bool isConsistent;                  // Whether selection is consistent
    std::vector<uint160> selectedValidators; // Selected validators
    uint256 selectionSeed;              // Seed used for selection
    std::string failureReason;          // Reason for inconsistency (if any)
    
    ValidatorSelectionResult() : isConsistent(true) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isConsistent);
        READWRITE(selectedValidators);
        READWRITE(selectionSeed);
        READWRITE(failureReason);
    }
};

/**
 * Gas Discount Consensus Result
 * 
 * Result of gas discount consensus validation.
 */
struct GasDiscountConsensusResult {
    bool isConsensus;                   // Whether all nodes agree
    uint64_t calculatedDiscount;        // Calculated discount
    uint8_t reputation;                 // Reputation used
    std::string failureReason;          // Reason for disagreement (if any)
    
    GasDiscountConsensusResult() : isConsensus(true), calculatedDiscount(0), reputation(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isConsensus);
        READWRITE(calculatedDiscount);
        READWRITE(reputation);
        READWRITE(failureReason);
    }
};

/**
 * Free Gas Eligibility Result
 * 
 * Result of free gas eligibility consensus validation.
 */
struct FreeGasEligibilityResult {
    bool isConsensus;                   // Whether all nodes agree
    bool isEligible;                    // Whether address is eligible
    uint8_t reputation;                 // Reputation used
    uint64_t allowance;                 // Free gas allowance
    std::string failureReason;          // Reason for disagreement (if any)
    
    FreeGasEligibilityResult() : isConsensus(true), isEligible(false), reputation(0), allowance(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isConsensus);
        READWRITE(isEligible);
        READWRITE(reputation);
        READWRITE(allowance);
        READWRITE(failureReason);
    }
};

/**
 * Trust Graph Sync State
 * 
 * State of trust graph synchronization.
 */
struct TrustGraphSyncState {
    uint256 stateHash;                  // Hash of trust graph state
    uint64_t lastSyncBlock;             // Last synchronized block
    uint64_t edgeCount;                 // Number of trust edges
    uint64_t nodeCount;                 // Number of nodes
    bool isSynchronized;                // Whether graph is synchronized
    
    TrustGraphSyncState() : lastSyncBlock(0), edgeCount(0), nodeCount(0), isSynchronized(false) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(stateHash);
        READWRITE(lastSyncBlock);
        READWRITE(edgeCount);
        READWRITE(nodeCount);
        READWRITE(isSynchronized);
    }
};

/**
 * Cross-Chain Attestation Validation Result
 * 
 * Result of cross-chain attestation consensus validation.
 */
struct CrossChainAttestationResult {
    bool isValid;                       // Whether attestation is valid
    bool isConsensusSafe;               // Whether attestation is consensus-safe
    uint16_t sourceChainId;             // Source chain ID
    uint8_t trustScore;                 // Trust score from attestation
    std::string failureReason;          // Reason for failure (if any)
    
    CrossChainAttestationResult() : isValid(false), isConsensusSafe(false), 
                                    sourceChainId(0), trustScore(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isValid);
        READWRITE(isConsensusSafe);
        READWRITE(sourceChainId);
        READWRITE(trustScore);
        READWRITE(failureReason);
    }
};

/**
 * Consensus Safety Validator
 * 
 * Validates consensus safety for HAT v2 and reputation-based features.
 * Ensures deterministic execution across all network nodes.
 * 
 * Requirements: 10.1, 10.2, 6.1, 22.4
 */
class ConsensusSafetyValidator {
public:
    ConsensusSafetyValidator();
    explicit ConsensusSafetyValidator(CVMDatabase* db, SecureHAT* hat, TrustGraph* graph);
    ~ConsensusSafetyValidator();
    
    // ========== Task 23.1: Deterministic Execution Validation ==========
    
    /**
     * Verify HAT v2 score calculation is deterministic
     * 
     * Ensures that given the same inputs, all nodes calculate
     * identical HAT v2 scores.
     * 
     * @param address Address to calculate score for
     * @param viewer Viewer address (for WoT personalization)
     * @param blockHeight Block height for deterministic state
     * @return Deterministic execution result
     */
    DeterministicExecutionResult ValidateHATv2Determinism(
        const uint160& address,
        const uint160& viewer,
        int blockHeight
    );
    
    /**
     * Verify validator selection produces identical results
     * 
     * Ensures that given the same transaction hash and block height,
     * all nodes select the same validators.
     * 
     * @param txHash Transaction hash
     * @param blockHeight Block height
     * @return Validator selection result
     */
    ValidatorSelectionResult ValidateValidatorSelection(
        const uint256& txHash,
        int blockHeight
    );
    
    /**
     * Calculate deterministic HAT v2 score hash
     * 
     * Calculates a hash of the HAT v2 score calculation that can
     * be compared across nodes for consensus verification.
     * 
     * @param address Address to calculate score for
     * @param viewer Viewer address
     * @param blockHeight Block height
     * @return Hash of the calculation
     */
    uint256 CalculateHATv2Hash(
        const uint160& address,
        const uint160& viewer,
        int blockHeight
    );
    
    /**
     * Calculate deterministic validator selection seed
     * 
     * @param txHash Transaction hash
     * @param blockHeight Block height
     * @return Deterministic seed
     */
    uint256 CalculateValidatorSelectionSeed(
        const uint256& txHash,
        int blockHeight
    );
    
    // ========== Task 23.2: Reputation-Based Feature Consensus ==========
    
    /**
     * Validate all nodes agree on gas discounts
     * 
     * Ensures that given the same reputation score, all nodes
     * calculate identical gas discounts.
     * 
     * @param address Address to check
     * @param reputation Reputation score
     * @param baseGas Base gas cost
     * @return Gas discount consensus result
     */
    GasDiscountConsensusResult ValidateGasDiscountConsensus(
        const uint160& address,
        uint8_t reputation,
        uint64_t baseGas
    );
    
    /**
     * Ensure free gas eligibility is consensus-safe
     * 
     * Validates that free gas eligibility determination is
     * deterministic and consistent across all nodes.
     * 
     * @param address Address to check
     * @param reputation Reputation score
     * @return Free gas eligibility result
     */
    FreeGasEligibilityResult ValidateFreeGasEligibility(
        const uint160& address,
        uint8_t reputation
    );
    
    /**
     * Calculate deterministic gas discount
     * 
     * @param reputation Reputation score
     * @param baseGas Base gas cost
     * @return Discounted gas cost
     */
    uint64_t CalculateDeterministicGasDiscount(
        uint8_t reputation,
        uint64_t baseGas
    );
    
    /**
     * Calculate deterministic free gas allowance
     * 
     * @param reputation Reputation score
     * @return Free gas allowance
     */
    uint64_t CalculateDeterministicFreeGasAllowance(uint8_t reputation);
    
    // ========== Task 23.3: Trust Score Synchronization ==========
    
    /**
     * Get current trust graph state
     * 
     * @return Trust graph sync state
     */
    TrustGraphSyncState GetTrustGraphState();
    
    /**
     * Calculate trust graph state hash
     * 
     * Calculates a deterministic hash of the trust graph state
     * that can be compared across nodes.
     * 
     * @return State hash
     */
    uint256 CalculateTrustGraphStateHash();
    
    /**
     * Verify trust graph state matches expected
     * 
     * @param expectedHash Expected state hash
     * @return true if state matches
     */
    bool VerifyTrustGraphState(const uint256& expectedHash);
    
    /**
     * Synchronize trust graph state with peer
     * 
     * @param peerState Peer's trust graph state
     * @return true if synchronization successful
     */
    bool SynchronizeTrustGraphState(const TrustGraphSyncState& peerState);
    
    /**
     * Get trust graph delta since block
     * 
     * Returns changes to trust graph since specified block.
     * 
     * @param sinceBlock Block height to get delta from
     * @return Vector of trust edge changes
     */
    std::vector<TrustEdge> GetTrustGraphDelta(int sinceBlock);
    
    /**
     * Apply trust graph delta
     * 
     * Applies trust edge changes to local trust graph.
     * 
     * @param delta Vector of trust edge changes
     * @return true if delta applied successfully
     */
    bool ApplyTrustGraphDelta(const std::vector<TrustEdge>& delta);
    
    // ========== Task 23.4: Cross-Chain Attestation Validation ==========
    
    /**
     * Verify cross-chain trust attestation is consensus-safe
     * 
     * Validates that cross-chain attestations can be verified
     * deterministically by all nodes.
     * 
     * @param attestation Trust attestation to verify
     * @return Cross-chain attestation result
     */
    CrossChainAttestationResult ValidateCrossChainAttestation(
        const TrustAttestation& attestation
    );
    
    /**
     * Validate cryptographic proof for cross-chain attestation
     * 
     * @param proof Trust state proof
     * @param sourceChainId Source chain ID
     * @return true if proof is valid
     */
    bool ValidateCryptographicProof(
        const TrustStateProof& proof,
        uint16_t sourceChainId
    );
    
    /**
     * Calculate deterministic attestation hash
     * 
     * @param attestation Trust attestation
     * @return Deterministic hash
     */
    uint256 CalculateAttestationHash(const TrustAttestation& attestation);
    
    /**
     * Verify attestation signature
     * 
     * @param attestation Trust attestation
     * @return true if signature is valid
     */
    bool VerifyAttestationSignature(const TrustAttestation& attestation);
    
    // ========== Utility Methods ==========
    
    /**
     * Run full consensus safety validation
     * 
     * Runs all consensus safety checks and returns overall result.
     * 
     * @param address Address to validate
     * @param blockHeight Block height
     * @return true if all checks pass
     */
    bool RunFullValidation(const uint160& address, int blockHeight);
    
    /**
     * Get validation report
     * 
     * Returns detailed report of all validation results.
     * 
     * @param address Address to validate
     * @param blockHeight Block height
     * @return Validation report as string
     */
    std::string GetValidationReport(const uint160& address, int blockHeight);
    
private:
    CVMDatabase* database;
    SecureHAT* secureHAT;
    TrustGraph* trustGraph;
    
    // Cached state for performance
    mutable std::mutex stateMutex;
    TrustGraphSyncState cachedState;
    uint64_t cacheBlockHeight;
    
    // Constants for deterministic calculations
    static constexpr uint8_t FREE_GAS_THRESHOLD = 80;
    static constexpr uint64_t BASE_FREE_GAS_ALLOWANCE = 100000;
    static constexpr double GAS_DISCOUNT_FACTOR = 0.005; // 0.5% per reputation point
    
    // Helper methods
    uint256 HashBehaviorComponent(const BehaviorMetrics& metrics);
    uint256 HashWoTComponent(const uint160& address, const uint160& viewer);
    uint256 HashEconomicComponent(const StakeInfo& stake);
    uint256 HashTemporalComponent(const TemporalMetrics& temporal);
    
    /**
     * Get deterministic block hash for given height
     */
    uint256 GetBlockHashAtHeight(int blockHeight);
    
    /**
     * Validate component calculation is deterministic
     */
    bool ValidateComponentDeterminism(
        const uint160& address,
        const uint160& viewer,
        int blockHeight
    );
};

// Global consensus safety validator instance
extern std::unique_ptr<ConsensusSafetyValidator> g_consensusSafetyValidator;

/**
 * Initialize the global consensus safety validator
 */
void InitializeConsensusSafetyValidator(CVMDatabase* db, SecureHAT* hat, TrustGraph* graph);

/**
 * Shutdown the global consensus safety validator
 */
void ShutdownConsensusSafetyValidator();

} // namespace CVM

#endif // CASCOIN_CVM_CONSENSUS_SAFETY_H
