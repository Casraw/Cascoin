// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BLOCK_FEE_INTEGRATION_H
#define CASCOIN_L2_BLOCK_FEE_INTEGRATION_H

/**
 * @file block_fee_integration.h
 * @brief Integration of FeeDistributor with L2 block production
 * 
 * This file implements the integration between the FeeDistributor and
 * L2 block production, ensuring fees are distributed to block producers
 * during block finalization.
 * 
 * Requirements: 6.3, 6.4
 */

#include <l2/fee_distributor.h>
#include <l2/l2_block.h>
#include <l2/state_manager.h>
#include <l2/l2_common.h>
#include <uint256.h>
#include <sync.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace l2 {

// ============================================================================
// BlockFeeEvent Structure
// ============================================================================

/**
 * @brief Event emitted when block fees are distributed
 */
struct BlockFeeEvent {
    /** Block number */
    uint64_t blockNumber;
    
    /** Block hash */
    uint256 blockHash;
    
    /** Sequencer (block producer) address */
    uint160 sequencer;
    
    /** Total fees collected */
    CAmount totalFees;
    
    /** Number of transactions */
    uint32_t transactionCount;
    
    /** Timestamp */
    uint64_t timestamp;
    
    BlockFeeEvent()
        : blockNumber(0)
        , totalFees(0)
        , transactionCount(0)
        , timestamp(0) {}
};

// ============================================================================
// BlockFeeIntegration Class
// ============================================================================

/**
 * @brief Integration layer for FeeDistributor and block production
 * 
 * This class hooks into block finalization to distribute fees to the
 * block producer. It ensures that sequencer rewards come exclusively
 * from transaction fees (no minting).
 * 
 * Requirements: 6.3, 6.4
 */
class BlockFeeIntegration {
public:
    /** Callback type for fee distribution notifications */
    using FeeDistributedCallback = std::function<void(const BlockFeeEvent&)>;
    
    /**
     * @brief Construct a BlockFeeIntegration
     * @param feeDistributor Reference to the fee distributor
     * @param stateManager Reference to the state manager
     */
    BlockFeeIntegration(FeeDistributor& feeDistributor, L2StateManager& stateManager);
    
    /**
     * @brief Destructor
     */
    ~BlockFeeIntegration();
    
    /**
     * @brief Initialize the integration
     * @return true if initialization succeeded
     */
    bool Initialize();
    
    /**
     * @brief Shutdown the integration
     */
    void Shutdown();
    
    /**
     * @brief Hook called when a block is being finalized
     * 
     * This is the main integration point. Called during block finalization
     * to distribute fees to the block producer.
     * 
     * @param block The block being finalized
     * @return true if fee distribution succeeded
     * 
     * Requirements: 6.3, 6.4
     */
    bool OnBlockFinalization(const L2Block& block);
    
    /**
     * @brief Process fees for a block
     * 
     * Calculates and distributes fees from all transactions in the block
     * to the block producer.
     * 
     * @param block The block to process
     * @return Total fees distributed
     * 
     * Requirements: 6.3
     */
    CAmount ProcessBlockFees(const L2Block& block);
    
    /**
     * @brief Validate fee distribution for a block
     * 
     * Verifies that the fee distribution in a block is correct:
     * - Sequencer received correct fee amount
     * - No unauthorized minting occurred
     * 
     * @param block The block to validate
     * @return true if fee distribution is valid
     * 
     * Requirements: 6.4
     */
    bool ValidateFeeDistribution(const L2Block& block);
    
    /**
     * @brief Credit fees to sequencer's balance
     * 
     * Updates the sequencer's balance in the state manager with
     * the collected fees.
     * 
     * @param sequencer Sequencer address
     * @param amount Fee amount to credit
     * @param blockNumber Block number
     * @return true if credit succeeded
     */
    bool CreditFeesToSequencer(const uint160& sequencer, CAmount amount, uint64_t blockNumber);
    
    /**
     * @brief Register callback for fee distribution events
     * @param callback Function to call when fees are distributed
     */
    void RegisterFeeDistributedCallback(FeeDistributedCallback callback);
    
    /**
     * @brief Get total fees distributed
     * @return Total fees distributed across all blocks
     */
    CAmount GetTotalFeesDistributed() const { return totalFeesDistributed_.load(); }
    
    /**
     * @brief Get number of blocks processed
     * @return Count of blocks processed
     */
    uint64_t GetBlocksProcessed() const { return blocksProcessed_.load(); }
    
    /**
     * @brief Check if integration is initialized
     * @return true if initialized
     */
    bool IsInitialized() const { return isInitialized_.load(); }

private:
    /** Reference to fee distributor */
    FeeDistributor& feeDistributor_;
    
    /** Reference to state manager */
    L2StateManager& stateManager_;
    
    /** Fee distributed callbacks */
    std::vector<FeeDistributedCallback> feeDistributedCallbacks_;
    
    /** Whether integration is initialized */
    std::atomic<bool> isInitialized_;
    
    /** Total fees distributed */
    std::atomic<CAmount> totalFeesDistributed_;
    
    /** Number of blocks processed */
    std::atomic<uint64_t> blocksProcessed_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_integration_;
    
    /**
     * @brief Emit a fee distribution event
     * @param event The event to emit
     */
    void EmitFeeEvent(const BlockFeeEvent& event);
    
    /**
     * @brief Create fee event from block
     * @param block The block
     * @param totalFees Total fees collected
     * @return The event
     */
    BlockFeeEvent CreateFeeEvent(const L2Block& block, CAmount totalFees);
};

/**
 * @brief Global block fee integration instance getter
 * @return Reference to the global BlockFeeIntegration
 */
BlockFeeIntegration& GetBlockFeeIntegration();

/**
 * @brief Initialize the global block fee integration
 * @param feeDistributor Reference to fee distributor
 * @param stateManager Reference to state manager
 */
void InitBlockFeeIntegration(FeeDistributor& feeDistributor, L2StateManager& stateManager);

/**
 * @brief Check if block fee integration is initialized
 * @return true if initialized
 */
bool IsBlockFeeIntegrationInitialized();

} // namespace l2

#endif // CASCOIN_L2_BLOCK_FEE_INTEGRATION_H
