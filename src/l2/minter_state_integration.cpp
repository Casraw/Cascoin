// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file minter_state_integration.cpp
 * @brief Implementation of integration layer between L2TokenMinter and StateManager
 * 
 * Requirements: 4.4, 4.5
 */

#include <l2/minter_state_integration.h>
#include <util.h>

#include <chrono>
#include <memory>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<MinterStateIntegration> g_minterStateIntegration;
static CCriticalSection cs_minterStateIntegrationInit;

MinterStateIntegration& GetMinterStateIntegration()
{
    LOCK(cs_minterStateIntegrationInit);
    if (!g_minterStateIntegration) {
        throw std::runtime_error("MinterStateIntegration not initialized");
    }
    return *g_minterStateIntegration;
}

void InitMinterStateIntegration(
    L2TokenMinter& minter,
    L2StateManager& stateManager,
    MintConsensusManager& consensusManager)
{
    LOCK(cs_minterStateIntegrationInit);
    g_minterStateIntegration = std::make_unique<MinterStateIntegration>(
        minter, stateManager, consensusManager);
    g_minterStateIntegration->Initialize();
    LogPrintf("MinterStateIntegration initialized\n");
}

bool IsMinterStateIntegrationInitialized()
{
    LOCK(cs_minterStateIntegrationInit);
    return g_minterStateIntegration != nullptr && g_minterStateIntegration->IsInitialized();
}

// ============================================================================
// MinterStateIntegration Implementation
// ============================================================================

MinterStateIntegration::MinterStateIntegration(
    L2TokenMinter& minter,
    L2StateManager& stateManager,
    MintConsensusManager& consensusManager)
    : minter_(minter)
    , stateManager_(stateManager)
    , consensusManager_(consensusManager)
    , isInitialized_(false)
    , successfulMints_(0)
    , failedMints_(0)
{
}

MinterStateIntegration::~MinterStateIntegration()
{
    Shutdown();
}

bool MinterStateIntegration::Initialize()
{
    LOCK(cs_integration_);
    
    if (isInitialized_.load()) {
        return true;  // Already initialized
    }
    
    // Register for consensus reached notifications
    consensusManager_.RegisterConsensusReachedCallback(
        [this](const MintConsensusState& state) {
            HandleConsensusReached(state);
        });
    
    isInitialized_.store(true);
    
    LogPrintf("MinterStateIntegration: Initialized and registered for consensus notifications\n");
    return true;
}

void MinterStateIntegration::Shutdown()
{
    LOCK(cs_integration_);
    
    isInitialized_.store(false);
    
    LogPrintf("MinterStateIntegration: Shutdown complete\n");
}

bool MinterStateIntegration::HandleConsensusReached(const MintConsensusState& state)
{
    LogPrint(BCLog::L2, "MinterStateIntegration: Handling consensus reached for %s\n",
        state.l1TxHash.ToString().substr(0, 16));
    
    // Get the first confirmation to extract recipient and amount
    if (state.confirmations.empty()) {
        LogPrintf("MinterStateIntegration: No confirmations in consensus state\n");
        return false;
    }
    
    const MintConfirmation& firstConf = state.confirmations.begin()->second;
    
    // Execute atomic mint
    MintResult result = ExecuteAtomicMint(
        state.l1TxHash,
        0,  // L1 block number (would be from validation result)
        uint256(),  // L1 block hash (would be from validation result)
        firstConf.l2Recipient,
        firstConf.amount);
    
    if (result.success) {
        // Mark as minted in consensus manager
        consensusManager_.MarkAsMinted(state.l1TxHash);
        
        ++successfulMints_;
        
        LogPrintf("MinterStateIntegration: Successfully minted %lld to %s for burn %s\n",
            firstConf.amount,
            firstConf.l2Recipient.ToString().substr(0, 16),
            state.l1TxHash.ToString().substr(0, 16));
        
        return true;
    } else {
        ++failedMints_;
        
        LogPrintf("MinterStateIntegration: Failed to mint for burn %s: %s\n",
            state.l1TxHash.ToString().substr(0, 16),
            result.errorMessage);
        
        return false;
    }
}

MintResult MinterStateIntegration::ExecuteAtomicMint(
    const uint256& l1TxHash,
    uint64_t l1BlockNumber,
    const uint256& l1BlockHash,
    const uint160& recipient,
    CAmount amount)
{
    LOCK(cs_integration_);
    
    // Get current state for potential rollback
    uint256 originalStateRoot = stateManager_.GetStateRoot();
    CAmount originalBalance = minter_.GetBalance(recipient);
    CAmount originalSupply = minter_.GetTotalSupply();
    
    // Get current block number
    uint64_t blockNumber = stateManager_.GetBlockNumber();
    
    // Execute the mint through the minter
    // The minter will update the state manager internally
    MintResult result = minter_.MintTokensWithDetails(
        l1TxHash,
        l1BlockNumber,
        l1BlockHash,
        recipient,
        amount);
    
    if (!result.success) {
        // Mint failed, no need to rollback as minter handles this
        return result;
    }
    
    // Verify state consistency
    if (!VerifyStateConsistency(l1TxHash, recipient, amount)) {
        // State inconsistency detected - this is a critical error
        LogPrintf("MinterStateIntegration: CRITICAL - State inconsistency after mint!\n");
        
        // Attempt to rollback
        stateManager_.RevertToStateRoot(originalStateRoot);
        
        return MintResult::Failure("State inconsistency after mint");
    }
    
    // Emit state events
    CAmount newBalance = minter_.GetBalance(recipient);
    CAmount newSupply = minter_.GetTotalSupply();
    
    // Balance increased event
    EmitStateEvent(CreateBalanceIncreasedEvent(
        l1TxHash, recipient, amount, newBalance, blockNumber));
    
    // Supply increased event
    EmitStateEvent(CreateSupplyIncreasedEvent(
        l1TxHash, amount, newSupply, blockNumber));
    
    // Mint completed event
    EmitStateEvent(CreateMintCompletedEvent(
        l1TxHash, recipient, amount, blockNumber));
    
    return result;
}

bool MinterStateIntegration::VerifyStateConsistency(
    const uint256& l1TxHash,
    const uint160& recipient,
    CAmount amount)
{
    // Verify the supply invariant
    if (!minter_.VerifySupplyInvariant()) {
        LogPrintf("MinterStateIntegration: Supply invariant violated\n");
        return false;
    }
    
    // Verify the balance was updated correctly
    CAmount balance = minter_.GetBalance(recipient);
    if (balance < amount) {
        LogPrintf("MinterStateIntegration: Balance not updated correctly\n");
        return false;
    }
    
    return true;
}

void MinterStateIntegration::RegisterStateEventCallback(StateEventCallback callback)
{
    LOCK(cs_integration_);
    stateEventCallbacks_.push_back(std::move(callback));
}

// ============================================================================
// Private Methods
// ============================================================================

void MinterStateIntegration::EmitStateEvent(const MintStateEvent& event)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<StateEventCallback> callbacks;
    {
        LOCK(cs_integration_);
        callbacks = stateEventCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LogPrintf("MinterStateIntegration: Exception in state event callback: %s\n", e.what());
        }
    }
}

MintStateEvent MinterStateIntegration::CreateBalanceIncreasedEvent(
    const uint256& l1TxHash,
    const uint160& address,
    CAmount amount,
    CAmount newBalance,
    uint64_t blockNumber)
{
    MintStateEvent event;
    event.type = MintStateEvent::Type::BALANCE_INCREASED;
    event.l1TxHash = l1TxHash;
    event.address = address;
    event.amount = amount;
    event.newBalance = newBalance;
    event.blockNumber = blockNumber;
    event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return event;
}

MintStateEvent MinterStateIntegration::CreateSupplyIncreasedEvent(
    const uint256& l1TxHash,
    CAmount amount,
    CAmount newTotalSupply,
    uint64_t blockNumber)
{
    MintStateEvent event;
    event.type = MintStateEvent::Type::SUPPLY_INCREASED;
    event.l1TxHash = l1TxHash;
    event.amount = amount;
    event.newTotalSupply = newTotalSupply;
    event.blockNumber = blockNumber;
    event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return event;
}

MintStateEvent MinterStateIntegration::CreateMintCompletedEvent(
    const uint256& l1TxHash,
    const uint160& address,
    CAmount amount,
    uint64_t blockNumber)
{
    MintStateEvent event;
    event.type = MintStateEvent::Type::MINT_COMPLETED;
    event.l1TxHash = l1TxHash;
    event.address = address;
    event.amount = amount;
    event.blockNumber = blockNumber;
    event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return event;
}

} // namespace l2
