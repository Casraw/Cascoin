// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_reorg_monitor_tests.cpp
 * @brief Property-based tests for L1 Reorg Monitor
 * 
 * **Feature: cascoin-l2-solution, Property 20: L1 Reorg Recovery**
 * **Validates: Requirements 19.2, 19.3**
 * 
 * Property 20: L1 Reorg Recovery
 * *For any* L1 reorganization affecting anchored L2 state, the L2 state 
 * SHALL revert to the last valid anchor and re-process subsequent transactions.
 */

#include <l2/reorg_monitor.h>
#include <l2/state_manager.h>
#include <l2/account_state.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <memory>

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
 * Helper function to generate a random L1 block info
 */
static l2::L1BlockInfo RandomL1Block(uint64_t blockNumber, const uint256& prevHash)
{
    l2::L1BlockInfo block;
    block.blockNumber = blockNumber;
    block.blockHash = TestRand256();
    block.prevBlockHash = prevHash;
    block.timestamp = 1700000000 + blockNumber * 600; // ~10 min blocks
    block.confirmations = 0;
    return block;
}

/**
 * Helper function to generate a chain of L1 blocks
 */
static std::vector<l2::L1BlockInfo> GenerateL1Chain(
    uint64_t startBlock, 
    size_t length,
    const uint256& startPrevHash = uint256())
{
    std::vector<l2::L1BlockInfo> chain;
    uint256 prevHash = startPrevHash;
    
    for (size_t i = 0; i < length; ++i) {
        l2::L1BlockInfo block = RandomL1Block(startBlock + i, prevHash);
        chain.push_back(block);
        prevHash = block.blockHash;
    }
    
    return chain;
}

/**
 * Helper function to generate a random anchor point
 */
static l2::L2AnchorPoint RandomAnchor(uint64_t l1Block, uint64_t l2Block, 
                                       const uint256& l1Hash, const uint256& stateRoot)
{
    l2::L2AnchorPoint anchor;
    anchor.l1BlockNumber = l1Block;
    anchor.l1BlockHash = l1Hash;
    anchor.l2BlockNumber = l2Block;
    anchor.l2StateRoot = stateRoot;
    anchor.batchHash = TestRand256();
    anchor.timestamp = 1700000000 + l1Block * 600;
    anchor.isFinalized = false;
    return anchor;
}

/**
 * Helper function to generate a random transaction log entry
 */
static l2::L2TxLogEntry RandomTxLogEntry(uint64_t l2Block, uint64_t l1Anchor)
{
    l2::L2TxLogEntry entry;
    entry.txHash = TestRand256();
    entry.l2BlockNumber = l2Block;
    entry.l1AnchorBlock = l1Anchor;
    entry.timestamp = 1700000000 + l2Block;
    entry.wasSuccessful = true;
    entry.gasUsed = TestRand64() % 100000;
    return entry;
}

/**
 * Helper function to generate a random account state
 */
static l2::AccountState RandomAccountState()
{
    l2::AccountState state;
    state.balance = TestRand64() % (1000 * COIN);
    state.nonce = TestRand64() % 1000;
    state.hatScore = TestRand32() % 101;
    state.lastActivity = TestRand64() % 1000000;
    return state;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_reorg_monitor_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_monitor_initialization)
{
    l2::ReorgMonitor monitor(1);
    
    BOOST_CHECK_EQUAL(monitor.GetChainId(), 1u);
    BOOST_CHECK_EQUAL(monitor.GetFinalityDepth(), l2::DEFAULT_L1_FINALITY_DEPTH);
    BOOST_CHECK(monitor.IsHealthy());
    
    l2::L1BlockInfo tip = monitor.GetCurrentL1Tip();
    BOOST_CHECK_EQUAL(tip.blockNumber, 0u);
}

BOOST_AUTO_TEST_CASE(process_single_l1_block)
{
    l2::ReorgMonitor monitor(1);
    
    l2::L1BlockInfo block = RandomL1Block(100, uint256());
    auto result = monitor.ProcessL1Block(block);
    
    BOOST_CHECK(!result.reorgDetected);
    
    l2::L1BlockInfo tip = monitor.GetCurrentL1Tip();
    BOOST_CHECK_EQUAL(tip.blockNumber, 100u);
    BOOST_CHECK(tip.blockHash == block.blockHash);
}

BOOST_AUTO_TEST_CASE(process_chain_of_blocks)
{
    l2::ReorgMonitor monitor(1);
    
    auto chain = GenerateL1Chain(100, 10);
    
    for (const auto& block : chain) {
        auto result = monitor.ProcessL1Block(block);
        BOOST_CHECK(!result.reorgDetected);
    }
    
    l2::L1BlockInfo tip = monitor.GetCurrentL1Tip();
    BOOST_CHECK_EQUAL(tip.blockNumber, 109u);
    BOOST_CHECK(tip.blockHash == chain.back().blockHash);
}

BOOST_AUTO_TEST_CASE(detect_simple_reorg)
{
    l2::ReorgMonitor monitor(1);
    
    // Build initial chain
    auto chain = GenerateL1Chain(100, 5);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    // Current tip is at block 104
    // Create a competing block at height 103 (reorg of 2 blocks: 103, 104)
    l2::L1BlockInfo forkBlock = RandomL1Block(103, chain[2].blockHash);
    auto result = monitor.CheckForReorg(forkBlock);
    
    // Should detect reorg
    BOOST_CHECK(result.reorgDetected);
    // Reorg depth is tip(104) - fork(103) + 1 = 2 blocks reorganized
    BOOST_CHECK_EQUAL(result.reorgDepth, 2u);
}

BOOST_AUTO_TEST_CASE(add_and_retrieve_anchor_point)
{
    l2::ReorgMonitor monitor(1);
    
    // Process some L1 blocks first
    auto chain = GenerateL1Chain(100, 10);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    // Add anchor point
    l2::L2AnchorPoint anchor = RandomAnchor(105, 1000, chain[5].blockHash, TestRand256());
    monitor.AddAnchorPoint(anchor);
    
    auto anchors = monitor.GetAnchorPoints();
    BOOST_CHECK_EQUAL(anchors.size(), 1u);
    BOOST_CHECK(anchors[0] == anchor);
}

BOOST_AUTO_TEST_CASE(anchor_finalization)
{
    l2::ReorgMonitor monitor(1, nullptr, 6); // 6 confirmations
    
    // Process initial blocks
    auto chain = GenerateL1Chain(100, 5);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    // Add anchor at block 102
    l2::L2AnchorPoint anchor = RandomAnchor(102, 1000, chain[2].blockHash, TestRand256());
    monitor.AddAnchorPoint(anchor);
    
    // Not finalized yet (only 2 confirmations: 103, 104)
    BOOST_CHECK(!monitor.IsAnchorFinalized(102));
    
    // Add more blocks to reach finality
    auto moreBlocks = GenerateL1Chain(105, 5, chain.back().blockHash);
    for (const auto& block : moreBlocks) {
        monitor.ProcessL1Block(block);
    }
    
    // Now should be finalized (6+ confirmations)
    BOOST_CHECK(monitor.IsAnchorFinalized(102));
}

BOOST_AUTO_TEST_CASE(get_last_valid_anchor)
{
    l2::ReorgMonitor monitor(1);
    
    // Process blocks
    auto chain = GenerateL1Chain(100, 20);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    // Add multiple anchors
    l2::L2AnchorPoint anchor1 = RandomAnchor(105, 500, chain[5].blockHash, TestRand256());
    l2::L2AnchorPoint anchor2 = RandomAnchor(110, 1000, chain[10].blockHash, TestRand256());
    l2::L2AnchorPoint anchor3 = RandomAnchor(115, 1500, chain[15].blockHash, TestRand256());
    
    monitor.AddAnchorPoint(anchor1);
    monitor.AddAnchorPoint(anchor2);
    monitor.AddAnchorPoint(anchor3);
    
    // Get last valid anchor before block 112
    auto lastValid = monitor.GetLastValidAnchor(112);
    BOOST_CHECK(lastValid.has_value());
    BOOST_CHECK_EQUAL(lastValid->l1BlockNumber, 110u);
    
    // Get last valid anchor before block 108
    lastValid = monitor.GetLastValidAnchor(108);
    BOOST_CHECK(lastValid.has_value());
    BOOST_CHECK_EQUAL(lastValid->l1BlockNumber, 105u);
}

BOOST_AUTO_TEST_CASE(transaction_logging)
{
    l2::ReorgMonitor monitor(1);
    
    // Log some transactions
    l2::L2TxLogEntry entry1 = RandomTxLogEntry(100, 50);
    l2::L2TxLogEntry entry2 = RandomTxLogEntry(101, 50);
    l2::L2TxLogEntry entry3 = RandomTxLogEntry(102, 51);
    
    monitor.LogTransaction(entry1);
    monitor.LogTransaction(entry2);
    monitor.LogTransaction(entry3);
    
    // Retrieve by hash
    auto retrieved = monitor.GetTransactionLog(entry2.txHash);
    BOOST_CHECK(retrieved.has_value());
    BOOST_CHECK(retrieved->txHash == entry2.txHash);
    BOOST_CHECK_EQUAL(retrieved->l2BlockNumber, 101u);
    
    // Get transactions in range
    auto txsInRange = monitor.GetTransactionsInRange(100, 101);
    BOOST_CHECK_EQUAL(txsInRange.size(), 2u);
}

BOOST_AUTO_TEST_CASE(transaction_log_pruning)
{
    l2::ReorgMonitor monitor(1);
    
    // Log transactions in multiple blocks
    for (uint64_t block = 100; block < 110; ++block) {
        l2::L2TxLogEntry entry = RandomTxLogEntry(block, 50);
        monitor.LogTransaction(entry);
    }
    
    // Prune logs before block 105
    size_t pruned = monitor.PruneTransactionLogs(105);
    BOOST_CHECK_EQUAL(pruned, 5u);
    
    // Verify remaining transactions
    auto remaining = monitor.GetTransactionsInRange(100, 109);
    BOOST_CHECK_EQUAL(remaining.size(), 5u);
    
    // All remaining should be >= block 105
    for (const auto& entry : remaining) {
        BOOST_CHECK(entry.l2BlockNumber >= 105);
    }
}

BOOST_AUTO_TEST_CASE(notification_callback)
{
    l2::ReorgMonitor monitor(1);
    
    bool callbackCalled = false;
    l2::ReorgDetectionResult capturedDetection;
    l2::ReorgRecoveryResult capturedRecovery;
    
    monitor.RegisterNotificationCallback(
        [&](const l2::ReorgDetectionResult& det, const l2::ReorgRecoveryResult& rec) {
            callbackCalled = true;
            capturedDetection = det;
            capturedRecovery = rec;
        });
    
    // Build chain and add anchor
    auto chain = GenerateL1Chain(100, 10);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    l2::L2AnchorPoint anchor = RandomAnchor(102, 500, chain[2].blockHash, TestRand256());
    monitor.AddAnchorPoint(anchor);
    
    // Simulate reorg detection
    l2::ReorgDetectionResult detection = l2::ReorgDetectionResult::Detected(
        2, 105, chain[5].blockHash, chain.back(), chain[7]);
    
    // Handle reorg (this should trigger callback)
    auto result = monitor.HandleReorg(detection);
    
    BOOST_CHECK(callbackCalled);
    BOOST_CHECK(capturedDetection.reorgDetected);
}

BOOST_AUTO_TEST_CASE(clear_state)
{
    l2::ReorgMonitor monitor(1);
    
    // Add some data
    auto chain = GenerateL1Chain(100, 5);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    l2::L2AnchorPoint anchor = RandomAnchor(102, 500, chain[2].blockHash, TestRand256());
    monitor.AddAnchorPoint(anchor);
    
    l2::L2TxLogEntry entry = RandomTxLogEntry(100, 50);
    monitor.LogTransaction(entry);
    
    // Clear
    monitor.Clear();
    
    // Verify cleared
    BOOST_CHECK_EQUAL(monitor.GetCurrentL1Tip().blockNumber, 0u);
    BOOST_CHECK(monitor.GetAnchorPoints().empty());
    BOOST_CHECK(!monitor.GetTransactionLog(entry.txHash).has_value());
}

BOOST_AUTO_TEST_CASE(statistics_output)
{
    l2::ReorgMonitor monitor(1);
    
    // Add some data
    auto chain = GenerateL1Chain(100, 5);
    for (const auto& block : chain) {
        monitor.ProcessL1Block(block);
    }
    
    std::string stats = monitor.GetStatistics();
    
    BOOST_CHECK(!stats.empty());
    BOOST_CHECK(stats.find("Chain ID: 1") != std::string::npos);
    BOOST_CHECK(stats.find("Current L1 Tip: 104") != std::string::npos);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 20: L1 Reorg Recovery**
 * 
 * *For any* L1 reorganization affecting anchored L2 state, the L2 state 
 * SHALL revert to the last valid anchor and re-process subsequent transactions.
 * 
 * **Validates: Requirements 19.2, 19.3**
 */
BOOST_AUTO_TEST_CASE(property_reorg_recovery_reverts_to_valid_anchor)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Create state manager
        auto stateManager = std::make_shared<l2::L2StateManager>(1);
        l2::ReorgMonitor monitor(1, stateManager, 6);
        
        // Build L1 chain
        uint64_t chainLength = 20 + (TestRand32() % 10);
        auto chain = GenerateL1Chain(100, chainLength);
        
        for (const auto& block : chain) {
            monitor.ProcessL1Block(block);
        }
        
        // Add anchors at regular intervals
        std::vector<l2::L2AnchorPoint> anchors;
        for (uint64_t i = 5; i < chainLength; i += 5) {
            uint64_t l1Block = 100 + i;
            uint64_t l2Block = i * 100;
            
            // Set up state at this anchor point
            uint256 addr = TestRand256();
            l2::AccountState state = RandomAccountState();
            stateManager->SetAccountState(addr, state);
            stateManager->CreateSnapshot(l2Block, l1Block);
            
            l2::L2AnchorPoint anchor = RandomAnchor(
                l1Block, l2Block, chain[i].blockHash, stateManager->GetStateRoot());
            monitor.AddAnchorPoint(anchor);
            anchors.push_back(anchor);
        }
        
        // Simulate reorg at a random point
        uint64_t reorgPoint = 100 + 10 + (TestRand32() % (chainLength - 15));
        
        // Find expected anchor after reorg
        l2::L2AnchorPoint* expectedAnchor = nullptr;
        for (auto it = anchors.rbegin(); it != anchors.rend(); ++it) {
            if (it->l1BlockNumber < reorgPoint) {
                expectedAnchor = &(*it);
                break;
            }
        }
        
        if (!expectedAnchor) continue; // Skip if no valid anchor
        
        // Perform revert
        bool reverted = monitor.RevertToLastValidAnchor(reorgPoint);
        
        BOOST_CHECK_MESSAGE(reverted,
            "Revert should succeed for iteration " << iteration);
        
        // Verify state was reverted to anchor
        uint256 currentRoot = stateManager->GetStateRoot();
        BOOST_CHECK_MESSAGE(currentRoot == expectedAnchor->l2StateRoot,
            "State root should match anchor for iteration " << iteration);
        
        // Verify L2 block number was reverted
        uint64_t currentBlock = stateManager->GetBlockNumber();
        BOOST_CHECK_MESSAGE(currentBlock == expectedAnchor->l2BlockNumber,
            "L2 block should match anchor for iteration " << iteration);
    }
}

/**
 * **Property: Anchor Finalization Consistency**
 * 
 * *For any* anchor point, it SHALL become finalized when it has at least
 * finalityDepth confirmations on L1.
 * 
 * **Validates: Requirement 19.5**
 */
BOOST_AUTO_TEST_CASE(property_anchor_finalization_consistency)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        uint32_t finalityDepth = 3 + (TestRand32() % 5); // 3-7 confirmations
        l2::ReorgMonitor monitor(1, nullptr, finalityDepth);
        
        // Build initial chain
        auto chain = GenerateL1Chain(100, 5);
        for (const auto& block : chain) {
            monitor.ProcessL1Block(block);
        }
        
        // Add anchor at block 102
        l2::L2AnchorPoint anchor = RandomAnchor(102, 1000, chain[2].blockHash, TestRand256());
        monitor.AddAnchorPoint(anchor);
        
        // Calculate confirmations: tip(104) - anchor(102) = 2
        uint32_t currentConfirmations = 2;
        
        // Should not be finalized yet if finalityDepth > 2
        if (finalityDepth > currentConfirmations) {
            BOOST_CHECK_MESSAGE(!monitor.IsAnchorFinalized(102),
                "Anchor should not be finalized with " << currentConfirmations 
                << " confirmations (need " << finalityDepth << ") for iteration " << iteration);
        }
        
        // Add blocks until finalized
        uint64_t nextBlock = 105;
        uint256 prevHash = chain.back().blockHash;
        
        while (currentConfirmations < finalityDepth) {
            l2::L1BlockInfo block = RandomL1Block(nextBlock, prevHash);
            monitor.ProcessL1Block(block);
            prevHash = block.blockHash;
            ++nextBlock;
            ++currentConfirmations;
        }
        
        // Now should be finalized
        BOOST_CHECK_MESSAGE(monitor.IsAnchorFinalized(102),
            "Anchor should be finalized with " << currentConfirmations 
            << " confirmations for iteration " << iteration);
    }
}

/**
 * **Property: Transaction Log Completeness**
 * 
 * *For any* logged transaction, it SHALL be retrievable by hash and
 * included in range queries that cover its block.
 * 
 * **Validates: Requirement 19.6**
 */
BOOST_AUTO_TEST_CASE(property_transaction_log_completeness)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ReorgMonitor monitor(1);
        
        // Generate random transactions
        size_t numTxs = 5 + (TestRand32() % 10);
        std::vector<l2::L2TxLogEntry> entries;
        
        uint64_t minBlock = UINT64_MAX;
        uint64_t maxBlock = 0;
        
        for (size_t i = 0; i < numTxs; ++i) {
            uint64_t block = 100 + (TestRand32() % 20);
            l2::L2TxLogEntry entry = RandomTxLogEntry(block, 50);
            entries.push_back(entry);
            monitor.LogTransaction(entry);
            
            minBlock = std::min(minBlock, block);
            maxBlock = std::max(maxBlock, block);
        }
        
        // Verify each transaction is retrievable by hash
        for (const auto& entry : entries) {
            auto retrieved = monitor.GetTransactionLog(entry.txHash);
            BOOST_CHECK_MESSAGE(retrieved.has_value(),
                "Transaction should be retrievable for iteration " << iteration);
            BOOST_CHECK_MESSAGE(retrieved->txHash == entry.txHash,
                "Retrieved tx hash should match for iteration " << iteration);
        }
        
        // Verify range query includes all transactions
        auto rangeResult = monitor.GetTransactionsInRange(minBlock, maxBlock);
        BOOST_CHECK_MESSAGE(rangeResult.size() == numTxs,
            "Range query should return all " << numTxs << " transactions, got " 
            << rangeResult.size() << " for iteration " << iteration);
    }
}

/**
 * **Property: Affected Transactions Identification**
 * 
 * *For any* reorg, all transactions in L2 blocks after the affected anchor
 * SHALL be identified as affected.
 * 
 * **Validates: Requirement 19.4**
 */
BOOST_AUTO_TEST_CASE(property_affected_transactions_identification)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::ReorgMonitor monitor(1);
        
        // Build L1 chain
        auto chain = GenerateL1Chain(100, 20);
        for (const auto& block : chain) {
            monitor.ProcessL1Block(block);
        }
        
        // Add anchor at block 105
        l2::L2AnchorPoint anchor = RandomAnchor(105, 500, chain[5].blockHash, TestRand256());
        monitor.AddAnchorPoint(anchor);
        
        // Log transactions before and after anchor
        std::vector<uint256> txsBeforeAnchor;
        std::vector<uint256> txsAfterAnchor;
        
        for (uint64_t l2Block = 400; l2Block < 600; l2Block += 20) {
            l2::L2TxLogEntry entry = RandomTxLogEntry(l2Block, 50);
            monitor.LogTransaction(entry);
            
            if (l2Block < 500) {
                txsBeforeAnchor.push_back(entry.txHash);
            } else {
                txsAfterAnchor.push_back(entry.txHash);
            }
        }
        
        // Get affected transactions for reorg at block 110
        auto affected = monitor.GetAffectedTransactions(110);
        
        // All transactions after anchor should be affected
        for (const auto& txHash : txsAfterAnchor) {
            bool found = std::find(affected.begin(), affected.end(), txHash) != affected.end();
            BOOST_CHECK_MESSAGE(found,
                "Transaction after anchor should be affected for iteration " << iteration);
        }
    }
}

/**
 * **Property: Reorg Detection Determinism**
 * 
 * *For any* chain state and new block, reorg detection SHALL produce
 * consistent results.
 * 
 * **Validates: Requirement 19.1**
 */
BOOST_AUTO_TEST_CASE(property_reorg_detection_determinism)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Create two identical monitors
        l2::ReorgMonitor monitor1(1);
        l2::ReorgMonitor monitor2(1);
        
        // Build identical chains
        auto chain = GenerateL1Chain(100, 10);
        for (const auto& block : chain) {
            monitor1.ProcessL1Block(block);
            monitor2.ProcessL1Block(block);
        }
        
        // Create a fork block
        l2::L1BlockInfo forkBlock = RandomL1Block(107, chain[6].blockHash);
        
        // Check for reorg on both monitors
        auto result1 = monitor1.CheckForReorg(forkBlock);
        auto result2 = monitor2.CheckForReorg(forkBlock);
        
        // Results should be identical
        BOOST_CHECK_MESSAGE(result1.reorgDetected == result2.reorgDetected,
            "Reorg detection should be deterministic for iteration " << iteration);
        
        if (result1.reorgDetected && result2.reorgDetected) {
            BOOST_CHECK_MESSAGE(result1.reorgDepth == result2.reorgDepth,
                "Reorg depth should be deterministic for iteration " << iteration);
            BOOST_CHECK_MESSAGE(result1.forkPoint == result2.forkPoint,
                "Fork point should be deterministic for iteration " << iteration);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
