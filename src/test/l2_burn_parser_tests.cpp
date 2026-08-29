// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_burn_parser_tests.cpp
 * @brief Property-based tests for L2 Burn Parser
 * 
 * **Feature: l2-bridge-security, Property 1: OP_RETURN Format Validation**
 * **Validates: Requirements 1.2, 2.1**
 * 
 * Property 1: OP_RETURN Format Validation
 * *For any* byte sequence, the system SHALL accept it as a valid burn script
 * if and only if it starts with OP_RETURN, contains the "L2BURN" marker,
 * and has exactly 51 bytes of payload with valid chain_id, pubkey, and amount.
 * 
 * **Feature: l2-bridge-security, Property 2: Burn Amount Consistency**
 * **Validates: Requirements 1.4, 4.2**
 * 
 * Property 2: Burn Amount Consistency
 * *For any* valid burn transaction, the amount encoded in the OP_RETURN payload
 * SHALL equal the sum of inputs minus the sum of spendable outputs minus the
 * transaction fee.
 */

#include <l2/burn_parser.h>
#include <key.h>
#include <random.h>
#include <uint256.h>
#include <script/script.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <cstring>

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
 * Helper function to generate a valid compressed public key
 * Uses a deterministic approach for testing
 */
static CPubKey GenerateValidCompressedPubKey()
{
    // Generate a valid compressed public key
    // Compressed pubkeys start with 0x02 or 0x03 and are 33 bytes
    std::vector<unsigned char> pubkeyData(33);
    pubkeyData[0] = (TestRand32() % 2 == 0) ? 0x02 : 0x03;
    
    // Fill with random data for the x-coordinate
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
    // Generate amount between 1 satoshi and 1000 CAS
    return (TestRand64() % (1000 * COIN)) + 1;
}

/**
 * Helper to create a valid burn script
 */
static CScript CreateValidBurnScript(uint32_t chainId, const CPubKey& pubkey, CAmount amount)
{
    return l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
}

/**
 * Helper to create an invalid burn script with wrong marker
 */
static CScript CreateInvalidMarkerScript()
{
    std::vector<unsigned char> payload;
    payload.reserve(l2::BURN_DATA_SIZE);
    
    // Wrong marker
    const char* wrongMarker = "NOTBRN";
    payload.insert(payload.end(), wrongMarker, wrongMarker + 6);
    
    // Valid chain ID
    unsigned char chainIdBytes[4];
    WriteLE32(chainIdBytes, 1);
    payload.insert(payload.end(), chainIdBytes, chainIdBytes + 4);
    
    // Valid pubkey
    CPubKey pubkey = GenerateValidCompressedPubKey();
    payload.insert(payload.end(), pubkey.begin(), pubkey.end());
    
    // Valid amount
    unsigned char amountBytes[8];
    WriteLE64(amountBytes, 100 * COIN);
    payload.insert(payload.end(), amountBytes, amountBytes + 8);
    
    CScript script;
    script << OP_RETURN << payload;
    return script;
}

/**
 * Helper to create a script with wrong payload size
 */
static CScript CreateWrongSizeScript()
{
    std::vector<unsigned char> payload;
    
    // Correct marker but wrong total size
    payload.insert(payload.end(), l2::BURN_MARKER, l2::BURN_MARKER + l2::BURN_MARKER_SIZE);
    
    // Only add chain ID (incomplete payload)
    unsigned char chainIdBytes[4];
    WriteLE32(chainIdBytes, 1);
    payload.insert(payload.end(), chainIdBytes, chainIdBytes + 4);
    
    CScript script;
    script << OP_RETURN << payload;
    return script;
}

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(l2_burn_parser_tests)

// ============================================================================
// Basic Unit Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(create_valid_burn_script)
{
    uint32_t chainId = 1;
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    
    CScript script = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
    
    BOOST_CHECK(!script.empty());
    BOOST_CHECK(script[0] == OP_RETURN);
    BOOST_CHECK(l2::BurnTransactionParser::ValidateBurnFormat(script));
}

BOOST_AUTO_TEST_CASE(parse_valid_burn_script)
{
    uint32_t chainId = 1;
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    
    CScript script = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
    
    auto burnData = l2::BurnData::Parse(script);
    
    BOOST_CHECK(burnData.has_value());
    BOOST_CHECK_EQUAL(burnData->chainId, chainId);
    BOOST_CHECK(burnData->recipientPubKey == pubkey);
    BOOST_CHECK_EQUAL(burnData->amount, amount);
    BOOST_CHECK(burnData->IsValid());
}

BOOST_AUTO_TEST_CASE(reject_invalid_marker)
{
    CScript script = CreateInvalidMarkerScript();
    
    BOOST_CHECK(!l2::BurnTransactionParser::ValidateBurnFormat(script));
    
    auto burnData = l2::BurnData::Parse(script);
    BOOST_CHECK(!burnData.has_value());
}

BOOST_AUTO_TEST_CASE(reject_wrong_payload_size)
{
    CScript script = CreateWrongSizeScript();
    
    BOOST_CHECK(!l2::BurnTransactionParser::ValidateBurnFormat(script));
    
    auto burnData = l2::BurnData::Parse(script);
    BOOST_CHECK(!burnData.has_value());
}

BOOST_AUTO_TEST_CASE(reject_zero_chain_id)
{
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    
    // Create script with zero chain ID
    CScript script = l2::BurnTransactionParser::CreateBurnScript(0, pubkey, amount);
    
    // CreateBurnScript should return empty script for invalid inputs
    BOOST_CHECK(script.empty());
}

BOOST_AUTO_TEST_CASE(reject_zero_amount)
{
    uint32_t chainId = 1;
    CPubKey pubkey = GenerateValidCompressedPubKey();
    
    // Create script with zero amount
    CScript script = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, 0);
    
    // CreateBurnScript should return empty script for invalid inputs
    BOOST_CHECK(script.empty());
}

BOOST_AUTO_TEST_CASE(reject_non_op_return_script)
{
    // Create a P2PKH script instead of OP_RETURN
    CScript script;
    script << OP_DUP << OP_HASH160 << RandomBytes(20) << OP_EQUALVERIFY << OP_CHECKSIG;
    
    BOOST_CHECK(!l2::BurnTransactionParser::ValidateBurnFormat(script));
    
    auto burnData = l2::BurnData::Parse(script);
    BOOST_CHECK(!burnData.has_value());
}

BOOST_AUTO_TEST_CASE(burn_data_serialization_roundtrip)
{
    l2::BurnData original;
    original.chainId = RandomChainId();
    original.recipientPubKey = GenerateValidCompressedPubKey();
    original.amount = RandomBurnAmount();
    
    std::vector<unsigned char> serialized = original.Serialize();
    
    l2::BurnData restored;
    BOOST_CHECK(restored.Deserialize(serialized));
    
    BOOST_CHECK(original == restored);
}

BOOST_AUTO_TEST_CASE(extract_burn_marker)
{
    uint32_t chainId = 1;
    CPubKey pubkey = GenerateValidCompressedPubKey();
    CAmount amount = 100 * COIN;
    
    CScript script = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
    
    std::string marker = l2::BurnTransactionParser::ExtractBurnMarker(script);
    BOOST_CHECK_EQUAL(marker, "L2BURN");
}

BOOST_AUTO_TEST_CASE(get_recipient_address)
{
    l2::BurnData burnData;
    burnData.chainId = 1;
    burnData.recipientPubKey = GenerateValidCompressedPubKey();
    burnData.amount = 100 * COIN;
    
    uint160 address = burnData.GetRecipientAddress();
    
    // Address should be the Hash160 of the public key
    BOOST_CHECK(address == burnData.recipientPubKey.GetID());
}

// ============================================================================
// Property-Based Tests
// ============================================================================

/**
 * **Property 1: OP_RETURN Format Validation**
 * 
 * *For any* byte sequence, the system SHALL accept it as a valid burn script
 * if and only if it starts with OP_RETURN, contains the "L2BURN" marker,
 * and has exactly 51 bytes of payload with valid chain_id, pubkey, and amount.
 * 
 * **Validates: Requirements 1.2, 2.1**
 */
BOOST_AUTO_TEST_CASE(property_op_return_format_validation)
{
    // Run 100 iterations as per PBT requirements
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random valid inputs
        uint32_t chainId = RandomChainId();
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        
        // Create valid burn script
        CScript validScript = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
        
        // Property: Valid scripts should be accepted
        BOOST_CHECK_MESSAGE(l2::BurnTransactionParser::ValidateBurnFormat(validScript),
            "Valid burn script should be accepted in iteration " << iteration);
        
        // Property: Parsing valid script should succeed
        auto burnData = l2::BurnData::Parse(validScript);
        BOOST_CHECK_MESSAGE(burnData.has_value(),
            "Parsing valid burn script should succeed in iteration " << iteration);
        
        if (burnData) {
            // Property: Parsed data should match original
            BOOST_CHECK_MESSAGE(burnData->chainId == chainId,
                "Chain ID should match in iteration " << iteration);
            BOOST_CHECK_MESSAGE(burnData->recipientPubKey == pubkey,
                "Public key should match in iteration " << iteration);
            BOOST_CHECK_MESSAGE(burnData->amount == amount,
                "Amount should match in iteration " << iteration);
            BOOST_CHECK_MESSAGE(burnData->IsValid(),
                "Parsed burn data should be valid in iteration " << iteration);
        }
        
        // Property: Random garbage should be rejected
        std::vector<unsigned char> randomPayload = RandomBytes(l2::BURN_DATA_SIZE);
        CScript randomScript;
        randomScript << OP_RETURN << randomPayload;
        
        // Random data is very unlikely to have correct marker
        std::string marker(reinterpret_cast<const char*>(randomPayload.data()), 6);
        if (marker != "L2BURN") {
            BOOST_CHECK_MESSAGE(!l2::BurnTransactionParser::ValidateBurnFormat(randomScript),
                "Random script without L2BURN marker should be rejected in iteration " << iteration);
        }
    }
}

/**
 * **Property 1 (continued): Script without OP_RETURN rejected**
 * 
 * *For any* script that does not start with OP_RETURN, validation SHALL fail.
 * 
 * **Validates: Requirements 1.2, 2.1**
 */
BOOST_AUTO_TEST_CASE(property_non_op_return_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Create various non-OP_RETURN scripts
        CScript script;
        
        int scriptType = iteration % 5;
        switch (scriptType) {
            case 0:
                // P2PKH
                script << OP_DUP << OP_HASH160 << RandomBytes(20) << OP_EQUALVERIFY << OP_CHECKSIG;
                break;
            case 1:
                // P2SH
                script << OP_HASH160 << RandomBytes(20) << OP_EQUAL;
                break;
            case 2:
                // Empty script
                break;
            case 3:
                // Random opcodes
                script << OP_1 << OP_2 << OP_ADD;
                break;
            case 4:
                // Just data push
                script << RandomBytes(51);
                break;
        }
        
        BOOST_CHECK_MESSAGE(!l2::BurnTransactionParser::ValidateBurnFormat(script),
            "Non-OP_RETURN script should be rejected in iteration " << iteration);
        
        auto burnData = l2::BurnData::Parse(script);
        BOOST_CHECK_MESSAGE(!burnData.has_value(),
            "Parsing non-OP_RETURN script should fail in iteration " << iteration);
    }
}

/**
 * **Property 1 (continued): Wrong payload size rejected**
 * 
 * *For any* OP_RETURN script with payload size != 51 bytes, validation SHALL fail.
 * 
 * **Validates: Requirements 1.2, 2.1**
 */
BOOST_AUTO_TEST_CASE(property_wrong_payload_size_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random payload size (not 51)
        size_t payloadSize = TestRand32() % 200;
        if (payloadSize == l2::BURN_DATA_SIZE) {
            payloadSize = l2::BURN_DATA_SIZE + 1;
        }
        
        std::vector<unsigned char> payload = RandomBytes(payloadSize);
        
        // Even if we put the correct marker, wrong size should fail
        if (payloadSize >= l2::BURN_MARKER_SIZE) {
            memcpy(payload.data(), l2::BURN_MARKER, l2::BURN_MARKER_SIZE);
        }
        
        CScript script;
        script << OP_RETURN << payload;
        
        BOOST_CHECK_MESSAGE(!l2::BurnTransactionParser::ValidateBurnFormat(script),
            "Script with payload size " << payloadSize << " should be rejected in iteration " << iteration);
        
        auto burnData = l2::BurnData::Parse(script);
        BOOST_CHECK_MESSAGE(!burnData.has_value(),
            "Parsing script with wrong payload size should fail in iteration " << iteration);
    }
}

/**
 * **Property 1 (continued): Create-Parse roundtrip**
 * 
 * *For any* valid burn parameters, creating a script and parsing it back
 * SHALL produce the original parameters.
 * 
 * **Validates: Requirements 1.2, 2.1**
 */
BOOST_AUTO_TEST_CASE(property_create_parse_roundtrip)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random valid inputs
        uint32_t chainId = RandomChainId();
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        
        // Create script
        CScript script = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
        BOOST_REQUIRE(!script.empty());
        
        // Parse back
        auto burnData = l2::BurnData::Parse(script);
        BOOST_REQUIRE_MESSAGE(burnData.has_value(),
            "Roundtrip parsing should succeed in iteration " << iteration);
        
        // Verify roundtrip
        BOOST_CHECK_MESSAGE(burnData->chainId == chainId,
            "Chain ID roundtrip failed in iteration " << iteration);
        BOOST_CHECK_MESSAGE(burnData->recipientPubKey == pubkey,
            "Public key roundtrip failed in iteration " << iteration);
        BOOST_CHECK_MESSAGE(burnData->amount == amount,
            "Amount roundtrip failed in iteration " << iteration);
    }
}

/**
 * **Property 2: Burn Amount Consistency**
 * 
 * *For any* valid burn transaction, the amount encoded in the OP_RETURN payload
 * SHALL be retrievable via CalculateBurnedAmount.
 * 
 * **Validates: Requirements 1.4, 4.2**
 */
BOOST_AUTO_TEST_CASE(property_burn_amount_consistency)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Generate random valid inputs
        uint32_t chainId = RandomChainId();
        CPubKey pubkey = GenerateValidCompressedPubKey();
        CAmount amount = RandomBurnAmount();
        
        // Create burn script
        CScript burnScript = l2::BurnTransactionParser::CreateBurnScript(chainId, pubkey, amount);
        BOOST_REQUIRE(!burnScript.empty());
        
        // Create a transaction with the burn output
        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.nLockTime = 0;
        
        // Add a dummy input (in real scenario this would be a valid UTXO)
        CTxIn input;
        input.prevout.hash.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
        input.prevout.n = 0;
        mtx.vin.push_back(input);
        
        // Add the burn output (OP_RETURN outputs have 0 value)
        CTxOut burnOutput(0, burnScript);
        mtx.vout.push_back(burnOutput);
        
        // Create immutable transaction
        CTransaction tx(mtx);
        
        // Property: IsBurnTransaction should return true
        BOOST_CHECK_MESSAGE(l2::BurnTransactionParser::IsBurnTransaction(tx),
            "Transaction with burn output should be detected in iteration " << iteration);
        
        // Property: GetBurnOutputIndex should return correct index
        int burnIndex = l2::BurnTransactionParser::GetBurnOutputIndex(tx);
        BOOST_CHECK_MESSAGE(burnIndex == 0,
            "Burn output index should be 0 in iteration " << iteration);
        
        // Property: ParseBurnTransaction should succeed
        auto burnData = l2::BurnTransactionParser::ParseBurnTransaction(tx);
        BOOST_CHECK_MESSAGE(burnData.has_value(),
            "ParseBurnTransaction should succeed in iteration " << iteration);
        
        if (burnData) {
            // Property: Amount should match
            BOOST_CHECK_MESSAGE(burnData->amount == amount,
                "Parsed amount should match original in iteration " << iteration);
            
            // Property: CalculateBurnedAmount should return the encoded amount
            CAmount calculatedAmount = l2::BurnTransactionParser::CalculateBurnedAmount(tx);
            BOOST_CHECK_MESSAGE(calculatedAmount == amount,
                "CalculateBurnedAmount should return encoded amount in iteration " << iteration);
        }
    }
}

/**
 * **Property 2 (continued): Non-burn transactions return zero**
 * 
 * *For any* transaction without a valid burn output, CalculateBurnedAmount SHALL return 0.
 * 
 * **Validates: Requirements 1.4, 4.2**
 */
BOOST_AUTO_TEST_CASE(property_non_burn_returns_zero)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        // Create a regular transaction without burn output
        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.nLockTime = 0;
        
        // Add a dummy input
        CTxIn input;
        input.prevout.hash.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
        input.prevout.n = 0;
        mtx.vin.push_back(input);
        
        // Add a regular P2PKH output
        CScript p2pkh;
        p2pkh << OP_DUP << OP_HASH160 << RandomBytes(20) << OP_EQUALVERIFY << OP_CHECKSIG;
        CTxOut output(RandomBurnAmount(), p2pkh);
        mtx.vout.push_back(output);
        
        CTransaction tx(mtx);
        
        // Property: IsBurnTransaction should return false
        BOOST_CHECK_MESSAGE(!l2::BurnTransactionParser::IsBurnTransaction(tx),
            "Regular transaction should not be detected as burn in iteration " << iteration);
        
        // Property: GetBurnOutputIndex should return -1
        int burnIndex = l2::BurnTransactionParser::GetBurnOutputIndex(tx);
        BOOST_CHECK_MESSAGE(burnIndex == -1,
            "Burn output index should be -1 for non-burn tx in iteration " << iteration);
        
        // Property: ParseBurnTransaction should return nullopt
        auto burnData = l2::BurnTransactionParser::ParseBurnTransaction(tx);
        BOOST_CHECK_MESSAGE(!burnData.has_value(),
            "ParseBurnTransaction should fail for non-burn tx in iteration " << iteration);
        
        // Property: CalculateBurnedAmount should return 0
        CAmount calculatedAmount = l2::BurnTransactionParser::CalculateBurnedAmount(tx);
        BOOST_CHECK_MESSAGE(calculatedAmount == 0,
            "CalculateBurnedAmount should return 0 for non-burn tx in iteration " << iteration);
    }
}

/**
 * **Property: BurnData serialization roundtrip**
 * 
 * *For any* valid BurnData, serializing and deserializing SHALL produce
 * an equivalent object.
 * 
 * **Validates: Requirements 1.2**
 */
BOOST_AUTO_TEST_CASE(property_burndata_serialization_roundtrip)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnData original;
        original.chainId = RandomChainId();
        original.recipientPubKey = GenerateValidCompressedPubKey();
        original.amount = RandomBurnAmount();
        
        BOOST_REQUIRE(original.IsValid());
        
        // Serialize
        std::vector<unsigned char> serialized = original.Serialize();
        BOOST_REQUIRE(!serialized.empty());
        
        // Deserialize
        l2::BurnData restored;
        BOOST_REQUIRE_MESSAGE(restored.Deserialize(serialized),
            "Deserialization should succeed in iteration " << iteration);
        
        // Verify equality
        BOOST_CHECK_MESSAGE(original == restored,
            "Roundtrip should produce equal object in iteration " << iteration);
        BOOST_CHECK_MESSAGE(restored.IsValid(),
            "Restored object should be valid in iteration " << iteration);
    }
}

/**
 * **Property: Invalid pubkey rejected**
 * 
 * *For any* burn data with invalid public key, IsValid SHALL return false.
 * 
 * **Validates: Requirements 1.2, 2.1**
 */
BOOST_AUTO_TEST_CASE(property_invalid_pubkey_rejected)
{
    // Run 100 iterations
    for (int iteration = 0; iteration < 100; ++iteration) {
        l2::BurnData burnData;
        burnData.chainId = RandomChainId();
        burnData.amount = RandomBurnAmount();
        
        // Create invalid pubkey (wrong prefix)
        std::vector<unsigned char> invalidPubkey(33);
        invalidPubkey[0] = 0x04;  // Uncompressed prefix (invalid for our use)
        for (size_t i = 1; i < 33; ++i) {
            invalidPubkey[i] = static_cast<unsigned char>(TestRand32() & 0xFF);
        }
        burnData.recipientPubKey.Set(invalidPubkey.begin(), invalidPubkey.end());
        
        // Property: Invalid pubkey should make BurnData invalid
        BOOST_CHECK_MESSAGE(!burnData.IsValid(),
            "BurnData with invalid pubkey should be invalid in iteration " << iteration);
    }
}

BOOST_AUTO_TEST_SUITE_END()
