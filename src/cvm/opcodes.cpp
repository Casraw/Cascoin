// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/opcodes.h>

namespace CVM {

uint64_t GetOpCodeGasCost(OpCode opcode) {
    switch (opcode) {
        // Stack operations - very low cost
        case OpCode::OP_PUSH:
        case OpCode::OP_POP:
        case OpCode::OP_DUP:
        case OpCode::OP_SWAP:
            return GasCost::VERYLOW;
        
        // Arithmetic - low cost
        case OpCode::OP_ADD:
        case OpCode::OP_SUB:
        case OpCode::OP_MUL:
        case OpCode::OP_DIV:
        case OpCode::OP_MOD:
            return GasCost::LOW;
        
        // Logical operations - very low cost
        case OpCode::OP_AND:
        case OpCode::OP_OR:
        case OpCode::OP_XOR:
        case OpCode::OP_NOT:
            return GasCost::VERYLOW;
        
        // Comparison - very low cost
        case OpCode::OP_EQ:
        case OpCode::OP_NE:
        case OpCode::OP_LT:
        case OpCode::OP_GT:
        case OpCode::OP_LE:
        case OpCode::OP_GE:
            return GasCost::VERYLOW;
        
        // Control flow
        case OpCode::OP_JUMP:
            return GasCost::JUMP;
        case OpCode::OP_JUMPI:
            return GasCost::JUMPI;
        case OpCode::OP_CALL:
            return GasCost::CALL;
        case OpCode::OP_RETURN:
        case OpCode::OP_STOP:
            return GasCost::BASE;
        
        // Storage - high cost
        case OpCode::OP_SLOAD:
            return GasCost::SLOAD;
        case OpCode::OP_SSTORE:
            return GasCost::SSTORE;
        
        // Cryptographic operations - high cost
        case OpCode::OP_SHA256:
            return GasCost::SHA256;
        case OpCode::OP_VERIFY_SIG:
            return GasCost::VERIFY_SIG;
        case OpCode::OP_PUBKEY:
            return GasCost::VERIFY_SIG;
        
        // Context operations
        case OpCode::OP_ADDRESS:
        case OpCode::OP_CALLER:
        case OpCode::OP_CALLVALUE:
        case OpCode::OP_TIMESTAMP:
        case OpCode::OP_BLOCKHASH:
        case OpCode::OP_BLOCKHEIGHT:
        case OpCode::OP_GAS:
            return GasCost::BASE;
        
        case OpCode::OP_BALANCE:
            return GasCost::BALANCE;
        
        // Special operations
        case OpCode::OP_LOG:
            return GasCost::LOG;
        case OpCode::OP_REVERT:
            return GasCost::BASE;
        
        default:
            return GasCost::BASE;
    }
}

bool IsValidOpCode(uint8_t byte) {
    // Check if the byte corresponds to a valid opcode
    OpCode opcode = static_cast<OpCode>(byte);
    
    switch (opcode) {
        case OpCode::OP_PUSH:
        case OpCode::OP_POP:
        case OpCode::OP_DUP:
        case OpCode::OP_SWAP:
        case OpCode::OP_ADD:
        case OpCode::OP_SUB:
        case OpCode::OP_MUL:
        case OpCode::OP_DIV:
        case OpCode::OP_MOD:
        case OpCode::OP_AND:
        case OpCode::OP_OR:
        case OpCode::OP_XOR:
        case OpCode::OP_NOT:
        case OpCode::OP_EQ:
        case OpCode::OP_NE:
        case OpCode::OP_LT:
        case OpCode::OP_GT:
        case OpCode::OP_LE:
        case OpCode::OP_GE:
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMPI:
        case OpCode::OP_CALL:
        case OpCode::OP_RETURN:
        case OpCode::OP_STOP:
        case OpCode::OP_SLOAD:
        case OpCode::OP_SSTORE:
        case OpCode::OP_SHA256:
        case OpCode::OP_VERIFY_SIG:
        case OpCode::OP_PUBKEY:
        case OpCode::OP_ADDRESS:
        case OpCode::OP_BALANCE:
        case OpCode::OP_CALLER:
        case OpCode::OP_CALLVALUE:
        case OpCode::OP_TIMESTAMP:
        case OpCode::OP_BLOCKHASH:
        case OpCode::OP_BLOCKHEIGHT:
        case OpCode::OP_GAS:
        case OpCode::OP_LOG:
        case OpCode::OP_REVERT:
            return true;
        default:
            return false;
    }
}

const char* GetOpCodeName(OpCode opcode) {
    switch (opcode) {
        case OpCode::OP_PUSH: return "PUSH";
        case OpCode::OP_POP: return "POP";
        case OpCode::OP_DUP: return "DUP";
        case OpCode::OP_SWAP: return "SWAP";
        case OpCode::OP_ADD: return "ADD";
        case OpCode::OP_SUB: return "SUB";
        case OpCode::OP_MUL: return "MUL";
        case OpCode::OP_DIV: return "DIV";
        case OpCode::OP_MOD: return "MOD";
        case OpCode::OP_AND: return "AND";
        case OpCode::OP_OR: return "OR";
        case OpCode::OP_XOR: return "XOR";
        case OpCode::OP_NOT: return "NOT";
        case OpCode::OP_EQ: return "EQ";
        case OpCode::OP_NE: return "NE";
        case OpCode::OP_LT: return "LT";
        case OpCode::OP_GT: return "GT";
        case OpCode::OP_LE: return "LE";
        case OpCode::OP_GE: return "GE";
        case OpCode::OP_JUMP: return "JUMP";
        case OpCode::OP_JUMPI: return "JUMPI";
        case OpCode::OP_CALL: return "CALL";
        case OpCode::OP_RETURN: return "RETURN";
        case OpCode::OP_STOP: return "STOP";
        case OpCode::OP_SLOAD: return "SLOAD";
        case OpCode::OP_SSTORE: return "SSTORE";
        case OpCode::OP_SHA256: return "SHA256";
        case OpCode::OP_VERIFY_SIG: return "VERIFY_SIG";
        case OpCode::OP_PUBKEY: return "PUBKEY";
        case OpCode::OP_ADDRESS: return "ADDRESS";
        case OpCode::OP_BALANCE: return "BALANCE";
        case OpCode::OP_CALLER: return "CALLER";
        case OpCode::OP_CALLVALUE: return "CALLVALUE";
        case OpCode::OP_TIMESTAMP: return "TIMESTAMP";
        case OpCode::OP_BLOCKHASH: return "BLOCKHASH";
        case OpCode::OP_BLOCKHEIGHT: return "BLOCKHEIGHT";
        case OpCode::OP_GAS: return "GAS";
        case OpCode::OP_LOG: return "LOG";
        case OpCode::OP_REVERT: return "REVERT";
        case OpCode::OP_INVALID: return "INVALID";
        default: return "UNKNOWN";
    }
}

} // namespace CVM

