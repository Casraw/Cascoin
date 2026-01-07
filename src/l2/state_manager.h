// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_STATE_MANAGER_H
#define CASCOIN_L2_STATE_MANAGER_H

/**
 * @file state_manager.h
 * @brief L2 State Manager for managing L2 chain state
 * 
 * This file implements the L2StateManager class that manages the complete
 * state of an L2 chain using a Sparse Merkle Tree. It provides operations
 * for applying transactions, computing state roots, and reverting state.
 * 
 * Requirements: 3.1, 19.2
 */

#include <l2/account_state.h>
#include <l2/sparse_merkle_tree.h>
#include <l2/l2_common.h>
#include <uint256.h>
#include <primitives/transaction.h>
#include <sync.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace l2 {

/** Maximum number of state snapshots to keep for rollback */
static constexpr size_t MAX_STATE_SNAPSHOTS = 100;

/** Maximum batch size for transaction processing */
static constexpr size_t MAX_BATCH_SIZE = 1000;

/**
 * @brief State snapshot for rollback support
 * 
 * Stores a complete snapshot of the state at a specific point
 * for supporting state reversion (Requirement 19.2).
 */
struct StateSnapshot {
    /** State root at this snapshot */
    uint256 stateRoot;
    
    /** L2 block number */
    uint64_t blockNumber;
    
    /** L1 anchor block number */
    uint64_t l1AnchorBlock;
    
    /** Timestamp of snapshot */
    uint64_t timestamp;
    
    /** Account states at this snapshot (for efficient rollback) */
    std::map<uint256, AccountState> accountStates;

    StateSnapshot() : blockNumber(0), l1AnchorBlock(0), timestamp(0) {}
};

/**
 * @brief Transaction execution result
 */
struct TxExecutionResult {
    /** Whether execution succeeded */
    bool success;
    
    /** Error message if failed */
    std::string error;
    
    /** Gas used by transaction */
    uint64_t gasUsed;
    
    /** New state root after execution */
    uint256 newStateRoot;
    
    /** Logs/events generated */
    std::vector<std::vector<unsigned char>> logs;

    TxExecutionResult() : success(false), gasUsed(0) {}
    
    static TxExecutionResult Success(uint64_t gas, const uint256& root) {
        TxExecutionResult result;
        result.success = true;
        result.gasUsed = gas;
        result.newStateRoot = root;
        return result;
    }
    
    static TxExecutionResult Failure(const std::string& err) {
        TxExecutionResult result;
        result.success = false;
        result.error = err;
        return result;
    }
};

/**
 * @brief L2 State Manager
 * 
 * Manages the complete state of an L2 chain using a Sparse Merkle Tree.
 * Provides operations for:
 * - Getting and setting account states
 * - Applying transactions and batches
 * - Computing state roots
 * - Reverting to previous states
 * - State rent processing
 * 
 * Thread-safe for concurrent access.
 */
class L2StateManager {
public:
    /**
     * @brief Construct a new L2 State Manager
     * @param chainId The L2 chain ID
     */
    explicit L2StateManager(uint64_t chainId);

    /**
     * @brief Get the current state root
     * @return The 256-bit state root hash
     */
    uint256 GetStateRoot() const;

    /**
     * @brief Get account state for an address
     * @param address The account address (as uint256 key)
     * @return The account state (empty state if not found)
     */
    AccountState GetAccountState(const uint256& address) const;

    /**
     * @brief Get account state for a uint160 address
     * @param address The 160-bit account address
     * @return The account state (empty state if not found)
     */
    AccountState GetAccountState(const uint160& address) const;

    /**
     * @brief Set account state for an address
     * @param address The account address (as uint256 key)
     * @param state The new account state
     */
    void SetAccountState(const uint256& address, const AccountState& state);

    /**
     * @brief Apply a single transaction to the state
     * @param tx The transaction to apply
     * @param blockNumber Current L2 block number
     * @return Execution result with new state root
     */
    TxExecutionResult ApplyTransaction(const CTransaction& tx, uint64_t blockNumber);

    /**
     * @brief Apply a batch of transactions atomically
     * @param txs Vector of transactions to apply
     * @param blockNumber Current L2 block number
     * @return Vector of execution results (one per transaction)
     * 
     * If any transaction fails, the entire batch is reverted.
     */
    std::vector<TxExecutionResult> ApplyBatch(
        const std::vector<CTransaction>& txs,
        uint64_t blockNumber);

    /**
     * @brief Revert state to a previous state root
     * @param stateRoot The state root to revert to
     * @return true if reversion succeeded, false if state root not found
     * 
     * Requirement 19.2: Support state reversion for L1 reorg handling
     */
    bool RevertToStateRoot(const uint256& stateRoot);

    /**
     * @brief Create a state snapshot at current state
     * @param blockNumber L2 block number
     * @param l1AnchorBlock L1 anchor block number
     */
    void CreateSnapshot(uint64_t blockNumber, uint64_t l1AnchorBlock);

    /**
     * @brief Get the number of stored snapshots
     * @return Number of snapshots
     */
    size_t GetSnapshotCount() const;

    /**
     * @brief Generate inclusion proof for an account
     * @param address The account address
     * @return Merkle proof for the account state
     */
    MerkleProof GenerateAccountProof(const uint256& address) const;

    /**
     * @brief Verify an account proof against a state root
     * @param proof The Merkle proof
     * @param stateRoot The state root to verify against
     * @param address The account address
     * @param state The expected account state
     * @return true if proof is valid
     */
    static bool VerifyAccountProof(
        const MerkleProof& proof,
        const uint256& stateRoot,
        const uint256& address,
        const AccountState& state);

    /**
     * @brief Get contract storage value
     * @param contractAddress The contract address
     * @param key The storage key
     * @return The storage value (zero if not found)
     */
    uint256 GetContractStorage(const uint256& contractAddress, const uint256& key) const;

    /**
     * @brief Set contract storage value
     * @param contractAddress The contract address
     * @param key The storage key
     * @param value The storage value
     */
    void SetContractStorage(const uint256& contractAddress, const uint256& key, const uint256& value);

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get current L2 block number
     * @return Block number
     */
    uint64_t GetBlockNumber() const { return currentBlockNumber_; }

    /**
     * @brief Set current L2 block number
     * @param blockNumber New block number
     */
    void SetBlockNumber(uint64_t blockNumber) { currentBlockNumber_ = blockNumber; }

    /**
     * @brief Check if state is empty
     * @return true if no accounts exist
     */
    bool IsEmpty() const;

    /**
     * @brief Get number of accounts in state
     * @return Account count
     */
    size_t GetAccountCount() const;

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

    // =========================================================================
    // State Rent and Archiving (Requirements 20.1, 20.2, 20.3)
    // =========================================================================

    /**
     * @brief Process state rent for all accounts
     * @param currentBlock Current L2 block number
     * @param config State rent configuration
     * @return Number of accounts charged rent
     * 
     * Requirement 20.1: Implement state rent for contract storage
     */
    size_t ProcessStateRent(uint64_t currentBlock, const StateRentConfig& config);

    /**
     * @brief Archive inactive accounts
     * @param currentBlock Current L2 block number
     * @param inactivityThreshold Blocks of inactivity before archiving
     * @return Number of accounts archived
     * 
     * Requirement 20.2: Archive inactive state after threshold
     */
    size_t ArchiveInactiveState(uint64_t currentBlock, uint64_t inactivityThreshold);

    /**
     * @brief Restore an archived account
     * @param address The account address to restore
     * @param archived The archived account data
     * @return true if restoration succeeded
     * 
     * Requirement 20.3: Enable state restoration from archive with proof
     */
    bool RestoreArchivedState(const uint256& address, const ArchivedAccountState& archived);

    /**
     * @brief Get archived account state
     * @param address The account address
     * @return Archived state if found, empty optional otherwise
     */
    std::optional<ArchivedAccountState> GetArchivedState(const uint256& address) const;

    /**
     * @brief Check if an account is archived
     * @param address The account address
     * @return true if account is archived
     */
    bool IsArchived(const uint256& address) const;

    /**
     * @brief Get the number of archived accounts
     * @return Count of archived accounts
     */
    size_t GetArchivedCount() const;

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Current L2 block number */
    uint64_t currentBlockNumber_;

    /** Main state tree (accounts) */
    SparseMerkleTree stateTree_;

    /** Account cache for tracking all accounts (address -> state) */
    std::map<uint256, AccountState> accountCache_;

    /** Archived accounts (address -> archived state) */
    std::map<uint256, ArchivedAccountState> archivedAccounts_;

    /** Contract storage trees (contract address -> storage tree) */
    std::map<uint256, std::unique_ptr<SparseMerkleTree>> storageTrees_;

    /** State snapshots for rollback */
    std::map<uint256, StateSnapshot> snapshots_;

    /** Ordered list of snapshot roots (for pruning) */
    std::vector<uint256> snapshotOrder_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_state_;

    /**
     * @brief Prune old snapshots if limit exceeded
     */
    void PruneSnapshots();

    /**
     * @brief Get or create storage tree for a contract
     * @param contractAddress The contract address
     * @return Reference to the storage tree
     */
    SparseMerkleTree& GetStorageTree(const uint256& contractAddress);

    /**
     * @brief Update account's storage root after storage modification
     * @param contractAddress The contract address
     */
    void UpdateStorageRoot(const uint256& contractAddress);

    /**
     * @brief Execute a value transfer
     * @param from Sender address
     * @param to Recipient address
     * @param amount Amount to transfer
     * @param blockNumber Current block number
     * @return Execution result
     */
    TxExecutionResult ExecuteTransfer(
        const uint256& from,
        const uint256& to,
        CAmount amount,
        uint64_t blockNumber);
};

} // namespace l2

#endif // CASCOIN_L2_STATE_MANAGER_H
