// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_cross_layer_messaging_tests.cpp
 * @brief Property-based tests for L2 Cross-Layer Messaging
 * 
 * **Feature: cascoin-l2-solution, Property 9: Cross-Layer Message Integrity**
 * **Validates: Requirements 9.1, 9.2, 9.4**
 * 
 * Property 9: Cross-Layer Message Integrity
 * *For any* L1→L2 or L2→L1 message, the message content received SHALL be
 * identical to the message content sent.
 */

#include <l2/cross_layer_messaging.h>
#include <random.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>

namespace {

// Local random context for tests
static FastRandomContext g_test_rand_ctx(true);

static uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

static uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

static uint256 TestRand256() { 
    return g_test_rand_ctx.rand256(); 
}

/**
 * Helper function to generate a random uint160 address
 */
static uint160 RandomAddress160()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        uint32_t val = TestRand32();
        memcpy(addr.begin() + i * 4, &val, 4);
    }
    return addr;
}

/**
 * Helper function to generate random message data
 */
static std::vector<unsigned char> RandomMessageData(size_t maxSize = 1024)
{
    size_t size = TestRand32() % maxSize + 1;
    std::vector<unsigned char> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<unsigned char>(TestRand32() % 256);
    }
    return data;
}

/**
 * Helper function to generate a random L1→L2 message
 */
static l2::L1ToL2Message RandomL1ToL2Message(uint64_t timestamp)
{
    l2::L1ToL2Message msg;
    msg.messageId = TestRand256();
    msg.l1Sender = RandomAddress160();
    msg.l2Target = RandomAddress160();
    msg.data = RandomMessageData();
    msg.value = (TestRand64() % 1000 + 1) * COIN;
    msg.l1BlockNumber = TestRand64() % 1000000;
    msg.l1TxHash = TestRand256();
    msg.timestamp = timestamp;
    msg.status = l2::MessageStatus::PENDING;
    msg.gasLimit = l2::MESSAGE_GAS_LIMIT;
    msg.retryCount = 0;
    return msg;
}

/**
 * Helper function to generate a random L2→L1 message
 */
static l2::L2ToL1Message RandomL2ToL1Message(uint64_t timestamp)
{
    l2::L2ToL1Message msg;
    msg.messageId = TestRand256();
    msg.l2Sender = RandomAddress160();
    msg.l1Target = RandomAddress160();
    msg.data = RandomMessageData();
    msg.value = (TestRand64() % 1000 + 1) * COIN;
    msg.l2BlockNumber = TestRand64() % 1000000;
    msg.stateRoot = TestRand256();
    msg.timestamp = timestamp;
    msg.challengeDeadline = timestamp + l2::L2_TO_L1_CHALLENGE_PERIOD;
    msg.status = l2::MessageStatus::PENDING;
    msg.gasLimit = l2::MESSAGE_GAS_LIMIT;
    return msg;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_cross_layer_messaging_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_messaging_has_zero_counts)
{
    l2::CrossLayerMessaging messaging(1);
    
    BOOST_CHECK_EQUAL(messaging.GetL1ToL2MessageCount(), 0u);
    BOOST_CHECK_EQUAL(messaging.GetL2ToL1MessageCount(), 0u);
    BOOST_CHECK_EQUAL(messaging.GetQueuedMessageCount(), 0u);
    
    auto stats = messaging.GetStats();
    BOOST_CHECK_EQUAL(stats.totalL1ToL2Messages, 0u);
    BOOST_CHECK_EQUAL(stats.totalL2ToL1Messages, 0u);
}

BOOST_AUTO_TEST_CASE(send_l1_to_l2_message)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint160 sender = RandomAddress160();
    uint160 target = RandomAddress160();
    std::vector<unsigned char> data = RandomMessageData();
    CAmount value = 100 * COIN;
    uint64_t blockNum = 12345;
    uint256 txHash = TestRand256();
    uint64_t timestamp = 1000;
    
    uint256 messageId = messaging.SendL1ToL2(sender, target, data, value, 
                                              blockNum, txHash, timestamp);
    
    BOOST_CHECK(!messageId.IsNull());
    BOOST_CHECK_EQUAL(messaging.GetL1ToL2MessageCount(), 1u);
    
    auto msg = messaging.GetL1ToL2Message(messageId);
    BOOST_CHECK(msg.has_value());
    BOOST_CHECK(msg->l1Sender == sender);
    BOOST_CHECK(msg->l2Target == target);
    BOOST_CHECK(msg->data == data);
    BOOST_CHECK_EQUAL(msg->value, value);
    BOOST_CHECK(msg->status == l2::MessageStatus::PENDING);
}

BOOST_AUTO_TEST_CASE(send_l2_to_l1_message)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint160 sender = RandomAddress160();
    uint160 target = RandomAddress160();
    std::vector<unsigned char> data = RandomMessageData();
    CAmount value = 100 * COIN;
    uint64_t blockNum = 12345;
    uint256 stateRoot = TestRand256();
    uint64_t timestamp = 1000;
    
    uint256 messageId = messaging.SendL2ToL1(sender, target, data, value,
                                              blockNum, stateRoot, timestamp);
    
    BOOST_CHECK(!messageId.IsNull());
    BOOST_CHECK_EQUAL(messaging.GetL2ToL1MessageCount(), 1u);
    
    auto msg = messaging.GetL2ToL1Message(messageId);
    BOOST_CHECK(msg.has_value());
    BOOST_CHECK(msg->l2Sender == sender);
    BOOST_CHECK(msg->l1Target == target);
    BOOST_CHECK(msg->data == data);
    BOOST_CHECK_EQUAL(msg->value, value);
    BOOST_CHECK(msg->status == l2::MessageStatus::PENDING);
}

BOOST_AUTO_TEST_CASE(message_data_size_limit)
{
    l2::CrossLayerMessaging messaging(1);
    
    // Create data that exceeds the limit
    std::vector<unsigned char> largeData(l2::MAX_MESSAGE_DATA_SIZE + 1, 0x42);
    
    uint256 messageId = messaging.SendL1ToL2(
        RandomAddress160(), RandomAddress160(), largeData, 100 * COIN,
        12345, TestRand256(), 1000);
    
    // Should fail due to size limit
    BOOST_CHECK(messageId.IsNull());
    BOOST_CHECK_EQUAL(messaging.GetL1ToL2MessageCount(), 0u);
}

BOOST_AUTO_TEST_CASE(process_l1_to_l2_message)
{
    l2::CrossLayerMessaging messaging(1);
    
    // Send a message
    uint160 sender = RandomAddress160();
    uint160 target = RandomAddress160();
    std::vector<unsigned char> data = {0x01, 0x02, 0x03};
    
    uint256 messageId = messaging.SendL1ToL2(sender, target, data, 100 * COIN,
                                              12345, TestRand256(), 1000);
    
    auto msg = messaging.GetL1ToL2Message(messageId);
    BOOST_REQUIRE(msg.has_value());
    
    // Process the message
    auto result = messaging.ProcessL1ToL2Message(*msg, 100);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK(result.gasUsed > 0);
    
    // Check status updated
    auto updatedMsg = messaging.GetL1ToL2Message(messageId);
    BOOST_CHECK(updatedMsg->status == l2::MessageStatus::EXECUTED);
}

BOOST_AUTO_TEST_CASE(finalize_l2_to_l1_message_after_challenge_period)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint64_t timestamp = 1000;
    
    // Send a message
    uint256 messageId = messaging.SendL2ToL1(
        RandomAddress160(), RandomAddress160(), {0x01, 0x02}, 100 * COIN,
        12345, TestRand256(), timestamp);
    
    // Try to finalize before challenge period - should fail
    BOOST_CHECK(!messaging.FinalizeL2ToL1Message(messageId, timestamp + 1000));
    
    // Finalize after challenge period
    uint64_t afterChallenge = timestamp + l2::L2_TO_L1_CHALLENGE_PERIOD + 1;
    BOOST_CHECK(messaging.FinalizeL2ToL1Message(messageId, afterChallenge));
    
    // Check status
    auto msg = messaging.GetL2ToL1Message(messageId);
    BOOST_CHECK(msg->status == l2::MessageStatus::FINALIZED);
}

BOOST_AUTO_TEST_CASE(challenge_l2_to_l1_message)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint64_t timestamp = 1000;
    
    // Send a message
    uint256 messageId = messaging.SendL2ToL1(
        RandomAddress160(), RandomAddress160(), {0x01, 0x02}, 100 * COIN,
        12345, TestRand256(), timestamp);
    
    // Challenge the message
    uint160 challenger = RandomAddress160();
    std::vector<unsigned char> proof = {0x01, 0x02, 0x03};
    
    BOOST_CHECK(messaging.ChallengeL2ToL1Message(messageId, challenger, proof, timestamp + 1000));
    
    // Check status
    auto msg = messaging.GetL2ToL1Message(messageId);
    BOOST_CHECK(msg->status == l2::MessageStatus::CHALLENGED);
}

BOOST_AUTO_TEST_CASE(cannot_challenge_after_period)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint64_t timestamp = 1000;
    
    // Send a message
    uint256 messageId = messaging.SendL2ToL1(
        RandomAddress160(), RandomAddress160(), {0x01, 0x02}, 100 * COIN,
        12345, TestRand256(), timestamp);
    
    // Try to challenge after period - should fail
    uint64_t afterChallenge = timestamp + l2::L2_TO_L1_CHALLENGE_PERIOD + 1;
    uint160 challenger = RandomAddress160();
    std::vector<unsigned char> proof = {0x01, 0x02, 0x03};
    
    BOOST_CHECK(!messaging.ChallengeL2ToL1Message(messageId, challenger, proof, afterChallenge));
}

BOOST_AUTO_TEST_CASE(queue_message_for_next_block)
{
    l2::CrossLayerMessaging messaging(1);
    
    l2::L1ToL2Message msg = RandomL1ToL2Message(1000);
    
    uint64_t currentBlock = 100;
    BOOST_CHECK(messaging.QueueL1ToL2Message(msg, currentBlock));
    
    BOOST_CHECK_EQUAL(messaging.GetQueuedMessageCount(), 1u);
    
    // Get queued messages for next block
    auto queued = messaging.GetQueuedMessagesForBlock(currentBlock + 1);
    BOOST_CHECK_EQUAL(queued.size(), 1u);
}

BOOST_AUTO_TEST_CASE(process_queued_messages)
{
    l2::CrossLayerMessaging messaging(1);
    
    // Queue multiple messages
    uint64_t currentBlock = 100;
    for (int i = 0; i < 3; ++i) {
        l2::L1ToL2Message msg = RandomL1ToL2Message(1000 + i);
        messaging.QueueL1ToL2Message(msg, currentBlock);
    }
    
    BOOST_CHECK_EQUAL(messaging.GetQueuedMessageCount(), 3u);
    
    // Process queued messages
    size_t processed = messaging.ProcessQueuedMessages(currentBlock + 1);
    BOOST_CHECK_EQUAL(processed, 3u);
    
    // Queue should be empty for that block
    BOOST_CHECK_EQUAL(messaging.GetQueuedMessagesForBlock(currentBlock + 1).size(), 0u);
}

// ============================================================================
// Reentrancy Protection Tests (Requirements 28.1, 28.4)
// ============================================================================

BOOST_AUTO_TEST_CASE(reentrancy_guard_basic)
{
    l2::ReentrancyGuard guard;
    
    uint256 messageId = TestRand256();
    
    // First acquire should succeed
    BOOST_CHECK(guard.TryAcquire(messageId));
    BOOST_CHECK(guard.IsExecuting(messageId));
    BOOST_CHECK_EQUAL(guard.GetExecutingCount(), 1u);
    
    // Second acquire should fail (reentrancy)
    BOOST_CHECK(!guard.TryAcquire(messageId));
    
    // Release
    guard.Release(messageId);
    BOOST_CHECK(!guard.IsExecuting(messageId));
    BOOST_CHECK_EQUAL(guard.GetExecutingCount(), 0u);
    
    // Can acquire again after release
    BOOST_CHECK(guard.TryAcquire(messageId));
}

BOOST_AUTO_TEST_CASE(scoped_reentrancy_guard)
{
    l2::ReentrancyGuard guard;
    uint256 messageId = TestRand256();
    
    {
        l2::ScopedReentrancyGuard scoped(guard, messageId);
        BOOST_CHECK(scoped.Acquired());
        BOOST_CHECK(guard.IsExecuting(messageId));
        
        // Try to acquire again - should fail
        l2::ScopedReentrancyGuard scoped2(guard, messageId);
        BOOST_CHECK(!scoped2.Acquired());
    }
    
    // After scope, should be released
    BOOST_CHECK(!guard.IsExecuting(messageId));
}

BOOST_AUTO_TEST_CASE(execute_message_safe_prevents_reentrancy)
{
    l2::CrossLayerMessaging messaging(1);
    
    uint256 messageId = TestRand256();
    uint160 target = RandomAddress160();
    std::vector<unsigned char> data = {0x01, 0x02};
    
    // Set up a callback that tries to re-execute the same message
    bool reentrancyAttempted = false;
    bool reentrancyBlocked = false;
    
    messaging.SetExecutionCallback([&](const uint160& t, const std::vector<unsigned char>& d,
                                        CAmount v, uint64_t g) -> l2::MessageExecutionResult {
        if (!reentrancyAttempted) {
            reentrancyAttempted = true;
            // Try to execute the same message again (reentrancy)
            auto result = messaging.ExecuteMessageSafe(t, d, v, g, messageId);
            if (!result.success && result.error == "Reentrancy detected") {
                reentrancyBlocked = true;
            }
        }
        return l2::MessageExecutionResult::Success(21000);
    });
    
    // Execute message
    auto result = messaging.ExecuteMessageSafe(target, data, 100 * COIN, 
                                                l2::MESSAGE_GAS_LIMIT, messageId);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK(reentrancyAttempted);
    BOOST_CHECK(reentrancyBlocked);
}

// ============================================================================
// Serialization Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l1_to_l2_message_serialization_roundtrip)
{
    l2::L1ToL2Message original = RandomL1ToL2Message(1000);
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::L1ToL2Message restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(l2_to_l1_message_serialization_roundtrip)
{
    l2::L2ToL1Message original = RandomL2ToL1Message(1000);
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::L2ToL1Message restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}


// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 9: Cross-Layer Message Integrity**
 * 
 * *For any* L1→L2 message, the message content received SHALL be identical
 * to the message content sent.
 * 
 * **Validates: Requirements 9.1, 9.2, 9.4**
 */
BOOST_AUTO_TEST_CASE(property_l1_to_l2_message_integrity)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        // Generate random message parameters
        uint160 sender = RandomAddress160();
        uint160 target = RandomAddress160();
        std::vector<unsigned char> data = RandomMessageData();
        CAmount value = (TestRand64() % 1000 + 1) * COIN;
        uint64_t blockNum = TestRand64() % 1000000;
        uint256 txHash = TestRand256();
        uint64_t timestamp = 1000 + iteration;
        
        // Send message
        uint256 messageId = messaging.SendL1ToL2(sender, target, data, value,
                                                  blockNum, txHash, timestamp);
        
        BOOST_REQUIRE_MESSAGE(!messageId.IsNull(),
            "Message ID should not be null in iteration " << iteration);
        
        // Retrieve message
        auto retrieved = messaging.GetL1ToL2Message(messageId);
        BOOST_REQUIRE_MESSAGE(retrieved.has_value(),
            "Message should be retrievable in iteration " << iteration);
        
        // Verify integrity - all fields should match
        BOOST_CHECK_MESSAGE(retrieved->l1Sender == sender,
            "Sender should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->l2Target == target,
            "Target should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->data == data,
            "Data should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->value == value,
            "Value should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->l1BlockNumber == blockNum,
            "Block number should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->l1TxHash == txHash,
            "Transaction hash should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->timestamp == timestamp,
            "Timestamp should match in iteration " << iteration);
    }
}

/**
 * **Property 9: Cross-Layer Message Integrity (L2→L1)**
 * 
 * *For any* L2→L1 message, the message content received SHALL be identical
 * to the message content sent.
 * 
 * **Validates: Requirements 9.1, 9.2, 9.4**
 */
BOOST_AUTO_TEST_CASE(property_l2_to_l1_message_integrity)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        // Generate random message parameters
        uint160 sender = RandomAddress160();
        uint160 target = RandomAddress160();
        std::vector<unsigned char> data = RandomMessageData();
        CAmount value = (TestRand64() % 1000 + 1) * COIN;
        uint64_t blockNum = TestRand64() % 1000000;
        uint256 stateRoot = TestRand256();
        uint64_t timestamp = 1000 + iteration;
        
        // Send message
        uint256 messageId = messaging.SendL2ToL1(sender, target, data, value,
                                                  blockNum, stateRoot, timestamp);
        
        BOOST_REQUIRE_MESSAGE(!messageId.IsNull(),
            "Message ID should not be null in iteration " << iteration);
        
        // Retrieve message
        auto retrieved = messaging.GetL2ToL1Message(messageId);
        BOOST_REQUIRE_MESSAGE(retrieved.has_value(),
            "Message should be retrievable in iteration " << iteration);
        
        // Verify integrity - all fields should match
        BOOST_CHECK_MESSAGE(retrieved->l2Sender == sender,
            "Sender should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->l1Target == target,
            "Target should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->data == data,
            "Data should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->value == value,
            "Value should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->l2BlockNumber == blockNum,
            "Block number should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->stateRoot == stateRoot,
            "State root should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(retrieved->timestamp == timestamp,
            "Timestamp should match in iteration " << iteration);
    }
}

/**
 * **Property 9: Message Serialization Round-Trip**
 * 
 * *For any* message, serializing and deserializing SHALL produce an
 * identical message.
 * 
 * **Validates: Requirements 9.4**
 */
BOOST_AUTO_TEST_CASE(property_message_serialization_roundtrip)
{
    // Run 100 iterations for L1→L2 messages
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::L1ToL2Message original = RandomL1ToL2Message(1000 + iteration);
        
        std::vector<unsigned char> serialized = original.Serialize();
        
        l2::L1ToL2Message restored;
        BOOST_REQUIRE_MESSAGE(restored.Deserialize(serialized),
            "L1→L2 deserialization should succeed in iteration " << iteration);
        
        BOOST_CHECK_MESSAGE(original == restored,
            "L1→L2 message should be identical after round-trip in iteration " << iteration);
    }
    
    // Run 100 iterations for L2→L1 messages
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::L2ToL1Message original = RandomL2ToL1Message(1000 + iteration);
        
        std::vector<unsigned char> serialized = original.Serialize();
        
        l2::L2ToL1Message restored;
        BOOST_REQUIRE_MESSAGE(restored.Deserialize(serialized),
            "L2→L1 deserialization should succeed in iteration " << iteration);
        
        BOOST_CHECK_MESSAGE(original == restored,
            "L2→L1 message should be identical after round-trip in iteration " << iteration);
    }
}

/**
 * **Property: Message Queue Ordering**
 * 
 * *For any* set of queued messages, messages SHALL be processed in the
 * correct block order.
 * 
 * **Validates: Requirements 28.2**
 */
BOOST_AUTO_TEST_CASE(property_message_queue_ordering)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        uint64_t baseBlock = 100;
        int numMessages = 5 + (TestRand32() % 10);
        
        // Queue messages for different blocks
        std::map<uint64_t, int> messagesPerBlock;
        
        for (int i = 0; i < numMessages; ++i) {
            l2::L1ToL2Message msg = RandomL1ToL2Message(1000 + i);
            uint64_t targetBlock = baseBlock + (TestRand32() % 5);
            
            if (messaging.QueueL1ToL2Message(msg, targetBlock - 1)) {
                messagesPerBlock[targetBlock]++;
            }
        }
        
        // Verify each block has the expected number of messages
        for (const auto& pair : messagesPerBlock) {
            auto queued = messaging.GetQueuedMessagesForBlock(pair.first);
            BOOST_CHECK_MESSAGE(queued.size() == static_cast<size_t>(pair.second),
                "Block " << pair.first << " should have " << pair.second 
                << " messages in iteration " << iteration);
        }
    }
}

/**
 * **Property: Reentrancy Prevention**
 * 
 * *For any* message execution, attempting to re-execute the same message
 * during execution SHALL fail.
 * 
 * **Validates: Requirements 28.1, 28.4**
 */
BOOST_AUTO_TEST_CASE(property_reentrancy_prevention)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::ReentrancyGuard guard;
        
        uint256 messageId = TestRand256();
        
        // Acquire guard
        BOOST_REQUIRE(guard.TryAcquire(messageId));
        
        // Attempt reentrancy multiple times
        for (int attempt = 0; attempt < 5; ++attempt) {
            BOOST_CHECK_MESSAGE(!guard.TryAcquire(messageId),
                "Reentrancy should be blocked in iteration " << iteration 
                << ", attempt " << attempt);
        }
        
        // Release
        guard.Release(messageId);
        
        // Should be able to acquire again
        BOOST_CHECK_MESSAGE(guard.TryAcquire(messageId),
            "Should be able to acquire after release in iteration " << iteration);
        
        guard.Release(messageId);
    }
}

/**
 * **Property: Challenge Period Enforcement**
 * 
 * *For any* L2→L1 message, finalization SHALL only succeed after the
 * challenge period has passed.
 * 
 * **Validates: Requirements 9.2**
 */
BOOST_AUTO_TEST_CASE(property_challenge_period_enforcement)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        uint64_t timestamp = 1000 + iteration * 1000;
        
        // Send message
        uint256 messageId = messaging.SendL2ToL1(
            RandomAddress160(), RandomAddress160(), RandomMessageData(),
            (TestRand64() % 1000 + 1) * COIN, TestRand64() % 1000000,
            TestRand256(), timestamp);
        
        BOOST_REQUIRE(!messageId.IsNull());
        
        // Get the message to check deadline
        auto msg = messaging.GetL2ToL1Message(messageId);
        BOOST_REQUIRE(msg.has_value());
        
        // Try to finalize at random times before deadline - should all fail
        for (int t = 0; t < 5; ++t) {
            uint64_t beforeDeadline = timestamp + (TestRand64() % l2::L2_TO_L1_CHALLENGE_PERIOD);
            BOOST_CHECK_MESSAGE(!messaging.FinalizeL2ToL1Message(messageId, beforeDeadline),
                "Finalization should fail before deadline in iteration " << iteration);
        }
        
        // Finalize after deadline - should succeed
        uint64_t afterDeadline = msg->challengeDeadline + 1;
        BOOST_CHECK_MESSAGE(messaging.FinalizeL2ToL1Message(messageId, afterDeadline),
            "Finalization should succeed after deadline in iteration " << iteration);
    }
}

/**
 * **Property: Message ID Uniqueness**
 * 
 * *For any* set of messages, each message SHALL have a unique ID.
 * 
 * **Validates: Requirements 9.1, 9.2**
 */
BOOST_AUTO_TEST_CASE(property_message_id_uniqueness)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        std::set<uint256> messageIds;
        int numMessages = 20 + (TestRand32() % 30);
        
        for (int i = 0; i < numMessages; ++i) {
            uint256 messageId;
            
            if (i % 2 == 0) {
                // L1→L2 message
                messageId = messaging.SendL1ToL2(
                    RandomAddress160(), RandomAddress160(), RandomMessageData(),
                    (TestRand64() % 1000 + 1) * COIN, TestRand64() % 1000000,
                    TestRand256(), 1000 + i);
            } else {
                // L2→L1 message
                messageId = messaging.SendL2ToL1(
                    RandomAddress160(), RandomAddress160(), RandomMessageData(),
                    (TestRand64() % 1000 + 1) * COIN, TestRand64() % 1000000,
                    TestRand256(), 1000 + i);
            }
            
            BOOST_REQUIRE(!messageId.IsNull());
            
            // Check uniqueness
            BOOST_CHECK_MESSAGE(messageIds.count(messageId) == 0,
                "Message ID should be unique in iteration " << iteration 
                << ", message " << i);
            
            messageIds.insert(messageId);
        }
        
        BOOST_CHECK_EQUAL(messageIds.size(), static_cast<size_t>(numMessages));
    }
}

/**
 * **Property: Statistics Consistency**
 * 
 * *For any* sequence of operations, statistics SHALL accurately reflect
 * the state of the messaging system.
 * 
 * **Validates: Requirements 9.1, 9.2**
 */
BOOST_AUTO_TEST_CASE(property_statistics_consistency)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::CrossLayerMessaging messaging(1);
        
        int numL1ToL2 = TestRand32() % 10 + 1;
        int numL2ToL1 = TestRand32() % 10 + 1;
        
        // Send L1→L2 messages
        for (int i = 0; i < numL1ToL2; ++i) {
            messaging.SendL1ToL2(
                RandomAddress160(), RandomAddress160(), RandomMessageData(),
                (TestRand64() % 1000 + 1) * COIN, TestRand64() % 1000000,
                TestRand256(), 1000 + i);
        }
        
        // Send L2→L1 messages
        for (int i = 0; i < numL2ToL1; ++i) {
            messaging.SendL2ToL1(
                RandomAddress160(), RandomAddress160(), RandomMessageData(),
                (TestRand64() % 1000 + 1) * COIN, TestRand64() % 1000000,
                TestRand256(), 2000 + i);
        }
        
        auto stats = messaging.GetStats();
        
        BOOST_CHECK_MESSAGE(stats.totalL1ToL2Messages == static_cast<uint64_t>(numL1ToL2),
            "L1→L2 count should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(stats.totalL2ToL1Messages == static_cast<uint64_t>(numL2ToL1),
            "L2→L1 count should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(messaging.GetL1ToL2MessageCount() == static_cast<size_t>(numL1ToL2),
            "L1→L2 message count should match in iteration " << iteration);
        BOOST_CHECK_MESSAGE(messaging.GetL2ToL1MessageCount() == static_cast<size_t>(numL2ToL1),
            "L2→L1 message count should match in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
