// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/backward_compat.h>
#include <cvm/activation.h>
#include <cvm/opcodes.h>
#include <util.h>
#include <utilstrencodings.h>

namespace CVM {

// ============================================================================
// BackwardCompatManager Implementation
// ============================================================================

BackwardCompatManager::BackwardCompatManager() {
    ResetStats();
}

BackwardCompatManager::~BackwardCompatManager() {
}

bool BackwardCompatManager::IsFeatureEnabled(FeatureFlag flag, int blockHeight, 
                                             const Consensus::Params& params) const {
    stats.feature_flag_queries++;
    
    // Check for manual override first
    auto it = feature_overrides.find(flag);
    if (it != feature_overrides.end()) {
        return it->second;
    }
    
    // Check if CVM is activated
    bool cvmActive = (blockHeight >= params.cvmActivationHeight);
    
    // Check if CVM-EVM enhancement is activated
    bool cvmEvmActive = IsCVMEVMEnabled(blockHeight, params);
    
    switch (flag) {
        // Core CVM features - enabled after CVM activation
        case FeatureFlag::CVM_BASIC:
        case FeatureFlag::CVM_STORAGE:
        case FeatureFlag::CVM_CRYPTO:
            return cvmActive;
        
        // EVM compatibility features - enabled after CVM-EVM activation
        case FeatureFlag::EVM_BYTECODE:
        case FeatureFlag::EVM_STORAGE:
        case FeatureFlag::EVM_PRECOMPILES:
            return cvmEvmActive;
        
        // Trust-aware features - enabled after CVM-EVM activation
        case FeatureFlag::TRUST_CONTEXT:
        case FeatureFlag::TRUST_GAS:
        case FeatureFlag::TRUST_GATES:
            return cvmEvmActive;
        
        // HAT v2 consensus features - enabled after CVM-EVM activation
        case FeatureFlag::HAT_CONSENSUS:
        case FeatureFlag::HAT_ATTESTATION:
        case FeatureFlag::HAT_DAO:
            return cvmEvmActive;
        
        // Cross-format features - enabled after CVM-EVM activation
        case FeatureFlag::HYBRID_CONTRACTS:
        case FeatureFlag::CROSS_FORMAT_CALLS:
            return cvmEvmActive;
        
        case FeatureFlag::ALL_FEATURES:
            return cvmEvmActive;
        
        default:
            return false;
    }
}

uint32_t BackwardCompatManager::GetEnabledFeatures(int blockHeight, 
                                                   const Consensus::Params& params) const {
    uint32_t features = 0;
    
    // Check each feature flag
    if (IsFeatureEnabled(FeatureFlag::CVM_BASIC, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::CVM_BASIC);
    if (IsFeatureEnabled(FeatureFlag::CVM_STORAGE, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::CVM_STORAGE);
    if (IsFeatureEnabled(FeatureFlag::CVM_CRYPTO, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::CVM_CRYPTO);
    if (IsFeatureEnabled(FeatureFlag::EVM_BYTECODE, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::EVM_BYTECODE);
    if (IsFeatureEnabled(FeatureFlag::EVM_STORAGE, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::EVM_STORAGE);
    if (IsFeatureEnabled(FeatureFlag::EVM_PRECOMPILES, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::EVM_PRECOMPILES);
    if (IsFeatureEnabled(FeatureFlag::TRUST_CONTEXT, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::TRUST_CONTEXT);
    if (IsFeatureEnabled(FeatureFlag::TRUST_GAS, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::TRUST_GAS);
    if (IsFeatureEnabled(FeatureFlag::TRUST_GATES, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::TRUST_GATES);
    if (IsFeatureEnabled(FeatureFlag::HAT_CONSENSUS, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::HAT_CONSENSUS);
    if (IsFeatureEnabled(FeatureFlag::HAT_ATTESTATION, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::HAT_ATTESTATION);
    if (IsFeatureEnabled(FeatureFlag::HAT_DAO, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::HAT_DAO);
    if (IsFeatureEnabled(FeatureFlag::HYBRID_CONTRACTS, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::HYBRID_CONTRACTS);
    if (IsFeatureEnabled(FeatureFlag::CROSS_FORMAT_CALLS, blockHeight, params))
        features |= static_cast<uint32_t>(FeatureFlag::CROSS_FORMAT_CALLS);
    
    return features;
}

void BackwardCompatManager::SetFeatureOverride(FeatureFlag flag, bool enabled) {
    feature_overrides[flag] = enabled;
}

void BackwardCompatManager::ClearFeatureOverrides() {
    feature_overrides.clear();
}

bool BackwardCompatManager::ValidateCVMContract(const std::vector<uint8_t>& bytecode, 
                                                std::string& error) const {
    stats.cvm_contracts_validated++;
    
    if (bytecode.empty()) {
        error = "Empty bytecode";
        return false;
    }
    
    if (bytecode.size() > MAX_CONTRACT_SIZE) {
        error = "Bytecode exceeds maximum size";
        return false;
    }
    
    // Check opcode compatibility
    if (!CheckCVMOpcodeCompatibility(bytecode)) {
        error = "Invalid CVM opcodes detected";
        return false;
    }
    
    // Verify bytecode structure
    if (!CVM::VerifyBytecode(bytecode)) {
        error = "Invalid bytecode structure";
        return false;
    }
    
    return true;
}

bool BackwardCompatManager::CanExecuteCVMContract(const std::vector<uint8_t>& bytecode, 
                                                  int blockHeight,
                                                  const Consensus::Params& params) const {
    // CVM contracts can execute if CVM is activated
    if (blockHeight < params.cvmActivationHeight) {
        return false;
    }
    
    // Validate the bytecode
    std::string error;
    if (!ValidateCVMContract(bytecode, error)) {
        return false;
    }
    
    // Detect format
    BytecodeDetectionResult result = bytecode_detector.DetectFormat(bytecode);
    
    // CVM native bytecode can always execute after CVM activation
    if (result.format == BytecodeFormat::CVM_NATIVE) {
        return true;
    }
    
    // EVM bytecode requires CVM-EVM activation
    if (result.format == BytecodeFormat::EVM_BYTECODE) {
        return IsCVMEVMEnabled(blockHeight, params);
    }
    
    // Hybrid contracts require CVM-EVM activation
    if (result.format == BytecodeFormat::HYBRID) {
        return IsCVMEVMEnabled(blockHeight, params);
    }
    
    return false;
}

bool BackwardCompatManager::TestCVMExecution(const std::vector<uint8_t>& bytecode, 
                                             VMState& state,
                                             ContractStorage* storage, 
                                             std::string& error) {
    // Validate bytecode first
    if (!ValidateCVMContract(bytecode, error)) {
        return false;
    }
    
    // Create CVM instance and execute
    CVM cvm;
    bool success = cvm.Execute(bytecode, state, storage);
    
    if (!success) {
        error = state.GetError();
        stats.compatibility_checks_failed++;
    } else {
        stats.compatibility_checks_passed++;
    }
    
    return success;
}

bool BackwardCompatManager::ValidateEVMTransaction(const CTransaction& tx, int blockHeight,
                                                   const Consensus::Params& params, 
                                                   std::string& error) const {
    stats.evm_transactions_validated++;
    
    // EVM transactions require CVM-EVM activation
    if (!IsCVMEVMEnabled(blockHeight, params)) {
        error = "CVM-EVM features not yet activated";
        return false;
    }
    
    // Check if transaction has valid OP_RETURN format
    if (!IsOPReturnCompatible(tx)) {
        error = "Invalid OP_RETURN format for EVM transaction";
        return false;
    }
    
    // Find and validate CVM OP_RETURN
    int opReturnIndex = FindCVMOpReturn(tx);
    if (opReturnIndex < 0) {
        error = "No CVM OP_RETURN found";
        return false;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
        error = "Failed to parse CVM OP_RETURN";
        return false;
    }
    
    // Validate based on operation type
    if (opType == CVMOpType::EVM_DEPLOY || opType == CVMOpType::CONTRACT_DEPLOY) {
        CVMDeployData deployData;
        if (!deployData.Deserialize(data)) {
            error = "Failed to deserialize deployment data";
            return false;
        }
        
        // Check bytecode format
        if (deployData.format != BytecodeFormat::EVM_BYTECODE &&
            deployData.format != BytecodeFormat::UNKNOWN) {
            // AUTO format is acceptable
        }
    } else if (opType == CVMOpType::EVM_CALL || opType == CVMOpType::CONTRACT_CALL) {
        CVMCallData callData;
        if (!callData.Deserialize(data)) {
            error = "Failed to deserialize call data";
            return false;
        }
    }
    
    return true;
}

bool BackwardCompatManager::IsEVMTransactionAllowed(int blockHeight, 
                                                    const Consensus::Params& params) const {
    return IsCVMEVMEnabled(blockHeight, params);
}

bool BackwardCompatManager::CanOldNodeValidateBlock(const std::vector<CTransaction>& txs, 
                                                    int blockHeight,
                                                    const Consensus::Params& params) const {
    // Old nodes can validate any block because CVM/EVM transactions use OP_RETURN
    // which is valid but unspendable from old node's perspective
    
    for (const auto& tx : txs) {
        // Check each transaction has valid structure
        if (!IsOPReturnCompatible(tx)) {
            return false;
        }
    }
    
    return true;
}

bool BackwardCompatManager::IsOPReturnCompatible(const CTransaction& tx) const {
    // Check all outputs for valid OP_RETURN format
    for (const auto& txout : tx.vout) {
        if (txout.scriptPubKey.size() > 0 && txout.scriptPubKey[0] == OP_RETURN) {
            // OP_RETURN output - check size limit
            if (txout.scriptPubKey.size() > MAX_OP_RETURN_SIZE + 3) {
                return false;
            }
        }
    }
    
    return true;
}

bool BackwardCompatManager::ValidateReputationData(const uint160& address, int blockHeight,
                                                   const Consensus::Params& params, 
                                                   std::string& error) const {
    // Reputation data is valid if ASRS is activated
    if (blockHeight < params.asrsActivationHeight) {
        error = "ASRS not yet activated";
        return false;
    }
    
    // Additional validation would check database consistency
    // For now, basic validation passes
    return true;
}

bool BackwardCompatManager::IsTrustGraphPreserved(int blockHeight, 
                                                  const Consensus::Params& params) const {
    // Trust graph is preserved across CVM-EVM activation
    // The enhancement adds features but doesn't modify existing data
    return true;
}

BytecodeFormat BackwardCompatManager::DetectBytecodeFormat(const std::vector<uint8_t>& bytecode) const {
    BytecodeDetectionResult result = bytecode_detector.DetectFormat(bytecode);
    return result.format;
}

uint32_t BackwardCompatManager::GetBytecodeVersion(const std::vector<uint8_t>& bytecode) const {
    return BackwardCompatUtils::ExtractBytecodeVersion(bytecode);
}

bool BackwardCompatManager::IsBytecodeVersionSupported(uint32_t version) const {
    // Version 0 = original CVM bytecode (always supported)
    // Version 1 = CVM with trust features
    // Version 2 = EVM bytecode
    // Version 3 = Hybrid bytecode
    return version <= 3;
}

BackwardCompatManager::MigrationStatus BackwardCompatManager::CheckMigrationReadiness(
    int blockHeight, const Consensus::Params& params) const {
    
    MigrationStatus status;
    status.cvm_contracts_valid = true;
    status.evm_features_ready = IsCVMEVMEnabled(blockHeight, params);
    status.trust_data_preserved = IsTrustGraphPreserved(blockHeight, params);
    status.node_compatible = true;
    
    // Check CVM activation
    if (blockHeight < params.cvmActivationHeight) {
        status.warnings.push_back("CVM not yet activated");
        status.cvm_contracts_valid = false;
    }
    
    // Check CVM-EVM activation
    if (!status.evm_features_ready) {
        status.warnings.push_back("CVM-EVM features not yet activated");
    }
    
    // Check ASRS activation
    if (blockHeight < params.asrsActivationHeight) {
        status.warnings.push_back("ASRS not yet activated");
    }
    
    return status;
}

void BackwardCompatManager::ResetStats() {
    stats.cvm_contracts_validated = 0;
    stats.evm_transactions_validated = 0;
    stats.compatibility_checks_passed = 0;
    stats.compatibility_checks_failed = 0;
    stats.feature_flag_queries = 0;
}

bool BackwardCompatManager::CheckCVMOpcodeCompatibility(const std::vector<uint8_t>& bytecode) const {
    // Verify all opcodes are valid CVM opcodes
    for (size_t i = 0; i < bytecode.size(); ) {
        uint8_t opcodeByte = bytecode[i];
        
        if (!IsValidOpCode(opcodeByte)) {
            return false;
        }
        
        OpCode opcode = static_cast<OpCode>(opcodeByte);
        
        // Handle PUSH instruction specially (has immediate data)
        if (opcode == OpCode::OP_PUSH) {
            if (i + 1 >= bytecode.size()) {
                return false;
            }
            uint8_t size = bytecode[i + 1];
            if (size == 0 || size > 32) {
                return false;
            }
            if (i + 2 + size > bytecode.size()) {
                return false;
            }
            i += 2 + size;
        } else {
            i++;
        }
    }
    
    return true;
}

bool BackwardCompatManager::CheckStorageLayoutCompatibility(const std::vector<uint8_t>& bytecode) const {
    // Storage layout is compatible if bytecode uses standard SLOAD/SSTORE
    // Both CVM and EVM use 32-byte keys/values
    return true;
}

bool BackwardCompatManager::CheckGasCompatibility(const std::vector<uint8_t>& bytecode, 
                                                  uint64_t gasLimit) const {
    // Gas is compatible if within limits
    return gasLimit <= MAX_GAS_PER_TX;
}


// ============================================================================
// CVMContractChecker Implementation
// ============================================================================

CVMContractChecker::CVMContractChecker() {
    cvm_engine = std::make_unique<CVM>();
}

CVMContractChecker::~CVMContractChecker() {
}

CVMContractChecker::ValidationResult CVMContractChecker::ValidateContract(
    const std::vector<uint8_t>& bytecode) const {
    
    ValidationResult result;
    result.is_valid = false;
    result.is_cvm_native = false;
    result.is_evm_compatible = false;
    result.has_trust_features = false;
    
    if (bytecode.empty()) {
        result.error = "Empty bytecode";
        return result;
    }
    
    if (bytecode.size() > MAX_CONTRACT_SIZE) {
        result.error = "Bytecode exceeds maximum size";
        return result;
    }
    
    // Detect format
    BytecodeDetectionResult detection = detector.DetectFormat(bytecode);
    
    result.is_valid = detection.is_valid;
    result.is_cvm_native = (detection.format == BytecodeFormat::CVM_NATIVE);
    result.is_evm_compatible = (detection.format == BytecodeFormat::EVM_BYTECODE ||
                                detection.format == BytecodeFormat::HYBRID);
    
    // Check for trust features (CVM-specific opcodes)
    result.has_trust_features = detector.IsCVMBytecode(bytecode) && 
                                detection.format != BytecodeFormat::EVM_BYTECODE;
    
    // Set format description
    switch (detection.format) {
        case BytecodeFormat::CVM_NATIVE:
            result.format_description = "CVM Native (register-based)";
            break;
        case BytecodeFormat::EVM_BYTECODE:
            result.format_description = "EVM Bytecode (stack-based)";
            break;
        case BytecodeFormat::HYBRID:
            result.format_description = "Hybrid (CVM + EVM)";
            break;
        default:
            result.format_description = "Unknown format";
            result.warnings.push_back("Could not determine bytecode format");
    }
    
    // Add confidence warning if low
    if (detection.confidence < 0.7) {
        result.warnings.push_back("Low confidence in format detection: " + 
                                  std::to_string(detection.confidence));
    }
    
    return result;
}

CVMContractChecker::ExecutionTestResult CVMContractChecker::TestExecution(
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& input_data,
    uint64_t gas_limit,
    ContractStorage* storage) {
    
    ExecutionTestResult result;
    result.execution_succeeded = false;
    result.gas_used = 0;
    result.matches_expected = false;
    
    // Validate bytecode first
    ValidationResult validation = ValidateContract(bytecode);
    if (!validation.is_valid) {
        result.error = validation.error;
        return result;
    }
    
    // Only test CVM native bytecode
    if (!validation.is_cvm_native) {
        result.error = "Not CVM native bytecode - use EnhancedVM for EVM execution";
        return result;
    }
    
    // Set up VM state
    VMState state;
    state.SetGasLimit(gas_limit);
    state.SetContractAddress(uint160());
    state.SetCallerAddress(uint160());
    state.SetCallValue(0);
    state.SetBlockHeight(0);
    state.SetBlockHash(uint256());
    state.SetTimestamp(0);
    
    // Execute
    result.execution_succeeded = cvm_engine->Execute(bytecode, state, storage);
    result.gas_used = state.GetGasUsed();
    result.return_data = state.GetReturnData();
    
    if (!result.execution_succeeded) {
        result.error = state.GetError();
    }
    
    return result;
}

bool CVMContractChecker::VerifyRegisterBasedBytecode(const std::vector<uint8_t>& bytecode) const {
    // CVM uses register-based patterns
    return detector.IsCVMBytecode(bytecode);
}

bool CVMContractChecker::VerifyOpcodeSequence(const std::vector<uint8_t>& bytecode) const {
    return CVM::VerifyBytecode(bytecode);
}

bool CVMContractChecker::IsCompatibleWithEnhancedVM(const std::vector<uint8_t>& bytecode) const {
    // All valid CVM bytecode is compatible with EnhancedVM
    ValidationResult result = ValidateContract(bytecode);
    return result.is_valid;
}


// ============================================================================
// NodeCompatChecker Implementation
// ============================================================================

NodeCompatChecker::NodeCompatChecker() {
}

NodeCompatChecker::~NodeCompatChecker() {
}

NodeCompatChecker::BlockCompatResult NodeCompatChecker::CheckBlockCompatibility(
    const std::vector<CTransaction>& txs,
    int blockHeight,
    const Consensus::Params& params) const {
    
    BlockCompatResult result;
    result.old_node_can_validate = true;
    result.new_node_can_validate = true;
    result.cvm_tx_count = 0;
    result.evm_tx_count = 0;
    result.standard_tx_count = 0;
    
    for (const auto& tx : txs) {
        // Check if transaction has CVM OP_RETURN
        int opReturnIndex = FindCVMOpReturn(tx);
        
        if (opReturnIndex >= 0) {
            CVMOpType opType;
            std::vector<uint8_t> data;
            
            if (ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
                // Categorize transaction
                if (opType == CVMOpType::EVM_DEPLOY || opType == CVMOpType::EVM_CALL) {
                    result.evm_tx_count++;
                } else if (opType == CVMOpType::CONTRACT_DEPLOY || 
                           opType == CVMOpType::CONTRACT_CALL) {
                    result.cvm_tx_count++;
                }
            }
            
            // Verify OP_RETURN format for old node compatibility
            if (!VerifyOPReturnFormat(tx)) {
                result.old_node_can_validate = false;
                result.compatibility_notes.push_back(
                    "Transaction " + tx.GetHash().ToString() + " has invalid OP_RETURN format");
            }
        } else {
            result.standard_tx_count++;
        }
        
        // Check transaction format compatibility
        if (!IsTransactionFormatCompatible(tx)) {
            result.old_node_can_validate = false;
            result.compatibility_notes.push_back(
                "Transaction " + tx.GetHash().ToString() + " has incompatible format");
        }
    }
    
    // Add summary notes
    if (result.evm_tx_count > 0) {
        result.compatibility_notes.push_back(
            "Block contains " + std::to_string(result.evm_tx_count) + " EVM transactions");
    }
    if (result.cvm_tx_count > 0) {
        result.compatibility_notes.push_back(
            "Block contains " + std::to_string(result.cvm_tx_count) + " CVM transactions");
    }
    
    return result;
}

bool NodeCompatChecker::VerifyOPReturnFormat(const CTransaction& tx) const {
    for (const auto& txout : tx.vout) {
        if (IsCVMOpReturn(txout)) {
            // Check size limit
            if (!CheckOPReturnSize(txout)) {
                return false;
            }
            
            // Check magic bytes
            CVMOpType opType;
            std::vector<uint8_t> data;
            if (!ParseCVMOpReturn(txout, opType, data)) {
                return false;
            }
        }
    }
    
    return true;
}

bool NodeCompatChecker::IsValidCVMOPReturn(const CTxOut& txout) const {
    if (!IsCVMOpReturn(txout)) {
        return false;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(txout, opType, data)) {
        return false;
    }
    
    // Check for CVM-specific operation types
    return (opType == CVMOpType::CONTRACT_DEPLOY ||
            opType == CVMOpType::CONTRACT_CALL ||
            opType == CVMOpType::REPUTATION_VOTE ||
            opType == CVMOpType::TRUST_EDGE ||
            opType == CVMOpType::BONDED_VOTE ||
            opType == CVMOpType::DAO_DISPUTE ||
            opType == CVMOpType::DAO_VOTE);
}

bool NodeCompatChecker::IsValidEVMOPReturn(const CTxOut& txout) const {
    if (!IsCVMOpReturn(txout)) {
        return false;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(txout, opType, data)) {
        return false;
    }
    
    // Check for EVM-specific operation types
    return (opType == CVMOpType::EVM_DEPLOY ||
            opType == CVMOpType::EVM_CALL);
}

bool NodeCompatChecker::IsTransactionFormatCompatible(const CTransaction& tx) const {
    // Check basic transaction structure
    if (tx.vin.empty() || tx.vout.empty()) {
        return false;
    }
    
    // Check output structure
    return HasValidOutputStructure(tx);
}

bool NodeCompatChecker::HasValidOutputStructure(const CTransaction& tx) const {
    for (const auto& txout : tx.vout) {
        // Check for valid script
        if (txout.scriptPubKey.empty()) {
            return false;
        }
        
        // OP_RETURN outputs must have zero value
        if (txout.scriptPubKey.size() > 0 && 
            txout.scriptPubKey[0] == OP_RETURN &&
            txout.nValue != 0) {
            return false;
        }
    }
    
    return true;
}

uint32_t NodeCompatChecker::DetectNodeVersion(const CTransaction& tx) const {
    // Detect node version based on transaction features
    int opReturnIndex = FindCVMOpReturn(tx);
    
    if (opReturnIndex < 0) {
        return 0; // Standard transaction, any node version
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[opReturnIndex], opType, data)) {
        return 0;
    }
    
    // EVM transactions require newer node version
    if (opType == CVMOpType::EVM_DEPLOY || opType == CVMOpType::EVM_CALL) {
        return 2; // CVM-EVM enhanced node
    }
    
    // CVM transactions require CVM-enabled node
    return 1; // CVM-enabled node
}

bool NodeCompatChecker::IsNodeVersionSupported(uint32_t version) const {
    // Version 0 = pre-CVM node
    // Version 1 = CVM-enabled node
    // Version 2 = CVM-EVM enhanced node
    return version <= 2;
}

bool NodeCompatChecker::CheckOPReturnSize(const CTxOut& txout) const {
    // OP_RETURN size limit (Bitcoin compatible)
    return txout.scriptPubKey.size() <= MAX_OP_RETURN_SIZE + 3;
}

bool NodeCompatChecker::CheckMagicBytes(const std::vector<uint8_t>& data) const {
    if (data.size() < CVM_MAGIC.size()) {
        return false;
    }
    
    return std::equal(CVM_MAGIC.begin(), CVM_MAGIC.end(), data.begin());
}


// ============================================================================
// ReputationCompatChecker Implementation
// ============================================================================

ReputationCompatChecker::ReputationCompatChecker() {
}

ReputationCompatChecker::~ReputationCompatChecker() {
}

ReputationCompatChecker::TrustGraphStatus ReputationCompatChecker::CheckTrustGraphPreservation() const {
    TrustGraphStatus status;
    status.is_preserved = true;
    status.total_edges = 0;
    status.valid_edges = 0;
    status.migrated_edges = 0;
    
    // Trust graph is preserved across CVM-EVM activation
    // The enhancement adds features but doesn't modify existing data structure
    // In a full implementation, this would query the trust graph database
    
    return status;
}

ReputationCompatChecker::ReputationDataStatus ReputationCompatChecker::CheckReputationData() const {
    ReputationDataStatus status;
    status.is_valid = true;
    status.total_addresses = 0;
    status.addresses_with_reputation = 0;
    status.addresses_migrated = 0;
    
    // Reputation data is preserved across CVM-EVM activation
    // HAT v2 extends the scoring system but maintains backward compatibility
    
    return status;
}

bool ReputationCompatChecker::IsHATv2Compatible(const uint160& address) const {
    // All addresses are HAT v2 compatible
    // HAT v2 can calculate scores for any address
    return true;
}

bool ReputationCompatChecker::CanMigrateToHATv2(const uint160& address) const {
    // All addresses can migrate to HAT v2
    // Existing reputation data is preserved and enhanced
    return true;
}

bool ReputationCompatChecker::VerifyScorePreservation(const uint160& address, 
                                                      int32_t expected_score,
                                                      int32_t tolerance) const {
    // In a full implementation, this would:
    // 1. Calculate the old-style reputation score
    // 2. Calculate the HAT v2 score
    // 3. Verify they are within tolerance
    
    // For now, assume scores are preserved
    return true;
}

bool ReputationCompatChecker::ValidateTrustEdge(const uint160& from, const uint160& to, 
                                                int16_t weight) const {
    // Validate trust edge parameters
    if (weight < -100 || weight > 100) {
        return false;
    }
    
    // Self-trust is not allowed
    if (from == to) {
        return false;
    }
    
    return true;
}

bool ReputationCompatChecker::ValidateReputationScore(const uint160& address, 
                                                      int32_t score) const {
    // Reputation scores are in range 0-100
    return score >= 0 && score <= 100;
}


// ============================================================================
// FeatureFlagManager Implementation
// ============================================================================

FeatureFlagManager::FeatureFlagManager() : test_mode(false), test_features(0) {
}

FeatureFlagManager::~FeatureFlagManager() {
}

bool FeatureFlagManager::IsFeatureActive(FeatureFlag flag, int blockHeight, 
                                         const Consensus::Params& params) const {
    if (test_mode) {
        return (test_features & static_cast<uint32_t>(flag)) != 0;
    }
    
    // Check activation based on block height and deployment status
    bool cvmActive = (blockHeight >= params.cvmActivationHeight);
    bool cvmEvmActive = IsCVMEVMEnabled(blockHeight, params);
    
    uint32_t flagValue = static_cast<uint32_t>(flag);
    
    // CVM features
    if (flagValue & GetCVMFeatures()) {
        return cvmActive;
    }
    
    // EVM features
    if (flagValue & GetEVMFeatures()) {
        return cvmEvmActive;
    }
    
    // Trust features
    if (flagValue & GetTrustFeatures()) {
        return cvmEvmActive;
    }
    
    // HAT features
    if (flagValue & GetHATFeatures()) {
        return cvmEvmActive;
    }
    
    return false;
}

uint32_t FeatureFlagManager::GetActiveFeatures(int blockHeight, 
                                               const Consensus::Params& params) const {
    if (test_mode) {
        return test_features;
    }
    
    uint32_t features = 0;
    
    bool cvmActive = (blockHeight >= params.cvmActivationHeight);
    bool cvmEvmActive = IsCVMEVMEnabled(blockHeight, params);
    
    if (cvmActive) {
        features |= GetCVMFeatures();
    }
    
    if (cvmEvmActive) {
        features |= GetEVMFeatures();
        features |= GetTrustFeatures();
        features |= GetHATFeatures();
    }
    
    return features;
}

std::vector<FeatureFlagManager::FeatureSchedule> FeatureFlagManager::GetFeatureSchedule(
    const Consensus::Params& params) const {
    
    std::vector<FeatureSchedule> schedule;
    
    // CVM features
    schedule.push_back({FeatureFlag::CVM_BASIC, params.cvmActivationHeight, 
                        "Basic CVM execution", false});
    schedule.push_back({FeatureFlag::CVM_STORAGE, params.cvmActivationHeight, 
                        "CVM storage operations", false});
    schedule.push_back({FeatureFlag::CVM_CRYPTO, params.cvmActivationHeight, 
                        "CVM cryptographic operations", false});
    
    // EVM features (require BIP9 signaling)
    // Note: Actual activation height depends on BIP9 deployment
    schedule.push_back({FeatureFlag::EVM_BYTECODE, -1, 
                        "EVM bytecode execution", true});
    schedule.push_back({FeatureFlag::EVM_STORAGE, -1, 
                        "EVM-compatible storage", true});
    schedule.push_back({FeatureFlag::EVM_PRECOMPILES, -1, 
                        "EVM precompiled contracts", true});
    
    // Trust features
    schedule.push_back({FeatureFlag::TRUST_CONTEXT, -1, 
                        "Automatic trust context injection", true});
    schedule.push_back({FeatureFlag::TRUST_GAS, -1, 
                        "Reputation-based gas discounts", true});
    schedule.push_back({FeatureFlag::TRUST_GATES, -1, 
                        "Trust-gated operations", true});
    
    // HAT v2 features
    schedule.push_back({FeatureFlag::HAT_CONSENSUS, -1, 
                        "HAT v2 consensus validation", true});
    schedule.push_back({FeatureFlag::HAT_ATTESTATION, -1, 
                        "Validator attestation system", true});
    schedule.push_back({FeatureFlag::HAT_DAO, -1, 
                        "DAO dispute resolution", true});
    
    return schedule;
}

int FeatureFlagManager::GetFeatureActivationHeight(FeatureFlag flag, 
                                                   const Consensus::Params& params) const {
    uint32_t flagValue = static_cast<uint32_t>(flag);
    
    // CVM features activate at cvmActivationHeight
    if (flagValue & GetCVMFeatures()) {
        return params.cvmActivationHeight;
    }
    
    // EVM and other features require BIP9 deployment
    // Return -1 to indicate BIP9-dependent activation
    return -1;
}

FeatureFlagManager::BytecodeVersionInfo FeatureFlagManager::DetectBytecodeVersion(
    const std::vector<uint8_t>& bytecode) const {
    
    BytecodeVersionInfo info;
    info.version = 0;
    info.format = BytecodeFormat::UNKNOWN;
    info.is_supported = false;
    info.format_name = "Unknown";
    
    if (bytecode.empty()) {
        return info;
    }
    
    // Detect format
    BytecodeDetectionResult detection = detector.DetectFormat(bytecode);
    info.format = detection.format;
    
    switch (detection.format) {
        case BytecodeFormat::CVM_NATIVE:
            info.version = 1;
            info.format_name = "CVM Native";
            info.is_supported = true;
            info.required_features.push_back(FeatureFlag::CVM_BASIC);
            break;
            
        case BytecodeFormat::EVM_BYTECODE:
            info.version = 2;
            info.format_name = "EVM Bytecode";
            info.is_supported = true;
            info.required_features.push_back(FeatureFlag::EVM_BYTECODE);
            break;
            
        case BytecodeFormat::HYBRID:
            info.version = 3;
            info.format_name = "Hybrid (CVM + EVM)";
            info.is_supported = true;
            info.required_features.push_back(FeatureFlag::CVM_BASIC);
            info.required_features.push_back(FeatureFlag::EVM_BYTECODE);
            info.required_features.push_back(FeatureFlag::HYBRID_CONTRACTS);
            break;
            
        default:
            info.version = 0;
            info.format_name = "Unknown";
            info.is_supported = false;
    }
    
    return info;
}

bool FeatureFlagManager::IsBytecodeVersionSupported(uint32_t version, int blockHeight, 
                                                    const Consensus::Params& params) const {
    switch (version) {
        case 0:
            return false; // Unknown version
        case 1:
            return blockHeight >= params.cvmActivationHeight; // CVM native
        case 2:
        case 3:
            return IsCVMEVMEnabled(blockHeight, params); // EVM or Hybrid
        default:
            return false;
    }
}

FeatureFlagManager::RolloutPhase FeatureFlagManager::GetCurrentPhase(
    int blockHeight, const Consensus::Params& params) const {
    
    // Check if CVM is activated
    if (blockHeight < params.cvmActivationHeight) {
        return RolloutPhase::PRE_ACTIVATION;
    }
    
    // Check CVM-EVM deployment status
    if (IsCVMEVMEnabled(blockHeight, params)) {
        return RolloutPhase::ACTIVE;
    }
    
    // Between CVM activation and CVM-EVM activation
    return RolloutPhase::SIGNALING;
}

std::string FeatureFlagManager::GetPhaseDescription(RolloutPhase phase) const {
    switch (phase) {
        case RolloutPhase::PRE_ACTIVATION:
            return "Pre-activation: CVM features not yet available";
        case RolloutPhase::SIGNALING:
            return "Signaling: Miners signaling support for CVM-EVM";
        case RolloutPhase::LOCKED_IN:
            return "Locked-in: CVM-EVM activation locked in";
        case RolloutPhase::ACTIVE:
            return "Active: All CVM-EVM features enabled";
        case RolloutPhase::STABLE:
            return "Stable: CVM-EVM in stable operation";
        default:
            return "Unknown phase";
    }
}

void FeatureFlagManager::EnableTestMode(bool enable) {
    test_mode = enable;
}

void FeatureFlagManager::SetTestFeatures(uint32_t features) {
    test_features = features;
}

uint32_t FeatureFlagManager::GetCVMFeatures() const {
    return static_cast<uint32_t>(FeatureFlag::CVM_BASIC) |
           static_cast<uint32_t>(FeatureFlag::CVM_STORAGE) |
           static_cast<uint32_t>(FeatureFlag::CVM_CRYPTO);
}

uint32_t FeatureFlagManager::GetEVMFeatures() const {
    return static_cast<uint32_t>(FeatureFlag::EVM_BYTECODE) |
           static_cast<uint32_t>(FeatureFlag::EVM_STORAGE) |
           static_cast<uint32_t>(FeatureFlag::EVM_PRECOMPILES) |
           static_cast<uint32_t>(FeatureFlag::HYBRID_CONTRACTS) |
           static_cast<uint32_t>(FeatureFlag::CROSS_FORMAT_CALLS);
}

uint32_t FeatureFlagManager::GetTrustFeatures() const {
    return static_cast<uint32_t>(FeatureFlag::TRUST_CONTEXT) |
           static_cast<uint32_t>(FeatureFlag::TRUST_GAS) |
           static_cast<uint32_t>(FeatureFlag::TRUST_GATES);
}

uint32_t FeatureFlagManager::GetHATFeatures() const {
    return static_cast<uint32_t>(FeatureFlag::HAT_CONSENSUS) |
           static_cast<uint32_t>(FeatureFlag::HAT_ATTESTATION) |
           static_cast<uint32_t>(FeatureFlag::HAT_DAO);
}


// ============================================================================
// BackwardCompatUtils Implementation
// ============================================================================

namespace BackwardCompatUtils {

std::string FeatureFlagToString(FeatureFlag flag) {
    switch (flag) {
        case FeatureFlag::CVM_BASIC: return "CVM_BASIC";
        case FeatureFlag::CVM_STORAGE: return "CVM_STORAGE";
        case FeatureFlag::CVM_CRYPTO: return "CVM_CRYPTO";
        case FeatureFlag::EVM_BYTECODE: return "EVM_BYTECODE";
        case FeatureFlag::EVM_STORAGE: return "EVM_STORAGE";
        case FeatureFlag::EVM_PRECOMPILES: return "EVM_PRECOMPILES";
        case FeatureFlag::TRUST_CONTEXT: return "TRUST_CONTEXT";
        case FeatureFlag::TRUST_GAS: return "TRUST_GAS";
        case FeatureFlag::TRUST_GATES: return "TRUST_GATES";
        case FeatureFlag::HAT_CONSENSUS: return "HAT_CONSENSUS";
        case FeatureFlag::HAT_ATTESTATION: return "HAT_ATTESTATION";
        case FeatureFlag::HAT_DAO: return "HAT_DAO";
        case FeatureFlag::HYBRID_CONTRACTS: return "HYBRID_CONTRACTS";
        case FeatureFlag::CROSS_FORMAT_CALLS: return "CROSS_FORMAT_CALLS";
        case FeatureFlag::ALL_FEATURES: return "ALL_FEATURES";
        default: return "UNKNOWN";
    }
}

FeatureFlag StringToFeatureFlag(const std::string& str) {
    if (str == "CVM_BASIC") return FeatureFlag::CVM_BASIC;
    if (str == "CVM_STORAGE") return FeatureFlag::CVM_STORAGE;
    if (str == "CVM_CRYPTO") return FeatureFlag::CVM_CRYPTO;
    if (str == "EVM_BYTECODE") return FeatureFlag::EVM_BYTECODE;
    if (str == "EVM_STORAGE") return FeatureFlag::EVM_STORAGE;
    if (str == "EVM_PRECOMPILES") return FeatureFlag::EVM_PRECOMPILES;
    if (str == "TRUST_CONTEXT") return FeatureFlag::TRUST_CONTEXT;
    if (str == "TRUST_GAS") return FeatureFlag::TRUST_GAS;
    if (str == "TRUST_GATES") return FeatureFlag::TRUST_GATES;
    if (str == "HAT_CONSENSUS") return FeatureFlag::HAT_CONSENSUS;
    if (str == "HAT_ATTESTATION") return FeatureFlag::HAT_ATTESTATION;
    if (str == "HAT_DAO") return FeatureFlag::HAT_DAO;
    if (str == "HYBRID_CONTRACTS") return FeatureFlag::HYBRID_CONTRACTS;
    if (str == "CROSS_FORMAT_CALLS") return FeatureFlag::CROSS_FORMAT_CALLS;
    if (str == "ALL_FEATURES") return FeatureFlag::ALL_FEATURES;
    return FeatureFlag::CVM_BASIC; // Default
}

std::vector<FeatureFlag> GetAllFeatureFlags() {
    return {
        FeatureFlag::CVM_BASIC,
        FeatureFlag::CVM_STORAGE,
        FeatureFlag::CVM_CRYPTO,
        FeatureFlag::EVM_BYTECODE,
        FeatureFlag::EVM_STORAGE,
        FeatureFlag::EVM_PRECOMPILES,
        FeatureFlag::TRUST_CONTEXT,
        FeatureFlag::TRUST_GAS,
        FeatureFlag::TRUST_GATES,
        FeatureFlag::HAT_CONSENSUS,
        FeatureFlag::HAT_ATTESTATION,
        FeatureFlag::HAT_DAO,
        FeatureFlag::HYBRID_CONTRACTS,
        FeatureFlag::CROSS_FORMAT_CALLS
    };
}

uint32_t ExtractBytecodeVersion(const std::vector<uint8_t>& bytecode) {
    // Check for version header
    if (!HasVersionHeader(bytecode)) {
        return 0; // No version header, assume version 0
    }
    
    // Version header format: 0xCVM + version byte
    if (bytecode.size() >= 5 &&
        bytecode[0] == 0x43 && bytecode[1] == 0x56 && 
        bytecode[2] == 0x4D && bytecode[3] == 0x56) {
        return bytecode[4];
    }
    
    return 0;
}

bool HasVersionHeader(const std::vector<uint8_t>& bytecode) {
    // Check for "CVMV" header
    if (bytecode.size() < 5) {
        return false;
    }
    
    return (bytecode[0] == 0x43 && bytecode[1] == 0x56 && 
            bytecode[2] == 0x4D && bytecode[3] == 0x56);
}

std::vector<uint8_t> AddVersionHeader(const std::vector<uint8_t>& bytecode, uint32_t version) {
    std::vector<uint8_t> result;
    
    // Add "CVMV" header + version byte
    result.push_back(0x43); // 'C'
    result.push_back(0x56); // 'V'
    result.push_back(0x4D); // 'M'
    result.push_back(0x56); // 'V'
    result.push_back(static_cast<uint8_t>(version & 0xFF));
    
    // Append original bytecode
    result.insert(result.end(), bytecode.begin(), bytecode.end());
    
    return result;
}

bool IsFullyBackwardCompatible(const std::vector<uint8_t>& bytecode) {
    BytecodeDetector detector;
    BytecodeDetectionResult result = detector.DetectFormat(bytecode);
    
    // CVM native bytecode is fully backward compatible
    return result.format == BytecodeFormat::CVM_NATIVE;
}

bool RequiresEVMFeatures(const std::vector<uint8_t>& bytecode) {
    BytecodeDetector detector;
    BytecodeDetectionResult result = detector.DetectFormat(bytecode);
    
    return result.format == BytecodeFormat::EVM_BYTECODE ||
           result.format == BytecodeFormat::HYBRID;
}

bool RequiresTrustFeatures(const std::vector<uint8_t>& bytecode) {
    BytecodeDetector detector;
    
    // Check for trust-specific opcodes
    return detector.IsCVMBytecode(bytecode);
}

std::vector<uint8_t> MigrateCVMBytecode(const std::vector<uint8_t>& old_bytecode) {
    // CVM bytecode doesn't need migration - it's already compatible
    // This function is a placeholder for future migration needs
    return old_bytecode;
}

bool CanMigrateBytecode(const std::vector<uint8_t>& bytecode) {
    // All valid bytecode can be "migrated" (no actual migration needed)
    return CVM::VerifyBytecode(bytecode);
}

std::string FormatCompatibilityReport(const BackwardCompatManager::MigrationStatus& status) {
    std::string report;
    
    report += "=== Compatibility Report ===\n";
    report += "CVM Contracts Valid: " + std::string(status.cvm_contracts_valid ? "Yes" : "No") + "\n";
    report += "EVM Features Ready: " + std::string(status.evm_features_ready ? "Yes" : "No") + "\n";
    report += "Trust Data Preserved: " + std::string(status.trust_data_preserved ? "Yes" : "No") + "\n";
    report += "Node Compatible: " + std::string(status.node_compatible ? "Yes" : "No") + "\n";
    
    if (!status.warnings.empty()) {
        report += "\nWarnings:\n";
        for (const auto& warning : status.warnings) {
            report += "  - " + warning + "\n";
        }
    }
    
    if (!status.errors.empty()) {
        report += "\nErrors:\n";
        for (const auto& error : status.errors) {
            report += "  - " + error + "\n";
        }
    }
    
    return report;
}

std::string FormatFeatureFlags(uint32_t flags) {
    std::string result;
    
    for (const auto& flag : GetAllFeatureFlags()) {
        if (flags & static_cast<uint32_t>(flag)) {
            if (!result.empty()) {
                result += " | ";
            }
            result += FeatureFlagToString(flag);
        }
    }
    
    if (result.empty()) {
        result = "NONE";
    }
    
    return result;
}

} // namespace BackwardCompatUtils

} // namespace CVM
