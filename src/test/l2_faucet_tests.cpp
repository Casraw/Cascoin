// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_faucet_tests.cpp
 * @brief Tests for L2 Faucet
 * 
 * This file contains unit tests and property-based tests for the L2 faucet
 * functionality including rate limiting and network restrictions.
 * 
 * Feature: l2-bridge-security
 */

#include <l2/l2_faucet.h>
#include <l2/l2_token_manager.h>
#include <l2/state_manager.h>
#include <chainparams.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(l2_faucet_tests, TestingSetup)

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate a random uint160 address
 * @return Random address
 */
static uint160 GenerateRandomAddress() {
    uint160 addr;
    for (size_t i = 0; i < 20; ++i) {
        *(addr.begin() + i) = static_cast<unsigned char>(InsecureRandRange(256));
    }
    return addr;
}

/**
 * @brief Generate a random amount within faucet limits
 * @return Random amount between 1 and MAX_FAUCET_AMOUNT
 */
static CAmount GenerateRandomFaucetAmount() {
    // Generate amount between 1 satoshi and MAX_FAUCET_AMOUNT
    return 1 + InsecureRandRange(l2::MAX_FAUCET_AMOUNT);
}

// ============================================================================
// Property 8: Faucet Rate Limiting
// Feature: l2-bridge-security, Property 8: Faucet Rate Limiting
// Validates: Requirements 5.2, 5.3
// ============================================================================

/**
 * Property 8: Faucet Rate Limiting
 * 
 * For any faucet request, the system SHALL distribute at most 100 tokens per request.
 * For any address, the system SHALL reject requests made within 1 hour of a previous
 * successful request from the same address.
 */
BOOST_AUTO_TEST_CASE(property_faucet_rate_limiting)
{
    // Feature: l2-bridge-security, Property 8: Faucet Rate Limiting
    // Validates: Requirements 5.2, 5.3
    
    // Note: This test runs on regtest which enables the faucet
    SelectParams(CBaseChainParams::REGTEST);
    
    SeedInsecureRand(false);  // Use random seed for property testing
    
    const size_t NUM_ITERATIONS = 100;
    
    // Create token manager and faucet
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    // Property 8a: Maximum 100 tokens per request
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        uint160 addr = GenerateRandomAddress();
        
        // Request a random amount (could be more than MAX_FAUCET_AMOUNT)
        CAmount requestedAmount = 1 + InsecureRandRange(200 * COIN);  // Up to 200 tokens
        
        l2::FaucetResult result = faucet.RequestTokens(addr, requestedAmount, stateManager);
        
        BOOST_CHECK_MESSAGE(result.success,
            "Faucet request failed unexpectedly: " << result.error);
        
        // Verify amount is capped at MAX_FAUCET_AMOUNT
        BOOST_CHECK_MESSAGE(result.amount <= l2::MAX_FAUCET_AMOUNT,
            "Faucet distributed more than max: " << result.amount << " > " << l2::MAX_FAUCET_AMOUNT);
        
        // Verify amount is the minimum of requested and max
        CAmount expectedAmount = std::min(requestedAmount, l2::MAX_FAUCET_AMOUNT);
        BOOST_CHECK_MESSAGE(result.amount == expectedAmount,
            "Faucet amount mismatch: expected " << expectedAmount << ", got " << result.amount);
        
        // Clear faucet state for next iteration
        faucet.Clear();
    }
    
    // Property 8b: 1 hour cooldown per address
    for (size_t i = 0; i < NUM_ITERATIONS / 10; ++i) {
        faucet.Clear();
        
        uint160 addr = GenerateRandomAddress();
        CAmount amount = GenerateRandomFaucetAmount();
        
        // First request should succeed
        l2::FaucetResult result1 = faucet.RequestTokens(addr, amount, stateManager);
        BOOST_CHECK_MESSAGE(result1.success,
            "First faucet request failed: " << result1.error);
        
        // Immediate second request should fail (within cooldown)
        l2::FaucetResult result2 = faucet.RequestTokens(addr, amount, stateManager);
        BOOST_CHECK_MESSAGE(!result2.success,
            "Second request within cooldown should fail");
        BOOST_CHECK_MESSAGE(result2.cooldownRemaining > 0,
            "Cooldown remaining should be > 0");
        
        // Verify cooldown is approximately COOLDOWN_SECONDS
        BOOST_CHECK_MESSAGE(result2.cooldownRemaining <= l2::COOLDOWN_SECONDS,
            "Cooldown remaining exceeds max: " << result2.cooldownRemaining);
    }
}

/**
 * Property test: Cooldown timing is accurate
 * After cooldown expires, requests should succeed again
 */
BOOST_AUTO_TEST_CASE(property_cooldown_timing)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    SeedInsecureRand(false);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    const size_t NUM_ITERATIONS = 50;
    
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        faucet.Clear();
        
        uint160 addr = GenerateRandomAddress();
        CAmount amount = GenerateRandomFaucetAmount();
        
        // Before any request, should be able to request
        BOOST_CHECK(faucet.CanRequest(addr, 0));
        BOOST_CHECK_EQUAL(faucet.GetCooldownRemaining(addr, 0), 0);
        
        // Make a request (uses real system time internally)
        l2::FaucetResult result = faucet.RequestTokens(addr, amount, stateManager);
        BOOST_CHECK(result.success);
        
        // Immediately after, should not be able to request (using real time)
        BOOST_CHECK(!faucet.CanRequest(addr, 0));
        
        // Cooldown remaining should be positive and <= COOLDOWN_SECONDS
        uint64_t remaining = faucet.GetCooldownRemaining(addr, 0);
        BOOST_CHECK(remaining > 0);
        BOOST_CHECK(remaining <= l2::COOLDOWN_SECONDS);
        
        // Simulate time passing by using a future timestamp
        // Get current time and add COOLDOWN_SECONDS
        uint64_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // At exactly COOLDOWN_SECONDS - 1 from now, should still be in cooldown
        BOOST_CHECK(!faucet.CanRequest(addr, currentTime + l2::COOLDOWN_SECONDS - 1));
        
        // At exactly COOLDOWN_SECONDS from now, should be able to request again
        BOOST_CHECK(faucet.CanRequest(addr, currentTime + l2::COOLDOWN_SECONDS));
        BOOST_CHECK_EQUAL(faucet.GetCooldownRemaining(addr, currentTime + l2::COOLDOWN_SECONDS), 0);
    }
}

/**
 * Property test: Different addresses have independent cooldowns
 */
BOOST_AUTO_TEST_CASE(property_independent_cooldowns)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    SeedInsecureRand(false);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    const size_t NUM_ITERATIONS = 50;
    
    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        faucet.Clear();
        
        // Generate two different addresses
        uint160 addr1 = GenerateRandomAddress();
        uint160 addr2 = GenerateRandomAddress();
        
        // Ensure they're different
        while (addr1 == addr2) {
            addr2 = GenerateRandomAddress();
        }
        
        CAmount amount = GenerateRandomFaucetAmount();
        
        // First address requests tokens
        l2::FaucetResult result1 = faucet.RequestTokens(addr1, amount, stateManager);
        BOOST_CHECK(result1.success);
        
        // Second address should still be able to request (independent cooldown)
        l2::FaucetResult result2 = faucet.RequestTokens(addr2, amount, stateManager);
        BOOST_CHECK_MESSAGE(result2.success,
            "Second address should not be affected by first address's cooldown");
        
        // First address should be in cooldown
        l2::FaucetResult result3 = faucet.RequestTokens(addr1, amount, stateManager);
        BOOST_CHECK(!result3.success);
        
        // Second address should also be in cooldown now
        l2::FaucetResult result4 = faucet.RequestTokens(addr2, amount, stateManager);
        BOOST_CHECK(!result4.success);
    }
}

// ============================================================================
// Property 9: Faucet Network Restriction
// Feature: l2-bridge-security, Property 9: Faucet Network Restriction
// Validates: Requirements 5.1, 5.5
// ============================================================================

/**
 * Property 9: Faucet Network Restriction
 * 
 * For any faucet request on mainnet, the system SHALL reject the request.
 * For any faucet request on regtest or testnet, the system SHALL process
 * the request (subject to rate limits).
 */
BOOST_AUTO_TEST_CASE(property_faucet_network_restriction)
{
    // Feature: l2-bridge-security, Property 9: Faucet Network Restriction
    // Validates: Requirements 5.1, 5.5
    
    SeedInsecureRand(false);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    const size_t NUM_ITERATIONS = 100;
    
    // Test on mainnet - should be disabled
    SelectParams(CBaseChainParams::MAIN);
    
    BOOST_CHECK_MESSAGE(!l2::L2Faucet::IsEnabled(),
        "Faucet should be disabled on mainnet");
    
    for (size_t i = 0; i < NUM_ITERATIONS / 3; ++i) {
        faucet.Clear();
        
        uint160 addr = GenerateRandomAddress();
        CAmount amount = GenerateRandomFaucetAmount();
        
        l2::FaucetResult result = faucet.RequestTokens(addr, amount, stateManager);
        
        BOOST_CHECK_MESSAGE(!result.success,
            "Faucet request should fail on mainnet");
        BOOST_CHECK_MESSAGE(result.error.find("testnet") != std::string::npos ||
                           result.error.find("regtest") != std::string::npos,
            "Error message should mention testnet/regtest");
    }
    
    // Test on testnet - should be enabled
    SelectParams(CBaseChainParams::TESTNET);
    
    BOOST_CHECK_MESSAGE(l2::L2Faucet::IsEnabled(),
        "Faucet should be enabled on testnet");
    
    for (size_t i = 0; i < NUM_ITERATIONS / 3; ++i) {
        faucet.Clear();
        
        uint160 addr = GenerateRandomAddress();
        CAmount amount = GenerateRandomFaucetAmount();
        
        l2::FaucetResult result = faucet.RequestTokens(addr, amount, stateManager);
        
        BOOST_CHECK_MESSAGE(result.success,
            "Faucet request should succeed on testnet: " << result.error);
    }
    
    // Test on regtest - should be enabled
    SelectParams(CBaseChainParams::REGTEST);
    
    BOOST_CHECK_MESSAGE(l2::L2Faucet::IsEnabled(),
        "Faucet should be enabled on regtest");
    
    for (size_t i = 0; i < NUM_ITERATIONS / 3; ++i) {
        faucet.Clear();
        
        uint160 addr = GenerateRandomAddress();
        CAmount amount = GenerateRandomFaucetAmount();
        
        l2::FaucetResult result = faucet.RequestTokens(addr, amount, stateManager);
        
        BOOST_CHECK_MESSAGE(result.success,
            "Faucet request should succeed on regtest: " << result.error);
    }
}

// ============================================================================
// Unit Tests - Edge Cases
// ============================================================================

BOOST_AUTO_TEST_CASE(faucet_invalid_address)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    // Null address should fail
    uint160 nullAddr;
    l2::FaucetResult result = faucet.RequestTokens(nullAddr, 10 * COIN, stateManager);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(result.error.find("Invalid") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(faucet_zero_amount)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    uint160 addr = GenerateRandomAddress();
    
    // Zero amount should fail
    l2::FaucetResult result = faucet.RequestTokens(addr, 0, stateManager);
    
    BOOST_CHECK(!result.success);
    BOOST_CHECK(result.error.find("greater than zero") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(faucet_negative_amount)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    uint160 addr = GenerateRandomAddress();
    
    // Negative amount should fail
    l2::FaucetResult result = faucet.RequestTokens(addr, -10 * COIN, stateManager);
    
    BOOST_CHECK(!result.success);
}

BOOST_AUTO_TEST_CASE(faucet_distribution_logging)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    // Make several distributions to different addresses
    std::vector<uint160> addresses;
    for (int i = 0; i < 5; ++i) {
        uint160 addr = GenerateRandomAddress();
        addresses.push_back(addr);
        
        l2::FaucetResult result = faucet.RequestTokens(addr, 50 * COIN, stateManager);
        BOOST_CHECK(result.success);
    }
    
    // Check distribution log
    std::vector<l2::FaucetDistribution> log = faucet.GetDistributionLog();
    BOOST_CHECK_EQUAL(log.size(), 5);
    
    // Verify all distributions are marked as test tokens
    for (const auto& dist : log) {
        BOOST_CHECK(dist.isTestTokens);
        BOOST_CHECK_EQUAL(dist.amount, 50 * COIN);
    }
    
    // Check total distributed
    BOOST_CHECK_EQUAL(faucet.GetTotalDistributed(), 250 * COIN);
    
    // Check unique recipient count
    BOOST_CHECK_EQUAL(faucet.GetUniqueRecipientCount(), 5);
    
    // Check distribution log for specific address
    std::vector<l2::FaucetDistribution> addrLog = faucet.GetDistributionLog(addresses[0]);
    BOOST_CHECK_EQUAL(addrLog.size(), 1);
    BOOST_CHECK(addrLog[0].recipient == addresses[0]);
}

BOOST_AUTO_TEST_CASE(faucet_exact_max_amount)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    uint160 addr = GenerateRandomAddress();
    
    // Request exactly MAX_FAUCET_AMOUNT
    l2::FaucetResult result = faucet.RequestTokens(addr, l2::MAX_FAUCET_AMOUNT, stateManager);
    
    BOOST_CHECK(result.success);
    BOOST_CHECK_EQUAL(result.amount, l2::MAX_FAUCET_AMOUNT);
}

BOOST_AUTO_TEST_CASE(faucet_over_max_amount)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    uint160 addr = GenerateRandomAddress();
    
    // Request more than MAX_FAUCET_AMOUNT
    l2::FaucetResult result = faucet.RequestTokens(addr, l2::MAX_FAUCET_AMOUNT + 1, stateManager);
    
    BOOST_CHECK(result.success);
    // Should be capped at MAX_FAUCET_AMOUNT
    BOOST_CHECK_EQUAL(result.amount, l2::MAX_FAUCET_AMOUNT);
}

BOOST_AUTO_TEST_CASE(faucet_clear)
{
    SelectParams(CBaseChainParams::REGTEST);
    
    l2::L2TokenConfig config("TestToken", "TEST");
    l2::L2TokenManager tokenManager(1, config);
    l2::L2Faucet faucet(tokenManager);
    l2::L2StateManager stateManager(1);
    
    uint160 addr = GenerateRandomAddress();
    
    // Make a request
    l2::FaucetResult result1 = faucet.RequestTokens(addr, 50 * COIN, stateManager);
    BOOST_CHECK(result1.success);
    
    // Should be in cooldown
    BOOST_CHECK(!faucet.CanRequest(addr, 0));
    
    // Clear faucet state
    faucet.Clear();
    
    // Should be able to request again
    BOOST_CHECK(faucet.CanRequest(addr, 0));
    BOOST_CHECK_EQUAL(faucet.GetTotalDistributed(), 0);
    BOOST_CHECK_EQUAL(faucet.GetDistributionLog().size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
