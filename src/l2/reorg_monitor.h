// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_REORG_MONITOR_H
#define CASCOIN_L2_REORG_MONITOR_H

/**
 * @file reorg_monitor.h
 * @brief L1 Reorganization Monitor for Cascoin L2
 * 
 * This file implements the L1 reorg monitoring and recovery system for the
 * Cascoin L2 solution. It detects L1 chain reorganizations, reverts L2 state
 * to the last valid anchor, and replays affected transactions.
 * 
 * Requirements: 19.1, 19.2, 19.3, 19.4, 19.5, 19.6
 */

#include <l2/l2_common.h>
#include <l2/state_manager.h>
#include <uint256.h>
#include <sync.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <optional>

namespace l2 {

/** Default L1 finality depth (6 confirmations) */
static constexpr uint32_t DEFAULT_L1_FINALITY_DEPTH = 6;

/** Maximum reorg depth to handle (beyond this, manual intervention needed) */
static constexpr uint32_t MAX_REORG_DEPTH = 100;

/** Minimum blocks between anchor points */
static constexpr uint32_t MIN_ANCHOR_INTERVAL = 10;

/** Maximum number of L2 transaction logs to keep for replay */
static constexpr size_t MAX_TX_LOG_SIZE = 100000;

/**
 * @brief L1 Block information for tracking
 */
struct L1BlockInfo {
    /** L1 block number */
    uint64_t blockNumber;
    
    /** L1 block hash */
    uint256 blockHash;
    
    /** Previous block hash */
    uint256 prevBlockHash;
    
    /** Timestamp of the block */
    uint64_t timestamp;
    
    /** Number of confirmations */
    uint32_t confirmations;

    L1BlockInfo() : blockNumber(0), timestamp(0), confirmations(0) {}
    
    L1BlockInfo(uint64_t num, const uint256& hash, const uint256& prev, 
                uint64_t ts, uint32_t conf)
        : blockNumber(num), blockHash(hash), prevBlockHash(prev),
          timestamp(ts), confirmations(conf) {}

    bool operator==(const L1BlockInfo& other) const {
        return blockNumber == other.blockNumber && 
               blockHash == other.blockHash;
    }
    
    bool operator!=(const L1BlockInfo& other) const {
        return !(*this == other);
    }
};

/**
 * @brief L2 Anchor point on L1
 * 
 * Represents a point where L2 state was anchored to L1.
 */
struct L2AnchorPoint {
    /** L1 block number where anchor was submitted */
    uint64_t l1BlockNumber;
    
    /** L1 block hash */
    uint256 l1BlockHash;
    
    /** L2 block number at anchor time */
    uint64_t l2BlockNumber;
    
    /** L2 state root at anchor time */
    uint256 l2StateRoot;
    
    /** Batch hash submitted to L1 */
    uint256 batchHash;
    
    /** Timestamp of anchor */
    uint64_t timestamp;
    
    /** Whether this anchor is finalized (has enough L1 confirmations) */
    bool isFinalized;

    L2AnchorPoint() 
        : l1BlockNumber(0), l2BlockNumber(0), timestamp(0), isFinalized(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1BlockNumber);
        READWRITE(l1BlockHash);
        READWRITE(l2BlockNumber);
        READWRITE(l2StateRoot);
        READWRITE(batchHash);
        READWRITE(timestamp);
        READWRITE(isFinalized);
    }

    bool operator==(const L2AnchorPoint& other) const {
        return l1BlockNumber == other.l1BlockNumber &&
               l1BlockHash == other.l1BlockHash &&
               l2StateRoot == other.l2StateRoot;
    }
};

/**
 * @brief L2 Transaction log entry for replay
 * 
 * Stores transaction information needed for replay after reorg.
 * Requirement 19.6: Maintain L2 transaction logs for replay after reorg
 */
struct L2TxLogEntry {
    /** Transaction hash */
    uint256 txHash;
    
    /** Serialized transaction data (for replay) */
    std::vector<unsigned char> txData;
    
    /** L2 block number where tx was included */
    uint64_t l2BlockNumber;
    
    /** L1 anchor block at time of inclusion */
    uint64_t l1AnchorBlock;
    
    /** Timestamp of inclusion */
    uint64_t timestamp;
    
    /** Whether tx was successfully executed */
    bool wasSuccessful;
    
    /** Gas used by transaction */
    uint64_t gasUsed;

    L2TxLogEntry() 
        : l2BlockNumber(0), l1AnchorBlock(0), timestamp(0), 
          wasSuccessful(false), gasUsed(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(txData);
        READWRITE(l2BlockNumber);
        READWRITE(l1AnchorBlock);
        READWRITE(timestamp);
        READWRITE(wasSuccessful);
        READWRITE(gasUsed);
    }
    
    /** Set transaction from CTransaction */
    void SetTransaction(const CTransaction& tx) {
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << tx;
        txData.assign(ss.begin(), ss.end());
    }
    
    /** Get transaction as CTransaction */
    bool GetTransaction(CMutableTransaction& mtx) const {
        if (txData.empty()) return false;
        try {
            CDataStream ss(txData, SER_DISK, PROTOCOL_VERSION);
            ss >> mtx;
            return true;
        } catch (...) {
            return false;
        }
    }
};

/**
 * @brief Reorg detection result
 */
struct ReorgDetectionResult {
    /** Whether a reorg was detected */
    bool reorgDetected;
    
    /** Depth of the reorg (number of blocks reorganized) */
    uint32_t reorgDepth;
    
    /** L1 block number where chains diverged */
    uint64_t forkPoint;
    
    /** Hash of the fork point block */
    uint256 forkPointHash;
    
    /** Old chain tip before reorg */
    L1BlockInfo oldTip;
    
    /** New chain tip after reorg */
    L1BlockInfo newTip;
    
    /** Error message if detection failed */
    std::string error;

    ReorgDetectionResult() 
        : reorgDetected(false), reorgDepth(0), forkPoint(0) {}

    static ReorgDetectionResult NoReorg() {
        return ReorgDetectionResult();
    }

    static ReorgDetectionResult Detected(uint32_t depth, uint64_t fork,
                                         const uint256& forkHash,
                                         const L1BlockInfo& oldT,
                                         const L1BlockInfo& newT) {
        ReorgDetectionResult result;
        result.reorgDetected = true;
        result.reorgDepth = depth;
        result.forkPoint = fork;
        result.forkPointHash = forkHash;
        result.oldTip = oldT;
        result.newTip = newT;
        return result;
    }

    static ReorgDetectionResult Error(const std::string& err) {
        ReorgDetectionResult result;
        result.error = err;
        return result;
    }
};

/**
 * @brief Reorg recovery result
 */
struct ReorgRecoveryResult {
    /** Whether recovery was successful */
    bool success;
    
    /** L2 state root after recovery */
    uint256 newStateRoot;
    
    /** L2 block number after recovery */
    uint64_t newL2BlockNumber;
    
    /** Number of transactions replayed */
    size_t transactionsReplayed;
    
    /** Number of transactions that failed replay */
    size_t transactionsFailed;
    
    /** Transactions that were affected by the reorg */
    std::vector<uint256> affectedTransactions;
    
    /** Error message if recovery failed */
    std::string error;

    ReorgRecoveryResult() 
        : success(false), newL2BlockNumber(0), 
          transactionsReplayed(0), transactionsFailed(0) {}

    static ReorgRecoveryResult Success(const uint256& root, uint64_t blockNum,
                                       size_t replayed, size_t failed,
                                       const std::vector<uint256>& affected) {
        ReorgRecoveryResult result;
        result.success = true;
        result.newStateRoot = root;
        result.newL2BlockNumber = blockNum;
        result.transactionsReplayed = replayed;
        result.transactionsFailed = failed;
        result.affectedTransactions = affected;
        return result;
    }

    static ReorgRecoveryResult Failure(const std::string& err) {
        ReorgRecoveryResult result;
        result.success = false;
        result.error = err;
        return result;
    }
};

/**
 * @brief Callback type for reorg notifications
 */
using ReorgNotificationCallback = std::function<void(
    const ReorgDetectionResult& detection,
    const ReorgRecoveryResult& recovery)>;

/**
 * @brief L1 Reorganization Monitor
 * 
 * Monitors the L1 chain for reorganizations and handles L2 state recovery.
 * 
 * Key responsibilities:
 * - Detect L1 chain reorganizations (Requirement 19.1)
 * - Revert L2 state to last valid anchor (Requirement 19.2)
 * - Re-process L2 transactions after reorg recovery (Requirement 19.3)
 * - Notify users of affected transactions (Requirement 19.4)
 * - Wait for L1 finality before considering L2 state final (Requirement 19.5)
 * - Maintain transaction logs for replay (Requirement 19.6)
 * 
 * Thread-safe for concurrent access.
 */
class ReorgMonitor {
public:
    /**
     * @brief Construct a new Reorg Monitor
     * @param chainId L2 chain ID
     * @param stateManager Pointer to the L2 state manager
     * @param finalityDepth Number of L1 confirmations for finality
     */
    explicit ReorgMonitor(
        uint64_t chainId,
        std::shared_ptr<L2StateManager> stateManager = nullptr,
        uint32_t finalityDepth = DEFAULT_L1_FINALITY_DEPTH);

    ~ReorgMonitor();

    // =========================================================================
    // L1 Block Tracking (Requirement 19.1)
    // =========================================================================

    /**
     * @brief Process a new L1 block
     * @param blockInfo Information about the new L1 block
     * @return Reorg detection result
     * 
     * Requirement 19.1: Monitor L1 for chain reorganizations
     */
    ReorgDetectionResult ProcessL1Block(const L1BlockInfo& blockInfo);

    /**
     * @brief Check if a reorg has occurred since last check
     * @param currentTip Current L1 chain tip
     * @return Reorg detection result
     */
    ReorgDetectionResult CheckForReorg(const L1BlockInfo& currentTip);

    /**
     * @brief Get the current L1 chain tip being tracked
     * @return Current L1 block info
     */
    L1BlockInfo GetCurrentL1Tip() const;

    /**
     * @brief Get L1 block info by number
     * @param blockNumber L1 block number
     * @return Block info if found
     */
    std::optional<L1BlockInfo> GetL1Block(uint64_t blockNumber) const;

    // =========================================================================
    // State Reversion (Requirement 19.2)
    // =========================================================================

    /**
     * @brief Revert L2 state to last valid anchor point
     * @param forkPoint L1 block number where reorg occurred
     * @return true if reversion succeeded
     * 
     * Requirement 19.2: Revert L2 to last valid anchor when L1 reorg affects anchored state
     */
    bool RevertToLastValidAnchor(uint64_t forkPoint);

    /**
     * @brief Get the last valid anchor point before a given L1 block
     * @param beforeL1Block L1 block number
     * @return Anchor point if found
     */
    std::optional<L2AnchorPoint> GetLastValidAnchor(uint64_t beforeL1Block) const;

    /**
     * @brief Get all anchor points
     * @return Vector of anchor points
     */
    std::vector<L2AnchorPoint> GetAnchorPoints() const;

    // =========================================================================
    // Transaction Replay (Requirement 19.3)
    // =========================================================================

    /**
     * @brief Replay L2 transactions after reorg recovery
     * @param fromL2Block Starting L2 block number
     * @param toL2Block Ending L2 block number (inclusive)
     * @return Number of transactions successfully replayed
     * 
     * Requirement 19.3: Re-process L2 transactions after reorg recovery
     */
    size_t ReplayTransactions(uint64_t fromL2Block, uint64_t toL2Block);

    /**
     * @brief Get transactions that need replay after reorg
     * @param fromL2Block Starting L2 block number
     * @return Vector of transaction log entries
     */
    std::vector<L2TxLogEntry> GetTransactionsForReplay(uint64_t fromL2Block) const;

    // =========================================================================
    // Full Reorg Recovery
    // =========================================================================

    /**
     * @brief Handle a detected reorg - full recovery process
     * @param detection The reorg detection result
     * @return Recovery result
     * 
     * This performs the complete recovery:
     * 1. Revert to last valid anchor (Requirement 19.2)
     * 2. Replay transactions (Requirement 19.3)
     * 3. Notify affected users (Requirement 19.4)
     */
    ReorgRecoveryResult HandleReorg(const ReorgDetectionResult& detection);

    // =========================================================================
    // Anchor Point Management
    // =========================================================================

    /**
     * @brief Add a new anchor point
     * @param anchor The anchor point to add
     */
    void AddAnchorPoint(const L2AnchorPoint& anchor);

    /**
     * @brief Update anchor finalization status
     * @param l1BlockNumber L1 block number of the anchor
     * @param confirmations Current number of confirmations
     * 
     * Requirement 19.5: Wait for L1 finality before considering L2 state final
     */
    void UpdateAnchorFinalization(uint64_t l1BlockNumber, uint32_t confirmations);

    /**
     * @brief Check if an anchor is finalized
     * @param l1BlockNumber L1 block number of the anchor
     * @return true if anchor has enough confirmations
     */
    bool IsAnchorFinalized(uint64_t l1BlockNumber) const;

    /**
     * @brief Get the latest finalized anchor
     * @return Latest finalized anchor point if any
     */
    std::optional<L2AnchorPoint> GetLatestFinalizedAnchor() const;

    // =========================================================================
    // Transaction Logging (Requirement 19.6)
    // =========================================================================

    /**
     * @brief Log an L2 transaction for potential replay
     * @param entry Transaction log entry
     * 
     * Requirement 19.6: Maintain L2 transaction logs for replay after reorg
     */
    void LogTransaction(const L2TxLogEntry& entry);

    /**
     * @brief Get transaction log entry by hash
     * @param txHash Transaction hash
     * @return Log entry if found
     */
    std::optional<L2TxLogEntry> GetTransactionLog(const uint256& txHash) const;

    /**
     * @brief Get all transactions in a given L2 block range
     * @param fromBlock Starting L2 block
     * @param toBlock Ending L2 block (inclusive)
     * @return Vector of transaction log entries
     */
    std::vector<L2TxLogEntry> GetTransactionsInRange(
        uint64_t fromBlock, uint64_t toBlock) const;

    /**
     * @brief Prune old transaction logs
     * @param beforeL2Block Remove logs before this L2 block
     * @return Number of entries pruned
     */
    size_t PruneTransactionLogs(uint64_t beforeL2Block);

    // =========================================================================
    // Notification (Requirement 19.4)
    // =========================================================================

    /**
     * @brief Register a callback for reorg notifications
     * @param callback The callback function
     * 
     * Requirement 19.4: Notify users of transactions affected by reorg
     */
    void RegisterNotificationCallback(ReorgNotificationCallback callback);

    /**
     * @brief Get transactions affected by a reorg
     * @param forkPoint L1 block where reorg occurred
     * @return Vector of affected transaction hashes
     */
    std::vector<uint256> GetAffectedTransactions(uint64_t forkPoint) const;

    // =========================================================================
    // Configuration and Status
    // =========================================================================

    /**
     * @brief Set the L1 finality depth
     * @param depth Number of confirmations required
     */
    void SetFinalityDepth(uint32_t depth);

    /**
     * @brief Get the L1 finality depth
     * @return Number of confirmations required
     */
    uint32_t GetFinalityDepth() const;

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get statistics about the monitor
     * @return Statistics string
     */
    std::string GetStatistics() const;

    /**
     * @brief Check if the monitor is healthy
     * @return true if operating normally
     */
    bool IsHealthy() const;

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** L1 finality depth (confirmations required) */
    uint32_t finalityDepth_;

    /** Pointer to L2 state manager */
    std::shared_ptr<L2StateManager> stateManager_;

    /** L1 block history (block number -> block info) */
    std::map<uint64_t, L1BlockInfo> l1BlockHistory_;

    /** Current L1 chain tip */
    L1BlockInfo currentL1Tip_;

    /** L2 anchor points (L1 block number -> anchor) */
    std::map<uint64_t, L2AnchorPoint> anchorPoints_;

    /** Transaction logs (tx hash -> log entry) */
    std::map<uint256, L2TxLogEntry> transactionLogs_;

    /** Transaction logs by L2 block (block number -> tx hashes) */
    std::map<uint64_t, std::vector<uint256>> txLogsByBlock_;

    /** Notification callbacks */
    std::vector<ReorgNotificationCallback> notificationCallbacks_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_reorg_;

    /**
     * @brief Find the fork point between old and new chain
     * @param oldTip Old chain tip
     * @param newTip New chain tip
     * @return Fork point block number, or 0 if not found
     */
    uint64_t FindForkPoint(const L1BlockInfo& oldTip, const L1BlockInfo& newTip) const;

    /**
     * @brief Notify all registered callbacks
     * @param detection Reorg detection result
     * @param recovery Recovery result
     */
    void NotifyCallbacks(const ReorgDetectionResult& detection,
                        const ReorgRecoveryResult& recovery);

    /**
     * @brief Prune old L1 block history
     * @param keepBlocks Number of recent blocks to keep
     */
    void PruneL1History(size_t keepBlocks);

    /**
     * @brief Prune old anchor points
     * @param keepAnchors Number of recent anchors to keep
     */
    void PruneAnchorPoints(size_t keepAnchors);
};

} // namespace l2

#endif // CASCOIN_L2_REORG_MONITOR_H
