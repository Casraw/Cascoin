// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/metrics.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace CVM {

// Global instances
std::unique_ptr<PrometheusMetricsExporter> g_metricsExporter;
std::unique_ptr<TrustOperationLogger> g_trustLogger;

// ============================================================================
// PrometheusMetricsExporter Implementation
// ============================================================================

PrometheusMetricsExporter::PrometheusMetricsExporter()
    : m_port(9100)
{
    // Initialize execution time histogram buckets (microseconds)
    m_executionTimeBuckets = {100, 500, 1000, 5000, 10000, 50000, 100000, 500000, 1000000};
    for (double bucket : m_executionTimeBuckets) {
        m_executionTimeHistogram[bucket].store(0);
    }
}

PrometheusMetricsExporter::~PrometheusMetricsExporter()
{
    Shutdown();
}

bool PrometheusMetricsExporter::Initialize(uint16_t port)
{
    LOCK(m_cs);
    
    if (m_running) {
        return true;
    }
    
    m_port = port;
    m_startTime = std::chrono::steady_clock::now();
    m_running = true;
    
    LogPrintf("CVM Metrics: Prometheus metrics exporter initialized on port %d\n", port);
    return true;
}

void PrometheusMetricsExporter::Shutdown()
{
    LOCK(m_cs);
    
    if (!m_running) {
        return;
    }
    
    m_running = false;
    LogPrintf("CVM Metrics: Prometheus metrics exporter shutdown\n");
}

std::string PrometheusMetricsExporter::GetPrometheusMetrics() const
{
    LOCK(m_cs);
    
    std::ostringstream ss;
    
    // ========== EVM Execution Metrics ==========
    ss << "# HELP cvm_evm_executions_total Total number of EVM executions\n";
    ss << "# TYPE cvm_evm_executions_total counter\n";
    ss << "cvm_evm_executions_total{status=\"success\"} " << m_evmMetrics.successfulExecutions.load() << "\n";
    ss << "cvm_evm_executions_total{status=\"failed\"} " << m_evmMetrics.failedExecutions.load() << "\n";
    ss << "cvm_evm_executions_total{status=\"out_of_gas\"} " << m_evmMetrics.outOfGasExecutions.load() << "\n";
    ss << "cvm_evm_executions_total{status=\"reverted\"} " << m_evmMetrics.revertedExecutions.load() << "\n";
    
    ss << "# HELP cvm_evm_gas_used_total Total gas used in EVM executions\n";
    ss << "# TYPE cvm_evm_gas_used_total counter\n";
    ss << "cvm_evm_gas_used_total " << m_evmMetrics.totalGasUsed.load() << "\n";
    
    ss << "# HELP cvm_evm_gas_limit_total Total gas limit in EVM executions\n";
    ss << "# TYPE cvm_evm_gas_limit_total counter\n";
    ss << "cvm_evm_gas_limit_total " << m_evmMetrics.totalGasLimit.load() << "\n";
    
    ss << "# HELP cvm_evm_free_gas_used_total Total free gas used\n";
    ss << "# TYPE cvm_evm_free_gas_used_total counter\n";
    ss << "cvm_evm_free_gas_used_total " << m_evmMetrics.freeGasUsed.load() << "\n";
    
    ss << "# HELP cvm_evm_subsidized_gas_used_total Total subsidized gas used\n";
    ss << "# TYPE cvm_evm_subsidized_gas_used_total counter\n";
    ss << "cvm_evm_subsidized_gas_used_total " << m_evmMetrics.subsidizedGasUsed.load() << "\n";
    
    ss << "# HELP cvm_evm_execution_time_microseconds_total Total execution time in microseconds\n";
    ss << "# TYPE cvm_evm_execution_time_microseconds_total counter\n";
    ss << "cvm_evm_execution_time_microseconds_total " << m_evmMetrics.totalExecutionTime.load() << "\n";
    
    ss << "# HELP cvm_evm_contract_deployments_total Total contract deployments\n";
    ss << "# TYPE cvm_evm_contract_deployments_total counter\n";
    ss << "cvm_evm_contract_deployments_total " << m_evmMetrics.contractDeployments.load() << "\n";
    
    ss << "# HELP cvm_evm_contract_calls_total Total contract calls\n";
    ss << "# TYPE cvm_evm_contract_calls_total counter\n";
    ss << "cvm_evm_contract_calls_total{type=\"external\"} " << m_evmMetrics.contractCalls.load() << "\n";
    ss << "cvm_evm_contract_calls_total{type=\"internal\"} " << m_evmMetrics.internalCalls.load() << "\n";
    
    ss << "# HELP cvm_evm_storage_operations_total Total storage operations\n";
    ss << "# TYPE cvm_evm_storage_operations_total counter\n";
    ss << "cvm_evm_storage_operations_total{type=\"read\"} " << m_evmMetrics.storageReads.load() << "\n";
    ss << "cvm_evm_storage_operations_total{type=\"write\"} " << m_evmMetrics.storageWrites.load() << "\n";
    
    ss << "# HELP cvm_evm_storage_bytes_written_total Total bytes written to storage\n";
    ss << "# TYPE cvm_evm_storage_bytes_written_total counter\n";
    ss << "cvm_evm_storage_bytes_written_total " << m_evmMetrics.storageBytesWritten.load() << "\n";
    
    // ========== Trust Operation Metrics ==========
    ss << "# HELP cvm_trust_context_injections_total Total trust context injections\n";
    ss << "# TYPE cvm_trust_context_injections_total counter\n";
    ss << "cvm_trust_context_injections_total{status=\"success\"} " << m_trustMetrics.trustContextInjections.load() << "\n";
    ss << "cvm_trust_context_injections_total{status=\"failed\"} " << m_trustMetrics.trustContextFailures.load() << "\n";
    
    ss << "# HELP cvm_reputation_queries_total Total reputation queries\n";
    ss << "# TYPE cvm_reputation_queries_total counter\n";
    ss << "cvm_reputation_queries_total{cache=\"hit\"} " << m_trustMetrics.reputationCacheHits.load() << "\n";
    ss << "cvm_reputation_queries_total{cache=\"miss\"} " << m_trustMetrics.reputationCacheMisses.load() << "\n";
    
    ss << "# HELP cvm_trust_gated_operations_total Total trust-gated operations\n";
    ss << "# TYPE cvm_trust_gated_operations_total counter\n";
    ss << "cvm_trust_gated_operations_total{result=\"allowed\"} " << m_trustMetrics.trustGatedOperations.load() << "\n";
    ss << "cvm_trust_gated_operations_total{result=\"denied\"} " << m_trustMetrics.trustGatedDenials.load() << "\n";
    
    ss << "# HELP cvm_gas_discounts_applied_total Total gas discounts applied\n";
    ss << "# TYPE cvm_gas_discounts_applied_total counter\n";
    ss << "cvm_gas_discounts_applied_total " << m_trustMetrics.gasDiscountsApplied.load() << "\n";
    
    ss << "# HELP cvm_gas_discount_amount_total Total gas discount amount\n";
    ss << "# TYPE cvm_gas_discount_amount_total counter\n";
    ss << "cvm_gas_discount_amount_total " << m_trustMetrics.totalGasDiscountAmount.load() << "\n";
    
    ss << "# HELP cvm_free_gas_transactions_total Total free gas transactions\n";
    ss << "# TYPE cvm_free_gas_transactions_total counter\n";
    ss << "cvm_free_gas_transactions_total " << m_trustMetrics.freeGasTransactions.load() << "\n";
    
    // HAT v2 Consensus Metrics
    ss << "# HELP cvm_hat_validations_total Total HAT v2 validations\n";
    ss << "# TYPE cvm_hat_validations_total counter\n";
    ss << "cvm_hat_validations_total{status=\"success\"} " << m_trustMetrics.validationSuccesses.load() << "\n";
    ss << "cvm_hat_validations_total{status=\"failed\"} " << m_trustMetrics.validationFailures.load() << "\n";
    ss << "cvm_hat_validations_total{status=\"timeout\"} " << m_trustMetrics.validationTimeouts.load() << "\n";
    
    ss << "# HELP cvm_hat_validation_requests_total Total validation requests\n";
    ss << "# TYPE cvm_hat_validation_requests_total counter\n";
    ss << "cvm_hat_validation_requests_total " << m_trustMetrics.validationRequests.load() << "\n";
    
    ss << "# HELP cvm_dao_disputes_total Total DAO disputes\n";
    ss << "# TYPE cvm_dao_disputes_total counter\n";
    ss << "cvm_dao_disputes_total " << m_trustMetrics.daoDisputes.load() << "\n";
    
    ss << "# HELP cvm_validator_responses_total Total validator responses\n";
    ss << "# TYPE cvm_validator_responses_total counter\n";
    ss << "cvm_validator_responses_total{vote=\"accept\"} " << m_trustMetrics.validatorAccepts.load() << "\n";
    ss << "cvm_validator_responses_total{vote=\"reject\"} " << m_trustMetrics.validatorRejects.load() << "\n";
    ss << "cvm_validator_responses_total{vote=\"abstain\"} " << m_trustMetrics.validatorAbstains.load() << "\n";
    
    ss << "# HELP cvm_fraud_attempts_total Total fraud attempts detected\n";
    ss << "# TYPE cvm_fraud_attempts_total counter\n";
    ss << "cvm_fraud_attempts_total " << m_trustMetrics.fraudAttemptsDetected.load() << "\n";
    
    ss << "# HELP cvm_fraud_records_total Total fraud records created\n";
    ss << "# TYPE cvm_fraud_records_total counter\n";
    ss << "cvm_fraud_records_total " << m_trustMetrics.fraudRecordsCreated.load() << "\n";
    
    // ========== Network Metrics ==========
    ss << "# HELP cvm_validation_challenges_total Total validation challenges\n";
    ss << "# TYPE cvm_validation_challenges_total counter\n";
    ss << "cvm_validation_challenges_total{direction=\"sent\"} " << m_networkMetrics.validationChallengesSent.load() << "\n";
    ss << "cvm_validation_challenges_total{direction=\"received\"} " << m_networkMetrics.validationChallengesReceived.load() << "\n";
    
    ss << "# HELP cvm_validation_responses_messages_total Total validation response messages\n";
    ss << "# TYPE cvm_validation_responses_messages_total counter\n";
    ss << "cvm_validation_responses_messages_total{direction=\"sent\"} " << m_networkMetrics.validationResponsesSent.load() << "\n";
    ss << "cvm_validation_responses_messages_total{direction=\"received\"} " << m_networkMetrics.validationResponsesReceived.load() << "\n";
    
    ss << "# HELP cvm_network_bytes_total Total network bytes\n";
    ss << "# TYPE cvm_network_bytes_total counter\n";
    ss << "cvm_network_bytes_total{direction=\"sent\"} " << m_networkMetrics.bytesSent.load() << "\n";
    ss << "cvm_network_bytes_total{direction=\"received\"} " << m_networkMetrics.bytesReceived.load() << "\n";
    
    ss << "# HELP cvm_rate_limited_total Total rate limited requests\n";
    ss << "# TYPE cvm_rate_limited_total counter\n";
    ss << "cvm_rate_limited_total{type=\"transaction\"} " << m_networkMetrics.rateLimitedTransactions.load() << "\n";
    ss << "cvm_rate_limited_total{type=\"rpc\"} " << m_networkMetrics.rateLimitedRPCCalls.load() << "\n";
    ss << "cvm_rate_limited_total{type=\"p2p\"} " << m_networkMetrics.rateLimitedP2PMessages.load() << "\n";
    
    // ========== System Metrics ==========
    ss << "# HELP cvm_uptime_seconds CVM uptime in seconds\n";
    ss << "# TYPE cvm_uptime_seconds gauge\n";
    ss << "cvm_uptime_seconds " << GetUptimeSeconds() << "\n";
    
    return ss.str();
}


std::string PrometheusMetricsExporter::GetJSONMetrics() const
{
    LOCK(m_cs);
    
    std::ostringstream ss;
    ss << "{\n";
    
    // EVM Metrics
    ss << "  \"evm\": {\n";
    ss << "    \"total_executions\": " << m_evmMetrics.totalExecutions.load() << ",\n";
    ss << "    \"successful_executions\": " << m_evmMetrics.successfulExecutions.load() << ",\n";
    ss << "    \"failed_executions\": " << m_evmMetrics.failedExecutions.load() << ",\n";
    ss << "    \"out_of_gas_executions\": " << m_evmMetrics.outOfGasExecutions.load() << ",\n";
    ss << "    \"reverted_executions\": " << m_evmMetrics.revertedExecutions.load() << ",\n";
    ss << "    \"total_gas_used\": " << m_evmMetrics.totalGasUsed.load() << ",\n";
    ss << "    \"total_gas_limit\": " << m_evmMetrics.totalGasLimit.load() << ",\n";
    ss << "    \"free_gas_used\": " << m_evmMetrics.freeGasUsed.load() << ",\n";
    ss << "    \"subsidized_gas_used\": " << m_evmMetrics.subsidizedGasUsed.load() << ",\n";
    ss << "    \"total_execution_time_us\": " << m_evmMetrics.totalExecutionTime.load() << ",\n";
    ss << "    \"contract_deployments\": " << m_evmMetrics.contractDeployments.load() << ",\n";
    ss << "    \"contract_calls\": " << m_evmMetrics.contractCalls.load() << ",\n";
    ss << "    \"internal_calls\": " << m_evmMetrics.internalCalls.load() << ",\n";
    ss << "    \"storage_reads\": " << m_evmMetrics.storageReads.load() << ",\n";
    ss << "    \"storage_writes\": " << m_evmMetrics.storageWrites.load() << ",\n";
    ss << "    \"storage_bytes_written\": " << m_evmMetrics.storageBytesWritten.load() << "\n";
    ss << "  },\n";
    
    // Trust Metrics
    ss << "  \"trust\": {\n";
    ss << "    \"context_injections\": " << m_trustMetrics.trustContextInjections.load() << ",\n";
    ss << "    \"context_failures\": " << m_trustMetrics.trustContextFailures.load() << ",\n";
    ss << "    \"reputation_queries\": " << m_trustMetrics.reputationQueries.load() << ",\n";
    ss << "    \"reputation_cache_hits\": " << m_trustMetrics.reputationCacheHits.load() << ",\n";
    ss << "    \"reputation_cache_misses\": " << m_trustMetrics.reputationCacheMisses.load() << ",\n";
    ss << "    \"trust_gated_operations\": " << m_trustMetrics.trustGatedOperations.load() << ",\n";
    ss << "    \"trust_gated_denials\": " << m_trustMetrics.trustGatedDenials.load() << ",\n";
    ss << "    \"gas_discounts_applied\": " << m_trustMetrics.gasDiscountsApplied.load() << ",\n";
    ss << "    \"total_gas_discount_amount\": " << m_trustMetrics.totalGasDiscountAmount.load() << ",\n";
    ss << "    \"free_gas_transactions\": " << m_trustMetrics.freeGasTransactions.load() << ",\n";
    ss << "    \"validation_requests\": " << m_trustMetrics.validationRequests.load() << ",\n";
    ss << "    \"validation_successes\": " << m_trustMetrics.validationSuccesses.load() << ",\n";
    ss << "    \"validation_failures\": " << m_trustMetrics.validationFailures.load() << ",\n";
    ss << "    \"validation_timeouts\": " << m_trustMetrics.validationTimeouts.load() << ",\n";
    ss << "    \"dao_disputes\": " << m_trustMetrics.daoDisputes.load() << ",\n";
    ss << "    \"validator_responses\": " << m_trustMetrics.validatorResponses.load() << ",\n";
    ss << "    \"validator_accepts\": " << m_trustMetrics.validatorAccepts.load() << ",\n";
    ss << "    \"validator_rejects\": " << m_trustMetrics.validatorRejects.load() << ",\n";
    ss << "    \"validator_abstains\": " << m_trustMetrics.validatorAbstains.load() << ",\n";
    ss << "    \"fraud_attempts_detected\": " << m_trustMetrics.fraudAttemptsDetected.load() << ",\n";
    ss << "    \"fraud_records_created\": " << m_trustMetrics.fraudRecordsCreated.load() << "\n";
    ss << "  },\n";
    
    // Network Metrics
    ss << "  \"network\": {\n";
    ss << "    \"validation_challenges_sent\": " << m_networkMetrics.validationChallengesSent.load() << ",\n";
    ss << "    \"validation_challenges_received\": " << m_networkMetrics.validationChallengesReceived.load() << ",\n";
    ss << "    \"validation_responses_sent\": " << m_networkMetrics.validationResponsesSent.load() << ",\n";
    ss << "    \"validation_responses_received\": " << m_networkMetrics.validationResponsesReceived.load() << ",\n";
    ss << "    \"bytes_sent\": " << m_networkMetrics.bytesSent.load() << ",\n";
    ss << "    \"bytes_received\": " << m_networkMetrics.bytesReceived.load() << ",\n";
    ss << "    \"rate_limited_transactions\": " << m_networkMetrics.rateLimitedTransactions.load() << ",\n";
    ss << "    \"rate_limited_rpc_calls\": " << m_networkMetrics.rateLimitedRPCCalls.load() << ",\n";
    ss << "    \"rate_limited_p2p_messages\": " << m_networkMetrics.rateLimitedP2PMessages.load() << "\n";
    ss << "  },\n";
    
    // System Metrics
    ss << "  \"system\": {\n";
    ss << "    \"uptime_seconds\": " << GetUptimeSeconds() << "\n";
    ss << "  }\n";
    
    ss << "}\n";
    
    return ss.str();
}

void PrometheusMetricsExporter::RecordEVMExecution(bool success, uint64_t gasUsed, uint64_t gasLimit,
                                                   uint64_t executionTimeUs, bool outOfGas, bool reverted)
{
    m_evmMetrics.totalExecutions++;
    m_evmMetrics.totalGasUsed += gasUsed;
    m_evmMetrics.totalGasLimit += gasLimit;
    m_evmMetrics.totalExecutionTime += executionTimeUs;
    
    if (success) {
        m_evmMetrics.successfulExecutions++;
    } else {
        m_evmMetrics.failedExecutions++;
        if (outOfGas) {
            m_evmMetrics.outOfGasExecutions++;
        }
        if (reverted) {
            m_evmMetrics.revertedExecutions++;
        }
    }
    
    // Update min/max execution time
    uint64_t currentMax = m_evmMetrics.maxExecutionTime.load();
    while (executionTimeUs > currentMax && 
           !m_evmMetrics.maxExecutionTime.compare_exchange_weak(currentMax, executionTimeUs)) {}
    
    uint64_t currentMin = m_evmMetrics.minExecutionTime.load();
    while (executionTimeUs < currentMin && 
           !m_evmMetrics.minExecutionTime.compare_exchange_weak(currentMin, executionTimeUs)) {}
    
    // Update histogram
    for (double bucket : m_executionTimeBuckets) {
        if (executionTimeUs <= bucket) {
            m_executionTimeHistogram[bucket]++;
            break;
        }
    }
}

void PrometheusMetricsExporter::RecordContractDeployment(bool success, uint64_t gasUsed)
{
    m_evmMetrics.contractDeployments++;
    if (success) {
        m_evmMetrics.totalGasUsed += gasUsed;
    }
}

void PrometheusMetricsExporter::RecordContractCall(bool success, uint64_t gasUsed, bool isInternal)
{
    if (isInternal) {
        m_evmMetrics.internalCalls++;
    } else {
        m_evmMetrics.contractCalls++;
    }
    if (success) {
        m_evmMetrics.totalGasUsed += gasUsed;
    }
}

void PrometheusMetricsExporter::RecordStorageOperation(bool isWrite, uint64_t bytesAffected)
{
    if (isWrite) {
        m_evmMetrics.storageWrites++;
        m_evmMetrics.storageBytesWritten += bytesAffected;
    } else {
        m_evmMetrics.storageReads++;
    }
}

void PrometheusMetricsExporter::RecordOpcodeExecution(uint8_t opcode)
{
    // Note: This would need thread-safe map access in production
    // For now, we skip detailed opcode tracking
}

void PrometheusMetricsExporter::RecordTrustContextInjection(bool success)
{
    if (success) {
        m_trustMetrics.trustContextInjections++;
    } else {
        m_trustMetrics.trustContextFailures++;
    }
}

void PrometheusMetricsExporter::RecordReputationQuery(bool cacheHit)
{
    m_trustMetrics.reputationQueries++;
    if (cacheHit) {
        m_trustMetrics.reputationCacheHits++;
    } else {
        m_trustMetrics.reputationCacheMisses++;
    }
}

void PrometheusMetricsExporter::RecordTrustGatedOperation(bool allowed)
{
    if (allowed) {
        m_trustMetrics.trustGatedOperations++;
    } else {
        m_trustMetrics.trustGatedDenials++;
    }
}

void PrometheusMetricsExporter::RecordGasDiscount(uint64_t discountAmount, bool isFreeGas)
{
    m_trustMetrics.gasDiscountsApplied++;
    m_trustMetrics.totalGasDiscountAmount += discountAmount;
    if (isFreeGas) {
        m_trustMetrics.freeGasTransactions++;
        m_evmMetrics.freeGasUsed += discountAmount;
    } else {
        m_evmMetrics.subsidizedGasUsed += discountAmount;
    }
}

void PrometheusMetricsExporter::RecordValidation(bool success, bool timeout, bool disputed)
{
    m_trustMetrics.validationRequests++;
    if (success) {
        m_trustMetrics.validationSuccesses++;
    } else {
        m_trustMetrics.validationFailures++;
    }
    if (timeout) {
        m_trustMetrics.validationTimeouts++;
    }
    if (disputed) {
        m_trustMetrics.daoDisputes++;
    }
}

void PrometheusMetricsExporter::RecordValidatorResponse(int vote)
{
    m_trustMetrics.validatorResponses++;
    if (vote > 0) {
        m_trustMetrics.validatorAccepts++;
    } else if (vote < 0) {
        m_trustMetrics.validatorRejects++;
    } else {
        m_trustMetrics.validatorAbstains++;
    }
}

void PrometheusMetricsExporter::RecordFraudDetection(bool recordCreated)
{
    m_trustMetrics.fraudAttemptsDetected++;
    if (recordCreated) {
        m_trustMetrics.fraudRecordsCreated++;
    }
}

void PrometheusMetricsExporter::RecordValidationChallenge(bool sent)
{
    if (sent) {
        m_networkMetrics.validationChallengesSent++;
    } else {
        m_networkMetrics.validationChallengesReceived++;
    }
}

void PrometheusMetricsExporter::RecordValidationResponseMessage(bool sent)
{
    if (sent) {
        m_networkMetrics.validationResponsesSent++;
    } else {
        m_networkMetrics.validationResponsesReceived++;
    }
}

void PrometheusMetricsExporter::RecordBandwidth(uint64_t bytes, bool sent)
{
    if (sent) {
        m_networkMetrics.bytesSent += bytes;
    } else {
        m_networkMetrics.bytesReceived += bytes;
    }
}

void PrometheusMetricsExporter::RecordRateLimiting(const std::string& type)
{
    if (type == "transaction") {
        m_networkMetrics.rateLimitedTransactions++;
    } else if (type == "rpc") {
        m_networkMetrics.rateLimitedRPCCalls++;
    } else if (type == "p2p") {
        m_networkMetrics.rateLimitedP2PMessages++;
    }
}

void PrometheusMetricsExporter::ResetMetrics()
{
    LOCK(m_cs);
    m_evmMetrics.Reset();
    m_trustMetrics.Reset();
    m_networkMetrics.Reset();
    
    for (auto& bucket : m_executionTimeHistogram) {
        bucket.second.store(0);
    }
}

uint64_t PrometheusMetricsExporter::GetUptimeSeconds() const
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();
}

std::string PrometheusMetricsExporter::FormatCounter(const std::string& name, const std::string& help,
                                                     uint64_t value, const std::vector<MetricLabel>& labels) const
{
    std::ostringstream ss;
    ss << "# HELP " << name << " " << help << "\n";
    ss << "# TYPE " << name << " counter\n";
    ss << name << FormatLabels(labels) << " " << value << "\n";
    return ss.str();
}

std::string PrometheusMetricsExporter::FormatGauge(const std::string& name, const std::string& help,
                                                   double value, const std::vector<MetricLabel>& labels) const
{
    std::ostringstream ss;
    ss << "# HELP " << name << " " << help << "\n";
    ss << "# TYPE " << name << " gauge\n";
    ss << name << FormatLabels(labels) << " " << value << "\n";
    return ss.str();
}

std::string PrometheusMetricsExporter::FormatLabels(const std::vector<MetricLabel>& labels) const
{
    if (labels.empty()) {
        return "";
    }
    
    std::ostringstream ss;
    ss << "{";
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) ss << ",";
        ss << labels[i].key << "=\"" << labels[i].value << "\"";
    }
    ss << "}";
    return ss.str();
}


// ============================================================================
// TrustOperationLogger Implementation
// ============================================================================

TrustOperationLogger::TrustOperationLogger()
    : m_minLevel(LogLevel::INFO)
    , m_currentBlockHeight(0)
    , m_maxEntriesInMemory(10000)
{
}

TrustOperationLogger::~TrustOperationLogger()
{
    Shutdown();
}

bool TrustOperationLogger::Initialize(const std::string& logPath, LogLevel minLevel)
{
    LOCK(m_cs);
    
    if (m_initialized) {
        return true;
    }
    
    m_logPath = logPath;
    m_minLevel = minLevel;
    m_initialized = true;
    
    LogPrintf("CVM Trust Logger: Initialized with log level %s\n", LevelToString(minLevel));
    if (!logPath.empty()) {
        LogPrintf("CVM Trust Logger: Logging to file %s\n", logPath);
    }
    
    return true;
}

void TrustOperationLogger::Shutdown()
{
    LOCK(m_cs);
    
    if (!m_initialized) {
        return;
    }
    
    m_initialized = false;
    LogPrintf("CVM Trust Logger: Shutdown\n");
}

void TrustOperationLogger::LogTrustContextInjection(const uint160& caller, const uint160& contract,
                                                    uint8_t callerReputation, bool success,
                                                    const std::string& reason)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = success ? LogLevel::DEBUG : LogLevel::WARNING;
    entry.category = "trust_context";
    entry.address = caller;
    entry.blockHeight = m_currentBlockHeight;
    
    if (success) {
        entry.message = strprintf("Trust context injected for caller %s (reputation: %d) calling contract %s",
                                  caller.GetHex().substr(0, 16), callerReputation, contract.GetHex().substr(0, 16));
    } else {
        entry.message = strprintf("Trust context injection failed for caller %s: %s",
                                  caller.GetHex().substr(0, 16), reason);
    }
    
    entry.context["caller"] = caller.GetHex();
    entry.context["contract"] = contract.GetHex();
    entry.context["reputation"] = std::to_string(callerReputation);
    entry.context["success"] = success ? "true" : "false";
    if (!reason.empty()) {
        entry.context["reason"] = reason;
    }
    
    AddEntry(entry);
}

void TrustOperationLogger::LogReputationQuery(const uint160& requester, const uint160& target,
                                              uint8_t score, bool cacheHit)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::DEBUG;
    entry.category = "reputation_query";
    entry.address = target;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("Reputation query: %s queried %s, score=%d, cache=%s",
                              requester.GetHex().substr(0, 16), target.GetHex().substr(0, 16),
                              score, cacheHit ? "hit" : "miss");
    
    entry.context["requester"] = requester.GetHex();
    entry.context["target"] = target.GetHex();
    entry.context["score"] = std::to_string(score);
    entry.context["cache_hit"] = cacheHit ? "true" : "false";
    
    AddEntry(entry);
}

void TrustOperationLogger::LogTrustGatedOperation(const uint160& caller, const std::string& operation,
                                                  uint8_t requiredReputation, uint8_t actualReputation,
                                                  bool allowed)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = allowed ? LogLevel::INFO : LogLevel::WARNING;
    entry.category = "trust_gated";
    entry.address = caller;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("Trust-gated operation '%s' by %s: required=%d, actual=%d, %s",
                              operation, caller.GetHex().substr(0, 16),
                              requiredReputation, actualReputation,
                              allowed ? "ALLOWED" : "DENIED");
    
    entry.context["caller"] = caller.GetHex();
    entry.context["operation"] = operation;
    entry.context["required_reputation"] = std::to_string(requiredReputation);
    entry.context["actual_reputation"] = std::to_string(actualReputation);
    entry.context["allowed"] = allowed ? "true" : "false";
    
    AddEntry(entry);
}

void TrustOperationLogger::LogGasDiscount(const uint160& address, uint64_t originalGas,
                                          uint64_t discountedGas, uint8_t reputation)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::DEBUG;
    entry.category = "gas_discount";
    entry.address = address;
    entry.blockHeight = m_currentBlockHeight;
    
    uint64_t discount = originalGas - discountedGas;
    double discountPercent = (originalGas > 0) ? (100.0 * discount / originalGas) : 0;
    
    entry.message = strprintf("Gas discount applied for %s: %lu -> %lu (%.1f%% off, reputation=%d)",
                              address.GetHex().substr(0, 16), originalGas, discountedGas,
                              discountPercent, reputation);
    
    entry.context["address"] = address.GetHex();
    entry.context["original_gas"] = std::to_string(originalGas);
    entry.context["discounted_gas"] = std::to_string(discountedGas);
    entry.context["discount_amount"] = std::to_string(discount);
    entry.context["reputation"] = std::to_string(reputation);
    
    AddEntry(entry);
}

void TrustOperationLogger::LogFreeGasUsage(const uint160& address, uint64_t gasUsed,
                                           uint64_t allowanceRemaining)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::INFO;
    entry.category = "free_gas";
    entry.address = address;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("Free gas used by %s: %lu gas, %lu remaining",
                              address.GetHex().substr(0, 16), gasUsed, allowanceRemaining);
    
    entry.context["address"] = address.GetHex();
    entry.context["gas_used"] = std::to_string(gasUsed);
    entry.context["allowance_remaining"] = std::to_string(allowanceRemaining);
    
    AddEntry(entry);
}

void TrustOperationLogger::LogGasSubsidy(const uint160& address, uint64_t subsidyAmount,
                                         const std::string& reason)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::INFO;
    entry.category = "gas_subsidy";
    entry.address = address;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("Gas subsidy for %s: %lu gas (%s)",
                              address.GetHex().substr(0, 16), subsidyAmount, reason);
    
    entry.context["address"] = address.GetHex();
    entry.context["subsidy_amount"] = std::to_string(subsidyAmount);
    entry.context["reason"] = reason;
    
    AddEntry(entry);
}

void TrustOperationLogger::LogValidationRequest(const uint256& txHash, const uint160& sender,
                                                uint8_t selfReportedScore,
                                                const std::vector<uint160>& validators)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::INFO;
    entry.category = "hat_validation";
    entry.address = sender;
    entry.txHash = txHash;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("HAT v2 validation request for tx %s from %s (score=%d, %zu validators)",
                              txHash.GetHex().substr(0, 16), sender.GetHex().substr(0, 16),
                              selfReportedScore, validators.size());
    
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["sender"] = sender.GetHex();
    entry.context["self_reported_score"] = std::to_string(selfReportedScore);
    entry.context["validator_count"] = std::to_string(validators.size());
    
    AddEntry(entry);
}

void TrustOperationLogger::LogValidatorResponse(const uint256& txHash, const uint160& validator,
                                                int vote, double confidence, bool hasWoT,
                                                uint8_t calculatedScore, uint8_t reportedScore)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::DEBUG;
    entry.category = "validator_response";
    entry.address = validator;
    entry.txHash = txHash;
    entry.blockHeight = m_currentBlockHeight;
    
    std::string voteStr = (vote > 0) ? "ACCEPT" : ((vote < 0) ? "REJECT" : "ABSTAIN");
    entry.message = strprintf("Validator %s response for tx %s: %s (confidence=%.2f, WoT=%s, calc=%d, reported=%d)",
                              validator.GetHex().substr(0, 16), txHash.GetHex().substr(0, 16),
                              voteStr, confidence, hasWoT ? "yes" : "no",
                              calculatedScore, reportedScore);
    
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["validator"] = validator.GetHex();
    entry.context["vote"] = voteStr;
    entry.context["confidence"] = strprintf("%.2f", confidence);
    entry.context["has_wot"] = hasWoT ? "true" : "false";
    entry.context["calculated_score"] = std::to_string(calculatedScore);
    entry.context["reported_score"] = std::to_string(reportedScore);
    
    AddEntry(entry);
}

void TrustOperationLogger::LogConsensusResult(const uint256& txHash, bool consensusReached,
                                              double acceptanceRate, int totalResponses)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = consensusReached ? LogLevel::INFO : LogLevel::WARNING;
    entry.category = "consensus_result";
    entry.txHash = txHash;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("HAT v2 consensus for tx %s: %s (acceptance=%.1f%%, responses=%d)",
                              txHash.GetHex().substr(0, 16),
                              consensusReached ? "REACHED" : "FAILED",
                              acceptanceRate * 100, totalResponses);
    
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["consensus_reached"] = consensusReached ? "true" : "false";
    entry.context["acceptance_rate"] = strprintf("%.4f", acceptanceRate);
    entry.context["total_responses"] = std::to_string(totalResponses);
    
    AddEntry(entry);
}

void TrustOperationLogger::LogDAODispute(const uint256& disputeId, const uint256& txHash,
                                         const uint160& address, const std::string& reason)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::WARNING;
    entry.category = "dao_dispute";
    entry.address = address;
    entry.txHash = txHash;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("DAO dispute created: %s for tx %s, address %s: %s",
                              disputeId.GetHex().substr(0, 16), txHash.GetHex().substr(0, 16),
                              address.GetHex().substr(0, 16), reason);
    
    entry.context["dispute_id"] = disputeId.GetHex();
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["address"] = address.GetHex();
    entry.context["reason"] = reason;
    
    AddEntry(entry);
}

void TrustOperationLogger::LogDAOResolution(const uint256& disputeId, bool approved,
                                            const std::string& resolution)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::INFO;
    entry.category = "dao_resolution";
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("DAO dispute %s resolved: %s - %s",
                              disputeId.GetHex().substr(0, 16),
                              approved ? "APPROVED" : "REJECTED", resolution);
    
    entry.context["dispute_id"] = disputeId.GetHex();
    entry.context["approved"] = approved ? "true" : "false";
    entry.context["resolution"] = resolution;
    
    AddEntry(entry);
}

void TrustOperationLogger::LogFraudAttempt(const uint160& address, const uint256& txHash,
                                           uint8_t claimedScore, uint8_t actualScore,
                                           const std::string& details)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::ERROR;
    entry.category = "fraud_attempt";
    entry.address = address;
    entry.txHash = txHash;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("FRAUD ATTEMPT DETECTED: %s claimed score %d but actual is %d (diff=%d): %s",
                              address.GetHex().substr(0, 16), claimedScore, actualScore,
                              claimedScore - actualScore, details);
    
    entry.context["address"] = address.GetHex();
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["claimed_score"] = std::to_string(claimedScore);
    entry.context["actual_score"] = std::to_string(actualScore);
    entry.context["score_difference"] = std::to_string(claimedScore - actualScore);
    entry.context["details"] = details;
    
    AddEntry(entry);
}

void TrustOperationLogger::LogFraudRecordCreation(const uint160& address, const uint256& txHash,
                                                  int blockHeight)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = LogLevel::CRITICAL;
    entry.category = "fraud_record";
    entry.address = address;
    entry.txHash = txHash;
    entry.blockHeight = blockHeight;
    
    entry.message = strprintf("FRAUD RECORD CREATED: %s at block %d for tx %s",
                              address.GetHex().substr(0, 16), blockHeight,
                              txHash.GetHex().substr(0, 16));
    
    entry.context["address"] = address.GetHex();
    entry.context["tx_hash"] = txHash.GetHex();
    entry.context["block_height"] = std::to_string(blockHeight);
    
    AddEntry(entry);
}

void TrustOperationLogger::LogSecurityEvent(LogLevel level, const std::string& category,
                                            const std::string& message,
                                            const std::map<std::string, std::string>& context)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.context = context;
    entry.blockHeight = m_currentBlockHeight;
    
    AddEntry(entry);
}

void TrustOperationLogger::LogAnomalyDetection(const uint160& address, const std::string& anomalyType,
                                               double severity, const std::string& description)
{
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = (severity > 0.7) ? LogLevel::ERROR : ((severity > 0.4) ? LogLevel::WARNING : LogLevel::INFO);
    entry.category = "anomaly_detection";
    entry.address = address;
    entry.blockHeight = m_currentBlockHeight;
    
    entry.message = strprintf("Anomaly detected for %s: %s (severity=%.2f) - %s",
                              address.GetHex().substr(0, 16), anomalyType, severity, description);
    
    entry.context["address"] = address.GetHex();
    entry.context["anomaly_type"] = anomalyType;
    entry.context["severity"] = strprintf("%.2f", severity);
    entry.context["description"] = description;
    
    AddEntry(entry);
}

std::vector<TrustOperationLogger::LogEntry> TrustOperationLogger::GetRecentEntries(size_t count) const
{
    LOCK(m_cs);
    
    std::vector<LogEntry> result;
    size_t start = (m_recentEntries.size() > count) ? (m_recentEntries.size() - count) : 0;
    for (size_t i = start; i < m_recentEntries.size(); ++i) {
        result.push_back(m_recentEntries[i]);
    }
    return result;
}

std::vector<TrustOperationLogger::LogEntry> TrustOperationLogger::GetEntriesByLevel(LogLevel level, size_t count) const
{
    LOCK(m_cs);
    
    std::vector<LogEntry> result;
    for (auto it = m_recentEntries.rbegin(); it != m_recentEntries.rend() && result.size() < count; ++it) {
        if (it->level == level) {
            result.push_back(*it);
        }
    }
    return result;
}

std::vector<TrustOperationLogger::LogEntry> TrustOperationLogger::GetEntriesForAddress(const uint160& address, size_t count) const
{
    LOCK(m_cs);
    
    std::vector<LogEntry> result;
    for (auto it = m_recentEntries.rbegin(); it != m_recentEntries.rend() && result.size() < count; ++it) {
        if (it->address == address) {
            result.push_back(*it);
        }
    }
    return result;
}

void TrustOperationLogger::AddEntry(const LogEntry& entry)
{
    if (entry.level < m_minLevel) {
        return;
    }
    
    {
        LOCK(m_cs);
        m_recentEntries.push_back(entry);
        while (m_recentEntries.size() > m_maxEntriesInMemory) {
            m_recentEntries.pop_front();
        }
    }
    
    WriteToStdout(entry);
    if (!m_logPath.empty()) {
        WriteToFile(entry);
    }
}

void TrustOperationLogger::WriteToFile(const LogEntry& entry)
{
    std::ofstream file(m_logPath, std::ios::app);
    if (file.is_open()) {
        file << FormatEntry(entry) << "\n";
    }
}

void TrustOperationLogger::WriteToStdout(const LogEntry& entry)
{
    LogPrintf("CVM [%s] %s: %s\n", LevelToString(entry.level), entry.category, entry.message);
}

std::string TrustOperationLogger::FormatEntry(const LogEntry& entry) const
{
    std::ostringstream ss;
    ss << entry.timestamp << " [" << LevelToString(entry.level) << "] "
       << "[" << entry.category << "] " << entry.message;
    
    if (!entry.context.empty()) {
        ss << " {";
        bool first = true;
        for (const auto& kv : entry.context) {
            if (!first) ss << ", ";
            ss << kv.first << "=" << kv.second;
            first = false;
        }
        ss << "}";
    }
    
    return ss.str();
}

std::string TrustOperationLogger::LevelToString(LogLevel level) const
{
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

int64_t TrustOperationLogger::GetCurrentTimestamp() const
{
    return GetTimeMillis();
}

// ============================================================================
// Global Initialization Functions
// ============================================================================

bool InitializeMonitoring(uint16_t metricsPort, const std::string& logPath,
                          TrustOperationLogger::LogLevel minLevel)
{
    // Initialize metrics exporter
    g_metricsExporter = std::make_unique<PrometheusMetricsExporter>();
    if (!g_metricsExporter->Initialize(metricsPort)) {
        LogPrintf("CVM Monitoring: Failed to initialize metrics exporter\n");
        return false;
    }
    
    // Initialize trust logger
    g_trustLogger = std::make_unique<TrustOperationLogger>();
    if (!g_trustLogger->Initialize(logPath, minLevel)) {
        LogPrintf("CVM Monitoring: Failed to initialize trust logger\n");
        return false;
    }
    
    LogPrintf("CVM Monitoring: Initialized successfully\n");
    return true;
}

void ShutdownMonitoring()
{
    if (g_metricsExporter) {
        g_metricsExporter->Shutdown();
        g_metricsExporter.reset();
    }
    
    if (g_trustLogger) {
        g_trustLogger->Shutdown();
        g_trustLogger.reset();
    }
    
    LogPrintf("CVM Monitoring: Shutdown complete\n");
}

} // namespace CVM
