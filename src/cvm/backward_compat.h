// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BACKWARD_COMPAT_H
#define CASCOIN_CVM_BACKWARD_COMPAT_H

#include <cvm/bytecode_detector.h>
#include <cvm/vmstate.h>
#include <cvm/cvm.h>
#include <cvm/softfork.h>
#include <primitives/transaction.h>
#include <consensus/params.h>
#include <uint256.h>
#include <vector>
#include <string>
#include <map>

namespace CVM {

/**
 * Feature Flags for gradual EVM rollout
 * 
 * These flags control which features are enabled at different stages
 * of the CVM-EVM enhancement rollout.
 */
enum class FeatureFlag : uint32_t {
    // Core CVM features (always enabled after CVM activation)
    CVM_BASIC = 0x00000001,           // Basic CVM execution
    CVM_STORAGE = 0x00000002,         // CVM storage operations
    CVM_CRYPTO = 0x00000004,          // CVM cryptographic operations
    
    // EVM compatibility features (enabled after CVM-EVM activation)
    EVM_BYTECODE = 0x00000010,        // EVM bytecode execution
    EVM_STORAGE = 0x00000020,         // EVM-compatible storage layout
    EVM_PRECOMPILES = 0x00000040,     // EVM precompiled contracts
    
    // Trust-aware features
    TRUST_CONTEXT = 0x00000100,       // Automatic trust context injection
    TRUST_GAS = 0x00000200,           // Reputation-based gas discounts
    TRUST_GATES = 0x00000400,         // Trust-gated operations
    
    // HAT v2 consensus features
    HAT_CONSENSUS = 0x00001000,       // HAT v2 consensus validation
    HAT_ATTESTATION = 0x00002000,     // Validator attestation system
    HAT_DAO = 0x00004000,             // DAO dispute resolution
    
    // Cross-format features
    HYBRID_CONTRACTS = 0x00010000,    // Hybrid CVM/EVM contracts
    CROSS_FORMAT_CALLS = 0x00020000,  // Cross-format contract calls
    
    // All features
    ALL_FEATURES = 0xFFFFFFFF
};

/**
 * Backward Compatibility Manager
 * 
 * Manages backward compatibility for CVM contracts and ensures
 * existing contracts continue to work correctly after EVM enhancement.
 */
class BackwardCompatManager {
public:
    BackwardCompatManager();
    ~BackwardCompatManager();
    
    // Feature flag management
    bool IsFeatureEnabled(FeatureFlag flag, int blockHeight, const Consensus::Params& params) const;
    uint32_t GetEnabledFeatures(int blockHeight, const Consensus::Params& params) const;
    void SetFeatureOverride(FeatureFlag flag, bool enabled);
    void ClearFeatureOverrides();
    
    // CVM contract compatibility
    bool ValidateCVMContract(const std::vector<uint8_t>& bytecode, std::string& error) const;
    bool CanExecuteCVMContract(const std::vector<uint8_t>& bytecode, int blockHeight, 
                               const Consensus::Params& params) const;
    bool TestCVMExecution(const std::vector<uint8_t>& bytecode, VMState& state, 
                          ContractStorage* storage, std::string& error);
    
    // EVM transaction compatibility
    bool ValidateEVMTransaction(const CTransaction& tx, int blockHeight,
                                const Consensus::Params& params, std::string& error) const;
    bool IsEVMTransactionAllowed(int blockHeight, const Consensus::Params& params) const;
    
    // Node compatibility
    bool CanOldNodeValidateBlock(const std::vector<CTransaction>& txs, int blockHeight,
                                 const Consensus::Params& params) const;
    bool IsOPReturnCompatible(const CTransaction& tx) const;
    
    // Reputation system compatibility
    bool ValidateReputationData(const uint160& address, int blockHeight,
                                const Consensus::Params& params, std::string& error) const;
    bool IsTrustGraphPreserved(int blockHeight, const Consensus::Params& params) const;
    
    // Bytecode format detection and version
    BytecodeFormat DetectBytecodeFormat(const std::vector<uint8_t>& bytecode) const;
    uint32_t GetBytecodeVersion(const std::vector<uint8_t>& bytecode) const;
    bool IsBytecodeVersionSupported(uint32_t version) const;
    
    // Migration helpers
    struct MigrationStatus {
        bool cvm_contracts_valid;
        bool evm_features_ready;
        bool trust_data_preserved;
        bool node_compatible;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };
    
    MigrationStatus CheckMigrationReadiness(int blockHeight, const Consensus::Params& params) const;
    
    // Statistics
    struct CompatibilityStats {
        size_t cvm_contracts_validated;
        size_t evm_transactions_validated;
        size_t compatibility_checks_passed;
        size_t compatibility_checks_failed;
        size_t feature_flag_queries;
    };
    
    CompatibilityStats GetStats() const { return stats; }
    void ResetStats();

private:
    // Feature flag state
    std::map<FeatureFlag, bool> feature_overrides;
    
    // Statistics
    mutable CompatibilityStats stats;
    
    // Internal helpers
    bool CheckCVMOpcodeCompatibility(const std::vector<uint8_t>& bytecode) const;
    bool CheckStorageLayoutCompatibility(const std::vector<uint8_t>& bytecode) const;
    bool CheckGasCompatibility(const std::vector<uint8_t>& bytecode, uint64_t gasLimit) const;
    
    // Bytecode detector instance
    mutable BytecodeDetector bytecode_detector;
};

/**
 * CVM Contract Compatibility Checker
 * 
 * Verifies that existing CVM contracts execute correctly
 * after the EVM enhancement.
 */
class CVMContractChecker {
public:
    CVMContractChecker();
    ~CVMContractChecker();
    
    // Contract validation
    struct ValidationResult {
        bool is_valid;
        bool is_cvm_native;
        bool is_evm_compatible;
        bool has_trust_features;
        std::string format_description;
        std::vector<std::string> warnings;
        std::string error;
    };
    
    ValidationResult ValidateContract(const std::vector<uint8_t>& bytecode) const;
    
    // Execution testing
    struct ExecutionTestResult {
        bool execution_succeeded;
        uint64_t gas_used;
        std::vector<uint8_t> return_data;
        std::string error;
        bool matches_expected;
    };
    
    ExecutionTestResult TestExecution(const std::vector<uint8_t>& bytecode,
                                      const std::vector<uint8_t>& input_data,
                                      uint64_t gas_limit,
                                      ContractStorage* storage);
    
    // Register-based bytecode verification
    bool VerifyRegisterBasedBytecode(const std::vector<uint8_t>& bytecode) const;
    bool VerifyOpcodeSequence(const std::vector<uint8_t>& bytecode) const;
    
    // Compatibility with enhanced VM
    bool IsCompatibleWithEnhancedVM(const std::vector<uint8_t>& bytecode) const;
    
private:
    BytecodeDetector detector;
    std::unique_ptr<CVM> cvm_engine;
};

/**
 * Node Compatibility Checker
 * 
 * Verifies that old nodes can validate blocks containing
 * EVM transactions via OP_RETURN soft-fork.
 */
class NodeCompatChecker {
public:
    NodeCompatChecker();
    ~NodeCompatChecker();
    
    // Block validation compatibility
    struct BlockCompatResult {
        bool old_node_can_validate;
        bool new_node_can_validate;
        size_t cvm_tx_count;
        size_t evm_tx_count;
        size_t standard_tx_count;
        std::vector<std::string> compatibility_notes;
    };
    
    BlockCompatResult CheckBlockCompatibility(const std::vector<CTransaction>& txs,
                                              int blockHeight,
                                              const Consensus::Params& params) const;
    
    // OP_RETURN soft-fork verification
    bool VerifyOPReturnFormat(const CTransaction& tx) const;
    bool IsValidCVMOPReturn(const CTxOut& txout) const;
    bool IsValidEVMOPReturn(const CTxOut& txout) const;
    
    // Transaction format compatibility
    bool IsTransactionFormatCompatible(const CTransaction& tx) const;
    bool HasValidOutputStructure(const CTransaction& tx) const;
    
    // Version detection
    uint32_t DetectNodeVersion(const CTransaction& tx) const;
    bool IsNodeVersionSupported(uint32_t version) const;
    
private:
    bool CheckOPReturnSize(const CTxOut& txout) const;
    bool CheckMagicBytes(const std::vector<uint8_t>& data) const;
};

/**
 * Reputation System Compatibility Checker
 * 
 * Ensures HAT v2 consensus doesn't break existing reputation data
 * and trust graph is preserved.
 */
class ReputationCompatChecker {
public:
    ReputationCompatChecker();
    ~ReputationCompatChecker();
    
    // Trust graph preservation
    struct TrustGraphStatus {
        bool is_preserved;
        size_t total_edges;
        size_t valid_edges;
        size_t migrated_edges;
        std::vector<std::string> issues;
    };
    
    TrustGraphStatus CheckTrustGraphPreservation() const;
    
    // Reputation data validation
    struct ReputationDataStatus {
        bool is_valid;
        size_t total_addresses;
        size_t addresses_with_reputation;
        size_t addresses_migrated;
        std::vector<std::string> issues;
    };
    
    ReputationDataStatus CheckReputationData() const;
    
    // HAT v2 compatibility
    bool IsHATv2Compatible(const uint160& address) const;
    bool CanMigrateToHATv2(const uint160& address) const;
    
    // Score preservation
    bool VerifyScorePreservation(const uint160& address, 
                                 int32_t expected_score,
                                 int32_t tolerance = 5) const;
    
private:
    bool ValidateTrustEdge(const uint160& from, const uint160& to, int16_t weight) const;
    bool ValidateReputationScore(const uint160& address, int32_t score) const;
};

/**
 * Feature Flag Manager
 * 
 * Manages feature flags for gradual EVM rollout with
 * version detection for contract bytecode format.
 */
class FeatureFlagManager {
public:
    FeatureFlagManager();
    ~FeatureFlagManager();
    
    // Feature activation
    bool IsFeatureActive(FeatureFlag flag, int blockHeight, const Consensus::Params& params) const;
    uint32_t GetActiveFeatures(int blockHeight, const Consensus::Params& params) const;
    
    // Feature scheduling
    struct FeatureSchedule {
        FeatureFlag flag;
        int activation_height;
        std::string description;
        bool requires_signaling;
    };
    
    std::vector<FeatureSchedule> GetFeatureSchedule(const Consensus::Params& params) const;
    int GetFeatureActivationHeight(FeatureFlag flag, const Consensus::Params& params) const;
    
    // Version detection
    struct BytecodeVersionInfo {
        uint32_t version;
        BytecodeFormat format;
        bool is_supported;
        std::string format_name;
        std::vector<FeatureFlag> required_features;
    };
    
    BytecodeVersionInfo DetectBytecodeVersion(const std::vector<uint8_t>& bytecode) const;
    bool IsBytecodeVersionSupported(uint32_t version, int blockHeight, 
                                    const Consensus::Params& params) const;
    
    // Rollout management
    enum class RolloutPhase {
        PRE_ACTIVATION,      // Before CVM-EVM activation
        SIGNALING,           // Miners signaling support
        LOCKED_IN,           // Activation locked in
        ACTIVE,              // Features active
        STABLE               // Stable operation
    };
    
    RolloutPhase GetCurrentPhase(int blockHeight, const Consensus::Params& params) const;
    std::string GetPhaseDescription(RolloutPhase phase) const;
    
    // Testing support
    void EnableTestMode(bool enable);
    void SetTestFeatures(uint32_t features);
    
private:
    bool test_mode;
    uint32_t test_features;
    
    BytecodeDetector detector;
    
    uint32_t GetCVMFeatures() const;
    uint32_t GetEVMFeatures() const;
    uint32_t GetTrustFeatures() const;
    uint32_t GetHATFeatures() const;
};

/**
 * Backward Compatibility Utilities
 */
namespace BackwardCompatUtils {
    // Feature flag helpers
    std::string FeatureFlagToString(FeatureFlag flag);
    FeatureFlag StringToFeatureFlag(const std::string& str);
    std::vector<FeatureFlag> GetAllFeatureFlags();
    
    // Bytecode version helpers
    uint32_t ExtractBytecodeVersion(const std::vector<uint8_t>& bytecode);
    bool HasVersionHeader(const std::vector<uint8_t>& bytecode);
    std::vector<uint8_t> AddVersionHeader(const std::vector<uint8_t>& bytecode, uint32_t version);
    
    // Compatibility checking
    bool IsFullyBackwardCompatible(const std::vector<uint8_t>& bytecode);
    bool RequiresEVMFeatures(const std::vector<uint8_t>& bytecode);
    bool RequiresTrustFeatures(const std::vector<uint8_t>& bytecode);
    
    // Migration helpers
    std::vector<uint8_t> MigrateCVMBytecode(const std::vector<uint8_t>& old_bytecode);
    bool CanMigrateBytecode(const std::vector<uint8_t>& bytecode);
    
    // Debugging
    std::string FormatCompatibilityReport(const BackwardCompatManager::MigrationStatus& status);
    std::string FormatFeatureFlags(uint32_t flags);
}

} // namespace CVM

#endif // CASCOIN_CVM_BACKWARD_COMPAT_H
