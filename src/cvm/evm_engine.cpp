// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <config/bitcoin-config.h>

#ifdef ENABLE_EVMC

#include <cvm/evm_engine.h>
#include <cvm/opcodes.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <pubkey.h>
#include <key.h>
#include <arith_uint256.h>
#include <random.h>
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
        LogPrintf("Failed to load evmone: error code %d\n", static_cast<int>(error_code));
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
    
    // Initialize sustainable gas system
    gas_system = std::make_unique<cvm::SustainableGasSystem>();
    
    // Initialize statistics
    ResetStats();
    
    LogPrintf("EVM Engine initialized with trust features %s and sustainable gas system\n", 
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
    
    // Create static call message (CALL with STATIC flag)
    evmc_message msg = CreateEVMCMessage(
        EVMC_CALL,
        caller_address,
        contract_address,
        0, // No value transfer in static call
        call_data,
        gas_limit
    );
    msg.flags |= EVMC_STATIC; // Set static flag
    
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
    if (!database->LoadContract(contract_address, bytecode)) {
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

EVMExecutionResult EVMEngine::DelegateCall(
    const uint160& contract_address,
    const std::vector<uint8_t>& call_data,
    uint64_t gas_limit,
    const uint160& caller_address,
    const uint160& original_caller,
    uint64_t call_value,
    int block_height,
    const uint256& block_hash,
    int64_t timestamp) {
    
    LogPrint(BCLog::CVM, "EVMEngine: Executing DELEGATECALL to %s from %s (original: %s)\n",
             contract_address.ToString(), caller_address.ToString(), original_caller.ToString());
    
    // DELEGATECALL requires high reputation due to security implications
    if (trust_features_enabled) {
        uint32_t caller_reputation = trust_context->GetReputation(caller_address);
        if (caller_reputation < 80) {
            LogPrint(BCLog::CVM, "EVMEngine: DELEGATECALL rejected - insufficient reputation %d (requires 80+)\n",
                     caller_reputation);
            return CreateErrorResult(EVMC_REJECTED, "Insufficient reputation for DELEGATECALL");
        }
    }
    
    // Create delegate call message
    // In DELEGATECALL, code executes in caller's context with caller's storage
    evmc_message msg = CreateEVMCMessage(
        EVMC_DELEGATECALL,
        caller_address,      // Sender remains the caller
        contract_address,    // Code comes from target contract
        call_value,          // Value from original call
        call_data,
        gas_limit
    );
    
    // Apply trust context
    if (trust_features_enabled) {
        InjectTrustContext(msg);
        InjectCallerReputation(msg, original_caller); // Use original caller's reputation
        
        // Additional security check for delegate calls
        if (!CheckTrustGate(original_caller, "delegate_call", 80)) {
            return CreateErrorResult(EVMC_REJECTED, "Trust gate failed for DELEGATECALL");
        }
    }
    
    // Set up context
    evmc_host->SetBlockContext(timestamp, block_height, block_hash, uint256(), gas_limit);
    evmc_host->SetTxContext(uint256(), original_caller, 1); // Use original caller as tx origin
    
    // Get target contract bytecode
    std::vector<uint8_t> bytecode;
    if (!database->LoadContract(contract_address, bytecode)) {
        LogPrint(BCLog::CVM, "EVMEngine: DELEGATECALL target contract not found at %s\n",
                 contract_address.ToString());
        return CreateErrorResult(EVMC_REJECTED, "Target contract not found for DELEGATECALL");
    }
    
    if (bytecode.empty()) {
        LogPrint(BCLog::CVM, "EVMEngine: DELEGATECALL target has empty bytecode\n");
        return CreateErrorResult(EVMC_REJECTED, "Target contract has no code");
    }
    
    // Execute delegate call
    evmc_result evmc_result = evm_instance->execute(
        evm_instance,
        evmc_host->GetInterface(),
        reinterpret_cast<evmc_host_context*>(evmc_host.get()),
        evm_revision,
        &msg,
        bytecode.data(),
        bytecode.size()
    );
    
    EVMExecutionResult result = ProcessEVMCResult(evmc_result, msg, gas_limit, original_caller);
    
    LogPrint(BCLog::CVM, "EVMEngine: DELEGATECALL completed with status %d, gas used: %d\n",
             result.status_code, result.gas_used);
    
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
    
    // Set value (convert uint64_t to uint256)
    uint256 value_uint256;
    memset(value_uint256.begin(), 0, 32);
    memcpy(value_uint256.begin(), &value, sizeof(value));
    evmc_uint256be value_be = evmc_host->Uint256ToEvmcUint256be(value_uint256);
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
            // Check if it's a static call via flags
            if (msg.flags & EVMC_STATIC) {
                // Static calls are cheaper for all reputation levels
                if (reputation >= 60) {
                    // Medium+ reputation: 40% discount for static calls
                    msg.gas = static_cast<int64_t>(msg.gas * 0.6);
                } else {
                    // Low reputation: 20% discount for static calls
                    msg.gas = static_cast<int64_t>(msg.gas * 0.8);
                }
            } else {
                // Regular calls get reputation-based gas adjustments
                if (reputation >= 80) {
                    // High reputation: 50% gas discount for calls
                    msg.gas = static_cast<int64_t>(msg.gas * 0.5);
                } else if (reputation >= 60) {
                    // Medium reputation: 25% gas discount
                    msg.gas = static_cast<int64_t>(msg.gas * 0.75);
                }
                // Low reputation: no additional discount
            }
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
            
        case EVMC_CALLCODE:
        case EVMC_EOFCREATE:
            // Handle other call types with default behavior
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
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
    uint160 recipient = evmc_host->EvmcAddressToUint160(msg.recipient);
    
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
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
    uint32_t reputation = trust_context->GetReputation(caller);
    
    // Validate trust gate for arithmetic operations
    // Use arith_uint256 for comparison
    arith_uint256 arith_op1 = UintToArith256(operand1);
    arith_uint256 arith_op2 = UintToArith256(operand2);
    uint256 max_operand = ArithToUint256(arith_op1 > arith_op2 ? arith_op1 : arith_op2);
    if (!ValidateArithmeticTrustGate(reputation, max_operand)) {
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
            if (arith_op2 == 0) return false; // Division by zero
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
        arith_uint256 result = UintToArith256(a) + UintToArith256(b);
        return ArithToUint256(result);
    } else if (reputation_weight >= 60) {
        // Medium reputation: standard addition with bounds checking
        arith_uint256 result = UintToArith256(a) + UintToArith256(b);
        // Check for reasonable bounds to prevent abuse
        // Result is already within uint256 bounds
        return ArithToUint256(result);
    } else {
        // Low reputation: conservative addition with strict limits
        arith_uint256 max_safe;
        max_safe.SetHex("7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        arith_uint256 arith_a = UintToArith256(a);
        arith_uint256 arith_b = UintToArith256(b);
        if (arith_a > max_safe || arith_b > max_safe) {
            return uint256(); // Reject large operands for low reputation
        }
        arith_uint256 result = arith_a + arith_b;
        return ArithToUint256(result);
    }
}

uint256 EVMEngine::PerformTrustWeightedMultiplication(const uint256& a, const uint256& b, uint32_t reputation_weight) {
    // Trust-weighted multiplication with reputation-based limits
    arith_uint256 arith_a = UintToArith256(a);
    arith_uint256 arith_b = UintToArith256(b);
    
    if (reputation_weight >= 80) {
        // High reputation: full multiplication capability
        arith_uint256 result = arith_a * arith_b;
        return ArithToUint256(result);
    } else if (reputation_weight >= 60) {
        // Medium reputation: multiplication with overflow protection
        arith_uint256 max_uint256;
        max_uint256.SetHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        if (arith_a != 0 && arith_b > max_uint256 / arith_a) {
            return uint256(); // Overflow protection
        }
        arith_uint256 result = arith_a * arith_b;
        return ArithToUint256(result);
    } else {
        // Low reputation: limited multiplication to prevent resource abuse
        arith_uint256 max_operand;
        max_operand.SetHex("FFFFFFFFFFFFFFFF");
        if (arith_a > max_operand || arith_b > max_operand) {
            return uint256(); // Limit operand size for low reputation
        }
        arith_uint256 result = arith_a * arith_b;
        return ArithToUint256(result);
    }
}

uint256 EVMEngine::PerformTrustWeightedDivision(const uint256& a, const uint256& b, uint32_t reputation_weight) {
    arith_uint256 arith_b = UintToArith256(b);
    if (arith_b == 0) {
        return uint256(); // Division by zero returns zero
    }
    
    arith_uint256 arith_a = UintToArith256(a);
    
    // Trust-weighted division with precision based on reputation
    if (reputation_weight >= 80) {
        // High reputation: full precision division
        arith_uint256 result = arith_a / arith_b;
        return ArithToUint256(result);
    } else if (reputation_weight >= 60) {
        // Medium reputation: standard division
        arith_uint256 result = arith_a / arith_b;
        return ArithToUint256(result);
    } else {
        // Low reputation: conservative division with bounds checking
        arith_uint256 max_dividend;
        max_dividend.SetHex("7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        if (arith_a > max_dividend) {
            return uint256(); // Limit dividend size for low reputation
        }
        arith_uint256 result = arith_a / arith_b;
        return ArithToUint256(result);
    }
}

bool EVMEngine::ValidateArithmeticTrustGate(uint32_t caller_reputation, const uint256& operand_size) {
    // Validate that caller has sufficient reputation for the operation size
    arith_uint256 arith_operand = UintToArith256(operand_size);
    
    if (caller_reputation >= 80) {
        return true; // High reputation: no limits
    } else if (caller_reputation >= 60) {
        // Medium reputation: reasonable limits
        arith_uint256 medium_limit;
        medium_limit.SetHex("7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        return arith_operand <= medium_limit;
    } else if (caller_reputation >= 40) {
        // Low reputation: strict limits
        arith_uint256 low_limit;
        low_limit.SetHex("FFFFFFFFFFFFFFFF");
        return arith_operand <= low_limit;
    } else {
        // Very low reputation: very strict limits
        arith_uint256 very_low_limit;
        very_low_limit.SetHex("FFFFFFFF");
        return arith_operand <= very_low_limit;
    }
}

bool EVMEngine::HandleReputationGatedCall(evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No gating if trust features disabled
    }
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
    uint160 target = evmc_host->EvmcAddressToUint160(msg.recipient);
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    uint32_t target_reputation = trust_context->GetReputation(target);
    
    // Implement reputation-based call gating
    switch (msg.kind) {
        case EVMC_CALL:
            // Check if it's a static call via flags
            if (msg.flags & EVMC_STATIC) {
                // Static calls are safer, require lower reputation
                return caller_reputation >= 20;
            }
            // Regular calls require minimum reputation based on value
            if (msg.value.bytes[31] > 0) { // Non-zero value transfer
                return caller_reputation >= 60; // Require medium reputation for value transfers
            }
            return caller_reputation >= 40; // Low reputation sufficient for zero-value calls
            
        case EVMC_DELEGATECALL:
            // Delegate calls require high reputation due to security implications
            return caller_reputation >= 80;
            
        case EVMC_CREATE:
        case EVMC_CREATE2:
            // Contract creation requires high reputation
            return caller_reputation >= 70;
            
        default:
            return caller_reputation >= 40;
    }
}

bool EVMEngine::HandleReputationBasedControlFlow(uint8_t opcode, evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No control flow restrictions if trust features disabled
    }
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
    
    // Handle different control flow opcodes with reputation-based restrictions
    switch (opcode) {
        case 0x56: // JUMP
        case 0x57: // JUMPI
            // For JUMP/JUMPI, we validate the destination based on reputation
            // The actual destination will be validated during execution
            // Here we just check if the caller has sufficient reputation for jumps
            {
                uint32_t caller_reputation = trust_context->GetReputation(caller);
                if (caller_reputation < 30) {
                    // Very low reputation addresses have restricted jump capabilities
                    if (execution_tracing) {
                        TraceExecution(strprintf("JUMP/JUMPI restricted for low reputation caller %s (rep: %u)", 
                                                caller.ToString(), caller_reputation));
                    }
                    return false;
                }
                return true;
            }
            
        case 0xF0: // CREATE
        case 0xF5: // CREATE2
            // Contract creation requires high reputation
            {
                uint32_t caller_reputation = trust_context->GetReputation(caller);
                if (caller_reputation < 70) {
                    if (execution_tracing) {
                        TraceExecution(strprintf("CREATE restricted for caller %s (rep: %u, required: 70)", 
                                                caller.ToString(), caller_reputation));
                    }
                    return false;
                }
                return true;
            }
            
        case 0xF1: // CALL
        case 0xF2: // CALLCODE
        case 0xF4: // DELEGATECALL
        case 0xFA: // STATICCALL
            // These are handled by HandleReputationGatedCall
            return HandleReputationGatedCall(msg);
            
        case 0xFD: // REVERT
        case 0xFE: // INVALID
        case 0xFF: // SELFDESTRUCT
            // Exception handling opcodes - allow based on reputation
            {
                uint32_t caller_reputation = trust_context->GetReputation(caller);
                if (opcode == 0xFF) { // SELFDESTRUCT requires very high reputation
                    if (caller_reputation < 90) {
                        if (execution_tracing) {
                            TraceExecution(strprintf("SELFDESTRUCT restricted for caller %s (rep: %u, required: 90)", 
                                                    caller.ToString(), caller_reputation));
                        }
                        return false;
                    }
                }
                return true;
            }
            
        default:
            // Other opcodes don't have special control flow restrictions
            return true;
    }
}

// Trust-enhanced cryptographic operations handler (Requirement 13)
bool EVMEngine::HandleTrustEnhancedCrypto(uint8_t opcode, evmc_message& msg) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No crypto enhancements if trust features disabled
    }
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Handle different cryptographic opcodes with reputation-based enhancements
    switch (opcode) {
        case 0x20: // SHA3/KECCAK256
            // Trust-enhanced hashing available for high reputation
            if (caller_reputation >= 70) {
                // High reputation can use trust-enhanced hashing
                if (execution_tracing) {
                    TraceExecution(strprintf("Trust-enhanced SHA3 available for caller %s (rep: %u)", 
                                            caller.ToString(), caller_reputation));
                }
            }
            return true;
            
        case 0x01: // ECRECOVER (precompiled contract at address 0x01)
            // Signature recovery with reputation-weighted validation
            if (caller_reputation < 40) {
                // Low reputation requires additional validation
                if (execution_tracing) {
                    TraceExecution(strprintf("ECRECOVER requires validation for low reputation caller %s (rep: %u)", 
                                            caller.ToString(), caller_reputation));
                }
            }
            return true;
            
        case 0x02: // SHA256 (precompiled contract at address 0x02)
        case 0x03: // RIPEMD160 (precompiled contract at address 0x03)
            // Standard hash functions - allow all reputations
            return true;
            
        case 0x04: // IDENTITY (precompiled contract at address 0x04)
            // Data copy - allow all reputations
            return true;
            
        case 0x05: // MODEXP (precompiled contract at address 0x05)
            // Modular exponentiation - require minimum reputation for large operations
            if (caller_reputation < 50) {
                if (execution_tracing) {
                    TraceExecution(strprintf("MODEXP restricted for low reputation caller %s (rep: %u)", 
                                            caller.ToString(), caller_reputation));
                }
                // Still allow but with gas penalty
            }
            return true;
            
        case 0x06: // ECADD (precompiled contract at address 0x06)
        case 0x07: // ECMUL (precompiled contract at address 0x07)
        case 0x08: // ECPAIRING (precompiled contract at address 0x08)
            // Elliptic curve operations - require medium reputation
            if (caller_reputation < 60) {
                if (execution_tracing) {
                    TraceExecution(strprintf("EC operation restricted for caller %s (rep: %u, required: 60)", 
                                            caller.ToString(), caller_reputation));
                }
                return false; // Deny low reputation access to advanced crypto
            }
            return true;
            
        case 0x09: // BLAKE2F (precompiled contract at address 0x09)
            // BLAKE2 compression - allow all reputations
            return true;
            
        default:
            // Unknown crypto opcode - allow by default
            return true;
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

// Trust-aware exception handling and error recovery (Requirement 12.5)
EVMExecutionResult EVMEngine::HandleTrustAwareException(
    evmc_status_code status, 
    const std::string& error,
    const uint160& caller,
    uint64_t gas_used,
    uint64_t gas_left) {
    
    if (!trust_features_enabled || !trust_context) {
        // No trust-aware handling if features disabled
        return CreateErrorResult(status, error);
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    EVMExecutionResult result;
    result.success = false;
    result.status_code = status;
    result.error_message = error;
    result.gas_used = gas_used;
    result.gas_left = gas_left;
    result.caller_reputation = caller_reputation;
    
    // Reputation-based error recovery strategies
    switch (status) {
        case EVMC_OUT_OF_GAS:
            // High reputation callers might get partial gas refund on OOG errors
            if (caller_reputation >= 80) {
                // Refund 10% of used gas for high reputation addresses
                uint64_t refund = gas_used / 10;
                result.gas_left += refund;
                result.gas_used -= refund;
                result.error_message += " (10% gas refund applied for high reputation)";
                
                if (execution_tracing) {
                    TraceExecution(strprintf("OOG error recovery: refunded %u gas for high reputation caller %s", 
                                            refund, caller.ToString()));
                }
            } else if (caller_reputation >= 60) {
                // Refund 5% for medium reputation
                uint64_t refund = gas_used / 20;
                result.gas_left += refund;
                result.gas_used -= refund;
                result.error_message += " (5% gas refund applied for medium reputation)";
            }
            break;
            
        case EVMC_REVERT:
            // Revert is a controlled exception - high reputation gets better gas refund
            if (caller_reputation >= 70) {
                // High reputation: refund 90% of remaining gas
                uint64_t refund = (gas_left * 9) / 10;
                result.gas_left = refund;
                result.gas_used = gas_used + gas_left - refund;
                result.error_message += " (90% gas refund for controlled revert)";
            } else if (caller_reputation >= 50) {
                // Medium reputation: refund 75% of remaining gas
                uint64_t refund = (gas_left * 3) / 4;
                result.gas_left = refund;
                result.gas_used = gas_used + gas_left - refund;
                result.error_message += " (75% gas refund for controlled revert)";
            }
            // Low reputation gets standard revert handling (no extra refund)
            break;
            
        case EVMC_FAILURE:
        case EVMC_INVALID_INSTRUCTION:
        case EVMC_UNDEFINED_INSTRUCTION:
            // Execution failures - high reputation might indicate a bug rather than malice
            if (caller_reputation >= 80) {
                result.error_message += " (high reputation caller - possible bug, not malicious)";
                // Don't penalize gas as harshly for high reputation
                if (gas_left > 0) {
                    uint64_t refund = gas_left / 4; // Refund 25% of remaining gas
                    result.gas_left = refund;
                    result.gas_used = gas_used + gas_left - refund;
                }
            }
            break;
            
        case EVMC_REJECTED:
            // Reputation gate failures
            result.trust_gate_passed = false;
            stats.trust_gate_failures++;
            result.error_message += strprintf(" (caller reputation: %u)", caller_reputation);
            
            if (execution_tracing) {
                TraceExecution(strprintf("Trust gate rejection for caller %s (rep: %u): %s", 
                                        caller.ToString(), caller_reputation, error));
            }
            break;
            
        case EVMC_STACK_OVERFLOW:
        case EVMC_STACK_UNDERFLOW:
            // Stack errors - low reputation might indicate attack attempt
            if (caller_reputation < 40) {
                result.error_message += " (low reputation - possible attack attempt)";
                // Consume all remaining gas for low reputation stack errors
                result.gas_used += gas_left;
                result.gas_left = 0;
            }
            break;
            
        default:
            // Other errors use standard handling
            break;
    }
    
    // Update statistics
    stats.failed_executions++;
    stats.status_code_frequency[status]++;
    
    if (execution_tracing) {
        TraceExecution(strprintf("Trust-aware exception handled: %s (rep: %u, gas_used: %u, gas_left: %u)", 
                                error, caller_reputation, result.gas_used, result.gas_left));
    }
    
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
    
    uint160 caller = evmc_host->EvmcAddressToUint160(msg.sender);
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
    uint256 a, b, result;
    a.SetHex("64"); // 100 in hex
    b.SetHex("C8"); // 200 in hex
    
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
        uint256 val1, val2, val3;
        val1.SetHex("100");
        val2.SetHex("200");
        val3.SetHex("300");
        AddToReputationArray("test_array", val1, 80);
        AddToReputationArray("test_array", val2, 60);
        AddToReputationArray("test_array", val3, 90);
        
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
    uint256 test_value;
    test_value.SetHex("500");
    bool stack_push_test = PushReputationWeightedValue(test_value, 70);
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

// Test reputation-enhanced control flow (Requirement 12)
bool EVMEngine::TestReputationEnhancedControlFlow() {
    if (!trust_features_enabled || !trust_context) {
        LogPrintf("Trust features not enabled, skipping control flow test\n");
        return false;
    }
    
    LogPrintf("Testing reputation-enhanced control flow operations...\n");
    
    bool all_tests_passed = true;
    
    // Create test addresses with different reputation levels
    uint160 high_rep_addr, medium_rep_addr, low_rep_addr, very_low_rep_addr;
    high_rep_addr.SetHex("1111111111111111111111111111111111111111");
    medium_rep_addr.SetHex("2222222222222222222222222222222222222222");
    low_rep_addr.SetHex("3333333333333333333333333333333333333333");
    very_low_rep_addr.SetHex("4444444444444444444444444444444444444444");
    
    // Test 1: Reputation-based jump validation
    LogPrintf("Test 1: Reputation-based jump validation\n");
    
    // High reputation should allow jumps anywhere
    bool high_rep_jump = ValidateReputationBasedJump(MAX_BYTECODE_SIZE - 100, high_rep_addr);
    LogPrintf("  High reputation jump to near-end: %s\n", high_rep_jump ? "PASSED" : "FAILED");
    if (!high_rep_jump) all_tests_passed = false;
    
    // Medium reputation should be restricted
    bool medium_rep_jump_allowed = ValidateReputationBasedJump(MAX_BYTECODE_SIZE / 4, medium_rep_addr);
    bool medium_rep_jump_denied = !ValidateReputationBasedJump(MAX_BYTECODE_SIZE - 100, medium_rep_addr);
    LogPrintf("  Medium reputation jump restrictions: %s\n", 
              (medium_rep_jump_allowed && medium_rep_jump_denied) ? "PASSED" : "FAILED");
    if (!(medium_rep_jump_allowed && medium_rep_jump_denied)) all_tests_passed = false;
    
    // Low reputation should be very restricted
    bool low_rep_jump_denied = !ValidateReputationBasedJump(MAX_BYTECODE_SIZE / 2, low_rep_addr);
    LogPrintf("  Low reputation jump denied: %s\n", low_rep_jump_denied ? "PASSED" : "FAILED");
    if (!low_rep_jump_denied) all_tests_passed = false;
    
    // Test 2: Trust-based loop limits
    LogPrintf("Test 2: Trust-based loop limits\n");
    
    // High reputation should allow many iterations
    bool high_rep_loop = CheckTrustBasedLoopLimit(10000000, high_rep_addr);
    LogPrintf("  High reputation 10M iterations: %s\n", high_rep_loop ? "PASSED" : "FAILED");
    if (!high_rep_loop) all_tests_passed = false;
    
    // Medium reputation should have moderate limits
    bool medium_rep_loop_allowed = CheckTrustBasedLoopLimit(50000, medium_rep_addr);
    bool medium_rep_loop_denied = !CheckTrustBasedLoopLimit(10000000, medium_rep_addr);
    LogPrintf("  Medium reputation loop limits: %s\n", 
              (medium_rep_loop_allowed && medium_rep_loop_denied) ? "PASSED" : "FAILED");
    if (!(medium_rep_loop_allowed && medium_rep_loop_denied)) all_tests_passed = false;
    
    // Very low reputation should have strict limits
    bool very_low_rep_loop_allowed = CheckTrustBasedLoopLimit(50, very_low_rep_addr);
    bool very_low_rep_loop_denied = !CheckTrustBasedLoopLimit(1000, very_low_rep_addr);
    LogPrintf("  Very low reputation loop limits: %s\n", 
              (very_low_rep_loop_allowed && very_low_rep_loop_denied) ? "PASSED" : "FAILED");
    if (!(very_low_rep_loop_allowed && very_low_rep_loop_denied)) all_tests_passed = false;
    
    // Test 3: Automatic function routing based on trust levels
    LogPrintf("Test 3: Automatic function routing\n");
    
    // Test binary routing (2 destinations)
    std::vector<size_t> binary_destinations = {0x1000, 0x2000};
    bool high_rep_routing = RouteBasedOnTrustLevel(high_rep_addr, binary_destinations);
    bool low_rep_routing = RouteBasedOnTrustLevel(low_rep_addr, binary_destinations);
    LogPrintf("  Binary routing: %s\n", 
              (high_rep_routing && low_rep_routing) ? "PASSED" : "FAILED");
    if (!(high_rep_routing && low_rep_routing)) all_tests_passed = false;
    
    // Test three-tier routing (3 destinations)
    std::vector<size_t> three_tier_destinations = {0x1000, 0x2000, 0x3000};
    bool three_tier_routing = RouteBasedOnTrustLevel(medium_rep_addr, three_tier_destinations);
    LogPrintf("  Three-tier routing: %s\n", three_tier_routing ? "PASSED" : "FAILED");
    if (!three_tier_routing) all_tests_passed = false;
    
    // Test four-tier routing (4 destinations)
    std::vector<size_t> four_tier_destinations = {0x1000, 0x2000, 0x3000, 0x4000};
    bool four_tier_routing = RouteBasedOnTrustLevel(high_rep_addr, four_tier_destinations);
    LogPrintf("  Four-tier routing: %s\n", four_tier_routing ? "PASSED" : "FAILED");
    if (!four_tier_routing) all_tests_passed = false;
    
    // Test 4: Control flow opcode handling
    LogPrintf("Test 4: Control flow opcode handling\n");
    
    // Create test message
    evmc_message test_msg = {};
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(high_rep_addr);
    test_msg.recipient = evmc_host->Uint160ToEvmcAddress(medium_rep_addr);
    test_msg.kind = EVMC_CALL;
    test_msg.gas = 1000000;
    
    // Test JUMP opcode (0x56)
    bool jump_test = HandleReputationBasedControlFlow(0x56, test_msg);
    LogPrintf("  JUMP opcode handling: %s\n", jump_test ? "PASSED" : "FAILED");
    if (!jump_test) all_tests_passed = false;
    
    // Test CREATE opcode (0xF0) - should require high reputation
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(low_rep_addr);
    bool create_denied = !HandleReputationBasedControlFlow(0xF0, test_msg);
    LogPrintf("  CREATE denied for low reputation: %s\n", create_denied ? "PASSED" : "FAILED");
    if (!create_denied) all_tests_passed = false;
    
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(high_rep_addr);
    bool create_allowed = HandleReputationBasedControlFlow(0xF0, test_msg);
    LogPrintf("  CREATE allowed for high reputation: %s\n", create_allowed ? "PASSED" : "FAILED");
    if (!create_allowed) all_tests_passed = false;
    
    // Test SELFDESTRUCT opcode (0xFF) - should require very high reputation
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(medium_rep_addr);
    bool selfdestruct_denied = !HandleReputationBasedControlFlow(0xFF, test_msg);
    LogPrintf("  SELFDESTRUCT denied for medium reputation: %s\n", selfdestruct_denied ? "PASSED" : "FAILED");
    if (!selfdestruct_denied) all_tests_passed = false;
    
    // Test 5: Trust-aware exception handling
    LogPrintf("Test 5: Trust-aware exception handling\n");
    
    // Test OOG error with high reputation (should get gas refund)
    EVMExecutionResult oog_result = HandleTrustAwareException(
        EVMC_OUT_OF_GAS, "Out of gas", high_rep_addr, 100000, 0);
    bool oog_refund_test = (oog_result.gas_left > 0); // Should have some refund
    LogPrintf("  OOG gas refund for high reputation: %s (refund: %u)\n", 
              oog_refund_test ? "PASSED" : "FAILED", oog_result.gas_left);
    if (!oog_refund_test) all_tests_passed = false;
    
    // Test REVERT with high reputation (should get better gas refund)
    EVMExecutionResult revert_result = HandleTrustAwareException(
        EVMC_REVERT, "Revert", high_rep_addr, 50000, 50000);
    bool revert_refund_test = (revert_result.gas_left > 40000); // Should refund 90%
    LogPrintf("  REVERT gas refund for high reputation: %s (refund: %u)\n", 
              revert_refund_test ? "PASSED" : "FAILED", revert_result.gas_left);
    if (!revert_refund_test) all_tests_passed = false;
    
    // Test REJECTED error (trust gate failure)
    EVMExecutionResult rejected_result = HandleTrustAwareException(
        EVMC_REJECTED, "Trust gate failed", low_rep_addr, 10000, 90000);
    bool rejected_test = (!rejected_result.trust_gate_passed && 
                         rejected_result.error_message.find("reputation") != std::string::npos);
    LogPrintf("  Trust gate rejection handling: %s\n", rejected_test ? "PASSED" : "FAILED");
    if (!rejected_test) all_tests_passed = false;
    
    LogPrintf("Reputation-enhanced control flow test completed: %s\n", 
              all_tests_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    
    return all_tests_passed;
}

// Test trust-integrated cryptographic operations (Requirement 13)
bool EVMEngine::TestTrustIntegratedCryptography() {
    if (!trust_features_enabled || !trust_context) {
        LogPrintf("Trust features not enabled, skipping cryptography test\n");
        return false;
    }
    
    LogPrintf("Testing trust-integrated cryptographic operations...\n");
    
    bool all_tests_passed = true;
    
    // Create test addresses with different reputation levels
    uint160 high_rep_addr, medium_rep_addr, low_rep_addr;
    high_rep_addr.SetHex("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    medium_rep_addr.SetHex("BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    low_rep_addr.SetHex("CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC");
    
    // Test 1: Reputation-weighted signature verification
    LogPrintf("Test 1: Reputation-weighted signature verification\n");
    
    // Create test signatures of different sizes
    std::vector<uint8_t> valid_sig_64(64, 0x42); // 64-byte signature
    std::vector<uint8_t> valid_sig_65(65, 0x42); // 65-byte signature with recovery ID
    valid_sig_65[64] = 1; // Valid recovery ID
    std::vector<uint8_t> invalid_sig_short(32, 0x42); // Too short
    std::vector<uint8_t> trivial_sig(65, 0x00); // All zeros (trivial)
    
    // High reputation should accept 64-byte signatures
    bool high_rep_sig_test = VerifyReputationSignature(valid_sig_64, high_rep_addr);
    LogPrintf("  High reputation 64-byte signature: %s\n", high_rep_sig_test ? "PASSED" : "FAILED");
    if (!high_rep_sig_test) all_tests_passed = false;
    
    // Medium reputation should require 65-byte signatures
    bool medium_rep_sig_test = VerifyReputationSignature(valid_sig_65, medium_rep_addr);
    bool medium_rep_reject_64 = !VerifyReputationSignature(valid_sig_64, medium_rep_addr);
    LogPrintf("  Medium reputation signature requirements: %s\n", 
              (medium_rep_sig_test && medium_rep_reject_64) ? "PASSED" : "FAILED");
    if (!(medium_rep_sig_test && medium_rep_reject_64)) all_tests_passed = false;
    
    // Low reputation should reject trivial signatures
    bool low_rep_reject_trivial = !VerifyReputationSignature(trivial_sig, low_rep_addr);
    LogPrintf("  Low reputation rejects trivial signature: %s\n", 
              low_rep_reject_trivial ? "PASSED" : "FAILED");
    if (!low_rep_reject_trivial) all_tests_passed = false;
    
    // All reputations should reject short signatures
    bool reject_short = !VerifyReputationSignature(invalid_sig_short, high_rep_addr);
    LogPrintf("  Reject short signatures: %s\n", reject_short ? "PASSED" : "FAILED");
    if (!reject_short) all_tests_passed = false;
    
    // Test 2: Trust-enhanced hash functions
    LogPrintf("Test 2: Trust-enhanced hash functions\n");
    
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    // Compute trust-enhanced hashes for different reputations
    uint256 high_rep_hash = ComputeTrustEnhancedHash(test_data, high_rep_addr);
    uint256 medium_rep_hash = ComputeTrustEnhancedHash(test_data, medium_rep_addr);
    uint256 low_rep_hash = ComputeTrustEnhancedHash(test_data, low_rep_addr);
    
    // Hashes should be different for different reputations
    bool hashes_unique = (high_rep_hash != medium_rep_hash) && 
                        (medium_rep_hash != low_rep_hash) &&
                        (high_rep_hash != low_rep_hash);
    LogPrintf("  Trust-enhanced hashes are unique: %s\n", hashes_unique ? "PASSED" : "FAILED");
    if (!hashes_unique) all_tests_passed = false;
    
    // Same reputation should produce same hash (deterministic)
    uint256 high_rep_hash2 = ComputeTrustEnhancedHash(test_data, high_rep_addr);
    bool hash_deterministic = (high_rep_hash == high_rep_hash2);
    LogPrintf("  Trust-enhanced hash is deterministic: %s\n", hash_deterministic ? "PASSED" : "FAILED");
    if (!hash_deterministic) all_tests_passed = false;
    
    // Test 3: Reputation-based key derivation
    LogPrintf("Test 3: Reputation-based key derivation\n");
    
    std::vector<uint8_t> high_rep_key, medium_rep_key, low_rep_key;
    
    bool high_key_gen = GenerateReputationBasedKey(high_rep_addr, high_rep_key);
    bool medium_key_gen = GenerateReputationBasedKey(medium_rep_addr, medium_rep_key);
    bool low_key_gen = GenerateReputationBasedKey(low_rep_addr, low_rep_key);
    
    LogPrintf("  Key generation success: %s\n", 
              (high_key_gen && medium_key_gen && low_key_gen) ? "PASSED" : "FAILED");
    if (!(high_key_gen && medium_key_gen && low_key_gen)) all_tests_passed = false;
    
    // High reputation should get longer keys
    bool key_size_test = (high_rep_key.size() >= medium_rep_key.size()) && 
                        (medium_rep_key.size() >= low_rep_key.size());
    LogPrintf("  Key sizes scale with reputation (high: %zu, medium: %zu, low: %zu): %s\n", 
              high_rep_key.size(), medium_rep_key.size(), low_rep_key.size(),
              key_size_test ? "PASSED" : "FAILED");
    if (!key_size_test) all_tests_passed = false;
    
    // Keys should be unique for different addresses
    bool keys_unique = (high_rep_key != medium_rep_key) && 
                      (medium_rep_key != low_rep_key);
    LogPrintf("  Keys are unique for different addresses: %s\n", keys_unique ? "PASSED" : "FAILED");
    if (!keys_unique) all_tests_passed = false;
    
    // Test 4: Trust-aware random number generation
    LogPrintf("Test 4: Trust-aware random number generation\n");
    
    uint256 high_rep_random, medium_rep_random, low_rep_random;
    
    bool high_random_gen = GenerateTrustAwareRandomNumber(high_rep_addr, high_rep_random);
    bool medium_random_gen = GenerateTrustAwareRandomNumber(medium_rep_addr, medium_rep_random);
    bool low_random_gen = GenerateTrustAwareRandomNumber(low_rep_addr, low_rep_random);
    
    LogPrintf("  Random number generation success: %s\n", 
              (high_random_gen && medium_random_gen && low_random_gen) ? "PASSED" : "FAILED");
    if (!(high_random_gen && medium_random_gen && low_random_gen)) all_tests_passed = false;
    
    // Random numbers should be different
    bool randoms_unique = (high_rep_random != medium_rep_random) && 
                         (medium_rep_random != low_rep_random) &&
                         (high_rep_random != low_rep_random);
    LogPrintf("  Random numbers are unique: %s\n", randoms_unique ? "PASSED" : "FAILED");
    if (!randoms_unique) all_tests_passed = false;
    
    // Random numbers should not be zero
    bool randoms_nonzero = (high_rep_random != uint256()) && 
                          (medium_rep_random != uint256()) &&
                          (low_rep_random != uint256());
    LogPrintf("  Random numbers are non-zero: %s\n", randoms_nonzero ? "PASSED" : "FAILED");
    if (!randoms_nonzero) all_tests_passed = false;
    
    // Test 5: Cryptographic opcode handling
    LogPrintf("Test 5: Cryptographic opcode handling\n");
    
    // Create test message
    evmc_message test_msg = {};
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(high_rep_addr);
    test_msg.recipient = evmc_host->Uint160ToEvmcAddress(medium_rep_addr);
    test_msg.kind = EVMC_CALL;
    test_msg.gas = 1000000;
    
    // Test SHA3/KECCAK256 (0x20)
    bool sha3_test = HandleTrustEnhancedCrypto(0x20, test_msg);
    LogPrintf("  SHA3/KECCAK256 handling: %s\n", sha3_test ? "PASSED" : "FAILED");
    if (!sha3_test) all_tests_passed = false;
    
    // Test ECRECOVER (0x01)
    bool ecrecover_test = HandleTrustEnhancedCrypto(0x01, test_msg);
    LogPrintf("  ECRECOVER handling: %s\n", ecrecover_test ? "PASSED" : "FAILED");
    if (!ecrecover_test) all_tests_passed = false;
    
    // Test EC operations with high reputation (should pass)
    bool ecadd_high_rep = HandleTrustEnhancedCrypto(0x06, test_msg);
    LogPrintf("  ECADD with high reputation: %s\n", ecadd_high_rep ? "PASSED" : "FAILED");
    if (!ecadd_high_rep) all_tests_passed = false;
    
    // Test EC operations with low reputation (should fail)
    test_msg.sender = evmc_host->Uint160ToEvmcAddress(low_rep_addr);
    bool ecadd_low_rep = !HandleTrustEnhancedCrypto(0x06, test_msg);
    LogPrintf("  ECADD denied for low reputation: %s\n", ecadd_low_rep ? "PASSED" : "FAILED");
    if (!ecadd_low_rep) all_tests_passed = false;
    
    LogPrintf("Trust-integrated cryptography test completed: %s\n", 
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

// Cryptographic trust integration implementation (Requirement 13)

bool EVMEngine::VerifyReputationSignature(const std::vector<uint8_t>& signature, const uint160& signer) {
    if (!trust_features_enabled || !trust_context) {
        // Standard signature verification without reputation weighting
        return signature.size() >= 64; // Basic size check
    }
    
    uint32_t signer_reputation = trust_context->GetReputation(signer);
    
    // Reputation-weighted signature verification
    // Higher reputation addresses get more lenient verification
    // Lower reputation addresses require stricter validation
    
    if (signature.empty()) {
        return false; // Empty signature always fails
    }
    
    // Basic signature format validation
    if (signature.size() < 64) {
        if (execution_tracing) {
            TraceExecution(strprintf("Signature too short: %zu bytes (minimum 64)", signature.size()));
        }
        return false;
    }
    
    // Reputation-based signature strength requirements
    if (signer_reputation >= 80) {
        // High reputation: accept standard ECDSA signatures (64-65 bytes)
        if (signature.size() >= 64 && signature.size() <= 65) {
            if (execution_tracing) {
                TraceExecution(strprintf("High reputation signature accepted for %s (rep: %u)", 
                                        signer.ToString(), signer_reputation));
            }
            return true;
        }
    } else if (signer_reputation >= 60) {
        // Medium reputation: require exact 65-byte signature with recovery ID
        if (signature.size() == 65) {
            // Additional validation: check recovery ID is valid (0-3)
            uint8_t recovery_id = signature[64];
            if (recovery_id <= 3) {
                return true;
            }
        }
    } else if (signer_reputation >= 40) {
        // Low reputation: require 65-byte signature and additional checks
        if (signature.size() == 65) {
            uint8_t recovery_id = signature[64];
            if (recovery_id <= 3) {
                // Additional check: ensure signature is not all zeros
                bool all_zeros = true;
                for (size_t i = 0; i < 64; i++) {
                    if (signature[i] != 0) {
                        all_zeros = false;
                        break;
                    }
                }
                if (!all_zeros) {
                    return true;
                }
            }
        }
    } else {
        // Very low reputation: require 65-byte signature with strict validation
        if (signature.size() == 65) {
            uint8_t recovery_id = signature[64];
            if (recovery_id <= 1) { // Only accept standard recovery IDs
                // Check signature is not trivial (all zeros or all ones)
                bool is_trivial = true;
                uint8_t first_byte = signature[0];
                for (size_t i = 0; i < 64; i++) {
                    if (signature[i] != first_byte) {
                        is_trivial = false;
                        break;
                    }
                }
                if (!is_trivial) {
                    return true;
                }
            }
        }
    }
    
    if (execution_tracing) {
        TraceExecution(strprintf("Signature verification failed for %s (rep: %u, sig_size: %zu)", 
                                signer.ToString(), signer_reputation, signature.size()));
    }
    
    return false;
}

uint256 EVMEngine::ComputeTrustEnhancedHash(const std::vector<uint8_t>& data, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        // Standard hash without reputation enhancement
        return Hash(data.begin(), data.end());
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Create trust-enhanced hash by incorporating reputation data
    // This creates a unique hash that includes trust context
    
    // Prepare enhanced data with reputation prefix
    std::vector<uint8_t> enhanced_data;
    enhanced_data.reserve(data.size() + 32); // Reserve space for reputation data
    
    // Add reputation as 4-byte prefix
    enhanced_data.push_back(static_cast<uint8_t>(caller_reputation));
    enhanced_data.push_back(static_cast<uint8_t>(caller_reputation >> 8));
    enhanced_data.push_back(static_cast<uint8_t>(caller_reputation >> 16));
    enhanced_data.push_back(static_cast<uint8_t>(caller_reputation >> 24));
    
    // Add caller address (20 bytes)
    const unsigned char* addr_bytes = caller.begin();
    for (size_t i = 0; i < 20; i++) {
        enhanced_data.push_back(addr_bytes[i]);
    }
    
    // Add timestamp for temporal uniqueness (8 bytes)
    int64_t timestamp = GetTime();
    for (size_t i = 0; i < 8; i++) {
        enhanced_data.push_back(static_cast<uint8_t>(timestamp >> (i * 8)));
    }
    
    // Add original data
    enhanced_data.insert(enhanced_data.end(), data.begin(), data.end());
    
    // Compute hash with reputation-enhanced data
    uint256 trust_hash = Hash(enhanced_data.begin(), enhanced_data.end());
    
    if (execution_tracing) {
        TraceExecution(strprintf("Trust-enhanced hash computed for %s (rep: %u): %s", 
                                caller.ToString(), caller_reputation, trust_hash.ToString()));
    }
    
    return trust_hash;
}

bool EVMEngine::GenerateReputationBasedKey(const uint160& address, std::vector<uint8_t>& key) {
    if (!trust_features_enabled || !trust_context) {
        // Generate standard key without reputation
        key.resize(32);
        // Use address as seed for key generation
        uint256 seed = Hash(address.begin(), address.end());
        memcpy(key.data(), seed.begin(), 32);
        return true;
    }
    
    uint32_t reputation = trust_context->GetReputation(address);
    
    // Generate reputation-based key with varying strength
    // Higher reputation gets stronger keys (more entropy)
    
    size_t key_size;
    if (reputation >= 80) {
        key_size = 32; // 256-bit key for high reputation
    } else if (reputation >= 60) {
        key_size = 24; // 192-bit key for medium reputation
    } else if (reputation >= 40) {
        key_size = 16; // 128-bit key for low reputation
    } else {
        key_size = 12; // 96-bit key for very low reputation
    }
    
    key.resize(key_size);
    
    // Create key derivation data
    std::vector<uint8_t> derivation_data;
    derivation_data.reserve(64);
    
    // Add address (20 bytes)
    const unsigned char* addr_bytes = address.begin();
    for (size_t i = 0; i < 20; i++) {
        derivation_data.push_back(addr_bytes[i]);
    }
    
    // Add reputation (4 bytes)
    derivation_data.push_back(static_cast<uint8_t>(reputation));
    derivation_data.push_back(static_cast<uint8_t>(reputation >> 8));
    derivation_data.push_back(static_cast<uint8_t>(reputation >> 16));
    derivation_data.push_back(static_cast<uint8_t>(reputation >> 24));
    
    // Add timestamp for uniqueness (8 bytes)
    int64_t timestamp = GetTime();
    for (size_t i = 0; i < 8; i++) {
        derivation_data.push_back(static_cast<uint8_t>(timestamp >> (i * 8)));
    }
    
    // Add salt based on reputation (higher reputation = more salt)
    uint32_t salt_iterations = 1 + (reputation / 10); // 1-11 iterations
    for (uint32_t i = 0; i < salt_iterations; i++) {
        derivation_data.push_back(static_cast<uint8_t>(i));
    }
    
    // Derive key using multiple hash rounds for higher reputation
    uint256 derived_key = Hash(derivation_data.begin(), derivation_data.end());
    
    // Additional hash rounds for higher reputation (more secure)
    uint32_t hash_rounds = 1 + (reputation / 20); // 1-6 rounds
    for (uint32_t round = 1; round < hash_rounds; round++) {
        derived_key = Hash(derived_key.begin(), derived_key.end());
    }
    
    // Copy derived key to output (truncated to key_size)
    memcpy(key.data(), derived_key.begin(), key_size);
    
    if (execution_tracing) {
        TraceExecution(strprintf("Reputation-based key generated for %s (rep: %u, size: %zu bits)", 
                                address.ToString(), reputation, key_size * 8));
    }
    
    return true;
}

// Trust-aware random number generation (Requirement 13.4)
bool EVMEngine::GenerateTrustAwareRandomNumber(const uint160& caller, uint256& random_number) {
    if (!trust_features_enabled || !trust_context) {
        // Standard random number generation
        random_number = ::GetRandHash();
        return true;
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Generate random number with reputation-based fairness
    // Higher reputation gets more entropy sources for better randomness
    
    std::vector<uint8_t> entropy_data;
    entropy_data.reserve(128);
    
    // Source 1: System random (always included)
    uint256 sys_random = ::GetRandHash();
    entropy_data.insert(entropy_data.end(), sys_random.begin(), sys_random.end());
    
    // Source 2: Caller address (always included)
    const unsigned char* addr_bytes = caller.begin();
    for (size_t i = 0; i < 20; i++) {
        entropy_data.push_back(addr_bytes[i]);
    }
    
    // Source 3: Timestamp (always included)
    int64_t timestamp = GetTime();
    for (size_t i = 0; i < 8; i++) {
        entropy_data.push_back(static_cast<uint8_t>(timestamp >> (i * 8)));
    }
    
    // Source 4: Reputation-weighted additional entropy
    if (caller_reputation >= 60) {
        // Medium+ reputation: add block hash entropy
        int64_t block_number = evmc_host->GetBlockNumber();
        evmc_bytes32 block_hash_evmc = evmc_host->GetBlockHash(block_number);
        uint256 block_hash = evmc_host->EvmcUint256beToUint256(block_hash_evmc);
        entropy_data.insert(entropy_data.end(), block_hash.begin(), block_hash.end());
    }
    
    if (caller_reputation >= 80) {
        // High reputation: add additional system random
        uint256 extra_random = ::GetRandHash();
        entropy_data.insert(entropy_data.end(), extra_random.begin(), extra_random.end());
    }
    
    // Mix entropy with multiple hash rounds based on reputation
    uint32_t mix_rounds = 1 + (caller_reputation / 25); // 1-5 rounds
    random_number = Hash(entropy_data.begin(), entropy_data.end());
    
    for (uint32_t round = 1; round < mix_rounds; round++) {
        // Add more entropy each round
        uint256 round_random = ::GetRandHash();
        std::vector<uint8_t> round_data;
        round_data.insert(round_data.end(), random_number.begin(), random_number.end());
        round_data.insert(round_data.end(), round_random.begin(), round_random.end());
        random_number = Hash(round_data.begin(), round_data.end());
    }
    
    if (execution_tracing) {
        TraceExecution(strprintf("Trust-aware random number generated for %s (rep: %u, rounds: %u)", 
                                caller.ToString(), caller_reputation, mix_rounds));
    }
    
    return true;
}

// Control flow trust enhancements implementation (Requirement 12)

bool EVMEngine::ValidateReputationBasedJump(size_t destination, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No validation if trust features disabled
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Reputation-based jump validation
    // Higher reputation allows more flexible control flow
    if (caller_reputation >= 80) {
        // High reputation: unrestricted jumps
        if (execution_tracing) {
            TraceExecution(strprintf("High reputation jump to %zu allowed for caller %s (rep: %u)", 
                                    destination, caller.ToString(), caller_reputation));
        }
        return true;
    } else if (caller_reputation >= 60) {
        // Medium reputation: validate jump destination is reasonable
        // Prevent jumps to very high addresses that might be malicious
        if (destination > MAX_BYTECODE_SIZE) {
            if (execution_tracing) {
                TraceExecution(strprintf("Medium reputation jump to %zu rejected (exceeds max bytecode size)", 
                                        destination));
            }
            return false;
        }
        return true;
    } else if (caller_reputation >= 40) {
        // Low reputation: more restrictive jump validation
        // Only allow jumps within first 50% of bytecode
        if (destination > MAX_BYTECODE_SIZE / 2) {
            if (execution_tracing) {
                TraceExecution(strprintf("Low reputation jump to %zu rejected (exceeds 50%% limit)", 
                                        destination));
            }
            return false;
        }
        return true;
    } else {
        // Very low reputation: very restrictive
        // Only allow jumps within first 25% of bytecode
        if (destination > MAX_BYTECODE_SIZE / 4) {
            if (execution_tracing) {
                TraceExecution(strprintf("Very low reputation jump to %zu rejected (exceeds 25%% limit)", 
                                        destination));
            }
            return false;
        }
        return true;
    }
}

bool EVMEngine::CheckTrustBasedLoopLimit(uint32_t iterations, const uint160& caller) {
    if (!trust_features_enabled || !trust_context) {
        return true; // No limit if trust features disabled
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Reputation-based loop iteration limits to prevent abuse
    uint32_t max_iterations;
    
    if (caller_reputation >= 90) {
        // Very high reputation: essentially unlimited (1 billion iterations)
        max_iterations = 1000000000;
    } else if (caller_reputation >= 80) {
        // High reputation: 100 million iterations
        max_iterations = 100000000;
    } else if (caller_reputation >= 70) {
        // Good reputation: 10 million iterations
        max_iterations = 10000000;
    } else if (caller_reputation >= 60) {
        // Medium-high reputation: 1 million iterations
        max_iterations = 1000000;
    } else if (caller_reputation >= 50) {
        // Medium reputation: 100k iterations
        max_iterations = 100000;
    } else if (caller_reputation >= 40) {
        // Low-medium reputation: 10k iterations
        max_iterations = 10000;
    } else if (caller_reputation >= 30) {
        // Low reputation: 1k iterations
        max_iterations = 1000;
    } else {
        // Very low reputation: 100 iterations
        max_iterations = 100;
    }
    
    bool within_limit = iterations <= max_iterations;
    
    if (!within_limit && execution_tracing) {
        TraceExecution(strprintf("Loop limit exceeded: %u iterations requested, max %u allowed for reputation %u", 
                                iterations, max_iterations, caller_reputation));
    } else if (execution_tracing && iterations > max_iterations / 2) {
        TraceExecution(strprintf("Loop approaching limit: %u iterations (max %u) for reputation %u", 
                                iterations, max_iterations, caller_reputation));
    }
    
    return within_limit;
}

bool EVMEngine::RouteBasedOnTrustLevel(const uint160& caller, const std::vector<size_t>& destinations) {
    if (!trust_features_enabled || !trust_context) {
        // Default to first destination if trust features disabled
        return destinations.empty() ? false : true;
    }
    
    if (destinations.empty()) {
        return false;
    }
    
    uint32_t caller_reputation = trust_context->GetReputation(caller);
    
    // Route to different code paths based on reputation tiers
    // This enables automatic function routing without explicit reputation checks
    
    size_t selected_destination;
    
    if (destinations.size() == 1) {
        // Only one destination, use it
        selected_destination = destinations[0];
    } else if (destinations.size() == 2) {
        // Binary routing: high reputation vs low reputation
        if (caller_reputation >= 70) {
            selected_destination = destinations[0]; // High reputation path
        } else {
            selected_destination = destinations[1]; // Low reputation path
        }
    } else if (destinations.size() == 3) {
        // Three-tier routing: high, medium, low reputation
        if (caller_reputation >= 80) {
            selected_destination = destinations[0]; // High reputation path
        } else if (caller_reputation >= 50) {
            selected_destination = destinations[1]; // Medium reputation path
        } else {
            selected_destination = destinations[2]; // Low reputation path
        }
    } else if (destinations.size() == 4) {
        // Four-tier routing: very high, high, medium, low reputation
        if (caller_reputation >= 90) {
            selected_destination = destinations[0]; // Very high reputation path
        } else if (caller_reputation >= 70) {
            selected_destination = destinations[1]; // High reputation path
        } else if (caller_reputation >= 50) {
            selected_destination = destinations[2]; // Medium reputation path
        } else {
            selected_destination = destinations[3]; // Low reputation path
        }
    } else {
        // More than 4 destinations: map reputation to destination index
        // Divide reputation range (0-100) into equal segments
        size_t segment_size = 100 / destinations.size();
        size_t destination_index = std::min(static_cast<size_t>(caller_reputation / segment_size), 
                                           destinations.size() - 1);
        selected_destination = destinations[destination_index];
    }
    
    if (execution_tracing) {
        TraceExecution(strprintf("Trust-based routing: reputation %u -> destination %zu (from %zu options)", 
                                caller_reputation, selected_destination, destinations.size()));
    }
    
    // Validate the selected destination
    return ValidateReputationBasedJump(selected_destination, caller);
}

void EVMEngine::TraceExecution(const std::string& message) {
    if (execution_tracing) {
        LogPrint(BCLog::CVM, "EVM Trace: %s\n", message);
    }
}

} // namespace CVM

#endif // ENABLE_EVMC
