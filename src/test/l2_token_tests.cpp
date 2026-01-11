// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_token_tests.cpp
 * @brief Tests for L2 Token structures
 * 
 * This file contains unit tests and property-based tests for the L2 token
 * model structures: L2TokenConfig, L2TokenSupply, and MintingRecord.
 * 
 * Feature: l2-bridge-security
 */

#include <l2/l2_token.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(l2_token_tests, BasicTestingSetup)

// ============================================================================
// Helper Functions for Property-Based Testing
// ============================================================================

/**
 * @brief Generate a random string of specified length
 * @param length The length of the string to generate
 * @return Random alphanumeric string
 */
static std::string GenerateRandomString(size_t length) {
    static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[InsecureRandRange(sizeof(charset) - 1)];
    }
    return result;
}

/**
 * @brief Generate a random length within a range
 * @param min Minimum length (inclusive)
 * @param max Maximum length (inclusive)
 * @return Random length
 */
static size_t GenerateRandomLength(size_t min, size_t max) {
    return min + InsecureRandRange(max - min + 1);
}

// ============================================================================
// Property 1: Token Name and Symbol Validation
// Feature: l2-bridge-security, Property 1: Token Name and Symbol Validation
// Validates: Requirements 1.3, 1.4
// ============================================================================

/**
 * Property 1: Token Name and Symbol Validation
 * 
 * For any token name string, the system SHALL accept it if and only if
 * its length is between 3 and 32 characters inclusive.
 * For any token symbol string, the system SHALL accept it if and only if
 * its length is between 2 and 8 characters inclusive.
 */
BOOST_AUTO_TEST_CASE(property_token_name_symbol_validation)
{
    // Feature: l2-bridge-security, Property 1: Token Name and Symbol Validation
    // Validates: Requirements 1.3, 1.4
    
    SeedInsecureRand(false);  // Use random seed for property testing
    
    const size_t NUM_ITERATIONS = 100;
    
    // Test token name validation
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        // Generate random length (0 to 50 to cover all cases)
        size_t length = InsecureRandRange(51);
        std::string name = GenerateRandomString(length);
        
        bool expected = (length >= l2::MIN_TOKEN_NAME_LENGTH && 
                        length <= l2::MAX_TOKEN_NAME_LENGTH);
        bool actual = l2::L2TokenConfig::ValidateTokenName(name);
        
        BOOST_CHECK_MESSAGE(expected == actual,
            "Token name validation failed for length " << length << 
            ": expected " << expected << ", got " << actual);
    }
    
    // Test token symbol validation
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        // Generate random length (0 to 15 to cover all cases)
        size_t length = InsecureRandRange(16);
        std::string symbol = GenerateRandomString(length);
        
        bool expected = (length >= l2::MIN_TOKEN_SYMBOL_LENGTH && 
                        length <= l2::MAX_TOKEN_SYMBOL_LENGTH);
        bool actual = l2::L2TokenConfig::ValidateTokenSymbol(symbol);
        
        BOOST_CHECK_MESSAGE(expected == actual,
            "Token symbol validation failed for length " << length << 
            ": expected " << expected << ", got " << actual);
    }
}

/**
 * Property test: Valid names are always accepted
 * For any string with length in [3, 32], validation returns true
 */
BOOST_AUTO_TEST_CASE(property_valid_names_accepted)
{
    SeedInsecureRand(false);
    
    const size_t NUM_ITERATIONS = 100;
    
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        size_t length = GenerateRandomLength(l2::MIN_TOKEN_NAME_LENGTH, 
                                             l2::MAX_TOKEN_NAME_LENGTH);
        std::string name = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(l2::L2TokenConfig::ValidateTokenName(name),
            "Valid token name rejected: length=" << length << ", name=" << name);
    }
}

/**
 * Property test: Valid symbols are always accepted
 * For any string with length in [2, 8], validation returns true
 */
BOOST_AUTO_TEST_CASE(property_valid_symbols_accepted)
{
    SeedInsecureRand(false);
    
    const size_t NUM_ITERATIONS = 100;
    
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        size_t length = GenerateRandomLength(l2::MIN_TOKEN_SYMBOL_LENGTH, 
                                             l2::MAX_TOKEN_SYMBOL_LENGTH);
        std::string symbol = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(l2::L2TokenConfig::ValidateTokenSymbol(symbol),
            "Valid token symbol rejected: length=" << length << ", symbol=" << symbol);
    }
}

/**
 * Property test: Invalid names are always rejected
 * For any string with length < 3 or > 32, validation returns false
 */
BOOST_AUTO_TEST_CASE(property_invalid_names_rejected)
{
    SeedInsecureRand(false);
    
    const size_t NUM_ITERATIONS = 100;
    
    // Test names that are too short (0-2 chars)
    for (size_t i = 0; i < NUM_ITERATIONS / 2; ++i) {
        size_t length = InsecureRandRange(l2::MIN_TOKEN_NAME_LENGTH);  // 0, 1, or 2
        std::string name = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(!l2::L2TokenConfig::ValidateTokenName(name),
            "Too short token name accepted: length=" << length);
    }
    
    // Test names that are too long (33+ chars)
    for (size_t i = 0; i < NUM_ITERATIONS / 2; ++i) {
        size_t length = l2::MAX_TOKEN_NAME_LENGTH + 1 + InsecureRandRange(20);
        std::string name = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(!l2::L2TokenConfig::ValidateTokenName(name),
            "Too long token name accepted: length=" << length);
    }
}

/**
 * Property test: Invalid symbols are always rejected
 * For any string with length < 2 or > 8, validation returns false
 */
BOOST_AUTO_TEST_CASE(property_invalid_symbols_rejected)
{
    SeedInsecureRand(false);
    
    const size_t NUM_ITERATIONS = 100;
    
    // Test symbols that are too short (0-1 chars)
    for (size_t i = 0; i < NUM_ITERATIONS / 2; ++i) {
        size_t length = InsecureRandRange(l2::MIN_TOKEN_SYMBOL_LENGTH);  // 0 or 1
        std::string symbol = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(!l2::L2TokenConfig::ValidateTokenSymbol(symbol),
            "Too short token symbol accepted: length=" << length);
    }
    
    // Test symbols that are too long (9+ chars)
    for (size_t i = 0; i < NUM_ITERATIONS / 2; ++i) {
        size_t length = l2::MAX_TOKEN_SYMBOL_LENGTH + 1 + InsecureRandRange(10);
        std::string symbol = GenerateRandomString(length);
        
        BOOST_CHECK_MESSAGE(!l2::L2TokenConfig::ValidateTokenSymbol(symbol),
            "Too long token symbol accepted: length=" << length);
    }
}

// ============================================================================
// Boundary Tests (Edge Cases)
// ============================================================================

BOOST_AUTO_TEST_CASE(token_name_boundary_values)
{
    // Exact boundary values for token name
    
    // Length 2 (just below minimum) - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenName("AB"));
    
    // Length 3 (minimum) - should pass
    BOOST_CHECK(l2::L2TokenConfig::ValidateTokenName("ABC"));
    
    // Length 32 (maximum) - should pass
    BOOST_CHECK(l2::L2TokenConfig::ValidateTokenName("12345678901234567890123456789012"));
    
    // Length 33 (just above maximum) - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenName("123456789012345678901234567890123"));
    
    // Empty string - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenName(""));
}

BOOST_AUTO_TEST_CASE(token_symbol_boundary_values)
{
    // Exact boundary values for token symbol
    
    // Length 1 (just below minimum) - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenSymbol("A"));
    
    // Length 2 (minimum) - should pass
    BOOST_CHECK(l2::L2TokenConfig::ValidateTokenSymbol("AB"));
    
    // Length 8 (maximum) - should pass
    BOOST_CHECK(l2::L2TokenConfig::ValidateTokenSymbol("ABCDEFGH"));
    
    // Length 9 (just above maximum) - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenSymbol("ABCDEFGHI"));
    
    // Empty string - should fail
    BOOST_CHECK(!l2::L2TokenConfig::ValidateTokenSymbol(""));
}

// ============================================================================
// L2TokenConfig Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l2_token_config_default_constructor)
{
    l2::L2TokenConfig config;
    
    BOOST_CHECK_EQUAL(config.tokenName, "L2Token");
    BOOST_CHECK_EQUAL(config.tokenSymbol, "L2T");
    BOOST_CHECK_EQUAL(config.sequencerReward, l2::DEFAULT_SEQUENCER_REWARD);
    BOOST_CHECK_EQUAL(config.mintingFee, l2::DEFAULT_MINTING_FEE);
    BOOST_CHECK_EQUAL(config.maxGenesisSupply, l2::DEFAULT_MAX_GENESIS_SUPPLY);
    BOOST_CHECK_EQUAL(config.minTransferFee, l2::DEFAULT_MIN_TRANSFER_FEE);
    BOOST_CHECK(config.IsValid());
}

BOOST_AUTO_TEST_CASE(l2_token_config_custom_constructor)
{
    l2::L2TokenConfig config("CasLayer", "CLAY");
    
    BOOST_CHECK_EQUAL(config.tokenName, "CasLayer");
    BOOST_CHECK_EQUAL(config.tokenSymbol, "CLAY");
    BOOST_CHECK(config.IsValid());
}

BOOST_AUTO_TEST_CASE(l2_token_config_full_constructor)
{
    l2::L2TokenConfig config("FastCoin", "FAST", 20 * COIN, COIN / 50, 
                             500000 * COIN, COIN / 5000);
    
    BOOST_CHECK_EQUAL(config.tokenName, "FastCoin");
    BOOST_CHECK_EQUAL(config.tokenSymbol, "FAST");
    BOOST_CHECK_EQUAL(config.sequencerReward, 20 * COIN);
    BOOST_CHECK_EQUAL(config.mintingFee, COIN / 50);
    BOOST_CHECK_EQUAL(config.maxGenesisSupply, 500000 * COIN);
    BOOST_CHECK_EQUAL(config.minTransferFee, COIN / 5000);
    BOOST_CHECK(config.IsValid());
}

BOOST_AUTO_TEST_CASE(l2_token_config_serialization)
{
    l2::L2TokenConfig config1("TestToken", "TEST", 15 * COIN, COIN / 100,
                              750000 * COIN, COIN / 10000);
    
    // Serialize
    std::vector<unsigned char> data = config1.Serialize();
    BOOST_CHECK(!data.empty());
    
    // Deserialize
    l2::L2TokenConfig config2;
    BOOST_CHECK(config2.Deserialize(data));
    
    // Verify equality
    BOOST_CHECK(config1 == config2);
}

BOOST_AUTO_TEST_CASE(l2_token_config_invalid)
{
    // Invalid name (too short)
    l2::L2TokenConfig config1("AB", "TEST");
    BOOST_CHECK(!config1.IsValid());
    
    // Invalid symbol (too long)
    l2::L2TokenConfig config2("ValidName", "TOOLONGSYM");
    BOOST_CHECK(!config2.IsValid());
}

// ============================================================================
// L2TokenSupply Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(l2_token_supply_default_constructor)
{
    l2::L2TokenSupply supply;
    
    BOOST_CHECK_EQUAL(supply.totalSupply, 0);
    BOOST_CHECK_EQUAL(supply.genesisSupply, 0);
    BOOST_CHECK_EQUAL(supply.mintedSupply, 0);
    BOOST_CHECK_EQUAL(supply.burnedSupply, 0);
    BOOST_CHECK_EQUAL(supply.totalBlocksRewarded, 0);
    BOOST_CHECK(supply.VerifyInvariant());
}

BOOST_AUTO_TEST_CASE(l2_token_supply_invariant_valid)
{
    // Valid supply: total = genesis + minted - burned
    l2::L2TokenSupply supply(1500000 * COIN, 1000000 * COIN, 600000 * COIN, 
                             100000 * COIN, 60000);
    
    BOOST_CHECK(supply.VerifyInvariant());
    BOOST_CHECK_EQUAL(supply.CalculateExpectedTotal(), supply.totalSupply);
}

BOOST_AUTO_TEST_CASE(l2_token_supply_invariant_invalid)
{
    // Invalid supply: total != genesis + minted - burned
    l2::L2TokenSupply supply(2000000 * COIN, 1000000 * COIN, 600000 * COIN, 
                             100000 * COIN, 60000);
    
    BOOST_CHECK(!supply.VerifyInvariant());
    BOOST_CHECK_NE(supply.CalculateExpectedTotal(), supply.totalSupply);
}

BOOST_AUTO_TEST_CASE(l2_token_supply_serialization)
{
    l2::L2TokenSupply supply1(1500000 * COIN, 1000000 * COIN, 600000 * COIN, 
                              100000 * COIN, 60000);
    
    // Serialize
    std::vector<unsigned char> data = supply1.Serialize();
    BOOST_CHECK(!data.empty());
    
    // Deserialize
    l2::L2TokenSupply supply2;
    BOOST_CHECK(supply2.Deserialize(data));
    
    // Verify equality
    BOOST_CHECK(supply1 == supply2);
}

// ============================================================================
// MintingRecord Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(minting_record_default_constructor)
{
    l2::MintingRecord record;
    
    BOOST_CHECK(record.l2BlockHash.IsNull());
    BOOST_CHECK_EQUAL(record.l2BlockNumber, 0);
    BOOST_CHECK(record.sequencerAddress.IsNull());
    BOOST_CHECK_EQUAL(record.rewardAmount, 0);
    BOOST_CHECK(record.l1TxHash.IsNull());
    BOOST_CHECK_EQUAL(record.l1BlockNumber, 0);
    BOOST_CHECK_EQUAL(record.feePaid, 0);
    BOOST_CHECK_EQUAL(record.timestamp, 0);
}

BOOST_AUTO_TEST_CASE(minting_record_full_constructor)
{
    uint256 l2Hash, l1Hash;
    l2Hash.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    l1Hash.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    
    uint160 sequencer;
    sequencer.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::MintingRecord record(l2Hash, 100, sequencer, 10 * COIN, 
                             l1Hash, 50000, COIN / 100, 1700000000);
    
    BOOST_CHECK(record.l2BlockHash == l2Hash);
    BOOST_CHECK_EQUAL(record.l2BlockNumber, 100);
    BOOST_CHECK(record.sequencerAddress == sequencer);
    BOOST_CHECK_EQUAL(record.rewardAmount, 10 * COIN);
    BOOST_CHECK(record.l1TxHash == l1Hash);
    BOOST_CHECK_EQUAL(record.l1BlockNumber, 50000);
    BOOST_CHECK_EQUAL(record.feePaid, COIN / 100);
    BOOST_CHECK_EQUAL(record.timestamp, 1700000000);
}

BOOST_AUTO_TEST_CASE(minting_record_hash)
{
    uint256 l2Hash, l1Hash;
    l2Hash.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    l1Hash.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    
    uint160 sequencer;
    sequencer.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::MintingRecord record1(l2Hash, 100, sequencer, 10 * COIN, 
                              l1Hash, 50000, COIN / 100, 1700000000);
    l2::MintingRecord record2(l2Hash, 100, sequencer, 10 * COIN, 
                              l1Hash, 50000, COIN / 100, 1700000000);
    l2::MintingRecord record3(l2Hash, 101, sequencer, 10 * COIN, 
                              l1Hash, 50000, COIN / 100, 1700000000);
    
    // Same records should have same hash
    BOOST_CHECK(record1.GetHash() == record2.GetHash());
    
    // Different records should have different hash
    BOOST_CHECK(record1.GetHash() != record3.GetHash());
}

BOOST_AUTO_TEST_CASE(minting_record_serialization)
{
    uint256 l2Hash, l1Hash;
    l2Hash.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    l1Hash.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    
    uint160 sequencer;
    sequencer.SetHex("abcdef1234567890abcdef1234567890abcdef12");
    
    l2::MintingRecord record1(l2Hash, 100, sequencer, 10 * COIN, 
                              l1Hash, 50000, COIN / 100, 1700000000);
    
    // Serialize
    std::vector<unsigned char> data = record1.Serialize();
    BOOST_CHECK(!data.empty());
    
    // Deserialize
    l2::MintingRecord record2;
    BOOST_CHECK(record2.Deserialize(data));
    
    // Verify equality
    BOOST_CHECK(record1 == record2);
    BOOST_CHECK(record1.GetHash() == record2.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()
