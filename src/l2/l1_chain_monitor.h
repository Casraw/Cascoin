// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_L1_CHAIN_MONITOR_H
#define CASCOIN_L2_L1_CHAIN_MONITOR_H

/**
 * @file l1_chain_monitor.h
 * @brief L1 Chain Monitor for detecting burn transactions
 * 
 * This file implements the L1ChainMonitor class that subscribes to new L1 blocks,
 * scans for OP_RETURN burn transactions, and triggers validation when burns are
 * detected. It integrates the BurnValidator with the L1 chain.
 * 
 * Requirements: 2.1, 2.2
 */

#include <l2/burn_validator.h>
#include <l2/burn_parser.h>
#include <l2/mint_consensus.h>
#include <l2/l2_common.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <sync.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>

namespace l2 {

// ============================================================================
// DetectedBurn Structure
// ============================================================================

/**
 * @brief Information about a detected burn transaction
 */
struct DetectedBurn {
    /** L1 transaction hash */
    uint256 l1TxHash;
    
    /** L1 block number */
    uint64_t l1BlockNumber;
    
    /** L1 block hash */
    uint256 l1BlockHash;
    
    /** Parsed burn data */
    BurnData burnData;
    
    /** Number of confirmations when detected */
    int confirmations;
    
    /** Timestamp when detected */
    uint64_t detectedTime;
    
    /** Whether validation has been triggered */
    bool validationTriggered;
    
    DetectedBurn()
        : l1BlockNumber(0)
        , confirmations(0)
        , detectedTime(0)
        , validationTriggered(false) {}
};

// ============================================================================
// L1ChainMonitor Class
// ============================================================================

/**
 * @brief Monitor for L1 chain to detect burn transactions
 * 
 * This class subscribes to new L1 blocks and scans them for OP_RETURN
 * burn transactions. When a valid burn is detected with sufficient
 * confirmations, it triggers the validation and consensus process.
 * 
 * Requirements: 2.1, 2.2
 */
class L1ChainMonitor {
public:
    /** Callback type for burn detection notifications */
    using BurnDetectedCallback = std::function<void(const DetectedBurn&)>;
    
    /** Callback type for validation triggered notifications */
    using ValidationTriggeredCallback = std::function<void(const uint256&, const BurnValidationResult&)>;
    
    /**
     * @brief Construct an L1ChainMonitor
     * @param chainId The L2 chain ID to monitor for
     * @param validator Reference to the burn validator
     * @param consensusManager Reference to the mint consensus manager
     */
    L1ChainMonitor(
        uint32_t chainId,
        BurnValidator& validator,
        MintConsensusManager& consensusManager);
    
    /**
     * @brief Destructor - stops monitoring
     */
    ~L1ChainMonitor();
    
    /**
     * @brief Start monitoring L1 chain
     * @return true if monitoring started successfully
     */
    bool Start();
    
    /**
     * @brief Stop monitoring L1 chain
     */
    void Stop();
    
    /**
     * @brief Check if monitoring is active
     * @return true if monitoring is running
     */
    bool IsRunning() const { return isRunning_.load(); }
    
    /**
     * @brief Process a new L1 block
     * 
     * Called when a new L1 block is received. Scans all transactions
     * for OP_RETURN burn outputs and processes them.
     * 
     * @param block The new L1 block
     * @param blockHeight The block height
     * @param blockHash The block hash
     * 
     * Requirements: 2.1
     */
    void ProcessNewBlock(const CBlock& block, uint64_t blockHeight, const uint256& blockHash);
    
    /**
     * @brief Process a single transaction for burn detection
     * 
     * @param tx The transaction to check
     * @param blockHeight The block height containing the transaction
     * @param blockHash The block hash
     * @return true if a valid burn was detected
     */
    bool ProcessTransaction(const CTransaction& tx, uint64_t blockHeight, const uint256& blockHash);
    
    /**
     * @brief Check pending burns for sufficient confirmations
     * 
     * Called periodically to check if pending burns have reached
     * the required confirmation count and trigger validation.
     * 
     * Requirements: 2.2
     */
    void CheckPendingBurns();
    
    /**
     * @brief Handle L1 chain reorganization
     * 
     * Called when an L1 reorg is detected. Removes burns from
     * reverted blocks and re-scans the new chain.
     * 
     * @param reorgFromHeight The height from which reorg occurred
     */
    void HandleReorg(uint64_t reorgFromHeight);
    
    /**
     * @brief Get all detected burns
     * @return Vector of detected burns
     */
    std::vector<DetectedBurn> GetDetectedBurns() const;
    
    /**
     * @brief Get pending burns (waiting for confirmations)
     * @return Vector of pending burns
     */
    std::vector<DetectedBurn> GetPendingBurns() const;
    
    /**
     * @brief Get the number of detected burns
     * @return Count of detected burns
     */
    size_t GetDetectedCount() const;
    
    /**
     * @brief Register callback for burn detection
     * @param callback Function to call when burn is detected
     */
    void RegisterBurnDetectedCallback(BurnDetectedCallback callback);
    
    /**
     * @brief Register callback for validation triggered
     * @param callback Function to call when validation is triggered
     */
    void RegisterValidationTriggeredCallback(ValidationTriggeredCallback callback);
    
    /**
     * @brief Get the L2 chain ID being monitored
     * @return Chain ID
     */
    uint32_t GetChainId() const { return chainId_; }
    
    /**
     * @brief Get the last processed L1 block height
     * @return Last processed block height
     */
    uint64_t GetLastProcessedHeight() const;
    
    /**
     * @brief Set the last processed L1 block height
     * @param height Block height
     */
    void SetLastProcessedHeight(uint64_t height);
    
    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

private:
    /** L2 chain ID to monitor for */
    uint32_t chainId_;
    
    /** Reference to burn validator */
    BurnValidator& validator_;
    
    /** Reference to mint consensus manager */
    MintConsensusManager& consensusManager_;
    
    /** Map of L1 TX hash -> detected burn */
    std::map<uint256, DetectedBurn> detectedBurns_;
    
    /** Set of pending burns (waiting for confirmations) */
    std::set<uint256> pendingBurns_;
    
    /** Last processed L1 block height */
    uint64_t lastProcessedHeight_;
    
    /** Burn detected callbacks */
    std::vector<BurnDetectedCallback> burnDetectedCallbacks_;
    
    /** Validation triggered callbacks */
    std::vector<ValidationTriggeredCallback> validationTriggeredCallbacks_;
    
    /** Whether monitoring is running */
    std::atomic<bool> isRunning_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_monitor_;
    
    /** Maximum burns to track */
    static constexpr size_t MAX_TRACKED_BURNS = 10000;
    
    /**
     * @brief Scan a transaction for burn outputs
     * @param tx The transaction to scan
     * @return Parsed burn data if found, nullopt otherwise
     */
    std::optional<BurnData> ScanForBurn(const CTransaction& tx);
    
    /**
     * @brief Trigger validation for a burn
     * @param l1TxHash The L1 transaction hash
     * @return true if validation was triggered
     */
    bool TriggerValidation(const uint256& l1TxHash);
    
    /**
     * @brief Submit confirmation to consensus manager
     * @param l1TxHash The L1 transaction hash
     * @param result The validation result
     * @return true if confirmation was submitted
     */
    bool SubmitConfirmation(const uint256& l1TxHash, const BurnValidationResult& result);
    
    /**
     * @brief Notify callbacks of burn detection
     * @param burn The detected burn
     */
    void NotifyBurnDetected(const DetectedBurn& burn);
    
    /**
     * @brief Notify callbacks of validation triggered
     * @param l1TxHash The L1 transaction hash
     * @param result The validation result
     */
    void NotifyValidationTriggered(const uint256& l1TxHash, const BurnValidationResult& result);
    
    /**
     * @brief Prune old tracked burns
     */
    void PruneOldBurns();
};

/**
 * @brief Global L1 chain monitor instance getter
 * @return Reference to the global L1ChainMonitor
 */
L1ChainMonitor& GetL1ChainMonitor();

/**
 * @brief Initialize the global L1 chain monitor
 * @param chainId The L2 chain ID
 * @param validator Reference to burn validator
 * @param consensusManager Reference to mint consensus manager
 */
void InitL1ChainMonitor(
    uint32_t chainId,
    BurnValidator& validator,
    MintConsensusManager& consensusManager);

/**
 * @brief Check if L1 chain monitor is initialized
 * @return true if initialized
 */
bool IsL1ChainMonitorInitialized();

} // namespace l2

#endif // CASCOIN_L2_L1_CHAIN_MONITOR_H
