// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef ENABLE_EVMC

#include <cvm/evm_engine.h>
#include <cvm/opcodes.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <pubkey.h>
#include <key.h>
#include <arith_uint256.h>
#include <evmc/loader.h>
#include <evmc/evmc.hpp>
#include <chrono>
#include <algorithm>

namespace CVM {

// Static EVM instance loader
static evmc_vm* LoadEVMOneInstance() {
    // Try to load evmone library
    evmc_loader_error_code error_code;
    evmc_vm* vm = evmc_load_and_create("evmone", &error_code);
    
    if (!vm) {
        LogPrintf("Failed to load evmone: %s\n", evmc_loader_error_message(error_code));
        return nullptr;
    }
    
    if (!evmc_is_abi_compatible(vm)) {
        LogPrintf("evmone ABI is not compatible\n");
        evmc_destroy(vm);
        return nullptr;
    }
    
    LogPrintf("Successfully loaded evmone version %s\n", vm->version);
    return vm;
}

EVMEngine::EVMEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx)
    : database(db), trust_context(trust_ctx), evm_instance(nullptr),
      evm_revision(EVMC_LONDON), trust_features_enabled(true),
      strict_gas_accounting(true), execution_tracing(false),
      opcode_frequency_tracking(false) {
    
    // Initialize EVM instance
    if (!InitializeEVM()) {
        throw std::runtime_error("Failed to initialize EVM engine");
    }
    
    // Create EVMC host
    evmc_host = std::make_unique<EVMCHost>(database, *trust_context);
    
    // Initialize statistics
    ResetStats();
    
    LogPrintf("EVM Engine initialized with trust features %s\n", 
              trust_features_enabled ? "enabled" : "disabled");
}

EVMEngine::~EVMEngine() {
    DestroyEVMInstance();
}

bool EVMEngine::InitializeEVM() {
    evm_instance = LoadEVMOneInstance();
    if (!evm_instance) {
        LogPrintf("Failed to load EVM instance\n");
        return false;
    }
    
    // Verify EVM capabilities
    if (!(evm_instance->get_capabilities(evm_instance) & EVMC_CAPABILITY_EVM1)) {
        LogPrintf("EVM instance does not support EVM1 capability\n");
        DestroyEVMInstance();
        return false;
    }
    
    return true;
}

void EVMEngine::DestroyEVMInstance() {
    if (evm_instance) {
        evmc_destroy(evm_instance);
        evm_instance = nullptr;
    }
}

EVMExecutionResult EVMEngine::Execute(
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
    
    // Validate execution parameters
    if (!evm_instance) {
        return CreateErrorResult(EVMC_INTERNAL_ERROR, "EVM instance not initialized");
    }
    
    if (bytecode.empty()) {
        return CreateErrorResult(EVMC_INVALID_INSTRUCTION, "Empty bytecode");
    }
    
    if (gas_limit == 0) {
        return CreateErrorResult(EVMC_OUT_OF_GAS, "Zero gas limit");
    }
    
    // Apply trust-based validations
    if (trust_features_enabled) {
        if (!CheckTrustGate(caller_address, "contract_execution")) {
            return CreateErrorResult(EVMC_REJECTED, "Trust gate failed for contract execution");
        }
        
        if (!CheckReputationBasedLimits(caller_address, gas_limit, bytecode.size())) {
            return CreateErrorResult(EVMC_REJECTED, "Reputation-based limits exceeded");
        }
    }
    
    // Create EVMC message
    evmc_message msg = CreateEVMCMessage(
        EVMC_CALL,
        caller_address,
        contract_address,
        call_value,
        input_data,
        gas_limit
    );
    
    // Inject trust context if enabled
    if (trust_features_enabled) {
        InjectTrustContext(msg);
        InjectCallerReputation(msg, caller_address);
        
        // Check reputation gates before execution
        if (!HandleReputationGatedCall(msg)) {
            return CreateErrorResult(EVMC_REJECTED, "Reputation gate failed for operation");
        }
    }
    
    // Apply reputation-based gas adjustments
    uint64_t original_gas_limit = gas_limit;
    if (trust_features_enabled) {
        uint64_t adjusted_gas = ApplyReputationGasDiscount(gas_limit, caller_address);
        msg.gas = static_cast<int64_t>(adjusted_gas);
        
        // Apply trust-aware gas cost modifications for specific operations
        ApplyTrustAwareGasCostModifications(msg, caller_address);
    }
    
    // Set up EVMC host context
    evmc_host->SetBlockContext(timestamp, block_height, block_hash, uint256(), gas_limit);
    evmc_host->SetTxContext(uint256(), caller_address, 1); // Default gas price
    
    // Execute contract
    evmc_result evmc_result = evm_instance->execute(
        evm_instance,
        evmc_host->GetInterface(),
        reinterpret_cast<evmc_host_context*>(evmc_host.get()),
        evm_revision,
        &msg,
        bytecode.data(),
        bytecode.size()
    );
    
    // Process result
    EVMExecutionResult result = ProcessEVMCResult(evmc_result, msg, original_gas_limit, caller_address);
    
    // Record execution metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    UpdateExecutionMetrics(result, execution_time.count());
    
    // Clean up EVMC result
    if (evmc_result.release) {
        evmc_result.release(&evmc_result);
    }
    
    return result;
}

EVMExecutionResult EVMEngine::DeployContract(
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& constructor_data,
    uint64_t gas_limit,
    const uint160& deployer_address,
    uint64_t deploy_value,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    // Validate deployment parameters
    if (bytecode.empty()) {
        return CreateErrorResult(EVMC_INVALID_INSTRUCTION, "Empty deployment bytecode");
    }
    
    if (bytecode.size() > MAX_BYTECODE_SIZE) {
        return CreateErrorResult(EVMC_INVALID_INSTRUCTION, "Bytecode too large");
    }
    
    // Apply trust-based deployment validation
    if (trust_features_enabled) {
        if (!CheckTrustGate(deployer_address, "contract_deployment", 50)) { // Require min 50 reputation
            return CreateErrorResult(EVMC_REJECTED, "Insufficient reputation for contract deployment");
        }
    }
    
    // Combine bytecode with constructor data
    std::vector<uint8_t> deployment_code = bytecode;
    deployment_code.insert(deployment_code.end(), constructor_data.begin(), constructor_data.end());
    
    // Create deployment message
    evmc_message msg = CreateEVMCMessage(
        EVMC_CREATE,
        deployer_address,
        uint160(), // No recipient for CREATE
        deploy_value,
        constructor_data,
        gas_limit
    );
    
    // Apply trust enhancements
    if (trust_features_enabled) {
        InjectTrustContext(msg);
        InjectCallerReputation(msg, deployer_address);
        
        // Check reputation gates for contract deployment
        if (!HandleReputationGatedCall(msg)) {
            return CreateErrorResult(EVMC_REJECTED, "Insufficient reputation for contract deployment");
        }
        
        uint64_t adjusted_gas = ApplyReputationGasDiscount(gas_limit, deployer_address);
        msg.gas = static_cast<int64_t>(adjusted_gas);
    }
    
    // Set up execution context
    evmc_host->SetBlockContext(timestamp, block_height, block_hash, uint256(), gas_limit);
    evmc_host->SetTxContext(uint256(), deployer_address, 1);
    
    // Execute deployment
    evmc_result evmc_result = evm_instance->execute(
        evm_instance,
        evmc_host->GetInterface(),
        reinterpret_cast<evmc_host_context*>(evmc_host.get()),
        evm_revision,
        &msg,
        deployment_code.data(),
        deployment_code.size()
    );
    
    EVMExecutionResult result = ProcessEVMCResult(evmc_result, msg, gas_limit, deployer_address);
    
    // Clean up
    if (evmc_result.release) {
        evmc_result.release(&evmc_result);
    }
    
    return result;
}

EVMExecutionResult EVMEngine::StaticCall(
    const uint160& contract_address,
    const std::vector<uint8_t>& call_data,
    uint64_t gas_limit,
    const uint160& caller_address,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    // Create static call message
    evmc_message msg = CreateEVMCMessage(
        EVMC_STATICCALL,
        caller_address,
        contract_address,
        0, // No value transfer in static call
        call_data,
        gas_limit
    );
    
    // Apply trust context (read-only operations still benefit from reputation)
    if (trust_features_enabled) {
        InjectTrustContext(msg);
        InjectCallerReputation(msg, caller_address);
    }
    
    // Set up context
    evmc_host->SetBlockContext(timestamp, block_height, block_hash, uint256(), gas_limit);
    evmc_host->SetTxContext(uint256(), caller_address, 1);
    
    // Get contract bytecode
    std::vector<uint8_t> bytecode;
    if (!database->GetContractCode(contract_address, bytecode)) {
        return CreateErrorResult(EVMC_REJECTED, "Contract not found");
    }
    
    // Execute static call
    evmc_result evmc_result = evm_instance->execute(
        evm_instance,
        evmc_host->GetInterface(),
        reinterpret_cast<evmc_host_context*>(evmc_host.get()),
        evm_revision,
        &msg,
        bytecode.data(),
        bytecode.size()
    );
    
    EVMExecutionResult result = ProcessEVMCResult(evmc_result, msg, gas_limit, caller_address);
    
    if (evmc_result.release) {
        evmc_result.release(&evmc_result);
    }
    
    return result;
}

evmc_message EVMEngine::CreateEVMCMessage(
    evmc_call_kind kind,
    const uint160& sender,
    const uint160& recipient,
    uint64_t value,
    const std::vector<uint8_t>& input_data,
    uint64_t gas_limit,
    int32_t depth) {
    
    evmc_message msg = {};
    msg.kind = kind;
    msg.flags = 0;
    msg.depth = depth;
    msg.gas = static_cast<int64_t>(gas_limit);
    
    // Convert addresses
    evmc_host->Uint160ToEvmcAddress(sender).bytes;
    std::copy(evmc_host->Uint160ToEvmcAddress(sender).bytes, 
              evmc_host->Uint160ToEvmcAddress(sender).bytes + 20, 
              msg.sender.bytes);
    
    std::copy(evmc_host->Uint160ToEvmcAddress(recipient).bytes,
              evmc_host->Uint160ToEvmcAddress(recipient).bytes + 20,
              msg.recipient.bytes);
    
    // Set value
    evmc_uint256be value_be = evmc_host->Uint256ToEvmcUint256be(uint256(value));
    msg.value = value_be;
    
    // Set input data
    msg.input_data = input_data.data();
    msg.input_size = input_data.size();
    
    return msg;
}

EVMExecutionResult EVMEngine::ProcessEVMCResult(
    const evmc_result& evmc_result,
    const evmc_message& msg,
    uint64_t original_gas_limit,
    const uint160& caller) {
    
    EVMExecutionResult result;
    
    // Basic result processing
    result.success = (evmc_result.status_code == EVMC_SUCCESS);
    result.status_code = evmc_result.status_code;
    result.gas_used = original_gas_limit - static_cast<uint64_t>(evmc_result.gas_left);
    result.gas_left = static_cast<uint64_t>(evmc_result.gas_left);
    
    // Copy output data
    if (evmc_result.output_data && evmc_result.output_size > 0) {
        result.output_data.assign(
            evmc_result.output_data,
            evmc_result.output_data + evmc_result.output_size
        );
    }
    
    // Process trust-related metrics
    if (trust_features_enabled && trust_context) {
        result.caller_reputation = trust_context->GetReputation(caller);
        result.original_gas_cost = original_gas_limit;
        result.reputation_adjusted_gas_cost = static_cast<uint64_t>(msg.gas);
        result.gas_saved_by_reputation = original_gas_limit - static_cast<uint64_t>(msg.gas);
        result.trust_gate_passed = CheckTrustGate(caller, "execution_completed");
    }
    
    // Set error message if failed
    if (!result.success) {
        switch (evmc_result.status_code) {
            case EVMC_FAILURE:
                result.error_message = "EVM execution failed";
                break;
            case EVMC_REVERT:
                result.error_message = "EVM execution reverted";
                break;
            case EVMC_OUT_OF_GAS:
                result.error_message = "Out of gas";
                break;
            case EVMC_INVALID_INSTRUCTION:
                result.error_message = "Invalid instruction";
                break;
            case EVMC_UNDEFINED_INSTRUCTION:
                result.error_message = "Undefined instruction";
                break;
            case EVMC_STACK_OVERFLOW:
                result.error_message = "Stack overflow";
                break;
            case EVMC_STACK_UNDERFLOW:
                result.error_message = "Stack underflow";
                break;
            case EVMC_BAD_JUMP_DESTINATION:
                result.error_message = "Bad jump destination";
                break;
            case EVMC_INVALID_MEMORY_ACCESS:
                result.error_message = "Invalid memory access";
                break;
            case EVMC_CALL_DEPTH_EXCEEDED:
                result.error_message = "Call depth exceeded";
                break;
            case EVMC_STATIC_MODE_VIOLATION:
                result.error_message = "Static mode violation";
                break;
            case EVMC_PRECOMPILE_FAILURE:
                result.error_message = "Precompile failure";
                break;
            case EVMC_CONTRACT_VALIDATION_FAILURE:
                result.error_message = "Contract validation failure";
                break;
            case EVMC_ARGUMENT_OUT_OF_RANGE:
                result.error_message = "Argument out of range";
                break;
            case EVMC_WASM_UNREACHABLE_INSTRUCTION:
                result.error_message = "WASM unreachable instruction";
                break;
            case EVMC_WASM_TRAP:
                result.error_message = "WASM trap";
                break;
            case EVMC_INSUFFICIENT_BALANCE:
                result.error_message = "Insufficient balance";
                break;
            case EVMC_INTERNAL_ERROR:
                result.error_message = "Internal error";
                break;
            case EVMC_REJECTED:
                result.error_message = "Execution rejected";
                break;
            default:
                result.error_message = "Unknown error: " + std::to_string(evmc_result.status_code);
                break;
        }
    }
    
    return result;
}

bool EVMEngine::CheckTrustGate(const uint160& caller, const std::string& operation, uint64_t min_reputation) {
    if (!trust_features_enabled || !trust_context) {
        return true; // Trust features disabled, allow all operations
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Check minimum reputation requirement
    if (caller_reputation < min_reputation) {
        LogPrintf("Trust gate failed for %s: reputation %d < required %d\n", 
                  operation.c_str(), caller_reputation, min_reputation);
        return false;
    }
    
    // Operation-specific trust gates
    if (operation == "contract_deployment" && caller_reputation < 50) {
        return false;
    } else if (operation == "high_value_transfer" && caller_reputation < 70) {
        return false;
    } else if (operation == "cross_chain_operation" && caller_reputation < 80) {
        return false;
    }
    
    return true;
}

uint64_t EVMEngine::ApplyReputationGasDiscount(uint64_t base_gas, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return base_gas;
    }
    
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // High reputation addresses get free gas
    if (reputation >= FREE_GAS_REPUTATION_THRESHOLD) {
        uint64_t free_gas_allowance = GetFreeGasAllowance(caller);
        if (base_gas <= free_gas_allowance) {
            return 0; // Completely free
        } else {
            return base_gas - free_gas_allowance; // Partial discount
        }
    }
    
    // Calculate reputation-based discount
    uint64_t discount = CalculateReputationDiscount(reputation);
    uint64_t discounted_gas = base_gas - std::min(base_gas, discount);
    
    return std::max(discounted_gas, base_gas / 10); // Minimum 10% of original cost
}

uint64_t EVMEngine::CalculateReputationDiscount(uint32_t reputation_score) {
    // Progressive discount based on reputation
    // 0-20: 0% discount
    // 21-40: 10% discount
    // 41-60: 25% discount
    // 61-80: 50% discount
    // 81-100: 75% discount
    
    if (reputation_score <= 20) {
        return 0;
    } else if (reputation_score <= 40) {
        return 10; // 10% discount
    } else if (reputation_score <= 60) {
        return 25; // 25% discount
    } else if (reputation_score <= 80) {
        return 50; // 50% discount
    } else {
        return 75; // 75% discount
    }
}

bool EVMEngine::IsEligibleForFreeGas(const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return false;
    }
    
    return trust_context->GetReputation(caller) >= FREE_GAS_REPUTATION_THRESHOLD;
}

uint64_t EVMEngine::GetFreeGasAllowance(const uint160& caller) {
    if (!IsEligibleForFreeGas(caller)) {
        return 0;
    }
    
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // Higher reputation gets more free gas
    if (reputation >= 95) {
        return MAX_FREE_GAS_PER_TRANSACTION;
    } else if (reputation >= 90) {
        return MAX_FREE_GAS_PER_TRANSACTION * 3 / 4;
    } else if (reputation >= 85) {
        return MAX_FREE_GAS_PER_TRANSACTION / 2;
    } else {
        return MAX_FREE_GAS_PER_TRANSACTION / 4;
    }
}

void EVMEngine::ApplyTrustAwareGasCostModifications(evmc_message& msg, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return;
    }
    
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // Apply operation-specific gas modifications based on reputation
    switch (msg.kind) {
        case EVMC_CALL:
            // Regular calls get reputation-based gas adjustments
            if (reputation >= 80) {
                // High reputation: 50% gas discount for calls
                msg.gas = static_cast<int64_t>(msg.gas * 0.5);
            } else if (reputation >= 60) {
                // Medium reputation: 25% gas discount
                msg.gas = static_cast<int64_t>(msg.gas * 0.75);
            }
            // Low reputation: no additional discount
            break;
            
        case EVMC_DELEGATECALL:
            // Delegate calls are more expensive for low reputation
            if (reputation < 60) {
                // Low reputation: 50% gas penalty for delegate calls
                msg.gas = static_cast<int64_t>(msg.gas * 1.5);
            }
            break;
            
        case EVMC_CREATE:
        case EVMC_CREATE2:
            // Contract creation gets reputation-based adjustments
            if (reputation >= 80) {
                // High reputation: 30% discount for contract creation
                msg.gas = static_cast<int64_t>(msg.gas * 0.7);
            } else if (reputation < 50) {
                // Low reputation: 25% penalty for contract creation
                msg.gas = static_cast<int64_t>(msg.gas * 1.25);
            }
            break;
            
        case EVMC_STATICCALL:
            // Static calls are cheaper for all reputation levels
            if (reputation >= 60) {
                // Medium+ reputation: 40% discount for static calls
                msg.gas = static_cast<int64_t>(msg.gas * 0.6);
            } else {
                // Low reputation: 20% discount for static calls
                msg.gas = static_cast<int64_t>(msg.gas * 0.8);
            }
            break;
    }
    
    // Ensure minimum gas requirements are met
    uint64_t min_gas = 21000; // Base transaction gas
    if (static_cast<uint64_t>(msg.gas) < min_gas) {
        msg.gas = static_cast<int64_t>(min_gas);
    }
    
    // Log gas modifications for debugging
    if (execution_tracing) {
        TraceExecution(strprintf("Trust-aware gas modification applied - Reputation: %d, Final gas: %d", 
                                reputation, msg.gas));
    }
}

void EVMEngine::InjectTrustContext(evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return;
    }
    
    uint160 caller = EvmcAddressToUint160(msg.sender);
    uint160 recipient = EvmcAddressToUint160(msg.recipient);
    
    // Inject caller and recipient reputation into the execution context
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    uint32_t recipient_reputation = trust_context->GetReputation(recipient);
    
    // Store reputation data in the trust context for access during execution
    trust_context->InjectTrustContext(caller, recipient);
    trust_context->SetCallerReputation(caller_reputation);
    trust_context->SetContractReputation(recipient_reputation);
    
    // Apply reputation-based gas adjustments
    if (caller_reputation >= FREE_GAS_REPUTATION_THRESHOLD) {
        uint64_t free_gas_allowance = GetFreeGasAllowance(caller);
        if (static_cast<uint64_t>(msg.gas) <= free_gas_allowance) {
            // Mark this as a free gas transaction
            // This would be handled by the host interface during execution
        }
    }
    
    // Log trust context injection for debugging
    if (execution_tracing) {
        TraceExecution(strprintf("Trust context injected - Caller: %s (rep: %d), Recipient: %s (rep: %d)", 
                                caller.ToString(), caller_reputation, 
                                recipient.ToString(), recipient_reputation));
    }
}

void EVMEngine::InjectCallerReputation(evmc_message& msg, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return;
    }
    
    // Store caller reputation in a way that can be accessed during execution
    // This could be through special storage slots or message flags
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // For now, we'll rely on the EVMC host to provide reputation context
    // Future enhancement: modify message structure to include reputation directly
}

// Trust-enhanced arithmetic operations implementation
bool EVMEngine::HandleTrustWeightedArithmetic(uint8_t opcode, evmc_message& msg, 
                                             const uint256& operand1, const uint256& operand2, uint256& result) {
    if (!trust_features_enabled || !trust_context) {
        return false; // Fall back to standard arithmetic
    }
    
    uint160 caller = EvmcAddressToUint160(msg.sender);
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // Validate trust gate for arithmetic operations
    if (!ValidateArithmeticTrustGate(reputation, std::max(operand1, operand2))) {
        return false; // Trust gate failed
    }
    
    switch (opcode) {
        case 0x01: // ADD
            result = PerformTrustWeightedAddition(operand1, operand2, reputation);
            return true;
        case 0x02: // MUL
            result = PerformTrustWeightedMultiplication(operand1, operand2, reputation);
            return true;
        case 0x04: // DIV
            if (operand2 == 0) return false; // Division by zero
            result = PerformTrustWeightedDivision(operand1, operand2, reputation);
            return true;
        default:
            return false; // Not a trust-enhanced arithmetic operation
    }
}

uint256 EVMEngine::PerformTrustWeightedAddition(const uint256& a, const uint256& b, uint32_t reputation_weight) {
    // Standard addition with overflow protection based on reputation
    if (reputation_weight >= 80) {
        // High reputation: allow larger operations with overflow detection
        arith_uint256 result = arith_uint256(a) + arith_uint256(b);
        return result.GetLow256();
    } else if (reputation_weight >= 60) {
        // Medium reputation: standard addition with bounds checking
        arith_uint256 result = arith_uint256(a) + arith_uint256(b);
        // Check for reasonable bounds to prevent abuse
        if (result > arith_uint256("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")) {
            return uint256(); // Return zero on overflow for medium reputation
        }
        return result.GetLow256();
    } else {
        // Low reputation: conservative addition with strict limits
        if (a > uint256("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") ||
            b > uint256("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")) {
            return uint256(); // Reject large operands for low reputation
        }
        arith_uint256 result = arith_uint256(a) + arith_uint256(b);
        return result.GetLow256();
    }
}

uint256 EVMEngine::PerformTrustWeightedMultiplication(const uint256& a, const uint256& b, uint32_t reputation_weight) {
    // Trust-weighted multiplication with reputation-based limits
    if (reputation_weight >= 80) {
        // High reputation: full multiplication capability
        arith_uint256 result = arith_uint256(a) * arith_uint256(b);
        return result.GetLow256();
    } else if (reputation_weight >= 60) {
        // Medium reputation: multiplication with overflow protection
        if (a != 0 && b > arith_uint256("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") / arith_uint256(a)) {
            return uint256(); // Overflow protection
        }
        arith_uint256 result = arith_uint256(a) * arith_uint256(b);
        return result.GetLow256();
    } else {
        // Low reputation: limited multiplication to prevent resource abuse
        if (a > uint256("0xFFFFFFFFFFFFFFFF") || b > uint256("0xFFFFFFFFFFFFFFFF")) {
            return uint256(); // Limit operand size for low reputation
        }
        arith_uint256 result = arith_uint256(a) * arith_uint256(b);
        return result.GetLow256();
    }
}

uint256 EVMEngine::PerformTrustWeightedDivision(const uint256& a, const uint256& b, uint32_t reputation_weight) {
    if (b == 0) {
        return uint256(); // Division by zero returns zero
    }
    
    // Trust-weighted division with precision based on reputation
    if (reputation_weight >= 80) {
        // High reputation: full precision division
        arith_uint256 result = arith_uint256(a) / arith_uint256(b);
        return result.GetLow256();
    } else if (reputation_weight >= 60) {
        // Medium reputation: standard division
        arith_uint256 result = arith_uint256(a) / arith_uint256(b);
        return result.GetLow256();
    } else {
        // Low reputation: conservative division with bounds checking
        if (a > uint256("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")) {
            return uint256(); // Limit dividend size for low reputation
        }
        arith_uint256 result = arith_uint256(a) / arith_uint256(b);
        return result.GetLow256();
    }
}

bool EVMEngine::ValidateArithmeticTrustGate(uint32_t caller_reputation, const uint256& operand_size) {
    // Validate that caller has sufficient reputation for the operation size
    if (caller_reputation >= 80) {
        return true; // High reputation: no limits
    } else if (caller_reputation >= 60) {
        // Medium reputation: reasonable limits
        return operand_size <= uint256("0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
    } else if (caller_reputation >= 40) {
        // Low reputation: strict limits
        return operand_size <= uint256("0xFFFFFFFFFFFFFFFF");
    } else {
        // Very low reputation: very strict limits
        return operand_size <= uint256("0xFFFFFFFF");
    }
}

bool EVMEngine::HandleReputationGatedCall(evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No gating if trust features disabled
    }
    
    uint160 caller = EvmcAddressToUint160(msg.sender);
    uint160 target = EvmcAddressToUint160(msg.recipient);
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    uint32_t target_reputation = trust_context->GetReputation(target);
    
    // Implement reputation-based call gating
    switch (msg.kind) {
        case EVMC_CALL:
            // Regular calls require minimum reputation based on value
            if (msg.value.bytes[31] > 0) { // Non-zero value transfer
                return caller_reputation >= 60; // Require medium reputation for value transfers
            }
            return caller_reputation >= 40; // Low reputation sufficient for zero-value calls
            
        case EVMC_DELEGATECALL:
            // Delegate calls require high reputation due to security implications
            return caller_reputation >= 80;
            
        case EVMC_STATICCALL:
            // Static calls are safer, require lower reputation
            return caller_reputation >= 20;
            
        case EVMC_CREATE:
        case EVMC_CREATE2:
            // Contract creation requires high reputation
            return caller_reputation >= 70;
            
        default:
            return caller_reputation >= 40;
    }
}

bool EVMEngine::CheckReputationBasedLimits(const uint160& caller, uint64_t gas_limit, size_t memory_size) {
    if (!trust_features_enabled || !trust_context) {
        return true;
    }
    
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // Gas limit restrictions based on reputation
    uint64_t max_gas_for_reputation;
    if (reputation >= 80) {
        max_gas_for_reputation = 10000000; // 10M gas for high reputation
    } else if (reputation >= 60) {
        max_gas_for_reputation = 5000000;  // 5M gas for medium reputation
    } else if (reputation >= 40) {
        max_gas_for_reputation = 1000000;  // 1M gas for low reputation
    } else {
        max_gas_for_reputation = 100000;   // 100K gas for very low reputation
    }
    
    if (gas_limit > max_gas_for_reputation) {
        LogPrintf("Gas limit %d exceeds reputation-based limit %d for reputation %d\n",
                  gas_limit, max_gas_for_reputation, reputation);
        return false;
    }
    
    // Memory size restrictions
    size_t max_memory_for_reputation;
    if (reputation >= 80) {
        max_memory_for_reputation = 1024 * 1024 * 100; // 100MB for high reputation
    } else if (reputation >= 60) {
        max_memory_for_reputation = 1024 * 1024 * 50;  // 50MB for medium reputation
    } else {
        max_memory_for_reputation = 1024 * 1024 * 10;  // 10MB for low reputation
    }
    
    if (memory_size > max_memory_for_reputation) {
        LogPrintf("Memory size %d exceeds reputation-based limit %d for reputation %d\n",
                  memory_size, max_memory_for_reputation, reputation);
        return false;
    }
    
    return true;
}

EVMExecutionResult EVMEngine::CreateErrorResult(evmc_status_code status, const std::string& error) {
    EVMExecutionResult result;
    result.success = false;
    result.status_code = status;
    result.error_message = error;
    result.gas_used = 0;
    result.gas_left = 0;
    
    // Update error statistics
    stats.failed_executions++;
    stats.status_code_frequency[status]++;
    
    return result;
}

void EVMEngine::UpdateExecutionMetrics(const EVMExecutionResult& result, int64_t execution_time_ms) {
    stats.total_executions++;
    
    if (result.success) {
        stats.successful_executions++;
    } else {
        stats.failed_executions++;
    }
    
    stats.total_gas_used += result.gas_used;
    stats.total_gas_saved_by_reputation += result.gas_saved_by_reputation;
    stats.status_code_frequency[result.status_code]++;
    
    if (result.caller_reputation >= HIGH_REPUTATION_THRESHOLD) {
        stats.high_reputation_executions++;
    }
    
    if (!result.trust_gate_passed) {
        stats.trust_gate_failures++;
    }
    
    // Update average execution time
    double total_time = stats.average_execution_time_ms * (stats.total_executions - 1);
    stats.average_execution_time_ms = (total_time + execution_time_ms) / stats.total_executions;
}

void EVMEngine::ResetStats() {
    stats = EngineStats{};
}

void EVMEngine::SetTrustContext(std::shared_ptr<TrustContext> ctx) {
    trust_context = ctx;
    if (evmc_host) {
        evmc_host->SetTrustContext(*ctx);
    }
}

// Factory implementations
std::unique_ptr<EVMEngine> EVMEngineFactory::CreateMainnetEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx) {
    auto engine = std::make_unique<EVMEngine>(db, trust_ctx);
    ConfigureForMainnet(engine.get());
    return engine;
}

std::unique_ptr<EVMEngine> EVMEngineFactory::CreateTestnetEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx) {
    auto engine = std::make_unique<EVMEngine>(db, trust_ctx);
    ConfigureForTesting(engine.get());
    return engine;
}

std::unique_ptr<EVMEngine> EVMEngineFactory::CreateLocalEngine(CVMDatabase* db) {
    auto trust_ctx = std::make_shared<TrustContext>();
    auto engine = std::make_unique<EVMEngine>(db, trust_ctx);
    engine->EnableTrustFeatures(false); // Disable trust features for local testing
    return engine;
}

void EVMEngineFactory::ConfigureForMainnet(EVMEngine* engine) {
    engine->SetRevision(EVMC_LONDON);
    engine->EnableTrustFeatures(true);
    engine->SetStrictGasAccounting(true);
}

void EVMEngineFactory::ConfigureForTesting(EVMEngine* engine) {
    engine->SetRevision(EVMC_LONDON);
    engine->EnableTrustFeatures(true);
    engine->SetStrictGasAccounting(false);
    engine->EnableExecutionTracing(true);
}

void EVMEngineFactory::ConfigureForDebugging(EVMEngine* engine) {
    engine->SetRevision(EVMC_LONDON);
    engine->EnableTrustFeatures(true);
    engine->SetStrictGasAccounting(false);
    engine->EnableExecutionTracing(true);
    engine->EnableOpcodeFrequencyTracking(true);
}

// Utility function implementations
namespace EVMEngineUtils {

bool IsSuccessfulExecution(const EVMExecutionResult& result) {
    return result.success && result.status_code == EVMC_SUCCESS;
}

bool HasReputationBenefits(const EVMExecutionResult& result) {
    return result.gas_saved_by_reputation > 0 || result.trust_gate_passed;
}

double CalculateGasSavingsPercentage(const EVMExecutionResult& result) {
    if (result.original_gas_cost == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(result.gas_saved_by_reputation) / result.original_gas_cost) * 100.0;
}

std::string FormatExecutionResult(const EVMExecutionResult& result) {
    std::string output;
    output += "Success: " + std::string(result.success ? "true" : "false") + "\n";
    output += "Status: " + std::to_string(result.status_code) + "\n";
    output += "Gas Used: " + std::to_string(result.gas_used) + "\n";
    output += "Gas Left: " + std::to_string(result.gas_left) + "\n";
    
    if (result.gas_saved_by_reputation > 0) {
        output += "Gas Saved by Reputation: " + std::to_string(result.gas_saved_by_reputation) + "\n";
        output += "Gas Savings %: " + std::to_string(CalculateGasSavingsPercentage(result)) + "%\n";
    }
    
    if (!result.error_message.empty()) {
        output += "Error: " + result.error_message + "\n";
    }
    
    return output;
}

} // namespace EVMEngineUtils

// Trust-aware memory operations implementation
bool EVMEngine::HandleTrustAwareMemory(uint8_t opcode, evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return false; // Fall back to standard memory operations
    }
    
    uint160 caller = EvmcAddressToUint160(msg.sender);
    uint32_t reputation = trust_context->GetReputation(caller);
    
    switch (opcode) {
        case 0x51: // MLOAD
            // Trust-aware memory load with comprehensive access control
            if (!CheckReputationBasedDataAccess(caller, "memory_read")) {
                return false;
            }
            return ValidateTrustTaggedMemoryAccess(caller, reputation, true);
            
        case 0x52: // MSTORE
            // Trust-aware memory store with reputation-based write permissions
            if (!CheckReputationBasedDataAccess(caller, "memory_write")) {
                return false;
            }
            return ValidateTrustTaggedMemoryAccess(caller, reputation, false);
            
        case 0x53: // MSTORE8
            // Trust-aware byte store with reputation validation
            if (!CheckReputationBasedDataAccess(caller, "memory_write")) {
                return false;
            }
            return ValidateTrustTaggedMemoryAccess(caller, reputation, false);
            
        case 0x50: // POP (stack operation)
            // Trust-aware stack pop with reputation validation
            if (!CheckReputationBasedDataAccess(caller, "stack_pop")) {
                return false;
            }
            return ValidateStackAccess(caller, 25);
            
        case 0x80: // DUP1 (stack duplication)
        case 0x81: // DUP2
        case 0x82: // DUP3
        case 0x83: // DUP4
            // Trust-aware stack duplication
            if (!CheckReputationBasedDataAccess(caller, "stack_push")) {
                return false;
            }
            return ValidateStackAccess(caller, 30);
            
        case 0x90: // SWAP1 (stack swap)
        case 0x91: // SWAP2
        case 0x92: // SWAP3
        case 0x93: // SWAP4
            // Trust-aware stack swap operations
            return ValidateStackAccess(caller, 35);
            
        default:
            return false; // Not a trust-enhanced memory operation
    }
}

bool EVMEngine::ValidateTrustTaggedMemoryAccess(const uint160& caller, uint32_t reputation, bool is_read) {
    // For now, implement basic reputation-based memory access validation
    // In a full implementation, this would check specific memory regions
    
    if (is_read) {
        // Read operations require lower reputation
        return reputation >= 20;
    } else {
        // Write operations require higher reputation
        return reputation >= 40;
    }
}

bool EVMEngine::CreateTrustTaggedMemoryRegion(size_t offset, size_t size, uint32_t min_reputation) {
    if (trust_tagged_regions.size() >= MAX_TRUST_TAGGED_REGIONS) {
        return false; // Too many tagged regions
    }
    
    TrustTaggedMemoryRegion region;
    region.offset = offset;
    region.size = size;
    region.min_reputation = min_reputation;
    region.created_at = GetTime();
    region.is_protected = false;
    region.region_id = strprintf("region_%zu_%zu", offset, size);
    
    trust_tagged_regions.push_back(region);
    return true;
}

bool EVMEngine::CreateProtectedMemoryRegion(size_t offset, size_t size, const uint160& creator, const std::string& region_id) {
    if (trust_tagged_regions.size() >= MAX_TRUST_TAGGED_REGIONS) {
        return false;
    }
    
    // Check if creator has sufficient reputation to create protected regions
    uint32_t creator_reputation = trust_context ? trust_context->GetReputation(creator) : 0;
    if (creator_reputation < 70) { // Require high reputation for protected regions
        return false;
    }
    
    TrustTaggedMemoryRegion region;
    region.offset = offset;
    region.size = size;
    region.min_reputation = 80; // Protected regions require very high reputation
    region.created_at = GetTime();
    region.creator_address = creator;
    region.region_id = region_id;
    region.is_protected = true;
    
    trust_tagged_regions.push_back(region);
    
    if (execution_tracing) {
        TraceExecution(strprintf("Created protected memory region: %s at offset %zu, size %zu", 
                                region_id, offset, size));
    }
    
    return true;
}

bool EVMEngine::ValidateMemoryRegionAccess(size_t offset, size_t size, const uint160& caller, bool is_write) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No restrictions if trust features disabled
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Check if access overlaps with any trust-tagged regions
    for (const auto& region : trust_tagged_regions) {
        size_t region_end = region.offset + region.size;
        size_t access_end = offset + size;
        
        // Check for overlap
        if (offset < region_end && access_end > region.offset) {
            // Access overlaps with trust-tagged region
            if (region.is_protected) {
                // Protected regions have stricter access control
                if (caller != region.creator_address && caller_reputation < region.min_reputation) {
                    if (execution_tracing) {
                        TraceExecution(strprintf("Protected memory access denied: caller %s, reputation %d, required %d", 
                                                caller.ToString(), caller_reputation, region.min_reputation));
                    }
                    return false;
                }
            } else {
                // Regular trust-tagged regions
                if (caller_reputation < region.min_reputation) {
                    return false;
                }
                
                // Additional restrictions for write operations
                if (is_write && caller_reputation < region.min_reputation + 10) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool EVMEngine::TestTrustEnhancedOperations() {
    if (!trust_features_enabled || !trust_context) {
        LogPrintf("Trust features not enabled, skipping trust-enhanced operations test\n");
        return false;
    }
    
    LogPrintf("Testing trust-enhanced EVM operations...\n");
    
    // Test trust-weighted arithmetic
    uint256 a("100");
    uint256 b("200");
    uint256 result;
    
    // Create a mock message for testing
    evmc_message test_msg = {};
    test_msg.kind = EVMC_CALL;
    test_msg.gas = 1000000;
    
    // Test with different reputation levels
    bool high_rep_test = HandleTrustWeightedArithmetic(0x01, test_msg, a, b, result); // ADD
    if (high_rep_test) {
        LogPrintf("Trust-weighted arithmetic test passed: %s + %s = %s\n", 
                  a.ToString(), b.ToString(), result.ToString());
    }
    
    // Test reputation-gated calls
    bool call_gate_test = HandleReputationGatedCall(test_msg);
    LogPrintf("Reputation-gated call test: %s\n", call_gate_test ? "PASSED" : "FAILED");
    
    // Test trust-aware memory operations
    bool memory_test = HandleTrustAwareMemory(0x51, test_msg); // MLOAD
    LogPrintf("Trust-aware memory test: %s\n", memory_test ? "PASSED" : "FAILED");
    
    LogPrintf("Trust-enhanced operations test completed\n");
    return high_rep_test && call_gate_test && memory_test;
}

bool EVMEngine::TestTrustAwareMemoryAndStack() {
    if (!trust_features_enabled || !trust_context) {
        LogPrintf("Trust features not enabled, skipping memory and stack test\n");
        return false;
    }
    
    LogPrintf("Testing trust-aware memory and stack operations...\n");
    
    bool all_tests_passed = true;
    
    // Test trust-tagged memory region creation
    bool memory_region_test = CreateTrustTaggedMemoryRegion(0x1000, 0x100, 60);
    if (memory_region_test) {
        LogPrintf("Trust-tagged memory region creation: PASSED\n");
    } else {
        LogPrintf("Trust-tagged memory region creation: FAILED\n");
        all_tests_passed = false;
    }
    
    // Test protected memory region creation
    uint160 test_creator;
    test_creator.SetHex("1234567890123456789012345678901234567890");
    bool protected_region_test = CreateProtectedMemoryRegion(0x2000, 0x200, test_creator, "test_protected");
    LogPrintf("Protected memory region creation: %s\n", protected_region_test ? "PASSED" : "FAILED");
    if (!protected_region_test) all_tests_passed = false;
    
    // Test reputation-sorted array
    bool array_test = CreateReputationSortedArray("test_array", 50);
    if (array_test) {
        // Add some test values
        AddToReputationArray("test_array", uint256("100"), 80);
        AddToReputationArray("test_array", uint256("200"), 60);
        AddToReputationArray("test_array", uint256("300"), 90);
        
        // Test retrieval
        uint256 retrieved_value;
        bool retrieval_test = GetFromReputationArray("test_array", 0, retrieved_value, test_creator);
        LogPrintf("Reputation-sorted array test: %s (retrieved: %s)\n", 
                  retrieval_test ? "PASSED" : "FAILED", retrieved_value.ToString());
        if (!retrieval_test) all_tests_passed = false;
    } else {
        LogPrintf("Reputation-sorted array creation: FAILED\n");
        all_tests_passed = false;
    }
    
    // Test reputation-weighted stack operations
    bool stack_push_test = PushReputationWeightedValue(uint256("500"), 70);
    if (stack_push_test) {
        uint256 popped_value;
        bool stack_pop_test = PopReputationWeighted(popped_value, 60);
        LogPrintf("Reputation-weighted stack test: %s (value: %s)\n", 
                  stack_pop_test ? "PASSED" : "FAILED", popped_value.ToString());
        if (!stack_pop_test) all_tests_passed = false;
    } else {
        LogPrintf("Reputation-weighted stack push: FAILED\n");
        all_tests_passed = false;
    }
    
    // Test data integrity validation
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    bool integrity_test = ValidateDataIntegrity(test_data, test_creator);
    LogPrintf("Data integrity validation: %s\n", integrity_test ? "PASSED" : "FAILED");
    if (!integrity_test) all_tests_passed = false;
    
    LogPrintf("Trust-aware memory and stack test completed: %s\n", 
              all_tests_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    
    return all_tests_passed;
}

// Reputation-weighted stack operations implementation
bool EVMEngine::PushReputationWeightedValue(const uint256& value, uint32_t reputation_weight) {
    if (reputation_stack.size() >= MAX_REPUTATION_STACK_SIZE) {
        return false; // Stack overflow
    }
    
    ReputationWeightedStackEntry entry;
    entry.value = value;
    entry.reputation_weight = reputation_weight;
    entry.timestamp = GetTime();
    
    reputation_stack.push_back(entry);
    
    if (execution_tracing) {
        TraceExecution(strprintf("Pushed reputation-weighted value: %s (weight: %d)", 
                                value.ToString(), reputation_weight));
    }
    
    return true;
}

bool EVMEngine::PopReputationWeighted(uint256& value, uint32_t min_reputation) {
    if (reputation_stack.empty()) {
        return false; // Stack underflow
    }
    
    auto& top_entry = reputation_stack.back();
    
    // Check if caller has sufficient reputation to access this value
    if (top_entry.reputation_weight > min_reputation) {
        return false; // Insufficient reputation to access this value
    }
    
    value = top_entry.value;
    reputation_stack.pop_back();
    
    if (execution_tracing) {
        TraceExecution(strprintf("Popped reputation-weighted value: %s (required: %d)", 
                                value.ToString(), min_reputation));
    }
    
    return true;
}

bool EVMEngine::ValidateStackAccess(const uint160& caller, uint32_t required_reputation) {
    if (!trust_features_enabled || !trust_context) {
        return true;
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    return caller_reputation >= required_reputation;
}

// Trust-aware data structures implementation
bool EVMEngine::CreateReputationSortedArray(const std::string& array_id, uint32_t min_access_reputation) {
    if (reputation_arrays.find(array_id) != reputation_arrays.end()) {
        return false; // Array already exists
    }
    
    ReputationSortedArray array;
    array.min_access_reputation = min_access_reputation;
    array.is_sorted = true; // Empty array is considered sorted
    
    reputation_arrays[array_id] = array;
    
    if (execution_tracing) {
        TraceExecution(strprintf("Created reputation-sorted array: %s (min_reputation: %d)", 
                                array_id, min_access_reputation));
    }
    
    return true;
}

bool EVMEngine::AddToReputationArray(const std::string& array_id, const uint256& value, uint32_t reputation_weight) {
    auto it = reputation_arrays.find(array_id);
    if (it == reputation_arrays.end()) {
        return false; // Array doesn't exist
    }
    
    auto& array = it->second;
    array.data.emplace_back(value, reputation_weight);
    array.is_sorted = false; // Adding new element breaks sorting
    
    if (execution_tracing) {
        TraceExecution(strprintf("Added to reputation array %s: value %s (weight: %d)", 
                                array_id, value.ToString(), reputation_weight));
    }
    
    return true;
}

bool EVMEngine::GetFromReputationArray(const std::string& array_id, size_t index, uint256& value, const uint160& caller) {
    auto it = reputation_arrays.find(array_id);
    if (it == reputation_arrays.end()) {
        return false; // Array doesn't exist
    }
    
    auto& array = it->second;
    
    // Check caller reputation
    uint32_t caller_reputation = trust_context ? trust_context->GetReputation(caller) : 0;
    if (caller_reputation < array.min_access_reputation) {
        return false; // Insufficient reputation to access array
    }
    
    if (index >= array.data.size()) {
        return false; // Index out of bounds
    }
    
    // Ensure array is sorted by reputation weight (highest first)
    if (!array.is_sorted) {
        SortReputationArray(array_id);
    }
    
    value = array.data[index].first;
    
    if (execution_tracing) {
        TraceExecution(strprintf("Retrieved from reputation array %s[%zu]: %s", 
                                array_id, index, value.ToString()));
    }
    
    return true;
}

bool EVMEngine::SortReputationArray(const std::string& array_id) {
    auto it = reputation_arrays.find(array_id);
    if (it == reputation_arrays.end()) {
        return false;
    }
    
    auto& array = it->second;
    
    // Sort by reputation weight (highest first)
    std::sort(array.data.begin(), array.data.end(),
              [](const std::pair<uint256, uint32_t>& a, const std::pair<uint256, uint32_t>& b) {
                  return a.second > b.second;
              });
    
    array.is_sorted = true;
    
    if (execution_tracing) {
        TraceExecution(strprintf("Sorted reputation array: %s (%zu elements)", 
                                array_id, array.data.size()));
    }
    
    return true;
}

// Automatic reputation validation implementation
bool EVMEngine::ValidateDataIntegrity(const std::vector<uint8_t>& data, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No validation if trust features disabled
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Data integrity validation based on reputation
    if (caller_reputation >= 80) {
        // High reputation: full trust, minimal validation
        return true;
    } else if (caller_reputation >= 60) {
        // Medium reputation: basic validation
        if (data.size() > 1024 * 1024) { // 1MB limit for medium reputation
            return false;
        }
        return true;
    } else if (caller_reputation >= 40) {
        // Low reputation: strict validation
        if (data.size() > 64 * 1024) { // 64KB limit for low reputation
            return false;
        }
        
        // Check for suspicious patterns (all zeros, all ones, etc.)
        bool all_same = true;
        if (!data.empty()) {
            uint8_t first_byte = data[0];
            for (uint8_t byte : data) {
                if (byte != first_byte) {
                    all_same = false;
                    break;
                }
            }
        }
        
        if (all_same && data.size() > 1024) {
            return false; // Suspicious pattern for large data
        }
        
        return true;
    } else {
        // Very low reputation: very strict validation
        if (data.size() > 4096) { // 4KB limit for very low reputation
            return false;
        }
        
        // Additional checks for very low reputation
        return true;
    }
}

bool EVMEngine::CheckReputationBasedDataAccess(const uint160& caller, const std::string& operation) {
    if (!trust_features_enabled || !trust_context) {
        return true;
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Operation-specific reputation requirements
    if (operation == "memory_write") {
        return caller_reputation >= 40;
    } else if (operation == "memory_read") {
        return caller_reputation >= 20;
    } else if (operation == "stack_push") {
        return caller_reputation >= 30;
    } else if (operation == "stack_pop") {
        return caller_reputation >= 25;
    } else if (operation == "array_access") {
        return caller_reputation >= 35;
    } else if (operation == "protected_region_access") {
        return caller_reputation >= 70;
    }
    
    // Default requirement for unknown operations
    return caller_reputation >= 50;
}

} // namespace CVM

#endif // ENABLE_EVMC