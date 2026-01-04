// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/sustainable_gas.h>
#include <cvm/trust_context.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_sustainable_gas_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(base_gas_price)
{
    cvm::SustainableGasSystem gas_system;
    
    // Get base gas price from parameters
    const cvm::GasParameters& params = gas_system.GetGasParameters();
    uint64_t base_price = params.baseGasPrice;
    
    // Should be 100x lower than Ethereum (0.01 gwei = 10000000 wei)
    BOOST_CHECK_EQUAL(base_price, 10000000);
}

BOOST_AUTO_TEST_CASE(reputation_multiplier)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test reputation multipliers (linear interpolation per design doc)
    // Reputation 0 = 1.0x (full cost)
    // Reputation 50 = 0.75x (25% discount)
    // Reputation 100 = 0.5x (50% discount)
    BOOST_CHECK_CLOSE(gas_system.CalculateReputationMultiplier(0), 1.0, 0.01);
    BOOST_CHECK_CLOSE(gas_system.CalculateReputationMultiplier(50), 0.75, 0.01);
    BOOST_CHECK_CLOSE(gas_system.CalculateReputationMultiplier(100), 0.5, 0.01);
    
    // Intermediate values should follow linear interpolation
    double mult25 = gas_system.CalculateReputationMultiplier(25);
    BOOST_CHECK_CLOSE(mult25, 0.875, 0.01);  // 1.0 - (25/200) = 0.875
    
    double mult75 = gas_system.CalculateReputationMultiplier(75);
    BOOST_CHECK_CLOSE(mult75, 0.625, 0.01);  // 1.0 - (75/200) = 0.625
    
    // All multipliers should be in valid range [0.5, 1.0]
    for (uint8_t rep = 0; rep <= 100; rep += 10) {
        double mult = gas_system.CalculateReputationMultiplier(rep);
        BOOST_CHECK_GE(mult, 0.5);
        BOOST_CHECK_LE(mult, 1.0);
    }
}

BOOST_AUTO_TEST_CASE(free_gas_eligibility)
{
    cvm::SustainableGasSystem gas_system;
    
    // Reputation < 80: not eligible
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(0), false);
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(50), false);
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(79), false);
    
    // Reputation >= 80: eligible
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(80), true);
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(90), true);
    BOOST_CHECK_EQUAL(gas_system.IsEligibleForFreeGas(100), true);
}

BOOST_AUTO_TEST_CASE(gas_allowance_calculation)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test gas allowance for different reputation levels
    // Reputation < 80: 0 allowance
    BOOST_CHECK_EQUAL(gas_system.GetFreeGasAllowance(0), 0);
    BOOST_CHECK_EQUAL(gas_system.GetFreeGasAllowance(79), 0);
    
    // Reputation >= 80: should have some allowance
    uint64_t allowance80 = gas_system.GetFreeGasAllowance(80);
    uint64_t allowance90 = gas_system.GetFreeGasAllowance(90);
    uint64_t allowance100 = gas_system.GetFreeGasAllowance(100);
    
    BOOST_CHECK_GT(allowance80, 0);
    BOOST_CHECK_GE(allowance90, allowance80);
    BOOST_CHECK_GE(allowance100, allowance90);
}

BOOST_AUTO_TEST_CASE(predictable_pricing)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test that pricing is predictable (max 2x variation)
    const cvm::GasParameters& params = gas_system.GetGasParameters();
    uint64_t base_price = params.baseGasPrice;
    
    // Even with network load, price should not exceed 2x base
    uint64_t max_price = base_price * params.maxPriceVariation;
    
    // Test with various network loads
    for (int load = 0; load <= 100; load += 10) {
        uint64_t price = gas_system.GetPredictableGasPrice(50, load);
        BOOST_CHECK_LE(price, max_price);
        BOOST_CHECK_GE(price, base_price / 2);  // With reputation discount
    }
}

BOOST_AUTO_TEST_CASE(anti_congestion_pricing)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test anti-congestion pricing
    // Low network load: base price (with reputation)
    uint64_t price_low = gas_system.GetPredictableGasPrice(50, 10);
    
    // High network load: higher price (but max 2x)
    uint64_t price_high = gas_system.GetPredictableGasPrice(50, 90);
    
    // High load should result in higher or equal price
    BOOST_CHECK_GE(price_high, price_low);
    
    // But still within max variation
    const cvm::GasParameters& params = gas_system.GetGasParameters();
    BOOST_CHECK_LE(price_high, params.baseGasPrice * params.maxPriceVariation);
}

BOOST_AUTO_TEST_CASE(reputation_range_validation)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test that functions handle reputation range correctly
    for (uint32_t rep = 0; rep <= 100; rep += 10) {
        // Should not throw
        double multiplier = gas_system.CalculateReputationMultiplier(rep);
        BOOST_CHECK_GE(multiplier, 0.5);
        BOOST_CHECK_LE(multiplier, 1.0);
        
        bool eligible = gas_system.IsEligibleForFreeGas(rep);
        BOOST_CHECK(eligible == true || eligible == false);
        
        uint64_t allowance = gas_system.GetFreeGasAllowance(rep);
        BOOST_CHECK_GE(allowance, 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
