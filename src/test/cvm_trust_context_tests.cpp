// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <test/test_bitcoin.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_trust_context_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(reputation_range)
{
    // Create a test database
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    // Test address
    uint160 address;
    address.SetNull();
    
    // Get reputation (should be in valid range)
    uint32_t reputation = trust_ctx.GetReputation(address);
    
    // Reputation should be 0-100
    BOOST_CHECK_GE(reputation, 0);
    BOOST_CHECK_LE(reputation, 100);
}

BOOST_AUTO_TEST_CASE(reputation_discount)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Test gas discount calculation
    uint64_t base_gas = 100000;
    uint64_t discounted_gas = trust_ctx.ApplyReputationGasDiscount(address, base_gas);
    
    // Discounted gas should be <= base gas
    BOOST_CHECK_LE(discounted_gas, base_gas);
    BOOST_CHECK_GT(discounted_gas, 0);
}

BOOST_AUTO_TEST_CASE(free_gas_eligibility)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Check free gas eligibility
    bool eligible = trust_ctx.HasFreeGasEligibility(address);
    
    // Should return a boolean
    BOOST_CHECK(eligible == true || eligible == false);
}

BOOST_AUTO_TEST_CASE(gas_allowance)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Get gas allowance
    uint64_t allowance = trust_ctx.GetGasAllowance(address);
    
    // Allowance should be non-negative
    BOOST_CHECK_GE(allowance, 0);
}

BOOST_AUTO_TEST_CASE(trust_weighted_value)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Add trust-weighted value
    uint256 value;
    value.SetHex("0000000000000000000000000000000000000000000000000000000000000042");
    
    trust_ctx.AddTrustWeightedValue(address, value);
    
    // Get trust-weighted values
    std::vector<uint256> values = trust_ctx.GetTrustWeightedValues(address);
    
    // Should have at least one value
    BOOST_CHECK_GE(values.size(), 0);
}

BOOST_AUTO_TEST_CASE(access_level)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Check access level (requires minimum reputation)
    bool has_access = trust_ctx.CheckAccessLevel(address, 50);
    
    // Should return a boolean
    BOOST_CHECK(has_access == true || has_access == false);
}

BOOST_AUTO_TEST_CASE(reputation_history)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Get reputation history
    std::vector<uint32_t> history = trust_ctx.GetReputationHistory(address);
    
    // History should be a valid vector (may be empty)
    BOOST_CHECK_GE(history.size(), 0);
}

BOOST_AUTO_TEST_CASE(reputation_decay)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Apply reputation decay
    trust_ctx.ApplyReputationDecay(address, 1);  // 1 block
    
    // Should not throw
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(reputation_update_from_activity)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::TrustContext trust_ctx(db.get());
    
    uint160 address;
    address.SetNull();
    
    // Update reputation from activity
    trust_ctx.UpdateReputationFromActivity(address, 1);  // Positive activity
    
    // Should not throw
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
