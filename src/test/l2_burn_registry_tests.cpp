// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_burn_registry_tests.cpp
 * @brief Property-based tests for L2 Burn Registry
 * 
 * **Feature: l2-bridge-security, Property 3: Double-Mint Prevention**
 * **Validates: Requirements 2.4, 5.3, 5.4**
 * 
 * Property 3: Double-Mint Prevention
 * *For any* L1 transaction hash, the system SHALL mint L2 tokens at most once.
 * If a mint request references an already-processed L1 transaction, the system
 * SHALL reject it.
 */

#include <l2/burn_registry.h>
#include <random.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <set>
#include <vector>

namespace {

// Local random context for tests
FastRandomContext g_test_rand_ctx(true);

uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

/**
 * Helper to generate a random uint256 hash
 */
uint256 RandomHash()
{
    uint256 hash;
    for (int i = 0; i < 8; ++i) {
        ((uint32_t*)hash.begin())[i] = TestRand32();
    }
    return hash;
}

/**
 * Helper to generate a random uint160 address
 */
uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        ((uint32_t*)addr.begin())[i] = TestRand32();
    }
    return addr;
}

/**
 * Helper to generate a random valid burn amount
 */
CAmount RandomBurnAmount()
{
    return (TestRand64() % (1000 * COIN)) + 1;
}

/**
 * Helper to create a valid BurnRecord with random data
 */
l2::BurnRecord CreateRandomBurnRecord()
{
    l2::BurnRecord record;
    record.l1TxHash = RandomHash();
    record.l1BlockNumber = (TestRand64() % 1000000) + 1;
    record.l1BlockHash = RandomHash();
    record.l2Recipient = RandomAddress();
    record.amount = RandomBurnAmount();
    record.l2MintBlock = (TestRand64() % 1000000) + 1;
    record.l2MintTxHash = RandomHash();
    record.timestamp = (TestRand64() % 2000000000) + 1;
    return record;
}

/**
 * Helper to create a BurnRecord with specific L1 TX hash
 */
l2::BurnRecord CreateBurnRecordWithHash(const uint256& l1TxHash)
{
    l2::BurnRecord record = CreateRandomBurnRecord();
    record.l1TxHash = l1TxHash;
    return record;
}

/**
 * Helper to create a BurnRecord with specific L2 block
 */
l2::BurnRecord CreateBurnRecordAtBlock(uint64_t l2Block)
{
    l2::BurnRecord record = CreateRandomBurnRecord();
    record.l2MintBlock = l2Block;
    return record;
}

/**
 * Helper to create a BurnRecord with specific recipient
 */
l2::BurnRecord CreateBurnRecordForAddress(const uint160& address)
{
    l2::BurnRecord record = CreateRandomBurnRecord();
    record.l2Recipient = address;
    return record;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_burn_registry_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(registry_construction)
{
    l2::BurnRegistry registry;
    
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 0u);
    BOOST_CHECK_EQUAL(registry.GetTotalBurned(), 0);
}

BOOST_AUTO_TEST_CASE(burn_record_validity)
{
    // Valid record
    l2::BurnRecord valid = CreateRandomBurnRecord();
    BOOST_CHECK(valid.IsValid());
    
    // Invalid: null L1 TX hash
    l2::BurnRecord invalidHash = valid;
    invalidHash.l1TxHash.SetNull();
    BOOST_CHECK(!invalidHash.IsValid());
    
    // Invalid: zero L1 block number
    l2::BurnRecord invalidBlock = valid;
    invalidBlock.l1BlockNumber = 0;
    BOOST_CHECK(!invalidBlock.IsValid());
    
    // Invalid: zero amount
    l2::BurnRecord invalidAmount = valid;
    invalidAmount.amount = 0;
    BOOST_CHECK(!invalidAmount.IsValid());
    
    // Invalid: zero timestamp
    l2::BurnRecord invalidTime = valid;
    invalidTime.timestamp = 0;
    BOOST_CHECK(!invalidTime.IsValid());
}

BOOST_AUTO_TEST_CASE(burn_record_serialization)
{
    l2::BurnRecord original = CreateRandomBurnRecord();
    
    std::vector<unsigned char> serialized = original.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    l2::BurnRecord restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(record_burn_success)
{
    l2::BurnRegistry registry;
    l2::BurnRecord record = CreateRandomBurnRecord();
    
    BOOST_CHECK(registry.RecordBurn(record));
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 1u);
    BOOST_CHECK_EQUAL(registry.GetTotalBurned(), record.amount);
    BOOST_CHECK(registry.IsProcessed(record.l1TxHash));
}

BOOST_AUTO_TEST_CASE(record_burn_duplicate_rejected)
{
    l2::BurnRegistry registry;
    l2::BurnRecord record = CreateRandomBurnRecord();
    
    // First recording should succeed
    BOOST_CHECK(registry.RecordBurn(record));
    
    // Second recording with same L1 TX hash should fail
    BOOST_CHECK(!registry.RecordBurn(record));
    
    // Count should still be 1
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 1u);
}

BOOST_AUTO_TEST_CASE(get_burn_record)
{
    l2::BurnRegistry registry;
    l2::BurnRecord record = CreateRandomBurnRecord();
    
    registry.RecordBurn(record);
    
    auto retrieved = registry.GetBurnRecord(record.l1TxHash);
    BOOST_REQUIRE(retrieved.has_value());
    BOOST_CHECK(retrieved.value() == record);
    
    // Non-existent record
    auto notFound = registry.GetBurnRecord(RandomHash());
    BOOST_CHECK(!notFound.has_value());
}

BOOST_AUTO_TEST_CASE(get_burns_for_address)
{
    l2::BurnRegistry registry;
    uint160 address = RandomAddress();
    
    // Add multiple burns for the same address
    std::vector<l2::BurnRecord> records;
    for (int i = 0; i < 5; ++i) {
        l2::BurnRecord record = CreateBurnRecordForAddress(address);
        records.push_back(record);
        registry.RecordBurn(record);
    }
    
    // Add burns for other addresses
    for (int i = 0; i < 3; ++i) {
        registry.RecordBurn(CreateRandomBurnRecord());
    }
    
    auto addressBurns = registry.GetBurnsForAddress(address);
    BOOST_CHECK_EQUAL(addressBurns.size(), 5u);
    
    // Verify all returned burns are for the correct address
    for (const auto& burn : addressBurns) {
        BOOST_CHECK(burn.l2Recipient == address);
    }
}

BOOST_AUTO_TEST_CASE(get_burn_history)
{
    l2::BurnRegistry registry;
    
    // Add burns at different L2 blocks
    for (uint64_t block = 100; block <= 200; block += 10) {
        l2::BurnRecord record = CreateBurnRecordAtBlock(block);
        registry.RecordBurn(record);
    }
    
    // Query range [120, 170]
    auto history = registry.GetBurnHistory(120, 170);
    
    // Should include blocks 120, 130, 140, 150, 160, 170 = 6 burns
    BOOST_CHECK_EQUAL(history.size(), 6u);
    
    for (const auto& burn : history) {
        BOOST_CHECK(burn.l2MintBlock >= 120 && burn.l2MintBlock <= 170);
    }
}

BOOST_AUTO_TEST_CASE(handle_reorg)
{
    l2::BurnRegistry registry;
    
    // Add burns at blocks 100, 200, 300, 400, 500
    std::vector<l2::BurnRecord> records;
    for (uint64_t block = 100; block <= 500; block += 100) {
        l2::BurnRecord record = CreateBurnRecordAtBlock(block);
        records.push_back(record);
        registry.RecordBurn(record);
    }
    
    CAmount totalBefore = registry.GetTotalBurned();
    (void)totalBefore;  // Suppress unused variable warning
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 5u);
    
    // Reorg from block 300 - should remove burns at 300, 400, 500
    size_t removed = registry.HandleReorg(300);
    BOOST_CHECK_EQUAL(removed, 3u);
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 2u);
    
    // Burns at 100 and 200 should still exist
    BOOST_CHECK(registry.IsProcessed(records[0].l1TxHash));
    BOOST_CHECK(registry.IsProcessed(records[1].l1TxHash));
    
    // Burns at 300, 400, 500 should be removed
    BOOST_CHECK(!registry.IsProcessed(records[2].l1TxHash));
    BOOST_CHECK(!registry.IsProcessed(records[3].l1TxHash));
    BOOST_CHECK(!registry.IsProcessed(records[4].l1TxHash));
    
    // Total burned should be reduced
    CAmount expectedTotal = records[0].amount + records[1].amount;
    BOOST_CHECK_EQUAL(registry.GetTotalBurned(), expectedTotal);
}

BOOST_AUTO_TEST_CASE(clear_registry)
{
    l2::BurnRegistry registry;
    
    // Add some burns
    for (int i = 0; i < 10; ++i) {
        registry.RecordBurn(CreateRandomBurnRecord());
    }
    
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 10u);
    BOOST_CHECK(registry.GetTotalBurned() > 0);
    
    registry.Clear();
    
    BOOST_CHECK_EQUAL(registry.GetBurnCount(), 0u);
    BOOST_CHECK_EQUAL(registry.GetTotalBurned(), 0);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 3: Double-Mint Prevention**
 * 
 * *For any* L1 transaction hash, the system SHALL mint L2 tokens at most once.
 * If a mint request references an already-processed L1 transaction, the system
 * SHALL reject it.
 * 
 * **Validates: Requirements 2.4, 5.3, 5.4**
 */
BOOST_AUTO_TEST_CASE(property_double_mint_prevention)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Generate a random L1 TX hash
        uint256 l1TxHash = RandomHash();
        
        // Create first burn record
        l2::BurnRecord record1 = CreateBurnRecordWithHash(l1TxHash);
        
        // Property: First recording should succeed
        BOOST_CHECK_MESSAGE(registry.RecordBurn(record1),
            "First burn recording should succeed in iteration " << iteration);
        
        // Property: IsProcessed should return true after recording
        BOOST_CHECK_MESSAGE(registry.IsProcessed(l1TxHash),
            "IsProcessed should return true after recording in iteration " << iteration);
        
        // Create second burn record with same L1 TX hash but different data
        l2::BurnRecord record2 = CreateBurnRecordWithHash(l1TxHash);
        record2.amount = record1.amount + 1000;  // Different amount
        record2.l2MintBlock = record1.l2MintBlock + 100;  // Different block
        
        // Property: Second recording with same L1 TX hash should fail
        BOOST_CHECK_MESSAGE(!registry.RecordBurn(record2),
            "Second burn recording with same L1 TX hash should fail in iteration " << iteration);
        
        // Property: Count should still be 1
        BOOST_CHECK_MESSAGE(registry.GetBurnCount() == 1,
            "Burn count should be 1 after duplicate attempt in iteration " << iteration);
        
        // Property: Total burned should be from first record only
        BOOST_CHECK_MESSAGE(registry.GetTotalBurned() == record1.amount,
            "Total burned should equal first record amount in iteration " << iteration);
        
        // Property: Retrieved record should match first record
        auto retrieved = registry.GetBurnRecord(l1TxHash);
        BOOST_CHECK_MESSAGE(retrieved.has_value() && retrieved.value() == record1,
            "Retrieved record should match first record in iteration " << iteration);
    }
}

/**
 * **Property 3 (continued): Multiple unique burns are all recorded**
 * 
 * *For any* set of unique L1 transaction hashes, all burns should be recorded
 * and the total should equal the sum of all amounts.
 * 
 * **Validates: Requirements 2.4, 5.3, 5.4**
 */
BOOST_AUTO_TEST_CASE(property_multiple_unique_burns)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Generate random number of burns (1-20)
        size_t numBurns = (TestRand32() % 20) + 1;
        
        std::set<uint256> usedHashes;
        CAmount expectedTotal = 0;
        
        for (size_t i = 0; i < numBurns; ++i) {
            // Generate unique L1 TX hash
            uint256 l1TxHash;
            do {
                l1TxHash = RandomHash();
            } while (usedHashes.count(l1TxHash) > 0);
            usedHashes.insert(l1TxHash);
            
            l2::BurnRecord record = CreateBurnRecordWithHash(l1TxHash);
            expectedTotal += record.amount;
            
            // Property: Each unique burn should be recorded
            BOOST_CHECK_MESSAGE(registry.RecordBurn(record),
                "Unique burn " << i << " should be recorded in iteration " << iteration);
        }
        
        // Property: Count should equal number of unique burns
        BOOST_CHECK_MESSAGE(registry.GetBurnCount() == numBurns,
            "Burn count should equal " << numBurns << " in iteration " << iteration);
        
        // Property: Total burned should equal sum of all amounts
        BOOST_CHECK_MESSAGE(registry.GetTotalBurned() == expectedTotal,
            "Total burned should equal sum of amounts in iteration " << iteration);
        
        // Property: All burns should be queryable
        for (const uint256& hash : usedHashes) {
            BOOST_CHECK_MESSAGE(registry.IsProcessed(hash),
                "All recorded burns should be queryable in iteration " << iteration);
        }
    }
}

/**
 * **Property 3 (continued): Reorg allows re-processing**
 * 
 * *For any* burn that is removed via HandleReorg, it should be possible
 * to record it again.
 * 
 * **Validates: Requirements 5.6**
 */
BOOST_AUTO_TEST_CASE(property_reorg_allows_reprocessing)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Create a burn at a specific block
        uint64_t burnBlock = (TestRand64() % 1000) + 100;
        l2::BurnRecord record = CreateBurnRecordAtBlock(burnBlock);
        
        // Record the burn
        BOOST_CHECK(registry.RecordBurn(record));
        BOOST_CHECK(registry.IsProcessed(record.l1TxHash));
        
        // Reorg from a block before the burn
        uint64_t reorgBlock = burnBlock - (TestRand64() % 50);
        registry.HandleReorg(reorgBlock);
        
        // Property: After reorg, burn should no longer be processed
        BOOST_CHECK_MESSAGE(!registry.IsProcessed(record.l1TxHash),
            "Burn should not be processed after reorg in iteration " << iteration);
        
        // Property: Should be able to record the same burn again
        BOOST_CHECK_MESSAGE(registry.RecordBurn(record),
            "Should be able to re-record burn after reorg in iteration " << iteration);
        
        // Property: Burn should be processed again
        BOOST_CHECK_MESSAGE(registry.IsProcessed(record.l1TxHash),
            "Burn should be processed after re-recording in iteration " << iteration);
    }
}

/**
 * **Property: Address index consistency**
 * 
 * *For any* set of burns, GetBurnsForAddress should return exactly the burns
 * for that address.
 * 
 * **Validates: Requirements 5.5**
 */
BOOST_AUTO_TEST_CASE(property_address_index_consistency)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Generate a few addresses
        std::vector<uint160> addresses;
        for (int i = 0; i < 3; ++i) {
            addresses.push_back(RandomAddress());
        }
        
        // Track expected burns per address
        std::map<uint160, std::vector<uint256>> expectedBurns;
        
        // Add random burns for random addresses
        size_t numBurns = (TestRand32() % 20) + 5;
        for (size_t i = 0; i < numBurns; ++i) {
            uint160 addr = addresses[TestRand32() % addresses.size()];
            l2::BurnRecord record = CreateBurnRecordForAddress(addr);
            
            if (registry.RecordBurn(record)) {
                expectedBurns[addr].push_back(record.l1TxHash);
            }
        }
        
        // Property: GetBurnsForAddress should return correct burns
        for (const uint160& addr : addresses) {
            auto burns = registry.GetBurnsForAddress(addr);
            
            BOOST_CHECK_MESSAGE(burns.size() == expectedBurns[addr].size(),
                "Burn count for address should match in iteration " << iteration);
            
            // All returned burns should be for the correct address
            for (const auto& burn : burns) {
                BOOST_CHECK_MESSAGE(burn.l2Recipient == addr,
                    "All burns should be for correct address in iteration " << iteration);
            }
        }
    }
}

/**
 * **Property: Block index consistency for reorg**
 * 
 * *For any* reorg point, only burns at or after that block should be removed.
 * 
 * **Validates: Requirements 5.6**
 */
BOOST_AUTO_TEST_CASE(property_block_index_consistency)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Add burns at various blocks
        std::map<uint64_t, std::vector<uint256>> burnsByBlock;
        
        for (int i = 0; i < 20; ++i) {
            uint64_t block = (TestRand64() % 100) + 1;
            l2::BurnRecord record = CreateBurnRecordAtBlock(block);
            
            if (registry.RecordBurn(record)) {
                burnsByBlock[block].push_back(record.l1TxHash);
            }
        }
        
        // Pick a random reorg point
        uint64_t reorgBlock = (TestRand64() % 100) + 1;
        
        // Count expected removals
        size_t expectedRemoved = 0;
        for (const auto& pair : burnsByBlock) {
            if (pair.first >= reorgBlock) {
                expectedRemoved += pair.second.size();
            }
        }
        
        size_t countBefore = registry.GetBurnCount();
        size_t removed = registry.HandleReorg(reorgBlock);
        
        // Property: Removed count should match expected
        BOOST_CHECK_MESSAGE(removed == expectedRemoved,
            "Removed count should match expected in iteration " << iteration);
        
        // Property: Remaining count should be correct
        BOOST_CHECK_MESSAGE(registry.GetBurnCount() == countBefore - removed,
            "Remaining count should be correct in iteration " << iteration);
        
        // Property: Burns before reorg block should still exist
        for (const auto& pair : burnsByBlock) {
            for (const uint256& hash : pair.second) {
                if (pair.first < reorgBlock) {
                    BOOST_CHECK_MESSAGE(registry.IsProcessed(hash),
                        "Burns before reorg should still exist in iteration " << iteration);
                } else {
                    BOOST_CHECK_MESSAGE(!registry.IsProcessed(hash),
                        "Burns at/after reorg should be removed in iteration " << iteration);
                }
            }
        }
    }
}

/**
 * **Property: Total burned consistency**
 * 
 * *For any* sequence of operations, GetTotalBurned should equal the sum
 * of amounts of all currently recorded burns.
 * 
 * **Validates: Requirements 5.1**
 */
BOOST_AUTO_TEST_CASE(property_total_burned_consistency)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRegistry registry;
        
        // Add random burns
        size_t numBurns = (TestRand32() % 20) + 5;
        for (size_t i = 0; i < numBurns; ++i) {
            registry.RecordBurn(CreateRandomBurnRecord());
        }
        
        // Calculate expected total from all burns
        CAmount expectedTotal = 0;
        auto allBurns = registry.GetAllBurns();
        for (const auto& burn : allBurns) {
            expectedTotal += burn.amount;
        }
        
        // Property: GetTotalBurned should match sum
        BOOST_CHECK_MESSAGE(registry.GetTotalBurned() == expectedTotal,
            "Total burned should match sum of all burns in iteration " << iteration);
        
        // Do a reorg
        if (!allBurns.empty()) {
            uint64_t maxBlock = 0;
            for (const auto& burn : allBurns) {
                maxBlock = std::max(maxBlock, burn.l2MintBlock);
            }
            
            uint64_t reorgBlock = (TestRand64() % maxBlock) + 1;
            registry.HandleReorg(reorgBlock);
            
            // Recalculate expected total
            expectedTotal = 0;
            allBurns = registry.GetAllBurns();
            for (const auto& burn : allBurns) {
                expectedTotal += burn.amount;
            }
            
            // Property: Total should still be consistent after reorg
            BOOST_CHECK_MESSAGE(registry.GetTotalBurned() == expectedTotal,
                "Total burned should match after reorg in iteration " << iteration);
        }
    }
}

/**
 * **Property: BurnRecord serialization roundtrip**
 * 
 * *For any* valid BurnRecord, serializing and deserializing should produce
 * an equivalent object.
 * 
 * **Validates: Requirements 5.2**
 */
BOOST_AUTO_TEST_CASE(property_burn_record_serialization)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnRecord original = CreateRandomBurnRecord();
        BOOST_REQUIRE(original.IsValid());
        
        // Serialize
        std::vector<unsigned char> serialized = original.Serialize();
        BOOST_REQUIRE(!serialized.empty());
        
        // Deserialize
        l2::BurnRecord restored;
        BOOST_REQUIRE_MESSAGE(restored.Deserialize(serialized),
            "Deserialization should succeed in iteration " << iteration);
        
        // Property: Roundtrip should produce equal object
        BOOST_CHECK_MESSAGE(original == restored,
            "Roundtrip should produce equal object in iteration " << iteration);
        BOOST_CHECK_MESSAGE(restored.IsValid(),
            "Restored object should be valid in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
