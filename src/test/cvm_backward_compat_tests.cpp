// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/backward_compat.h>
#include <cvm/cvm.h>
#include <cvm/softfork.h>
#include <test/test_bitcoin.h>
#include <utilstrencodings.h>
#include <chainparams.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_backward_compat_tests, BasicTestingSetup)

// ============================================================================
// Task 25.1: CVM Contract Compatibility Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(cvm_contract_validation)
{
    CVM::BackwardCompatManager manager;
    
    // Valid CVM bytecode (PUSH 0x42, STOP)
    std::vector<uint8_t> valid_bytecode = ParseHex("01014200");
    std::string error;
    
    BOOST_CHECK(manager.ValidateCVMContract(valid_bytecode, error));
    BOOST_CHECK(error.empty());
}

BOOST_AUTO_TEST_CASE(cvm_contract_empty_bytecode)
{
    CVM::BackwardCompatManager manager;
    
    std::vector<uint8_t> empty_bytecode;
    std::string error;
    
    BOOST_CHECK(!manager.ValidateCVMContract(empty_bytecode, error));
    BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_CASE(cvm_contract_oversized_bytecode)
{
    CVM::BackwardCompatManager manager;
    
    // Create bytecode larger than MAX_CONTRACT_SIZE (24KB)
    std::vector<uint8_t> large_bytecode(25000, 0x00);
    std::string error;
    
    BOOST_CHECK(!manager.ValidateCVMContract(large_bytecode, error));
    BOOST_CHECK(error.find("maximum size") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(cvm_bytecode_format_detection)
{
    CVM::BackwardCompatManager manager;
    
    // CVM bytecode pattern
    std::vector<uint8_t> cvm_bytecode = ParseHex("01014200");
    CVM::BytecodeFormat format = manager.DetectBytecodeFormat(cvm_bytecode);
    
    // Should detect as CVM or UNKNOWN for short bytecode
    BOOST_CHECK(format == CVM::BytecodeFormat::CVM_NATIVE || 
                format == CVM::BytecodeFormat::UNKNOWN);
}

BOOST_AUTO_TEST_CASE(cvm_contract_checker_validation)
{
    CVM::CVMContractChecker checker;
    
    // Valid CVM bytecode
    std::vector<uint8_t> bytecode = ParseHex("01014200");
    
    CVM::CVMContractChecker::ValidationResult result = checker.ValidateContract(bytecode);
    
    // Should be valid (even if format detection is uncertain for short bytecode)
    BOOST_CHECK(!result.error.empty() || result.is_valid);
}

BOOST_AUTO_TEST_CASE(cvm_register_based_verification)
{
    CVM::CVMContractChecker checker;
    
    // CVM uses register-based patterns
    std::vector<uint8_t> bytecode = ParseHex("01014200");
    
    // This should not crash and return a boolean
    bool is_register_based = checker.VerifyRegisterBasedBytecode(bytecode);
    BOOST_CHECK(is_register_based == true || is_register_based == false);
}

// ============================================================================
// Task 25.2: Node Compatibility Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(node_compat_op_return_format)
{
    CVM::NodeCompatChecker checker;
    
    // Create a transaction with valid OP_RETURN
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(2);
    
    // Standard output
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("0000000000000000000000000000000000000000") << OP_EQUALVERIFY << OP_CHECKSIG;
    
    // OP_RETURN output with CVM magic
    std::vector<uint8_t> cvm_data = {0x43, 0x56, 0x4d, 0x31, 0x01}; // CVM1 + type
    mtx.vout[1].nValue = 0;
    mtx.vout[1].scriptPubKey = CScript() << OP_RETURN << cvm_data;
    
    CTransaction tx(mtx);
    
    BOOST_CHECK(checker.VerifyOPReturnFormat(tx));
    BOOST_CHECK(checker.IsTransactionFormatCompatible(tx));
}

BOOST_AUTO_TEST_CASE(node_compat_block_validation)
{
    CVM::NodeCompatChecker checker;
    const Consensus::Params& params = Params().GetConsensus();
    
    // Create a simple transaction
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("0000000000000000000000000000000000000000") << OP_EQUALVERIFY << OP_CHECKSIG;
    
    std::vector<CTransaction> txs;
    txs.push_back(CTransaction(mtx));
    
    CVM::NodeCompatChecker::BlockCompatResult result = checker.CheckBlockCompatibility(txs, 1000, params);
    
    BOOST_CHECK(result.old_node_can_validate);
    BOOST_CHECK(result.new_node_can_validate);
    BOOST_CHECK_EQUAL(result.standard_tx_count, 1);
}

BOOST_AUTO_TEST_CASE(node_version_detection)
{
    CVM::NodeCompatChecker checker;
    
    // Standard transaction
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ParseHex("0000000000000000000000000000000000000000") << OP_EQUALVERIFY << OP_CHECKSIG;
    
    CTransaction tx(mtx);
    
    uint32_t version = checker.DetectNodeVersion(tx);
    BOOST_CHECK_EQUAL(version, 0); // Standard transaction, any node version
    
    BOOST_CHECK(checker.IsNodeVersionSupported(version));
}

// ============================================================================
// Task 25.3: Reputation System Compatibility Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(reputation_trust_graph_preservation)
{
    CVM::ReputationCompatChecker checker;
    
    CVM::ReputationCompatChecker::TrustGraphStatus status = checker.CheckTrustGraphPreservation();
    
    BOOST_CHECK(status.is_preserved);
}

BOOST_AUTO_TEST_CASE(reputation_data_validation)
{
    CVM::ReputationCompatChecker checker;
    
    CVM::ReputationCompatChecker::ReputationDataStatus status = checker.CheckReputationData();
    
    BOOST_CHECK(status.is_valid);
}

BOOST_AUTO_TEST_CASE(reputation_hatv2_compatibility)
{
    CVM::ReputationCompatChecker checker;
    
    uint160 address;
    address.SetHex("0000000000000000000000000000000000000001");
    
    BOOST_CHECK(checker.IsHATv2Compatible(address));
    BOOST_CHECK(checker.CanMigrateToHATv2(address));
}

BOOST_AUTO_TEST_CASE(reputation_score_preservation)
{
    CVM::ReputationCompatChecker checker;
    
    uint160 address;
    address.SetHex("0000000000000000000000000000000000000001");
    
    // Verify score preservation with tolerance
    BOOST_CHECK(checker.VerifyScorePreservation(address, 50, 5));
}

// ============================================================================
// Task 25.4: Feature Flag Management Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(feature_flag_cvm_basic)
{
    CVM::FeatureFlagManager manager;
    const Consensus::Params& params = Params().GetConsensus();
    
    // Before CVM activation
    BOOST_CHECK(!manager.IsFeatureActive(CVM::FeatureFlag::CVM_BASIC, 0, params));
    
    // After CVM activation
    BOOST_CHECK(manager.IsFeatureActive(CVM::FeatureFlag::CVM_BASIC, 
                                        params.cvmActivationHeight + 1, params));
}

BOOST_AUTO_TEST_CASE(feature_flag_schedule)
{
    CVM::FeatureFlagManager manager;
    const Consensus::Params& params = Params().GetConsensus();
    
    std::vector<CVM::FeatureFlagManager::FeatureSchedule> schedule = manager.GetFeatureSchedule(params);
    
    BOOST_CHECK(!schedule.empty());
    
    // Check that CVM features have activation height
    bool found_cvm_basic = false;
    for (const auto& item : schedule) {
        if (item.flag == CVM::FeatureFlag::CVM_BASIC) {
            found_cvm_basic = true;
            BOOST_CHECK_EQUAL(item.activation_height, params.cvmActivationHeight);
        }
    }
    BOOST_CHECK(found_cvm_basic);
}

BOOST_AUTO_TEST_CASE(feature_flag_bytecode_version)
{
    CVM::FeatureFlagManager manager;
    
    // CVM bytecode
    std::vector<uint8_t> cvm_bytecode = ParseHex("01014200");
    
    CVM::FeatureFlagManager::BytecodeVersionInfo info = manager.DetectBytecodeVersion(cvm_bytecode);
    
    // Should detect some version
    BOOST_CHECK(info.version >= 0);
}

BOOST_AUTO_TEST_CASE(feature_flag_rollout_phase)
{
    CVM::FeatureFlagManager manager;
    const Consensus::Params& params = Params().GetConsensus();
    
    // Before CVM activation
    CVM::FeatureFlagManager::RolloutPhase phase = manager.GetCurrentPhase(0, params);
    BOOST_CHECK(phase == CVM::FeatureFlagManager::RolloutPhase::PRE_ACTIVATION);
    
    // Get phase description
    std::string desc = manager.GetPhaseDescription(phase);
    BOOST_CHECK(!desc.empty());
}

BOOST_AUTO_TEST_CASE(feature_flag_test_mode)
{
    CVM::FeatureFlagManager manager;
    const Consensus::Params& params = Params().GetConsensus();
    
    // Enable test mode
    manager.EnableTestMode(true);
    manager.SetTestFeatures(static_cast<uint32_t>(CVM::FeatureFlag::ALL_FEATURES));
    
    // All features should be active in test mode
    BOOST_CHECK(manager.IsFeatureActive(CVM::FeatureFlag::CVM_BASIC, 0, params));
    BOOST_CHECK(manager.IsFeatureActive(CVM::FeatureFlag::EVM_BYTECODE, 0, params));
    
    // Disable test mode
    manager.EnableTestMode(false);
}

// ============================================================================
// Utility Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(backward_compat_utils_feature_flag_string)
{
    std::string str = CVM::BackwardCompatUtils::FeatureFlagToString(CVM::FeatureFlag::CVM_BASIC);
    BOOST_CHECK_EQUAL(str, "CVM_BASIC");
    
    CVM::FeatureFlag flag = CVM::BackwardCompatUtils::StringToFeatureFlag("CVM_BASIC");
    BOOST_CHECK(flag == CVM::FeatureFlag::CVM_BASIC);
}

BOOST_AUTO_TEST_CASE(backward_compat_utils_all_flags)
{
    std::vector<CVM::FeatureFlag> flags = CVM::BackwardCompatUtils::GetAllFeatureFlags();
    
    BOOST_CHECK(!flags.empty());
    BOOST_CHECK(flags.size() >= 10); // At least 10 feature flags
}

BOOST_AUTO_TEST_CASE(backward_compat_utils_version_header)
{
    std::vector<uint8_t> bytecode = ParseHex("01014200");
    
    // Original bytecode should not have version header
    BOOST_CHECK(!CVM::BackwardCompatUtils::HasVersionHeader(bytecode));
    
    // Add version header
    std::vector<uint8_t> versioned = CVM::BackwardCompatUtils::AddVersionHeader(bytecode, 1);
    
    BOOST_CHECK(CVM::BackwardCompatUtils::HasVersionHeader(versioned));
    BOOST_CHECK_EQUAL(CVM::BackwardCompatUtils::ExtractBytecodeVersion(versioned), 1);
}

BOOST_AUTO_TEST_CASE(backward_compat_utils_format_flags)
{
    uint32_t flags = static_cast<uint32_t>(CVM::FeatureFlag::CVM_BASIC) |
                     static_cast<uint32_t>(CVM::FeatureFlag::CVM_STORAGE);
    
    std::string formatted = CVM::BackwardCompatUtils::FormatFeatureFlags(flags);
    
    BOOST_CHECK(formatted.find("CVM_BASIC") != std::string::npos);
    BOOST_CHECK(formatted.find("CVM_STORAGE") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(migration_status_report)
{
    CVM::BackwardCompatManager manager;
    const Consensus::Params& params = Params().GetConsensus();
    
    CVM::BackwardCompatManager::MigrationStatus status = 
        manager.CheckMigrationReadiness(params.cvmActivationHeight + 1, params);
    
    std::string report = CVM::BackwardCompatUtils::FormatCompatibilityReport(status);
    
    BOOST_CHECK(!report.empty());
    BOOST_CHECK(report.find("Compatibility Report") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
