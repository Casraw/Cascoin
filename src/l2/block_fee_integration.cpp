// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file block_fee_integration.cpp
 * @brief Implementation of FeeDistributor integration with block production
 * 
 * Requirements: 6.3, 6.4
 */

#include <l2/block_fee_integration.h>
#include <l2/account_state.h>
#include <util.h>

#include <chrono>
#include <memory>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<BlockFeeIntegration> g_blockFeeIntegration;
static CCriticalSection cs_blockFeeIntegrationInit;

BlockFeeIntegration& GetBlockFeeIntegration()
{
    LOCK(cs_blockFeeIntegrationInit);
    if (!g_blockFeeIntegration) {
        throw std::runtime_error("BlockFeeIntegration not initialized");
    }
    return *g_blockFeeIntegration;
}

void InitBlockFeeIntegration(FeeDistributor& feeDistributor, L2StateManager& stateManager)
{
    LOCK(cs_blockFeeIntegrationInit);
    g_blockFeeIntegration = std::make_unique<BlockFeeIntegration>(feeDistributor, stateManager);
    g_blockFeeIntegration->Initialize();
    LogPrintf("BlockFeeIntegration initialized\n");
}

bool IsBlockFeeIntegrationInitialized()
{
    LOCK(cs_blockFeeIntegrationInit);
    return g_blockFeeIntegration != nullptr && g_blockFeeIntegration->IsInitialized();
}

// ============================================================================
// BlockFeeIntegration Implementation
// ============================================================================

BlockFeeIntegration::BlockFeeIntegration(FeeDistributor& feeDistributor, L2StateManager& stateManager)
    : feeDistributor_(feeDistributor)
    , stateManager_(stateManager)
    , isInitialized_(false)
    , totalFeesDistributed_(0)
    , blocksProcessed_(0)
{
}

BlockFeeIntegration::~BlockFeeIntegration()
{
    Shutdown();
}

bool BlockFeeIntegration::Initialize()
{
    LOCK(cs_integration_);
    
    if (isInitialized_.load()) {
        return true;  // Already initialized
    }
    
    isInitialized_.store(true);
    
    LogPrintf("BlockFeeIntegration: Initialized\n");
    return true;
}

void BlockFeeIntegration::Shutdown()
{
    LOCK(cs_integration_);
    
    isInitialized_.store(false);
    
    LogPrintf("BlockFeeIntegration: Shutdown complete\n");
}

bool BlockFeeIntegration::OnBlockFinalization(const L2Block& block)
{
    if (!isInitialized_.load()) {
        LogPrintf("BlockFeeIntegration: Not initialized\n");
        return false;
    }
    
    LogPrint(BCLog::L2, "BlockFeeIntegration: Processing block %u finalization\n",
        block.GetBlockNumber());
    
    // Process fees for the block
    CAmount totalFees = ProcessBlockFees(block);
    
    // Update statistics
    totalFeesDistributed_ += totalFees;
    ++blocksProcessed_;
    
    // Emit event
    EmitFeeEvent(CreateFeeEvent(block, totalFees));
    
    LogPrint(BCLog::L2, "BlockFeeIntegration: Distributed %lld fees for block %u\n",
        totalFees, block.GetBlockNumber());
    
    return true;
}

CAmount BlockFeeIntegration::ProcessBlockFees(const L2Block& block)
{
    LOCK(cs_integration_);
    
    // Calculate total fees from transactions
    CAmount totalFees = feeDistributor_.CalculateBlockFees(block.transactions);
    
    // If no fees, nothing to distribute
    // Requirement 6.5: If a block has no transactions, sequencer gets no rewards
    if (totalFees == 0) {
        LogPrint(BCLog::L2, "BlockFeeIntegration: No fees in block %u\n",
            block.GetBlockNumber());
        return 0;
    }
    
    // Distribute fees through the fee distributor
    bool distributed = feeDistributor_.DistributeBlockFees(
        block.GetBlockNumber(),
        block.GetSequencer(),
        block.transactions);
    
    if (!distributed) {
        LogPrintf("BlockFeeIntegration: Failed to distribute fees for block %u\n",
            block.GetBlockNumber());
        return 0;
    }
    
    // Credit fees to sequencer's balance in state manager
    if (!CreditFeesToSequencer(block.GetSequencer(), totalFees, block.GetBlockNumber())) {
        LogPrintf("BlockFeeIntegration: Failed to credit fees to sequencer for block %u\n",
            block.GetBlockNumber());
        return 0;
    }
    
    return totalFees;
}

bool BlockFeeIntegration::ValidateFeeDistribution(const L2Block& block)
{
    LOCK(cs_integration_);
    
    // Calculate expected fees
    CAmount expectedFees = feeDistributor_.CalculateBlockFees(block.transactions);
    
    // Get the fee distribution record
    auto feeHistory = feeDistributor_.GetFeeHistory(
        block.GetSequencer(),
        block.GetBlockNumber(),
        block.GetBlockNumber());
    
    if (feeHistory.empty()) {
        // No fee distribution record - this is okay if there were no transactions
        if (block.transactions.empty()) {
            return true;
        }
        LogPrint(BCLog::L2, "BlockFeeIntegration: No fee distribution record for block %u\n",
            block.GetBlockNumber());
        return false;
    }
    
    const BlockFeeDistribution& dist = feeHistory[0];
    
    // Verify the distributed amount matches expected
    if (dist.totalFees != expectedFees) {
        LogPrintf("BlockFeeIntegration: Fee mismatch for block %u: expected %lld, got %lld\n",
            block.GetBlockNumber(), expectedFees, dist.totalFees);
        return false;
    }
    
    // Verify transaction count matches
    if (dist.transactionCount != block.transactions.size()) {
        LogPrintf("BlockFeeIntegration: Transaction count mismatch for block %u\n",
            block.GetBlockNumber());
        return false;
    }
    
    return true;
}

bool BlockFeeIntegration::CreditFeesToSequencer(const uint160& sequencer, CAmount amount, uint64_t blockNumber)
{
    if (amount <= 0) {
        return true;  // Nothing to credit
    }
    
    // Convert address to key for state manager
    uint256 key = AddressToKey(sequencer);
    
    // Get current account state
    AccountState state = stateManager_.GetAccountState(key);
    
    // Add fees to balance
    state.balance += amount;
    state.lastActivity = blockNumber;
    
    // Update state
    stateManager_.SetAccountState(key, state);
    
    LogPrint(BCLog::L2, "BlockFeeIntegration: Credited %lld fees to sequencer %s\n",
        amount, sequencer.ToString().substr(0, 16));
    
    return true;
}

void BlockFeeIntegration::RegisterFeeDistributedCallback(FeeDistributedCallback callback)
{
    LOCK(cs_integration_);
    feeDistributedCallbacks_.push_back(std::move(callback));
}

// ============================================================================
// Private Methods
// ============================================================================

void BlockFeeIntegration::EmitFeeEvent(const BlockFeeEvent& event)
{
    // Make a copy of callbacks to avoid holding lock during callbacks
    std::vector<FeeDistributedCallback> callbacks;
    {
        LOCK(cs_integration_);
        callbacks = feeDistributedCallbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LogPrintf("BlockFeeIntegration: Exception in fee distributed callback: %s\n", e.what());
        }
    }
}

BlockFeeEvent BlockFeeIntegration::CreateFeeEvent(const L2Block& block, CAmount totalFees)
{
    BlockFeeEvent event;
    event.blockNumber = block.GetBlockNumber();
    event.blockHash = block.GetHash();
    event.sequencer = block.GetSequencer();
    event.totalFees = totalFees;
    event.transactionCount = static_cast<uint32_t>(block.transactions.size());
    event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return event;
}

} // namespace l2
