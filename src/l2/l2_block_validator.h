// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BLOCK_VALIDATOR_H
#define CASCOIN_L2_BLOCK_VALIDATOR_H

/**
 * @file l2_block_validator.h
 * @brief L2 Block Validation for Cascoin Layer 2
 * 
 * This file implements comprehensive validation for L2 blocks including:
 * - Block header validation
 * - Transaction validation
 * - Signature verification
 * - State transition validation
 * 
 * Requirements: 3.1, 2a.5
 */

#include <l2/l2_block.h>
#include <l2/l2_transaction.h>
#include <l2/state_manager.h>
#include <uint256.h>
#include <pubkey.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace l2 {

/**
 * @brief Validation error codes
 */
enum class ValidationError {
    VALID = 0,
    
    // Header errors
    INVALID_BLOCK_NUMBER,
    INVALID_PARENT_HASH,
    INVALID_TIMESTAMP,
    TIMESTAMP_TOO_FAR_FUTURE,
    TIMESTAMP_NOT_MONOTONIC,
    INVALID_GAS_LIMIT,
    GAS_USED_EXCEEDS_LIMIT,
    INVALID_SEQUENCER,
    INVALID_CHAIN_ID,
    INVALID_EXTRA_DATA,
    
    // Transaction errors
    TOO_MANY_TRANSACTIONS,
    INVALID_TRANSACTION,
    INVALID_TRANSACTIONS_ROOT,
    DUPLICATE_TRANSACTION,
    INVALID_TX_NONCE,
    INSUFFICIENT_BALANCE,
    INVALID_TX_SIGNATURE,
    
    // Signature errors
    TOO_MANY_SIGNATURES,
    INVALID_SIGNATURE,
    DUPLICATE_SIGNATURE,
    UNKNOWN_SEQUENCER,
    INSUFFICIENT_SIGNATURES,
    
    // State errors
    INVALID_STATE_ROOT,
    STATE_TRANSITION_FAILED,
    
    // Consensus errors
    NOT_LEADER,
    CONSENSUS_NOT_REACHED,
    
    // Other errors
    BLOCK_TOO_LARGE,
    UNKNOWN_ERROR
};

/**
 * @brief Convert ValidationError to string
 */
std::string ValidationErrorToString(ValidationError error);

/**
 * @brief Stream output operator for ValidationError (needed for Boost.Test)
 */
inline std::ostream& operator<<(std::ostream& os, ValidationError error) {
    return os << ValidationErrorToString(error);
}

/**
 * @brief Validation result with error details
 */
struct ValidationResult {
    /** Whether validation passed */
    bool isValid = false;
    
    /** Error code if validation failed */
    ValidationError error = ValidationError::VALID;
    
    /** Detailed error message */
    std::string errorMessage;
    
    /** Index of problematic item (transaction, signature, etc.) */
    int errorIndex = -1;

    ValidationResult() = default;
    
    static ValidationResult Valid() {
        ValidationResult result;
        result.isValid = true;
        result.error = ValidationError::VALID;
        return result;
    }
    
    static ValidationResult Invalid(ValidationError err, const std::string& msg = "", int index = -1) {
        ValidationResult result;
        result.isValid = false;
        result.error = err;
        result.errorMessage = msg.empty() ? ValidationErrorToString(err) : msg;
        result.errorIndex = index;
        return result;
    }
    
    explicit operator bool() const { return isValid; }
};

/**
 * @brief Validation context with chain state
 */
struct ValidationContext {
    /** Previous block (for parent hash and timestamp validation) */
    std::optional<L2Block> previousBlock;
    
    /** Current L1 block number */
    uint64_t currentL1Block = 0;
    
    /** Current L1 block hash */
    uint256 currentL1Hash;
    
    /** Current timestamp */
    uint64_t currentTimestamp = 0;
    
    /** Expected sequencer (leader) for this slot */
    std::optional<uint160> expectedSequencer;
    
    /** Map of sequencer addresses to public keys */
    std::map<uint160, CPubKey> sequencerPubkeys;
    
    /** Total sequencer weight for consensus calculation */
    uint64_t totalSequencerWeight = 0;
    
    /** Map of sequencer addresses to weights */
    std::map<uint160, uint64_t> sequencerWeights;
    
    /** Consensus threshold (default 2/3) */
    double consensusThreshold = 0.67;
    
    /** State manager for state validation */
    L2StateManager* stateManager = nullptr;
    
    /** Whether to validate state transitions */
    bool validateState = true;
    
    /** Whether to validate signatures */
    bool validateSignatures = true;
    
    /** Whether to require consensus (2/3+ signatures) */
    bool requireConsensus = true;
};

/**
 * @brief L2 Block Validator
 * 
 * Provides comprehensive validation for L2 blocks including header,
 * transactions, signatures, and state transitions.
 * 
 * Requirements: 3.1, 2a.5
 */
class L2BlockValidator {
public:
    /**
     * @brief Validate a complete L2 block
     * 
     * Performs all validation checks:
     * - Header validation
     * - Transaction validation
     * - Signature verification
     * - State transition validation (if context provided)
     * 
     * @param block The block to validate
     * @param context Validation context with chain state
     * @return Validation result
     */
    static ValidationResult ValidateBlock(
        const L2Block& block,
        const ValidationContext& context);

    /**
     * @brief Validate block header only
     * 
     * Checks:
     * - Block number consistency
     * - Parent hash validity
     * - Timestamp validity
     * - Gas limit/used validity
     * - Sequencer validity
     * - Chain ID validity
     * 
     * @param header The header to validate
     * @param context Validation context
     * @return Validation result
     */
    static ValidationResult ValidateHeader(
        const L2BlockHeader& header,
        const ValidationContext& context);

    /**
     * @brief Validate all transactions in a block
     * 
     * Checks:
     * - Transaction count limits
     * - Individual transaction validity
     * - Transactions root matches
     * - No duplicate transactions
     * - Nonce ordering
     * - Signature validity
     * 
     * @param block The block containing transactions
     * @param context Validation context
     * @return Validation result
     */
    static ValidationResult ValidateTransactions(
        const L2Block& block,
        const ValidationContext& context);

    /**
     * @brief Validate a single transaction
     * 
     * @param tx The transaction to validate
     * @param context Validation context
     * @return Validation result
     */
    static ValidationResult ValidateTransaction(
        const L2Transaction& tx,
        const ValidationContext& context);

    /**
     * @brief Validate sequencer signatures
     * 
     * Checks:
     * - Signature count limits
     * - Individual signature validity
     * - No duplicate signatures
     * - Signers are known sequencers
     * - Consensus threshold met (if required)
     * 
     * @param block The block with signatures
     * @param context Validation context
     * @return Validation result
     */
    static ValidationResult ValidateSignatures(
        const L2Block& block,
        const ValidationContext& context);

    /**
     * @brief Verify a single sequencer signature
     * 
     * @param sig The signature to verify
     * @param blockHash The block hash that was signed
     * @param pubkey The sequencer's public key
     * @return true if signature is valid
     */
    static bool VerifySignature(
        const SequencerSignature& sig,
        const uint256& blockHash,
        const CPubKey& pubkey);

    /**
     * @brief Validate state transition
     * 
     * Re-executes all transactions and verifies the resulting
     * state root matches the block's state root.
     * 
     * @param block The block to validate
     * @param stateManager State manager for execution
     * @return Validation result
     */
    static ValidationResult ValidateStateTransition(
        const L2Block& block,
        L2StateManager& stateManager);

    /**
     * @brief Check if consensus is reached
     * 
     * Calculates weighted vote percentage and checks against threshold.
     * 
     * @param block The block with signatures
     * @param context Validation context with sequencer weights
     * @return true if consensus threshold is met
     */
    static bool HasConsensus(
        const L2Block& block,
        const ValidationContext& context);

    /**
     * @brief Calculate weighted signature percentage
     * 
     * @param block The block with signatures
     * @param context Validation context with sequencer weights
     * @return Weighted percentage (0.0 - 1.0)
     */
    static double CalculateWeightedSignaturePercent(
        const L2Block& block,
        const ValidationContext& context);

    /**
     * @brief Validate timestamp monotonicity
     * 
     * Ensures block timestamp is greater than parent timestamp.
     * 
     * @param timestamp Block timestamp
     * @param parentTimestamp Parent block timestamp
     * @return true if timestamp is valid
     */
    static bool ValidateTimestampMonotonicity(
        uint64_t timestamp,
        uint64_t parentTimestamp);

    /**
     * @brief Validate timestamp is not too far in future
     * 
     * @param timestamp Block timestamp
     * @param currentTimestamp Current time
     * @return true if timestamp is valid
     */
    static bool ValidateTimestampFuture(
        uint64_t timestamp,
        uint64_t currentTimestamp);

    /**
     * @brief Validate gas limit adjustment
     * 
     * Gas limit can only change by a small percentage per block.
     * 
     * @param gasLimit Block gas limit
     * @param parentGasLimit Parent block gas limit
     * @return true if gas limit change is valid
     */
    static bool ValidateGasLimitAdjustment(
        uint64_t gasLimit,
        uint64_t parentGasLimit);

private:
    /** Maximum gas limit change per block (1/1024 of parent) */
    static constexpr uint64_t GAS_LIMIT_BOUND_DIVISOR = 1024;
    
    /** Minimum gas limit */
    static constexpr uint64_t MIN_GAS_LIMIT = 5000;
};

} // namespace l2

#endif // CASCOIN_L2_BLOCK_VALIDATOR_H
