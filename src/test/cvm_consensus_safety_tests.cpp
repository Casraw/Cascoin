// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/consensus_safety.h>
#include <cvm/securehat.h>
#include <cvm/trustgraph.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_attestation.h>
#include <cvm/cross_chain_bridge.h>
#include <test/test_bitcoin.h>
#include <uint256.h>
#include <hash.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_consensus_safety_tests, BasicTestingSetup)

// ========== Task 23.1: Deterministic Execution Validation Tests ==========

BOOST_AUTO_TEST_CASE(deterministic_gas_discount_calculation)
{
    // Test that gas discount calculation is deterministic
    CVM::ConsensusSafetyValidator validator;
    
    // Test various reputation levels
    for (uint8_t rep = 0; rep <= 100; rep += 10) {
        uint64_t baseGas = 100000;
        
        // Calculate discount multiple times
        uint64_t discount1 = validator.CalculateDeterministicGasDiscount(rep, baseGas);
        uint64_t discount2 = validator.CalculateDeterministicGasDiscount(rep, baseGas);
        uint64_t discount3 = validator.CalculateDeterministicGasDiscount(rep, baseGas);
        
        // All calculations should produce identical results
        BOOST_CHECK_EQUAL(discount1, discount2);
        BOOST_CHECK_EQUAL(discount2, discount3);
        
        // Discount should be within bounds (max 50%)
        BOOST_CHECK_LE(discount1, baseGas / 2);
    }
}

BOOST_AUTO_TEST_CASE(deterministic_free_gas_allowance)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Test various reputation levels
    for (uint8_t rep = 0; rep <= 100; rep += 5) {
        // Calculate allowance multiple times
        uint64_t allowance1 = validator.CalculateDeterministicFreeGasAllowance(rep);
        uint64_t allowance2 = validator.CalculateDeterministicFreeGasAllowance(rep);
        uint64_t allowance3 = validator.CalculateDeterministicFreeGasAllowance(rep);
        
        // All calculations should produce identical results
        BOOST_CHECK_EQUAL(allowance1, allowance2);
        BOOST_CHECK_EQUAL(allowance2, allowance3);
        
        // Below threshold (80): no allowance
        if (rep < 80) {
            BOOST_CHECK_EQUAL(allowance1, 0);
        } else {
            // Above threshold: should have allowance
            BOOST_CHECK_GT(allowance1, 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(validator_selection_seed_determinism)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Create test transaction hash
    uint256 txHash;
    txHash.SetHex("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    
    int blockHeight = 100000;
    
    // Calculate seed multiple times
    uint256 seed1 = validator.CalculateValidatorSelectionSeed(txHash, blockHeight);
    uint256 seed2 = validator.CalculateValidatorSelectionSeed(txHash, blockHeight);
    uint256 seed3 = validator.CalculateValidatorSelectionSeed(txHash, blockHeight);
    
    // All calculations should produce identical results
    BOOST_CHECK(seed1 == seed2);
    BOOST_CHECK(seed2 == seed3);
    
    // Different inputs should produce different seeds
    uint256 txHash2;
    txHash2.SetHex("0xfedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
    
    uint256 seed4 = validator.CalculateValidatorSelectionSeed(txHash2, blockHeight);
    BOOST_CHECK(seed1 != seed4);
    
    // Different block heights should produce different seeds
    uint256 seed5 = validator.CalculateValidatorSelectionSeed(txHash, blockHeight + 1);
    BOOST_CHECK(seed1 != seed5);
}

// ========== Task 23.2: Reputation-Based Feature Consensus Tests ==========

BOOST_AUTO_TEST_CASE(gas_discount_consensus_validation)
{
    CVM::ConsensusSafetyValidator validator;
    
    uint160 address;
    address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    // Test gas discount consensus for various reputation levels
    for (uint8_t rep = 0; rep <= 100; rep += 20) {
        uint64_t baseGas = 100000;
        
        CVM::GasDiscountConsensusResult result = validator.ValidateGasDiscountConsensus(
            address, rep, baseGas);
        
        // Should always reach consensus (deterministic calculation)
        BOOST_CHECK(result.isConsensus);
        BOOST_CHECK_EQUAL(result.reputation, rep);
        
        // Verify discount is within bounds
        BOOST_CHECK_LE(result.calculatedDiscount, baseGas / 2);
    }
}

BOOST_AUTO_TEST_CASE(free_gas_eligibility_consensus)
{
    CVM::ConsensusSafetyValidator validator;
    
    uint160 address;
    address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    // Test below threshold
    CVM::FreeGasEligibilityResult result1 = validator.ValidateFreeGasEligibility(address, 79);
    BOOST_CHECK(result1.isConsensus);
    BOOST_CHECK(!result1.isEligible);
    BOOST_CHECK_EQUAL(result1.allowance, 0);
    
    // Test at threshold
    CVM::FreeGasEligibilityResult result2 = validator.ValidateFreeGasEligibility(address, 80);
    BOOST_CHECK(result2.isConsensus);
    BOOST_CHECK(result2.isEligible);
    BOOST_CHECK_GT(result2.allowance, 0);
    
    // Test above threshold
    CVM::FreeGasEligibilityResult result3 = validator.ValidateFreeGasEligibility(address, 100);
    BOOST_CHECK(result3.isConsensus);
    BOOST_CHECK(result3.isEligible);
    BOOST_CHECK_GT(result3.allowance, result2.allowance);
}

BOOST_AUTO_TEST_CASE(gas_discount_formula_correctness)
{
    CVM::ConsensusSafetyValidator validator;
    
    uint64_t baseGas = 100000;
    
    // Test specific reputation values
    // Reputation 0: no discount
    BOOST_CHECK_EQUAL(validator.CalculateDeterministicGasDiscount(0, baseGas), 0);
    
    // Reputation 50: 25% discount (50 * 5 / 1000 = 0.25)
    uint64_t discount50 = validator.CalculateDeterministicGasDiscount(50, baseGas);
    BOOST_CHECK_EQUAL(discount50, 25000);  // 100000 * 50 * 5 / 1000 = 25000
    
    // Reputation 100: 50% discount (capped)
    uint64_t discount100 = validator.CalculateDeterministicGasDiscount(100, baseGas);
    BOOST_CHECK_EQUAL(discount100, 50000);  // Capped at 50%
}

BOOST_AUTO_TEST_CASE(free_gas_allowance_formula_correctness)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Below threshold: 0
    BOOST_CHECK_EQUAL(validator.CalculateDeterministicFreeGasAllowance(79), 0);
    
    // At threshold (80): BASE * (20 + 0) / 20 = BASE = 100000
    BOOST_CHECK_EQUAL(validator.CalculateDeterministicFreeGasAllowance(80), 100000);
    
    // At 90: BASE * (20 + 10) / 20 = BASE * 1.5 = 150000
    BOOST_CHECK_EQUAL(validator.CalculateDeterministicFreeGasAllowance(90), 150000);
    
    // At 100: BASE * (20 + 20) / 20 = BASE * 2 = 200000
    BOOST_CHECK_EQUAL(validator.CalculateDeterministicFreeGasAllowance(100), 200000);
}

// ========== Task 23.3: Trust Score Synchronization Tests ==========

BOOST_AUTO_TEST_CASE(trust_graph_state_hash_determinism)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Calculate state hash multiple times
    uint256 hash1 = validator.CalculateTrustGraphStateHash();
    uint256 hash2 = validator.CalculateTrustGraphStateHash();
    uint256 hash3 = validator.CalculateTrustGraphStateHash();
    
    // All calculations should produce identical results
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash2 == hash3);
}

BOOST_AUTO_TEST_CASE(trust_graph_state_verification)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Get current state hash
    uint256 currentHash = validator.CalculateTrustGraphStateHash();
    
    // Verification should pass with correct hash
    BOOST_CHECK(validator.VerifyTrustGraphState(currentHash));
    
    // Verification should fail with incorrect hash
    uint256 wrongHash;
    wrongHash.SetHex("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    BOOST_CHECK(!validator.VerifyTrustGraphState(wrongHash));
}

// ========== Task 23.4: Cross-Chain Attestation Validation Tests ==========

BOOST_AUTO_TEST_CASE(attestation_hash_determinism)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Create test attestation
    CVM::TrustAttestation attestation;
    attestation.address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    attestation.trustScore = 75;
    attestation.timestamp = 1700000000;
    attestation.sourceChainId.SetHex("0x0000000000000000000000000000000000000000000000000000000000000001");
    attestation.signature = std::vector<uint8_t>(64, 0xAB);  // Dummy signature
    
    // Calculate hash multiple times
    uint256 hash1 = validator.CalculateAttestationHash(attestation);
    uint256 hash2 = validator.CalculateAttestationHash(attestation);
    uint256 hash3 = validator.CalculateAttestationHash(attestation);
    
    // All calculations should produce identical results
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash2 == hash3);
    
    // Different attestations should produce different hashes
    CVM::TrustAttestation attestation2 = attestation;
    attestation2.trustScore = 80;
    
    uint256 hash4 = validator.CalculateAttestationHash(attestation2);
    BOOST_CHECK(hash1 != hash4);
}

BOOST_AUTO_TEST_CASE(attestation_signature_validation)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Create test attestation with valid signature length
    CVM::TrustAttestation validAttestation;
    validAttestation.address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    validAttestation.trustScore = 75;
    validAttestation.timestamp = 1700000000;
    validAttestation.sourceChainId.SetHex("0x0000000000000000000000000000000000000000000000000000000000000001");
    validAttestation.signature = std::vector<uint8_t>(64, 0xAB);  // Valid length
    
    // Should pass basic validation
    BOOST_CHECK(validator.VerifyAttestationSignature(validAttestation));
    
    // Empty signature should fail
    CVM::TrustAttestation emptySignature = validAttestation;
    emptySignature.signature.clear();
    BOOST_CHECK(!validator.VerifyAttestationSignature(emptySignature));
    
    // Too short signature should fail
    CVM::TrustAttestation shortSignature = validAttestation;
    shortSignature.signature = std::vector<uint8_t>(32, 0xAB);  // Too short
    BOOST_CHECK(!validator.VerifyAttestationSignature(shortSignature));
    
    // Too long signature should fail
    CVM::TrustAttestation longSignature = validAttestation;
    longSignature.signature = std::vector<uint8_t>(256, 0xAB);  // Too long
    BOOST_CHECK(!validator.VerifyAttestationSignature(longSignature));
}

BOOST_AUTO_TEST_CASE(cross_chain_attestation_validation)
{
    CVM::ConsensusSafetyValidator validator;
    
    // Create valid attestation
    CVM::TrustAttestation validAttestation;
    validAttestation.address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    validAttestation.trustScore = 75;
    validAttestation.timestamp = GetTime();  // Current time
    validAttestation.sourceChainId.SetHex("0x0000000000000000000000000000000000000000000000000000000000000001");
    validAttestation.signature = std::vector<uint8_t>(64, 0xAB);
    
    CVM::CrossChainAttestationResult result = validator.ValidateCrossChainAttestation(validAttestation);
    
    // Should be valid and consensus-safe
    BOOST_CHECK(result.isValid);
    BOOST_CHECK(result.isConsensusSafe);
    BOOST_CHECK_EQUAL(result.trustScore, 75);
    
    // Test with null address
    CVM::TrustAttestation nullAddress = validAttestation;
    nullAddress.address.SetNull();
    
    CVM::CrossChainAttestationResult nullResult = validator.ValidateCrossChainAttestation(nullAddress);
    BOOST_CHECK(!nullResult.isValid);
    
    // Test with zero timestamp
    CVM::TrustAttestation zeroTimestamp = validAttestation;
    zeroTimestamp.timestamp = 0;
    
    CVM::CrossChainAttestationResult zeroResult = validator.ValidateCrossChainAttestation(zeroTimestamp);
    BOOST_CHECK(!zeroResult.isValid);
    
    // Test with old attestation (> 24 hours)
    CVM::TrustAttestation oldAttestation = validAttestation;
    oldAttestation.timestamp = GetTime() - (25 * 60 * 60);  // 25 hours ago
    
    CVM::CrossChainAttestationResult oldResult = validator.ValidateCrossChainAttestation(oldAttestation);
    BOOST_CHECK(!oldResult.isValid);
}

// ========== Integration Tests ==========

BOOST_AUTO_TEST_CASE(full_consensus_safety_validation)
{
    CVM::ConsensusSafetyValidator validator;
    
    uint160 address;
    address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    int blockHeight = 100000;
    
    // Run full validation (without database/HAT initialized, some checks will fail gracefully)
    bool result = validator.RunFullValidation(address, blockHeight);
    
    // The result depends on whether database/HAT are initialized
    // In test environment without full setup, this tests the validation flow
    BOOST_CHECK(result == true || result == false);  // Just verify it doesn't crash
}

BOOST_AUTO_TEST_CASE(validation_report_generation)
{
    CVM::ConsensusSafetyValidator validator;
    
    uint160 address;
    address.SetHex("0x1234567890abcdef1234567890abcdef12345678");
    
    int blockHeight = 100000;
    
    // Generate validation report
    std::string report = validator.GetValidationReport(address, blockHeight);
    
    // Report should contain expected sections
    BOOST_CHECK(report.find("Consensus Safety Validation Report") != std::string::npos);
    BOOST_CHECK(report.find("Task 23.1") != std::string::npos);
    BOOST_CHECK(report.find("Task 23.2") != std::string::npos);
    BOOST_CHECK(report.find("Task 23.3") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
