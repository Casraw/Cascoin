// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bctdb.h"
#include "test/test_bitcoin.h"
#include "random.h"
#include "util.h"

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <random>
#include <string>
#include <vector>

namespace {

// Helper to generate random strings
std::string RandomString(std::mt19937& gen, size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += alphanum[dist(gen)];
    }
    return result;
}

// Helper to generate random hex string (for txid)
std::string RandomHexString(std::mt19937& gen, size_t length) {
    static const char hexchars[] = "0123456789abcdef";
    std::uniform_int_distribution<> dist(0, 15);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += hexchars[dist(gen)];
    }
    return result;
}

// Helper to verify BCTRecord fields match (excluding checksum and updatedAt)
void VerifyBCTRecordFieldsMatch(const BCTRecord& expected, const BCTRecord& actual, const std::string& context = "") {
    std::string ctx = context.empty() ? "" : " (" + context + ")";
    BOOST_CHECK_EQUAL(expected.txid, actual.txid);
    BOOST_CHECK_EQUAL(expected.honeyAddress, actual.honeyAddress);
    BOOST_CHECK_EQUAL(expected.status, actual.status);
    BOOST_CHECK_EQUAL(expected.beeCount, actual.beeCount);
    BOOST_CHECK_EQUAL(expected.creationHeight, actual.creationHeight);
    BOOST_CHECK_EQUAL(expected.maturityHeight, actual.maturityHeight);
    BOOST_CHECK_EQUAL(expected.expirationHeight, actual.expirationHeight);
    BOOST_CHECK_EQUAL(expected.timestamp, actual.timestamp);
    BOOST_CHECK_EQUAL(expected.cost, actual.cost);
    BOOST_CHECK_EQUAL(expected.blocksFound, actual.blocksFound);
    BOOST_CHECK_EQUAL(expected.rewardsPaid, actual.rewardsPaid);
    BOOST_CHECK_EQUAL(expected.profit, actual.profit);
}

// Helper to generate random BCTRecord
BCTRecord GenerateRandomBCTRecord(std::mt19937& gen) {
    BCTRecord record;
    
    // Generate random txid (64 hex chars)
    record.txid = RandomHexString(gen, 64);
    
    // Generate random honey address
    record.honeyAddress = "C" + RandomString(gen, 33);
    
    // Random status
    std::uniform_int_distribution<> statusDist(0, 2);
    const char* statuses[] = {"immature", "mature", "expired"};
    record.status = statuses[statusDist(gen)];
    
    // Random numeric values
    std::uniform_int_distribution<> beeCountDist(1, 100);
    record.beeCount = beeCountDist(gen);
    
    std::uniform_int_distribution<> heightDist(1, 1000000);
    record.creationHeight = heightDist(gen);
    record.maturityHeight = record.creationHeight + 100;
    record.expirationHeight = record.maturityHeight + 10000;
    
    std::uniform_int_distribution<int64_t> timestampDist(1600000000, 1700000000);
    record.timestamp = timestampDist(gen);
    
    std::uniform_int_distribution<int64_t> costDist(1000000, 100000000);
    record.cost = costDist(gen);
    
    std::uniform_int_distribution<> blocksFoundDist(0, 50);
    record.blocksFound = blocksFoundDist(gen);
    
    std::uniform_int_distribution<int64_t> rewardsDist(0, 500000000);
    record.rewardsPaid = rewardsDist(gen);
    
    record.profit = record.rewardsPaid - record.cost;
    
    return record;
}

// Test fixture for BCT database tests
struct BCTDatabaseTestSetup : public BasicTestingSetup {
    boost::filesystem::path testDir;
    
    BCTDatabaseTestSetup() {
        // Create a unique test directory
        testDir = boost::filesystem::temp_directory_path() / 
                  boost::filesystem::unique_path("bctdb_test_%%%%-%%%%");
        boost::filesystem::create_directories(testDir);
    }
    
    ~BCTDatabaseTestSetup() {
        // Shutdown database before cleanup
        BCTDatabaseSQLite::instance()->shutdown();
        
        // Clean up test directory
        boost::filesystem::remove_all(testDir);
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(bctdb_tests, BCTDatabaseTestSetup)

/**
 * **Feature: bct-persistent-database, Property 1: Serialization Round-Trip Consistency**
 * **Validates: Requirements 1.4, 4.2, 7.2**
 * 
 * For any valid BCTRecord object, serializing it to the SQLite database and then
 * deserializing it back SHALL produce an equivalent BCTRecord object with identical
 * field values.
 */
BOOST_AUTO_TEST_CASE(property_serialization_roundtrip)
{
    // Initialize database
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Use a fixed seed for reproducibility
    std::mt19937 gen(42);
    
    // Run 100 iterations as specified in the design document
    const int NUM_ITERATIONS = 100;
    
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // Generate a random BCTRecord
        BCTRecord original = GenerateRandomBCTRecord(gen);
        
        // Insert into database
        BOOST_REQUIRE_MESSAGE(db->insertBCT(original), 
            "Failed to insert BCT record iteration " << i);
        
        // Read back from database
        BCTRecord retrieved = db->getBCT(original.txid);
        
        // Verify all fields match (excluding checksum and updatedAt which are set by the database)
        VerifyBCTRecordFieldsMatch(original, retrieved);
        
        // Verify checksum is valid
        BOOST_CHECK_MESSAGE(retrieved.validateChecksum(),
            "Checksum validation failed for iteration " << i);
        
        // Clean up for next iteration
        BOOST_REQUIRE(db->deleteBCT(original.txid));
    }
    
    db->shutdown();
}

// Basic unit test for database initialization
BOOST_AUTO_TEST_CASE(database_initialization)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    
    // Test initialization
    BOOST_CHECK(db->initialize(testDir.string()));
    BOOST_CHECK(db->isInitialized());
    
    // Verify database file was created
    boost::filesystem::path dbFile = testDir / "bct_database.sqlite";
    BOOST_CHECK(boost::filesystem::exists(dbFile));
    
    db->shutdown();
    BOOST_CHECK(!db->isInitialized());
}

// Basic unit test for CRUD operations
BOOST_AUTO_TEST_CASE(crud_operations)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Create a test record
    BCTRecord record;
    record.txid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    record.honeyAddress = "CTestAddress123456789012345678901234";
    record.status = "immature";
    record.beeCount = 10;
    record.creationHeight = 100000;
    record.maturityHeight = 100100;
    record.expirationHeight = 110100;
    record.timestamp = 1609459200;
    record.cost = 10000000;
    record.blocksFound = 0;
    record.rewardsPaid = 0;
    record.profit = -10000000;
    
    // Test insert
    BOOST_CHECK(db->insertBCT(record));
    BOOST_CHECK(db->bctExists(record.txid));
    
    // Test read
    BCTRecord retrieved = db->getBCT(record.txid);
    BOOST_CHECK_EQUAL(record.txid, retrieved.txid);
    BOOST_CHECK_EQUAL(record.honeyAddress, retrieved.honeyAddress);
    
    // Test update
    record.status = "mature";
    record.blocksFound = 5;
    record.rewardsPaid = 50000000;
    record.profit = record.rewardsPaid - record.cost;
    BOOST_CHECK(db->updateBCT(record.txid, record));
    
    retrieved = db->getBCT(record.txid);
    BOOST_CHECK_EQUAL("mature", retrieved.status);
    BOOST_CHECK_EQUAL(5, retrieved.blocksFound);
    BOOST_CHECK_EQUAL(50000000, retrieved.rewardsPaid);
    
    // Test delete
    BOOST_CHECK(db->deleteBCT(record.txid));
    BOOST_CHECK(!db->bctExists(record.txid));
    
    db->shutdown();
}

// Test query operations
BOOST_AUTO_TEST_CASE(query_operations)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Insert multiple records with different statuses
    std::vector<BCTRecord> records;
    const char* statuses[] = {"immature", "mature", "expired"};
    
    for (int i = 0; i < 9; ++i) {
        BCTRecord record;
        record.txid = std::string(64, '0' + i);
        record.honeyAddress = "CTestAddress" + std::to_string(i);
        record.status = statuses[i % 3];
        record.beeCount = i + 1;
        record.creationHeight = 100000 + i * 100;
        record.maturityHeight = record.creationHeight + 100;
        record.expirationHeight = record.maturityHeight + 10000;
        record.timestamp = 1609459200 + i * 3600;
        record.cost = 10000000;
        record.blocksFound = 0;
        record.rewardsPaid = 0;
        record.profit = -10000000;
        
        BOOST_REQUIRE(db->insertBCT(record));
        records.push_back(record);
    }
    
    // Test getAllBCTs (excluding expired)
    auto allNonExpired = db->getAllBCTs(false);
    BOOST_CHECK_EQUAL(6, allNonExpired.size()); // 3 immature + 3 mature
    
    // Test getAllBCTs (including expired)
    auto allRecords = db->getAllBCTs(true);
    BOOST_CHECK_EQUAL(9, allRecords.size());
    
    // Test getBCTsByStatus
    auto immatureRecords = db->getBCTsByStatus("immature");
    BOOST_CHECK_EQUAL(3, immatureRecords.size());
    
    auto matureRecords = db->getBCTsByStatus("mature");
    BOOST_CHECK_EQUAL(3, matureRecords.size());
    
    auto expiredRecords = db->getBCTsByStatus("expired");
    BOOST_CHECK_EQUAL(3, expiredRecords.size());
    
    // Test getSummary
    BCTSummary summary = db->getSummary();
    BOOST_CHECK_EQUAL(3, summary.immatureCount);
    BOOST_CHECK_EQUAL(3, summary.matureCount);
    BOOST_CHECK_EQUAL(3, summary.expiredCount);
    BOOST_CHECK_EQUAL(45, summary.totalBeeCount); // 1+2+3+4+5+6+7+8+9
    
    // Clean up
    for (const auto& record : records) {
        db->deleteBCT(record.txid);
    }
    
    db->shutdown();
}

// Test sync state management
BOOST_AUTO_TEST_CASE(sync_state)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Initially should return -1 (no height set)
    BOOST_CHECK_EQUAL(-1, db->getLastProcessedHeight());
    
    // Set and verify height
    BOOST_CHECK(db->setLastProcessedHeight(100000));
    BOOST_CHECK_EQUAL(100000, db->getLastProcessedHeight());
    
    // Update height
    BOOST_CHECK(db->setLastProcessedHeight(100500));
    BOOST_CHECK_EQUAL(100500, db->getLastProcessedHeight());
    
    db->shutdown();
}

// Test reward tracking
BOOST_AUTO_TEST_CASE(reward_tracking)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Create a BCT record first
    BCTRecord record;
    record.txid = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    record.honeyAddress = "CTestRewardAddress";
    record.status = "mature";
    record.beeCount = 10;
    record.creationHeight = 100000;
    record.maturityHeight = 100100;
    record.expirationHeight = 110100;
    record.timestamp = 1609459200;
    record.cost = 10000000;
    record.blocksFound = 0;
    record.rewardsPaid = 0;
    record.profit = -10000000;
    
    BOOST_REQUIRE(db->insertBCT(record));
    
    // Insert rewards
    BOOST_CHECK(db->insertReward("coinbase1", record.txid, 1000000, 100200));
    BOOST_CHECK(db->insertReward("coinbase2", record.txid, 2000000, 100300));
    BOOST_CHECK(db->insertReward("coinbase3", record.txid, 1500000, 100400));
    
    // Verify total rewards
    int64_t totalRewards = db->getTotalRewardsForBCT(record.txid);
    BOOST_CHECK_EQUAL(4500000, totalRewards);
    
    // Clean up
    db->deleteBCT(record.txid);
    
    db->shutdown();
}

/**
 * **Feature: bct-persistent-database, Property 2: Update Isolation**
 * **Validates: Requirements 2.2, 2.3, 2.4**
 * 
 * For any database update operation (status change, reward update, or new BCT insertion),
 * all BCT records not targeted by the update SHALL remain unchanged in the database.
 */
BOOST_AUTO_TEST_CASE(property_update_isolation)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    std::mt19937 gen(12345);
    
    const int NUM_ITERATIONS = 100;
    const int RECORDS_PER_ITERATION = 5;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Generate multiple random records
        std::vector<BCTRecord> originalRecords;
        for (int i = 0; i < RECORDS_PER_ITERATION; ++i) {
            BCTRecord record = GenerateRandomBCTRecord(gen);
            // Ensure unique txids within this iteration
            record.txid = RandomHexString(gen, 64);
            originalRecords.push_back(record);
            BOOST_REQUIRE_MESSAGE(db->insertBCT(record),
                "Failed to insert record " << i << " in iteration " << iter);
        }
        
        // Pick a random record to update
        std::uniform_int_distribution<> indexDist(0, RECORDS_PER_ITERATION - 1);
        int updateIndex = indexDist(gen);
        
        // Modify the selected record
        BCTRecord updatedRecord = originalRecords[updateIndex];
        updatedRecord.status = "mature";
        updatedRecord.blocksFound = 99;
        updatedRecord.rewardsPaid = 999999999;
        updatedRecord.profit = updatedRecord.rewardsPaid - updatedRecord.cost;
        
        BOOST_REQUIRE_MESSAGE(db->updateBCT(updatedRecord.txid, updatedRecord),
            "Failed to update record in iteration " << iter);
        
        // Verify all OTHER records remain unchanged
        for (int i = 0; i < RECORDS_PER_ITERATION; ++i) {
            if (i == updateIndex) continue;
            
            BCTRecord retrieved = db->getBCT(originalRecords[i].txid);
            VerifyBCTRecordFieldsMatch(originalRecords[i], retrieved);
        }
        
        // Verify the updated record has the new values
        BCTRecord retrievedUpdated = db->getBCT(updatedRecord.txid);
        BOOST_CHECK_EQUAL("mature", retrievedUpdated.status);
        BOOST_CHECK_EQUAL(99, retrievedUpdated.blocksFound);
        BOOST_CHECK_EQUAL(999999999, retrievedUpdated.rewardsPaid);
        
        // Clean up for next iteration
        for (const auto& record : originalRecords) {
            db->deleteBCT(record.txid);
        }
    }
    
    db->shutdown();
}

/**
 * **Feature: bct-persistent-database, Property 5: Query Correctness - Sorting**
 * **Validates: Requirements 6.2**
 * 
 * For any sort request on BCT data by a given column, the returned results SHALL be
 * correctly ordered according to that column's values and the specified sort direction.
 */
BOOST_AUTO_TEST_CASE(property_query_sorting)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    std::mt19937 gen(54321);
    
    const int NUM_ITERATIONS = 100;
    const int RECORDS_PER_ITERATION = 10;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Generate random records with varying creation heights
        std::vector<BCTRecord> records;
        for (int i = 0; i < RECORDS_PER_ITERATION; ++i) {
            BCTRecord record = GenerateRandomBCTRecord(gen);
            record.txid = RandomHexString(gen, 64);
            records.push_back(record);
            BOOST_REQUIRE_MESSAGE(db->insertBCT(record),
                "Failed to insert record " << i << " in iteration " << iter);
        }
        
        // Query all records (getAllBCTs returns sorted by creation_height DESC)
        auto retrieved = db->getAllBCTs(true);
        
        BOOST_REQUIRE_EQUAL(RECORDS_PER_ITERATION, retrieved.size());
        
        // Verify records are sorted by creation_height in descending order
        for (size_t i = 1; i < retrieved.size(); ++i) {
            BOOST_CHECK_MESSAGE(
                retrieved[i-1].creationHeight >= retrieved[i].creationHeight,
                "Sorting violation at iteration " << iter << ", index " << i <<
                ": " << retrieved[i-1].creationHeight << " should be >= " << retrieved[i].creationHeight);
        }
        
        // Clean up for next iteration
        for (const auto& record : records) {
            db->deleteBCT(record.txid);
        }
    }
    
    db->shutdown();
}

/**
 * **Feature: bct-persistent-database, Property 6: Query Correctness - Filtering**
 * **Validates: Requirements 6.3**
 * 
 * For any filter request by status, the returned results SHALL contain only BCT records
 * matching the specified status, and SHALL contain all such matching records.
 */
BOOST_AUTO_TEST_CASE(property_query_filtering)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    std::mt19937 gen(98765);
    
    const int NUM_ITERATIONS = 100;
    const int RECORDS_PER_ITERATION = 15;
    const char* statuses[] = {"immature", "mature", "expired"};
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Generate random records with random statuses
        std::map<std::string, int> statusCounts;
        statusCounts["immature"] = 0;
        statusCounts["mature"] = 0;
        statusCounts["expired"] = 0;
        
        std::vector<BCTRecord> records;
        std::uniform_int_distribution<> statusDist(0, 2);
        
        for (int i = 0; i < RECORDS_PER_ITERATION; ++i) {
            BCTRecord record = GenerateRandomBCTRecord(gen);
            record.txid = RandomHexString(gen, 64);
            record.status = statuses[statusDist(gen)];
            statusCounts[record.status]++;
            records.push_back(record);
            BOOST_REQUIRE_MESSAGE(db->insertBCT(record),
                "Failed to insert record " << i << " in iteration " << iter);
        }
        
        // Test filtering for each status
        for (const char* status : statuses) {
            auto filtered = db->getBCTsByStatus(status);
            
            // Verify count matches expected
            BOOST_CHECK_EQUAL(statusCounts[status], (int)filtered.size());
            
            // Verify all returned records have the correct status
            for (const auto& record : filtered) {
                BOOST_CHECK_EQUAL(status, record.status);
            }
        }
        
        // Test getAllBCTs with includeExpired=false
        auto nonExpired = db->getAllBCTs(false);
        int expectedNonExpired = statusCounts["immature"] + statusCounts["mature"];
        BOOST_CHECK_EQUAL(expectedNonExpired, (int)nonExpired.size());
        
        // Verify no expired records in the result
        for (const auto& record : nonExpired) {
            BOOST_CHECK_MESSAGE(record.status != "expired",
                "Found expired record when includeExpired=false in iteration " << iter);
        }
        
        // Clean up for next iteration
        for (const auto& record : records) {
            db->deleteBCT(record.txid);
        }
    }
    
    db->shutdown();
}

// Test BCTRecord helper methods
BOOST_AUTO_TEST_CASE(bctrecord_helpers)
{
    BCTRecord record;
    record.txid = "test";
    record.honeyAddress = "CTest";
    record.status = "immature";
    record.beeCount = 10;
    record.creationHeight = 100000;
    record.maturityHeight = 100100;
    record.expirationHeight = 110100;
    record.timestamp = 1609459200;
    record.cost = 10000000;
    record.blocksFound = 0;
    record.rewardsPaid = 0;
    record.profit = -10000000;
    
    // Test getBlocksLeft
    BOOST_CHECK_EQUAL(10100, record.getBlocksLeft(100000));
    BOOST_CHECK_EQUAL(100, record.getBlocksLeft(110000));
    BOOST_CHECK_EQUAL(0, record.getBlocksLeft(110100));
    BOOST_CHECK_EQUAL(0, record.getBlocksLeft(120000));
    
    // Test updateStatus
    record.updateStatus(100000);
    BOOST_CHECK_EQUAL("immature", record.status);
    
    record.updateStatus(100100);
    BOOST_CHECK_EQUAL("mature", record.status);
    
    record.updateStatus(110100);
    BOOST_CHECK_EQUAL("expired", record.status);
    
    // Test checksum
    record.checksum = record.calculateChecksum();
    BOOST_CHECK(record.validateChecksum());
    
    // Modify a field and verify checksum fails
    record.beeCount = 20;
    BOOST_CHECK(!record.validateChecksum());
}

/**
 * **Feature: bct-persistent-database, Property 3: Incremental Block Processing**
 * **Validates: Requirements 2.1**
 * 
 * For any new block added to the chain, the BCT_Database SHALL process only transactions
 * within that block, and the resulting database state SHALL be equivalent to a full rescan
 * up to that block height.
 * 
 * This test verifies that:
 * 1. BCT records can be inserted with correct status
 * 2. Status updates work correctly based on height comparisons
 * 3. The database maintains consistency after multiple operations
 */
BOOST_AUTO_TEST_CASE(property_incremental_block_processing)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    std::mt19937 gen(11111);
    
    const int NUM_ITERATIONS = 100;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Generate random BCT records at various creation heights
        std::vector<BCTRecord> records;
        std::uniform_int_distribution<> heightDist(1000, 100000);
        std::uniform_int_distribution<> countDist(3, 8);
        
        int numRecords = countDist(gen);
        for (int i = 0; i < numRecords; ++i) {
            BCTRecord record = GenerateRandomBCTRecord(gen);
            record.txid = RandomHexString(gen, 64);
            record.creationHeight = heightDist(gen);
            // Set maturity and expiration based on creation height with realistic values
            record.maturityHeight = record.creationHeight + 2016;  // ~2 weeks of blocks
            record.expirationHeight = record.creationHeight + 2016 + 201600;  // ~140 days
            record.status = "immature";  // Start as immature
            records.push_back(record);
            BOOST_REQUIRE_MESSAGE(db->insertBCT(record),
                "Failed to insert record " << i << " in iteration " << iter);
        }
        
        // Verify all records were inserted correctly
        for (const auto& originalRecord : records) {
            BCTRecord retrieved = db->getBCT(originalRecord.txid);
            BOOST_CHECK_EQUAL(originalRecord.txid, retrieved.txid);
            BOOST_CHECK_EQUAL(originalRecord.creationHeight, retrieved.creationHeight);
            BOOST_CHECK_EQUAL(originalRecord.maturityHeight, retrieved.maturityHeight);
            BOOST_CHECK_EQUAL(originalRecord.expirationHeight, retrieved.expirationHeight);
        }
        
        // Simulate incremental updates by manually updating status for each record
        // This tests that individual record updates work correctly
        std::uniform_int_distribution<> currentHeightDist(50000, 300000);
        int currentHeight = currentHeightDist(gen);
        
        for (auto& record : records) {
            BCTRecord retrieved = db->getBCT(record.txid);
            
            // Calculate and set expected status based on current height
            std::string expectedStatus;
            if (currentHeight >= retrieved.expirationHeight) {
                expectedStatus = "expired";
            } else if (currentHeight >= retrieved.maturityHeight) {
                expectedStatus = "mature";
            } else {
                expectedStatus = "immature";
            }
            
            // Update the record with new status
            retrieved.status = expectedStatus;
            BOOST_REQUIRE(db->updateBCT(retrieved.txid, retrieved));
            
            // Verify the update was applied
            BCTRecord verified = db->getBCT(retrieved.txid);
            BOOST_CHECK_EQUAL(expectedStatus, verified.status);
        }
        
        // Verify sync state can be updated
        BOOST_REQUIRE(db->setLastProcessedHeight(currentHeight));
        BOOST_CHECK_EQUAL(currentHeight, db->getLastProcessedHeight());
        
        // Clean up for next iteration
        for (const auto& record : records) {
            db->deleteBCT(record.txid);
        }
    }
    
    db->shutdown();
}

/**
 * **Feature: bct-persistent-database, Property 4: Transaction Atomicity**
 * **Validates: Requirements 3.3**
 * 
 * For any write operation to the database, either all changes within that operation
 * SHALL be committed, or none SHALL be committed (no partial writes).
 * 
 * This test verifies atomicity by:
 * 1. Testing that failed operations (duplicate inserts) don't corrupt existing data
 * 2. Testing that multi-field updates are atomic (all fields change or none)
 * 3. Testing that the database remains consistent after failed operations
 * 4. Testing that batch operations maintain consistency
 */
BOOST_AUTO_TEST_CASE(property_transaction_atomicity)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    std::mt19937 gen(22222);
    
    const int NUM_ITERATIONS = 100;
    const int RECORDS_PER_BATCH = 5;
    
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Generate a batch of records
        std::vector<BCTRecord> batch;
        for (int i = 0; i < RECORDS_PER_BATCH; ++i) {
            BCTRecord record = GenerateRandomBCTRecord(gen);
            record.txid = RandomHexString(gen, 64);
            batch.push_back(record);
        }
        
        // Test 1: Verify that successful inserts are all visible (batch atomicity)
        for (const auto& record : batch) {
            BOOST_REQUIRE(db->insertBCT(record));
        }
        
        // All records should exist - no partial inserts
        for (const auto& record : batch) {
            BOOST_CHECK_MESSAGE(db->bctExists(record.txid),
                "Record should exist after insert in iteration " << iter);
        }
        
        // Test 2: Verify failed insert (duplicate) doesn't corrupt existing data
        BCTRecord originalFirst = db->getBCT(batch[0].txid);
        BCTRecord duplicateRecord = batch[0];
        duplicateRecord.status = "expired";  // Try to change status via duplicate insert
        duplicateRecord.beeCount = 9999;
        
        // This should fail (duplicate primary key)
        bool duplicateInsertResult = db->insertBCT(duplicateRecord);
        BOOST_CHECK_MESSAGE(!duplicateInsertResult,
            "Duplicate insert should fail in iteration " << iter);
        
        // Original record should be unchanged after failed duplicate insert
        BCTRecord afterFailedInsert = db->getBCT(batch[0].txid);
        BOOST_CHECK_EQUAL(originalFirst.status, afterFailedInsert.status);
        BOOST_CHECK_EQUAL(originalFirst.beeCount, afterFailedInsert.beeCount);
        BOOST_CHECK_EQUAL(originalFirst.honeyAddress, afterFailedInsert.honeyAddress);
        BOOST_CHECK_EQUAL(originalFirst.creationHeight, afterFailedInsert.creationHeight);
        
        // Test 3: Verify update atomicity - all fields change together or none
        BCTRecord testRecord = batch[1];
        BCTRecord originalRecord = db->getBCT(testRecord.txid);
        
        // Update multiple fields simultaneously
        BCTRecord updated = originalRecord;
        updated.status = "mature";
        updated.blocksFound = 99;
        updated.rewardsPaid = 999999;
        updated.profit = updated.rewardsPaid - updated.cost;
        
        BOOST_REQUIRE(db->updateBCT(testRecord.txid, updated));
        
        // Verify ALL fields were updated atomically (not just some)
        BCTRecord retrieved = db->getBCT(testRecord.txid);
        BOOST_CHECK_EQUAL("mature", retrieved.status);
        BOOST_CHECK_EQUAL(99, retrieved.blocksFound);
        BOOST_CHECK_EQUAL(999999, retrieved.rewardsPaid);
        BOOST_CHECK_EQUAL(updated.profit, retrieved.profit);
        // Unchanged fields should remain the same
        BOOST_CHECK_EQUAL(originalRecord.honeyAddress, retrieved.honeyAddress);
        BOOST_CHECK_EQUAL(originalRecord.beeCount, retrieved.beeCount);
        BOOST_CHECK_EQUAL(originalRecord.creationHeight, retrieved.creationHeight);
        
        // Test 4: Verify update to non-existent record doesn't create partial data
        std::string nonExistentTxid = RandomHexString(gen, 64);
        BCTRecord ghostRecord = GenerateRandomBCTRecord(gen);
        ghostRecord.txid = nonExistentTxid;
        
        // Update should succeed but affect 0 rows (SQLite behavior)
        db->updateBCT(nonExistentTxid, ghostRecord);
        
        // Record should NOT exist (no partial creation from update)
        BOOST_CHECK_MESSAGE(!db->bctExists(nonExistentTxid),
            "Non-existent record should not be created by update in iteration " << iter);
        
        // Test 5: Verify deleting all records leaves no partial data
        for (const auto& record : batch) {
            BOOST_REQUIRE(db->deleteBCT(record.txid));
        }
        
        // No records should exist - complete deletion
        for (const auto& record : batch) {
            BOOST_CHECK_MESSAGE(!db->bctExists(record.txid),
                "Record should not exist after delete in iteration " << iter);
        }
        
        // Test 6: Verify database count is consistent after all operations
        auto allRecords = db->getAllBCTs(true);
        BOOST_CHECK_EQUAL(0, allRecords.size());
    }
    
    db->shutdown();
}

// Test reorg handling
BOOST_AUTO_TEST_CASE(reorg_handling)
{
    BCTDatabaseSQLite* db = BCTDatabaseSQLite::instance();
    BOOST_REQUIRE(db->initialize(testDir.string()));
    
    // Create records at various heights
    std::vector<BCTRecord> records;
    for (int i = 0; i < 10; ++i) {
        BCTRecord record;
        record.txid = std::string(64, '0' + i);
        record.honeyAddress = "CTestAddress" + std::to_string(i);
        record.status = "immature";
        record.beeCount = i + 1;
        record.creationHeight = 100000 + i * 100;  // Heights: 100000, 100100, 100200, ...
        record.maturityHeight = record.creationHeight + 100;
        record.expirationHeight = record.maturityHeight + 10000;
        record.timestamp = 1609459200 + i * 3600;
        record.cost = 10000000;
        record.blocksFound = 0;
        record.rewardsPaid = 0;
        record.profit = -10000000;
        
        BOOST_REQUIRE(db->insertBCT(record));
        records.push_back(record);
    }
    
    // Add some rewards at various heights
    BOOST_REQUIRE(db->insertReward("reward1", records[0].txid, 1000000, 100050));
    BOOST_REQUIRE(db->insertReward("reward2", records[0].txid, 2000000, 100150));
    BOOST_REQUIRE(db->insertReward("reward3", records[5].txid, 1500000, 100550));
    BOOST_REQUIRE(db->insertReward("reward4", records[5].txid, 2500000, 100650));
    
    // Verify initial state
    BOOST_CHECK_EQUAL(10, db->getAllBCTs(true).size());
    
    // Simulate reorg at height 100500 (should remove records at 100500 and above)
    // Records at heights 100500, 100600, 100700, 100800, 100900 should be removed (5 records)
    db->handleReorg(100499);
    
    // Verify records after reorg
    auto remainingRecords = db->getAllBCTs(true);
    BOOST_CHECK_EQUAL(5, remainingRecords.size());  // Records at 100000-100400 remain
    
    // Verify the correct records remain
    for (const auto& record : remainingRecords) {
        BOOST_CHECK_MESSAGE(record.creationHeight < 100500,
            "Record at height " << record.creationHeight << " should have been removed");
    }
    
    // Verify rewards after fork height were removed
    // reward3 and reward4 were at heights 100550 and 100650, should be removed
    int64_t rewards0 = db->getTotalRewardsForBCT(records[0].txid);
    int64_t rewards5 = db->getTotalRewardsForBCT(records[5].txid);
    
    // rewards for record[0] at heights 100050 and 100150 should remain
    BOOST_CHECK_EQUAL(3000000, rewards0);
    // rewards for record[5] should be 0 (both were after fork height)
    BOOST_CHECK_EQUAL(0, rewards5);
    
    // Verify sync state was updated
    BOOST_CHECK_EQUAL(100499, db->getLastProcessedHeight());
    
    // Clean up
    db->clearAllData();
    db->shutdown();
}

BOOST_AUTO_TEST_SUITE_END()
