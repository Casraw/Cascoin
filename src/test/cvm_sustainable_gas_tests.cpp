// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/sustainable_gas.h>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_sustainable_gas_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(base_gas_price)
{
    cvm::SustainableGasSystem gas_system;
    
    // Get base gas price
    uint64_t base_price = gas_system.GetBaseGasPrice();
    
    // Should be 100x lower than Ethereum (20 gwei)
    // Cascoin: 0.2 gwei = 200000000 wei
    BOOST_CHECK_EQUAL(base_price, 200000000);
}

BOOST_AUTO_TEST_CASE(reputation_multiplier)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test reputation multipliers
    // Reputation 0-49: 1.0x (no discount)
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(0), 1.0);
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(49), 1.0);
    
    // Reputation 50-79: 0.75x (25% discount)
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(50), 0.75);
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(79), 0.75);
    
    // Reputation 80-100: 0.5x (50% discount)
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(80), 0.5);
    BOOST_CHECK_EQUAL(gas_system.CalculateReputationMultiplier(100), 0.5);
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
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(0), 0);
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(79), 0);
    
    // Reputation 80-89: 1M gas
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(80), 1000000);
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(89), 1000000);
    
    // Reputation 90-100: 5M gas
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(90), 5000000);
    BOOST_CHECK_EQUAL(gas_system.CalculateGasAllowance(100), 5000000);
}

BOOST_AUTO_TEST_CASE(gas_cost_calculation)
{
    cvm::SustainableGasSystem gas_system;
    
    uint64_t gas_used = 100000;
    
    // Test gas cost for different reputation levels
    // Reputation 0: full price
    uint64_t cost_rep0 = gas_system.CalculateGasCost(gas_used, 0);
    BOOST_CHECK_EQUAL(cost_rep0, gas_used * 200000000);
    
    // Reputation 50: 25% discount
    uint64_t cost_rep50 = gas_system.CalculateGasCost(gas_used, 50);
    BOOST_CHECK_EQUAL(cost_rep50, gas_used * 200000000 * 0.75);
    
    // Reputation 80: 50% discount
    uint64_t cost_rep80 = gas_system.CalculateGasCost(gas_used, 80);
    BOOST_CHECK_EQUAL(cost_rep80, gas_used * 200000000 * 0.5);
}

BOOST_AUTO_TEST_CASE(predictable_pricing)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test that pricing is predictable (max 2x variation)
    uint64_t base_price = gas_system.GetBaseGasPrice();
    
    // Even with network load, price should not exceed 2x base
    uint64_t max_price = base_price * 2;
    
    // Test with various network loads
    for (int load = 0; load <= 100; load += 10) {
        uint64_t price = gas_system.CalculateDynamicGasPrice(load);
        BOOST_CHECK_LE(price, max_price);
        BOOST_CHECK_GE(price, base_price);
    }
}

BOOST_AUTO_TEST_CASE(anti_congestion_pricing)
{
    cvm::SustainableGasSystem gas_system;
    
    // Test anti-congestion pricing
    // Low network load: base price
    uint64_t price_low = gas_system.CalculateDynamicGasPrice(10);
    BOOST_CHECK_EQUAL(price_low, 200000000);
    
    // High network load: higher price (but max 2x)
    uint64_t price_high = gas_system.CalculateDynamicGasPrice(90);
    BOOST_CHECK_GT(price_high, price_low);
    BOOST_CHECK_LE(price_high, 400000000);  // Max 2x
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
        
        uint64_t allowance = gas_system.CalculateGasAllowance(rep);
        BOOST_CHECK_GE(allowance, 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
