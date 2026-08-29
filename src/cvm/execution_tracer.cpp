// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/execution_tracer.h>
#include <utilstrencodings.h>
#include <util.h>

namespace CVM {

// Opcode names for EVM opcodes
static const std::map<uint8_t, std::string> OPCODE_NAMES = {
    {0x00, "STOP"}, {0x01, "ADD"}, {0x02, "MUL"}, {0x03, "SUB"}, {0x04, "DIV"},
    {0x05, "SDIV"}, {0x06, "MOD"}, {0x07, "SMOD"}, {0x08, "ADDMOD"}, {0x09, "MULMOD"},
    {0x0a, "EXP"}, {0x0b, "SIGNEXTEND"},
    {0x10, "LT"}, {0x11, "GT"}, {0x12, "SLT"}, {0x13, "SGT"}, {0x14, "EQ"},
    {0x15, "ISZERO"}, {0x16, "AND"}, {0x17, "OR"}, {0x18, "XOR"}, {0x19, "NOT"},
    {0x1a, "BYTE"}, {0x1b, "SHL"}, {0x1c, "SHR"}, {0x1d, "SAR"},
    {0x20, "SHA3"},
    {0x30, "ADDRESS"}, {0x31, "BALANCE"}, {0x32, "ORIGIN"}, {0x33, "CALLER"},
    {0x34, "CALLVALUE"}, {0x35, "CALLDATALOAD"}, {0x36, "CALLDATASIZE"},
    {0x37, "CALLDATACOPY"}, {0x38, "CODESIZE"}, {0x39, "CODECOPY"},
    {0x3a, "GASPRICE"}, {0x3b, "EXTCODESIZE"}, {0x3c, "EXTCODECOPY"},
    {0x3d, "RETURNDATASIZE"}, {0x3e, "RETURNDATACOPY"}, {0x3f, "EXTCODEHASH"},
    {0x40, "BLOCKHASH"}, {0x41, "COINBASE"}, {0x42, "TIMESTAMP"},
    {0x43, "NUMBER"}, {0x44, "DIFFICULTY"}, {0x45, "GASLIMIT"}, {0x46, "CHAINID"},
    {0x47, "SELFBALANCE"}, {0x48, "BASEFEE"},
    {0x50, "POP"}, {0x51, "MLOAD"}, {0x52, "MSTORE"}, {0x53, "MSTORE8"},
    {0x54, "SLOAD"}, {0x55, "SSTORE"}, {0x56, "JUMP"}, {0x57, "JUMPI"},
    {0x58, "PC"}, {0x59, "MSIZE"}, {0x5a, "GAS"}, {0x5b, "JUMPDEST"},
    {0x60, "PUSH1"}, {0x61, "PUSH2"}, {0x62, "PUSH3"}, {0x63, "PUSH4"},
    {0x64, "PUSH5"}, {0x65, "PUSH6"}, {0x66, "PUSH7"}, {0x67, "PUSH8"},
    {0x68, "PUSH9"}, {0x69, "PUSH10"}, {0x6a, "PUSH11"}, {0x6b, "PUSH12"},
    {0x6c, "PUSH13"}, {0x6d, "PUSH14"}, {0x6e, "PUSH15"}, {0x6f, "PUSH16"},
    {0x70, "PUSH17"}, {0x71, "PUSH18"}, {0x72, "PUSH19"}, {0x73, "PUSH20"},
    {0x74, "PUSH21"}, {0x75, "PUSH22"}, {0x76, "PUSH23"}, {0x77, "PUSH24"},
    {0x78, "PUSH25"}, {0x79, "PUSH26"}, {0x7a, "PUSH27"}, {0x7b, "PUSH28"},
    {0x7c, "PUSH29"}, {0x7d, "PUSH30"}, {0x7e, "PUSH31"}, {0x7f, "PUSH32"},
    {0x80, "DUP1"}, {0x81, "DUP2"}, {0x82, "DUP3"}, {0x83, "DUP4"},
    {0x84, "DUP5"}, {0x85, "DUP6"}, {0x86, "DUP7"}, {0x87, "DUP8"},
    {0x88, "DUP9"}, {0x89, "DUP10"}, {0x8a, "DUP11"}, {0x8b, "DUP12"},
    {0x8c, "DUP13"}, {0x8d, "DUP14"}, {0x8e, "DUP15"}, {0x8f, "DUP16"},
    {0x90, "SWAP1"}, {0x91, "SWAP2"}, {0x92, "SWAP3"}, {0x93, "SWAP4"},
    {0x94, "SWAP5"}, {0x95, "SWAP6"}, {0x96, "SWAP7"}, {0x97, "SWAP8"},
    {0x98, "SWAP9"}, {0x99, "SWAP10"}, {0x9a, "SWAP11"}, {0x9b, "SWAP12"},
    {0x9c, "SWAP13"}, {0x9d, "SWAP14"}, {0x9e, "SWAP15"}, {0x9f, "SWAP16"},
    {0xa0, "LOG0"}, {0xa1, "LOG1"}, {0xa2, "LOG2"}, {0xa3, "LOG3"}, {0xa4, "LOG4"},
    {0xf0, "CREATE"}, {0xf1, "CALL"}, {0xf2, "CALLCODE"}, {0xf3, "RETURN"},
    {0xf4, "DELEGATECALL"}, {0xf5, "CREATE2"}, {0xfa, "STATICCALL"},
    {0xfd, "REVERT"}, {0xfe, "INVALID"}, {0xff, "SELFDESTRUCT"}
};

std::string GetOpcodeName(uint8_t op) {
    auto it = OPCODE_NAMES.find(op);
    if (it != OPCODE_NAMES.end()) {
        return it->second;
    }
    return "UNKNOWN_" + std::to_string(op);
}

// OpcodeStep implementation

UniValue OpcodeStep::ToJSON() const {
    UniValue result(UniValue::VOBJ);
    result.pushKV("pc", (int64_t)pc);
    result.pushKV("op", (int)op);
    result.pushKV("opName", opName);
    result.pushKV("gas", (int64_t)gas);
    result.pushKV("gasCost", (int64_t)gasCost);
    result.pushKV("depth", (int64_t)depth);
    
    // Stack (top 10 items for brevity)
    UniValue stackArr(UniValue::VARR);
    size_t stackSize = std::min(stack.size(), size_t(10));
    for (size_t i = 0; i < stackSize; i++) {
        stackArr.push_back("0x" + stack[i].GetHex());
    }
    result.pushKV("stack", stackArr);
    
    // Memory (if present and not too large)
    if (!memory.empty() && memory.size() <= 1024) {
        result.pushKV("memory", "0x" + HexStr(memory));
    }
    
    // Storage changes
    if (!storage.empty()) {
        UniValue storageObj(UniValue::VOBJ);
        for (const auto& pair : storage) {
            storageObj.pushKV("0x" + pair.first.GetHex(), "0x" + pair.second.GetHex());
        }
        result.pushKV("storage", storageObj);
    }
    
    if (!error.empty()) {
        result.pushKV("error", error);
    }
    
    return result;
}

// CallFrame implementation

UniValue CallFrame::ToJSON(bool includeSteps) const {
    UniValue result(UniValue::VOBJ);
    result.pushKV("type", type);
    result.pushKV("from", "0x" + HexStr(from.begin(), from.end()));
    result.pushKV("to", "0x" + HexStr(to.begin(), to.end()));
    result.pushKV("value", "0x" + value.GetHex());
    result.pushKV("gas", (int64_t)gas);
    result.pushKV("gasUsed", (int64_t)gasUsed);
    result.pushKV("input", "0x" + HexStr(input));
    result.pushKV("output", "0x" + HexStr(output));
    
    if (!error.empty()) {
        result.pushKV("error", error);
    }
    
    // Include opcode steps if requested
    if (includeSteps && !steps.empty()) {
        UniValue stepsArr(UniValue::VARR);
        for (const auto& step : steps) {
            stepsArr.push_back(step.ToJSON());
        }
        result.pushKV("structLogs", stepsArr);
    }
    
    // Include nested calls
    if (!calls.empty()) {
        UniValue callsArr(UniValue::VARR);
        for (const auto& call : calls) {
            callsArr.push_back(call.ToJSON(includeSteps));
        }
        result.pushKV("calls", callsArr);
    }
    
    return result;
}

// ExecutionTrace implementation

UniValue ExecutionTrace::ToJSON(const std::string& tracerType) const {
    UniValue result(UniValue::VOBJ);
    
    if (tracerType == "callTracer") {
        // Call tracer format (nested calls only, no opcode steps)
        result = rootCall.ToJSON(false);
    } else if (tracerType == "prestateTracer") {
        // Prestate tracer format (storage before execution)
        result.pushKV("type", "prestateTracer");
        result.pushKV("note", "Prestate tracing not fully implemented");
    } else {
        // Default tracer format (full opcode trace)
        result.pushKV("gas", (int64_t)totalGas);
        result.pushKV("failed", failed);
        result.pushKV("returnValue", returnValue);
        
        // Struct logs (opcode execution steps)
        UniValue structLogs(UniValue::VARR);
        for (const auto& step : rootCall.steps) {
            structLogs.push_back(step.ToJSON());
        }
        result.pushKV("structLogs", structLogs);
        
        // Trust-specific data
        if (callerReputation > 0) {
            UniValue trustData(UniValue::VOBJ);
            trustData.pushKV("callerReputation", (int)callerReputation);
            trustData.pushKV("reputationGasDiscount", (int64_t)reputationGasDiscount);
            trustData.pushKV("trustGatePassed", trustGatePassed);
            
            if (!trustEvents.empty()) {
                UniValue eventsArr(UniValue::VARR);
                for (const auto& event : trustEvents) {
                    eventsArr.push_back(event);
                }
                trustData.pushKV("trustEvents", eventsArr);
            }
            
            result.pushKV("trustData", trustData);
        }
    }
    
    return result;
}

// ExecutionTracer implementation

ExecutionTracer::ExecutionTracer()
    : m_tracing(false)
    , m_traceMemory(false)
    , m_traceStorage(true)
    , m_maxDepth(1024)
{
}

ExecutionTracer::~ExecutionTracer() {
}

void ExecutionTracer::StartTrace(const uint256& txHash) {
    m_tracing = true;
    m_trace = ExecutionTrace();
    m_trace.txHash = txHash;
    m_callStack.clear();
    m_callStack.push_back(&m_trace.rootCall);
}

ExecutionTrace ExecutionTracer::StopTrace() {
    m_tracing = false;
    m_callStack.clear();
    return m_trace;
}

void ExecutionTracer::RecordOpcode(
    uint64_t pc,
    uint8_t op,
    const std::string& opName,
    uint64_t gas,
    uint64_t gasCost,
    const std::vector<uint256>& stack,
    const std::vector<uint8_t>& memory,
    const std::map<uint256, uint256>& storage)
{
    if (!m_tracing || m_callStack.empty()) {
        return;
    }
    
    CallFrame* frame = GetCurrentFrame();
    if (!frame) {
        return;
    }
    
    OpcodeStep step;
    step.pc = pc;
    step.op = op;
    step.opName = opName.empty() ? GetOpcodeName(op) : opName;
    step.gas = gas;
    step.gasCost = gasCost;
    step.depth = m_callStack.size();
    step.stack = stack;
    
    if (m_traceMemory) {
        step.memory = memory;
    }
    
    if (m_traceStorage) {
        step.storage = storage;
    }
    
    frame->steps.push_back(step);
}

void ExecutionTracer::RecordCallStart(
    const std::string& type,
    const uint160& from,
    const uint160& to,
    const uint256& value,
    uint64_t gas,
    const std::vector<uint8_t>& input)
{
    if (!m_tracing || m_callStack.size() >= m_maxDepth) {
        return;
    }
    
    CallFrame* parent = GetCurrentFrame();
    if (!parent) {
        return;
    }
    
    CallFrame newCall;
    newCall.type = type;
    newCall.from = from;
    newCall.to = to;
    newCall.value = value;
    newCall.gas = gas;
    newCall.input = input;
    
    parent->calls.push_back(newCall);
    m_callStack.push_back(&parent->calls.back());
}

void ExecutionTracer::RecordCallEnd(
    uint64_t gasUsed,
    const std::vector<uint8_t>& output,
    const std::string& error)
{
    if (!m_tracing || m_callStack.empty()) {
        return;
    }
    
    CallFrame* frame = GetCurrentFrame();
    if (frame) {
        frame->gasUsed = gasUsed;
        frame->output = output;
        frame->error = error;
        
        // Update total gas
        m_trace.totalGas += gasUsed;
        
        // Update failed status
        if (!error.empty()) {
            m_trace.failed = true;
        }
    }
    
    // Pop call stack
    if (m_callStack.size() > 1) {
        m_callStack.pop_back();
    }
}

void ExecutionTracer::RecordStorageChange(const uint256& key, const uint256& value) {
    if (!m_tracing || !m_traceStorage || m_callStack.empty()) {
        return;
    }
    
    CallFrame* frame = GetCurrentFrame();
    if (frame && !frame->steps.empty()) {
        frame->steps.back().storage[key] = value;
    }
}

void ExecutionTracer::RecordTrustEvent(const std::string& event) {
    if (!m_tracing) {
        return;
    }
    
    m_trace.trustEvents.push_back(event);
}

void ExecutionTracer::SetCallerReputation(uint32_t reputation) {
    if (!m_tracing) {
        return;
    }
    
    m_trace.callerReputation = reputation;
}

void ExecutionTracer::SetReputationGasDiscount(uint64_t discount) {
    if (!m_tracing) {
        return;
    }
    
    m_trace.reputationGasDiscount = discount;
}

void ExecutionTracer::SetTrustGatePassed(bool passed) {
    if (!m_tracing) {
        return;
    }
    
    m_trace.trustGatePassed = passed;
}

CallFrame* ExecutionTracer::GetCurrentFrame() {
    if (m_callStack.empty()) {
        return nullptr;
    }
    return m_callStack.back();
}

// TracerFactory implementation

std::unique_ptr<ExecutionTracer> TracerFactory::CreateTracer(const std::string& tracerType) {
    auto tracer = std::make_unique<ExecutionTracer>();
    
    if (tracerType == "callTracer") {
        // Call tracer doesn't need opcode-level tracing
        tracer->SetTraceMemory(false);
        tracer->SetTraceStorage(false);
    } else if (tracerType == "prestateTracer") {
        // Prestate tracer needs storage
        tracer->SetTraceMemory(false);
        tracer->SetTraceStorage(true);
    } else {
        // Default tracer includes everything
        tracer->SetTraceMemory(true);
        tracer->SetTraceStorage(true);
    }
    
    return tracer;
}

void TracerFactory::ParseTracerOptions(ExecutionTracer* tracer, const UniValue& options) {
    if (!options.isObject()) {
        return;
    }
    
    if (options.exists("disableMemory") && options["disableMemory"].isBool()) {
        tracer->SetTraceMemory(!options["disableMemory"].get_bool());
    }
    
    if (options.exists("disableStorage") && options["disableStorage"].isBool()) {
        tracer->SetTraceStorage(!options["disableStorage"].get_bool());
    }
    
    if (options.exists("maxDepth") && options["maxDepth"].isNum()) {
        tracer->SetMaxDepth(options["maxDepth"].get_int64());
    }
}

} // namespace CVM
