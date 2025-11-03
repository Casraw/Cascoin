// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_OPCODES_H
#define CASCOIN_CVM_OPCODES_H

#include <cstdint>

namespace CVM {

/**
 * CVM Instruction Set
 * 
 * The Cascoin Virtual Machine uses a register-based architecture with
 * a stack for temporary values and persistent storage for contract state.
 */
enum class OpCode : uint8_t {
    // Stack manipulation
    OP_PUSH = 0x01,      // Push value onto stack
    OP_POP = 0x02,       // Pop value from stack
    OP_DUP = 0x03,       // Duplicate top stack value
    OP_SWAP = 0x04,      // Swap two top stack values
    
    // Arithmetic operations
    OP_ADD = 0x10,       // Addition
    OP_SUB = 0x11,       // Subtraction
    OP_MUL = 0x12,       // Multiplication
    OP_DIV = 0x13,       // Division
    OP_MOD = 0x14,       // Modulo
    
    // Logical operations
    OP_AND = 0x20,       // Bitwise AND
    OP_OR = 0x21,        // Bitwise OR
    OP_XOR = 0x22,       // Bitwise XOR
    OP_NOT = 0x23,       // Bitwise NOT
    
    // Comparison operations
    OP_EQ = 0x30,        // Equal
    OP_NE = 0x31,        // Not equal
    OP_LT = 0x32,        // Less than
    OP_GT = 0x33,        // Greater than
    OP_LE = 0x34,        // Less than or equal
    OP_GE = 0x35,        // Greater than or equal
    
    // Control flow
    OP_JUMP = 0x40,      // Unconditional jump
    OP_JUMPI = 0x41,     // Conditional jump
    OP_CALL = 0x42,      // Call another contract
    OP_RETURN = 0x43,    // Return from execution
    OP_STOP = 0x44,      // Halt execution
    
    // Storage operations
    OP_SLOAD = 0x50,     // Load from storage (STORAGE_READ)
    OP_SSTORE = 0x51,    // Store to storage (STORAGE_WRITE)
    
    // Cryptographic operations
    OP_SHA256 = 0x60,    // SHA256 hash
    OP_VERIFY_SIG = 0x61, // Verify signature
    OP_PUBKEY = 0x62,    // Get public key from signature
    
    // Context operations
    OP_ADDRESS = 0x70,   // Get current contract address
    OP_BALANCE = 0x71,   // Get address balance
    OP_CALLER = 0x72,    // Get caller address
    OP_CALLVALUE = 0x73, // Get call value
    OP_TIMESTAMP = 0x74, // Get block timestamp
    OP_BLOCKHASH = 0x75, // Get block hash
    OP_BLOCKHEIGHT = 0x76, // Get block height
    
    // Gas operations
    OP_GAS = 0x80,       // Get remaining gas
    
    // Special operations
    OP_LOG = 0x90,       // Emit event log
    OP_REVERT = 0x91,    // Revert state changes
    
    // Invalid opcode
    OP_INVALID = 0xFF
};

/**
 * Gas costs for each operation
 * These values link to block size limits and prevent DoS
 */
struct GasCost {
    static constexpr uint64_t BASE = 1;
    static constexpr uint64_t VERYLOW = 3;
    static constexpr uint64_t LOW = 5;
    static constexpr uint64_t MID = 8;
    static constexpr uint64_t HIGH = 10;
    static constexpr uint64_t EXTCODE = 700;
    static constexpr uint64_t BALANCE = 400;
    static constexpr uint64_t SLOAD = 200;
    static constexpr uint64_t SSTORE = 5000;
    static constexpr uint64_t JUMP = 8;
    static constexpr uint64_t JUMPI = 10;
    static constexpr uint64_t CALL = 700;
    static constexpr uint64_t SHA256 = 60;
    static constexpr uint64_t VERIFY_SIG = 3000;
    static constexpr uint64_t LOG = 375;
};

/**
 * Get gas cost for a specific opcode
 */
uint64_t GetOpCodeGasCost(OpCode opcode);

/**
 * Check if an opcode is valid
 */
bool IsValidOpCode(uint8_t byte);

/**
 * Get opcode name for debugging
 */
const char* GetOpCodeName(OpCode opcode);

} // namespace CVM

#endif // CASCOIN_CVM_OPCODES_H

