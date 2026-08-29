// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_EXECUTION_TRACER_H
#define CASCOIN_CVM_EXECUTION_TRACER_H

#include <uint256.h>
#include <univalue.h>
#include <vector>
#include <string>
#include <map>
#include <memory>

namespace CVM {

/**
 * Opcode Execution Step
 * 
 * Captures detailed information about a single opcode execution
 */
struct OpcodeStep {
    uint64_t pc;                           // Program counter
    uint8_t op;                            // Opcode value
    std::string opName;                    // Opcode name (e.g., "PUSH1", "ADD")
    uint64_t gas;                          // Gas remaining before execution
    uint64_t gasCost;                      // Gas cost of this operation
    uint64_t depth;                        // Call depth
    std::vector<uint256> stack;            // Stack state after execution
    std::vector<uint8_t> memory;           // Memory state (if changed)
    std::map<uint256, uint256> storage;    // Storage changes
    std::string error;                     // Error message if failed
    
    OpcodeStep() : pc(0), op(0), gas(0), gasCost(0), depth(0) {}
    
    // Convert to JSON for RPC response
    UniValue ToJSON() const;
};

/**
 * Call Frame
 * 
 * Represents a contract call in the execution trace
 */
struct CallFrame {
    std::string type;                      // "CALL", "DELEGATECALL", "STATICCALL", "CREATE"
    uint160 from;                          // Caller address
    uint160 to;                            // Callee address
    uint256 value;                         // Value transferred
    uint64_t gas;                          // Gas provided
    uint64_t gasUsed;                      // Gas actually used
    std::vector<uint8_t> input;            // Call data
    std::vector<uint8_t> output;           // Return data
    std::string error;                     // Error message if failed
    std::vector<OpcodeStep> steps;         // Opcode execution steps
    std::vector<CallFrame> calls;          // Nested calls
    
    CallFrame() : gas(0), gasUsed(0) {}
    
    // Convert to JSON for RPC response
    UniValue ToJSON(bool includeSteps = true) const;
};

/**
 * Execution Trace
 * 
 * Complete execution trace for a transaction or call
 */
struct ExecutionTrace {
    uint256 txHash;                        // Transaction hash (if applicable)
    CallFrame rootCall;                    // Root call frame
    uint64_t totalGas;                     // Total gas used
    bool failed;                           // Execution failed
    std::string returnValue;               // Final return value (hex)
    
    // Trust-specific trace data
    uint32_t callerReputation;             // Caller reputation
    uint64_t reputationGasDiscount;        // Gas discount from reputation
    bool trustGatePassed;                  // Trust gate check result
    std::vector<std::string> trustEvents;  // Trust-related events
    
    ExecutionTrace() : totalGas(0), failed(false), callerReputation(0), 
                       reputationGasDiscount(0), trustGatePassed(true) {}
    
    // Convert to JSON for RPC response
    UniValue ToJSON(const std::string& tracerType = "default") const;
};

/**
 * Execution Tracer
 * 
 * Captures detailed execution traces for debugging and analysis
 */
class ExecutionTracer {
public:
    ExecutionTracer();
    ~ExecutionTracer();
    
    /**
     * Start tracing a new execution
     */
    void StartTrace(const uint256& txHash = uint256());
    
    /**
     * Stop tracing and return the trace
     */
    ExecutionTrace StopTrace();
    
    /**
     * Check if currently tracing
     */
    bool IsTracing() const { return m_tracing; }
    
    /**
     * Record opcode execution
     */
    void RecordOpcode(
        uint64_t pc,
        uint8_t op,
        const std::string& opName,
        uint64_t gas,
        uint64_t gasCost,
        const std::vector<uint256>& stack,
        const std::vector<uint8_t>& memory,
        const std::map<uint256, uint256>& storage
    );
    
    /**
     * Record call start
     */
    void RecordCallStart(
        const std::string& type,
        const uint160& from,
        const uint160& to,
        const uint256& value,
        uint64_t gas,
        const std::vector<uint8_t>& input
    );
    
    /**
     * Record call end
     */
    void RecordCallEnd(
        uint64_t gasUsed,
        const std::vector<uint8_t>& output,
        const std::string& error = ""
    );
    
    /**
     * Record storage change
     */
    void RecordStorageChange(const uint256& key, const uint256& value);
    
    /**
     * Record trust event
     */
    void RecordTrustEvent(const std::string& event);
    
    /**
     * Set caller reputation
     */
    void SetCallerReputation(uint32_t reputation);
    
    /**
     * Set reputation gas discount
     */
    void SetReputationGasDiscount(uint64_t discount);
    
    /**
     * Set trust gate result
     */
    void SetTrustGatePassed(bool passed);
    
    /**
     * Get current call depth
     */
    uint64_t GetCallDepth() const { return m_callStack.size(); }
    
    /**
     * Enable/disable memory tracing (can be expensive)
     */
    void SetTraceMemory(bool enable) { m_traceMemory = enable; }
    
    /**
     * Enable/disable storage tracing
     */
    void SetTraceStorage(bool enable) { m_traceStorage = enable; }
    
    /**
     * Set maximum trace depth
     */
    void SetMaxDepth(uint64_t depth) { m_maxDepth = depth; }

private:
    bool m_tracing;
    bool m_traceMemory;
    bool m_traceStorage;
    uint64_t m_maxDepth;
    
    ExecutionTrace m_trace;
    std::vector<CallFrame*> m_callStack;
    
    // Get current call frame
    CallFrame* GetCurrentFrame();
};

/**
 * Tracer Factory
 * 
 * Creates tracers based on tracer type
 */
class TracerFactory {
public:
    /**
     * Create tracer for transaction
     */
    static std::unique_ptr<ExecutionTracer> CreateTracer(
        const std::string& tracerType = "default"
    );
    
    /**
     * Parse tracer options
     */
    static void ParseTracerOptions(
        ExecutionTracer* tracer,
        const UniValue& options
    );
};

} // namespace CVM

#endif // CASCOIN_CVM_EXECUTION_TRACER_H
