// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ENHANCED_VM_H
#define CASCOIN_CVM_ENHANCED_VM_H

#include <cvm/vmstate.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <cvm/bytecode_detector.h>
#include <cvm/enhanced_storage.h>
#include <uint256.h>
#include <vector>
#include <memory>

#ifdef ENABLE_EVMC
#include <cvm/evmc_host.h>
#include <cvm/evm_engine.h>
#include <evmc/evmc.h>
#endif

namespace CVM {

/**
 * Enhanced VM Execution Result
 * 
 * Unified result structure for both CVM and EVM execution
 */
struct EnhancedExecutionResult {
    bool success;
    uint64_t gas_used;
    std::vector<uint8_t> return_data;
    std::vector<VMState::LogEntry> logs;
    std::string error;
    BytecodeFormat executed_format;
    
    // Trust-specific results
    uint32_t caller_reputation_before;
    uint32_t caller_reputation_after;
    bool trust_gate_passed;
    uint64_t reputation_gas_discount;
    
    // Cross-format execution tracking
    bool cross_format_calls_made;
    size_t total_cross_calls;
    
    EnhancedExecutionResult() 
        : success(false), gas_used(0), executed_format(BytecodeFormat::UNKNOWN),
          caller_reputation_before(0), caller_reputation_after(0), 
          trust_gate_passed(false), reputation_gas_discount(0),
          cross_format_calls_made(false), total_cross_calls(0) {}
};

/**
 * Enhanced Virtual Machine Engine
 * 
 * Main coordinator that routes bytecode execution to the appropriate VM engine
 * (CVM native or EVM via EVMC) based on bytecode format detection. Provides
 * unified interface for both execution engines with comprehensive trust integration.
 */
class EnhancedVM {
public:
    EnhancedVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    ~EnhancedVM();

    // Main execution interface
    EnhancedExecutionResult Execute(
        const std::vector<uint8_t>& bytecode,
        uint64_t gas_limit,
        const uint160& contract_address,
        const uint160& caller_address,
        uint64_t call_value,
        const std::vector<uint8_t>& input_data,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Contract deployment
    EnhancedExecutionResult DeployContract(
        const std::vector<uint8_t>& bytecode,
        const std::vector<uint8_t>& constructor_data,
        uint64_t gas_limit,
        const uint160& deployer_address,
        uint64_t deploy_value,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Cross-format contract calls
    EnhancedExecutionResult CallContract(
        const uint160& contract_address,
        const std::vector<uint8_t>& call_data,
        uint64_t gas_limit,
        const uint160& caller_address,
        uint64_t call_value,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Format detection and routing
    BytecodeDetectionResult DetectBytecodeFormat(const std::vector<uint8_t>& bytecode);
    bool CanExecuteFormat(BytecodeFormat format);
    
    // Trust integration
    void SetTrustContext(std::shared_ptr<TrustContext> ctx) { trust_context = ctx; }
    std::shared_ptr<TrustContext> GetTrustContext() const { return trust_context; }
    
    // Configuration
    void EnableStrictValidation(bool enable) { strict_validation = enable; }
    void SetGasLimit(uint64_t limit) { default_gas_limit = limit; }
    void EnableCrossFormatCalls(bool enable) { cross_format_calls_enabled = enable; }
    
    // Statistics and monitoring
    struct ExecutionStats {
        size_t total_executions;
        size_t cvm_executions;
        size_t evm_executions;
        size_t hybrid_executions;
        size_t failed_executions;
        uint64_t total_gas_used;
        uint64_t total_gas_saved_by_reputation;
        size_t cross_format_calls;
        double average_execution_time_ms;
    };
    
    ExecutionStats GetStats() const { return stats; }
    void ResetStats();
    
    // Debugging and analysis
    std::string GetLastExecutionTrace() const { return last_execution_trace; }
    std::vector<std::string> GetExecutionLogs() const { return execution_logs; }
    void EnableExecutionTracing(bool enable) { execution_tracing = enable; }
    
    // Testing and validation
    bool TestTrustEnhancedSystem();
    bool TestMemoryAndStackFeatures();

private:
    // Core components
    CVMDatabase* database;
    std::shared_ptr<TrustContext> trust_context;
    
    // VM engine instances
    class CVM* cvm_engine;
#ifdef ENABLE_EVMC
    std::unique_ptr<EVMCHost> evmc_host;
    std::unique_ptr<EVMEngine> evm_engine;
#endif
    std::unique_ptr<BytecodeDetector> bytecode_detector;
    std::unique_ptr<BytecodeDetectionCache> detection_cache;
    
    // Execution routing
    EnhancedExecutionResult ExecuteCVMBytecode(
        const std::vector<uint8_t>& bytecode,
        VMState& state,
        ContractStorage* storage
    );
    
#ifdef ENABLE_EVMC
    EnhancedExecutionResult ExecuteEVMBytecode(
        const std::vector<uint8_t>& bytecode,
        uint64_t gas_limit,
        const uint160& contract_address,
        const uint160& caller_address,
        uint64_t call_value,
        const std::vector<uint8_t>& input_data,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
#endif
    
    EnhancedExecutionResult ExecuteHybridContract(
        const std::vector<uint8_t>& bytecode,
        uint64_t gas_limit,
        const uint160& contract_address,
        const uint160& caller_address,
        uint64_t call_value,
        const std::vector<uint8_t>& input_data,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Trust-aware execution
    bool CheckTrustGates(const uint160& caller, const std::string& operation, uint64_t gas_limit);
    uint64_t ApplyReputationGasAdjustments(uint64_t base_gas, const uint160& caller);
    void UpdateReputationFromExecution(const uint160& caller, const EnhancedExecutionResult& result);
    
    // Cross-format call handling
    EnhancedExecutionResult HandleCrossFormatCall(
        BytecodeFormat source_format,
        BytecodeFormat target_format,
        const uint160& target_contract,
        const std::vector<uint8_t>& call_data,
        uint64_t gas_limit,
        const uint160& caller_address,
        uint64_t call_value
    );
    
    // Contract address generation
    uint160 GenerateContractAddress(const uint160& deployer, uint64_t nonce);
    uint160 GenerateCreate2Address(const uint160& deployer, const uint256& salt, const std::vector<uint8_t>& bytecode);
    
    // Validation and security
    bool ValidateContractDeployment(const std::vector<uint8_t>& bytecode, const uint160& deployer);
    bool ValidateContractCall(const uint160& contract, const uint160& caller, uint64_t gas_limit);
    bool CheckResourceLimits(const uint160& caller, uint64_t gas_limit, size_t bytecode_size);
    
    // State management
    void SaveExecutionState();
    void RestoreExecutionState();
    void CommitExecutionState();
    
    // Execution tracing and logging
    void TraceExecution(const std::string& message);
    void LogExecution(const std::string& level, const std::string& message);
    void RecordExecutionMetrics(const EnhancedExecutionResult& result, int64_t execution_time_ms);
    
    // Error handling
    EnhancedExecutionResult CreateErrorResult(const std::string& error, BytecodeFormat format = BytecodeFormat::UNKNOWN);
    void HandleExecutionError(const std::string& error, const uint160& contract, const uint160& caller);
    
    // Configuration
    bool strict_validation;
    uint64_t default_gas_limit;
    bool cross_format_calls_enabled;
    bool execution_tracing;
    
    // Statistics and monitoring
    ExecutionStats stats;
    std::string last_execution_trace;
    std::vector<std::string> execution_logs;
    
    // Execution state stack for nested calls
    struct ExecutionFrame {
        uint160 contract_address;
        uint160 caller_address;
        BytecodeFormat format;
        uint64_t gas_remaining;
        size_t call_depth;
    };
    std::vector<ExecutionFrame> execution_stack;
    
    // Constants
    static constexpr size_t MAX_CALL_DEPTH = 1024;
    static constexpr size_t MAX_BYTECODE_SIZE = 24576; // 24KB
    static constexpr uint64_t MIN_GAS_LIMIT = 21000;
    static constexpr size_t MAX_EXECUTION_LOGS = 1000;
};

/**
 * Enhanced VM Factory
 * 
 * Creates and configures Enhanced VM instances for different use cases
 */
class EnhancedVMFactory {
public:
    // Standard VM configurations
    static std::unique_ptr<EnhancedVM> CreateProductionVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EnhancedVM> CreateTestVM(CVMDatabase* db);
    static std::unique_ptr<EnhancedVM> CreateDebugVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    
    // Specialized configurations
    static std::unique_ptr<EnhancedVM> CreateHighSecurityVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EnhancedVM> CreatePerformanceOptimizedVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EnhancedVM> CreateCrossChainVM(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    
    // Configuration helpers
    static void ConfigureForProduction(EnhancedVM* vm);
    static void ConfigureForTesting(EnhancedVM* vm);
    static void ConfigureForDebugging(EnhancedVM* vm);
};

/**
 * Enhanced VM Utilities
 */
namespace EnhancedVMUtils {
    // Execution result analysis
    bool IsSuccessfulExecution(const EnhancedExecutionResult& result);
    bool HasTrustBenefits(const EnhancedExecutionResult& result);
    double CalculateGasSavingsPercentage(const EnhancedExecutionResult& result, uint64_t base_gas);
    
    // Format compatibility
    bool AreFormatsCompatible(BytecodeFormat source, BytecodeFormat target);
    std::vector<BytecodeFormat> GetSupportedFormats();
    
    // Performance analysis
    struct PerformanceMetrics {
        double executions_per_second;
        double average_gas_per_execution;
        double trust_overhead_percentage;
        double cross_format_call_overhead;
    };
    
    PerformanceMetrics AnalyzePerformance(const EnhancedVM::ExecutionStats& stats, int64_t time_period_ms);
    
    // Debugging utilities
    std::string FormatExecutionResult(const EnhancedExecutionResult& result);
    std::string FormatExecutionStats(const EnhancedVM::ExecutionStats& stats);
    
    // Trust analysis
    struct TrustAnalysis {
        double average_reputation_score;
        double gas_savings_from_trust;
        size_t high_reputation_executions;
        size_t trust_gate_failures;
    };
    
    TrustAnalysis AnalyzeTrustMetrics(const std::vector<EnhancedExecutionResult>& results);
}

} // namespace CVM

#endif // CASCOIN_CVM_ENHANCED_VM_H