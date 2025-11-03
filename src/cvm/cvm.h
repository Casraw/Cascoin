// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CVM_H
#define CASCOIN_CVM_CVM_H

#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <uint256.h>
#include <vector>
#include <memory>

namespace CVM {

/**
 * Cascoin Virtual Machine (CVM)
 * 
 * A register-based virtual machine for executing smart contracts on Cascoin.
 * Supports bytecode execution, gas metering, storage operations, and
 * cryptographic operations.
 */
class CVM {
public:
    CVM();
    ~CVM();
    
    /**
     * Execute contract bytecode
     * 
     * @param code The bytecode to execute
     * @param state The VM state (including gas limit, context)
     * @param storage Contract storage interface
     * @return true if execution succeeded, false otherwise
     */
    bool Execute(const std::vector<uint8_t>& code, VMState& state, ContractStorage* storage);
    
    /**
     * Deploy a new contract
     * 
     * @param code The contract bytecode
     * @param contractAddr The address where contract will be deployed
     * @param storage Contract storage interface
     * @return true if deployment succeeded
     */
    bool DeployContract(const std::vector<uint8_t>& code, const uint160& contractAddr, ContractStorage* storage);
    
    /**
     * Call an existing contract
     * 
     * @param contractAddr The contract address to call
     * @param inputData Call data
     * @param state VM state with caller context and gas
     * @param storage Contract storage interface
     * @return true if call succeeded
     */
    bool CallContract(const uint160& contractAddr, const std::vector<uint8_t>& inputData, 
                     VMState& state, ContractStorage* storage);
    
    /**
     * Verify contract bytecode is valid
     * 
     * @param code The bytecode to verify
     * @return true if bytecode is valid
     */
    static bool VerifyBytecode(const std::vector<uint8_t>& code);
    
private:
    /**
     * Execute a single instruction
     * 
     * @param opcode The opcode to execute
     * @param code The full bytecode (for reading immediate values)
     * @param state The VM state
     * @param storage Contract storage interface
     * @return true if instruction executed successfully
     */
    bool ExecuteInstruction(OpCode opcode, const std::vector<uint8_t>& code, 
                           VMState& state, ContractStorage* storage);
    
    // Opcode handlers
    bool HandlePush(const std::vector<uint8_t>& code, VMState& state);
    bool HandleArithmetic(OpCode opcode, VMState& state);
    bool HandleLogical(OpCode opcode, VMState& state);
    bool HandleComparison(OpCode opcode, VMState& state);
    bool HandleJump(OpCode opcode, const std::vector<uint8_t>& code, VMState& state);
    bool HandleStorage(OpCode opcode, VMState& state, ContractStorage* storage);
    bool HandleCrypto(OpCode opcode, VMState& state);
    bool HandleContext(OpCode opcode, VMState& state, ContractStorage* storage);
    bool HandleCall(const std::vector<uint8_t>& code, VMState& state, ContractStorage* storage);
    
    // Helper functions
    arith_uint256 ReadImmediate(const std::vector<uint8_t>& code, size_t& pc, size_t bytes);
    bool VerifySignature(const std::vector<uint8_t>& message, const std::vector<uint8_t>& signature, 
                        const std::vector<uint8_t>& pubkey);
};

/**
 * Contract execution result
 */
struct ExecutionResult {
    bool success;
    uint64_t gasUsed;
    std::vector<uint8_t> returnData;
    std::vector<VMState::LogEntry> logs;
    std::string error;
    
    ExecutionResult() : success(false), gasUsed(0) {}
};

/**
 * Execute contract with full result information
 * 
 * @param code Contract bytecode
 * @param gasLimit Maximum gas to use
 * @param contractAddr Contract address
 * @param callerAddr Caller address
 * @param callValue Value sent with call
 * @param inputData Call input data
 * @param blockHeight Current block height
 * @param blockHash Current block hash
 * @param timestamp Block timestamp
 * @param storage Contract storage interface
 * @return Execution result
 */
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
);

} // namespace CVM

#endif // CASCOIN_CVM_CVM_H

