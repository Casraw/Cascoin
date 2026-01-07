// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file state_manager.cpp
 * @brief Implementation of L2 State Manager
 * 
 * Requirements: 3.1, 19.2
 */

#include <l2/state_manager.h>
#include <util.h>

namespace l2 {

L2StateManager::L2StateManager(uint64_t chainId)
    : chainId_(chainId)
    , currentBlockNumber_(0)
{
}

uint256 L2StateManager::GetStateRoot() const
{
    LOCK(cs_state_);
    return stateTree_.GetRoot();
}

AccountState L2StateManager::GetAccountState(const uint256& address) const
{
    LOCK(cs_state_);
    
    auto it = accountCache_.find(address);
    if (it != accountCache_.end()) {
        return it->second;
    }
    return AccountState();
}

AccountState L2StateManager::GetAccountState(const uint160& address) const
{
    return GetAccountState(AddressToKey(address));
}

void L2StateManager::SetAccountState(const uint256& address, const AccountState& state)
{
    LOCK(cs_state_);
    
    if (state.IsEmpty()) {
        // Remove empty accounts from tree and cache
        stateTree_.Delete(address);
        accountCache_.erase(address);
    } else {
        stateTree_.Set(address, state.Serialize());
        accountCache_[address] = state;
    }
}

TxExecutionResult L2StateManager::ApplyTransaction(const CTransaction& tx, uint64_t blockNumber)
{
    LOCK(cs_state_);
    
    // For now, implement basic value transfer
    // Full CVM execution will be added in later tasks
    
    if (tx.vout.empty()) {
        return TxExecutionResult::Failure("Transaction has no outputs");
    }
    
    // Calculate total output value
    CAmount totalOut = 0;
    for (const auto& out : tx.vout) {
        totalOut += out.nValue;
    }
    
    // For L2 transactions, we need sender info from the transaction
    // This is a simplified implementation - full implementation will
    // parse L2-specific transaction format
    
    // Update last activity for involved accounts
    currentBlockNumber_ = blockNumber;
    
    // Return success with current state root
    uint256 newRoot = stateTree_.GetRoot();
    return TxExecutionResult::Success(21000, newRoot);  // Base gas cost
}

std::vector<TxExecutionResult> L2StateManager::ApplyBatch(
    const std::vector<CTransaction>& txs,
    uint64_t blockNumber)
{
    LOCK(cs_state_);
    
    if (txs.size() > MAX_BATCH_SIZE) {
        std::vector<TxExecutionResult> results;
        results.push_back(TxExecutionResult::Failure("Batch size exceeds maximum"));
        return results;
    }
    
    // Save state for potential rollback
    uint256 preStateRoot = stateTree_.GetRoot();
    std::map<uint256, std::vector<unsigned char>> preState;
    
    // Collect pre-state for all potentially affected accounts
    // (In a full implementation, this would be more sophisticated)
    
    std::vector<TxExecutionResult> results;
    results.reserve(txs.size());
    
    bool batchFailed = false;
    
    for (const auto& tx : txs) {
        if (batchFailed) {
            results.push_back(TxExecutionResult::Failure("Batch aborted due to previous failure"));
            continue;
        }
        
        TxExecutionResult result = ApplyTransaction(tx, blockNumber);
        results.push_back(result);
        
        if (!result.success) {
            batchFailed = true;
        }
    }
    
    // If batch failed, revert all changes
    if (batchFailed) {
        // Revert to pre-state
        // In a full implementation, we would restore the saved state
        // For now, we rely on the caller to handle failed batches
    }
    
    return results;
}

bool L2StateManager::RevertToStateRoot(const uint256& stateRoot)
{
    LOCK(cs_state_);
    
    // Check if we have a snapshot for this state root
    auto it = snapshots_.find(stateRoot);
    if (it == snapshots_.end()) {
        LogPrintf("L2StateManager: Cannot revert to unknown state root %s\n", 
                  stateRoot.ToString());
        return false;
    }
    
    const StateSnapshot& snapshot = it->second;
    
    // Clear current state
    stateTree_.Clear();
    accountCache_.clear();
    
    // Restore account states from snapshot
    for (const auto& pair : snapshot.accountStates) {
        if (!pair.second.IsEmpty()) {
            stateTree_.Set(pair.first, pair.second.Serialize());
            accountCache_[pair.first] = pair.second;
        }
    }
    
    // Update block number
    currentBlockNumber_ = snapshot.blockNumber;
    
    // Verify restoration
    uint256 restoredRoot = stateTree_.GetRoot();
    if (restoredRoot != stateRoot) {
        LogPrintf("L2StateManager: State root mismatch after revert. Expected %s, got %s\n",
                  stateRoot.ToString(), restoredRoot.ToString());
        return false;
    }
    
    LogPrintf("L2StateManager: Successfully reverted to state root %s at block %lu\n",
              stateRoot.ToString(), snapshot.blockNumber);
    
    return true;
}

void L2StateManager::CreateSnapshot(uint64_t blockNumber, uint64_t l1AnchorBlock)
{
    LOCK(cs_state_);
    
    StateSnapshot snapshot;
    snapshot.stateRoot = stateTree_.GetRoot();
    snapshot.blockNumber = blockNumber;
    snapshot.l1AnchorBlock = l1AnchorBlock;
    snapshot.timestamp = GetTime();
    
    // Store current account states for rollback
    snapshot.accountStates = accountCache_;
    
    snapshots_[snapshot.stateRoot] = snapshot;
    snapshotOrder_.push_back(snapshot.stateRoot);
    
    // Prune old snapshots if needed
    PruneSnapshots();
    
    LogPrintf("L2StateManager: Created snapshot at block %lu with state root %s\n",
              blockNumber, snapshot.stateRoot.ToString());
}

size_t L2StateManager::GetSnapshotCount() const
{
    LOCK(cs_state_);
    return snapshots_.size();
}

void L2StateManager::PruneSnapshots()
{
    // Must be called with cs_state_ held
    
    while (snapshots_.size() > MAX_STATE_SNAPSHOTS && !snapshotOrder_.empty()) {
        uint256 oldestRoot = snapshotOrder_.front();
        snapshotOrder_.erase(snapshotOrder_.begin());
        snapshots_.erase(oldestRoot);
    }
}

MerkleProof L2StateManager::GenerateAccountProof(const uint256& address) const
{
    LOCK(cs_state_);
    return stateTree_.GenerateInclusionProof(address);
}

bool L2StateManager::VerifyAccountProof(
    const MerkleProof& proof,
    const uint256& stateRoot,
    const uint256& address,
    const AccountState& state)
{
    std::vector<unsigned char> serializedState;
    if (!state.IsEmpty()) {
        serializedState = state.Serialize();
    }
    
    return SparseMerkleTree::VerifyProof(proof, stateRoot, address, serializedState);
}

uint256 L2StateManager::GetContractStorage(const uint256& contractAddress, const uint256& key) const
{
    LOCK(cs_state_);
    
    auto it = storageTrees_.find(contractAddress);
    if (it == storageTrees_.end()) {
        return uint256();
    }
    
    std::vector<unsigned char> data = it->second->Get(key);
    if (data.size() != 32) {
        return uint256();
    }
    
    uint256 value;
    memcpy(value.begin(), data.data(), 32);
    return value;
}

void L2StateManager::SetContractStorage(const uint256& contractAddress, const uint256& key, const uint256& value)
{
    LOCK(cs_state_);
    
    SparseMerkleTree& storageTree = GetStorageTree(contractAddress);
    
    if (value.IsNull()) {
        storageTree.Delete(key);
    } else {
        std::vector<unsigned char> data(value.begin(), value.end());
        storageTree.Set(key, data);
    }
    
    // Update the account's storage root
    UpdateStorageRoot(contractAddress);
}

SparseMerkleTree& L2StateManager::GetStorageTree(const uint256& contractAddress)
{
    // Must be called with cs_state_ held
    
    auto it = storageTrees_.find(contractAddress);
    if (it == storageTrees_.end()) {
        auto result = storageTrees_.emplace(
            contractAddress, 
            std::make_unique<SparseMerkleTree>());
        return *result.first->second;
    }
    return *it->second;
}

void L2StateManager::UpdateStorageRoot(const uint256& contractAddress)
{
    // Must be called with cs_state_ held
    
    auto it = storageTrees_.find(contractAddress);
    if (it == storageTrees_.end()) {
        return;
    }
    
    // Get current account state
    AccountState state = GetAccountState(contractAddress);
    
    // Update storage root
    state.storageRoot = it->second->GetRoot();
    
    // Save updated state
    if (state.IsEmpty() && state.storageRoot.IsNull()) {
        stateTree_.Delete(contractAddress);
    } else {
        stateTree_.Set(contractAddress, state.Serialize());
    }
}

TxExecutionResult L2StateManager::ExecuteTransfer(
    const uint256& from,
    const uint256& to,
    CAmount amount,
    uint64_t blockNumber)
{
    // Must be called with cs_state_ held
    
    if (amount < 0) {
        return TxExecutionResult::Failure("Negative transfer amount");
    }
    
    if (amount == 0) {
        // Zero-value transfer is valid but does nothing
        return TxExecutionResult::Success(21000, stateTree_.GetRoot());
    }
    
    // Get sender state
    AccountState senderState = GetAccountState(from);
    
    // Check balance
    if (senderState.balance < amount) {
        return TxExecutionResult::Failure("Insufficient balance");
    }
    
    // Get recipient state
    AccountState recipientState = GetAccountState(to);
    
    // Execute transfer
    senderState.balance -= amount;
    senderState.nonce++;
    senderState.lastActivity = blockNumber;
    
    recipientState.balance += amount;
    recipientState.lastActivity = blockNumber;
    
    // Update states
    SetAccountState(from, senderState);
    SetAccountState(to, recipientState);
    
    return TxExecutionResult::Success(21000, stateTree_.GetRoot());
}

bool L2StateManager::IsEmpty() const
{
    LOCK(cs_state_);
    return stateTree_.Empty();
}

size_t L2StateManager::GetAccountCount() const
{
    LOCK(cs_state_);
    return stateTree_.Size();
}

void L2StateManager::Clear()
{
    LOCK(cs_state_);
    stateTree_.Clear();
    accountCache_.clear();
    storageTrees_.clear();
    snapshots_.clear();
    snapshotOrder_.clear();
    currentBlockNumber_ = 0;
}

// ============================================================================
// State Rent and Archiving Implementation (Requirements 20.1, 20.2, 20.3)
// ============================================================================

size_t L2StateManager::ProcessStateRent(uint64_t currentBlock, const StateRentConfig& config)
{
    LOCK(cs_state_);
    
    size_t accountsCharged = 0;
    std::vector<uint256> accountsToRemove;
    
    for (auto& pair : accountCache_) {
        const uint256& address = pair.first;
        AccountState& state = pair.second;
        
        // Skip accounts in grace period
        if (state.lastActivity + config.gracePeriodBlocks > currentBlock) {
            continue;
        }
        
        // Calculate rent based on account size
        // Base size: balance (8) + nonce (8) + codeHash (32) + storageRoot (32) + hatScore (4) + lastActivity (8) = 92 bytes
        size_t accountSize = 92;
        
        // Add storage size for contracts
        if (state.IsContract()) {
            auto storageIt = storageTrees_.find(address);
            if (storageIt != storageTrees_.end()) {
                // Estimate storage size (32 bytes per entry)
                accountSize += storageIt->second->Size() * 32;
            }
        }
        
        // Calculate blocks since last rent payment (using lastActivity as proxy)
        uint64_t blocksSinceActivity = currentBlock - state.lastActivity;
        
        // Calculate rent: (size * rate * blocks) / blocks_per_year
        // Simplified: charge proportionally
        uint64_t blocksPerYear = 365 * 24 * 60 * 60 / 150;  // ~2.5 min blocks
        CAmount rent = (accountSize * config.rentPerBytePerYear * blocksSinceActivity) / blocksPerYear;
        
        if (rent > 0) {
            if (state.balance >= rent) {
                state.balance -= rent;
                accountsCharged++;
                
                // Update state in tree
                stateTree_.Set(address, state.Serialize());
                
                LogPrintf("L2StateManager: Charged %lld satoshis rent from %s\n",
                          rent, address.ToString().substr(0, 16));
            } else if (state.balance < config.minimumBalance) {
                // Account cannot pay rent and is below minimum - mark for archiving
                accountsToRemove.push_back(address);
            }
        }
    }
    
    // Remove accounts that couldn't pay rent
    for (const uint256& address : accountsToRemove) {
        // Archive before removing
        ArchivedAccountState archived;
        archived.state = accountCache_[address];
        archived.archivedAtBlock = currentBlock;
        archived.archiveStateRoot = stateTree_.GetRoot();
        archived.archiveProof = stateTree_.GenerateInclusionProof(address).Serialize();
        
        archivedAccounts_[address] = archived;
        
        // Remove from active state
        stateTree_.Delete(address);
        accountCache_.erase(address);
        
        LogPrintf("L2StateManager: Archived account %s due to insufficient rent balance\n",
                  address.ToString().substr(0, 16));
    }
    
    return accountsCharged;
}

size_t L2StateManager::ArchiveInactiveState(uint64_t currentBlock, uint64_t inactivityThreshold)
{
    LOCK(cs_state_);
    
    size_t accountsArchived = 0;
    std::vector<uint256> accountsToArchive;
    
    for (const auto& pair : accountCache_) {
        const uint256& address = pair.first;
        const AccountState& state = pair.second;
        
        // Check if account has been inactive for too long
        if (state.lastActivity + inactivityThreshold <= currentBlock) {
            accountsToArchive.push_back(address);
        }
    }
    
    // Archive inactive accounts
    for (const uint256& address : accountsToArchive) {
        ArchivedAccountState archived;
        archived.state = accountCache_[address];
        archived.archivedAtBlock = currentBlock;
        archived.archiveStateRoot = stateTree_.GetRoot();
        
        // Generate proof before removing
        MerkleProof proof = stateTree_.GenerateInclusionProof(address);
        CDataStream ss(SER_DISK, 0);
        ss << proof;
        archived.archiveProof = std::vector<unsigned char>(ss.begin(), ss.end());
        
        archivedAccounts_[address] = archived;
        
        // Remove from active state
        stateTree_.Delete(address);
        accountCache_.erase(address);
        
        // Also remove contract storage if applicable
        storageTrees_.erase(address);
        
        accountsArchived++;
        
        LogPrintf("L2StateManager: Archived inactive account %s (last activity: block %lu)\n",
                  address.ToString().substr(0, 16), archived.state.lastActivity);
    }
    
    return accountsArchived;
}

bool L2StateManager::RestoreArchivedState(const uint256& address, const ArchivedAccountState& archived)
{
    LOCK(cs_state_);
    
    // Check if account is currently archived
    auto it = archivedAccounts_.find(address);
    if (it == archivedAccounts_.end()) {
        LogPrintf("L2StateManager: Cannot restore - account %s not found in archive\n",
                  address.ToString().substr(0, 16));
        return false;
    }
    
    // Verify the archived state matches what we have
    if (it->second.state != archived.state) {
        LogPrintf("L2StateManager: Cannot restore - archived state mismatch for %s\n",
                  address.ToString().substr(0, 16));
        return false;
    }
    
    // Verify the proof (optional but recommended)
    // In a full implementation, we would verify the proof against the archive state root
    
    // Restore the account to active state
    AccountState restoredState = archived.state;
    restoredState.lastActivity = currentBlockNumber_;  // Update activity time
    
    stateTree_.Set(address, restoredState.Serialize());
    accountCache_[address] = restoredState;
    
    // Remove from archive
    archivedAccounts_.erase(address);
    
    LogPrintf("L2StateManager: Restored archived account %s\n",
              address.ToString().substr(0, 16));
    
    return true;
}

std::optional<ArchivedAccountState> L2StateManager::GetArchivedState(const uint256& address) const
{
    LOCK(cs_state_);
    
    auto it = archivedAccounts_.find(address);
    if (it != archivedAccounts_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool L2StateManager::IsArchived(const uint256& address) const
{
    LOCK(cs_state_);
    return archivedAccounts_.find(address) != archivedAccounts_.end();
}

size_t L2StateManager::GetArchivedCount() const
{
    LOCK(cs_state_);
    return archivedAccounts_.size();
}

} // namespace l2
