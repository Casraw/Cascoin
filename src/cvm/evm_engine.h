// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_EVM_ENGINE_H
#define CASCOIN_CVM_EVM_ENGINE_H

#ifdef ENABLE_EVMC

#include <cvm/vmstate.h>
#include <cvm/cvmdb.h>
#include <cvm/trust_context.h>
#include <cvm/evmc_host.h>
#include <cvm/sustainable_gas.h>
#include <uint256.h>
#include <evmc/evmc.h>
#include <evmc/utils.h>
#include <vector>
#include <memory>
#include <map>
#include <string>

namespace CVM {

// EVM Constants
static constexpr size_t MAX_BYTECODE_SIZE = 24576; // 24KB max contract size
static constexpr size_t MAX_MEMORY_SIZE = 1024 * 1024; // 1MB
// MAX_STACK_SIZE is defined in vmstate.h

/**
 * EVM Engine Execution Result
 * 
 * Detailed result structure for EVM execution with trust integration
 */
struct EVMExecutionResult {
    bool success;
    evmc_status_code status_code;
    uint64_t gas_used;
    uint64_t gas_left;
    std::vector<uint8_t> output_data;
    std::vector<VMState::LogEntry> logs;
    std::string error_message;
    
    // Trust-specific metrics
    uint32_t caller_reputation;
    uint64_t original_gas_cost;
    uint64_t reputation_adjusted_gas_cost;
    uint64_t gas_saved_by_reputation;
    bool trust_gate_passed;
    std::vector<std::string> trust_operations_performed;
    
    // EVM-specific metrics
    size_t memory_size_peak;
    size_t stack_size_peak;
    uint32_t opcodes_executed;
    std::map<uint8_t, uint32_t> opcode_frequency;
    
    EVMExecutionResult() 
        : success(false), status_code(EVMC_FAILURE), gas_used(0), gas_left(0),
          caller_reputation(0), original_gas_cost(0), reputation_adjusted_gas_cost(0),
          gas_saved_by_reputation(0), trust_gate_passed(false),
          memory_size_peak(0), stack_size_peak(0), opcodes_executed(0) {}
};

/**
 * EVM Engine with Trust Integration
 * 
 * High-performance EVM execution engine using evmone backend with comprehensive
 * trust-aware features. Implements all 140+ EVM opcodes with identical semantics
 * to Ethereum mainnet while adding reputation-based enhancements.
 */
class EVMEngine {
public:
    EVMEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    ~EVMEngine();

    // Main execution interface
    EVMExecutionResult Execute(
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
    EVMExecutionResult DeployContract(
        const std::vector<uint8_t>& bytecode,
        const std::vector<uint8_t>& constructor_data,
        uint64_t gas_limit,
        const uint160& deployer_address,
        uint64_t deploy_value,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Static call (read-only)
    EVMExecutionResult StaticCall(
        const uint160& contract_address,
        const std::vector<uint8_t>& call_data,
        uint64_t gas_limit,
        const uint160& caller_address,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Delegate call
    EVMExecutionResult DelegateCall(
        const uint160& contract_address,
        const std::vector<uint8_t>& call_data,
        uint64_t gas_limit,
        const uint160& caller_address,
        const uint160& original_caller,
        uint64_t call_value,
        int block_height,
        const uint256& block_hash,
        int64_t timestamp
    );
    
    // Trust integration methods
    void SetTrustContext(std::shared_ptr<TrustContext> ctx);
    std::shared_ptr<TrustContext> GetTrustContext() const { return trust_context; }
    
    // Trust-enhanced operations
    bool CheckTrustGate(const uint160& caller, const std::string& operation, uint64_t min_reputation = 0);
    uint64_t ApplyReputationGasDiscount(uint64_t base_gas, const uint160& caller);
    bool IsHighReputationCaller(const uint160& caller, uint32_t threshold = 80);
    
    // EVM compatibility verification
    bool VerifyEthereumCompatibility(const std::vector<uint8_t>& bytecode);
    std::vector<std::string> GetCompatibilityIssues(const std::vector<uint8_t>& bytecode);
    
    // Gas cost calculations (Ethereum-compatible)
    uint64_t CalculateIntrinsicGas(const std::vector<uint8_t>& data, bool is_creation);
    uint64_t CalculateMemoryGas(size_t memory_size);
    uint64_t CalculateStorageGas(bool is_sstore, bool is_new_value, bool is_original_value);
    
    // Configuration
    void SetRevision(evmc_revision rev) { evm_revision = rev; }
    evmc_revision GetRevision() const { return evm_revision; }
    void EnableTrustFeatures(bool enable) { trust_features_enabled = enable; }
    void SetStrictGasAccounting(bool strict) { strict_gas_accounting = strict; }
    
    // Performance and monitoring
    struct EngineStats {
        size_t total_executions;
        size_t successful_executions;
        size_t failed_executions;
        uint64_t total_gas_used;
        uint64_t total_gas_saved_by_reputation;
        size_t high_reputation_executions;
        size_t trust_gate_failures;
        double average_execution_time_ms;
        std::map<evmc_status_code, size_t> status_code_frequency;
    };
    
    EngineStats GetStats() const { return stats; }
    void ResetStats();
    
    // Debugging and analysis
    void EnableExecutionTracing(bool enable) { execution_tracing = enable; }
    std::string GetLastExecutionTrace() const { return last_execution_trace; }
    void EnableOpcodeFrequencyTracking(bool enable) { opcode_frequency_tracking = enable; }
    
    // Testing and validation
    bool TestTrustEnhancedOperations();
    bool TestTrustAwareMemoryAndStack();
    bool TestReputationEnhancedControlFlow();
    bool TestTrustIntegratedCryptography();

private:
    // EVMC VM instance management
    evmc_vm* CreateEVMInstance();
    void DestroyEVMInstance();
    bool InitializeEVM();
    
    // Execution helpers
    evmc_message CreateEVMCMessage(
        evmc_call_kind kind,
        const uint160& sender,
        const uint160& recipient,
        uint64_t value,
        const std::vector<uint8_t>& input_data,
        uint64_t gas_limit,
        int32_t depth = 0
    );
    
    EVMExecutionResult ProcessEVMCResult(
        const evmc_result& evmc_result,
        const evmc_message& msg,
        uint64_t original_gas_limit,
        const uint160& caller
    );
    
    // Trust-enhanced opcode handling
    bool HandleTrustWeightedArithmetic(uint8_t opcode, evmc_message& msg, const uint256& operand1, const uint256& operand2, uint256& result);
    bool HandleReputationGatedCall(evmc_message& msg);
    bool HandleTrustAwareMemory(uint8_t opcode, evmc_message& msg);
    bool HandleReputationBasedControlFlow(uint8_t opcode, evmc_message& msg);
    bool HandleTrustEnhancedCrypto(uint8_t opcode, evmc_message& msg);
    
    // Trust-weighted arithmetic operations
    uint256 PerformTrustWeightedAddition(const uint256& a, const uint256& b, uint32_t reputation_weight);
    uint256 PerformTrustWeightedMultiplication(const uint256& a, const uint256& b, uint32_t reputation_weight);
    uint256 PerformTrustWeightedDivision(const uint256& a, const uint256& b, uint32_t reputation_weight);
    bool ValidateArithmeticTrustGate(uint32_t caller_reputation, const uint256& operand_size);
    
    // Automatic trust context injection
    void InjectTrustContext(evmc_message& msg);
    void InjectCallerReputation(evmc_message& msg, const uint160& caller);
    void InjectReputationHistory(evmc_message& msg, const uint160& caller);
    
    // Reputation-based gas adjustments
    uint64_t CalculateReputationDiscount(uint32_t reputation_score);
    uint64_t ApplyAntiCongestionPricing(uint64_t base_gas, uint32_t network_load);
    bool IsEligibleForFreeGas(const uint160& caller);
    uint64_t GetFreeGasAllowance(const uint160& caller);
    void ApplyTrustAwareGasCostModifications(evmc_message& msg, const uint160& caller);
    
    // Trust-aware resource management
    bool CheckReputationBasedLimits(const uint160& caller, uint64_t gas_limit, size_t memory_size);
    uint32_t GetExecutionPriority(const uint160& caller);
    bool ShouldPrioritizeExecution(const uint160& caller, uint32_t network_congestion);
    
    // Memory and stack trust operations
    bool CreateTrustTaggedMemoryRegion(size_t offset, size_t size, uint32_t min_reputation);
    bool CreateProtectedMemoryRegion(size_t offset, size_t size, const uint160& creator, const std::string& region_id);
    bool AccessTrustTaggedMemory(size_t offset, const uint160& caller);
    bool ValidateTrustTaggedMemoryAccess(const uint160& caller, uint32_t reputation, bool is_read);
    bool ValidateMemoryRegionAccess(size_t offset, size_t size, const uint160& caller, bool is_write);
    
    // Reputation-weighted stack operations
    bool PushReputationWeightedValue(const uint256& value, uint32_t reputation_weight);
    bool PopReputationWeighted(uint256& value, uint32_t min_reputation);
    bool ValidateStackAccess(const uint160& caller, uint32_t required_reputation);
    
    // Trust-aware data structures
    bool CreateReputationSortedArray(const std::string& array_id, uint32_t min_access_reputation);
    bool AddToReputationArray(const std::string& array_id, const uint256& value, uint32_t reputation_weight);
    bool GetFromReputationArray(const std::string& array_id, size_t index, uint256& value, const uint160& caller);
    bool SortReputationArray(const std::string& array_id);
    
    // Automatic reputation validation
    bool ValidateDataIntegrity(const std::vector<uint8_t>& data, const uint160& caller);
    bool CheckReputationBasedDataAccess(const uint160& caller, const std::string& operation);
    
    // Control flow trust enhancements
    bool ValidateReputationBasedJump(size_t destination, const uint160& caller);
    bool CheckTrustBasedLoopLimit(uint32_t iterations, const uint160& caller);
    bool RouteBasedOnTrustLevel(const uint160& caller, const std::vector<size_t>& destinations);
    
    // Cryptographic trust integration
    bool VerifyReputationSignature(const std::vector<uint8_t>& signature, const uint160& signer);
    uint256 ComputeTrustEnhancedHash(const std::vector<uint8_t>& data, const uint160& caller);
    bool GenerateReputationBasedKey(const uint160& address, std::vector<uint8_t>& key);
    bool GenerateTrustAwareRandomNumber(const uint160& caller, uint256& random_number);
    
    // Cross-chain trust operations
    bool ValidateCrossChainTrustAttestation(const std::vector<uint8_t>& attestation);
    uint32_t AggregateCrossChainReputation(const uint160& address);
    bool SynchronizeCrossChainTrustState(const uint160& address);
    
    // Error handling and validation
    EVMExecutionResult CreateErrorResult(evmc_status_code status, const std::string& error);
    EVMExecutionResult HandleTrustAwareException(evmc_status_code status, const std::string& error,
                                                 const uint160& caller, uint64_t gas_used, uint64_t gas_left);
    bool ValidateExecutionParameters(const evmc_message& msg, const std::vector<uint8_t>& bytecode);
    void HandleTrustRelatedError(const std::string& error, const uint160& caller);
    
    // Performance optimization
    void OptimizeTrustCalculations(const std::vector<uint160>& frequent_addresses);
    bool ShouldCacheTrustScore(const uint160& address);
    void UpdateTrustScoreCache(const uint160& address, uint32_t score, int64_t timestamp);
    
    // Execution tracing and monitoring
    void TraceExecution(const std::string& message);
    void RecordOpcodeExecution(uint8_t opcode);
    void UpdateExecutionMetrics(const EVMExecutionResult& result, int64_t execution_time_ms);
    
    // State management
    CVMDatabase* database;
    std::shared_ptr<TrustContext> trust_context;
    std::unique_ptr<EVMCHost> evmc_host;
    std::unique_ptr<cvm::SustainableGasSystem> gas_system;
    
    // EVM instance
    evmc_vm* evm_instance;
    evmc_revision evm_revision;
    
    // Configuration
    bool trust_features_enabled;
    bool strict_gas_accounting;
    bool execution_tracing;
    bool opcode_frequency_tracking;
    
    // Trust-aware memory regions
    struct TrustTaggedMemoryRegion {
        size_t offset;
        size_t size;
        uint32_t min_reputation;
        int64_t created_at;
        uint160 creator_address;
        std::string region_id;
        bool is_protected;
        std::map<size_t, uint32_t> access_permissions; // offset -> min_reputation
    };
    std::vector<TrustTaggedMemoryRegion> trust_tagged_regions;
    
    // Trust-aware data structures
    struct ReputationSortedArray {
        std::vector<std::pair<uint256, uint32_t>> data; // value, reputation_weight
        bool is_sorted;
        uint32_t min_access_reputation;
        
        ReputationSortedArray() : is_sorted(false), min_access_reputation(0) {}
    };
    std::map<std::string, ReputationSortedArray> reputation_arrays;
    
    // Reputation-weighted stack
    struct ReputationWeightedStackEntry {
        uint256 value;
        uint32_t reputation_weight;
        int64_t timestamp;
    };
    std::vector<ReputationWeightedStackEntry> reputation_stack;
    
    // Trust score cache
    struct CachedTrustScore {
        uint32_t score;
        int64_t timestamp;
        bool is_valid;
    };
    std::map<uint160, CachedTrustScore> trust_score_cache;
    
    // Cross-chain trust state
    std::map<uint160, std::vector<uint32_t>> cross_chain_trust_scores;
    
    // Performance metrics
    EngineStats stats;
    std::string last_execution_trace;
    
    // Constants
    static constexpr uint32_t HIGH_REPUTATION_THRESHOLD = 80;
    static constexpr uint32_t FREE_GAS_REPUTATION_THRESHOLD = 80;
    static constexpr uint64_t MAX_FREE_GAS_PER_TRANSACTION = 100000;
    static constexpr size_t MAX_TRUST_TAGGED_REGIONS = 256;
    static constexpr size_t MAX_REPUTATION_STACK_SIZE = 1024;
    static constexpr int64_t TRUST_SCORE_CACHE_TTL = 3600; // 1 hour in seconds
};

/**
 * EVM Engine Factory
 * 
 * Creates and configures EVM engine instances for different use cases
 */
class EVMEngineFactory {
public:
    // Standard configurations
    static std::unique_ptr<EVMEngine> CreateMainnetEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EVMEngine> CreateTestnetEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EVMEngine> CreateLocalEngine(CVMDatabase* db);
    
    // Specialized configurations
    static std::unique_ptr<EVMEngine> CreateHighPerformanceEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EVMEngine> CreateDebugEngine(CVMDatabase* db, std::shared_ptr<TrustContext> trust_ctx);
    static std::unique_ptr<EVMEngine> CreateCompatibilityTestEngine(CVMDatabase* db);
    
    // Configuration helpers
    static void ConfigureForMainnet(EVMEngine* engine);
    static void ConfigureForTesting(EVMEngine* engine);
    static void ConfigureForDebugging(EVMEngine* engine);
};

/**
 * EVM Engine Utilities
 */
namespace EVMEngineUtils {
    // Result analysis
    bool IsSuccessfulExecution(const EVMExecutionResult& result);
    bool HasReputationBenefits(const EVMExecutionResult& result);
    double CalculateGasSavingsPercentage(const EVMExecutionResult& result);
    
    // Compatibility checking
    bool IsEthereumCompatible(const std::vector<uint8_t>& bytecode);
    std::vector<std::string> AnalyzeCompatibilityIssues(const std::vector<uint8_t>& bytecode);
    
    // Trust analysis
    struct TrustMetrics {
        double average_reputation_score;
        double gas_savings_percentage;
        size_t high_reputation_calls;
        size_t trust_gate_failures;
        size_t free_gas_transactions;
    };
    
    TrustMetrics AnalyzeTrustMetrics(const std::vector<EVMExecutionResult>& results);
    
    // Performance analysis
    struct PerformanceMetrics {
        double executions_per_second;
        double average_gas_per_execution;
        double trust_overhead_percentage;
        std::map<uint8_t, double> opcode_performance;
    };
    
    PerformanceMetrics AnalyzePerformance(const EVMEngine::EngineStats& stats, int64_t time_period_ms);
    
    // Debugging utilities
    std::string FormatExecutionResult(const EVMExecutionResult& result);
    std::string FormatEngineStats(const EVMEngine::EngineStats& stats);
    std::string DecodeBytecode(const std::vector<uint8_t>& bytecode);
    
    // Gas analysis
    struct GasAnalysis {
        uint64_t base_gas_cost;
        uint64_t reputation_discount;
        uint64_t final_gas_cost;
        double savings_percentage;
        bool used_free_gas;
    };
    
    GasAnalysis AnalyzeGasCosts(const EVMExecutionResult& result);
}

} // namespace CVM

#endif // ENABLE_EVMC

#endif // CASCOIN_CVM_EVM_ENGINE_H