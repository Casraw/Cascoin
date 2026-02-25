// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_TOKEN_MANAGER_H
#define CASCOIN_L2_TOKEN_MANAGER_H

/**
 * @file l2_token_manager.h
 * @brief L2 Token Manager for managing L2 chain tokens
 * 
 * This file implements the L2TokenManager class that manages the complete
 * token system for an L2 chain. It handles token configuration, supply
 * tracking, genesis distribution, sequencer rewards, and transfers.
 * 
 * Requirements: 1.5, 1.6, 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.2
 */

#include <l2/l2_token.h>
#include <l2/state_manager.h>
#include <l2/account_state.h>
#include <sync.h>

#include <map>
#include <set>
#include <vector>
#include <string>

namespace l2 {

/**
 * @brief Result of a transfer operation
 */
struct TransferResult {
    /** Whether the transfer succeeded */
    bool success;
    
    /** Error message if failed */
    std::string error;
    
    /** New state root after transfer */
    uint256 newStateRoot;
    
    /** Transaction hash (if recorded) */
    uint256 txHash;

    TransferResult() : success(false) {}
    
    static TransferResult Success(const uint256& stateRoot, const uint256& hash = uint256()) {
        TransferResult result;
        result.success = true;
        result.newStateRoot = stateRoot;
        result.txHash = hash;
        return result;
    }
    
    static TransferResult Failure(const std::string& err) {
        TransferResult result;
        result.success = false;
        result.error = err;
        return result;
    }
};

/**
 * @brief L2 Token Manager
 * 
 * Manages the complete token system for an L2 chain:
 * - Token configuration (name, symbol, rewards, fees)
 * - Supply tracking with invariant verification
 * - Genesis distribution
 * - Sequencer reward minting
 * - Token transfers
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 1.5, 1.6, 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.2
 */
class L2TokenManager {
public:
    /**
     * @brief Construct a new L2 Token Manager
     * @param chainId The L2 chain ID
     * @param config The token configuration
     * 
     * Requirement 1.5: Store token config in genesis configuration
     */
    L2TokenManager(uint64_t chainId, const L2TokenConfig& config);

    /**
     * @brief Get the token configuration
     * @return Reference to the token configuration
     * 
     * Requirement 8.2: Provide RPC to query token name and symbol
     */
    const L2TokenConfig& GetConfig() const;

    /**
     * @brief Get the current token supply
     * @return Reference to the token supply tracking
     * 
     * Requirement 8.1: Provide RPC to query total supply
     */
    const L2TokenSupply& GetSupply() const;

    /**
     * @brief Get the token name
     * @return Token name string
     * 
     * Requirement 1.6: Display correct token name in RPC responses
     */
    std::string GetTokenName() const;

    /**
     * @brief Get the token symbol
     * @return Token symbol string
     * 
     * Requirement 1.6: Display correct token symbol in RPC responses
     */
    std::string GetTokenSymbol() const;

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    // =========================================================================
    // Genesis Distribution (Requirements 4.1, 4.2, 4.3, 4.4, 4.5)
    // =========================================================================

    /**
     * @brief Apply genesis distribution to initial state
     * @param stateManager The state manager to update
     * @return true if distribution succeeded
     * 
     * Requirement 4.1: Deployer MAY specify genesis distribution
     * Requirement 4.2: Genesis distribution recorded in genesis block
     * Requirement 4.3: Limited to maxGenesisSupply
     */
    bool ApplyGenesisDistribution(L2StateManager& stateManager);

    /**
     * @brief Get the genesis distribution
     * @return Vector of (address, amount) pairs
     * 
     * Requirement 4.4: Genesis distribution transparent and queryable
     */
    std::vector<std::pair<uint160, CAmount>> GetGenesisDistribution() const;

    /**
     * @brief Set the genesis distribution (before applying)
     * @param distribution Map of address to amount
     * @return true if distribution is valid (within limits)
     * 
     * Requirement 4.3: Limited to maxGenesisSupply
     */
    bool SetGenesisDistribution(const std::map<uint160, CAmount>& distribution);

    /**
     * @brief Check if genesis distribution has been applied
     * @return true if already applied
     */
    bool IsGenesisApplied() const;

    // =========================================================================
    // Minting (Sequencer Rewards) - DEPRECATED
    // Requirements 6.1, 6.2: Sequencer rewards now come from FeeDistributor only
    // The old ProcessBlockReward minting logic has been removed.
    // See src/l2/fee_distributor.h for the new fee-based reward system.
    // =========================================================================

    /**
     * @brief Process block reward for sequencer
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * This method is DEPRECATED. Sequencer rewards now come exclusively from
     * transaction fees via FeeDistributor. No new tokens are minted as block rewards.
     * 
     * This method now returns false and logs a deprecation warning.
     * Use FeeDistributor::DistributeBlockFees() instead.
     * 
     * @param sequencer Sequencer address (must be block producer)
     * @param l2BlockNumber L2 block number
     * @param l2BlockHash L2 block hash
     * @param l1TxHash L1 fee transaction hash (IGNORED)
     * @param l1BlockNumber L1 block number containing fee tx (IGNORED)
     * @param stateManager State manager to update
     * @return false - This method is deprecated and always fails
     * 
     * Requirements 6.1, 6.2: Sequencer rewards from fees only, no minting
     */
    [[deprecated("Use FeeDistributor::DistributeBlockFees() instead - no new tokens are minted as block rewards")]]
    bool ProcessBlockReward(
        const uint160& sequencer,
        uint64_t l2BlockNumber,
        const uint256& l2BlockHash,
        const uint256& l1TxHash,
        uint64_t l1BlockNumber,
        L2StateManager& stateManager);

    /**
     * @brief Verify L1 fee transaction exists and has correct amount
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * This method is DEPRECATED. The new burn-and-mint model does not require
     * L1 fee transactions for minting. Sequencer rewards come from L2 transaction
     * fees via FeeDistributor.
     * 
     * @param l1TxHash L1 transaction hash
     * @param expectedFee Expected fee amount
     * @return false - This method is deprecated and always fails
     */
    [[deprecated("No longer used - sequencer rewards come from L2 transaction fees")]]
    bool VerifyL1FeeTransaction(const uint256& l1TxHash, CAmount expectedFee) const;

    /**
     * @brief Check if L1 transaction was already used for minting
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * @param l1TxHash L1 transaction hash
     * @return false - This method is deprecated
     */
    [[deprecated("No longer used - no L1 fee transactions for minting")]]
    bool IsL1TxUsedForMinting(const uint256& l1TxHash) const;

    /**
     * @brief Mark L1 transaction as used for minting
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * @param l1TxHash L1 transaction hash
     */
    [[deprecated("No longer used - no L1 fee transactions for minting")]]
    void MarkL1TxUsedForMinting(const uint256& l1TxHash);

    /**
     * @brief Record a minting event
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * @param record The minting record to store
     */
    [[deprecated("No longer used - no block reward minting")]]
    void RecordMintingEvent(const MintingRecord& record);

    /**
     * @brief Get required L1 confirmations for minting
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * @return Number of confirmations required
     */
    [[deprecated("No longer used - no L1 fee transactions for minting")]]
    uint32_t GetRequiredL1Confirmations() const { return requiredL1Confirmations_; }

    /**
     * @brief Set required L1 confirmations for minting
     * 
     * *** DEPRECATED - Task 12.4 ***
     * 
     * @param confirmations Number of confirmations required
     */
    [[deprecated("No longer used - no L1 fee transactions for minting")]]
    void SetRequiredL1Confirmations(uint32_t confirmations) { requiredL1Confirmations_ = confirmations; }

    // =========================================================================
    // Transfers - Placeholder for Task 5
    // =========================================================================

    /**
     * @brief Process a token transfer
     * @param from Sender address
     * @param to Recipient address
     * @param amount Amount to transfer
     * @param fee Transfer fee
     * @param stateManager State manager to update
     * @return Transfer result
     * 
     * Note: Full implementation in Task 5
     */
    TransferResult ProcessTransfer(
        const uint160& from,
        const uint160& to,
        CAmount amount,
        CAmount fee,
        L2StateManager& stateManager);

    // =========================================================================
    // Queries
    // =========================================================================

    /**
     * @brief Get minting history for a block range
     * @param fromBlock Start block (inclusive)
     * @param toBlock End block (inclusive)
     * @return Vector of minting records
     * 
     * Requirement 8.4: Query total sequencer rewards
     */
    std::vector<MintingRecord> GetMintingHistory(uint64_t fromBlock, uint64_t toBlock) const;

    /**
     * @brief Get total sequencer rewards paid out
     * @return Total rewards in satoshis
     */
    CAmount GetTotalSequencerRewards() const;

    /**
     * @brief Verify supply invariant against state
     * @param stateManager State manager to verify against
     * @return true if invariant holds
     * 
     * Requirement 8.5: Sum of balances equals total supply
     */
    bool VerifySupplyInvariant(const L2StateManager& stateManager) const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Token configuration */
    L2TokenConfig config_;

    /** Token supply tracking */
    L2TokenSupply supply_;

    /** Genesis distribution (address -> amount) */
    std::map<uint160, CAmount> genesisDistribution_;

    /** Whether genesis has been applied */
    bool genesisApplied_;

    /** Minting records (l1TxHash -> record) */
    std::map<uint256, MintingRecord> mintingRecords_;

    /** Used L1 transactions (prevent double-use) */
    std::set<uint256> usedL1Transactions_;

    /** Required L1 confirmations for minting (default: 6) */
    uint32_t requiredL1Confirmations_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_token_;
};

} // namespace l2

#endif // CASCOIN_L2_TOKEN_MANAGER_H
