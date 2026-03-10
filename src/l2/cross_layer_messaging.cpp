// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file cross_layer_messaging.cpp
 * @brief Implementation of Cross-Layer Messaging for L1<->L2 communication
 * 
 * Requirements: 9.1, 9.2, 9.3, 9.4, 28.1, 28.2, 28.4
 */

#include <l2/cross_layer_messaging.h>
#include <hash.h>
#include <util.h>

namespace l2 {

// ============================================================================
// Constructor
// ============================================================================

CrossLayerMessaging::CrossLayerMessaging(uint64_t chainId)
    : chainId_(chainId)
    , nextMessageId_(1)
{
    // Set default execution callback
    executionCallback_ = DefaultExecutionCallback;
}

// ============================================================================
// L1→L2 Message Operations (Requirement 9.1)
// ============================================================================

uint256 CrossLayerMessaging::SendL1ToL2(
    const uint160& l1Sender,
    const uint160& l2Target,
    const std::vector<unsigned char>& data,
    CAmount value,
    uint64_t l1BlockNumber,
    const uint256& l1TxHash,
    uint64_t timestamp)
{
    LOCK(cs_messaging_);
    
    // Validate message data size
    if (data.size() > MAX_MESSAGE_DATA_SIZE) {
        return uint256();  // Invalid - data too large
    }
    
    // Generate unique message ID
    uint256 messageId = GenerateMessageId(l1Sender, l2Target, timestamp);
    
    // Check for duplicate
    if (l1ToL2Messages_.count(messageId) > 0) {
        return uint256();  // Duplicate message
    }
    
    // Create message
    L1ToL2Message message(messageId, l1Sender, l2Target, data, value,
                          l1BlockNumber, l1TxHash, timestamp);
    
    // Store message
    l1ToL2Messages_[messageId] = message;
    
    // Update statistics
    stats_.totalL1ToL2Messages++;
    stats_.pendingL1ToL2Messages++;
    
    return messageId;
}

MessageExecutionResult CrossLayerMessaging::ProcessL1ToL2Message(
    const L1ToL2Message& message,
    uint64_t currentBlock)
{
    LOCK(cs_messaging_);
    
    // Check if message exists
    auto it = l1ToL2Messages_.find(message.messageId);
    if (it == l1ToL2Messages_.end()) {
        return MessageExecutionResult::Failure("Message not found");
    }
    
    // Check if already executed
    if (it->second.status == MessageStatus::EXECUTED) {
        return MessageExecutionResult::Failure("Message already executed");
    }
    
    // Check if already failed too many times
    if (it->second.status == MessageStatus::FAILED && 
        it->second.retryCount >= MAX_MESSAGE_RETRIES) {
        return MessageExecutionResult::Failure("Max retries exceeded");
    }
    
    // Execute with reentrancy protection
    MessageExecutionResult result = ExecuteMessageSafe(
        message.l2Target,
        message.data,
        message.value,
        message.gasLimit,
        message.messageId);
    
    // Update message status
    if (result.success) {
        it->second.status = MessageStatus::EXECUTED;
        stats_.executedL1ToL2Messages++;
        stats_.pendingL1ToL2Messages--;
    } else {
        it->second.status = MessageStatus::FAILED;
        it->second.retryCount++;
        it->second.errorMessage = result.error;
        
        if (it->second.retryCount >= MAX_MESSAGE_RETRIES) {
            stats_.failedL1ToL2Messages++;
            stats_.pendingL1ToL2Messages--;
        }
    }
    
    return result;
}

bool CrossLayerMessaging::QueueL1ToL2Message(const L1ToL2Message& message, uint64_t currentBlock)
{
    LOCK(cs_messaging_);
    
    // Validate message
    if (message.data.size() > MAX_MESSAGE_DATA_SIZE) {
        return false;
    }
    
    // Target block is next block (Requirement 28.2)
    uint64_t targetBlock = currentBlock + L1_TO_L2_EXECUTION_DEADLINE;
    
    // Check queue limit for target block
    if (messageQueue_[targetBlock].size() >= MAX_MESSAGES_PER_BLOCK) {
        return false;
    }
    
    // Add to queue
    messageQueue_[targetBlock].push_back(message);
    
    // Also store in main message map if not already there
    if (l1ToL2Messages_.count(message.messageId) == 0) {
        l1ToL2Messages_[message.messageId] = message;
        stats_.totalL1ToL2Messages++;
        stats_.pendingL1ToL2Messages++;
    }
    
    return true;
}

std::vector<L1ToL2Message> CrossLayerMessaging::GetQueuedMessagesForBlock(uint64_t blockNumber) const
{
    LOCK(cs_messaging_);
    
    auto it = messageQueue_.find(blockNumber);
    if (it == messageQueue_.end()) {
        return {};
    }
    
    return it->second;
}

size_t CrossLayerMessaging::ProcessQueuedMessages(uint64_t blockNumber)
{
    std::vector<L1ToL2Message> messages;
    
    {
        LOCK(cs_messaging_);
        auto it = messageQueue_.find(blockNumber);
        if (it == messageQueue_.end()) {
            return 0;
        }
        messages = std::move(it->second);
        messageQueue_.erase(it);
    }
    
    size_t processed = 0;
    for (const auto& message : messages) {
        MessageExecutionResult result = ProcessL1ToL2Message(message, blockNumber);
        if (result.success) {
            processed++;
        }
    }
    
    return processed;
}

std::vector<L1ToL2Message> CrossLayerMessaging::GetPendingL1ToL2Messages() const
{
    LOCK(cs_messaging_);
    
    std::vector<L1ToL2Message> pending;
    for (const auto& pair : l1ToL2Messages_) {
        if (pair.second.status == MessageStatus::PENDING) {
            pending.push_back(pair.second);
        }
    }
    
    return pending;
}

// ============================================================================
// L2→L1 Message Operations (Requirement 9.2)
// ============================================================================

uint256 CrossLayerMessaging::SendL2ToL1(
    const uint160& l2Sender,
    const uint160& l1Target,
    const std::vector<unsigned char>& data,
    CAmount value,
    uint64_t l2BlockNumber,
    const uint256& stateRoot,
    uint64_t timestamp)
{
    LOCK(cs_messaging_);
    
    // Validate message data size
    if (data.size() > MAX_MESSAGE_DATA_SIZE) {
        return uint256();  // Invalid - data too large
    }
    
    // Generate unique message ID
    uint256 messageId = GenerateMessageId(l2Sender, l1Target, timestamp);
    
    // Check for duplicate
    if (l2ToL1Messages_.count(messageId) > 0) {
        return uint256();  // Duplicate message
    }
    
    // Create message
    L2ToL1Message message(messageId, l2Sender, l1Target, data, value,
                          l2BlockNumber, stateRoot, timestamp);
    
    // Generate and attach proof
    message.merkleProof = GenerateMessageProof(messageId);
    
    // Store message
    l2ToL1Messages_[messageId] = message;
    
    // Update statistics
    stats_.totalL2ToL1Messages++;
    stats_.pendingL2ToL1Messages++;
    
    return messageId;
}

bool CrossLayerMessaging::FinalizeL2ToL1Message(const uint256& messageId, uint64_t currentTime)
{
    LOCK(cs_messaging_);
    
    // Find message
    auto it = l2ToL1Messages_.find(messageId);
    if (it == l2ToL1Messages_.end()) {
        return false;  // Message not found
    }
    
    L2ToL1Message& message = it->second;
    
    // Check if can be finalized
    if (!message.CanFinalize(currentTime)) {
        return false;  // Not ready for finalization
    }
    
    // Verify proof
    if (!VerifyMessageProof(message, message.merkleProof, message.stateRoot)) {
        message.status = MessageStatus::FAILED;
        message.errorMessage = "Invalid message proof";
        return false;
    }
    
    // Mark as finalized
    message.status = MessageStatus::FINALIZED;
    
    // Update statistics
    stats_.finalizedL2ToL1Messages++;
    stats_.pendingL2ToL1Messages--;
    
    return true;
}

bool CrossLayerMessaging::ChallengeL2ToL1Message(
    const uint256& messageId,
    const uint160& challenger,
    const std::vector<unsigned char>& proof,
    uint64_t currentTime)
{
    LOCK(cs_messaging_);
    
    // Find message
    auto it = l2ToL1Messages_.find(messageId);
    if (it == l2ToL1Messages_.end()) {
        return false;  // Message not found
    }
    
    L2ToL1Message& message = it->second;
    
    // Can only challenge pending messages within challenge period
    if (message.status != MessageStatus::PENDING) {
        return false;
    }
    
    if (message.IsChallengePeriodOver(currentTime)) {
        return false;  // Challenge period has passed
    }
    
    // For now, accept any challenge (in production, would verify fraud proof)
    // The proof verification would be done by the fraud proof system
    if (proof.empty()) {
        return false;  // Need some proof data
    }
    
    // Mark as challenged
    message.status = MessageStatus::CHALLENGED;
    
    // Update statistics
    stats_.challengedL2ToL1Messages++;
    stats_.pendingL2ToL1Messages--;
    
    return true;
}

std::vector<L2ToL1Message> CrossLayerMessaging::GetPendingL2ToL1Messages() const
{
    LOCK(cs_messaging_);
    
    std::vector<L2ToL1Message> pending;
    for (const auto& pair : l2ToL1Messages_) {
        if (pair.second.status == MessageStatus::PENDING) {
            pending.push_back(pair.second);
        }
    }
    
    return pending;
}

// ============================================================================
// Message Query Operations
// ============================================================================

std::optional<L1ToL2Message> CrossLayerMessaging::GetL1ToL2Message(const uint256& messageId) const
{
    LOCK(cs_messaging_);
    
    auto it = l1ToL2Messages_.find(messageId);
    if (it == l1ToL2Messages_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

std::optional<L2ToL1Message> CrossLayerMessaging::GetL2ToL1Message(const uint256& messageId) const
{
    LOCK(cs_messaging_);
    
    auto it = l2ToL1Messages_.find(messageId);
    if (it == l2ToL1Messages_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

MessageStatus CrossLayerMessaging::GetMessageStatus(const uint256& messageId) const
{
    LOCK(cs_messaging_);
    
    // Check L1→L2 messages
    auto it1 = l1ToL2Messages_.find(messageId);
    if (it1 != l1ToL2Messages_.end()) {
        return it1->second.status;
    }
    
    // Check L2→L1 messages
    auto it2 = l2ToL1Messages_.find(messageId);
    if (it2 != l2ToL1Messages_.end()) {
        return it2->second.status;
    }
    
    // Message not found - return PENDING as default
    return MessageStatus::PENDING;
}

bool CrossLayerMessaging::RetryMessage(const uint256& messageId, uint64_t currentBlock)
{
    LOCK(cs_messaging_);
    
    // Find message
    auto it = l1ToL2Messages_.find(messageId);
    if (it == l1ToL2Messages_.end()) {
        return false;  // Message not found
    }
    
    L1ToL2Message& message = it->second;
    
    // Can only retry failed messages that haven't exceeded max retries
    if (!message.CanRetry()) {
        return false;
    }
    
    // Queue for next block
    return QueueL1ToL2Message(message, currentBlock);
}


// ============================================================================
// Reentrancy Protection (Requirements 28.1, 28.4)
// ============================================================================

MessageExecutionResult CrossLayerMessaging::ExecuteMessageSafe(
    const uint160& target,
    const std::vector<unsigned char>& data,
    CAmount value,
    uint64_t gasLimit,
    const uint256& messageId)
{
    // Use scoped reentrancy guard
    ScopedReentrancyGuard guard(reentrancyGuard_, messageId);
    
    if (!guard.Acquired()) {
        return MessageExecutionResult::Failure("Reentrancy detected");
    }
    
    // Execute via callback
    if (executionCallback_) {
        return executionCallback_(target, data, value, gasLimit);
    }
    
    return DefaultExecutionCallback(target, data, value, gasLimit);
}

bool CrossLayerMessaging::IsMessageExecuting(const uint256& messageId) const
{
    return reentrancyGuard_.IsExecuting(messageId);
}

// ============================================================================
// Message Proof Operations (Requirement 9.4)
// ============================================================================

std::vector<unsigned char> CrossLayerMessaging::GenerateMessageProof(const uint256& messageId) const
{
    // Generate a simple proof based on message hash
    // In production, this would be a proper Merkle proof from the state tree
    CHashWriter ss(SER_GETHASH, 0);
    ss << messageId << chainId_;
    uint256 proofHash = ss.GetHash();
    
    std::vector<unsigned char> proof(proofHash.begin(), proofHash.end());
    return proof;
}

bool CrossLayerMessaging::VerifyMessageProof(
    const L2ToL1Message& message,
    const std::vector<unsigned char>& proof,
    const uint256& stateRoot)
{
    // Verify the proof matches the expected format
    if (proof.size() != 32) {
        return false;  // Invalid proof size
    }
    
    // Reconstruct expected proof
    CHashWriter ss(SER_GETHASH, 0);
    ss << message.messageId << message.l2BlockNumber;
    
    // For now, accept any non-empty proof that's the right size
    // In production, this would verify against the actual state tree
    return !proof.empty();
}

// ============================================================================
// Configuration and Statistics
// ============================================================================

void CrossLayerMessaging::SetExecutionCallback(ExecutionCallback callback)
{
    LOCK(cs_messaging_);
    executionCallback_ = callback;
}

CrossLayerStats CrossLayerMessaging::GetStats() const
{
    LOCK(cs_messaging_);
    return stats_;
}

size_t CrossLayerMessaging::GetL1ToL2MessageCount() const
{
    LOCK(cs_messaging_);
    return l1ToL2Messages_.size();
}

size_t CrossLayerMessaging::GetL2ToL1MessageCount() const
{
    LOCK(cs_messaging_);
    return l2ToL1Messages_.size();
}

size_t CrossLayerMessaging::GetQueuedMessageCount() const
{
    LOCK(cs_messaging_);
    
    size_t count = 0;
    for (const auto& pair : messageQueue_) {
        count += pair.second.size();
    }
    return count;
}

void CrossLayerMessaging::Clear()
{
    LOCK(cs_messaging_);
    
    l1ToL2Messages_.clear();
    l2ToL1Messages_.clear();
    messageQueue_.clear();
    reentrancyGuard_.Clear();
    stats_ = CrossLayerStats();
    nextMessageId_ = 1;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

uint256 CrossLayerMessaging::GenerateMessageId(
    const uint160& sender,
    const uint160& target,
    uint64_t timestamp) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << sender << target << timestamp << chainId_ << nextMessageId_;
    
    // Increment counter for next message
    const_cast<CrossLayerMessaging*>(this)->nextMessageId_++;
    
    return ss.GetHash();
}

MessageExecutionResult CrossLayerMessaging::DefaultExecutionCallback(
    const uint160& target,
    const std::vector<unsigned char>& data,
    CAmount value,
    uint64_t gasLimit)
{
    // Default implementation - just succeeds with minimal gas
    // In production, this would call the CVM executor
    
    // Simulate some gas usage based on data size
    uint64_t gasUsed = 21000 + data.size() * 16;  // Base cost + data cost
    
    if (gasUsed > gasLimit) {
        return MessageExecutionResult::Failure("Out of gas", gasLimit);
    }
    
    return MessageExecutionResult::Success(gasUsed);
}

void CrossLayerMessaging::UpdateMessageStatus(const uint256& messageId, MessageStatus status, bool isL1ToL2)
{
    LOCK(cs_messaging_);
    
    if (isL1ToL2) {
        auto it = l1ToL2Messages_.find(messageId);
        if (it != l1ToL2Messages_.end()) {
            it->second.status = status;
        }
    } else {
        auto it = l2ToL1Messages_.find(messageId);
        if (it != l2ToL1Messages_.end()) {
            it->second.status = status;
        }
    }
}

} // namespace l2
