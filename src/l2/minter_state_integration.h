// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_MINTER_STATE_INTEGRATION_H
#define CASCOIN_L2_MINTER_STATE_INTEGRATION_H

/**
 * @file minter_state_integration.h
 * @brief Integration layer between L2TokenMinter and StateManager
 * 
 * This file implements the integration between the L2TokenMinter and the
 * StateManager, ensuring atomic balance updates and proper event emission
 * for mint operations.
 * 
 * Requirements: 4.4, 4.5
 */

#include <l2/l2_minter.h>
#include <l2/mint_consensus.h>
#include <l2/state_manager.h>
#include <l2/burn_registry.h>
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
// MintStateEvent Structure
// ============================================================================

/**
 * @brief Event emitted when state is updated due to minting
 */
struct MintStateEvent {
    /** Type of state change */
    enum class Type {
        BALANCE_INCREASED,      // Balance was increased
        SUPPLY_INCREASED,       // Total supply was increased
        MINT_COMPLETED          // Full mint operation completed
    };
    
    /** Event type */
    Type type;
    
    /** L1 burn transaction hash */
    uint256 l1TxHash;
    
    /** Affected address */
    uint160 address;
    
    /** Amount changed */
    CAmount amount;
    
    /** New balance (for BALANCE_INCREASED) */
    CAmount newBalance;
    
    /** New total supply (for SUPPLY_INCREASED) */
    CAmount newTotalSupply;
    
    /** L2 block number */
    uint64_t blockNumber;
    
    /** Timestamp */
    uint64_t timestamp;
    
    MintStateEvent()
        : type(Type::MINT_COMPLETED)
        , amount(0)
        , newBalance(0)
        , newTotalSupply(0)
        , blockNumber(0)
        , timestamp(0) {}
};

// ============================================================================
// MinterStateIntegration Class
// ============================================================================

/**
 * @brief Integration layer for L2TokenMinter and StateManager
 * 
 * This class coordinates between the L2TokenMinter and StateManager to ensure:
 * - Atomic balance updates
 * - Proper event emission
 * - State consistency
 * - Consensus-triggered minting
 * 
 * Requirements: 4.4, 4.5
 */
class MinterStateIntegration {
public:
    /** Callback type for state event notifications */
    using StateEventCallback = std::function<void(const MintStateEvent&)>;
    
    /**
     * @brief Construct a MinterStateIntegration
     * @param minter Reference to the L2 token minter
     * @param stateManager Reference to the state manager
     * @param consensusManager Reference to the mint consensus manager
     */
    MinterStateIntegration(
        L2TokenMinter& minter,
        L2StateManager& stateManager,
        MintConsensusManager& consensusManager);
    
    /**
     * @brief Destructor
     */
    ~MinterStateIntegration();
    
    /**
     * @brief Initialize the integration
     * 
     * Sets up callbacks between components and registers for
     * consensus notifications.
     * 
     * @return true if initialization succeeded
     */
    bool Initialize();
    
    /**
     * @brief Shutdown the integration
     */
    void Shutdown();
    
    /**
     * @brief Handle consensus reached for a burn
     * 
     * Called when the consensus manager reaches 2/3 consensus for a burn.
     * Triggers the minting process with atomic state updates.
     * 
     * @param state The consensus state that reached consensus
     * @return true if minting succeeded
     * 
     * Requirements: 4.4, 4.5
     */
    bool HandleConsensusReached(const MintConsensusState& state);
    
    /**
     * @brief Execute mint with atomic state update
     * 
     * Performs the mint operation with atomic balance update in the
     * state manager. If any step fails, the entire operation is rolled back.
     * 
     * @param l1TxHash L1 burn transaction hash
     * @param l1BlockNumber L1 block number
     * @param l1BlockHash L1 block hash
     * @param recipient L2 recipient address
     * @param amount Amount to mint
     * @return MintResult with success/failure and details
     * 
     * Requirements: 4.4, 4.5
     */
    MintResult ExecuteAtomicMint(
        const uint256& l1TxHash,
        uint64_t l1BlockNumber,
        const uint256& l1BlockHash,
        const uint160& recipient,
        CAmount amount);
    
    /**
     * @brief Verify state consistency after mint
     * 
     * Checks that the state is consistent after a mint operation:
     * - Balance was correctly updated
     * - Total supply matches sum of balances
     * - Burn registry is updated
     * 
     * @param l1TxHash L1 burn transaction hash
     * @param recipient L2 recipient address
     * @param amount Amount that was minted
     * @return true if state is consistent
     */
    bool VerifyStateConsistency(
        const uint256& l1TxHash,
        const uint160& recipient,
        CAmount amount);
    
    /**
     * @brief Register callback for state events
     * @param callback Function to call when state changes
     */
    void RegisterStateEventCallback(StateEventCallback callback);
    
    /**
     * @brief Get the number of successful mints
     * @return Count of successful mints
     */
    uint64_t GetSuccessfulMintCount() const { return successfulMints_.load(); }
    
    /**
     * @brief Get the number of failed mints
     * @return Count of failed mints
     */
    uint64_t GetFailedMintCount() const { return failedMints_.load(); }
    
    /**
     * @brief Check if integration is initialized
     * @return true if initialized
     */
    bool IsInitialized() const { return isInitialized_.load(); }

private:
    /** Reference to L2 token minter */
    L2TokenMinter& minter_;
    
    /** Reference to state manager */
    L2StateManager& stateManager_;
    
    /** Reference to mint consensus manager */
    MintConsensusManager& consensusManager_;
    
    /** State event callbacks */
    std::vector<StateEventCallback> stateEventCallbacks_;
    
    /** Whether integration is initialized */
    std::atomic<bool> isInitialized_;
    
    /** Count of successful mints */
    std::atomic<uint64_t> successfulMints_;
    
    /** Count of failed mints */
    std::atomic<uint64_t> failedMints_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_integration_;
    
    /**
     * @brief Emit a state event
     * @param event The event to emit
     */
    void EmitStateEvent(const MintStateEvent& event);
    
    /**
     * @brief Create balance increased event
     * @param l1TxHash L1 transaction hash
     * @param address Affected address
     * @param amount Amount increased
     * @param newBalance New balance
     * @param blockNumber Block number
     * @return The event
     */
    MintStateEvent CreateBalanceIncreasedEvent(
        const uint256& l1TxHash,
        const uint160& address,
        CAmount amount,
        CAmount newBalance,
        uint64_t blockNumber);
    
    /**
     * @brief Create supply increased event
     * @param l1TxHash L1 transaction hash
     * @param amount Amount increased
     * @param newTotalSupply New total supply
     * @param blockNumber Block number
     * @return The event
     */
    MintStateEvent CreateSupplyIncreasedEvent(
        const uint256& l1TxHash,
        CAmount amount,
        CAmount newTotalSupply,
        uint64_t blockNumber);
    
    /**
     * @brief Create mint completed event
     * @param l1TxHash L1 transaction hash
     * @param address Recipient address
     * @param amount Amount minted
     * @param blockNumber Block number
     * @return The event
     */
    MintStateEvent CreateMintCompletedEvent(
        const uint256& l1TxHash,
        const uint160& address,
        CAmount amount,
        uint64_t blockNumber);
};

/**
 * @brief Global minter state integration instance getter
 * @return Reference to the global MinterStateIntegration
 */
MinterStateIntegration& GetMinterStateIntegration();

/**
 * @brief Initialize the global minter state integration
 * @param minter Reference to L2 token minter
 * @param stateManager Reference to state manager
 * @param consensusManager Reference to mint consensus manager
 */
void InitMinterStateIntegration(
    L2TokenMinter& minter,
    L2StateManager& stateManager,
    MintConsensusManager& consensusManager);

/**
 * @brief Check if minter state integration is initialized
 * @return true if initialized
 */
bool IsMinterStateIntegrationInitialized();

} // namespace l2

#endif // CASCOIN_L2_MINTER_STATE_INTEGRATION_H
