// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_block_validator.cpp
 * @brief Implementation of L2 Block Validation
 * 
 * Requirements: 3.1, 2a.5
 */

#include <l2/l2_block_validator.h>
#include <tinyformat.h>

#include <algorithm>
#include <set>
#include <chrono>

namespace l2 {

std::string ValidationErrorToString(ValidationError error) {
    switch (error) {
        case ValidationError::VALID:
            return "Valid";
        case ValidationError::INVALID_BLOCK_NUMBER:
            return "Invalid block number";
        case ValidationError::INVALID_PARENT_HASH:
            return "Invalid parent hash";
        case ValidationError::INVALID_TIMESTAMP:
            return "Invalid timestamp";
        case ValidationError::TIMESTAMP_TOO_FAR_FUTURE:
            return "Timestamp too far in future";
        case ValidationError::TIMESTAMP_NOT_MONOTONIC:
            return "Timestamp not monotonically increasing";
        case ValidationError::INVALID_GAS_LIMIT:
            return "Invalid gas limit";
        case ValidationError::GAS_USED_EXCEEDS_LIMIT:
            return "Gas used exceeds gas limit";
        case ValidationError::INVALID_SEQUENCER:
            return "Invalid sequencer";
        case ValidationError::INVALID_CHAIN_ID:
            return "Invalid chain ID";
        case ValidationError::INVALID_EXTRA_DATA:
            return "Invalid extra data";
        case ValidationError::TOO_MANY_TRANSACTIONS:
            return "Too many transactions";
        case ValidationError::INVALID_TRANSACTION:
            return "Invalid transaction";
        case ValidationError::INVALID_TRANSACTIONS_ROOT:
            return "Invalid transactions root";
        case ValidationError::DUPLICATE_TRANSACTION:
            return "Duplicate transaction";
        case ValidationError::INVALID_TX_NONCE:
            return "Invalid transaction nonce";
        case ValidationError::INSUFFICIENT_BALANCE:
            return "Insufficient balance";
        case ValidationError::INVALID_TX_SIGNATURE:
            return "Invalid transaction signature";
        case ValidationError::TOO_MANY_SIGNATURES:
            return "Too many signatures";
        case ValidationError::INVALID_SIGNATURE:
            return "Invalid signature";
        case ValidationError::DUPLICATE_SIGNATURE:
            return "Duplicate signature";
        case ValidationError::UNKNOWN_SEQUENCER:
            return "Unknown sequencer";
        case ValidationError::INSUFFICIENT_SIGNATURES:
            return "Insufficient signatures for consensus";
        case ValidationError::INVALID_STATE_ROOT:
            return "Invalid state root";
        case ValidationError::STATE_TRANSITION_FAILED:
            return "State transition failed";
        case ValidationError::NOT_LEADER:
            return "Block proposer is not the expected leader";
        case ValidationError::CONSENSUS_NOT_REACHED:
            return "Consensus not reached";
        case ValidationError::BLOCK_TOO_LARGE:
            return "Block too large";
        case ValidationError::UNKNOWN_ERROR:
        default:
            return "Unknown error";
    }
}

ValidationResult L2BlockValidator::ValidateBlock(
    const L2Block& block,
    const ValidationContext& context)
{
    // Validate header
    auto headerResult = ValidateHeader(block.header, context);
    if (!headerResult) {
        return headerResult;
    }
    
    // Validate transactions
    auto txResult = ValidateTransactions(block, context);
    if (!txResult) {
        return txResult;
    }
    
    // Validate signatures (if required)
    if (context.validateSignatures) {
        auto sigResult = ValidateSignatures(block, context);
        if (!sigResult) {
            return sigResult;
        }
    }
    
    // Validate state transition (if state manager provided)
    if (context.validateState && context.stateManager != nullptr) {
        auto stateResult = ValidateStateTransition(block, *context.stateManager);
        if (!stateResult) {
            return stateResult;
        }
    }
    
    return ValidationResult::Valid();
}

ValidationResult L2BlockValidator::ValidateHeader(
    const L2BlockHeader& header,
    const ValidationContext& context)
{
    // Genesis block special handling
    if (header.blockNumber == 0) {
        if (!header.parentHash.IsNull()) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_PARENT_HASH,
                "Genesis block must have null parent hash");
        }
        return ValidationResult::Valid();
    }
    
    // Non-genesis validation
    
    // Parent hash must be set
    if (header.parentHash.IsNull()) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_PARENT_HASH,
            "Non-genesis block must have parent hash");
    }
    
    // Validate against previous block if provided
    if (context.previousBlock) {
        const auto& prevHeader = context.previousBlock->header;
        
        // Block number must be sequential
        if (header.blockNumber != prevHeader.blockNumber + 1) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_BLOCK_NUMBER,
                strprintf("Block number %u is not sequential (expected %u)",
                    header.blockNumber, prevHeader.blockNumber + 1));
        }
        
        // Parent hash must match previous block
        if (header.parentHash != context.previousBlock->GetHash()) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_PARENT_HASH,
                "Parent hash does not match previous block");
        }
        
        // Timestamp must be monotonically increasing
        if (!ValidateTimestampMonotonicity(header.timestamp, prevHeader.timestamp)) {
            return ValidationResult::Invalid(
                ValidationError::TIMESTAMP_NOT_MONOTONIC,
                strprintf("Timestamp %u is not greater than parent timestamp %u",
                    header.timestamp, prevHeader.timestamp));
        }
        
        // Gas limit adjustment validation
        if (!ValidateGasLimitAdjustment(header.gasLimit, prevHeader.gasLimit)) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_GAS_LIMIT,
                "Gas limit change exceeds allowed bounds");
        }
    }
    
    // Timestamp not too far in future
    if (context.currentTimestamp > 0) {
        if (!ValidateTimestampFuture(header.timestamp, context.currentTimestamp)) {
            return ValidationResult::Invalid(
                ValidationError::TIMESTAMP_TOO_FAR_FUTURE,
                strprintf("Timestamp %u is more than %u seconds in future",
                    header.timestamp, MAX_FUTURE_TIMESTAMP));
        }
    }
    
    // Gas used cannot exceed gas limit
    if (header.gasUsed > header.gasLimit) {
        return ValidationResult::Invalid(
            ValidationError::GAS_USED_EXCEEDS_LIMIT,
            strprintf("Gas used %u exceeds gas limit %u",
                header.gasUsed, header.gasLimit));
    }
    
    // Gas limit must be reasonable
    if (header.gasLimit < MIN_GAS_LIMIT) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_GAS_LIMIT,
            strprintf("Gas limit %u is below minimum %u",
                header.gasLimit, MIN_GAS_LIMIT));
    }
    
    // Sequencer must be set
    if (header.sequencer.IsNull()) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_SEQUENCER,
            "Sequencer address is not set");
    }
    
    // Validate expected sequencer (leader) if provided
    if (context.expectedSequencer && header.sequencer != *context.expectedSequencer) {
        return ValidationResult::Invalid(
            ValidationError::NOT_LEADER,
            strprintf("Sequencer %s is not the expected leader %s",
                header.sequencer.ToString().substr(0, 16),
                context.expectedSequencer->ToString().substr(0, 16)));
    }
    
    // Extra data size limit
    if (header.extraData.size() > MAX_EXTRA_DATA_SIZE) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_EXTRA_DATA,
            strprintf("Extra data size %u exceeds maximum %u",
                header.extraData.size(), MAX_EXTRA_DATA_SIZE));
    }
    
    return ValidationResult::Valid();
}

ValidationResult L2BlockValidator::ValidateTransactions(
    const L2Block& block,
    const ValidationContext& context)
{
    // Check transaction count limit
    if (block.transactions.size() > MAX_TRANSACTIONS_PER_BLOCK) {
        return ValidationResult::Invalid(
            ValidationError::TOO_MANY_TRANSACTIONS,
            strprintf("Transaction count %u exceeds maximum %u",
                block.transactions.size(), MAX_TRANSACTIONS_PER_BLOCK));
    }
    
    // Track transaction hashes for duplicate detection
    std::set<uint256> txHashes;
    
    // Track nonces per sender for ordering validation
    std::map<uint160, uint64_t> senderNonces;
    
    // Total gas tracking
    uint64_t totalGas = 0;
    
    for (size_t i = 0; i < block.transactions.size(); i++) {
        const auto& tx = block.transactions[i];
        
        // Validate individual transaction
        auto txResult = ValidateTransaction(tx, context);
        if (!txResult) {
            txResult.errorIndex = static_cast<int>(i);
            return txResult;
        }
        
        // Check for duplicate transactions
        uint256 txHash = tx.GetHash();
        if (txHashes.count(txHash) > 0) {
            return ValidationResult::Invalid(
                ValidationError::DUPLICATE_TRANSACTION,
                strprintf("Duplicate transaction at index %u", i),
                static_cast<int>(i));
        }
        txHashes.insert(txHash);
        
        // Validate nonce ordering per sender
        auto it = senderNonces.find(tx.from);
        if (it != senderNonces.end()) {
            if (tx.nonce != it->second + 1) {
                return ValidationResult::Invalid(
                    ValidationError::INVALID_TX_NONCE,
                    strprintf("Transaction nonce %u is not sequential (expected %u)",
                        tx.nonce, it->second + 1),
                    static_cast<int>(i));
            }
            it->second = tx.nonce;
        } else {
            senderNonces[tx.from] = tx.nonce;
        }
        
        // Accumulate gas
        totalGas += tx.gasLimit;
    }
    
    // Total gas cannot exceed block gas limit
    if (totalGas > block.header.gasLimit) {
        return ValidationResult::Invalid(
            ValidationError::GAS_USED_EXCEEDS_LIMIT,
            strprintf("Total transaction gas %u exceeds block gas limit %u",
                totalGas, block.header.gasLimit));
    }
    
    // Verify transactions root
    uint256 computedRoot = block.ComputeTransactionsRoot();
    if (computedRoot != block.header.transactionsRoot) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_TRANSACTIONS_ROOT,
            "Computed transactions root does not match header");
    }
    
    return ValidationResult::Valid();
}

ValidationResult L2BlockValidator::ValidateTransaction(
    const L2Transaction& tx,
    const ValidationContext& context)
{
    // Basic structure validation
    if (!tx.ValidateStructure()) {
        return ValidationResult::Invalid(
            ValidationError::INVALID_TRANSACTION,
            "Transaction structure validation failed");
    }
    
    // Signature validation (skip for deposit transactions)
    if (tx.type != L2TxType::DEPOSIT) {
        if (!tx.VerifySignature()) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_TX_SIGNATURE,
                "Transaction signature verification failed");
        }
    }
    
    return ValidationResult::Valid();
}

ValidationResult L2BlockValidator::ValidateSignatures(
    const L2Block& block,
    const ValidationContext& context)
{
    // Check signature count limit
    if (block.signatures.size() > MAX_SIGNATURES_PER_BLOCK) {
        return ValidationResult::Invalid(
            ValidationError::TOO_MANY_SIGNATURES,
            strprintf("Signature count %u exceeds maximum %u",
                block.signatures.size(), MAX_SIGNATURES_PER_BLOCK));
    }
    
    // Track signers for duplicate detection
    std::set<uint160> signers;
    
    uint256 blockHash = block.GetHash();
    
    for (size_t i = 0; i < block.signatures.size(); i++) {
        const auto& sig = block.signatures[i];
        
        // Check for duplicate signatures
        if (signers.count(sig.sequencerAddress) > 0) {
            return ValidationResult::Invalid(
                ValidationError::DUPLICATE_SIGNATURE,
                strprintf("Duplicate signature from sequencer %s",
                    sig.sequencerAddress.ToString().substr(0, 16)),
                static_cast<int>(i));
        }
        signers.insert(sig.sequencerAddress);
        
        // Find sequencer's public key
        auto it = context.sequencerPubkeys.find(sig.sequencerAddress);
        if (it == context.sequencerPubkeys.end()) {
            return ValidationResult::Invalid(
                ValidationError::UNKNOWN_SEQUENCER,
                strprintf("Unknown sequencer %s",
                    sig.sequencerAddress.ToString().substr(0, 16)),
                static_cast<int>(i));
        }
        
        // Verify signature
        if (!VerifySignature(sig, blockHash, it->second)) {
            return ValidationResult::Invalid(
                ValidationError::INVALID_SIGNATURE,
                strprintf("Invalid signature from sequencer %s",
                    sig.sequencerAddress.ToString().substr(0, 16)),
                static_cast<int>(i));
        }
    }
    
    // Check consensus threshold (if required)
    if (context.requireConsensus && !HasConsensus(block, context)) {
        double percent = CalculateWeightedSignaturePercent(block, context);
        return ValidationResult::Invalid(
            ValidationError::INSUFFICIENT_SIGNATURES,
            strprintf("Consensus not reached: %.1f%% < %.1f%% required",
                percent * 100, context.consensusThreshold * 100));
    }
    
    return ValidationResult::Valid();
}

bool L2BlockValidator::VerifySignature(
    const SequencerSignature& sig,
    const uint256& blockHash,
    const CPubKey& pubkey)
{
    return sig.Verify(blockHash, pubkey);
}

ValidationResult L2BlockValidator::ValidateStateTransition(
    const L2Block& block,
    L2StateManager& stateManager)
{
    // Save current state root for potential rollback
    uint256 originalStateRoot = stateManager.GetStateRoot();
    
    // Apply all transactions
    for (size_t i = 0; i < block.transactions.size(); i++) {
        const auto& tx = block.transactions[i];
        
        // Convert L2Transaction to CTransaction for state manager
        // Note: This is a simplified version - full implementation would
        // need proper conversion
        CTransaction ctx;  // Empty for now
        
        auto result = stateManager.ApplyTransaction(ctx, block.header.blockNumber);
        if (!result.success) {
            // Rollback state
            stateManager.RevertToStateRoot(originalStateRoot);
            
            return ValidationResult::Invalid(
                ValidationError::STATE_TRANSITION_FAILED,
                strprintf("Transaction %u failed: %s", i, result.error),
                static_cast<int>(i));
        }
    }
    
    // Verify final state root matches
    uint256 computedStateRoot = stateManager.GetStateRoot();
    if (computedStateRoot != block.header.stateRoot) {
        // Rollback state
        stateManager.RevertToStateRoot(originalStateRoot);
        
        return ValidationResult::Invalid(
            ValidationError::INVALID_STATE_ROOT,
            strprintf("Computed state root %s does not match header %s",
                computedStateRoot.ToString().substr(0, 16),
                block.header.stateRoot.ToString().substr(0, 16)));
    }
    
    return ValidationResult::Valid();
}

bool L2BlockValidator::HasConsensus(
    const L2Block& block,
    const ValidationContext& context)
{
    double percent = CalculateWeightedSignaturePercent(block, context);
    return percent >= context.consensusThreshold;
}

double L2BlockValidator::CalculateWeightedSignaturePercent(
    const L2Block& block,
    const ValidationContext& context)
{
    if (context.totalSequencerWeight == 0) {
        // If no weights provided, use simple count
        if (context.sequencerPubkeys.empty()) {
            return 0.0;
        }
        return static_cast<double>(block.signatures.size()) / 
               static_cast<double>(context.sequencerPubkeys.size());
    }
    
    uint64_t signedWeight = 0;
    for (const auto& sig : block.signatures) {
        auto it = context.sequencerWeights.find(sig.sequencerAddress);
        if (it != context.sequencerWeights.end()) {
            signedWeight += it->second;
        }
    }
    
    return static_cast<double>(signedWeight) / 
           static_cast<double>(context.totalSequencerWeight);
}

bool L2BlockValidator::ValidateTimestampMonotonicity(
    uint64_t timestamp,
    uint64_t parentTimestamp)
{
    return timestamp > parentTimestamp;
}

bool L2BlockValidator::ValidateTimestampFuture(
    uint64_t timestamp,
    uint64_t currentTimestamp)
{
    return timestamp <= currentTimestamp + MAX_FUTURE_TIMESTAMP;
}

bool L2BlockValidator::ValidateGasLimitAdjustment(
    uint64_t gasLimit,
    uint64_t parentGasLimit)
{
    // Gas limit can change by at most 1/1024 of parent
    uint64_t maxChange = parentGasLimit / GAS_LIMIT_BOUND_DIVISOR;
    
    if (gasLimit > parentGasLimit) {
        return (gasLimit - parentGasLimit) <= maxChange;
    } else {
        return (parentGasLimit - gasLimit) <= maxChange;
    }
}

} // namespace l2
