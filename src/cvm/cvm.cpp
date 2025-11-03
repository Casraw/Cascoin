// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cvm.h>
#include <cvm/opcodes.h>
#include <hash.h>
#include <pubkey.h>
#include <key.h>
#include <util.h>
#include <arith_uint256.h>

namespace CVM {

CVM::CVM() {
}

CVM::~CVM() {
}

bool CVM::Execute(const std::vector<uint8_t>& code, VMState& state, ContractStorage* storage) {
    if (code.empty()) {
        state.SetError("Empty bytecode");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    if (code.size() > MAX_CODE_SIZE) {
        state.SetError("Code size exceeds maximum");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    state.SetStatus(VMState::Status::RUNNING);
    state.SetPC(0);
    
    // Main execution loop
    while (state.IsRunning() && state.GetPC() < code.size()) {
        uint8_t opcodeByte = code[state.GetPC()];
        
        if (!IsValidOpCode(opcodeByte)) {
            state.SetError("Invalid opcode: " + std::to_string(opcodeByte));
            state.SetStatus(VMState::Status::INVALID_OPCODE);
            return false;
        }
        
        OpCode opcode = static_cast<OpCode>(opcodeByte);
        
        // Charge gas for the operation
        uint64_t gasCost = GetOpCodeGasCost(opcode);
        if (!state.UseGas(gasCost)) {
            return false; // Out of gas
        }
        
        // Execute the instruction
        if (!ExecuteInstruction(opcode, code, state, storage)) {
            return false;
        }
        
        // Increment PC if instruction didn't modify it (jumps)
        if (state.IsRunning() && opcode != OpCode::OP_JUMP && opcode != OpCode::OP_JUMPI) {
            state.IncrementPC();
        }
    }
    
    return state.GetStatus() == VMState::Status::STOPPED || 
           state.GetStatus() == VMState::Status::RETURNED;
}

bool CVM::ExecuteInstruction(OpCode opcode, const std::vector<uint8_t>& code, 
                             VMState& state, ContractStorage* storage) {
    switch (opcode) {
        // Stack operations
        case OpCode::OP_PUSH:
            return HandlePush(code, state);
        case OpCode::OP_POP:
            state.Pop();
            return state.GetStatus() != VMState::Status::STACK_UNDERFLOW;
        case OpCode::OP_DUP:
            state.Dup(0);
            return state.GetStatus() != VMState::Status::STACK_OVERFLOW &&
                   state.GetStatus() != VMState::Status::STACK_UNDERFLOW;
        case OpCode::OP_SWAP:
            state.Swap(1);
            return state.GetStatus() != VMState::Status::STACK_UNDERFLOW;
        
        // Arithmetic
        case OpCode::OP_ADD:
        case OpCode::OP_SUB:
        case OpCode::OP_MUL:
        case OpCode::OP_DIV:
        case OpCode::OP_MOD:
            return HandleArithmetic(opcode, state);
        
        // Logical operations
        case OpCode::OP_AND:
        case OpCode::OP_OR:
        case OpCode::OP_XOR:
        case OpCode::OP_NOT:
            return HandleLogical(opcode, state);
        
        // Comparison
        case OpCode::OP_EQ:
        case OpCode::OP_NE:
        case OpCode::OP_LT:
        case OpCode::OP_GT:
        case OpCode::OP_LE:
        case OpCode::OP_GE:
            return HandleComparison(opcode, state);
        
        // Control flow
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMPI:
            return HandleJump(opcode, code, state);
        case OpCode::OP_CALL:
            return HandleCall(code, state, storage);
        case OpCode::OP_RETURN:
            state.SetStatus(VMState::Status::RETURNED);
            return true;
        case OpCode::OP_STOP:
            state.SetStatus(VMState::Status::STOPPED);
            return true;
        
        // Storage
        case OpCode::OP_SLOAD:
        case OpCode::OP_SSTORE:
            return HandleStorage(opcode, state, storage);
        
        // Cryptographic
        case OpCode::OP_SHA256:
        case OpCode::OP_VERIFY_SIG:
        case OpCode::OP_PUBKEY:
            return HandleCrypto(opcode, state);
        
        // Context
        case OpCode::OP_ADDRESS:
        case OpCode::OP_BALANCE:
        case OpCode::OP_CALLER:
        case OpCode::OP_CALLVALUE:
        case OpCode::OP_TIMESTAMP:
        case OpCode::OP_BLOCKHASH:
        case OpCode::OP_BLOCKHEIGHT:
        case OpCode::OP_GAS:
            return HandleContext(opcode, state, storage);
        
        // Special
        case OpCode::OP_LOG:
            // Pop topic count and topics, then data
            return true; // Simplified for now
        case OpCode::OP_REVERT:
            state.SetStatus(VMState::Status::REVERTED);
            return false;
        
        default:
            state.SetError("Unimplemented opcode");
            state.SetStatus(VMState::Status::INVALID_OPCODE);
            return false;
    }
}

bool CVM::HandlePush(const std::vector<uint8_t>& code, VMState& state) {
    // Read size byte
    size_t pc = state.GetPC();
    if (pc + 1 >= code.size()) {
        state.SetError("PUSH: Not enough bytes for size");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    uint8_t size = code[pc + 1];
    if (size == 0 || size > 32) {
        state.SetError("PUSH: Invalid size");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    if (pc + 2 + size > code.size()) {
        state.SetError("PUSH: Not enough bytes for value");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    // Read the value
    arith_uint256 value = ReadImmediate(code, pc, size);
    state.Push(value);
    // Set PC to skip opcode byte (at pc) + size byte (at pc+1) + data bytes (size)
    // Main loop will increment by 1, so set to pc + 1 + size
    // Result after increment: pc + 2 + size (correct position of next instruction)
    state.SetPC(pc + 1 + size);
    
    return state.GetStatus() != VMState::Status::STACK_OVERFLOW;
}

bool CVM::HandleArithmetic(OpCode opcode, VMState& state) {
    if (state.StackSize() < 2) {
        state.SetError("Arithmetic: Stack underflow");
        state.SetStatus(VMState::Status::STACK_UNDERFLOW);
        return false;
    }
    
    arith_uint256 b = state.Pop();
    arith_uint256 a = state.Pop();
    arith_uint256 result;
    
    switch (opcode) {
        case OpCode::OP_ADD:
            result = a + b;
            break;
        case OpCode::OP_SUB:
            result = a - b;
            break;
        case OpCode::OP_MUL:
            result = a * b;
            break;
        case OpCode::OP_DIV:
            if (b == 0) {
                result = arith_uint256();
            } else {
                result = a / b;
            }
            break;
        case OpCode::OP_MOD:
            if (b == 0) {
                result = arith_uint256();
            } else {
                // Modulo operation: a % b = a - (a/b)*b
                result = a - (a / b) * b;
            }
            break;
        default:
            return false;
    }
    
    state.Push(result);
    return true;
}

bool CVM::HandleLogical(OpCode opcode, VMState& state) {
    size_t required = (opcode == OpCode::OP_NOT) ? 1 : 2;
    if (state.StackSize() < required) {
        state.SetError("Logical: Stack underflow");
        state.SetStatus(VMState::Status::STACK_UNDERFLOW);
        return false;
    }
    
    arith_uint256 result;
    
    if (opcode == OpCode::OP_NOT) {
        arith_uint256 a = state.Pop();
        result = ~a;
    } else {
        arith_uint256 b = state.Pop();
        arith_uint256 a = state.Pop();
        
        switch (opcode) {
            case OpCode::OP_AND:
                result = a & b;
                break;
            case OpCode::OP_OR:
                result = a | b;
                break;
            case OpCode::OP_XOR:
                result = a ^ b;
                break;
            default:
                return false;
        }
    }
    
    state.Push(result);
    return true;
}

bool CVM::HandleComparison(OpCode opcode, VMState& state) {
    if (state.StackSize() < 2) {
        state.SetError("Comparison: Stack underflow");
        state.SetStatus(VMState::Status::STACK_UNDERFLOW);
        return false;
    }
    
    arith_uint256 b = state.Pop();
    arith_uint256 a = state.Pop();
    bool result = false;
    
    switch (opcode) {
        case OpCode::OP_EQ:
            result = (a == b);
            break;
        case OpCode::OP_NE:
            result = (a != b);
            break;
        case OpCode::OP_LT:
            result = (a < b);
            break;
        case OpCode::OP_GT:
            result = (a > b);
            break;
        case OpCode::OP_LE:
            result = (a <= b);
            break;
        case OpCode::OP_GE:
            result = (a >= b);
            break;
        default:
            return false;
    }
    
    state.Push(result ? arith_uint256(1) : arith_uint256());
    return true;
}

bool CVM::HandleJump(OpCode opcode, const std::vector<uint8_t>& code, VMState& state) {
    if (state.StackSize() < 1) {
        state.SetError("Jump: Stack underflow");
        state.SetStatus(VMState::Status::STACK_UNDERFLOW);
        return false;
    }
    
    arith_uint256 target256 = state.Pop();
    size_t target = target256.GetLow64();
    
    if (opcode == OpCode::OP_JUMPI) {
        if (state.StackSize() < 1) {
            state.SetError("JUMPI: Stack underflow");
            state.SetStatus(VMState::Status::STACK_UNDERFLOW);
            return false;
        }
        arith_uint256 condition = state.Pop();
        if (condition == 0) {
            // Don't jump, continue execution
            return true;
        }
    }
    
    // Validate jump target
    if (target >= code.size()) {
        state.SetError("Jump: Invalid target");
        state.SetStatus(VMState::Status::INVALID_JUMP);
        return false;
    }
    
    state.SetPC(target);
    return true;
}

bool CVM::HandleStorage(OpCode opcode, VMState& state, ContractStorage* storage) {
    if (!storage) {
        state.SetError("Storage: No storage backend");
        state.SetStatus(VMState::Status::ERROR);
        return false;
    }
    
    if (opcode == OpCode::OP_SLOAD) {
        // SLOAD: key -> value
        if (state.StackSize() < 1) {
            state.SetError("SLOAD: Stack underflow");
            state.SetStatus(VMState::Status::STACK_UNDERFLOW);
            return false;
        }
        
        arith_uint256 key_arith = state.Pop();
        uint256 key = ArithToUint256(key_arith);
        uint256 value;
        
        if (!storage->Load(state.GetContractAddress(), key, value)) {
            value = uint256(); // Return 0 if key doesn't exist
        }
        
        state.Push(UintToArith256(value));
        return true;
    } else if (opcode == OpCode::OP_SSTORE) {
        // SSTORE: key value ->
        if (state.StackSize() < 2) {
            state.SetError("SSTORE: Stack underflow");
            state.SetStatus(VMState::Status::STACK_UNDERFLOW);
            return false;
        }
        
        arith_uint256 key_arith = state.Pop();
        arith_uint256 value_arith = state.Pop();
        uint256 key = ArithToUint256(key_arith);
        uint256 value = ArithToUint256(value_arith);
        
        return storage->Store(state.GetContractAddress(), key, value);
    }
    
    return false;
}

bool CVM::HandleCrypto(OpCode opcode, VMState& state) {
    if (opcode == OpCode::OP_SHA256) {
        if (state.StackSize() < 1) {
            state.SetError("SHA256: Stack underflow");
            state.SetStatus(VMState::Status::STACK_UNDERFLOW);
            return false;
        }
        
        arith_uint256 input_arith = state.Pop();
        uint256 input = ArithToUint256(input_arith);
        uint256 hash = Hash(input.begin(), input.end());
        state.Push(UintToArith256(hash));
        return true;
    } else if (opcode == OpCode::OP_VERIFY_SIG) {
        // Simplified: Pop message, signature, pubkey
        if (state.StackSize() < 3) {
            state.SetError("VERIFY_SIG: Stack underflow");
            state.SetStatus(VMState::Status::STACK_UNDERFLOW);
            return false;
        }
        
        state.Pop(); // pubkey
        state.Pop(); // signature
        state.Pop(); // message
        
        // Simplified: always return true for now
        state.Push(arith_uint256(1));
        return true;
    }
    
    return false;
}

bool CVM::HandleContext(OpCode opcode, VMState& state, ContractStorage* storage) {
    arith_uint256 value;
    
    switch (opcode) {
        case OpCode::OP_ADDRESS: {
            // Convert uint160 to arith_uint256
            uint160 addr = state.GetContractAddress();
            value = arith_uint256();
            for (int i = 0; i < 20; i++) {
                value = (value << 8) | arith_uint256(addr.begin()[i]);
            }
            break;
        }
        case OpCode::OP_CALLER: {
            uint160 caller = state.GetCallerAddress();
            value = arith_uint256();
            for (int i = 0; i < 20; i++) {
                value = (value << 8) | arith_uint256(caller.begin()[i]);
            }
            break;
        }
        case OpCode::OP_CALLVALUE:
            value = arith_uint256(state.GetCallValue());
            break;
        case OpCode::OP_TIMESTAMP:
            value = arith_uint256(state.GetTimestamp());
            break;
        case OpCode::OP_BLOCKHEIGHT:
            value = arith_uint256(state.GetBlockHeight());
            break;
        case OpCode::OP_BLOCKHASH:
            value = UintToArith256(state.GetBlockHash());
            break;
        case OpCode::OP_GAS:
            value = arith_uint256(state.GetGasRemaining());
            break;
        case OpCode::OP_BALANCE:
            // Simplified: return 0 for now
            value = arith_uint256();
            break;
        default:
            return false;
    }
    
    state.Push(value);
    return true;
}

bool CVM::HandleCall(const std::vector<uint8_t>& code, VMState& state, ContractStorage* storage) {
    // Simplified CALL implementation
    // Real implementation would need to load and execute target contract
    state.SetError("CALL not fully implemented");
    return false;
}

arith_uint256 CVM::ReadImmediate(const std::vector<uint8_t>& code, size_t& pc, size_t bytes) {
    arith_uint256 result;
    
    // Read bytes in big-endian order and construct arith_uint256
    for (size_t i = 0; i < bytes && pc + 2 + i < code.size(); i++) {
        result = (result << 8) | arith_uint256(code[pc + 2 + i]);
    }
    
    return result;
}

bool CVM::VerifyBytecode(const std::vector<uint8_t>& code) {
    if (code.empty() || code.size() > MAX_CODE_SIZE) {
        return false;
    }
    
    // Basic validation: check all opcodes are valid
    for (size_t i = 0; i < code.size(); ) {
        uint8_t opcodeByte = code[i];
        
        if (!IsValidOpCode(opcodeByte)) {
            return false;
        }
        
        OpCode opcode = static_cast<OpCode>(opcodeByte);
        
        // Handle PUSH instruction specially (has immediate data)
        if (opcode == OpCode::OP_PUSH) {
            if (i + 1 >= code.size()) {
                return false;
            }
            uint8_t size = code[i + 1];
            if (size == 0 || size > 32) {
                return false;
            }
            if (i + 2 + size > code.size()) {
                return false;
            }
            i += 2 + size;
        } else {
            i++;
        }
    }
    
    return true;
}

bool CVM::DeployContract(const std::vector<uint8_t>& code, const uint160& contractAddr, 
                        ContractStorage* storage) {
    if (!VerifyBytecode(code)) {
        return false;
    }
    
    if (!storage) {
        return false;
    }
    
    // Check if contract already exists
    if (storage->Exists(contractAddr)) {
        return false;
    }
    
    // Store contract would be done through CVMDatabase
    return true;
}

bool CVM::CallContract(const uint160& contractAddr, const std::vector<uint8_t>& inputData,
                      VMState& state, ContractStorage* storage) {
    // Simplified: would need to load contract code and execute
    return false;
}

ExecutionResult ExecuteContract(
    const std::vector<uint8_t>& code,
    uint64_t gasLimit,
    const uint160& contractAddr,
    const uint160& callerAddr,
    uint64_t callValue,
    const std::vector<uint8_t>& inputData,
    int blockHeight,
    const uint256& blockHash,
    int64_t timestamp,
    ContractStorage* storage
) {
    ExecutionResult result;
    
    VMState state;
    state.SetGasLimit(gasLimit);
    state.SetContractAddress(contractAddr);
    state.SetCallerAddress(callerAddr);
    state.SetCallValue(callValue);
    state.SetBlockHeight(blockHeight);
    state.SetBlockHash(blockHash);
    state.SetTimestamp(timestamp);
    
    CVM vm;
    result.success = vm.Execute(code, state, storage);
    result.gasUsed = state.GetGasUsed();
    result.returnData = state.GetReturnData();
    result.logs = state.GetLogs();
    result.error = state.GetError();
    
    return result;
}

} // namespace CVM

