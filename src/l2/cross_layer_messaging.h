// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_CROSS_LAYER_MESSAGING_H
#define CASCOIN_L2_CROSS_LAYER_MESSAGING_H

/**
 * @file cross_layer_messaging.h
 * @brief Cross-Layer Messaging for L1<->L2 communication
 * 
 * This file implements the cross-layer messaging system that enables
 * communication between L1 (Cascoin mainchain) and L2 (Layer 2 scaling solution).
 * 
 * Key features:
 * - L1→L2 message passing with guaranteed delivery
 * - L2→L1 message passing with challenge period
 * - Reentrancy protection for cross-layer calls
 * - Message queuing for next block execution
 * 
 * Requirements: 9.1, 9.2, 9.3, 9.4, 28.1, 28.2, 28.4
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <set>
#include <queue>
#include <vector>
#include <optional>
#include <functional>
#include <mutex>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Maximum message data size: 64KB */
static constexpr size_t MAX_MESSAGE_DATA_SIZE = 64 * 1024;

/** Maximum messages per block */
static constexpr size_t MAX_MESSAGES_PER_BLOCK = 100;

/** L2→L1 message challenge period: 7 days in seconds */
static constexpr uint64_t L2_TO_L1_CHALLENGE_PERIOD = 7 * 24 * 60 * 60;

/** L1→L2 message execution deadline: 1 block */
static constexpr uint64_t L1_TO_L2_EXECUTION_DEADLINE = 1;

/** Maximum retry attempts for failed messages */
static constexpr uint32_t MAX_MESSAGE_RETRIES = 3;

/** Message gas limit for execution */
static constexpr uint64_t MESSAGE_GAS_LIMIT = 1000000;

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Message from L1 to L2
 * 
 * Represents a message sent from L1 to L2. These messages are processed
 * with guaranteed delivery within the next L2 block.
 * 
 * Requirement 9.1: Support L1→L2 message passing with guaranteed delivery
 */
struct L1ToL2Message {
    /** Unique message identifier */
    uint256 messageId;
    
    /** L1 sender address */
    uint160 l1Sender;
    
    /** L2 target contract/address */
    uint160 l2Target;
    
    /** Message data payload */
    std::vector<unsigned char> data;
    
    /** CAS value to transfer with message */
    CAmount value;
    
    /** L1 block number where message was sent */
    uint64_t l1BlockNumber;
    
    /** L1 transaction hash containing the message */
    uint256 l1TxHash;
    
    /** Current status of the message */
    MessageStatus status;
    
    /** Timestamp when message was created */
    uint64_t timestamp;
    
    /** Gas limit for message execution */
    uint64_t gasLimit;
    
    /** Number of execution attempts */
    uint32_t retryCount;
    
    /** Error message if execution failed */
    std::string errorMessage;

    L1ToL2Message()
        : value(0)
        , l1BlockNumber(0)
        , status(MessageStatus::PENDING)
        , timestamp(0)
        , gasLimit(MESSAGE_GAS_LIMIT)
        , retryCount(0)
    {}

    L1ToL2Message(const uint256& id, const uint160& sender, const uint160& target,
                  const std::vector<unsigned char>& msgData, CAmount val,
                  uint64_t blockNum, const uint256& txHash, uint64_t ts)
        : messageId(id)
        , l1Sender(sender)
        , l2Target(target)
        , data(msgData)
        , value(val)
        , l1BlockNumber(blockNum)
        , l1TxHash(txHash)
        , status(MessageStatus::PENDING)
        , timestamp(ts)
        , gasLimit(MESSAGE_GAS_LIMIT)
        , retryCount(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(messageId);
        READWRITE(l1Sender);
        READWRITE(l2Target);
        READWRITE(data);
        READWRITE(value);
        READWRITE(l1BlockNumber);
        READWRITE(l1TxHash);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<MessageStatus>(statusByte);
        }
        READWRITE(timestamp);
        READWRITE(gasLimit);
        READWRITE(retryCount);
        READWRITE(errorMessage);
    }

    /** Compute unique hash for this message */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << messageId << l1Sender << l2Target << data << value 
           << l1BlockNumber << l1TxHash << timestamp;
        return ss.GetHash();
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& bytes) {
        if (bytes.empty()) return false;
        try {
            CDataStream ss(bytes, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /** Check if message can be retried */
    bool CanRetry() const {
        return status == MessageStatus::FAILED && retryCount < MAX_MESSAGE_RETRIES;
    }

    bool operator==(const L1ToL2Message& other) const {
        return messageId == other.messageId &&
               l1Sender == other.l1Sender &&
               l2Target == other.l2Target &&
               data == other.data &&
               value == other.value &&
               l1BlockNumber == other.l1BlockNumber &&
               l1TxHash == other.l1TxHash &&
               status == other.status;
    }
};

/**
 * @brief Message from L2 to L1
 * 
 * Represents a message sent from L2 to L1. These messages go through
 * a challenge period before they can be finalized on L1.
 * 
 * Requirement 9.2: Support L2→L1 message passing with challenge period
 */
struct L2ToL1Message {
    /** Unique message identifier */
    uint256 messageId;
    
    /** L2 sender address */
    uint160 l2Sender;
    
    /** L1 target contract/address */
    uint160 l1Target;
    
    /** Message data payload */
    std::vector<unsigned char> data;
    
    /** CAS value to transfer with message */
    CAmount value;
    
    /** L2 block number where message was sent */
    uint64_t l2BlockNumber;
    
    /** State root at the time of message creation */
    uint256 stateRoot;
    
    /** Merkle proof of message inclusion in state */
    std::vector<unsigned char> merkleProof;
    
    /** Timestamp when challenge period ends */
    uint64_t challengeDeadline;
    
    /** Current status of the message */
    MessageStatus status;
    
    /** Timestamp when message was created */
    uint64_t timestamp;
    
    /** Gas limit for message execution on L1 */
    uint64_t gasLimit;
    
    /** Error message if execution failed */
    std::string errorMessage;

    L2ToL1Message()
        : value(0)
        , l2BlockNumber(0)
        , challengeDeadline(0)
        , status(MessageStatus::PENDING)
        , timestamp(0)
        , gasLimit(MESSAGE_GAS_LIMIT)
    {}

    L2ToL1Message(const uint256& id, const uint160& sender, const uint160& target,
                  const std::vector<unsigned char>& msgData, CAmount val,
                  uint64_t blockNum, const uint256& root, uint64_t ts)
        : messageId(id)
        , l2Sender(sender)
        , l1Target(target)
        , data(msgData)
        , value(val)
        , l2BlockNumber(blockNum)
        , stateRoot(root)
        , challengeDeadline(ts + L2_TO_L1_CHALLENGE_PERIOD)
        , status(MessageStatus::PENDING)
        , timestamp(ts)
        , gasLimit(MESSAGE_GAS_LIMIT)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(messageId);
        READWRITE(l2Sender);
        READWRITE(l1Target);
        READWRITE(data);
        READWRITE(value);
        READWRITE(l2BlockNumber);
        READWRITE(stateRoot);
        READWRITE(merkleProof);
        READWRITE(challengeDeadline);
        uint8_t statusByte = static_cast<uint8_t>(status);
        READWRITE(statusByte);
        if (ser_action.ForRead()) {
            status = static_cast<MessageStatus>(statusByte);
        }
        READWRITE(timestamp);
        READWRITE(gasLimit);
        READWRITE(errorMessage);
    }

    /** Compute unique hash for this message */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << messageId << l2Sender << l1Target << data << value 
           << l2BlockNumber << stateRoot << timestamp;
        return ss.GetHash();
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& bytes) {
        if (bytes.empty()) return false;
        try {
            CDataStream ss(bytes, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /** Check if challenge period has passed */
    bool IsChallengePeriodOver(uint64_t currentTime) const {
        return currentTime >= challengeDeadline;
    }

    /** Check if message can be finalized */
    bool CanFinalize(uint64_t currentTime) const {
        return status == MessageStatus::PENDING && IsChallengePeriodOver(currentTime);
    }

    bool operator==(const L2ToL1Message& other) const {
        return messageId == other.messageId &&
               l2Sender == other.l2Sender &&
               l1Target == other.l1Target &&
               data == other.data &&
               value == other.value &&
               l2BlockNumber == other.l2BlockNumber &&
               stateRoot == other.stateRoot &&
               status == other.status;
    }
};

/**
 * @brief Message execution result
 */
struct MessageExecutionResult {
    /** Whether execution succeeded */
    bool success;
    
    /** Gas used during execution */
    uint64_t gasUsed;
    
    /** Return data from execution */
    std::vector<unsigned char> returnData;
    
    /** Error message if failed */
    std::string error;
    
    /** Logs/events generated */
    std::vector<std::vector<unsigned char>> logs;

    MessageExecutionResult() : success(false), gasUsed(0) {}
    
    static MessageExecutionResult Success(uint64_t gas, const std::vector<unsigned char>& ret = {}) {
        MessageExecutionResult result;
        result.success = true;
        result.gasUsed = gas;
        result.returnData = ret;
        return result;
    }
    
    static MessageExecutionResult Failure(const std::string& err, uint64_t gas = 0) {
        MessageExecutionResult result;
        result.success = false;
        result.error = err;
        result.gasUsed = gas;
        return result;
    }
};

/**
 * @brief Statistics for cross-layer messaging
 */
struct CrossLayerStats {
    /** Total L1→L2 messages sent */
    uint64_t totalL1ToL2Messages;
    
    /** Total L2→L1 messages sent */
    uint64_t totalL2ToL1Messages;
    
    /** Successfully executed L1→L2 messages */
    uint64_t executedL1ToL2Messages;
    
    /** Successfully finalized L2→L1 messages */
    uint64_t finalizedL2ToL1Messages;
    
    /** Failed L1→L2 messages */
    uint64_t failedL1ToL2Messages;
    
    /** Challenged L2→L1 messages */
    uint64_t challengedL2ToL1Messages;
    
    /** Pending L1→L2 messages */
    uint64_t pendingL1ToL2Messages;
    
    /** Pending L2→L1 messages */
    uint64_t pendingL2ToL1Messages;

    CrossLayerStats()
        : totalL1ToL2Messages(0)
        , totalL2ToL1Messages(0)
        , executedL1ToL2Messages(0)
        , finalizedL2ToL1Messages(0)
        , failedL1ToL2Messages(0)
        , challengedL2ToL1Messages(0)
        , pendingL1ToL2Messages(0)
        , pendingL2ToL1Messages(0)
    {}
};


// ============================================================================
// Reentrancy Guard (Requirement 28.1, 28.4)
// ============================================================================

/**
 * @brief Reentrancy guard for cross-layer message execution
 * 
 * Prevents reentrancy attacks during cross-layer message execution.
 * Uses a set of currently executing message IDs to detect and prevent
 * recursive calls.
 * 
 * Requirements 28.1, 28.4: Implement cross-layer call mutex and reentrancy guards
 */
class ReentrancyGuard {
public:
    ReentrancyGuard() = default;
    
    /**
     * @brief Try to acquire the guard for a message
     * @param messageId The message being executed
     * @return true if guard acquired, false if already executing
     */
    bool TryAcquire(const uint256& messageId) {
        LOCK(cs_guard_);
        if (executingMessages_.count(messageId) > 0) {
            return false;  // Already executing - reentrancy detected
        }
        executingMessages_.insert(messageId);
        return true;
    }
    
    /**
     * @brief Release the guard for a message
     * @param messageId The message that finished executing
     */
    void Release(const uint256& messageId) {
        LOCK(cs_guard_);
        executingMessages_.erase(messageId);
    }
    
    /**
     * @brief Check if a message is currently executing
     * @param messageId The message to check
     * @return true if message is currently executing
     */
    bool IsExecuting(const uint256& messageId) const {
        LOCK(cs_guard_);
        return executingMessages_.count(messageId) > 0;
    }
    
    /**
     * @brief Get number of currently executing messages
     * @return Count of executing messages
     */
    size_t GetExecutingCount() const {
        LOCK(cs_guard_);
        return executingMessages_.size();
    }
    
    /**
     * @brief Clear all executing messages (for testing)
     */
    void Clear() {
        LOCK(cs_guard_);
        executingMessages_.clear();
    }

private:
    /** Set of currently executing message IDs */
    std::set<uint256> executingMessages_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_guard_;
};

/**
 * @brief RAII guard for automatic release
 */
class ScopedReentrancyGuard {
public:
    ScopedReentrancyGuard(ReentrancyGuard& guard, const uint256& messageId)
        : guard_(guard), messageId_(messageId), acquired_(false)
    {
        acquired_ = guard_.TryAcquire(messageId_);
    }
    
    ~ScopedReentrancyGuard() {
        if (acquired_) {
            guard_.Release(messageId_);
        }
    }
    
    /** Check if guard was successfully acquired */
    bool Acquired() const { return acquired_; }
    
    // Non-copyable
    ScopedReentrancyGuard(const ScopedReentrancyGuard&) = delete;
    ScopedReentrancyGuard& operator=(const ScopedReentrancyGuard&) = delete;

private:
    ReentrancyGuard& guard_;
    uint256 messageId_;
    bool acquired_;
};

// ============================================================================
// Message Queue for Next Block Execution (Requirement 28.2)
// ============================================================================

/**
 * @brief Queued message for next block execution
 * 
 * L1→L2 messages are queued for execution in the next block to prevent
 * same-block execution and potential reentrancy issues.
 * 
 * Requirement 28.2: Queue L1→L2 messages for next block
 */
struct QueuedMessage {
    /** The L1→L2 message */
    L1ToL2Message message;
    
    /** Target L2 block for execution */
    uint64_t targetBlock;
    
    /** Priority (lower = higher priority) */
    uint32_t priority;

    QueuedMessage() : targetBlock(0), priority(0) {}
    
    QueuedMessage(const L1ToL2Message& msg, uint64_t block, uint32_t prio = 0)
        : message(msg), targetBlock(block), priority(prio) {}
    
    /** Comparison for priority queue (lower priority value = higher priority) */
    bool operator<(const QueuedMessage& other) const {
        if (targetBlock != other.targetBlock) {
            return targetBlock > other.targetBlock;  // Earlier blocks first
        }
        return priority > other.priority;  // Lower priority value first
    }
};

// ============================================================================
// Cross-Layer Messaging Class
// ============================================================================

/**
 * @brief Cross-Layer Messaging Manager
 * 
 * Manages all cross-layer communication between L1 and L2.
 * Provides message sending, processing, and finalization with
 * reentrancy protection and message queuing.
 * 
 * Thread-safe for concurrent access.
 * 
 * Requirements: 9.1, 9.2, 9.3, 9.4, 28.1, 28.2, 28.4
 */
class CrossLayerMessaging {
public:
    /** Message execution callback type */
    using ExecutionCallback = std::function<MessageExecutionResult(
        const uint160& target,
        const std::vector<unsigned char>& data,
        CAmount value,
        uint64_t gasLimit
    )>;

    /**
     * @brief Construct a new Cross Layer Messaging manager
     * @param chainId The L2 chain ID
     */
    explicit CrossLayerMessaging(uint64_t chainId);

    // =========================================================================
    // L1→L2 Message Operations (Requirement 9.1)
    // =========================================================================

    /**
     * @brief Send a message from L1 to L2
     * @param l1Sender L1 sender address
     * @param l2Target L2 target address
     * @param data Message data payload
     * @param value CAS value to transfer
     * @param l1BlockNumber L1 block number
     * @param l1TxHash L1 transaction hash
     * @param timestamp Current timestamp
     * @return The message ID
     * 
     * Requirement 9.1: Support L1→L2 message passing with guaranteed delivery
     */
    uint256 SendL1ToL2(
        const uint160& l1Sender,
        const uint160& l2Target,
        const std::vector<unsigned char>& data,
        CAmount value,
        uint64_t l1BlockNumber,
        const uint256& l1TxHash,
        uint64_t timestamp);

    /**
     * @brief Process an L1→L2 message on L2
     * @param message The message to process
     * @param currentBlock Current L2 block number
     * @return Execution result
     * 
     * Requirement 9.5: Execute L1→L2 messages within next L2 block
     */
    MessageExecutionResult ProcessL1ToL2Message(
        const L1ToL2Message& message,
        uint64_t currentBlock);

    /**
     * @brief Queue an L1→L2 message for next block execution
     * @param message The message to queue
     * @param currentBlock Current L2 block number
     * @return true if message was queued successfully
     * 
     * Requirement 28.2: Queue L1→L2 messages for next block
     */
    bool QueueL1ToL2Message(const L1ToL2Message& message, uint64_t currentBlock);

    /**
     * @brief Get messages queued for a specific block
     * @param blockNumber The target block number
     * @return Vector of queued messages for that block
     */
    std::vector<L1ToL2Message> GetQueuedMessagesForBlock(uint64_t blockNumber) const;

    /**
     * @brief Process all queued messages for a block
     * @param blockNumber The block number to process
     * @return Number of messages processed
     */
    size_t ProcessQueuedMessages(uint64_t blockNumber);

    /**
     * @brief Get pending L1→L2 messages
     * @return Vector of pending messages
     */
    std::vector<L1ToL2Message> GetPendingL1ToL2Messages() const;

    // =========================================================================
    // L2→L1 Message Operations (Requirement 9.2)
    // =========================================================================

    /**
     * @brief Send a message from L2 to L1
     * @param l2Sender L2 sender address
     * @param l1Target L1 target address
     * @param data Message data payload
     * @param value CAS value to transfer
     * @param l2BlockNumber Current L2 block number
     * @param stateRoot Current state root
     * @param timestamp Current timestamp
     * @return The message ID
     * 
     * Requirement 9.2: Support L2→L1 message passing with challenge period
     */
    uint256 SendL2ToL1(
        const uint160& l2Sender,
        const uint160& l1Target,
        const std::vector<unsigned char>& data,
        CAmount value,
        uint64_t l2BlockNumber,
        const uint256& stateRoot,
        uint64_t timestamp);

    /**
     * @brief Finalize an L2→L1 message on L1 after challenge period
     * @param messageId The message to finalize
     * @param currentTime Current timestamp
     * @return true if message was finalized successfully
     */
    bool FinalizeL2ToL1Message(const uint256& messageId, uint64_t currentTime);

    /**
     * @brief Challenge an L2→L1 message
     * @param messageId The message to challenge
     * @param challenger Address of the challenger
     * @param proof Fraud proof data
     * @param currentTime Current timestamp
     * @return true if challenge was accepted
     */
    bool ChallengeL2ToL1Message(
        const uint256& messageId,
        const uint160& challenger,
        const std::vector<unsigned char>& proof,
        uint64_t currentTime);

    /**
     * @brief Get pending L2→L1 messages
     * @return Vector of pending messages
     */
    std::vector<L2ToL1Message> GetPendingL2ToL1Messages() const;

    // =========================================================================
    // Message Query Operations
    // =========================================================================

    /**
     * @brief Get L1→L2 message by ID
     * @param messageId The message identifier
     * @return The message if found
     */
    std::optional<L1ToL2Message> GetL1ToL2Message(const uint256& messageId) const;

    /**
     * @brief Get L2→L1 message by ID
     * @param messageId The message identifier
     * @return The message if found
     */
    std::optional<L2ToL1Message> GetL2ToL1Message(const uint256& messageId) const;

    /**
     * @brief Get message status
     * @param messageId The message identifier
     * @return The message status
     */
    MessageStatus GetMessageStatus(const uint256& messageId) const;

    /**
     * @brief Retry a failed L1→L2 message
     * @param messageId The message to retry
     * @param currentBlock Current L2 block number
     * @return true if message was queued for retry
     */
    bool RetryMessage(const uint256& messageId, uint64_t currentBlock);

    // =========================================================================
    // Reentrancy Protection (Requirements 28.1, 28.4)
    // =========================================================================

    /**
     * @brief Execute a message with reentrancy protection
     * @param target Target address
     * @param data Message data
     * @param value Value to transfer
     * @param gasLimit Gas limit
     * @param messageId Message ID for reentrancy tracking
     * @return Execution result
     * 
     * Requirements 28.1, 28.4: Implement cross-layer call mutex and reentrancy guards
     */
    MessageExecutionResult ExecuteMessageSafe(
        const uint160& target,
        const std::vector<unsigned char>& data,
        CAmount value,
        uint64_t gasLimit,
        const uint256& messageId);

    /**
     * @brief Check if a message is currently being executed
     * @param messageId The message to check
     * @return true if message is executing
     */
    bool IsMessageExecuting(const uint256& messageId) const;

    // =========================================================================
    // Message Proof Operations (Requirement 9.4)
    // =========================================================================

    /**
     * @brief Generate proof for an L2→L1 message
     * @param messageId The message identifier
     * @return The Merkle proof
     * 
     * Requirement 9.4: Include message proofs for verification
     */
    std::vector<unsigned char> GenerateMessageProof(const uint256& messageId) const;

    /**
     * @brief Verify a message proof
     * @param message The L2→L1 message
     * @param proof The Merkle proof
     * @param stateRoot The state root to verify against
     * @return true if proof is valid
     */
    static bool VerifyMessageProof(
        const L2ToL1Message& message,
        const std::vector<unsigned char>& proof,
        const uint256& stateRoot);

    // =========================================================================
    // Configuration and Statistics
    // =========================================================================

    /**
     * @brief Set the execution callback for message processing
     * @param callback The callback function
     */
    void SetExecutionCallback(ExecutionCallback callback);

    /**
     * @brief Get cross-layer messaging statistics
     * @return Current statistics
     */
    CrossLayerStats GetStats() const;

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Get number of L1→L2 messages
     * @return Message count
     */
    size_t GetL1ToL2MessageCount() const;

    /**
     * @brief Get number of L2→L1 messages
     * @return Message count
     */
    size_t GetL2ToL1MessageCount() const;

    /**
     * @brief Get number of queued messages
     * @return Queue size
     */
    size_t GetQueuedMessageCount() const;

    /**
     * @brief Clear all state (for testing)
     */
    void Clear();

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** L1→L2 messages (messageId -> message) */
    std::map<uint256, L1ToL2Message> l1ToL2Messages_;

    /** L2→L1 messages (messageId -> message) */
    std::map<uint256, L2ToL1Message> l2ToL1Messages_;

    /** Message queue for next block execution (block -> messages) */
    std::map<uint64_t, std::vector<L1ToL2Message>> messageQueue_;

    /** Reentrancy guard */
    ReentrancyGuard reentrancyGuard_;

    /** Execution callback */
    ExecutionCallback executionCallback_;

    /** Statistics */
    CrossLayerStats stats_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_messaging_;

    /** Next message ID counter */
    uint64_t nextMessageId_;

    /**
     * @brief Generate a unique message ID
     * @param sender Sender address
     * @param target Target address
     * @param timestamp Current timestamp
     * @return Unique message ID
     */
    uint256 GenerateMessageId(
        const uint160& sender,
        const uint160& target,
        uint64_t timestamp) const;

    /**
     * @brief Default execution callback (for testing)
     */
    static MessageExecutionResult DefaultExecutionCallback(
        const uint160& target,
        const std::vector<unsigned char>& data,
        CAmount value,
        uint64_t gasLimit);

    /**
     * @brief Update message status
     * @param messageId Message ID
     * @param status New status
     * @param isL1ToL2 Whether this is an L1→L2 message
     */
    void UpdateMessageStatus(const uint256& messageId, MessageStatus status, bool isL1ToL2);
};

} // namespace l2

#endif // CASCOIN_L2_CROSS_LAYER_MESSAGING_H
