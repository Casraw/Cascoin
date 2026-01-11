// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_minter.cpp
 * @brief Implementation of L2 Token Minter for Burn-and-Mint Token Model
 * 
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.3
 */

#include <l2/l2_minter.h>
#include <l2/account_state.h>
#include <hash.h>
#include <util.h>

#include <chrono>
#include <memory>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<L2TokenMinter> g_l2TokenMinter;
static bool g_l2TokenMinterInitialized = false;

L2TokenMinter& GetL2TokenMinter() {
    assert(g_l2TokenMinterInitialized && "L2TokenMinter not initialized");
    return *g_l2TokenMinter;
}

void InitL2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry) {
    g_l2TokenMinter = std::make_unique<L2TokenMinter>(stateManager, burnRegistry);
    g_l2TokenMinterInitialized = true;
}

bool IsL2TokenMinterInitialized() {
    return g_l2TokenMinterInitialized;
}

// ============================================================================
// L2TokenMinter Implementation
// ============================================================================

L2TokenMinter::L2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry)
    : stateManager_(stateManager)
    , burnRegistry_(burnRegistry)
    , totalSupply_(0)
    , totalMinted_(0)
    , currentBlockNumber_(0)
{
}

L2TokenMinter::~L2TokenMinter() {
}

MintResult L2TokenMinter::MintTokens(
    const uint256& l1TxHash,
    const uint160& recipient,
    CAmount amount)
{
    // Use default values for L1 block info when not provided
    return MintTokensWithDetails(l1TxHash, 0, uint256(), recipient, amount);
}

MintResult L2TokenMinter::MintTokensWithDetails(
    const uint256& l1TxHash,
    uint64_t l1BlockNumber,
    const uint256& l1BlockHash,
    const uint160& recipient,
    CAmount amount)
{
    LOCK(cs_minter_);
    
    // Validate inputs
    if (l1TxHash.IsNull()) {
        return MintResult::Failure("L1 transaction hash is null");
    }
    
    if (recipient.IsNull()) {
        return MintResult::Failure("Recipient address is null");
    }
    
    if (amount <= 0) {
        return MintResult::Failure("Mint amount must be positive");
    }
    
    // Check if burn was already processed (double-mint prevention)
    // Requirement 4.3: Mark burn as processed
    if (burnRegistry_.IsProcessed(l1TxHash)) {
        return MintResult::Failure("Burn transaction already processed");
    }
    
    // Get current block number
    uint64_t blockNumber = currentBlockNumber_;
    if (blockNumber == 0) {
        blockNumber = stateManager_.GetBlockNumber();
    }
    
    // Generate L2 transaction hash
    uint256 l2TxHash = GenerateL2TxHash(l1TxHash, recipient, amount, blockNumber);
    
    // Update state atomically
    // Requirement 4.5: Tokens immediately available in L2 state
    if (!UpdateState(recipient, amount)) {
        return MintResult::Failure("Failed to update L2 state");
    }
    
    // Record the burn in registry
    // Requirement 4.3: Mark burn as processed
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    BurnRecord record(
        l1TxHash,
        l1BlockNumber > 0 ? l1BlockNumber : 1,  // Default to 1 if not provided
        l1BlockHash.IsNull() ? l1TxHash : l1BlockHash,  // Use l1TxHash as placeholder if not provided
        recipient,
        amount,
        blockNumber,
        l2TxHash,
        timestamp
    );
    
    if (!burnRegistry_.RecordBurn(record)) {
        // This shouldn't happen since we checked IsProcessed above
        // But handle it gracefully
        return MintResult::Failure("Failed to record burn in registry");
    }
    
    // Update supply tracking
    // Requirement 4.6: Increase L2 total supply
    totalSupply_ += amount;
    totalMinted_ += amount;
    
    // Emit mint event
    // Requirement 4.4: Emit MintEvent
    MintEvent event(l1TxHash, recipient, amount, l2TxHash, blockNumber, timestamp);
    EmitMintEvent(event);
    
    return MintResult::Success(l2TxHash, blockNumber, amount);
}

CAmount L2TokenMinter::GetTotalSupply() const {
    LOCK(cs_minter_);
    return totalSupply_;
}

bool L2TokenMinter::VerifySupplyInvariant() const {
    LOCK(cs_minter_);
    
    // Requirement 8.1: Total L2 supply == Total CAS burned on L1
    CAmount totalBurned = burnRegistry_.GetTotalBurned();
    if (totalSupply_ != totalBurned) {
        LogPrintf("L2TokenMinter: Supply invariant violated - totalSupply (%lld) != totalBurned (%lld)\n",
                  totalSupply_, totalBurned);
        return false;
    }
    
    // Requirement 8.3: Sum of all L2 balances == Total supply
    // Calculate sum of all balances from state manager
    CAmount sumOfBalances = 0;
    
    // Iterate through all mint events to get all recipients
    // and sum their balances
    std::set<uint160> allRecipients;
    for (const auto& event : mintEvents_) {
        allRecipients.insert(event.recipient);
    }
    
    for (const uint160& addr : allRecipients) {
        uint256 key = AddressToKey(addr);
        AccountState state = stateManager_.GetAccountState(key);
        sumOfBalances += state.balance;
    }
    
    if (sumOfBalances != totalSupply_) {
        LogPrintf("L2TokenMinter: Supply invariant violated - sumOfBalances (%lld) != totalSupply (%lld)\n",
                  sumOfBalances, totalSupply_);
        return false;
    }
    
    return true;
}

CAmount L2TokenMinter::GetBalance(const uint160& address) const {
    uint256 key = AddressToKey(address);
    AccountState state = stateManager_.GetAccountState(key);
    return state.balance;
}

CAmount L2TokenMinter::GetTotalBurnedL1() const {
    return burnRegistry_.GetTotalBurned();
}

CAmount L2TokenMinter::GetTotalMintedL2() const {
    LOCK(cs_minter_);
    return totalMinted_;
}

void L2TokenMinter::RegisterMintEventCallback(MintEventCallback callback) {
    LOCK(cs_minter_);
    mintEventCallbacks_.push_back(std::move(callback));
}

std::vector<MintEvent> L2TokenMinter::GetMintEvents() const {
    LOCK(cs_minter_);
    return mintEvents_;
}

std::vector<MintEvent> L2TokenMinter::GetMintEventsForAddress(const uint160& recipient) const {
    LOCK(cs_minter_);
    
    std::vector<MintEvent> result;
    auto it = mintEventsByRecipient_.find(recipient);
    if (it != mintEventsByRecipient_.end()) {
        for (size_t idx : it->second) {
            if (idx < mintEvents_.size()) {
                result.push_back(mintEvents_[idx]);
            }
        }
    }
    return result;
}

std::optional<MintEvent> L2TokenMinter::GetMintEventByL1TxHash(const uint256& l1TxHash) const {
    LOCK(cs_minter_);
    
    auto it = mintEventsByL1TxHash_.find(l1TxHash);
    if (it != mintEventsByL1TxHash_.end() && it->second < mintEvents_.size()) {
        return mintEvents_[it->second];
    }
    return std::nullopt;
}

void L2TokenMinter::Clear() {
    LOCK(cs_minter_);
    
    totalSupply_ = 0;
    totalMinted_ = 0;
    currentBlockNumber_ = 0;
    mintEvents_.clear();
    mintEventsByL1TxHash_.clear();
    mintEventsByRecipient_.clear();
    // Note: Don't clear callbacks
}

uint64_t L2TokenMinter::GetCurrentBlockNumber() const {
    LOCK(cs_minter_);
    return currentBlockNumber_;
}

void L2TokenMinter::SetCurrentBlockNumber(uint64_t blockNumber) {
    LOCK(cs_minter_);
    currentBlockNumber_ = blockNumber;
}

bool L2TokenMinter::UpdateState(const uint160& recipient, CAmount amount) {
    // Convert address to key for state manager
    uint256 key = AddressToKey(recipient);
    
    // Get current account state
    AccountState state = stateManager_.GetAccountState(key);
    
    // Add amount to balance
    // Requirement 4.2: Minted amount exactly equals burned amount (1:1)
    state.balance += amount;
    
    // Update last activity
    state.lastActivity = currentBlockNumber_ > 0 ? currentBlockNumber_ : stateManager_.GetBlockNumber();
    
    // Set updated state
    stateManager_.SetAccountState(key, state);
    
    return true;
}

void L2TokenMinter::EmitMintEvent(const MintEvent& event) {
    // Store event
    size_t idx = mintEvents_.size();
    mintEvents_.push_back(event);
    
    // Update indexes
    mintEventsByL1TxHash_[event.l1TxHash] = idx;
    mintEventsByRecipient_[event.recipient].push_back(idx);
    
    // Notify callbacks
    for (const auto& callback : mintEventCallbacks_) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LogPrintf("L2TokenMinter: Exception in mint event callback: %s\n", e.what());
        }
    }
}

uint256 L2TokenMinter::GenerateL2TxHash(
    const uint256& l1TxHash,
    const uint160& recipient,
    CAmount amount,
    uint64_t blockNumber) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("L2MINT");
    ss << l1TxHash;
    ss << recipient;
    ss << amount;
    ss << blockNumber;
    return ss.GetHash();
}

} // namespace l2
