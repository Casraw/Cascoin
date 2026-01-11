// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_burn_validator_tests.cpp
 * @brief Property-based tests for L2 Burn Validator
 * 
 * **Feature: l2-bridge-security, Property 9: Confirmation Count Requirement**
 * **Validates: Requirements 2.2**
 * 
 * Property 9: Confirmation Count Requirement
 * *For any* burn transaction, the system SHALL only begin consensus if the
 * L1 transaction has at least 6 confirmations.
 * 
 * **Feature: l2-bridge-security, Property 8: Chain ID Validation**
 * **Validates: Requirements 2.3**
 * 
 * Property 8: Chain ID Validation
 * *For any* burn transaction, the system SHALL only process it if the
 * chain_id in the OP_RETURN matches the current L2 chain's ID.
 */

#include <l2/burn_validator.h>
#include <l2/burn_parser.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <script/script.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

#include <map>
#include <memory>
#include <vector>

namespace {

// Local random context for tests
static FastRandomContext g_test_rand_ctx(true);

static uint32_t TestRand32() { 
    return g_test_rand_ctx.rand32(); 
}

static uint64_t TestRand64() {
    return ((uint64_t)g_test_rand_ctx.rand32() << 32) | g_test_rand_ctx.rand32();
}

/**
 * Helper function to generate a valid compressed public key
 */
static CPubKey GenerateValidCompressedPubKey()
{
    std::vector<unsigned char> pubkeyData(33);
    pubkeyData[0] = (TestRand32() % 2 == 0) ? 0x02 : 0x03;
    
    for (size_t i = 1; i < 33; ++i) {
        pubkeyData[i] = static_cast<unsigned char>(TestRand32() & 0xFF);
    }
    
    return CPubKey(pubkeyData.begin(), pubkeyData.end());
}

/**
 * Helper function to generate a random chain ID (non-zero)
 */
static uint32_t RandomChainId()
{
    uint32_t chainId = TestRand32();
    return chainId == 0 ? 1 : chainId;
}

/**
 * Helper function to generate a random valid burn amount
 */
static CAmount RandomBurnAmount()
{
    return (TestRand64() % (1000 * COIN)) + 1;
}

/**
 * Helper to generate a random transaction hash
 */
static uint256 RandomTxHash()
{
    uint256 hash;
    for (int i = 0; i < 8; ++i) {
        ((uint32_t*)hash.begin())[i] = TestRand32();
    }
    return hash;
}

/**
 * Helper to create a valid burn transaction
 */
static CTransaction CreateBurnTransaction(uint32_t chainId, const CPubKey& pubkey, CAmount amount)
{
    CScript burnScript = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
    
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.nLockTime = 0;
    
    // Add a dummy input
    CTxIn input;
    input.prevout.hash = RandomTxHash();
    input.prevout.n = 0;
    mtx.vin.push_back(input);
    
    // Add the burn output
    CTxOut burnOutput(0, burnScript);
    mtx.vout.push_back(burnOutput);
    
    return CTransaction(mtx);
}

/**
 * Mock transaction store for testing
 */
class MockTxStore {
public:
    void AddTransaction(const uint256& txHash, const CTransaction& tx, int confirmations, 
                        const uint256& blockHash = uint256(), uint64_t blockNumber = 0)
    {
        transactions_[txHash] = std::make_shared<CTransaction>(tx);
        confirmations_[txHash] = confirmations;
        blockInfo_[txHash] = std::make_pair(blockHash, blockNumber);
    }
    
    std::optional<CTransaction> GetTransaction(const uint256& txHash) const
    {
        auto it = transactions_.find(txHash);
        if (it != transactions_.end()) {
            return *(it->second);
        }
        return std::nullopt;
    }
    
    int GetConfirmations(const uint256& txHash) const
    {
        auto it = confirmations_.find(txHash);
        if (it != confirmations_.end()) {
            return it->second;
        }
        return -1;
    }
    
    std::optional<std::pair<uint256, uint64_t>> GetBlockInfo(const uint256& txHash) const
    {
        auto it = blockInfo_.find(txHash);
        if (it != blockInfo_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    void SetConfirmations(const uint256& txHash, int confirmations)
    {
        confirmations_[txHash] = confirmations;
    }
    
    void Clear()
    {
        transactions_.clear();
        confirmations_.clear();
        blockInfo_.clear();
    }

private:
    std::map<uint256, std::shared_ptr<CTransaction>> transactions_;
    std::map<uint256, int> confirmations_;
    std::map<uint256, std::pair<uint256, uint64_t>> blockInfo_;
};

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_burn_validator_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(validator_construction)
{
    uint32_t chainId = 1;
    l2::BurnValidator validator(chainId);
    
    BOOST_CHECK_EQUAL(validator.GetChainId(), chainId);
    BOOST_CHECK_EQUAL(l2::BurnValidator::GetRequiredConfirmations(), 6);
}

BOOST_AUTO_TEST_CASE(matches_chain_id_correct)
{
    uint32_t chainId = 42;
    l2::BurnValidator validator(chainId);
    
    l2::BurnData burnData;
    burnData.chainId = 42;
    burnData.recipientPubKey = GenerateValidCompressedPubKey();
    burnData.amount = 100 * COIN;
    
    BOOST_CHECK(validator.MatchesChainId(burnData));
}

BOOST_AUTO_TEST_CASE(matches_chain_id_incorrect)
{
    uint32_t chainId = 42;
    l2::BurnValidator validator(chainId);
    
    l2::BurnData burnData;
    burnData.chainId = 99;  // Different chain ID
    burnData.recipientPubKey = GenerateValidCompressedPubKey();
    burnData.amount = 100 * COIN;
    
    BOOST_CHECK(!validator.MatchesChainId(burnData));
}

BOOST_AUTO_TEST_CASE(is_already_processed_tracking)
{
    l2::BurnValidator validator(1);
    
    uint256 txHash = RandomTxHash();
    
    // Initially not processed
    BOOST_CHECK(!validator.IsAlreadyProcessed(txHash));
    BOOST_CHECK_EQUAL(validator.GetProcessedCount(), 0u);
    
    // Mark as processed
    validator.MarkAsProcessed(txHash);
    
    // Now should be processed
    BOOST_CHECK(validator.IsAlreadyProcessed(txHash));
    BOOST_CHECK_EQUAL(validator.GetProcessedCount(), 1u);
    
    // Clear and check again
    validator.ClearProcessed();
    BOOST_CHECK(!validator.IsAlreadyProcessed(txHash));
    BOOST_CHECK_EQUAL(validator.GetProcessedCount(), 0u);
}

BOOST_AUTO_TEST_CASE(validate_burn_with_callbacks)
{
    uint32_t chainId = 1;
    MockTxStore store;
    
    // Create a valid burn transaction
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
    uint256 txHash = tx.GetHash();
    
    // Add to store with sufficient confirmations
    uint256 blockHash = RandomTxHash();
    store.AddTransaction(txHash, tx, 10, blockHash, 1000);
    
    // Create validator with callbacks
    l2::BurnValidator validator(
        chainId,
        [&store](const uint256& hash) { return store.GetTransaction(hash); },
        [&store](const uint256& hash) { return store.GetConfirmations(hash); },
        [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
        nullptr  // No processed checker
    );
    
    // Validate
    l2::BurnValidationResult result = validator.ValidateBurn(txHash);
    
    BOOST_CHECK(result.isValid);
    BOOST_CHECK_EQUAL(result.burnData.chainId, chainId);
    BOOST_CHECK(result.burnData.recipientPubKey == pubkey);
    BOOST_CHECK_EQUAL(result.burnData.amount, amount);
    BOOST_CHECK_EQUAL(result.confirmations, 10);
    BOOST_CHECK(result.blockHash == blockHash);
    BOOST_CHECK_EQUAL(result.blockNumber, 1000u);
}

BOOST_AUTO_TEST_CASE(validate_burn_insufficient_confirmations)
{
    uint32_t chainId = 1;
    MockTxStore store;
    
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
    uint256 txHash = tx.GetHash();
    
    // Add to store with insufficient confirmations (only 3)
    store.AddTransaction(txHash, tx, 3);
    
    l2::BurnValidator validator(
        chainId,
        [&store](const uint256& hash) { return store.GetTransaction(hash); },
        [&store](const uint256& hash) { return store.GetConfirmations(hash); },
        [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
        nullptr
    );
    
    l2::BurnValidationResult result = validator.ValidateBurn(txHash);
    
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Insufficient confirmations") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_burn_wrong_chain_id)
{
    uint32_t validatorChainId = 1;
    uint32_t burnChainId = 99;  // Different chain
    MockTxStore store;
    
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    CTransaction tx = CreateBurnTransaction(burnChainId, pubkey, amount);
    uint256 txHash = tx.GetHash();
    
    store.AddTransaction(txHash, tx, 10);
    
    l2::BurnValidator validator(
        validatorChainId,
        [&store](const uint256& hash) { return store.GetTransaction(hash); },
        [&store](const uint256& hash) { return store.GetConfirmations(hash); },
        [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
        nullptr
    );
    
    l2::BurnValidationResult result = validator.ValidateBurn(txHash);
    
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Chain ID mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_burn_already_processed)
{
    uint32_t chainId = 1;
    MockTxStore store;
    
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
    uint256 txHash = tx.GetHash();
    
    store.AddTransaction(txHash, tx, 10);
    
    l2::BurnValidator validator(
        chainId,
        [&store](const uint256& hash) { return store.GetTransaction(hash); },
        [&store](const uint256& hash) { return store.GetConfirmations(hash); },
        [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
        nullptr
    );
    
    // Mark as processed
    validator.MarkAsProcessed(txHash);
    
    l2::BurnValidationResult result = validator.ValidateBurn(txHash);
    
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("already processed") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(validate_burn_tx_not_found)
{
    uint32_t chainId = 1;
    MockTxStore store;  // Empty store
    
    uint256 txHash = RandomTxHash();
    
    l2::BurnValidator validator(
        chainId,
        [&store](const uint256& hash) { return store.GetTransaction(hash); },
        [&store](const uint256& hash) { return store.GetConfirmations(hash); },
        [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
        nullptr
    );
    
    l2::BurnValidationResult result = validator.ValidateBurn(txHash);
    
    BOOST_CHECK(!result.isValid);
    BOOST_CHECK(result.errorMessage.find("Could not fetch") != std::string::npos);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 9: Confirmation Count Requirement**
 * 
 * *For any* burn transaction, the system SHALL only begin consensus if the
 * L1 transaction has at least 6 confirmations.
 * 
 * **Validates: Requirements 2.2**
 */
BOOST_AUTO_TEST_CASE(property_confirmation_count_requirement)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t chainId = RandomChainId();
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        // Generate random confirmation count (0 to 20)
        int confirmations = TestRand32() % 21;
        store.AddTransaction(txHash, tx, confirmations);
        
        l2::BurnValidator validator(
            chainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        l2::BurnValidationResult result = validator.ValidateBurn(txHash);
        
        // Property: Validation should succeed if and only if confirmations >= 6
        if (confirmations >= l2::REQUIRED_CONFIRMATIONS) {
            BOOST_CHECK_MESSAGE(result.isValid,
                "Burn with " << confirmations << " confirmations should be valid in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.confirmations == confirmations,
                "Confirmation count should match in iteration " << iteration);
        } else {
            BOOST_CHECK_MESSAGE(!result.isValid,
                "Burn with " << confirmations << " confirmations should be invalid in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.errorMessage.find("Insufficient confirmations") != std::string::npos,
                "Error should mention insufficient confirmations in iteration " << iteration);
        }
        
        // Property: HasSufficientConfirmations should match
        bool hasSufficient = validator.HasSufficientConfirmations(txHash);
        BOOST_CHECK_MESSAGE(hasSufficient == (confirmations >= l2::REQUIRED_CONFIRMATIONS),
            "HasSufficientConfirmations should return correct value in iteration " << iteration);
    }
}

/**
 * **Property 9 (continued): Exactly 6 confirmations is the threshold**
 * 
 * *For any* burn transaction with exactly 5 confirmations, validation SHALL fail.
 * *For any* burn transaction with exactly 6 confirmations, validation SHALL succeed.
 * 
 * **Validates: Requirements 2.2**
 */
BOOST_AUTO_TEST_CASE(property_confirmation_threshold_boundary)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t chainId = RandomChainId();
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        l2::BurnValidator validator(
            chainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        // Test with exactly 5 confirmations (should fail)
        store.AddTransaction(txHash, tx, 5);
        l2::BurnValidationResult result5 = validator.ValidateBurn(txHash);
        BOOST_CHECK_MESSAGE(!result5.isValid,
            "Burn with exactly 5 confirmations should fail in iteration " << iteration);
        
        // Update to exactly 6 confirmations (should succeed)
        store.SetConfirmations(txHash, 6);
        l2::BurnValidationResult result6 = validator.ValidateBurn(txHash);
        BOOST_CHECK_MESSAGE(result6.isValid,
            "Burn with exactly 6 confirmations should succeed in iteration " << iteration);
    }
}

/**
 * **Property 8: Chain ID Validation**
 * 
 * *For any* burn transaction, the system SHALL only process it if the
 * chain_id in the OP_RETURN matches the current L2 chain's ID.
 * 
 * **Validates: Requirements 2.3**
 */
BOOST_AUTO_TEST_CASE(property_chain_id_validation)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t validatorChainId = RandomChainId();
        uint32_t burnChainId = RandomChainId();
        
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(burnChainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        // Add with sufficient confirmations
        store.AddTransaction(txHash, tx, 10);
        
        l2::BurnValidator validator(
            validatorChainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        l2::BurnValidationResult result = validator.ValidateBurn(txHash);
        
        // Property: Validation should succeed if and only if chain IDs match
        if (validatorChainId == burnChainId) {
            BOOST_CHECK_MESSAGE(result.isValid,
                "Burn with matching chain ID should be valid in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.burnData.chainId == validatorChainId,
                "Parsed chain ID should match validator chain ID in iteration " << iteration);
        } else {
            BOOST_CHECK_MESSAGE(!result.isValid,
                "Burn with mismatched chain ID should be invalid in iteration " << iteration);
            BOOST_CHECK_MESSAGE(result.errorMessage.find("Chain ID mismatch") != std::string::npos,
                "Error should mention chain ID mismatch in iteration " << iteration);
        }
        
        // Property: MatchesChainId should return correct value
        l2::BurnData burnData;
        burnData.chainId = burnChainId;
        burnData.recipientPubKey = pubkey;
        burnData.amount = amount;
        
        bool matches = validator.MatchesChainId(burnData);
        BOOST_CHECK_MESSAGE(matches == (validatorChainId == burnChainId),
            "MatchesChainId should return correct value in iteration " << iteration);
    }
}

/**
 * **Property 8 (continued): Chain ID must be exact match**
 * 
 * *For any* two different chain IDs, validation SHALL fail when they don't match.
 * 
 * **Validates: Requirements 2.3**
 */
BOOST_AUTO_TEST_CASE(property_chain_id_exact_match)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate two guaranteed different chain IDs
        uint32_t chainId1 = RandomChainId();
        uint32_t chainId2 = chainId1 + 1;  // Guaranteed different
        
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        
        // Create burn for chainId1
        CTransaction tx = CreateBurnTransaction(chainId1, pubkey, amount);
        uint256 txHash = tx.GetHash();
        store.AddTransaction(txHash, tx, 10);
        
        // Validator for chainId2 (different)
        l2::BurnValidator validator(
            chainId2,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        l2::BurnValidationResult result = validator.ValidateBurn(txHash);
        
        // Property: Different chain IDs should always fail
        BOOST_CHECK_MESSAGE(!result.isValid,
            "Burn for chain " << chainId1 << " should fail on validator for chain " << chainId2 
            << " in iteration " << iteration);
        
        // Property: MatchesChainId should return false
        l2::BurnData burnData;
        burnData.chainId = chainId1;
        BOOST_CHECK_MESSAGE(!validator.MatchesChainId(burnData),
            "MatchesChainId should return false for different chain IDs in iteration " << iteration);
    }
}

/**
 * **Property: Double-processing prevention**
 * 
 * *For any* burn transaction that has been processed, subsequent validation
 * attempts SHALL fail.
 * 
 * **Validates: Requirements 2.4**
 */
BOOST_AUTO_TEST_CASE(property_double_processing_prevention)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t chainId = RandomChainId();
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        store.AddTransaction(txHash, tx, 10);
        
        l2::BurnValidator validator(
            chainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        // First validation should succeed
        l2::BurnValidationResult result1 = validator.ValidateBurn(txHash);
        BOOST_CHECK_MESSAGE(result1.isValid,
            "First validation should succeed in iteration " << iteration);
        
        // Mark as processed
        validator.MarkAsProcessed(txHash);
        
        // Second validation should fail
        l2::BurnValidationResult result2 = validator.ValidateBurn(txHash);
        BOOST_CHECK_MESSAGE(!result2.isValid,
            "Second validation should fail after marking as processed in iteration " << iteration);
        BOOST_CHECK_MESSAGE(result2.errorMessage.find("already processed") != std::string::npos,
            "Error should mention already processed in iteration " << iteration);
        
        // Property: IsAlreadyProcessed should return true
        BOOST_CHECK_MESSAGE(validator.IsAlreadyProcessed(txHash),
            "IsAlreadyProcessed should return true after marking in iteration " << iteration);
    }
}

/**
 * **Property: External processed checker callback**
 * 
 * *For any* burn transaction, if an external processed checker returns true,
 * validation SHALL fail.
 * 
 * **Validates: Requirements 2.4**
 */
BOOST_AUTO_TEST_CASE(property_external_processed_checker)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t chainId = RandomChainId();
        MockTxStore store;
        std::set<uint256> externalProcessed;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        store.AddTransaction(txHash, tx, 10);
        
        // Randomly decide if this should be marked as processed externally
        bool shouldBeProcessed = (TestRand32() % 2 == 0);
        if (shouldBeProcessed) {
            externalProcessed.insert(txHash);
        }
        
        l2::BurnValidator validator(
            chainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            [&externalProcessed](const uint256& hash) { 
                return externalProcessed.count(hash) > 0; 
            }
        );
        
        l2::BurnValidationResult result = validator.ValidateBurn(txHash);
        
        // Property: Validation should fail if external checker says processed
        if (shouldBeProcessed) {
            BOOST_CHECK_MESSAGE(!result.isValid,
                "Validation should fail when external checker returns true in iteration " << iteration);
        } else {
            BOOST_CHECK_MESSAGE(result.isValid,
                "Validation should succeed when external checker returns false in iteration " << iteration);
        }
    }
}

/**
 * **Property: Valid burn data is preserved through validation**
 * 
 * *For any* valid burn transaction, the parsed burn data in the result
 * SHALL match the original burn parameters.
 * 
 * **Validates: Requirements 2.1**
 */
BOOST_AUTO_TEST_CASE(property_burn_data_preserved)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t chainId = RandomChainId();
        MockTxStore store;
        
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        CTransaction tx = CreateBurnTransaction(chainId, pubkey, amount);
        uint256 txHash = tx.GetHash();
        
        uint256 blockHash = RandomTxHash();
        uint64_t blockNumber = TestRand64() % 1000000;
        store.AddTransaction(txHash, tx, 10, blockHash, blockNumber);
        
        l2::BurnValidator validator(
            chainId,
            [&store](const uint256& hash) { return store.GetTransaction(hash); },
            [&store](const uint256& hash) { return store.GetConfirmations(hash); },
            [&store](const uint256& hash) { return store.GetBlockInfo(hash); },
            nullptr
        );
        
        l2::BurnValidationResult result = validator.ValidateBurn(txHash);
        
        BOOST_REQUIRE_MESSAGE(result.isValid,
            "Validation should succeed in iteration " << iteration);
        
        // Property: All burn data should be preserved
        BOOST_CHECK_MESSAGE(result.burnData.chainId == chainId,
            "Chain ID should be preserved in iteration " << iteration);
        BOOST_CHECK_MESSAGE(result.burnData.recipientPubKey == pubkey,
            "Recipient pubkey should be preserved in iteration " << iteration);
        BOOST_CHECK_MESSAGE(result.burnData.amount == amount,
            "Amount should be preserved in iteration " << iteration);
        BOOST_CHECK_MESSAGE(result.blockHash == blockHash,
            "Block hash should be preserved in iteration " << iteration);
        BOOST_CHECK_MESSAGE(result.blockNumber == blockNumber,
            "Block number should be preserved in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
