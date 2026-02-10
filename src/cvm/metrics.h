// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_METRICS_H
#define CASCOIN_CVM_METRICS_H

#include <uint256.h>
#include <sync.h>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <deque>

namespace CVM {

/**
 * Metric Type
 * 
 * Types of metrics that can be collected.
 */
enum class MetricType {
    COUNTER,        // Monotonically increasing counter
    GAUGE,          // Value that can go up or down
    HISTOGRAM,      // Distribution of values
    SUMMARY         // Statistical summary
};

/**
 * Metric Label
 * 
 * Key-value pair for metric labeling.
 */
struct MetricLabel {
    std::string key;
    std::string value;
    
    MetricLabel() = default;
    MetricLabel(const std::string& k, const std::string& v) : key(k), value(v) {}
    
    bool operator<(const MetricLabel& other) const {
        if (key != other.key) return key < other.key;
        return value < other.value;
    }
};

/**
 * Histogram Bucket
 * 
 * Bucket for histogram metrics.
 */
struct HistogramBucket {
    double upperBound;
    uint64_t count;
    
    HistogramBucket() : upperBound(0), count(0) {}
    HistogramBucket(double bound) : upperBound(bound), count(0) {}
};

/**
 * Metric Value
 * 
 * Holds the value of a metric.
 */
struct MetricValue {
    MetricType type;
    std::string name;
    std::string help;
    std::vector<MetricLabel> labels;
    
    // Counter/Gauge value
    std::atomic<double> value{0.0};
    
    // Histogram data
    std::vector<HistogramBucket> buckets;
    std::atomic<double> sum{0.0};
    std::atomic<uint64_t> count{0};
    
    MetricValue() : type(MetricType::COUNTER) {}
    MetricValue(MetricType t, const std::string& n, const std::string& h)
        : type(t), name(n), help(h) {}
};

/**
 * EVM Execution Metrics
 * 
 * Metrics specific to EVM execution.
 */
struct EVMExecutionMetrics {
    // Execution counts
    std::atomic<uint64_t> totalExecutions{0};
    std::atomic<uint64_t> successfulExecutions{0};
    std::atomic<uint64_t> failedExecutions{0};
    std::atomic<uint64_t> outOfGasExecutions{0};
    std::atomic<uint64_t> revertedExecutions{0};
    
    // Gas metrics
    std::atomic<uint64_t> totalGasUsed{0};
    std::atomic<uint64_t> totalGasLimit{0};
    std::atomic<uint64_t> freeGasUsed{0};
    std::atomic<uint64_t> subsidizedGasUsed{0};
    
    // Timing metrics (microseconds)
    std::atomic<uint64_t> totalExecutionTime{0};
    std::atomic<uint64_t> maxExecutionTime{0};
    std::atomic<uint64_t> minExecutionTime{UINT64_MAX};
    
    // Contract metrics
    std::atomic<uint64_t> contractDeployments{0};
    std::atomic<uint64_t> contractCalls{0};
    std::atomic<uint64_t> internalCalls{0};
    
    // Storage metrics
    std::atomic<uint64_t> storageReads{0};
    std::atomic<uint64_t> storageWrites{0};
    std::atomic<uint64_t> storageBytesWritten{0};
    
    // Opcode metrics
    std::map<uint8_t, std::atomic<uint64_t>> opcodeExecutions;
    
    void Reset() {
        totalExecutions = 0;
        successfulExecutions = 0;
        failedExecutions = 0;
        outOfGasExecutions = 0;
        revertedExecutions = 0;
        totalGasUsed = 0;
        totalGasLimit = 0;
        freeGasUsed = 0;
        subsidizedGasUsed = 0;
        totalExecutionTime = 0;
        maxExecutionTime = 0;
        minExecutionTime = UINT64_MAX;
        contractDeployments = 0;
        contractCalls = 0;
        internalCalls = 0;
        storageReads = 0;
        storageWrites = 0;
        storageBytesWritten = 0;
    }
};

/**
 * Trust Operation Metrics
 * 
 * Metrics for trust-aware operations.
 */
struct TrustOperationMetrics {
    // Trust context injection
    std::atomic<uint64_t> trustContextInjections{0};
    std::atomic<uint64_t> trustContextFailures{0};
    
    // Reputation queries
    std::atomic<uint64_t> reputationQueries{0};
    std::atomic<uint64_t> reputationCacheHits{0};
    std::atomic<uint64_t> reputationCacheMisses{0};
    
    // Trust-gated operations
    std::atomic<uint64_t> trustGatedOperations{0};
    std::atomic<uint64_t> trustGatedDenials{0};
    
    // Gas discounts
    std::atomic<uint64_t> gasDiscountsApplied{0};
    std::atomic<uint64_t> totalGasDiscountAmount{0};
    std::atomic<uint64_t> freeGasTransactions{0};
    
    // HAT v2 consensus
    std::atomic<uint64_t> validationRequests{0};
    std::atomic<uint64_t> validationSuccesses{0};
    std::atomic<uint64_t> validationFailures{0};
    std::atomic<uint64_t> validationTimeouts{0};
    std::atomic<uint64_t> daoDisputes{0};
    
    // Validator metrics
    std::atomic<uint64_t> validatorResponses{0};
    std::atomic<uint64_t> validatorAccepts{0};
    std::atomic<uint64_t> validatorRejects{0};
    std::atomic<uint64_t> validatorAbstains{0};
    
    // Fraud detection
    std::atomic<uint64_t> fraudAttemptsDetected{0};
    std::atomic<uint64_t> fraudRecordsCreated{0};
    
    void Reset() {
        trustContextInjections = 0;
        trustContextFailures = 0;
        reputationQueries = 0;
        reputationCacheHits = 0;
        reputationCacheMisses = 0;
        trustGatedOperations = 0;
        trustGatedDenials = 0;
        gasDiscountsApplied = 0;
        totalGasDiscountAmount = 0;
        freeGasTransactions = 0;
        validationRequests = 0;
        validationSuccesses = 0;
        validationFailures = 0;
        validationTimeouts = 0;
        daoDisputes = 0;
        validatorResponses = 0;
        validatorAccepts = 0;
        validatorRejects = 0;
        validatorAbstains = 0;
        fraudAttemptsDetected = 0;
        fraudRecordsCreated = 0;
    }
};

/**
 * Network Metrics
 * 
 * Metrics for network operations.
 */
struct NetworkMetrics {
    // P2P messages
    std::atomic<uint64_t> validationChallengesSent{0};
    std::atomic<uint64_t> validationChallengesReceived{0};
    std::atomic<uint64_t> validationResponsesSent{0};
    std::atomic<uint64_t> validationResponsesReceived{0};
    
    // Bandwidth
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> bytesReceived{0};
    
    // Rate limiting
    std::atomic<uint64_t> rateLimitedTransactions{0};
    std::atomic<uint64_t> rateLimitedRPCCalls{0};
    std::atomic<uint64_t> rateLimitedP2PMessages{0};
    
    void Reset() {
        validationChallengesSent = 0;
        validationChallengesReceived = 0;
        validationResponsesSent = 0;
        validationResponsesReceived = 0;
        bytesSent = 0;
        bytesReceived = 0;
        rateLimitedTransactions = 0;
        rateLimitedRPCCalls = 0;
        rateLimitedP2PMessages = 0;
    }
};


/**
 * Prometheus Metrics Exporter
 * 
 * Exports metrics in Prometheus format for monitoring.
 * Implements requirement 9.1 for monitoring and observability.
 */
class PrometheusMetricsExporter {
public:
    PrometheusMetricsExporter();
    ~PrometheusMetricsExporter();
    
    /**
     * Initialize the metrics exporter
     * 
     * @param port HTTP port for metrics endpoint (default: 9100)
     * @return true if initialization successful
     */
    bool Initialize(uint16_t port = 9100);
    
    /**
     * Shutdown the metrics exporter
     */
    void Shutdown();
    
    /**
     * Check if exporter is running
     */
    bool IsRunning() const { return m_running; }
    
    /**
     * Get metrics in Prometheus format
     * 
     * @return Prometheus-formatted metrics string
     */
    std::string GetPrometheusMetrics() const;
    
    /**
     * Get metrics as JSON
     * 
     * @return JSON-formatted metrics
     */
    std::string GetJSONMetrics() const;
    
    // ========== EVM Execution Metrics ==========
    
    /**
     * Record EVM execution
     */
    void RecordEVMExecution(bool success, uint64_t gasUsed, uint64_t gasLimit,
                           uint64_t executionTimeUs, bool outOfGas = false,
                           bool reverted = false);
    
    /**
     * Record contract deployment
     */
    void RecordContractDeployment(bool success, uint64_t gasUsed);
    
    /**
     * Record contract call
     */
    void RecordContractCall(bool success, uint64_t gasUsed, bool isInternal = false);
    
    /**
     * Record storage operation
     */
    void RecordStorageOperation(bool isWrite, uint64_t bytesAffected = 0);
    
    /**
     * Record opcode execution
     */
    void RecordOpcodeExecution(uint8_t opcode);
    
    // ========== Trust Operation Metrics ==========
    
    /**
     * Record trust context injection
     */
    void RecordTrustContextInjection(bool success);
    
    /**
     * Record reputation query
     */
    void RecordReputationQuery(bool cacheHit);
    
    /**
     * Record trust-gated operation
     */
    void RecordTrustGatedOperation(bool allowed);
    
    /**
     * Record gas discount
     */
    void RecordGasDiscount(uint64_t discountAmount, bool isFreeGas);
    
    /**
     * Record HAT v2 validation
     */
    void RecordValidation(bool success, bool timeout = false, bool disputed = false);
    
    /**
     * Record validator response
     */
    void RecordValidatorResponse(int vote); // 1=accept, -1=reject, 0=abstain
    
    /**
     * Record fraud detection
     */
    void RecordFraudDetection(bool recordCreated);
    
    // ========== Network Metrics ==========
    
    /**
     * Record validation challenge
     */
    void RecordValidationChallenge(bool sent);
    
    /**
     * Record validation response message
     */
    void RecordValidationResponseMessage(bool sent);
    
    /**
     * Record bandwidth usage
     */
    void RecordBandwidth(uint64_t bytes, bool sent);
    
    /**
     * Record rate limiting
     */
    void RecordRateLimiting(const std::string& type);
    
    // ========== Metric Access ==========
    
    /**
     * Get EVM execution metrics
     */
    const EVMExecutionMetrics& GetEVMMetrics() const { return m_evmMetrics; }
    
    /**
     * Get trust operation metrics
     */
    const TrustOperationMetrics& GetTrustMetrics() const { return m_trustMetrics; }
    
    /**
     * Get network metrics
     */
    const NetworkMetrics& GetNetworkMetrics() const { return m_networkMetrics; }
    
    /**
     * Reset all metrics
     */
    void ResetMetrics();
    
    /**
     * Get uptime in seconds
     */
    uint64_t GetUptimeSeconds() const;
    
private:
    mutable CCriticalSection m_cs;
    
    // Metrics storage
    EVMExecutionMetrics m_evmMetrics;
    TrustOperationMetrics m_trustMetrics;
    NetworkMetrics m_networkMetrics;
    
    // State
    std::atomic<bool> m_running{false};
    std::chrono::steady_clock::time_point m_startTime;
    uint16_t m_port;
    
    // Histogram buckets for execution time (microseconds)
    std::vector<double> m_executionTimeBuckets;
    std::map<double, std::atomic<uint64_t>> m_executionTimeHistogram;
    
    // Helper methods
    std::string FormatCounter(const std::string& name, const std::string& help,
                             uint64_t value, const std::vector<MetricLabel>& labels = {}) const;
    std::string FormatGauge(const std::string& name, const std::string& help,
                           double value, const std::vector<MetricLabel>& labels = {}) const;
    std::string FormatHistogram(const std::string& name, const std::string& help,
                               const std::map<double, std::atomic<uint64_t>>& buckets,
                               uint64_t count, double sum) const;
    std::string FormatLabels(const std::vector<MetricLabel>& labels) const;
};

/**
 * Trust Operation Logger
 * 
 * Structured logging for trust-aware operations.
 * Implements requirement 10.3 for logging trust operations.
 */
class TrustOperationLogger {
public:
    /**
     * Log Level
     */
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERR_LOG,
        CRITICAL
    };
    
    /**
     * Log Entry
     */
    struct LogEntry {
        int64_t timestamp;
        LogLevel level;
        std::string category;
        std::string message;
        std::map<std::string, std::string> context;
        uint160 address;
        uint256 txHash;
        int blockHeight;
        
        LogEntry() : timestamp(0), level(LogLevel::INFO), blockHeight(0) {}
    };
    
    TrustOperationLogger();
    ~TrustOperationLogger();
    
    /**
     * Initialize the logger
     * 
     * @param logPath Path to log file (empty for stdout only)
     * @param minLevel Minimum log level to record
     * @return true if initialization successful
     */
    bool Initialize(const std::string& logPath = "", LogLevel minLevel = LogLevel::INFO);
    
    /**
     * Shutdown the logger
     */
    void Shutdown();
    
    // ========== Trust Context Logging ==========
    
    /**
     * Log trust context injection
     */
    void LogTrustContextInjection(const uint160& caller, const uint160& contract,
                                  uint8_t callerReputation, bool success,
                                  const std::string& reason = "");
    
    /**
     * Log reputation query
     */
    void LogReputationQuery(const uint160& requester, const uint160& target,
                           uint8_t score, bool cacheHit);
    
    /**
     * Log trust-gated operation
     */
    void LogTrustGatedOperation(const uint160& caller, const std::string& operation,
                               uint8_t requiredReputation, uint8_t actualReputation,
                               bool allowed);
    
    // ========== Gas System Logging ==========
    
    /**
     * Log gas discount application
     */
    void LogGasDiscount(const uint160& address, uint64_t originalGas,
                       uint64_t discountedGas, uint8_t reputation);
    
    /**
     * Log free gas usage
     */
    void LogFreeGasUsage(const uint160& address, uint64_t gasUsed,
                        uint64_t allowanceRemaining);
    
    /**
     * Log gas subsidy
     */
    void LogGasSubsidy(const uint160& address, uint64_t subsidyAmount,
                      const std::string& reason);
    
    // ========== HAT v2 Consensus Logging ==========
    
    /**
     * Log validation request
     */
    void LogValidationRequest(const uint256& txHash, const uint160& sender,
                             uint8_t selfReportedScore,
                             const std::vector<uint160>& validators);
    
    /**
     * Log validator response
     */
    void LogValidatorResponse(const uint256& txHash, const uint160& validator,
                             int vote, double confidence, bool hasWoT,
                             uint8_t calculatedScore, uint8_t reportedScore);
    
    /**
     * Log consensus result
     */
    void LogConsensusResult(const uint256& txHash, bool consensusReached,
                           double acceptanceRate, int totalResponses);
    
    /**
     * Log DAO dispute
     */
    void LogDAODispute(const uint256& disputeId, const uint256& txHash,
                      const uint160& address, const std::string& reason);
    
    /**
     * Log DAO resolution
     */
    void LogDAOResolution(const uint256& disputeId, bool approved,
                         const std::string& resolution);
    
    // ========== Fraud Detection Logging ==========
    
    /**
     * Log fraud attempt
     */
    void LogFraudAttempt(const uint160& address, const uint256& txHash,
                        uint8_t claimedScore, uint8_t actualScore,
                        const std::string& details);
    
    /**
     * Log fraud record creation
     */
    void LogFraudRecordCreation(const uint160& address, const uint256& txHash,
                               int blockHeight);
    
    // ========== Security Event Logging ==========
    
    /**
     * Log security event
     */
    void LogSecurityEvent(LogLevel level, const std::string& category,
                         const std::string& message,
                         const std::map<std::string, std::string>& context = {});
    
    /**
     * Log anomaly detection
     */
    void LogAnomalyDetection(const uint160& address, const std::string& anomalyType,
                            double severity, const std::string& description);
    
    // ========== Log Access ==========
    
    /**
     * Get recent log entries
     */
    std::vector<LogEntry> GetRecentEntries(size_t count = 100) const;
    
    /**
     * Get entries by level
     */
    std::vector<LogEntry> GetEntriesByLevel(LogLevel level, size_t count = 100) const;
    
    /**
     * Get entries for address
     */
    std::vector<LogEntry> GetEntriesForAddress(const uint160& address, size_t count = 100) const;
    
    /**
     * Set minimum log level
     */
    void SetMinLevel(LogLevel level) { m_minLevel = level; }
    
    /**
     * Set current block height
     */
    void SetBlockHeight(int height) { m_currentBlockHeight = height; }
    
private:
    mutable CCriticalSection m_cs;
    
    // Configuration
    std::string m_logPath;
    LogLevel m_minLevel;
    int m_currentBlockHeight;
    
    // Log storage
    std::deque<LogEntry> m_recentEntries;
    size_t m_maxEntriesInMemory;
    
    // State
    std::atomic<bool> m_initialized{false};
    
    // Helper methods
    void AddEntry(const LogEntry& entry);
    void WriteToFile(const LogEntry& entry);
    void WriteToStdout(const LogEntry& entry);
    std::string FormatEntry(const LogEntry& entry) const;
    std::string LevelToString(LogLevel level) const;
    int64_t GetCurrentTimestamp() const;
};

/**
 * Global metrics exporter instance
 */
extern std::unique_ptr<PrometheusMetricsExporter> g_metricsExporter;

/**
 * Global trust operation logger instance
 */
extern std::unique_ptr<TrustOperationLogger> g_trustLogger;

/**
 * Initialize monitoring and observability
 */
bool InitializeMonitoring(uint16_t metricsPort = 9100, 
                         const std::string& logPath = "",
                         TrustOperationLogger::LogLevel minLevel = TrustOperationLogger::LogLevel::INFO);

/**
 * Shutdown monitoring and observability
 */
void ShutdownMonitoring();

} // namespace CVM

#endif // CASCOIN_CVM_METRICS_H
