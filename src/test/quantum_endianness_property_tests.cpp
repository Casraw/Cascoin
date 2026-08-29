// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file quantum_endianness_property_tests.cpp
 * @brief Property-based tests for the quantum endianness fix
 *
 * Feature: quantum-endianness-fix
 *
 * These property-based tests verify that the endianness fix produces
 * consistent byte order across all quantum code paths using randomly
 * generated FALCON-512 keypairs. Each property test runs a minimum
 * of 100 iterations using BOOST_DATA_TEST_CASE.
 *
 * Properties tested:
 *   1. Address encoding round-trip (pubkey → address → destination)
 *   2. Address decoding round-trip (address → destination → address)
 *   3. Script stores canonical bytes
 *   4. Hash consistency across all quantum functions
 *   5. Signing path works with canonical bytes
 *   6. ECDSA non-interference
 */

#include <config/bitcoin-config.h>

#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>

#include <key.h>
#include <pubkey.h>
#include <base58.h>
#include <address_quantum.h>
#include <script/standard.h>
#include <script/sign.h>
#include <script/ismine.h>
#include <script/interpreter.h>
#include <crypto/sha256.h>
#include <quantum_registry.h>
#include <chainparams.h>
#include <policy/policy.h>
#include <test/test_bitcoin.h>
#include <utilstrencodings.h>
#include <keystore.h>

namespace bdata = boost::unit_test::data;

BOOST_FIXTURE_TEST_SUITE(quantum_endianness_property_tests, BasicTestingSetup)

#if ENABLE_QUANTUM

//=============================================================================
// Property 1: Address encoding round-trip
// Feature: quantum-endianness-fix, Property 1: Address encoding round-trip
//
// For any valid FALCON-512 public key, encoding a quantum address via
// EncodeQuantumAddress() then decoding via DecodeDestination() shall produce
// a WitnessV2Quantum whose raw bytes are identical to the GetQuantumID()
// output for that public key.
//
// **Validates: Requirements 1.1, 1.3, 2.1, 2.3, 9.1**
//=============================================================================
BOOST_DATA_TEST_CASE(property1_address_encoding_roundtrip,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration; // iteration index drives the loop; randomness comes from key generation

    SelectParams(CBaseChainParams::REGTEST);

    // Generate a fresh FALCON-512 keypair
    CKey key;
    key.MakeNewQuantumKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.IsQuantum());

    CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    BOOST_REQUIRE(pubkey.IsQuantum());

    // Get the expected quantum ID (SHA256 of pubkey, stored as LE uint256)
    uint256 expectedID = pubkey.GetQuantumID();
    BOOST_REQUIRE(!expectedID.IsNull());

    // Encode the quantum address
    std::string encoded = address::EncodeQuantumAddress(pubkey, Params());
    BOOST_REQUIRE_MESSAGE(!encoded.empty(),
        "EncodeQuantumAddress should succeed for iteration " << iteration);

    // Decode via DecodeDestination
    CTxDestination dest = DecodeDestination(encoded);
    BOOST_REQUIRE_MESSAGE(IsValidDestination(dest),
        "DecodeDestination should produce a valid destination for iteration " << iteration);

    // Extract the WitnessV2Quantum
    const WitnessV2Quantum* quantum = boost::get<WitnessV2Quantum>(&dest);
    BOOST_REQUIRE_MESSAGE(quantum != nullptr,
        "Decoded destination should be WitnessV2Quantum for iteration " << iteration);

    // Assert the decoded bytes match GetQuantumID() output exactly
    BOOST_CHECK_MESSAGE(*quantum == WitnessV2Quantum(expectedID),
        "Decoded WitnessV2Quantum bytes must match GetQuantumID() output for iteration " << iteration << ".\n"
        "  Expected: " << expectedID.GetHex() << "\n"
        "  Got:      " << quantum->GetHex());
}

//=============================================================================
// Property 2: Address decoding round-trip
// Feature: quantum-endianness-fix, Property 2: Address decoding round-trip
//
// For any valid quantum address string (well-formed Bech32m with quantum HRP,
// witness version 2, and 32-byte program), decoding via DecodeDestination()
// then re-encoding via EncodeDestination() shall produce the original address
// string.
//
// **Validates: Requirements 2.2**
//=============================================================================
BOOST_DATA_TEST_CASE(property2_address_decoding_roundtrip,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration;

    SelectParams(CBaseChainParams::REGTEST);

    // Generate a fresh FALCON-512 keypair
    CKey key;
    key.MakeNewQuantumKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.IsQuantum());

    CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    // Encode the quantum address (this is our "known" address string)
    std::string originalAddress = address::EncodeQuantumAddress(pubkey, Params());
    BOOST_REQUIRE_MESSAGE(!originalAddress.empty(),
        "EncodeQuantumAddress should succeed for iteration " << iteration);

    // Decode the address
    CTxDestination dest = DecodeDestination(originalAddress);
    BOOST_REQUIRE_MESSAGE(IsValidDestination(dest),
        "DecodeDestination should produce a valid destination for iteration " << iteration);

    // Re-encode via EncodeDestination (the generic visitor-based encoder)
    std::string reEncoded = EncodeDestination(dest);
    BOOST_REQUIRE_MESSAGE(!reEncoded.empty(),
        "EncodeDestination should succeed for WitnessV2Quantum for iteration " << iteration);

    // Assert the strings match exactly
    BOOST_CHECK_MESSAGE(originalAddress == reEncoded,
        "Decode-encode round-trip must produce the original address string for iteration " << iteration << ".\n"
        "  Original:   " << originalAddress << "\n"
        "  Re-encoded: " << reEncoded);
}

//=============================================================================
// Property 3: Script stores canonical bytes
// Feature: quantum-endianness-fix, Property 3: Script stores canonical bytes
//
// For any valid FALCON-512 public key, constructing a WitnessV2Quantum from
// GetQuantumID(), then calling GetScriptForDestination(), then extracting the
// 32-byte program from the resulting script, shall yield bytes identical to
// GetQuantumID() output.
//
// **Validates: Requirements 1.2, 3.1, 9.2**
//=============================================================================
BOOST_DATA_TEST_CASE(property3_script_canonical_bytes,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration;

    // Generate a fresh FALCON-512 keypair
    CKey key;
    key.MakeNewQuantumKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.IsQuantum());

    CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());

    // Get the quantum ID
    uint256 quantumID = pubkey.GetQuantumID();
    BOOST_REQUIRE(!quantumID.IsNull());

    // Create WitnessV2Quantum destination from GetQuantumID()
    WitnessV2Quantum quantumDest(quantumID);
    CTxDestination dest = quantumDest;

    // Create the script
    CScript script = GetScriptForDestination(dest);
    BOOST_REQUIRE_MESSAGE(!script.empty(),
        "GetScriptForDestination should produce a non-empty script for iteration " << iteration);

    // Extract the witness program from the script
    int witnessVersion;
    std::vector<unsigned char> witnessProgram;
    BOOST_REQUIRE_MESSAGE(script.IsWitnessProgram(witnessVersion, witnessProgram),
        "Script should be a valid witness program for iteration " << iteration);
    BOOST_CHECK_EQUAL(witnessVersion, 2);
    BOOST_CHECK_EQUAL(witnessProgram.size(), 32u);

    // Assert the extracted program bytes match GetQuantumID() output
    BOOST_CHECK_MESSAGE(memcmp(witnessProgram.data(), quantumID.begin(), 32) == 0,
        "Script witness program bytes must match GetQuantumID() output for iteration " << iteration << ".\n"
        "  Expected (GetQuantumID): " << HexStr(quantumID.begin(), quantumID.end()) << "\n"
        "  Got (from script):       " << HexStr(witnessProgram));
}

//=============================================================================
// Property 4: Hash consistency across all quantum functions
// Feature: quantum-endianness-fix, Property 4: Hash consistency across all quantum functions
//
// For any valid FALCON-512 public key, the following four computations shall
// all produce the same 32-byte value:
//   1. GetQuantumID() on the CPubKey
//   2. GetQuantumWitnessProgram() on the CPubKey
//   3. ParseQuantumWitness() .pubkeyHash field for a registration witness
//   4. Direct CSHA256 computation (same as VerifyQuantumTransaction internal logic)
//
// **Validates: Requirements 3.3, 5.1, 5.2, 5.3, 5.4, 5.5, 9.4**
//=============================================================================
BOOST_DATA_TEST_CASE(property4_hash_consistency,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration;

    // Generate a fresh FALCON-512 keypair
    CKey key;
    key.MakeNewQuantumKey();
    BOOST_REQUIRE(key.IsValid());
    BOOST_REQUIRE(key.IsQuantum());

    CPubKey pubkey = key.GetPubKey();
    BOOST_REQUIRE(pubkey.IsValid());
    BOOST_REQUIRE(pubkey.IsQuantum());

    // Method 1: GetQuantumID() on CPubKey
    uint256 hash1 = pubkey.GetQuantumID();
    BOOST_REQUIRE(!hash1.IsNull());

    // Method 2: GetQuantumWitnessProgram() on CPubKey
    uint256 hash2 = address::GetQuantumWitnessProgram(pubkey);

    // Method 3: ParseQuantumWitness() - build a registration witness and parse it
    std::vector<unsigned char> witnessData;
    witnessData.push_back(QUANTUM_WITNESS_MARKER_REGISTRATION);
    std::vector<unsigned char> pubkeyBytes(pubkey.begin(), pubkey.end());
    witnessData.insert(witnessData.end(), pubkeyBytes.begin(), pubkeyBytes.end());
    // Add a dummy signature (just needs to be non-empty for parsing)
    std::vector<unsigned char> dummySig(100, 0x42);
    witnessData.insert(witnessData.end(), dummySig.begin(), dummySig.end());

    std::vector<std::vector<unsigned char>> witnessStack;
    witnessStack.push_back(witnessData);

    QuantumWitnessData parsed = ParseQuantumWitness(witnessStack);
    BOOST_REQUIRE_MESSAGE(parsed.isValid,
        "ParseQuantumWitness should succeed for iteration " << iteration << ": " << parsed.error);
    BOOST_REQUIRE(parsed.isRegistration);
    uint256 hash3 = parsed.pubkeyHash;

    // Method 4: Direct CSHA256 computation (same as VerifyQuantumTransaction internal logic)
    uint256 hash4;
    CSHA256().Write(pubkeyBytes.data(), pubkeyBytes.size()).Finalize(hash4.begin());

    // Assert all four hashes are identical
    BOOST_CHECK_MESSAGE(hash1 == hash2,
        "GetQuantumID() and GetQuantumWitnessProgram() must match for iteration " << iteration << ".\n"
        "  GetQuantumID():             " << hash1.GetHex() << "\n"
        "  GetQuantumWitnessProgram(): " << hash2.GetHex());

    BOOST_CHECK_MESSAGE(hash1 == hash3,
        "GetQuantumID() and ParseQuantumWitness().pubkeyHash must match for iteration " << iteration << ".\n"
        "  GetQuantumID():             " << hash1.GetHex() << "\n"
        "  ParseQuantumWitness().hash: " << hash3.GetHex());

    BOOST_CHECK_MESSAGE(hash1 == hash4,
        "GetQuantumID() and direct CSHA256 must match for iteration " << iteration << ".\n"
        "  GetQuantumID(): " << hash1.GetHex() << "\n"
        "  Direct CSHA256: " << hash4.GetHex());
}

//=============================================================================
// Property 5: Signing path works with canonical bytes
// Feature: quantum-endianness-fix, Property 5: Signing path works with canonical bytes
//
// For any valid FALCON-512 keypair present in the keystore, and a quantum
// output script derived from that keypair's GetQuantumID():
//   - ProduceSignature() shall successfully produce a valid quantum witness
//   - IsSolvable() shall return true
//   - IsMine() shall return ISMINE_SPENDABLE
//
// **Validates: Requirements 4.1, 4.2, 4.3, 9.3**
//=============================================================================
BOOST_DATA_TEST_CASE(property5_signing_path,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration;

    SelectParams(CBaseChainParams::REGTEST);

    // Generate a fresh FALCON-512 keypair
    CKey quantumKey;
    quantumKey.MakeNewQuantumKey();
    BOOST_REQUIRE(quantumKey.IsValid());
    BOOST_REQUIRE(quantumKey.IsQuantum());

    CPubKey quantumPubKey = quantumKey.GetPubKey();
    BOOST_REQUIRE(quantumPubKey.IsValid());
    BOOST_REQUIRE(quantumPubKey.IsQuantum());

    // Add the key to a basic keystore
    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddKeyPubKey(quantumKey, quantumPubKey));

    // Create a quantum output script from GetQuantumID()
    uint256 quantumID = quantumPubKey.GetQuantumID();
    WitnessV2Quantum quantumDest(quantumID);
    CScript scriptPubKey = GetScriptForDestination(quantumDest);
    BOOST_REQUIRE_MESSAGE(!scriptPubKey.empty(),
        "GetScriptForDestination should produce a non-empty script for iteration " << iteration);

    // Verify the script is recognized as TX_WITNESS_V2_QUANTUM
    txnouttype whichType;
    std::vector<std::vector<unsigned char>> vSolutions;
    BOOST_REQUIRE(Solver(scriptPubKey, whichType, vSolutions));
    BOOST_CHECK_EQUAL(static_cast<int>(whichType), static_cast<int>(TX_WITNESS_V2_QUANTUM));

    // Create a simple transaction spending from the quantum output
    CMutableTransaction prevTx;
    prevTx.nVersion = 2;
    prevTx.vout.resize(1);
    prevTx.vout[0].nValue = 50 * COIN;
    prevTx.vout[0].scriptPubKey = scriptPubKey;

    CMutableTransaction spendTx;
    spendTx.nVersion = 2;
    spendTx.vin.resize(1);
    spendTx.vin[0].prevout = COutPoint(prevTx.GetHash(), 0);
    spendTx.vout.resize(1);
    spendTx.vout[0].nValue = 49 * COIN;
    spendTx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Sign the transaction using ProduceSignature
    CTransaction txToConst(spendTx);
    TransactionSignatureCreator creator(&keystore, &txToConst, 0, prevTx.vout[0].nValue, SIGHASH_ALL | SIGHASH_FORKID);
    SignatureData sigdata;
    bool signResult = ProduceSignature(creator, scriptPubKey, sigdata);
    BOOST_CHECK_MESSAGE(signResult,
        "ProduceSignature should succeed for quantum output at iteration " << iteration);

    // Verify the witness was populated
    BOOST_CHECK_MESSAGE(!sigdata.scriptWitness.stack.empty(),
        "Quantum signature should produce a non-empty witness stack at iteration " << iteration);

    // Verify IsSolvable returns true
    BOOST_CHECK_MESSAGE(IsSolvable(keystore, scriptPubKey),
        "IsSolvable should return true for quantum script at iteration " << iteration);

    // Verify IsMine returns ISMINE_SPENDABLE
    isminetype mine = IsMine(keystore, scriptPubKey);
    BOOST_CHECK_MESSAGE(mine == ISMINE_SPENDABLE,
        "IsMine should return ISMINE_SPENDABLE for quantum script at iteration " << iteration << ".\n"
        "  Expected: " << ISMINE_SPENDABLE << "\n"
        "  Got:      " << static_cast<int>(mine));
}

//=============================================================================
// Property 6: ECDSA non-interference
// Feature: quantum-endianness-fix, Property 6: ECDSA non-interference
//
// For any valid ECDSA keypair and standard transaction type (P2WPKH), the
// quantum endianness fix shall not alter the behavior of
// GetScriptForDestination(), ProduceSignature(), VerifyScript(), IsSolvable(),
// or IsMine() for that transaction.
//
// **Validates: Requirements 8.1, 8.2, 9.5**
//=============================================================================
BOOST_DATA_TEST_CASE(property6_ecdsa_noninterference,
                     bdata::xrange(100),
                     iteration)
{
    (void)iteration;

    SelectParams(CBaseChainParams::REGTEST);

    // Generate a fresh ECDSA keypair
    CKey ecdsaKey;
    ecdsaKey.MakeNewKey(true);  // compressed
    BOOST_REQUIRE(ecdsaKey.IsValid());
    BOOST_REQUIRE(ecdsaKey.IsECDSA());

    CPubKey ecdsaPubKey = ecdsaKey.GetPubKey();
    BOOST_REQUIRE(ecdsaPubKey.IsValid());
    BOOST_REQUIRE(ecdsaPubKey.IsCompressed());

    // Add the key to a basic keystore
    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddKeyPubKey(ecdsaKey, ecdsaPubKey));

    // Create a P2WPKH output script via GetScriptForDestination
    CScript p2wpkhScript = GetScriptForDestination(WitnessV0KeyHash(ecdsaPubKey.GetID()));
    BOOST_REQUIRE_MESSAGE(!p2wpkhScript.empty(),
        "GetScriptForDestination should produce a non-empty P2WPKH script at iteration " << iteration);

    // Add the P2SH wrapper script so IsMine recognizes it
    CScript p2shWrapper;
    p2shWrapper << OP_0 << ToByteVector(ecdsaPubKey.GetID());
    keystore.AddCScript(p2shWrapper);

    // Verify the script is recognized as TX_WITNESS_V0_KEYHASH
    txnouttype whichType;
    std::vector<std::vector<unsigned char>> vSolutions;
    BOOST_REQUIRE(Solver(p2wpkhScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(static_cast<int>(whichType), static_cast<int>(TX_WITNESS_V0_KEYHASH));

    // Create a simple transaction spending from the P2WPKH output
    CMutableTransaction prevTx;
    prevTx.nVersion = 2;
    prevTx.vout.resize(1);
    prevTx.vout[0].nValue = 50 * COIN;
    prevTx.vout[0].scriptPubKey = p2wpkhScript;

    CMutableTransaction spendTx;
    spendTx.nVersion = 2;
    spendTx.vin.resize(1);
    spendTx.vin[0].prevout = COutPoint(prevTx.GetHash(), 0);
    spendTx.vout.resize(1);
    spendTx.vout[0].nValue = 49 * COIN;
    spendTx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    // Sign the transaction using ProduceSignature
    CTransaction txToConst(spendTx);
    TransactionSignatureCreator creator(&keystore, &txToConst, 0, prevTx.vout[0].nValue, SIGHASH_ALL | SIGHASH_FORKID);
    SignatureData sigdata;
    bool signResult = ProduceSignature(creator, p2wpkhScript, sigdata);
    BOOST_CHECK_MESSAGE(signResult,
        "ProduceSignature should succeed for P2WPKH output at iteration " << iteration);

    // Verify the witness was populated (P2WPKH produces 2 stack items: sig + pubkey)
    BOOST_CHECK_MESSAGE(sigdata.scriptWitness.stack.size() == 2,
        "P2WPKH signature should produce 2 witness stack items at iteration " << iteration
        << ", got: " << sigdata.scriptWitness.stack.size());

    // Verify the signature using VerifyScript
    UpdateTransaction(spendTx, 0, sigdata);
    CTransaction finalTx(spendTx);
    ScriptError serror;
    bool verifyResult = VerifyScript(
        sigdata.scriptSig,
        p2wpkhScript,
        &sigdata.scriptWitness,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&finalTx, 0, prevTx.vout[0].nValue),
        &serror);
    BOOST_CHECK_MESSAGE(verifyResult,
        "VerifyScript should succeed for P2WPKH transaction at iteration " << iteration
        << ". Error: " << ScriptErrorString(serror));

    // Verify IsSolvable returns true
    BOOST_CHECK_MESSAGE(IsSolvable(keystore, p2wpkhScript),
        "IsSolvable should return true for P2WPKH script at iteration " << iteration);

    // Verify IsMine works correctly
    isminetype mine = IsMine(keystore, p2wpkhScript);
    BOOST_CHECK_MESSAGE(mine != ISMINE_NO,
        "IsMine should not return ISMINE_NO for P2WPKH script with matching key at iteration " << iteration);
}

#else // !ENABLE_QUANTUM

BOOST_AUTO_TEST_CASE(property_tests_skipped)
{
    BOOST_TEST_MESSAGE("Quantum endianness property-based tests skipped (--enable-quantum not set)");
    BOOST_CHECK(true);
}

#endif // ENABLE_QUANTUM

// Status summary
BOOST_AUTO_TEST_CASE(property_tests_status)
{
    BOOST_TEST_MESSAGE("Quantum endianness property-based tests (Tasks 9.1-9.6) completed");
#if ENABLE_QUANTUM
    BOOST_TEST_MESSAGE("  Property 1: Address encoding round-trip (100 iterations)");
    BOOST_TEST_MESSAGE("  Property 2: Address decoding round-trip (100 iterations)");
    BOOST_TEST_MESSAGE("  Property 3: Script stores canonical bytes (100 iterations)");
    BOOST_TEST_MESSAGE("  Property 4: Hash consistency across all quantum functions (100 iterations)");
    BOOST_TEST_MESSAGE("  Property 5: Signing path works with canonical bytes (100 iterations)");
    BOOST_TEST_MESSAGE("  Property 6: ECDSA non-interference (100 iterations)");
#else
    BOOST_TEST_MESSAGE("  All property tests skipped (--enable-quantum not set)");
#endif
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
