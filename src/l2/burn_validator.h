// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BURN_VALIDATOR_H
#define CASCOIN_L2_BURN_VALIDATOR_H

/**
 * @file burn_validator.h
 * @brief Burn Transaction Validator for L2 Burn-and-Mint Token Model
 * 
 * This file implements the validation of burn transactions on L1 before
 * L2 tokens can be minted. The validator ensures:
 * - Correct OP_RETURN format
 * - Sufficient L1 confirmations (minimum 6)
 * - Matching chain ID
 * - No double-processing of burns
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4
 */

#include <l2/burn_parser.h>
#include <uint256.h>
#include <amount.h>
#include <sync.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Minimum number of L1 confirmations required before processing a burn */
static constexpr int REQUIRED_CONFIRMATIONS = 6;

// ============================================================================
// BurnValidationResult Structure
// ============================================================================

/**
 * @brief Result of burn transaction validation
 * 
 * Contains the validation outcome along with parsed burn data
 * and additional context information.
 * 
 * Requirements: 2.1, 2.5, 2.6
 */
struct BurnValidationResult {
    /** Whether the burn transaction is valid */
    bool isValid;
    
    /** Error message if validation failed */
    std::string errorMessage;
    
    /** Parsed burn data (valid only if isValid is true) */
    BurnData burnData;
    
    /** Number of L1 confirmations */
    int confirmations;
    
    /** L1 block hash containing the burn transaction */
    uint256 blockHash;
    
    /** L1 block number containing the burn transaction */
    uint64_t blockNumber;
    
    /** Default constructor - creates invalid result */
    BurnValidationResult() 
        : isValid(false)
        , errorMessage("")
        , confirmations(0)
        , blockNumber(0) {}
    
    /** Create a successful validation result */
    static BurnValidationResult Success(
        const BurnData& data,
        int confs,
        const uint256& blkHash,
        uint64_t blkNum)
    {
        BurnValidationResult result;
        result.isValid = true;
        result.burnData = data;
        result.confirmations = confs;
        result.blockHash = blkHash;
        result.blockNumber = blkNum;
        return result;
    }
    
    /** Create a failed validation result */
    static BurnValidationResult Failure(const std::string& error)
    {
        BurnValidationResult result;
        result.isValid = false;
        result.errorMessage = error;
        return result;
    }
};

// ============================================================================
// BurnValidator Class
// ============================================================================

/**
 * @brief Validator for L1 burn transactions
 * 
 * This class validates burn transactions on L1 before allowing L2 token
 * minting. It checks:
 * - OP_RETURN format validity
 * - Sufficient confirmations (minimum 6)
 * - Chain ID matching
 * - Double-processing prevention
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4
 */
class BurnValidator {
public:
    /**
     * @brief Callback type for fetching L1 transaction
     * @param txHash The transaction hash to fetch
     * @return The transaction if found, nullopt otherwise
     */
    using TxFetcher = std::function<std::optional<CTransaction>(const uint256& txHash)>;
    
    /**
     * @brief Callback type for getting confirmation count
     * @param txHash The transaction hash
     * @return Number of confirmations, -1 if not found
     */
    using ConfirmationGetter = std::function<int(const uint256& txHash)>;
    
    /**
     * @brief Callback type for getting block info
     * @param txHash The transaction hash
     * @return Pair of (blockHash, blockNumber), nullopt if not in block
     */
    using BlockInfoGetter = std::function<std::optional<std::pair<uint256, uint64_t>>(const uint256& txHash)>;
    
    /**
     * @brief Callback type for checking if burn is already processed
     * @param l1TxHash The L1 transaction hash
     * @return true if already processed
     */
    using ProcessedChecker = std::function<bool(const uint256& l1TxHash)>;

    /**
     * @brief Construct a BurnValidator
     * @param chainId The L2 chain ID to validate against
     */
    explicit BurnValidator(uint32_t chainId);
    
    /**
     * @brief Construct a BurnValidator with custom callbacks
     * @param chainId The L2 chain ID
     * @param txFetcher Callback to fetch L1 transactions
     * @param confGetter Callback to get confirmation count
     * @param blockInfoGetter Callback to get block info
     * @param processedChecker Callback to check if already processed
     */
    BurnValidator(
        uint32_t chainId,
        TxFetcher txFetcher,
        ConfirmationGetter confGetter,
        BlockInfoGetter blockInfoGetter,
        ProcessedChecker processedChecker);
    
    /**
     * @brief Validate a burn transaction
     * @param l1TxHash The L1 transaction hash to validate
     * @return Validation result with burn data if valid
     * 
     * Performs all validation checks:
     * - Fetches transaction from L1
     * - Validates OP_RETURN format
     * - Checks confirmation count (>= 6)
     * - Verifies chain ID matches
     * - Ensures not already processed
     * 
     * Requirements: 2.1, 2.2, 2.3, 2.4
     */
    BurnValidationResult ValidateBurn(const uint256& l1TxHash);
    
    /**
     * @brief Check if burn has sufficient confirmations
     * @param l1TxHash The L1 transaction hash
     * @return true if confirmations >= REQUIRED_CONFIRMATIONS
     * 
     * Requirements: 2.2
     */
    bool HasSufficientConfirmations(const uint256& l1TxHash) const;
    
    /**
     * @brief Check if burn data matches our chain ID
     * @param data The parsed burn data
     * @return true if chain ID matches
     * 
     * Requirements: 2.3
     */
    bool MatchesChainId(const BurnData& data) const;
    
    /**
     * @brief Check if burn was already processed
     * @param l1TxHash The L1 transaction hash
     * @return true if already processed
     * 
     * Requirements: 2.4
     */
    bool IsAlreadyProcessed(const uint256& l1TxHash) const;
    
    /**
     * @brief Get the chain ID this validator is configured for
     * @return The L2 chain ID
     */
    uint32_t GetChainId() const { return chainId_; }
    
    /**
     * @brief Get the required confirmation count
     * @return Number of required confirmations
     */
    static int GetRequiredConfirmations() { return REQUIRED_CONFIRMATIONS; }
    
    /**
     * @brief Set the transaction fetcher callback
     * @param fetcher The callback function
     */
    void SetTxFetcher(TxFetcher fetcher) { txFetcher_ = std::move(fetcher); }
    
    /**
     * @brief Set the confirmation getter callback
     * @param getter The callback function
     */
    void SetConfirmationGetter(ConfirmationGetter getter) { confGetter_ = std::move(getter); }
    
    /**
     * @brief Set the block info getter callback
     * @param getter The callback function
     */
    void SetBlockInfoGetter(BlockInfoGetter getter) { blockInfoGetter_ = std::move(getter); }
    
    /**
     * @brief Set the processed checker callback
     * @param checker The callback function
     */
    void SetProcessedChecker(ProcessedChecker checker) { processedChecker_ = std::move(checker); }
    
    /**
     * @brief Mark a burn as processed (for testing)
     * @param l1TxHash The L1 transaction hash
     * 
     * Note: In production, this is managed by the BurnRegistry
     */
    void MarkAsProcessed(const uint256& l1TxHash);
    
    /**
     * @brief Clear processed burns (for testing)
     */
    void ClearProcessed();
    
    /**
     * @brief Get the number of processed burns (for testing)
     * @return Count of processed burns
     */
    size_t GetProcessedCount() const;

private:
    /** L2 chain ID to validate against */
    uint32_t chainId_;
    
    /** Callback to fetch L1 transactions */
    TxFetcher txFetcher_;
    
    /** Callback to get confirmation count */
    ConfirmationGetter confGetter_;
    
    /** Callback to get block info */
    BlockInfoGetter blockInfoGetter_;
    
    /** Callback to check if already processed */
    ProcessedChecker processedChecker_;
    
    /** Set of processed burn transaction hashes (for internal tracking) */
    std::set<uint256> processedBurns_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_validator_;
    
    /**
     * @brief Internal helper to get confirmation count
     * @param txHash The transaction hash
     * @return Number of confirmations
     */
    int GetConfirmationCount(const uint256& txHash) const;
    
    /**
     * @brief Internal helper to fetch transaction
     * @param txHash The transaction hash
     * @return The transaction if found
     */
    std::optional<CTransaction> FetchL1Transaction(const uint256& txHash) const;
    
    /**
     * @brief Internal helper to get block info
     * @param txHash The transaction hash
     * @return Block hash and number if found
     */
    std::optional<std::pair<uint256, uint64_t>> GetBlockInfo(const uint256& txHash) const;
};

} // namespace l2

#endif // CASCOIN_L2_BURN_VALIDATOR_H
