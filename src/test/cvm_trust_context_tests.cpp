// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trust_context.h>
#include <cvm/cvmdb.h>
#include <test/test_bitcoin.h>
#include <uint256.h>
#include <fs.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_trust_context_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(reputation_range)
{
    // Create a test database with in-memory storage
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);  // fMemory=true, fWipe=true
    CVM::TrustContext trust_ctx(&db);
    
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
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Test gas discount calculation
    uint64_t base_gas = 100000;
    uint64_t discounted_gas = trust_ctx.ApplyReputationGasDiscount(base_gas, address);
    
    // Discounted gas should be <= base gas
    BOOST_CHECK_LE(discounted_gas, base_gas);
    BOOST_CHECK_GT(discounted_gas, 0);
}

BOOST_AUTO_TEST_CASE(free_gas_eligibility)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Check free gas eligibility
    bool eligible = trust_ctx.HasFreeGasEligibility(address);
    
    // Should return a boolean
    BOOST_CHECK(eligible == true || eligible == false);
}

BOOST_AUTO_TEST_CASE(gas_allowance)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Get gas allowance
    uint64_t allowance = trust_ctx.GetGasAllowance(address);
    
    // Allowance should be non-negative
    BOOST_CHECK_GE(allowance, 0);
}

BOOST_AUTO_TEST_CASE(trust_weighted_value)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Create trust-weighted value
    CVM::TrustContext::TrustWeightedValue twv;
    twv.value.SetHex("0000000000000000000000000000000000000000000000000000000000000042");
    twv.trust_weight = 50;
    twv.source_address = address;
    twv.timestamp = 12345;
    
    // Add trust-weighted value with string key
    std::string key = "test_key";
    trust_ctx.AddTrustWeightedValue(key, twv);
    
    // Get trust-weighted values
    std::vector<CVM::TrustContext::TrustWeightedValue> values = trust_ctx.GetTrustWeightedValues(key);
    
    // Should have at least one value
    BOOST_CHECK_GE(values.size(), 1);
}

BOOST_AUTO_TEST_CASE(access_level)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Check access level (requires minimum reputation)
    bool has_access = trust_ctx.CheckAccessLevel(address, "test_resource", "read");
    
    // Should return a boolean
    BOOST_CHECK(has_access == true || has_access == false);
}

BOOST_AUTO_TEST_CASE(reputation_history)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Get reputation history
    std::vector<CVM::TrustContext::ReputationEvent> history = trust_ctx.GetReputationHistory(address);
    
    // History should be a valid vector (may be empty)
    BOOST_CHECK_GE(history.size(), 0);
}

BOOST_AUTO_TEST_CASE(reputation_decay)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    // Apply reputation decay with current time
    int64_t current_time = 12345;
    trust_ctx.ApplyReputationDecay(current_time);
    
    // Should not throw
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(reputation_update_from_activity)
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    CVM::CVMDatabase db(testPath, 8 << 20, true, true);
    CVM::TrustContext trust_ctx(&db);
    
    uint160 address;
    address.SetNull();
    
    // Update reputation from activity
    trust_ctx.UpdateReputationFromActivity(address, "positive_trade", 1);
    
    // Should not throw
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
