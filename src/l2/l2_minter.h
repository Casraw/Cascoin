// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_MINTER_H
#define CASCOIN_L2_MINTER_H

/**
 * @file l2_minter.h
 * @brief L2 Token Minter for Burn-and-Mint Token Model
 * 
 * This file implements the L2TokenMinter class that handles minting L2 tokens
 * after burn consensus is reached. It ensures:
 * - 1:1 mint ratio (minted amount equals burned amount)
 * - Supply invariant (total supply equals sum of all balances)
 * - Atomic state updates
 * - Mint event emission
 * 
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.3
 */

#include <l2/burn_registry.h>
#include <l2/state_manager.h>
#include <l2/l2_common.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace l2 {

// ============================================================================
// MintResult Structure
// ============================================================================

/**
 * @brief Result of a mint operation
 * 
 * Contains success/failure status, error message if failed,
 * and details of the mint operation if successful.
 */
struct MintResult {
    /** Whether the mint operation succeeded */
    bool success;
    
    /** Error message if failed */
    std::string errorMessage;
    
    /** L2 transaction hash for the mint */
    uint256 l2TxHash;
    
    /** L2 block number where mint occurred */
    uint64_t l2BlockNumber;
    
    /** Amount of tokens minted */
    CAmount amountMinted;
    
    /** Default constructor */
    MintResult()
        : success(false)
        , l2BlockNumber(0)
        , amountMinted(0) {}
    
    /**
     * @brief Create a successful result
     * @param txHash L2 transaction hash
     * @param blockNum L2 block number
     * @param amount Amount minted
     * @return Successful MintResult
     */
    static MintResult Success(const uint256& txHash, uint64_t blockNum, CAmount amount) {
        MintResult result;
        result.success = true;
        result.l2TxHash = txHash;
        result.l2BlockNumber = blockNum;
        result.amountMinted = amount;
        return result;
    }
    
    /**
     * @brief Create a failure result
     * @param error Error message
     * @return Failed MintResult
     */
    static MintResult Failure(const std::string& error) {
        MintResult result;
        result.success = false;
        result.errorMessage = error;
        return result;
    }
};

// ============================================================================
// MintEvent Structure
// ============================================================================

/**
 * @brief Event emitted when tokens are minted
 * 
 * Provides an audit trail for all mint operations.
 * 
 * Requirements: 4.4
 */
struct MintEvent {
    /** L1 burn transaction hash that triggered the mint */
    uint256 l1TxHash;
    
    /** L2 recipient address */
    uint160 recipient;
    
    /** Amount minted */
    CAmount amount;
    
    /** L2 transaction hash */
    uint256 l2TxHash;
    
    /** L2 block number */
    uint64_t l2BlockNumber;
    
    /** Timestamp when mint occurred */
    uint64_t timestamp;
    
    /** Default constructor */
    MintEvent()
        : amount(0)
        , l2BlockNumber(0)
        , timestamp(0) {}
    
    /** Full constructor */
    MintEvent(const uint256& l1Hash, const uint160& recip, CAmount amt,
              const uint256& l2Hash, uint64_t blockNum, uint64_t ts)
        : l1TxHash(l1Hash)
        , recipient(recip)
        , amount(amt)
        , l2TxHash(l2Hash)
        , l2BlockNumber(blockNum)
        , timestamp(ts) {}
    
    /** Serialization support */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1TxHash);
        READWRITE(recipient);
        READWRITE(amount);
        READWRITE(l2TxHash);
        READWRITE(l2BlockNumber);
        READWRITE(timestamp);
    }
};

// ============================================================================
// L2TokenMinter Class
// ============================================================================

/**
 * @brief L2 Token Minter for Burn-and-Mint Model
 * 
 * Handles minting L2 tokens after burn consensus is reached.
 * Ensures 1:1 mint ratio and maintains supply invariant.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.3
 */
class L2TokenMinter {
public:
    /** Callback type for mint event notifications */
    using MintEventCallback = std::function<void(const MintEvent&)>;
    
    /**
     * @brief Construct an L2TokenMinter
     * @param stateManager Reference to the L2 state manager
     * @param burnRegistry Reference to the burn registry
     */
    L2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry);
    
    /**
     * @brief Destructor
     */
    ~L2TokenMinter();
    
    /**
     * @brief Mint tokens after consensus is reached
     * @param l1TxHash L1 burn transaction hash
     * @param recipient L2 recipient address
     * @param amount Amount to mint (must match burn amount)
     * @return MintResult with success/failure and details
     * 
     * This method:
     * 1. Verifies the burn hasn't already been processed
     * 2. Updates the recipient's balance atomically
     * 3. Records the burn in the registry
     * 4. Emits a MintEvent
     * 5. Updates total supply tracking
     * 
     * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5
     */
    MintResult MintTokens(
        const uint256& l1TxHash,
        const uint160& recipient,
        CAmount amount);
    
    /**
     * @brief Mint tokens with full burn record details
     * @param l1TxHash L1 burn transaction hash
     * @param l1BlockNumber L1 block number containing burn
     * @param l1BlockHash L1 block hash
     * @param recipient L2 recipient address
     * @param amount Amount to mint
     * @return MintResult with success/failure and details
     */
    MintResult MintTokensWithDetails(
        const uint256& l1TxHash,
        uint64_t l1BlockNumber,
        const uint256& l1BlockHash,
        const uint160& recipient,
        CAmount amount);
    
    /**
     * @brief Get the current L2 token total supply
     * @return Total supply in satoshis
     * 
     * Requirements: 8.1
     */
    CAmount GetTotalSupply() const;
    
    /**
     * @brief Verify the supply invariant
     * @return true if total supply equals sum of all balances
     * 
     * The supply invariant states that:
     * - Total L2 supply == Total CAS burned on L1
     * - Sum of all L2 balances == Total L2 supply
     * 
     * Requirements: 8.1, 8.3
     */
    bool VerifySupplyInvariant() const;
    
    /**
     * @brief Get balance for an address
     * @param address The L2 address
     * @return Balance in satoshis
     * 
     * Requirements: 4.5
     */
    CAmount GetBalance(const uint160& address) const;
    
    /**
     * @brief Get the total amount of CAS burned on L1
     * @return Total burned amount in satoshis
     */
    CAmount GetTotalBurnedL1() const;
    
    /**
     * @brief Get the total amount minted on L2
     * @return Total minted amount in satoshis
     */
    CAmount GetTotalMintedL2() const;
    
    /**
     * @brief Register a callback for mint events
     * @param callback Function to call when tokens are minted
     */
    void RegisterMintEventCallback(MintEventCallback callback);
    
    /**
     * @brief Get all mint events
     * @return Vector of all mint events
     */
    std::vector<MintEvent> GetMintEvents() const;
    
    /**
     * @brief Get mint events for a specific recipient
     * @param recipient The L2 address
     * @return Vector of mint events for that address
     */
    std::vector<MintEvent> GetMintEventsForAddress(const uint160& recipient) const;
    
    /**
     * @brief Get mint event by L1 transaction hash
     * @param l1TxHash The L1 burn transaction hash
     * @return MintEvent if found, nullopt otherwise
     */
    std::optional<MintEvent> GetMintEventByL1TxHash(const uint256& l1TxHash) const;
    
    /**
     * @brief Clear all state (for testing)
     */
    void Clear();
    
    /**
     * @brief Get the current L2 block number
     * @return Current block number
     */
    uint64_t GetCurrentBlockNumber() const;
    
    /**
     * @brief Set the current L2 block number (for testing)
     * @param blockNumber Block number to set
     */
    void SetCurrentBlockNumber(uint64_t blockNumber);

private:
    /** Reference to L2 state manager */
    L2StateManager& stateManager_;
    
    /** Reference to burn registry */
    BurnRegistry& burnRegistry_;
    
    /** Total supply tracking */
    CAmount totalSupply_;
    
    /** Total minted on L2 */
    CAmount totalMinted_;
    
    /** Current L2 block number */
    uint64_t currentBlockNumber_;
    
    /** Mint events */
    std::vector<MintEvent> mintEvents_;
    
    /** Index: L1 TX hash -> mint event index */
    std::map<uint256, size_t> mintEventsByL1TxHash_;
    
    /** Index: recipient -> list of mint event indices */
    std::map<uint160, std::vector<size_t>> mintEventsByRecipient_;
    
    /** Mint event callbacks */
    std::vector<MintEventCallback> mintEventCallbacks_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_minter_;
    
    /**
     * @brief Update state atomically
     * @param recipient Recipient address
     * @param amount Amount to add to balance
     * @return true if update succeeded
     */
    bool UpdateState(const uint160& recipient, CAmount amount);
    
    /**
     * @brief Emit a mint event
     * @param event The mint event to emit
     */
    void EmitMintEvent(const MintEvent& event);
    
    /**
     * @brief Generate L2 transaction hash for mint
     * @param l1TxHash L1 burn transaction hash
     * @param recipient Recipient address
     * @param amount Amount minted
     * @param blockNumber L2 block number
     * @return Generated L2 transaction hash
     */
    uint256 GenerateL2TxHash(
        const uint256& l1TxHash,
        const uint160& recipient,
        CAmount amount,
        uint64_t blockNumber) const;
};

/**
 * @brief Global L2 token minter instance getter
 * @return Reference to the global L2TokenMinter
 */
L2TokenMinter& GetL2TokenMinter();

/**
 * @brief Initialize the global L2 token minter
 * @param stateManager Reference to state manager
 * @param burnRegistry Reference to burn registry
 */
void InitL2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry);

/**
 * @brief Check if L2 token minter is initialized
 * @return true if initialized
 */
bool IsL2TokenMinterInitialized();

} // namespace l2

#endif // CASCOIN_L2_MINTER_H
