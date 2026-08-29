// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_data_availability_tests.cpp
 * @brief Property-based tests for L2 Data Availability Layer
 * 
 * **Feature: cascoin-l2-solution, Property 13: Data Availability Reconstruction**
 * **Validates: Requirements 7.3, 11.6, 41.2**
 * 
 * Property 13: Data Availability Reconstruction
 * *For any* batch published to L1, it SHALL be possible to reconstruct 
 * the complete L2 state from L1 data alone.
 */

#include <l2/data_availability.h>
#include <l2/l2_common.h>
#include <primitives/transaction.h>
#include <random.h>
#include <uint256.h>
#include <hash.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <set>

namespace {

// Local random context for tests
static FastRandomContext g_test_rand_ctx(true);

static uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

static uint256 TestRand256() { 
    return g_test_rand_ctx.rand256(); 
}

/**
 * Helper function to generate random bytes
 */
static std::vector<unsigned char> RandomBytes(size_t len)
{
    std::vector<unsigned char> result(len);
    for (size_t i = 0; i < len; ++i) {
        result[i] = static_cast<unsigned char>(TestRand32() & 0xFF);
    }
    return result;
}

/**
 * Helper function to generate a random uint160 address
 */
static uint160 RandomAddress()
{
    std::vector<unsigned char> bytes(20);
    for (size_t i = 0; i < 20; ++i) {
        bytes[i] = static_cast<unsigned char>(TestRand32() & 0xFF);
    }
    return uint160(bytes);
}

/**
 * Helper function to create a simple test transaction
 */
static CTransaction CreateTestTransaction()
{
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.nLockTime = 0;
    
    // Add a simple input
    COutPoint prevout(TestRand256(), TestRand32() % 10);
    mtx.vin.push_back(CTxIn(prevout));
    
    // Add a simple output
    CScript scriptPubKey;
    scriptPubKey << OP_DUP << OP_HASH160 << ToByteVector(RandomAddress()) 
                 << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(1000000, scriptPubKey));
    
    return CTransaction(mtx);
}

/**
 * Helper function to create multiple test transactions
 */
static std::vector<CTransaction> CreateTestTransactions(size_t count)
{
    std::vector<CTransaction> txs;
    txs.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        txs.push_back(CreateTestTransaction());
    }
    return txs;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_data_availability_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(batch_data_serialization)
{
    l2::BatchData batch;
    batch.version = l2::L2_PROTOCOL_VERSION;
    batch.startBlock = 100;
    batch.endBlock = 199;
    batch.preStateRoot = TestRand256();
    batch.postStateRoot = TestRand256();
    batch.transactionsRoot = TestRand256();
    batch.transactionCount = 50;
    batch.totalGasUsed = 1000000;
    batch.l2ChainId = 1;
    batch.l1AnchorBlock = 500;
    batch.l1AnchorHash = TestRand256();
    batch.sequencerAddress = RandomAddress();
    batch.timestamp = 1700000000;
    batch.compressedTransactions = RandomBytes(1000);
    
    // Serialize
    std::vector<unsigned char> serialized = batch.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Deserialize
    l2::BatchData restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    // Verify equality
    BOOST_CHECK(batch == restored);
}

BOOST_AUTO_TEST_CASE(batch_data_validation)
{
    l2::BatchData batch;
    batch.version = l2::L2_PROTOCOL_VERSION;
    batch.startBlock = 100;
    batch.endBlock = 199;
    batch.sequencerAddress = RandomAddress();
    
    BOOST_CHECK(batch.ValidateStructure());
    
    // Invalid: end < start
    batch.endBlock = 50;
    BOOST_CHECK(!batch.ValidateStructure());
    
    // Invalid: null sequencer
    batch.endBlock = 199;
    batch.sequencerAddress = uint160();
    BOOST_CHECK(!batch.ValidateStructure());
}

BOOST_AUTO_TEST_CASE(da_commitment_serialization)
{
    l2::DACommitment commitment;
    commitment.dataHash = TestRand256();
    commitment.dataSize = 10000;
    commitment.erasureCodingRoot = TestRand256();
    commitment.dataShards = 4;
    commitment.parityShards = 2;
    commitment.shardSize = 2500;
    commitment.batchHash = TestRand256();
    commitment.timestamp = 1700000000;
    
    // Add roots
    for (uint32_t i = 0; i < commitment.GetTotalShards(); ++i) {
        commitment.columnRoots.push_back(TestRand256());
        commitment.rowRoots.push_back(TestRand256());
    }
    
    // Serialize
    std::vector<unsigned char> serialized = commitment.Serialize();
    BOOST_CHECK(!serialized.empty());
    
    // Deserialize
    l2::DACommitment restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    // Verify equality
    BOOST_CHECK(commitment == restored);
}

BOOST_AUTO_TEST_CASE(da_commitment_validation)
{
    l2::DACommitment commitment;
    commitment.dataShards = 4;
    commitment.parityShards = 2;
    commitment.dataSize = 1000;
    commitment.shardSize = 250;
    
    // Add correct number of roots
    for (uint32_t i = 0; i < commitment.GetTotalShards(); ++i) {
        commitment.columnRoots.push_back(TestRand256());
        commitment.rowRoots.push_back(TestRand256());
    }
    
    BOOST_CHECK(commitment.ValidateStructure());
    
    // Invalid: wrong number of roots
    commitment.columnRoots.pop_back();
    BOOST_CHECK(!commitment.ValidateStructure());
}

BOOST_AUTO_TEST_CASE(compression_decompression_empty)
{
    l2::DataAvailabilityLayer da(1);
    
    std::vector<CTransaction> empty;
    auto compressed = da.CompressTransactions(empty);
    auto decompressed = da.DecompressTransactions(compressed);
    
    BOOST_CHECK(decompressed.empty());
}

BOOST_AUTO_TEST_CASE(compression_decompression_single)
{
    l2::DataAvailabilityLayer da(1);
    
    std::vector<CTransaction> txs = CreateTestTransactions(1);
    auto compressed = da.CompressTransactions(txs);
    auto decompressed = da.DecompressTransactions(compressed);
    
    BOOST_CHECK_EQUAL(decompressed.size(), 1u);
    BOOST_CHECK(decompressed[0].GetHash() == txs[0].GetHash());
}

BOOST_AUTO_TEST_CASE(erasure_encode_decode_basic)
{
    l2::DataAvailabilityLayer da(1);
    
    // Create test data
    std::vector<unsigned char> data = RandomBytes(1000);
    
    // Encode
    auto shards = da.ErasureEncode(data, 4, 2);
    BOOST_CHECK_EQUAL(shards.size(), 6u);
    
    // Verify shard properties
    uint32_t dataCount = 0;
    uint32_t parityCount = 0;
    for (const auto& shard : shards) {
        BOOST_CHECK(shard.VerifyHash());
        if (shard.isData) dataCount++;
        else parityCount++;
    }
    BOOST_CHECK_EQUAL(dataCount, 4u);
    BOOST_CHECK_EQUAL(parityCount, 2u);
    
    // Decode with all shards
    auto decoded = da.ErasureDecode(shards, 4, 2, data.size());
    BOOST_CHECK_EQUAL(decoded.size(), data.size());
    BOOST_CHECK(decoded == data);
}

BOOST_AUTO_TEST_CASE(batch_creation_and_retrieval)
{
    l2::DataAvailabilityLayer da(1);
    
    // Create test transactions
    auto txs = CreateTestTransactions(5);
    
    // Create batch
    uint256 preState = TestRand256();
    uint256 postState = TestRand256();
    uint160 sequencer = RandomAddress();
    
    l2::BatchData batch = da.CreateBatch(
        txs, 100, 109, preState, postState, sequencer);
    
    BOOST_CHECK_EQUAL(batch.startBlock, 100u);
    BOOST_CHECK_EQUAL(batch.endBlock, 109u);
    BOOST_CHECK(batch.preStateRoot == preState);
    BOOST_CHECK(batch.postStateRoot == postState);
    BOOST_CHECK(batch.sequencerAddress == sequencer);
    BOOST_CHECK_EQUAL(batch.transactionCount, 5u);
    BOOST_CHECK(!batch.compressedTransactions.empty());
    BOOST_CHECK(batch.ValidateStructure());
}

BOOST_AUTO_TEST_CASE(batch_publish_and_retrieve)
{
    l2::DataAvailabilityLayer da(1);
    
    // Create and publish batch
    auto txs = CreateTestTransactions(3);
    l2::BatchData batch = da.CreateBatch(
        txs, 0, 9, TestRand256(), TestRand256(), RandomAddress());
    
    auto result = da.PublishBatch(batch);
    BOOST_CHECK(result.success);
    BOOST_CHECK(!result.batchHash.IsNull());
    
    // Retrieve batch
    auto retrieved = da.GetBatch(result.batchHash);
    BOOST_CHECK(retrieved.has_value());
    BOOST_CHECK(retrieved->GetHash() == batch.GetHash());
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 13: Data Availability Reconstruction**
 * 
 * *For any* batch of transactions, compressing and then decompressing
 * SHALL produce the original transactions.
 * 
 * **Validates: Requirements 7.3, 7.5**
 */
BOOST_AUTO_TEST_CASE(property_compression_round_trip)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 10 iterations with varying transaction counts
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random number of transactions (1-20)
        size_t txCount = 1 + (TestRand32() % 20);
        auto txs = CreateTestTransactions(txCount);
        
        // Compress
        auto compressed = da.CompressTransactions(txs);
        
        // Decompress
        auto decompressed = da.DecompressTransactions(compressed);
        
        // Verify count matches
        BOOST_CHECK_EQUAL(decompressed.size(), txs.size());
        
        // Verify each transaction matches
        for (size_t i = 0; i < txs.size(); ++i) {
            BOOST_CHECK_MESSAGE(
                decompressed[i].GetHash() == txs[i].GetHash(),
                "Transaction mismatch at index " << i << " in iteration " << iteration);
        }
    }
}

/**
 * **Property 13: Data Availability Reconstruction (Erasure Coding)**
 * 
 * *For any* data, erasure encoding and then decoding with all shards
 * SHALL produce the original data.
 * 
 * **Validates: Requirements 7.3, 11.6**
 */
BOOST_AUTO_TEST_CASE(property_erasure_coding_round_trip)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 10 iterations with varying data sizes
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random data (100-5000 bytes)
        size_t dataSize = 100 + (TestRand32() % 4900);
        auto data = RandomBytes(dataSize);
        
        // Encode with default shards (4 data, 2 parity)
        auto shards = da.ErasureEncode(data, 4, 2);
        BOOST_CHECK_EQUAL(shards.size(), 6u);
        
        // Decode with all shards
        auto decoded = da.ErasureDecode(shards, 4, 2, data.size());
        
        // Verify data matches
        BOOST_CHECK_EQUAL(decoded.size(), data.size());
        BOOST_CHECK_MESSAGE(decoded == data,
            "Erasure coding round-trip failed for iteration " << iteration);
    }
}

/**
 * **Property 13: Data Availability Reconstruction (Missing Shard)**
 * 
 * *For any* data with erasure coding, removing one data shard and
 * reconstructing from remaining shards SHALL produce the original data.
 * 
 * **Validates: Requirements 7.3, 41.2**
 */
BOOST_AUTO_TEST_CASE(property_erasure_reconstruction_with_missing_shard)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 5 iterations
    for (int iteration = 0; iteration < 5; ++iteration) {
        // Generate random data
        size_t dataSize = 500 + (TestRand32() % 1500);
        auto data = RandomBytes(dataSize);
        
        // Encode
        auto shards = da.ErasureEncode(data, 4, 2);
        BOOST_CHECK_EQUAL(shards.size(), 6u);
        
        // Remove one data shard (index 0-3)
        uint32_t removeIndex = TestRand32() % 4;
        std::vector<l2::ErasureShard> partialShards;
        for (const auto& shard : shards) {
            if (shard.index != removeIndex) {
                partialShards.push_back(shard);
            }
        }
        
        // Should still be able to reconstruct
        BOOST_CHECK(l2::DataAvailabilityLayer::CanReconstruct(partialShards, 4));
        
        // Decode with partial shards
        auto decoded = da.ErasureDecode(partialShards, 4, 2, data.size());
        
        // Verify data matches
        BOOST_CHECK_EQUAL(decoded.size(), data.size());
        BOOST_CHECK_MESSAGE(decoded == data,
            "Reconstruction with missing shard " << removeIndex 
            << " failed for iteration " << iteration);
    }
}

/**
 * **Property 13: DA Commitment Consistency**
 * 
 * *For any* batch data, generating a DA commitment and verifying it
 * against the original data SHALL succeed.
 * 
 * **Validates: Requirements 7.2, 24.4**
 */
BOOST_AUTO_TEST_CASE(property_da_commitment_verification)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random data
        size_t dataSize = 100 + (TestRand32() % 2000);
        auto data = RandomBytes(dataSize);
        uint256 batchHash = TestRand256();
        
        // Generate commitment
        auto commitment = da.GenerateDACommitment(data, batchHash);
        
        // Verify commitment
        BOOST_CHECK_MESSAGE(da.VerifyDACommitment(commitment, data),
            "DA commitment verification failed for iteration " << iteration);
        
        // Verify commitment structure
        BOOST_CHECK(commitment.ValidateStructure());
        BOOST_CHECK_EQUAL(commitment.dataSize, dataSize);
        BOOST_CHECK(commitment.batchHash == batchHash);
    }
}

/**
 * **Property 13: DA Commitment Tamper Detection**
 * 
 * *For any* batch data, modifying the data after commitment generation
 * SHALL cause verification to fail.
 * 
 * **Validates: Requirements 7.2**
 */
BOOST_AUTO_TEST_CASE(property_da_commitment_tamper_detection)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Generate random data
        size_t dataSize = 100 + (TestRand32() % 1000);
        auto data = RandomBytes(dataSize);
        uint256 batchHash = TestRand256();
        
        // Generate commitment
        auto commitment = da.GenerateDACommitment(data, batchHash);
        
        // Verify original data passes
        BOOST_CHECK(da.VerifyDACommitment(commitment, data));
        
        // Tamper with data
        auto tamperedData = data;
        size_t tamperIndex = TestRand32() % tamperedData.size();
        tamperedData[tamperIndex] ^= 0xFF;
        
        // Verify tampered data fails
        BOOST_CHECK_MESSAGE(!da.VerifyDACommitment(commitment, tamperedData),
            "Tamper detection failed for iteration " << iteration);
    }
}

/**
 * **Property 13: Batch Round-Trip**
 * 
 * *For any* set of transactions, creating a batch, publishing it,
 * and retrieving it SHALL preserve all batch properties.
 * 
 * **Validates: Requirements 7.1, 11.6**
 */
BOOST_AUTO_TEST_CASE(property_batch_round_trip)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 5 iterations
    for (int iteration = 0; iteration < 5; ++iteration) {
        // Create random transactions
        size_t txCount = 1 + (TestRand32() % 10);
        auto txs = CreateTestTransactions(txCount);
        
        // Create batch with random parameters
        uint64_t startBlock = TestRand32() % 1000;
        uint64_t endBlock = startBlock + (TestRand32() % 100);
        uint256 preState = TestRand256();
        uint256 postState = TestRand256();
        uint160 sequencer = RandomAddress();
        
        l2::BatchData batch = da.CreateBatch(
            txs, startBlock, endBlock, preState, postState, sequencer);
        
        // Publish batch
        auto result = da.PublishBatch(batch);
        BOOST_CHECK_MESSAGE(result.success,
            "Batch publish failed for iteration " << iteration);
        
        // Retrieve batch
        auto retrieved = da.GetBatch(result.batchHash);
        BOOST_CHECK(retrieved.has_value());
        
        // Verify all properties preserved
        BOOST_CHECK_EQUAL(retrieved->startBlock, startBlock);
        BOOST_CHECK_EQUAL(retrieved->endBlock, endBlock);
        BOOST_CHECK(retrieved->preStateRoot == preState);
        BOOST_CHECK(retrieved->postStateRoot == postState);
        BOOST_CHECK(retrieved->sequencerAddress == sequencer);
        BOOST_CHECK_EQUAL(retrieved->transactionCount, txCount);
        
        // Verify transactions can be decompressed
        auto decompressedTxs = da.DecompressTransactions(
            retrieved->compressedTransactions);
        BOOST_CHECK_EQUAL(decompressedTxs.size(), txCount);
    }
}

/**
 * **Property: DA Sampling Confidence**
 * 
 * *For any* valid DA commitment with available data, sampling
 * SHALL achieve high confidence.
 * 
 * **Validates: Requirements 7.2, 24.4**
 */
BOOST_AUTO_TEST_CASE(property_da_sampling_confidence)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 5 iterations
    for (int iteration = 0; iteration < 5; ++iteration) {
        // Generate random data
        size_t dataSize = 500 + (TestRand32() % 2000);
        auto data = RandomBytes(dataSize);
        uint256 batchHash = TestRand256();
        
        // Generate commitment (this also stores shards)
        auto commitment = da.GenerateDACommitment(data, batchHash);
        
        // Sample data availability
        auto result = da.SampleDataAvailability(commitment, 8);
        
        // With all data available, sampling should succeed
        BOOST_CHECK_MESSAGE(result.success,
            "DA sampling failed for iteration " << iteration);
        BOOST_CHECK_MESSAGE(result.confidence > 0.9,
            "DA sampling confidence too low: " << result.confidence 
            << " for iteration " << iteration);
    }
}

/**
 * **Property: Batch Hash Determinism**
 * 
 * *For any* batch, computing the hash multiple times SHALL produce
 * the same result.
 * 
 * **Validates: Requirements 3.2**
 */
BOOST_AUTO_TEST_CASE(property_batch_hash_determinism)
{
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        l2::BatchData batch;
        batch.version = l2::L2_PROTOCOL_VERSION;
        batch.startBlock = TestRand32() % 10000;
        batch.endBlock = batch.startBlock + (TestRand32() % 100);
        batch.preStateRoot = TestRand256();
        batch.postStateRoot = TestRand256();
        batch.transactionsRoot = TestRand256();
        batch.transactionCount = TestRand32() % 1000;
        batch.totalGasUsed = TestRand32();
        batch.l2ChainId = 1;
        batch.l1AnchorBlock = TestRand32();
        batch.l1AnchorHash = TestRand256();
        batch.sequencerAddress = RandomAddress();
        batch.timestamp = TestRand32();
        
        // Compute hash multiple times
        uint256 hash1 = batch.GetHash();
        uint256 hash2 = batch.GetHash();
        uint256 hash3 = batch.GetHash();
        
        BOOST_CHECK_MESSAGE(hash1 == hash2 && hash2 == hash3,
            "Batch hash not deterministic for iteration " << iteration);
    }
}

/**
 * **Property: Compression Ratio**
 * 
 * *For any* data, compression SHALL not increase size significantly
 * (accounting for header overhead).
 * 
 * **Validates: Requirements 7.5**
 */
BOOST_AUTO_TEST_CASE(property_compression_ratio)
{
    l2::DataAvailabilityLayer da(1);
    
    // Run 10 iterations
    for (int iteration = 0; iteration < 10; ++iteration) {
        // Create transactions
        size_t txCount = 5 + (TestRand32() % 20);
        auto txs = CreateTestTransactions(txCount);
        
        // Serialize to get original size
        CDataStream ss(SER_DISK, 0);
        ss << txs;
        size_t originalSize = ss.size();
        
        // Compress
        auto compressed = da.CompressTransactions(txs);
        
        // Compression should not increase size by more than 20% (header overhead)
        double ratio = l2::DataAvailabilityLayer::GetCompressionRatio(
            originalSize, compressed.size());
        
        BOOST_CHECK_MESSAGE(ratio < 1.2,
            "Compression ratio too high: " << ratio 
            << " for iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
