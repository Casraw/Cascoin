// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_GRACEFUL_DEGRADATION_H
#define CASCOIN_CVM_GRACEFUL_DEGRADATION_H

#include <uint256.h>
#include <sync.h>
#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace CVM {

/**
 * Circuit Breaker State
 * 
 * States for the circuit breaker pattern.
 */
enum class CircuitState {
    CLOSED,         // Normal operation, requests pass through
    OPEN,           // Failure threshold exceeded, requests blocked
    HALF_OPEN       // Testing if service recovered
};

/**
 * Degradation Level
 * 
 * Levels of system degradation.
 */
enum class DegradationLevel {
    NORMAL,         // Full functionality
    REDUCED,        // Some features disabled
    MINIMAL,        // Only essential features
    EMERGENCY       // Emergency mode, minimal processing
};

/**
 * Subsystem Type
 * 
 * Types of subsystems that can be degraded.
 */
enum class SubsystemType {
    TRUST_CONTEXT,          // Trust context injection
    REPUTATION_QUERY,       // Reputation queries
    HAT_VALIDATION,         // HAT v2 consensus validation
    GAS_DISCOUNT,           // Gas discount calculation
    FREE_GAS,               // Free gas allowance
    CROSS_CHAIN_TRUST,      // Cross-chain trust verification
    VALIDATOR_SELECTION,    // Validator selection
    DAO_DISPUTE,            // DAO dispute resolution
    STORAGE_RENT,           // Storage rent calculation
    ANOMALY_DETECTION       // Anomaly detection
};

/**
 * Circuit Breaker Configuration
 */
struct CircuitBreakerConfig {
    uint32_t failureThreshold;      // Number of failures before opening
    uint32_t successThreshold;      // Number of successes to close from half-open
    uint32_t openDurationMs;        // How long to stay open before half-open
    uint32_t halfOpenMaxRequests;   // Max requests in half-open state
    double failureRateThreshold;    // Failure rate to trigger open (0.0-1.0)
    uint32_t windowSizeMs;          // Time window for failure rate calculation
    
    CircuitBreakerConfig()
        : failureThreshold(5)
        , successThreshold(3)
        , openDurationMs(30000)     // 30 seconds
        , halfOpenMaxRequests(3)
        , failureRateThreshold(0.5) // 50% failure rate
        , windowSizeMs(60000)       // 1 minute window
    {}
    
    static CircuitBreakerConfig Default() { return CircuitBreakerConfig(); }
    static CircuitBreakerConfig Aggressive() {
        CircuitBreakerConfig config;
        config.failureThreshold = 3;
        config.openDurationMs = 60000;
        config.failureRateThreshold = 0.3;
        return config;
    }
    static CircuitBreakerConfig Lenient() {
        CircuitBreakerConfig config;
        config.failureThreshold = 10;
        config.openDurationMs = 15000;
        config.failureRateThreshold = 0.7;
        return config;
    }
};

/**
 * Circuit Breaker Statistics
 */
struct CircuitBreakerStats {
    uint64_t totalRequests;
    uint64_t successfulRequests;
    uint64_t failedRequests;
    uint64_t rejectedRequests;      // Rejected due to open circuit
    uint64_t timeoutsCount;
    uint64_t stateTransitions;
    CircuitState currentState;
    int64_t lastStateChangeTime;
    int64_t lastFailureTime;
    int64_t lastSuccessTime;
    double currentFailureRate;
    
    CircuitBreakerStats()
        : totalRequests(0), successfulRequests(0), failedRequests(0)
        , rejectedRequests(0), timeoutsCount(0), stateTransitions(0)
        , currentState(CircuitState::CLOSED), lastStateChangeTime(0)
        , lastFailureTime(0), lastSuccessTime(0), currentFailureRate(0)
    {}
};

/**
 * Circuit Breaker
 * 
 * Implements the circuit breaker pattern for fault tolerance.
 */
class CircuitBreaker {
public:
    explicit CircuitBreaker(const std::string& name, 
                           const CircuitBreakerConfig& config = CircuitBreakerConfig::Default());
    ~CircuitBreaker();
    
    /**
     * Check if request should be allowed
     * 
     * @return true if request can proceed
     */
    bool AllowRequest();
    
    /**
     * Record a successful request
     */
    void RecordSuccess();
    
    /**
     * Record a failed request
     */
    void RecordFailure();
    
    /**
     * Record a timeout
     */
    void RecordTimeout();
    
    /**
     * Get current state
     */
    CircuitState GetState() const { return m_state; }
    
    /**
     * Get statistics
     */
    CircuitBreakerStats GetStats() const;
    
    /**
     * Force state transition (for testing/admin)
     */
    void ForceState(CircuitState state);
    
    /**
     * Reset the circuit breaker
     */
    void Reset();
    
    /**
     * Get name
     */
    const std::string& GetName() const { return m_name; }
    
private:
    std::string m_name;
    CircuitBreakerConfig m_config;
    mutable CCriticalSection m_cs;
    
    // State
    std::atomic<CircuitState> m_state{CircuitState::CLOSED};
    std::atomic<uint32_t> m_consecutiveFailures{0};
    std::atomic<uint32_t> m_consecutiveSuccesses{0};
    std::atomic<uint32_t> m_halfOpenRequests{0};
    
    // Timing
    std::chrono::steady_clock::time_point m_lastStateChange;
    std::chrono::steady_clock::time_point m_lastFailure;
    std::chrono::steady_clock::time_point m_lastSuccess;
    
    // Statistics
    std::atomic<uint64_t> m_totalRequests{0};
    std::atomic<uint64_t> m_successfulRequests{0};
    std::atomic<uint64_t> m_failedRequests{0};
    std::atomic<uint64_t> m_rejectedRequests{0};
    std::atomic<uint64_t> m_timeoutsCount{0};
    std::atomic<uint64_t> m_stateTransitions{0};
    
    // Sliding window for failure rate
    std::vector<std::pair<int64_t, bool>> m_requestHistory;
    
    // Helper methods
    void TransitionTo(CircuitState newState);
    bool ShouldTransitionToOpen();
    bool ShouldTransitionToHalfOpen();
    bool ShouldTransitionToClosed();
    double CalculateFailureRate() const;
    void CleanupOldHistory();
    int64_t GetCurrentTimeMs() const;
};

/**
 * Fallback Result
 * 
 * Result of a fallback operation.
 */
template<typename T>
struct FallbackResult {
    bool success;
    T value;
    bool usedFallback;
    std::string fallbackReason;
    
    FallbackResult() : success(false), usedFallback(false) {}
    FallbackResult(const T& v) : success(true), value(v), usedFallback(false) {}
    
    static FallbackResult<T> Success(const T& v) {
        FallbackResult<T> result;
        result.success = true;
        result.value = v;
        return result;
    }
    
    static FallbackResult<T> Fallback(const T& v, const std::string& reason) {
        FallbackResult<T> result;
        result.success = true;
        result.value = v;
        result.usedFallback = true;
        result.fallbackReason = reason;
        return result;
    }
    
    static FallbackResult<T> Failure(const std::string& reason) {
        FallbackResult<T> result;
        result.success = false;
        result.fallbackReason = reason;
        return result;
    }
};

/**
 * Subsystem Status
 */
struct SubsystemStatus {
    SubsystemType type;
    bool enabled;
    DegradationLevel level;
    CircuitState circuitState;
    uint64_t requestsProcessed;
    uint64_t requestsFailed;
    uint64_t fallbacksUsed;
    int64_t lastHealthCheck;
    std::string statusMessage;
    
    SubsystemStatus()
        : type(SubsystemType::TRUST_CONTEXT), enabled(true)
        , level(DegradationLevel::NORMAL), circuitState(CircuitState::CLOSED)
        , requestsProcessed(0), requestsFailed(0), fallbacksUsed(0)
        , lastHealthCheck(0)
    {}
};


/**
 * Graceful Degradation Manager
 * 
 * Manages graceful degradation of CVM subsystems.
 * Implements requirements 10.1 and 10.2 for fault tolerance.
 */
class GracefulDegradationManager {
public:
    GracefulDegradationManager();
    ~GracefulDegradationManager();
    
    /**
     * Initialize the degradation manager
     */
    bool Initialize();
    
    /**
     * Shutdown the degradation manager
     */
    void Shutdown();
    
    // ========== Circuit Breaker Management ==========
    
    /**
     * Get or create circuit breaker for a subsystem
     */
    CircuitBreaker& GetCircuitBreaker(SubsystemType subsystem);
    
    /**
     * Check if subsystem is available
     */
    bool IsSubsystemAvailable(SubsystemType subsystem);
    
    /**
     * Record subsystem success
     */
    void RecordSubsystemSuccess(SubsystemType subsystem);
    
    /**
     * Record subsystem failure
     */
    void RecordSubsystemFailure(SubsystemType subsystem);
    
    // ========== Trust System Fallbacks ==========
    
    /**
     * Get reputation with fallback
     * 
     * Falls back to cached value or default if trust system unavailable.
     */
    FallbackResult<uint8_t> GetReputationWithFallback(const uint160& address,
                                                      uint8_t defaultValue = 50);
    
    /**
     * Inject trust context with fallback
     * 
     * Falls back to default trust context if injection fails.
     */
    FallbackResult<bool> InjectTrustContextWithFallback(const uint160& caller,
                                                        const uint160& contract);
    
    /**
     * Validate with HAT v2 with fallback
     * 
     * Falls back to local validation if consensus unavailable.
     */
    FallbackResult<bool> ValidateWithHATv2Fallback(const uint256& txHash,
                                                   const uint160& sender,
                                                   uint8_t selfReportedScore);
    
    // ========== Gas System Fallbacks ==========
    
    /**
     * Calculate gas discount with fallback
     * 
     * Falls back to no discount if calculation fails.
     */
    FallbackResult<uint64_t> CalculateGasDiscountWithFallback(uint8_t reputation,
                                                              uint64_t baseGas);
    
    /**
     * Check free gas eligibility with fallback
     * 
     * Falls back to not eligible if check fails.
     */
    FallbackResult<bool> CheckFreeGasEligibilityWithFallback(const uint160& address,
                                                             uint8_t reputation);
    
    // ========== Resource Exhaustion Protection ==========
    
    /**
     * Check memory usage and trigger degradation if needed
     */
    void CheckMemoryUsage();
    
    /**
     * Check CPU usage and trigger degradation if needed
     */
    void CheckCPUUsage();
    
    /**
     * Check storage usage and trigger degradation if needed
     */
    void CheckStorageUsage();
    
    /**
     * Set resource thresholds
     */
    void SetResourceThresholds(double memoryThreshold, double cpuThreshold,
                              double storageThreshold);
    
    // ========== Degradation Level Management ==========
    
    /**
     * Get current degradation level
     */
    DegradationLevel GetDegradationLevel() const { return m_degradationLevel; }
    
    /**
     * Set degradation level
     */
    void SetDegradationLevel(DegradationLevel level);
    
    /**
     * Get subsystem status
     */
    SubsystemStatus GetSubsystemStatus(SubsystemType subsystem);
    
    /**
     * Get all subsystem statuses
     */
    std::vector<SubsystemStatus> GetAllSubsystemStatuses();
    
    /**
     * Enable/disable subsystem
     */
    void SetSubsystemEnabled(SubsystemType subsystem, bool enabled);
    
    /**
     * Check if subsystem is enabled
     */
    bool IsSubsystemEnabled(SubsystemType subsystem);
    
    // ========== Health Checks ==========
    
    /**
     * Run health check on all subsystems
     */
    void RunHealthChecks();
    
    /**
     * Run health check on specific subsystem
     */
    bool RunHealthCheck(SubsystemType subsystem);
    
    /**
     * Get overall system health
     */
    double GetSystemHealth();
    
    // ========== Emergency Mode ==========
    
    /**
     * Enter emergency mode
     */
    void EnterEmergencyMode(const std::string& reason);
    
    /**
     * Exit emergency mode
     */
    void ExitEmergencyMode();
    
    /**
     * Check if in emergency mode
     */
    bool IsInEmergencyMode() const { return m_emergencyMode; }
    
    /**
     * Get emergency mode reason
     */
    std::string GetEmergencyModeReason() const { return m_emergencyReason; }
    
    // ========== Configuration ==========
    
    /**
     * Set circuit breaker config for subsystem
     */
    void SetCircuitBreakerConfig(SubsystemType subsystem, const CircuitBreakerConfig& config);
    
    /**
     * Set fallback cache TTL
     */
    void SetFallbackCacheTTL(uint32_t ttlSeconds) { m_fallbackCacheTTL = ttlSeconds; }
    
    /**
     * Set health check interval
     */
    void SetHealthCheckInterval(uint32_t intervalSeconds) { m_healthCheckInterval = intervalSeconds; }
    
private:
    mutable CCriticalSection m_cs;
    
    // Circuit breakers for each subsystem
    std::map<SubsystemType, std::unique_ptr<CircuitBreaker>> m_circuitBreakers;
    
    // Subsystem status
    std::map<SubsystemType, SubsystemStatus> m_subsystemStatus;
    
    // Fallback cache for reputation scores
    std::map<uint160, std::pair<uint8_t, int64_t>> m_reputationCache;
    
    // State
    std::atomic<DegradationLevel> m_degradationLevel{DegradationLevel::NORMAL};
    std::atomic<bool> m_emergencyMode{false};
    std::string m_emergencyReason;
    std::atomic<bool> m_initialized{false};
    
    // Resource thresholds
    double m_memoryThreshold;
    double m_cpuThreshold;
    double m_storageThreshold;
    
    // Configuration
    uint32_t m_fallbackCacheTTL;        // Seconds
    uint32_t m_healthCheckInterval;     // Seconds
    int64_t m_lastHealthCheck;
    
    // Helper methods
    void InitializeCircuitBreakers();
    void InitializeSubsystemStatus();
    std::string SubsystemTypeToString(SubsystemType type) const;
    int64_t GetCurrentTimeMs() const;
    void UpdateDegradationLevel();
    void CacheReputation(const uint160& address, uint8_t score);
    bool GetCachedReputation(const uint160& address, uint8_t& score);
    void CleanupExpiredCache();
};

/**
 * Global graceful degradation manager instance
 */
extern std::unique_ptr<GracefulDegradationManager> g_degradationManager;

/**
 * Initialize graceful degradation
 */
bool InitializeGracefulDegradation();

/**
 * Shutdown graceful degradation
 */
void ShutdownGracefulDegradation();

/**
 * Convenience macros for circuit breaker usage
 */
#define CVM_CIRCUIT_BREAKER_CHECK(subsystem) \
    if (g_degradationManager && !g_degradationManager->IsSubsystemAvailable(subsystem)) { \
        return false; \
    }

#define CVM_CIRCUIT_BREAKER_SUCCESS(subsystem) \
    if (g_degradationManager) { \
        g_degradationManager->RecordSubsystemSuccess(subsystem); \
    }

#define CVM_CIRCUIT_BREAKER_FAILURE(subsystem) \
    if (g_degradationManager) { \
        g_degradationManager->RecordSubsystemFailure(subsystem); \
    }

} // namespace CVM

#endif // CASCOIN_CVM_GRACEFUL_DEGRADATION_H
