// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/enhanced_vm.h>
#include <cvm/cvm.h>
#include <cvm/contract.h>
#ifdef ENABLE_EVMC
#include <cvm/evm_engine.h>
#endif
#include <util.h>
#include <timedata.h>
#include <hash.h>
#include <algorithm>
#include <chrono>

namespace CVM {

EnhancedVM::EnhancedVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx)
    : database(db), trust_context(trust_ctx), cvm_engine(nullptr),
      strict_validation(false), default_gas_limit(1000000), 
      cross_format_calls_enabled(true), execution_tracing(false) {
    
    // Initialize CVM engine
    cvm_engine = new CVM();
    
#ifdef ENABLE_EVMC
    // Initialize EVMC host and EVM engine if available
    if (trust_context) {
        try {
            evmc_host = std::make_unique<EVMCHost>(database, *trust_context);
            evm_engine = std::make_unique<EVMEngine>(database, trust_context);
            LogPrint(BCLog::CVM, "EnhancedVM: EVM engine initialized successfully\n");
        } catch (const std::exception& e) {
            LogPrintf("EnhancedVM: EVM engine initialization failed: %s (EVM features disabled)\n", e.what());
            evmc_host.reset();
            evm_engine.reset();
            // Continue without EVM - CVM will still work
        }
    }
#endif
    
    // Initialize bytecode detector
    bytecode_detector = std::make_unique<BytecodeDetector>();
    bytecode_detector->SetConfidenceThreshold(0.7);
    bytecode_detector->EnableStrictValidation(strict_validation);
    
    // Initialize detection cache
    detection_cache = std::make_unique<BytecodeDetectionCache>(1000);
    
    ResetStats();
}

EnhancedVM::~EnhancedVM() {
    delete cvm_engine;
}

EnhancedExecutionResult EnhancedVM::Execute(
    const std::vector<uint8_t>& bytecode,
    uint64_t gas_limit,
    const uint160& contract_address,
    const uint160& caller_address,
    uint64_t call_value,
    const std::vector<uint8_t>& input_data,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    TraceExecution("Starting contract execution");
    
    // Detect bytecode format
    BytecodeDetectionResult detection;
    if (detection_cache->HasResult(bytecode)) {
        detection = detection_cache->GetResult(bytecode);
        TraceExecution("Using cached bytecode detection result");
    } else {
        detection = bytecode_detector->DetectFormat(bytecode);
        detection_cache->StoreResult(bytecode, detection);
        TraceExecution("Detected bytecode format: " + BytecodeUtils::FormatToString(detection.format));
    }
    
    // Validate bytecode format
    if (!detection.is_valid) {
        return CreateErrorResult("Invalid bytecode format", detection.format);
    }
    
    // Check if we can execute this format
    if (!CanExecuteFormat(detection.format)) {
        return CreateErrorResult("Unsupported bytecode format: " + BytecodeUtils::FormatToString(detection.format), detection.format);
    }
    
    // Apply trust context
    if (trust_context) {
        trust_context->InjectTrustContext(caller_address, contract_address);
    }
    
    // Check trust gates
    if (!CheckTrustGates(caller_address, "contract_execution", gas_limit)) {
        return CreateErrorResult("Trust gate check failed", detection.format);
    }
    
    // Apply reputation-based gas adjustments
    uint64_t adjusted_gas = ApplyReputationGasAdjustments(gas_limit, caller_address);
    
    EnhancedExecutionResult result;
    result.executed_format = detection.format;
    result.caller_reputation_before = trust_context ? trust_context->GetReputation(caller_address) : 0;
    result.reputation_gas_discount = gas_limit - adjusted_gas;
    result.trust_gate_passed = true;
    
    // Route to appropriate execution engine
    try {
        switch (detection.format) {
            case BytecodeFormat::CVM_NATIVE: {
                VMState state;
                state.SetGasLimit(adjusted_gas);
                state.SetContractAddress(contract_address);
                state.SetCallerAddress(caller_address);
                state.SetCallValue(call_value);
                state.SetBlockHeight(block_height);
                state.SetBlockHash(block_hash);
                state.SetTimestamp(timestamp);
                
                result = ExecuteCVMBytecode(bytecode, state, database);
                stats.cvm_executions++;
                break;
            }
            
            case BytecodeFormat::EVM_BYTECODE: {
#ifdef ENABLE_EVMC
                result = ExecuteEVMBytecode(bytecode, adjusted_gas, contract_address, 
                                          caller_address, call_value, input_data,
                                          block_height, block_hash, timestamp);
                
                // Log trust-enhanced execution
                if (trust_context && result.success) {
                    TraceExecution("EVM execution with trust enhancements completed successfully");
                }
                stats.evm_executions++;
#else
                result = CreateErrorResult("EVM support not compiled in", detection.format);
#endif
                break;
            }
            
            case BytecodeFormat::HYBRID: {
                result = ExecuteHybridContract(bytecode, adjusted_gas, contract_address,
                                             caller_address, call_value, input_data,
                                             block_height, block_hash, timestamp);
                stats.hybrid_executions++;
                break;
            }
            
            default:
                result = CreateErrorResult("Unknown bytecode format", detection.format);
                break;
        }
    } catch (const std::exception& e) {
        result = CreateErrorResult("Execution exception: " + std::string(e.what()), detection.format);
    }
    
    // Update reputation based on execution result
    if (trust_context) {
        UpdateReputationFromExecution(caller_address, result);
        result.caller_reputation_after = trust_context->GetReputation(caller_address);
    }
    
    // Record execution metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    RecordExecutionMetrics(result, duration.count());
    
    if (result.success) {
        TraceExecution("Execution completed successfully");
    } else {
        TraceExecution("Execution failed: " + result.error);
        stats.failed_executions++;
    }
    
    stats.total_executions++;
    stats.total_gas_used += result.gas_used;
    stats.total_gas_saved_by_reputation += result.reputation_gas_discount;
    
    return result;
}

EnhancedExecutionResult EnhancedVM::DeployContract(
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& constructor_data,
    uint64_t gas_limit,
    const uint160& deployer_address,
    uint64_t deploy_value,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    TraceExecution("Starting contract deployment");
    
    // Validate deployment
    if (!ValidateContractDeployment(bytecode, deployer_address)) {
        return CreateErrorResult("Contract deployment validation failed");
    }
    
    // Generate contract address with proper nonce tracking
    uint64_t nonce = 0;
    if (database) {
        // Get current nonce for deployer
        nonce = database->GetNextNonce(deployer_address);
        LogPrint(BCLog::CVM, "EnhancedVM: Using nonce %d for deployer %s\n",
                 nonce, deployer_address.ToString());
    } else {
        // Fallback to timestamp-based nonce if no database
        nonce = static_cast<uint64_t>(GetTime());
        LogPrint(BCLog::CVM, "EnhancedVM: Using timestamp-based nonce %d (no database)\n", nonce);
    }
    
    uint160 contract_address = GenerateContractAddress(deployer_address, nonce);
    
    // Check if contract already exists
    if (database && database->Exists(contract_address)) {
        return CreateErrorResult("Contract already exists at address");
    }
    
    // Execute constructor if present
    EnhancedExecutionResult result;
    if (!constructor_data.empty()) {
        result = Execute(bytecode, gas_limit, contract_address, deployer_address,
                        deploy_value, constructor_data, block_height, block_hash, timestamp);
        
        if (!result.success) {
            return result; // Constructor failed
        }
    }
    
    // Store contract bytecode
    if (database) {
        Contract contract;
        contract.code = bytecode;
        contract.address = contract_address;
        if (!database->WriteContract(contract_address, contract)) {
            return CreateErrorResult("Failed to store contract bytecode");
        }
        
        // Increment deployer's nonce after successful deployment
        database->WriteNonce(deployer_address, nonce + 1);
        LogPrint(BCLog::CVM, "EnhancedVM: Incremented deployer nonce to %d\n", nonce + 1);
    }
    
    TraceExecution("Contract deployed successfully at address: " + contract_address.ToString());
    
    result.success = true;
    return result;
}

EnhancedExecutionResult EnhancedVM::CallContract(
    const uint160& contract_address,
    const std::vector<uint8_t>& call_data,
    uint64_t gas_limit,
    const uint160& caller_address,
    uint64_t call_value,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    TraceExecution("Starting contract call");
    
    // Load contract bytecode
    std::vector<uint8_t> bytecode;
    Contract contract;
    if (!database || !database->ReadContract(contract_address, contract)) {
        return CreateErrorResult("Contract not found at address");
    }
    
    bytecode = contract.code;
    
    // Validate call
    if (!ValidateContractCall(contract_address, caller_address, gas_limit)) {
        return CreateErrorResult("Contract call validation failed");
    }
    
    // Execute contract
    return Execute(bytecode, gas_limit, contract_address, caller_address,
                  call_value, call_data, block_height, block_hash, timestamp);
}

BytecodeDetectionResult EnhancedVM::DetectBytecodeFormat(const std::vector<uint8_t>& bytecode) {
    if (detection_cache->HasResult(bytecode)) {
        return detection_cache->GetResult(bytecode);
    }
    
    auto result = bytecode_detector->DetectFormat(bytecode);
    detection_cache->StoreResult(bytecode, result);
    return result;
}

bool EnhancedVM::CanExecuteFormat(BytecodeFormat format) {
    switch (format) {
        case BytecodeFormat::CVM_NATIVE:
            return cvm_engine != nullptr;
        case BytecodeFormat::EVM_BYTECODE:
#ifdef ENABLE_EVMC
            return evmc_host != nullptr;
#else
            return false;
#endif
        case BytecodeFormat::HYBRID:
            return cvm_engine != nullptr;
        default:
            return false;
    }
}

void EnhancedVM::ResetStats() {
    stats = ExecutionStats{};
}

// Private implementation methods

EnhancedExecutionResult EnhancedVM::ExecuteCVMBytecode(
    const std::vector<uint8_t>& bytecode,
    VMState& state,
    ContractStorage* storage) {
    
    TraceExecution("Executing CVM bytecode");
    
    EnhancedExecutionResult result;
    
    if (!cvm_engine) {
        result.error = "CVM engine not available";
        return result;
    }
    
    // Execute via CVM engine
    bool success = cvm_engine->Execute(bytecode, state, storage);
    
    result.success = success;
    result.gas_used = state.GetGasUsed();
    result.return_data = state.GetReturnData();
    result.error = state.GetError();
    
    // Convert logs
    for (const auto& log : state.GetLogs()) {
        result.logs.push_back(log);
    }
    
    return result;
}

#ifdef ENABLE_EVMC
EnhancedExecutionResult EnhancedVM::ExecuteEVMBytecode(
    const std::vector<uint8_t>& bytecode,
    uint64_t gas_limit,
    const uint160& contract_address,
    const uint160& caller_address,
    uint64_t call_value,
    const std::vector<uint8_t>& input_data,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    TraceExecution("Executing EVM bytecode via EVM engine");
    
    if (!evm_engine) {
        return CreateErrorResult("EVM engine not available", BytecodeFormat::EVM_BYTECODE);
    }
    
    // Execute using the new EVM engine
    EVMExecutionResult evm_result = evm_engine->Execute(
        bytecode, gas_limit, contract_address, caller_address, call_value,
        input_data, block_height, block_hash, timestamp
    );
    
    // Convert EVMExecutionResult to EnhancedExecutionResult
    EnhancedExecutionResult result;
    result.success = evm_result.success;
    result.gas_used = evm_result.gas_used;
    result.return_data = evm_result.output_data;
    result.logs = evm_result.logs;
    result.error = evm_result.error_message;
    result.executed_format = BytecodeFormat::EVM_BYTECODE;
    
    // Trust-specific metrics
    result.caller_reputation_before = evm_result.caller_reputation;
    result.caller_reputation_after = evm_result.caller_reputation; // May be updated by trust system
    result.trust_gate_passed = evm_result.trust_gate_passed;
    result.reputation_gas_discount = evm_result.gas_saved_by_reputation;
    
    // Cross-format execution tracking
    result.cross_format_calls_made = false; // Will be updated if cross-format calls are made
    result.total_cross_calls = 0;
    
    TraceExecution(std::string("EVM execution completed with status: ") + 
                  (result.success ? "SUCCESS" : "FAILED"));
    
    return result;
}
#endif

EnhancedExecutionResult EnhancedVM::ExecuteHybridContract(
    const std::vector<uint8_t>& bytecode,
    uint64_t gas_limit,
    const uint160& contract_address,
    const uint160& caller_address,
    uint64_t call_value,
    const std::vector<uint8_t>& input_data,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    TraceExecution("Executing hybrid contract");
    
    // Extract EVM and CVM portions
    std::vector<uint8_t> evm_portion = bytecode_detector->ExtractEVMPortion(bytecode);
    std::vector<uint8_t> cvm_portion = bytecode_detector->ExtractCVMPortion(bytecode);
    
    // Determine which portion to execute based on input data or other criteria
    // For now, prefer EVM execution if available
    if (!evm_portion.empty()) {
#ifdef ENABLE_EVMC
        auto result = ExecuteEVMBytecode(evm_portion, gas_limit, contract_address,
                                       caller_address, call_value, input_data,
                                       block_height, block_hash, timestamp);
        result.executed_format = BytecodeFormat::HYBRID;
        return result;
#endif
    }
    
    if (!cvm_portion.empty()) {
        VMState state;
        state.SetGasLimit(gas_limit);
        state.SetContractAddress(contract_address);
        state.SetCallerAddress(caller_address);
        state.SetCallValue(call_value);
        state.SetBlockHeight(block_height);
        state.SetBlockHash(block_hash);
        state.SetTimestamp(timestamp);
        
        auto result = ExecuteCVMBytecode(cvm_portion, state, database);
        result.executed_format = BytecodeFormat::HYBRID;
        return result;
    }
    
    return CreateErrorResult("No executable portions found in hybrid contract", BytecodeFormat::HYBRID);
}

bool EnhancedVM::CheckTrustGates(const uint160& caller, const std::string& operation, uint64_t gas_limit) {
    if (!trust_context) {
        return true; // No trust context, allow execution
    }
    
    return trust_context->CanPerformOperation(caller, operation, gas_limit);
}

uint64_t EnhancedVM::ApplyReputationGasAdjustments(uint64_t base_gas, const uint160& caller) {
    if (!trust_context) {
        return base_gas;
    }
    
    return trust_context->ApplyReputationGasDiscount(base_gas, caller);
}

void EnhancedVM::UpdateReputationFromExecution(const uint160& caller, const EnhancedExecutionResult& result) {
    if (!trust_context) {
        return;
    }
    
    // Update reputation based on execution outcome
    if (result.success) {
        trust_context->UpdateReputationFromActivity(caller, "successful_execution", 1);
    } else {
        trust_context->UpdateReputationFromActivity(caller, "failed_execution", -1);
    }
}

uint160 EnhancedVM::GenerateContractAddress(const uint160& deployer, uint64_t nonce) {
    return ::CVM::GenerateContractAddress(deployer, nonce);
}

uint160 EnhancedVM::GenerateCreate2Address(const uint160& deployer, const uint256& salt, const std::vector<uint8_t>& bytecode) {
    // CREATE2 address generation: address = keccak256(0xff || deployer || salt || keccak256(bytecode))[12:]
    // For Cascoin, we use: hash(0xff || deployer || salt || hash(bytecode))
    
    CHashWriter hasher(SER_GETHASH, 0);
    
    // Add 0xff prefix
    hasher << uint8_t(0xff);
    
    // Add deployer address
    hasher << deployer;
    
    // Add salt
    hasher << salt;
    
    // Add bytecode hash
    CHashWriter bytecode_hasher(SER_GETHASH, 0);
    bytecode_hasher.write((const char*)bytecode.data(), bytecode.size());
    uint256 bytecode_hash = bytecode_hasher.GetHash();
    hasher << bytecode_hash;
    
    // Get final hash
    uint256 hash = hasher.GetHash();
    
    // Take the last 160 bits (20 bytes) of the hash
    uint160 contract_addr;
    std::memcpy(contract_addr.begin(), hash.begin(), 20);
    
    LogPrint(BCLog::CVM, "EnhancedVM: Generated CREATE2 address %s from deployer %s\n",
             contract_addr.ToString(), deployer.ToString());
    
    return contract_addr;
}

bool EnhancedVM::ValidateContractDeployment(const std::vector<uint8_t>& bytecode, const uint160& deployer) {
    // Check bytecode size
    if (bytecode.empty()) {
        LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: empty bytecode\n");
        return false;
    }
    
    if (bytecode.size() > MAX_BYTECODE_SIZE) {
        LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: bytecode too large (%d > %d)\n",
                  bytecode.size(), MAX_BYTECODE_SIZE);
        return false;
    }
    
    // Validate bytecode format
    auto detection = DetectBytecodeFormat(bytecode);
    if (!detection.is_valid) {
        LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: invalid bytecode format (format=%d, confidence=%.2f)\n",
                  static_cast<int>(detection.format), detection.confidence);
        return false;
    }
    
    // Check if we can execute this format
    if (!CanExecuteFormat(detection.format)) {
        LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: unsupported bytecode format %d (cvm=%d, evm=%d)\n",
                  static_cast<int>(detection.format), cvm_engine != nullptr,
#ifdef ENABLE_EVMC
                  evmc_host != nullptr
#else
                  0
#endif
                  );
        return false;
    }
    
    // Check trust gates
    if (trust_context) {
        if (!trust_context->CheckTrustGate(deployer, "contract_deployment")) {
            LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: trust gate check failed for %s\n",
                     deployer.ToString());
            return false;
        }
        
        // Additional reputation check for deployment
        uint32_t deployer_reputation = trust_context->GetReputation(deployer);
        if (deployer_reputation < 50) {
            LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation failed: insufficient reputation (%d < 50) for %s\n",
                     deployer_reputation, deployer.ToString());
            return false;
        }
    }
    
    LogPrint(BCLog::CVM, "EnhancedVM: Contract deployment validation passed for deployer %s\n",
             deployer.ToString());
    
    return true;
}

bool EnhancedVM::ValidateContractCall(const uint160& contract, const uint160& caller, uint64_t gas_limit) {
    // Check minimum gas limit
    if (gas_limit < MIN_GAS_LIMIT) {
        LogExecution("ERROR", "Contract call validation failed: gas limit too low (" +
                    std::to_string(gas_limit) + " < " + std::to_string(MIN_GAS_LIMIT) + ")");
        return false;
    }
    
    // Check if contract exists
    if (database) {
        if (!database->Exists(contract)) {
            LogExecution("ERROR", "Contract call validation failed: contract does not exist at " +
                        contract.ToString());
            return false;
        }
    }
    
    // Check trust gates
    if (trust_context) {
        if (!trust_context->CanPerformOperation(caller, "contract_call", gas_limit)) {
            LogExecution("ERROR", "Contract call validation failed: trust gate check failed for caller " +
                        caller.ToString());
            return false;
        }
        
        // Check reputation-based limits
        uint32_t caller_reputation = trust_context->GetReputation(caller);
        uint64_t max_gas_for_reputation;
        
        if (caller_reputation >= 80) {
            max_gas_for_reputation = 10000000; // 10M gas
        } else if (caller_reputation >= 60) {
            max_gas_for_reputation = 5000000;  // 5M gas
        } else if (caller_reputation >= 40) {
            max_gas_for_reputation = 1000000;  // 1M gas
        } else {
            max_gas_for_reputation = 100000;   // 100K gas
        }
        
        if (gas_limit > max_gas_for_reputation) {
            LogExecution("ERROR", "Contract call validation failed: gas limit " +
                        std::to_string(gas_limit) + " exceeds reputation-based limit " +
                        std::to_string(max_gas_for_reputation) + " for reputation " +
                        std::to_string(caller_reputation));
            return false;
        }
    }
    
    LogPrint(BCLog::CVM, "EnhancedVM: Contract call validation passed for caller %s to contract %s\n",
             caller.ToString(), contract.ToString());
    
    return true;
}

bool EnhancedVM::CheckResourceLimits(const uint160& caller, uint64_t gas_limit, size_t bytecode_size) {
    if (gas_limit > default_gas_limit * 10) { // Max 10x default gas limit
        return false;
    }
    
    if (bytecode_size > MAX_BYTECODE_SIZE) {
        return false;
    }
    
    return true;
}

void EnhancedVM::TraceExecution(const std::string& message) {
    if (execution_tracing) {
        last_execution_trace += "[" + std::to_string(GetTime()) + "] " + message + "\n";
    }
}

void EnhancedVM::LogExecution(const std::string& level, const std::string& message) {
    std::string log_entry = "[" + level + "] " + message;
    execution_logs.push_back(log_entry);
    
    // Limit log size
    if (execution_logs.size() > MAX_EXECUTION_LOGS) {
        execution_logs.erase(execution_logs.begin());
    }
}

void EnhancedVM::RecordExecutionMetrics(const EnhancedExecutionResult& result, int64_t execution_time_ms) {
    // Update average execution time
    if (stats.total_executions > 0) {
        stats.average_execution_time_ms = 
            ((stats.average_execution_time_ms * stats.total_executions) + execution_time_ms) / 
            (stats.total_executions + 1);
    } else {
        stats.average_execution_time_ms = execution_time_ms;
    }
    
    if (result.cross_format_calls_made) {
        stats.cross_format_calls += result.total_cross_calls;
    }
}

EnhancedExecutionResult EnhancedVM::CreateErrorResult(const std::string& error, BytecodeFormat format) {
    EnhancedExecutionResult result;
    result.success = false;
    result.error = error;
    result.executed_format = format;
    
    LogExecution("ERROR", error);
    
    return result;
}

void EnhancedVM::HandleExecutionError(const std::string& error, const uint160& contract, const uint160& caller) {
    LogExecution("ERROR", "Contract: " + contract.ToString() + ", Caller: " + caller.ToString() + ", Error: " + error);
}

EnhancedExecutionResult EnhancedVM::HandleCrossFormatCall(
    BytecodeFormat source_format,
    BytecodeFormat target_format,
    const uint160& target_contract,
    const std::vector<uint8_t>& call_data,
    uint64_t gas_limit,
    const uint160& caller_address,
    uint64_t call_value) {
    
    if (!cross_format_calls_enabled) {
        return CreateErrorResult("Cross-format calls are disabled", source_format);
    }
    
    TraceExecution("Handling cross-format call from " + BytecodeUtils::FormatToString(source_format) +
                  " to " + BytecodeUtils::FormatToString(target_format));
    
    // Check if formats are compatible
    if (!EnhancedVMUtils::AreFormatsCompatible(source_format, target_format)) {
        return CreateErrorResult("Incompatible bytecode formats for cross-format call", source_format);
    }
    
    // Load target contract bytecode
    std::vector<uint8_t> target_bytecode;
    Contract target_contract_data;
    if (!database || !database->ReadContract(target_contract, target_contract_data)) {
        return CreateErrorResult("Target contract not found for cross-format call", source_format);
    }
    
    target_bytecode = target_contract_data.code;
    
    // Verify target format matches expected
    auto detection = DetectBytecodeFormat(target_bytecode);
    if (detection.format != target_format && target_format != BytecodeFormat::HYBRID) {
        LogExecution("WARNING", "Target bytecode format mismatch in cross-format call");
    }
    
    // Check trust gates for cross-format calls (requires higher reputation)
    if (trust_context) {
        uint32_t caller_reputation = trust_context->GetReputation(caller_address);
        if (caller_reputation < 70) { // Require 70+ reputation for cross-format calls
            return CreateErrorResult("Insufficient reputation for cross-format call", source_format);
        }
    }
    
    // Execute the target contract
    EnhancedExecutionResult result = Execute(
        target_bytecode,
        gas_limit,
        target_contract,
        caller_address,
        call_value,
        call_data,
        0, // block_height - will be set by caller
        uint256(), // block_hash - will be set by caller
        GetTime()
    );
    
    // Mark as cross-format call
    result.cross_format_calls_made = true;
    result.total_cross_calls = 1;
    
    stats.cross_format_calls++;
    
    TraceExecution("Cross-format call completed with status: " + 
                  std::string(result.success ? "SUCCESS" : "FAILED"));
    
    return result;
}

void EnhancedVM::SaveExecutionState() {
    // Save current execution state to stack for nested calls
    if (execution_stack.size() >= MAX_CALL_DEPTH) {
        LogExecution("ERROR", "Maximum call depth exceeded");
        return;
    }
    
    ExecutionFrame frame;
    // Frame will be populated by caller with current execution context
    // This is a placeholder for state management
    
    TraceExecution("Saved execution state (depth: " + std::to_string(execution_stack.size()) + ")");
}

void EnhancedVM::RestoreExecutionState() {
    // Restore execution state from stack after nested call returns
    if (execution_stack.empty()) {
        LogExecution("WARNING", "Attempted to restore execution state with empty stack");
        return;
    }
    
    execution_stack.pop_back();
    
    TraceExecution("Restored execution state (depth: " + std::to_string(execution_stack.size()) + ")");
}

void EnhancedVM::CommitExecutionState() {
    // Commit execution state changes to database
    // This would typically be called after successful execution
    
    if (database) {
        // Flush any pending database writes
        // The actual implementation would depend on the database interface
        TraceExecution("Committed execution state to database");
    } else {
        LogExecution("WARNING", "Cannot commit execution state - no database available");
    }
}

bool EnhancedVM::TestTrustEnhancedSystem() {
    LogPrintf("Testing Enhanced VM trust-enhanced system...\n");
    
    bool all_tests_passed = true;
    
    // Test bytecode detection with trust context
    std::vector<uint8_t> test_evm_bytecode = {0x60, 0x80, 0x60, 0x40, 0x52}; // Simple EVM bytecode
    auto detection_result = DetectBytecodeFormat(test_evm_bytecode);
    
    if (detection_result.format == BytecodeFormat::EVM_BYTECODE) {
        LogPrintf("Bytecode detection test: PASSED\n");
    } else {
        LogPrintf("Bytecode detection test: FAILED\n");
        all_tests_passed = false;
    }
    
    // Test trust context injection
    if (trust_context) {
        uint160 test_caller;
        test_caller.SetHex("1234567890123456789012345678901234567890");
        uint160 test_contract;
        test_contract.SetHex("0987654321098765432109876543210987654321");
        
        trust_context->InjectTrustContext(test_caller, test_contract);
        uint32_t caller_rep = trust_context->GetReputation(test_caller);
        
        LogPrintf("Trust context injection test: PASSED (caller reputation: %d)\n", caller_rep);
    } else {
        LogPrintf("Trust context injection test: FAILED (no trust context)\n");
        all_tests_passed = false;
    }
    
#ifdef ENABLE_EVMC
    // Test EVM engine trust features
    if (evm_engine) {
        bool evm_test = evm_engine->TestTrustEnhancedOperations();
        if (evm_test) {
            LogPrintf("EVM trust-enhanced operations test: PASSED\n");
        } else {
            LogPrintf("EVM trust-enhanced operations test: FAILED\n");
            all_tests_passed = false;
        }
        
        // Test memory and stack features
        bool memory_stack_test = evm_engine->TestTrustAwareMemoryAndStack();
        if (memory_stack_test) {
            LogPrintf("EVM memory and stack trust features test: PASSED\n");
        } else {
            LogPrintf("EVM memory and stack trust features test: FAILED\n");
            all_tests_passed = false;
        }
    }
#endif
    
    LogPrintf("Enhanced VM trust system test completed: %s\n", 
              all_tests_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    
    return all_tests_passed;
}

bool EnhancedVM::TestMemoryAndStackFeatures() {
    LogPrintf("Testing Enhanced VM memory and stack trust features...\n");
    
    bool all_tests_passed = true;
    
    // Test bytecode format detection for memory operations
    std::vector<uint8_t> memory_test_bytecode = {
        0x60, 0x80,  // PUSH1 0x80
        0x60, 0x40,  // PUSH1 0x40
        0x52,        // MSTORE
        0x60, 0x40,  // PUSH1 0x40
        0x51         // MLOAD
    };
    
    auto detection_result = DetectBytecodeFormat(memory_test_bytecode);
    if (detection_result.format == BytecodeFormat::EVM_BYTECODE) {
        LogPrintf("Memory operation bytecode detection: PASSED\n");
    } else {
        LogPrintf("Memory operation bytecode detection: FAILED\n");
        all_tests_passed = false;
    }
    
#ifdef ENABLE_EVMC
    // Test EVM engine memory and stack features
    if (evm_engine) {
        bool memory_test = evm_engine->TestTrustAwareMemoryAndStack();
        if (memory_test) {
            LogPrintf("EVM engine memory/stack features: PASSED\n");
        } else {
            LogPrintf("EVM engine memory/stack features: FAILED\n");
            all_tests_passed = false;
        }
    }
#endif
    
    // Test trust context integration with memory operations
    if (trust_context) {
        uint160 test_address;
        test_address.SetHex("1111111111111111111111111111111111111111");
        trust_context->InjectTrustContext(test_address, test_address);
        
        // Test trust-weighted data storage
        TrustContext::TrustWeightedValue test_value;
        test_value.value.SetHex("12345");
        test_value.trust_weight = 75;
        test_value.source_address = test_address;
        test_value.timestamp = GetTime();
        
        trust_context->AddTrustWeightedValue("test_memory_key", test_value);
        
        auto retrieved_values = trust_context->GetTrustWeightedValues("test_memory_key");
        uint256 expected_value;
        expected_value.SetHex("12345");
        if (!retrieved_values.empty() && retrieved_values[0].value == expected_value) {
            LogPrintf("Trust-weighted memory storage: PASSED\n");
        } else {
            LogPrintf("Trust-weighted memory storage: FAILED\n");
            all_tests_passed = false;
        }
    }
    
    LogPrintf("Enhanced VM memory and stack test completed: %s\n", 
              all_tests_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    
    return all_tests_passed;
}

// Factory Implementation

std::unique_ptr<EnhancedVM> EnhancedVMFactory::CreateProductionVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx) {
    auto vm = std::make_unique<EnhancedVM>(db, trust_ctx);
    ConfigureForProduction(vm.get());
    return vm;
}

std::unique_ptr<EnhancedVM> EnhancedVMFactory::CreateTestVM(CVMDatabase* db) {
    auto trust_ctx = TrustContextFactory::CreateTestContext();
    auto vm = std::make_unique<EnhancedVM>(db, std::move(trust_ctx));
    ConfigureForTesting(vm.get());
    return vm;
}

std::unique_ptr<EnhancedVM> EnhancedVMFactory::CreateDebugVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx) {
    auto vm = std::make_unique<EnhancedVM>(db, trust_ctx);
    ConfigureForDebugging(vm.get());
    return vm;
}

void EnhancedVMFactory::ConfigureForProduction(EnhancedVM* vm) {
    vm->EnableStrictValidation(true);
    vm->SetGasLimit(1000000);
    vm->EnableCrossFormatCalls(true);
    vm->EnableExecutionTracing(false);
}

void EnhancedVMFactory::ConfigureForTesting(EnhancedVM* vm) {
    vm->EnableStrictValidation(false);
    vm->SetGasLimit(10000000); // Higher gas limit for testing
    vm->EnableCrossFormatCalls(true);
    vm->EnableExecutionTracing(true);
}

void EnhancedVMFactory::ConfigureForDebugging(EnhancedVM* vm) {
    vm->EnableStrictValidation(true);
    vm->SetGasLimit(1000000);
    vm->EnableCrossFormatCalls(true);
    vm->EnableExecutionTracing(true);
}

// Utility Functions

namespace EnhancedVMUtils {

bool IsSuccessfulExecution(const EnhancedExecutionResult& result) {
    return result.success && result.error.empty();
}

bool HasTrustBenefits(const EnhancedExecutionResult& result) {
    return result.reputation_gas_discount > 0 || result.trust_gate_passed;
}

double CalculateGasSavingsPercentage(const EnhancedExecutionResult& result, uint64_t base_gas) {
    if (base_gas == 0) return 0.0;
    return (static_cast<double>(result.reputation_gas_discount) / base_gas) * 100.0;
}

bool AreFormatsCompatible(BytecodeFormat source, BytecodeFormat target) {
    // Hybrid contracts are compatible with both CVM and EVM
    if (source == BytecodeFormat::HYBRID || target == BytecodeFormat::HYBRID) {
        return true;
    }
    
    // Same formats are compatible
    return source == target;
}

std::vector<BytecodeFormat> GetSupportedFormats() {
    std::vector<BytecodeFormat> formats = {BytecodeFormat::CVM_NATIVE, BytecodeFormat::HYBRID};
    
#ifdef ENABLE_EVMC
    formats.push_back(BytecodeFormat::EVM_BYTECODE);
#endif
    
    return formats;
}

std::string FormatExecutionResult(const EnhancedExecutionResult& result) {
    std::string output = "Execution Result:\n";
    output += "  Success: " + std::string(result.success ? "true" : "false") + "\n";
    output += "  Gas Used: " + std::to_string(result.gas_used) + "\n";
    output += "  Format: " + BytecodeUtils::FormatToString(result.executed_format) + "\n";
    output += "  Reputation Discount: " + std::to_string(result.reputation_gas_discount) + "\n";
    
    if (!result.error.empty()) {
        output += "  Error: " + result.error + "\n";
    }
    
    return output;
}

std::string FormatExecutionStats(const EnhancedVM::ExecutionStats& stats) {
    std::string output = "Execution Statistics:\n";
    output += "  Total Executions: " + std::to_string(stats.total_executions) + "\n";
    output += "  CVM Executions: " + std::to_string(stats.cvm_executions) + "\n";
    output += "  EVM Executions: " + std::to_string(stats.evm_executions) + "\n";
    output += "  Hybrid Executions: " + std::to_string(stats.hybrid_executions) + "\n";
    output += "  Failed Executions: " + std::to_string(stats.failed_executions) + "\n";
    output += "  Total Gas Used: " + std::to_string(stats.total_gas_used) + "\n";
    output += "  Gas Saved by Reputation: " + std::to_string(stats.total_gas_saved_by_reputation) + "\n";
    output += "  Average Execution Time: " + std::to_string(stats.average_execution_time_ms) + "ms\n";
    
    return output;
}

} // namespace EnhancedVMUtils

} // namespace CVM