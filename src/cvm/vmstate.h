// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_VMSTATE_H
#define CASCOIN_CVM_VMSTATE_H

#include <uint256.h>
#include <arith_uint256.h>
#include <serialize.h>
#include <vector>
#include <stack>
#include <map>
#include <string>
#include <cstdint>

namespace CVM {

/**
 * Maximum values to prevent DoS attacks
 */
static constexpr size_t MAX_STACK_SIZE = 1024;
static constexpr size_t MAX_CODE_SIZE = 24576; // 24KB max contract size
static constexpr uint64_t MAX_GAS_PER_BLOCK = 10000000; // 10M gas per block
static constexpr uint64_t MAX_GAS_PER_TX = 1000000; // 1M gas per transaction

/**
 * VMState - Represents the execution state of the CVM
 * 
 * This is a register-based VM with a value stack for operations.
 * State changes are tracked and can be reverted if execution fails.
 */
class VMState {
public:
    VMState();
    ~VMState();

    // Stack operations
    void Push(const arith_uint256& value);
    arith_uint256 Pop();
    arith_uint256 Peek(size_t depth = 0) const;
    void Swap(size_t depth);
    void Dup(size_t depth);
    size_t StackSize() const { return stack.size(); }
    
    // Program counter
    void SetPC(size_t pc) { programCounter = pc; }
    size_t GetPC() const { return programCounter; }
    void IncrementPC(size_t offset = 1) { programCounter += offset; }
    
    // Gas management
    bool UseGas(uint64_t amount);
    uint64_t GetGasRemaining() const { return gasRemaining; }
    void SetGasLimit(uint64_t gas) { gasRemaining = gas; gasLimit = gas; }
    uint64_t GetGasUsed() const { return gasLimit - gasRemaining; }
    
    // Execution context
    void SetContractAddress(const uint160& addr) { contractAddress = addr; }
    uint160 GetContractAddress() const { return contractAddress; }
    
    void SetCallerAddress(const uint160& addr) { callerAddress = addr; }
    uint160 GetCallerAddress() const { return callerAddress; }
    
    void SetCallValue(uint64_t value) { callValue = value; }
    uint64_t GetCallValue() const { return callValue; }
    
    void SetBlockHeight(int height) { blockHeight = height; }
    int GetBlockHeight() const { return blockHeight; }
    
    void SetBlockHash(const uint256& hash) { blockHash = hash; }
    uint256 GetBlockHash() const { return blockHash; }
    
    void SetTimestamp(int64_t ts) { timestamp = ts; }
    int64_t GetTimestamp() const { return timestamp; }
    
    // Execution state
    enum class Status {
        RUNNING,
        STOPPED,
        RETURNED,
        REVERTED,
        OUT_OF_GAS,
        STACK_OVERFLOW,
        STACK_UNDERFLOW,
        INVALID_OPCODE,
        INVALID_JUMP,
        ERROR
    };
    
    void SetStatus(Status s) { status = s; }
    Status GetStatus() const { return status; }
    bool IsRunning() const { return status == Status::RUNNING; }
    
    // Return data
    void SetReturnData(const std::vector<uint8_t>& data) { returnData = data; }
    std::vector<uint8_t> GetReturnData() const { return returnData; }
    
    // Error message
    void SetError(const std::string& err) { errorMessage = err; }
    std::string GetError() const { return errorMessage; }
    
    // Logs (events)
    struct LogEntry {
        uint160 contractAddress;
        std::vector<uint256> topics;
        std::vector<uint8_t> data;
        
        ADD_SERIALIZE_METHODS;
        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(contractAddress);
            READWRITE(topics);
            READWRITE(data);
        }
    };
    
    void AddLog(const LogEntry& log) { logs.push_back(log); }
    const std::vector<LogEntry>& GetLogs() const { return logs; }
    
    // State snapshot for reverts
    void SaveSnapshot();
    void RevertToSnapshot();
    void CommitSnapshot();
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(contractAddress);
        READWRITE(callerAddress);
        READWRITE(callValue);
        READWRITE(gasRemaining);
        READWRITE(gasLimit);
        READWRITE(blockHeight);
        READWRITE(blockHash);
        READWRITE(timestamp);
        READWRITE(logs);
    }

private:
    // Execution stack
    std::vector<arith_uint256> stack;
    
    // Program counter
    size_t programCounter;
    
    // Gas accounting
    uint64_t gasRemaining;
    uint64_t gasLimit;
    
    // Execution context
    uint160 contractAddress;
    uint160 callerAddress;
    uint64_t callValue;
    int blockHeight;
    uint256 blockHash;
    int64_t timestamp;
    
    // Execution status
    Status status;
    std::vector<uint8_t> returnData;
    std::string errorMessage;
    
    // Event logs
    std::vector<LogEntry> logs;
    
    // Snapshots for revert
    struct Snapshot {
        std::vector<arith_uint256> stack;
        size_t programCounter;
        uint64_t gasRemaining;
    };
    std::vector<Snapshot> snapshots;
};

/**
 * Contract storage interface
 * Maps contract address + storage key to value
 */
class ContractStorage {
public:
    virtual ~ContractStorage() {}
    
    virtual bool Load(const uint160& contractAddr, const uint256& key, uint256& value) = 0;
    virtual bool Store(const uint160& contractAddr, const uint256& key, const uint256& value) = 0;
    virtual bool Exists(const uint160& contractAddr) = 0;
};

} // namespace CVM

#endif // CASCOIN_CVM_VMSTATE_H

