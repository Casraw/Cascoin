// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_rate_limiter_tests.cpp
 * @brief Property-based tests for L2 Rate Limiter
 * 
 * **Feature: cascoin-l2-solution, Property 14: Rate Limit Enforcement**
 * **Validates: Requirements 26.2, 26.3**
 * 
 * Property 14: Rate Limit Enforcement
 * *For any* address, the number of transactions per block SHALL not exceed
 * the rate limit determined by their reputation score.
 */

#include <l2/rate_limiter.h>
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

/**
 * Helper function to generate a random address
 */
static uint160 RandomAddress()
{
    uint160 addr;
    for (int i = 0; i < 5; ++i) {
        uint32_t val = TestRand32();
        memcpy(addr.begin() + i * 4, &val, 4);
    }
    return addr;
}

/**
 * Helper function to generate a random reputation score
 */
static uint32_t RandomReputationScore()
{
    return TestRand32() % 101;  // 0-100
}

/**
 * Helper function to generate a random gas price
 */
static CAmount RandomGasPrice()
{
    // Generate gas price between MIN_GAS_PRICE and 10x base
    return l2::MIN_GAS_PRICE + (TestRand64() % (l2::BASE_GAS_PRICE * 10));
}

/**
 * Helper function to generate a random gas limit
 */
static uint64_t RandomGasLimit()
{
    // Generate gas limit between 21000 and 1M
    return 21000 + (TestRand64() % 1000000);
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_rate_limiter_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(empty_rate_limiter)
{
    l2::RateLimiter limiter;
    
    BOOST_CHECK_EQUAL(limiter.GetTrackedAddressCount(), 0u);
    BOOST_CHECK_EQUAL(limiter.GetBlockGasLimit(), l2::DEFAULT_BLOCK_GAS_LIMIT);
    BOOST_CHECK_EQUAL(limiter.GetMinGasPrice(), l2::MIN_GAS_PRICE);
    BOOST_CHECK(limiter.GetEffectiveGasPrice() >= l2::MIN_GAS_PRICE);
}

BOOST_AUTO_TEST_CASE(basic_rate_limit_check)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    CAmount gasPrice = l2::BASE_GAS_PRICE;
    uint64_t gasLimit = 21000;
    uint64_t currentBlock = 1000;
    
    // First transaction should be allowed
    l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
    BOOST_CHECK(result.allowed);
    BOOST_CHECK_EQUAL(result.currentRateLimit, l2::DEFAULT_NEW_ADDRESS_TX_LIMIT);
    BOOST_CHECK_EQUAL(result.txUsedInBlock, 0u);
}

BOOST_AUTO_TEST_CASE(gas_price_too_low)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    CAmount gasPrice = 0;  // Zero gas price
    uint64_t gasLimit = 21000;
    uint64_t currentBlock = 1000;
    
    l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
    BOOST_CHECK(!result.allowed);
    BOOST_CHECK(result.reason.find("Gas price too low") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(record_transaction)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    uint64_t currentBlock = 1000;
    
    // Record a transaction
    limiter.RecordTransaction(addr, 21000, currentBlock);
    
    BOOST_CHECK_EQUAL(limiter.GetTrackedAddressCount(), 1u);
    BOOST_CHECK_EQUAL(limiter.GetTxCountInBlock(addr, currentBlock), 1u);
    
    // Record another transaction
    limiter.RecordTransaction(addr, 50000, currentBlock);
    BOOST_CHECK_EQUAL(limiter.GetTxCountInBlock(addr, currentBlock), 2u);
}

BOOST_AUTO_TEST_CASE(rate_limit_exceeded)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    CAmount gasPrice = l2::BASE_GAS_PRICE;
    uint64_t gasLimit = 21000;
    uint64_t currentBlock = 1000;
    
    // Record transactions up to the limit
    uint32_t limit = l2::DEFAULT_NEW_ADDRESS_TX_LIMIT;
    for (uint32_t i = 0; i < limit; ++i) {
        limiter.RecordTransaction(addr, gasLimit, currentBlock);
    }
    
    // Next transaction should be denied
    l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
    BOOST_CHECK(!result.allowed);
    BOOST_CHECK(result.reason.find("Rate limit exceeded") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(reputation_increases_rate_limit)
{
    l2::RateLimiter limiter;
    
    uint160 lowRepAddr = RandomAddress();
    uint160 highRepAddr = RandomAddress();
    
    // Set reputation scores
    limiter.UpdateReputation(lowRepAddr, 50);   // Below threshold
    limiter.UpdateReputation(highRepAddr, 80);  // Above threshold
    
    // Check rate limits
    uint32_t lowRepLimit = limiter.GetRateLimitForAddress(lowRepAddr);
    uint32_t highRepLimit = limiter.GetRateLimitForAddress(highRepAddr);
    
    BOOST_CHECK(highRepLimit > lowRepLimit);
    BOOST_CHECK_EQUAL(lowRepLimit, l2::DEFAULT_NEW_ADDRESS_TX_LIMIT * 2);  // 200 for score 50
    BOOST_CHECK_EQUAL(highRepLimit, l2::HIGH_REPUTATION_TX_LIMIT);  // 500 for score 80
}

BOOST_AUTO_TEST_CASE(very_high_reputation_rate_limit)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    limiter.UpdateReputation(addr, 95);  // Very high reputation
    
    uint32_t limit = limiter.GetRateLimitForAddress(addr);
    BOOST_CHECK_EQUAL(limit, l2::HIGH_REPUTATION_TX_LIMIT * 2);  // 1000 for score >= 90
}

BOOST_AUTO_TEST_CASE(block_gas_limit_enforcement)
{
    l2::RateLimiter limiter(1000000);  // 1M gas limit
    
    uint160 addr = RandomAddress();
    CAmount gasPrice = l2::BASE_GAS_PRICE;
    uint64_t currentBlock = 1000;
    
    // Record transactions that use most of the gas
    limiter.RecordTransaction(addr, 900000, currentBlock);
    
    // Transaction that would exceed block gas limit
    l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, 200000, currentBlock);
    BOOST_CHECK(!result.allowed);
    BOOST_CHECK(result.reason.find("Block gas limit exceeded") != std::string::npos);
    
    // Smaller transaction should still be allowed
    result = limiter.CheckRateLimit(addr, gasPrice, 50000, currentBlock);
    BOOST_CHECK(result.allowed);
}

BOOST_AUTO_TEST_CASE(manual_rate_limit)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    uint64_t currentBlock = 1000;
    
    // Initialize the current block first
    limiter.OnNewBlock(currentBlock);
    
    // Manually rate-limit the address
    limiter.RateLimitAddress(addr, 5);  // 5 blocks
    
    BOOST_CHECK(limiter.IsRateLimited(addr, currentBlock));
    BOOST_CHECK(limiter.IsRateLimited(addr, currentBlock + 4));
    BOOST_CHECK(!limiter.IsRateLimited(addr, currentBlock + 5));
    
    // Remove rate limit
    limiter.RemoveRateLimit(addr);
    BOOST_CHECK(!limiter.IsRateLimited(addr, currentBlock));
}

BOOST_AUTO_TEST_CASE(new_block_resets_per_block_counts)
{
    l2::RateLimiter limiter;
    
    uint160 addr = RandomAddress();
    uint64_t block1 = 1000;
    uint64_t block2 = 1001;
    
    // Record transactions in block 1
    limiter.RecordTransaction(addr, 21000, block1);
    limiter.RecordTransaction(addr, 21000, block1);
    BOOST_CHECK_EQUAL(limiter.GetTxCountInBlock(addr, block1), 2u);
    
    // Move to block 2
    limiter.OnNewBlock(block2);
    
    // Block 2 should have 0 transactions
    BOOST_CHECK_EQUAL(limiter.GetTxCountInBlock(addr, block2), 0u);
}

BOOST_AUTO_TEST_CASE(adaptive_gas_pricing)
{
    l2::RateLimiter limiter(1000000);  // 1M gas limit
    
    // Simulate high utilization blocks
    for (uint64_t block = 1; block <= 10; ++block) {
        limiter.UpdateGasPricing(block, 800000);  // 80% utilization
    }
    
    l2::GasPricingInfo info = limiter.GetGasPricingInfo();
    
    // Price multiplier should increase due to high utilization
    BOOST_CHECK(info.priceMultiplier >= 100);
    BOOST_CHECK(info.averageUtilization > l2::TARGET_BLOCK_UTILIZATION_PERCENT);
}

BOOST_AUTO_TEST_CASE(gas_pricing_info)
{
    l2::RateLimiter limiter;
    
    l2::GasPricingInfo info = limiter.GetGasPricingInfo();
    
    BOOST_CHECK_EQUAL(info.baseFee, l2::BASE_GAS_PRICE);
    BOOST_CHECK_EQUAL(info.blockGasLimit, l2::DEFAULT_BLOCK_GAS_LIMIT);
    BOOST_CHECK_EQUAL(info.priceMultiplier, 100u);  // 1x
    BOOST_CHECK(info.GetEffectiveGasPrice() >= l2::MIN_GAS_PRICE);
}

BOOST_AUTO_TEST_CASE(clear_rate_limiter)
{
    l2::RateLimiter limiter;
    
    // Add some addresses
    for (int i = 0; i < 5; ++i) {
        uint160 addr = RandomAddress();
        limiter.RecordTransaction(addr, 21000, 1000);
    }
    
    BOOST_CHECK_EQUAL(limiter.GetTrackedAddressCount(), 5u);
    
    limiter.Clear();
    
    BOOST_CHECK_EQUAL(limiter.GetTrackedAddressCount(), 0u);
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 14: Rate Limit Enforcement**
 * 
 * *For any* address, the number of transactions per block SHALL not exceed
 * the rate limit determined by their reputation score.
 * 
 * **Validates: Requirements 26.2, 26.3**
 */
BOOST_AUTO_TEST_CASE(property_rate_limit_enforcement)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::RateLimiter limiter;
        
        uint160 addr = RandomAddress();
        uint32_t reputation = RandomReputationScore();
        uint64_t currentBlock = 1000 + (TestRand32() % 1000);
        
        // Set reputation
        limiter.UpdateReputation(addr, reputation);
        
        // Get the rate limit for this reputation
        uint32_t expectedLimit = l2::RateLimiter::CalculateRateLimit(reputation);
        uint32_t actualLimit = limiter.GetRateLimitForAddress(addr);
        
        BOOST_CHECK_MESSAGE(actualLimit == expectedLimit,
            "Rate limit mismatch for iteration " << iteration <<
            " (reputation=" << reputation << ", expected=" << expectedLimit <<
            ", actual=" << actualLimit << ")");
        
        // Try to submit transactions up to and beyond the limit
        CAmount gasPrice = l2::BASE_GAS_PRICE;
        uint64_t gasLimit = 21000;
        
        uint32_t allowedCount = 0;
        uint32_t deniedCount = 0;
        
        // Try to submit limit + 10 transactions
        for (uint32_t i = 0; i < expectedLimit + 10; ++i) {
            l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
            
            if (result.allowed) {
                allowedCount++;
                limiter.RecordTransaction(addr, gasLimit, currentBlock);
            } else {
                deniedCount++;
            }
        }
        
        // Verify that exactly 'expectedLimit' transactions were allowed
        BOOST_CHECK_MESSAGE(allowedCount == expectedLimit,
            "Allowed count mismatch for iteration " << iteration <<
            " (expected=" << expectedLimit << ", actual=" << allowedCount << ")");
        
        // Verify that transactions beyond the limit were denied
        BOOST_CHECK_MESSAGE(deniedCount == 10,
            "Denied count mismatch for iteration " << iteration <<
            " (expected=10, actual=" << deniedCount << ")");
    }
}

/**
 * **Property: Rate Limit Monotonicity with Reputation**
 * 
 * *For any* two reputation scores where score1 > score2, the rate limit
 * for score1 SHALL be >= the rate limit for score2.
 * 
 * **Validates: Requirements 26.3**
 */
BOOST_AUTO_TEST_CASE(property_rate_limit_monotonicity)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        uint32_t score1 = RandomReputationScore();
        uint32_t score2 = RandomReputationScore();
        
        if (score1 < score2) {
            std::swap(score1, score2);
        }
        
        uint32_t limit1 = l2::RateLimiter::CalculateRateLimit(score1);
        uint32_t limit2 = l2::RateLimiter::CalculateRateLimit(score2);
        
        BOOST_CHECK_MESSAGE(limit1 >= limit2,
            "Rate limit not monotonic for iteration " << iteration <<
            " (score1=" << score1 << ", limit1=" << limit1 <<
            ", score2=" << score2 << ", limit2=" << limit2 << ")");
    }
}

/**
 * **Property: Gas Price Acceptance Consistency**
 * 
 * *For any* gas price >= effective gas price, the transaction SHALL be
 * accepted (assuming other conditions are met).
 * 
 * **Validates: Requirements 26.5, 26.6**
 */
BOOST_AUTO_TEST_CASE(property_gas_price_acceptance)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::RateLimiter limiter;
        
        CAmount effectivePrice = limiter.GetEffectiveGasPrice();
        
        // Gas price >= effective should be acceptable
        CAmount acceptablePrice = effectivePrice + (TestRand64() % 1000);
        BOOST_CHECK_MESSAGE(limiter.IsGasPriceAcceptable(acceptablePrice),
            "Acceptable price rejected for iteration " << iteration);
        
        // Gas price < effective should be rejected (if effective > MIN)
        if (effectivePrice > l2::MIN_GAS_PRICE) {
            CAmount lowPrice = effectivePrice - 1;
            BOOST_CHECK_MESSAGE(!limiter.IsGasPriceAcceptable(lowPrice),
                "Low price accepted for iteration " << iteration);
        }
    }
}

/**
 * **Property: Block Gas Limit Enforcement**
 * 
 * *For any* block, the total gas used SHALL not exceed the block gas limit.
 * 
 * **Validates: Requirements 26.1**
 */
BOOST_AUTO_TEST_CASE(property_block_gas_limit)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        uint64_t blockGasLimit = 1000000 + (TestRand64() % 10000000);
        l2::RateLimiter limiter(blockGasLimit);
        
        uint160 addr = RandomAddress();
        limiter.UpdateReputation(addr, 100);  // Max reputation for high rate limit
        
        uint64_t currentBlock = 1000;
        CAmount gasPrice = l2::BASE_GAS_PRICE;
        
        uint64_t totalGasUsed = 0;
        
        // Try to fill the block
        while (true) {
            uint64_t gasLimit = 21000 + (TestRand64() % 100000);
            
            l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
            
            if (!result.allowed) {
                // Either rate limit or gas limit exceeded
                break;
            }
            
            limiter.RecordTransaction(addr, gasLimit, currentBlock);
            totalGasUsed += gasLimit;
            
            // Safety check to prevent infinite loop
            if (totalGasUsed > blockGasLimit * 2) {
                BOOST_FAIL("Total gas exceeded 2x block limit");
            }
        }
        
        // Verify total gas used is within limit
        BOOST_CHECK_MESSAGE(totalGasUsed <= blockGasLimit,
            "Block gas limit exceeded for iteration " << iteration <<
            " (limit=" << blockGasLimit << ", used=" << totalGasUsed << ")");
    }
}

/**
 * **Property: Rate Limit Cooldown**
 * 
 * *For any* address that exceeds the rate limit, the address SHALL be
 * rate-limited for the cooldown period.
 * 
 * **Validates: Requirements 26.2**
 */
BOOST_AUTO_TEST_CASE(property_rate_limit_cooldown)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::RateLimiter limiter;
        
        uint160 addr = RandomAddress();
        uint64_t currentBlock = 1000;
        CAmount gasPrice = l2::BASE_GAS_PRICE;
        uint64_t gasLimit = 21000;
        
        // Get rate limit
        uint32_t limit = limiter.GetRateLimitForAddress(addr);
        
        // Fill up to the limit
        for (uint32_t i = 0; i < limit; ++i) {
            limiter.RecordTransaction(addr, gasLimit, currentBlock);
        }
        
        // Next transaction should trigger rate limit
        l2::RateLimitCheckResult result = limiter.CheckRateLimit(addr, gasPrice, gasLimit, currentBlock);
        BOOST_CHECK_MESSAGE(!result.allowed,
            "Transaction allowed after limit exceeded for iteration " << iteration);
        
        // Address should be rate-limited
        BOOST_CHECK_MESSAGE(limiter.IsRateLimited(addr, currentBlock),
            "Address not rate-limited after exceeding limit for iteration " << iteration);
        
        // Should still be rate-limited during cooldown
        BOOST_CHECK_MESSAGE(limiter.IsRateLimited(addr, currentBlock + l2::RATE_LIMIT_COOLDOWN_BLOCKS - 1),
            "Rate limit expired too early for iteration " << iteration);
        
        // Should not be rate-limited after cooldown
        BOOST_CHECK_MESSAGE(!limiter.IsRateLimited(addr, currentBlock + l2::RATE_LIMIT_COOLDOWN_BLOCKS),
            "Rate limit not expired after cooldown for iteration " << iteration);
    }
}

/**
 * **Property: Price Multiplier Bounds**
 * 
 * *For any* utilization percentage, the price multiplier SHALL be within
 * valid bounds [100, MAX_GAS_PRICE_MULTIPLIER * 100].
 * 
 * **Validates: Requirements 26.4, 26.5**
 */
BOOST_AUTO_TEST_CASE(property_price_multiplier_bounds)
{
    // Test all utilization percentages
    for (uint32_t utilization = 0; utilization <= 100; ++utilization) {
        uint32_t multiplier = l2::RateLimiter::CalculatePriceMultiplier(utilization);
        
        BOOST_CHECK_MESSAGE(multiplier >= 100,
            "Multiplier below minimum for utilization " << utilization);
        BOOST_CHECK_MESSAGE(multiplier <= 100 * l2::MAX_GAS_PRICE_MULTIPLIER,
            "Multiplier above maximum for utilization " << utilization);
    }
}

/**
 * **Property: Transaction Recording Consistency**
 * 
 * *For any* sequence of recorded transactions, the transaction count
 * SHALL accurately reflect the number of recorded transactions.
 * 
 * **Validates: Requirements 26.2**
 */
BOOST_AUTO_TEST_CASE(property_transaction_recording)
{
    // Run 50 iterations
    for (int iteration = 0; iteration < 50; ++iteration) {
        l2::RateLimiter limiter;
        
        uint160 addr = RandomAddress();
        uint64_t currentBlock = 1000;
        
        uint32_t numTx = TestRand32() % 50;
        
        for (uint32_t i = 0; i < numTx; ++i) {
            uint64_t gasUsed = 21000 + (TestRand64() % 100000);
            limiter.RecordTransaction(addr, gasUsed, currentBlock);
        }
        
        uint32_t recordedCount = limiter.GetTxCountInBlock(addr, currentBlock);
        
        BOOST_CHECK_MESSAGE(recordedCount == numTx,
            "Transaction count mismatch for iteration " << iteration <<
            " (expected=" << numTx << ", actual=" << recordedCount << ")");
    }
}

/**
 * **Property: Multiple Addresses Independence**
 * 
 * *For any* two different addresses, their rate limits SHALL be
 * independent of each other.
 * 
 * **Validates: Requirements 26.2**
 */
BOOST_AUTO_TEST_CASE(property_address_independence)
{
    // Run 20 iterations
    for (int iteration = 0; iteration < 20; ++iteration) {
        l2::RateLimiter limiter;
        
        uint160 addr1 = RandomAddress();
        uint160 addr2 = RandomAddress();
        
        // Ensure different addresses
        while (addr1 == addr2) {
            addr2 = RandomAddress();
        }
        
        uint64_t currentBlock = 1000;
        CAmount gasPrice = l2::BASE_GAS_PRICE;
        uint64_t gasLimit = 21000;
        
        // Fill addr1's rate limit
        uint32_t limit1 = limiter.GetRateLimitForAddress(addr1);
        for (uint32_t i = 0; i < limit1; ++i) {
            limiter.RecordTransaction(addr1, gasLimit, currentBlock);
        }
        
        // addr1 should be rate-limited
        l2::RateLimitCheckResult result1 = limiter.CheckRateLimit(addr1, gasPrice, gasLimit, currentBlock);
        BOOST_CHECK_MESSAGE(!result1.allowed,
            "addr1 not rate-limited for iteration " << iteration);
        
        // addr2 should still be able to transact
        l2::RateLimitCheckResult result2 = limiter.CheckRateLimit(addr2, gasPrice, gasLimit, currentBlock);
        BOOST_CHECK_MESSAGE(result2.allowed,
            "addr2 incorrectly rate-limited for iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
