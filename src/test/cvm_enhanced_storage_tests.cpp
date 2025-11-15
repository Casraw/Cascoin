// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/enhanced_storage.h>
#include <cvm/cvmdb.h>
#include <test/test_bitcoin.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_enhanced_storage_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(storage_read_write)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    uint160 contract_addr;
    contract_addr.SetNull();
    
    uint256 key;
    key.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
    
    uint256 value;
    value.SetHex("0000000000000000000000000000000000000000000000000000000000000042");
    
    // Write value
    storage.SetStorage(contract_addr, key, value);
    
    // Read value
    uint256 read_value = storage.GetStorage(contract_addr, key);
    
    // Values should match
    BOOST_CHECK_EQUAL(read_value.GetHex(), value.GetHex());
}

BOOST_AUTO_TEST_CASE(storage_32byte_keys)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    uint160 contract_addr;
    contract_addr.SetNull();
    
    // Test with various 32-byte keys
    for (int i = 0; i < 10; i++) {
        uint256 key;
        key.SetHex(strprintf("%064x", i));
        
        uint256 value;
        value.SetHex(strprintf("%064x", i * 2));
        
        storage.SetStorage(contract_addr, key, value);
        uint256 read_value = storage.GetStorage(contract_addr, key);
        
        BOOST_CHECK_EQUAL(read_value.GetHex(), value.GetHex());
    }
}

BOOST_AUTO_TEST_CASE(storage_multiple_contracts)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    // Create two different contract addresses
    uint160 contract1;
    contract1.SetHex("0000000000000000000000000000000000000001");
    
    uint160 contract2;
    contract2.SetHex("0000000000000000000000000000000000000002");
    
    uint256 key;
    key.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
    
    uint256 value1;
    value1.SetHex("0000000000000000000000000000000000000000000000000000000000000042");
    
    uint256 value2;
    value2.SetHex("0000000000000000000000000000000000000000000000000000000000000043");
    
    // Write different values for same key in different contracts
    storage.SetStorage(contract1, key, value1);
    storage.SetStorage(contract2, key, value2);
    
    // Read values
    uint256 read1 = storage.GetStorage(contract1, key);
    uint256 read2 = storage.GetStorage(contract2, key);
    
    // Values should be different
    BOOST_CHECK_EQUAL(read1.GetHex(), value1.GetHex());
    BOOST_CHECK_EQUAL(read2.GetHex(), value2.GetHex());
    BOOST_CHECK(read1 != read2);
}

BOOST_AUTO_TEST_CASE(storage_zero_value)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    uint160 contract_addr;
    contract_addr.SetNull();
    
    uint256 key;
    key.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
    
    uint256 zero_value;
    zero_value.SetNull();
    
    // Write zero value (should delete storage)
    storage.SetStorage(contract_addr, key, zero_value);
    
    // Read value (should be zero)
    uint256 read_value = storage.GetStorage(contract_addr, key);
    
    BOOST_CHECK(read_value.IsNull());
}

BOOST_AUTO_TEST_CASE(storage_overwrite)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    uint160 contract_addr;
    contract_addr.SetNull();
    
    uint256 key;
    key.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
    
    uint256 value1;
    value1.SetHex("0000000000000000000000000000000000000000000000000000000000000042");
    
    uint256 value2;
    value2.SetHex("0000000000000000000000000000000000000000000000000000000000000043");
    
    // Write first value
    storage.SetStorage(contract_addr, key, value1);
    
    // Overwrite with second value
    storage.SetStorage(contract_addr, key, value2);
    
    // Read value (should be second value)
    uint256 read_value = storage.GetStorage(contract_addr, key);
    
    BOOST_CHECK_EQUAL(read_value.GetHex(), value2.GetHex());
}

BOOST_AUTO_TEST_CASE(storage_nonexistent_key)
{
    auto db = std::make_shared<CVM::CVMDatabase>();
    CVM::EnhancedStorage storage(db.get());
    
    uint160 contract_addr;
    contract_addr.SetNull();
    
    uint256 key;
    key.SetHex("0000000000000000000000000000000000000000000000000000000000000099");
    
    // Read non-existent key (should return zero)
    uint256 read_value = storage.GetStorage(contract_addr, key);
    
    BOOST_CHECK(read_value.IsNull());
}

BOOST_AUTO_TEST_SUITE_END()
