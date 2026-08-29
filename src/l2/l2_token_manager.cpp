// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_token_manager.cpp
 * @brief Implementation of L2 Token Manager
 * 
 * Requirements: 1.5, 1.6, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.2
 */

#include <l2/l2_token_manager.h>
#include <util.h>
#include <validation.h>
#include <chain.h>
#include <chainparams.h>

#include <chrono>

namespace l2 {

/** Default required L1 confirmations for minting */
static constexpr uint32_t DEFAULT_REQUIRED_L1_CONFIRMATIONS = 6;

// ============================================================================
// Constructor
// ============================================================================

L2TokenManager::L2TokenManager(uint64_t chainId, const L2TokenConfig& config)
    : chainId_(chainId)
    , config_(config)
    , genesisApplied_(false)
    , requiredL1Confirmations_(DEFAULT_REQUIRED_L1_CONFIRMATIONS)
{
    // Validate configuration
    if (!config_.IsValid()) {
        LogPrintf("L2TokenManager: Warning - invalid token configuration\n");
    }
    
    LogPrintf("L2TokenManager: Initialized for chain %lu with token %s (%s)\n",
              chainId_, config_.tokenName, config_.tokenSymbol);
}

// ============================================================================
// Token Info Accessors (Requirements 1.6, 8.1, 8.2)
// ============================================================================

const L2TokenConfig& L2TokenManager::GetConfig() const
{
    LOCK(cs_token_);
    return config_;
}

const L2TokenSupply& L2TokenManager::GetSupply() const
{
    LOCK(cs_token_);
    return supply_;
}

std::string L2TokenManager::GetTokenName() const
{
    LOCK(cs_token_);
    return config_.tokenName;
}

std::string L2TokenManager::GetTokenSymbol() const
{
    LOCK(cs_token_);
    return config_.tokenSymbol;
}

// ============================================================================
// Genesis Distribution (Requirements 4.1, 4.2, 4.3, 4.4, 4.5)
// ============================================================================

bool L2TokenManager::SetGenesisDistribution(const std::map<uint160, CAmount>& distribution)
{
    LOCK(cs_token_);
    
    if (genesisApplied_) {
        LogPrintf("L2TokenManager: Cannot set genesis distribution - already applied\n");
        return false;
    }
    
    // Calculate total distribution
    CAmount totalDistribution = 0;
    for (const auto& pair : distribution) {
        if (pair.second < 0) {
            LogPrintf("L2TokenManager: Invalid negative amount in genesis distribution\n");
            return false;
        }
        totalDistribution += pair.second;
    }
    
    // Requirement 4.3: Enforce maxGenesisSupply limit
    if (totalDistribution > config_.maxGenesisSupply) {
        LogPrintf("L2TokenManager: Genesis distribution %lld exceeds max %lld\n",
                  totalDistribution, config_.maxGenesisSupply);
        return false;
    }
    
    genesisDistribution_ = distribution;
    
    LogPrintf("L2TokenManager: Set genesis distribution with %zu addresses, total %lld\n",
              distribution.size(), totalDistribution);
    
    return true;
}

bool L2TokenManager::ApplyGenesisDistribution(L2StateManager& stateManager)
{
    LOCK(cs_token_);
    
    if (genesisApplied_) {
        LogPrintf("L2TokenManager: Genesis distribution already applied\n");
        return false;
    }
    
    // Requirement 4.5: If no distribution specified, start with zero supply
    if (genesisDistribution_.empty()) {
        genesisApplied_ = true;
        supply_.totalSupply = 0;
        supply_.genesisSupply = 0;
        LogPrintf("L2TokenManager: No genesis distribution - starting with zero supply\n");
        return true;
    }
    
    // Calculate total and verify limit again
    CAmount totalDistribution = 0;
    for (const auto& pair : genesisDistribution_) {
        totalDistribution += pair.second;
    }
    
    // Requirement 4.3: Enforce maxGenesisSupply limit
    if (totalDistribution > config_.maxGenesisSupply) {
        LogPrintf("L2TokenManager: Genesis distribution exceeds max supply limit\n");
        return false;
    }
    
    // Apply distribution to state manager
    for (const auto& pair : genesisDistribution_) {
        const uint160& address = pair.first;
        CAmount amount = pair.second;
        
        if (amount <= 0) continue;
        
        // Get current account state (should be empty for genesis)
        uint256 addressKey = AddressToKey(address);
        AccountState state = stateManager.GetAccountState(addressKey);
        
        // Credit the genesis amount
        state.balance += amount;
        state.lastActivity = 0;  // Genesis block
        
        // Update state
        stateManager.SetAccountState(addressKey, state);
        
        LogPrintf("L2TokenManager: Genesis distribution - %s receives %lld %s\n",
                  address.ToString().substr(0, 16), amount / COIN, config_.tokenSymbol);
    }
    
    // Update supply tracking
    supply_.genesisSupply = totalDistribution;
    supply_.totalSupply = totalDistribution;
    
    genesisApplied_ = true;
    
    LogPrintf("L2TokenManager: Applied genesis distribution - total supply: %lld %s\n",
              supply_.totalSupply / COIN, config_.tokenSymbol);
    
    return true;
}

std::vector<std::pair<uint160, CAmount>> L2TokenManager::GetGenesisDistribution() const
{
    LOCK(cs_token_);
    
    std::vector<std::pair<uint160, CAmount>> result;
    result.reserve(genesisDistribution_.size());
    
    for (const auto& pair : genesisDistribution_) {
        result.emplace_back(pair.first, pair.second);
    }
    
    return result;
}

bool L2TokenManager::IsGenesisApplied() const
{
    LOCK(cs_token_);
    return genesisApplied_;
}

// ============================================================================
// Minting (Sequencer Rewards) - DEPRECATED
// Requirements 6.1, 6.2: Sequencer rewards now come from FeeDistributor only
// ============================================================================

bool L2TokenManager::VerifyL1FeeTransaction(const uint256& /* l1TxHash */, CAmount /* expectedFee */) const
{
    // DEPRECATED - Task 12.4
    // Sequencer rewards now come from FeeDistributor, not from L1 fee transactions
    LogPrintf("L2TokenManager: WARNING - VerifyL1FeeTransaction is DEPRECATED. "
              "Sequencer rewards now come from L2 transaction fees via FeeDistributor.\n");
    return false;
}

bool L2TokenManager::IsL1TxUsedForMinting(const uint256& /* l1TxHash */) const
{
    // DEPRECATED - Task 12.4
    // No longer tracking L1 transactions for minting
    LogPrintf("L2TokenManager: WARNING - IsL1TxUsedForMinting is DEPRECATED. "
              "No L1 fee transactions are used for minting in the new model.\n");
    return false;
}

void L2TokenManager::MarkL1TxUsedForMinting(const uint256& /* l1TxHash */)
{
    // DEPRECATED - Task 12.4
    // No longer tracking L1 transactions for minting
    LogPrintf("L2TokenManager: WARNING - MarkL1TxUsedForMinting is DEPRECATED. "
              "No L1 fee transactions are used for minting in the new model.\n");
}

void L2TokenManager::RecordMintingEvent(const MintingRecord& /* record */)
{
    // DEPRECATED - Task 12.4
    // No longer recording minting events from block rewards
    LogPrintf("L2TokenManager: WARNING - RecordMintingEvent is DEPRECATED. "
              "No block reward minting in the new model. Use FeeDistributor instead.\n");
}

bool L2TokenManager::ProcessBlockReward(
    const uint160& /* sequencer */,
    uint64_t /* l2BlockNumber */,
    const uint256& /* l2BlockHash */,
    const uint256& /* l1TxHash */,
    uint64_t /* l1BlockNumber */,
    L2StateManager& /* stateManager */)
{
    // DEPRECATED - Task 12.4
    // Requirements 6.1, 6.2: Sequencer rewards now come from FeeDistributor only
    // No new tokens are minted as block rewards
    
    LogPrintf("L2TokenManager: ERROR - ProcessBlockReward is DEPRECATED and disabled.\n"
              "Sequencer rewards now come exclusively from L2 transaction fees.\n"
              "Use FeeDistributor::DistributeBlockFees() instead.\n"
              "No new tokens are minted as block rewards (Requirements 6.1, 6.2).\n");
    
    // Always return false - this functionality is disabled
    return false;
}

// ============================================================================
// Transfers - Requirements 7.1, 7.2, 7.3, 7.4, 7.5
// ============================================================================

TransferResult L2TokenManager::ProcessTransfer(
    const uint160& from,
    const uint160& to,
    CAmount amount,
    CAmount fee,
    L2StateManager& stateManager)
{
    // Requirement 7.1: Verify sender has sufficient balance
    // Requirement 7.2: Atomic debit/credit
    // Requirement 7.3: Record transfer in L2 transaction
    // Requirement 7.4: Require small fee to prevent spam
    // Requirement 7.5: Reject if insufficient balance
    
    LOCK(cs_token_);
    
    // Validate inputs
    if (from.IsNull()) {
        return TransferResult::Failure("Invalid sender address");
    }
    
    if (to.IsNull()) {
        return TransferResult::Failure("Invalid recipient address");
    }
    
    if (amount < 0) {
        return TransferResult::Failure("Transfer amount cannot be negative");
    }
    
    if (amount == 0) {
        return TransferResult::Failure("Transfer amount must be greater than zero");
    }
    
    if (fee < 0) {
        return TransferResult::Failure("Transfer fee cannot be negative");
    }
    
    // Requirement 7.4: Require minimum fee to prevent spam
    if (fee < config_.minTransferFee) {
        return TransferResult::Failure("Transfer fee below minimum required (" + 
            std::to_string(config_.minTransferFee) + " " + config_.tokenSymbol + ")");
    }
    
    // Calculate total required balance (amount + fee)
    CAmount totalRequired = amount + fee;
    
    // Check for overflow
    if (totalRequired < amount || totalRequired < fee) {
        return TransferResult::Failure("Transfer amount overflow");
    }
    
    // Get sender's current state
    uint256 fromKey = AddressToKey(from);
    AccountState senderState = stateManager.GetAccountState(fromKey);
    
    // Requirement 7.1, 7.5: Verify sender has sufficient balance
    if (senderState.balance < totalRequired) {
        return TransferResult::Failure("Insufficient balance for transfer (need " +
            std::to_string(totalRequired) + ", have " + std::to_string(senderState.balance) + ")");
    }
    
    // Get recipient's current state
    uint256 toKey = AddressToKey(to);
    AccountState recipientState = stateManager.GetAccountState(toKey);
    
    // Check for recipient balance overflow
    if (recipientState.balance + amount < recipientState.balance) {
        return TransferResult::Failure("Recipient balance overflow");
    }
    
    // =========================================================================
    // Requirement 7.2: Atomic debit/credit
    // We perform both operations together - if either fails, neither is applied
    // =========================================================================
    
    // Store original states for potential rollback (though we validate first)
    CAmount originalSenderBalance = senderState.balance;
    CAmount originalRecipientBalance = recipientState.balance;
    
    // Debit sender (amount + fee)
    senderState.balance -= totalRequired;
    senderState.nonce++;
    senderState.lastActivity = stateManager.GetBlockNumber();
    
    // Credit recipient (amount only, fee is handled separately)
    recipientState.balance += amount;
    recipientState.lastActivity = stateManager.GetBlockNumber();
    
    // Apply state changes atomically
    stateManager.SetAccountState(fromKey, senderState);
    stateManager.SetAccountState(toKey, recipientState);
    
    // Handle fee - burn it (reduce total supply)
    // This is the simplest approach; alternatively fees could be redistributed
    supply_.burnedSupply += fee;
    supply_.totalSupply -= fee;
    
    // Verify supply invariant still holds
    if (!supply_.VerifyInvariant()) {
        // This should never happen if our logic is correct
        // Rollback the changes
        senderState.balance = originalSenderBalance;
        senderState.nonce--;
        recipientState.balance = originalRecipientBalance;
        stateManager.SetAccountState(fromKey, senderState);
        stateManager.SetAccountState(toKey, recipientState);
        supply_.burnedSupply -= fee;
        supply_.totalSupply += fee;
        
        LogPrintf("L2TokenManager: ProcessTransfer - supply invariant violation, rolled back\n");
        return TransferResult::Failure("Internal error: supply invariant violation");
    }
    
    // Get new state root after transfer
    uint256 newStateRoot = stateManager.GetStateRoot();
    
    // Generate a transaction hash for this transfer
    // In a full implementation, this would be the actual L2 transaction hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << from;
    ss << to;
    ss << amount;
    ss << fee;
    ss << senderState.nonce;
    ss << stateManager.GetBlockNumber();
    uint256 txHash = ss.GetHash();
    
    LogPrintf("L2TokenManager: ProcessTransfer - %s sent %lld %s to %s (fee: %lld)\n",
              from.ToString().substr(0, 16), amount / COIN, config_.tokenSymbol,
              to.ToString().substr(0, 16), fee);
    
    return TransferResult::Success(newStateRoot, txHash);
}

// ============================================================================
// Queries
// ============================================================================

std::vector<MintingRecord> L2TokenManager::GetMintingHistory(uint64_t fromBlock, uint64_t toBlock) const
{
    LOCK(cs_token_);
    
    std::vector<MintingRecord> result;
    
    for (const auto& pair : mintingRecords_) {
        const MintingRecord& record = pair.second;
        if (record.l2BlockNumber >= fromBlock && record.l2BlockNumber <= toBlock) {
            result.push_back(record);
        }
    }
    
    // Sort by block number
    std::sort(result.begin(), result.end(),
              [](const MintingRecord& a, const MintingRecord& b) {
                  return a.l2BlockNumber < b.l2BlockNumber;
              });
    
    return result;
}

CAmount L2TokenManager::GetTotalSequencerRewards() const
{
    LOCK(cs_token_);
    return supply_.mintedSupply;
}

bool L2TokenManager::VerifySupplyInvariant(const L2StateManager& /* stateManager */) const
{
    LOCK(cs_token_);
    
    // First verify internal invariant
    if (!supply_.VerifyInvariant()) {
        LogPrintf("L2TokenManager: Internal supply invariant failed\n");
        return false;
    }
    
    // Note: Full balance sum verification would require iterating all accounts
    // This is expensive and should be done sparingly
    // For now, we trust the internal tracking
    
    return true;
}

} // namespace l2
