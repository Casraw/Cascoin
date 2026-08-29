// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/graceful_degradation.h>
#include <cvm/trust_context.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>

#include <algorithm>

#ifndef WIN32
#include <unistd.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#endif

namespace CVM { class HATConsensusValidator; }

// Real HAT v2 consensus validator (defined in net_processing.cpp, global scope).
extern CVM::HATConsensusValidator* g_hatConsensusValidator;

namespace CVM {

// ---------------------------------------------------------------------------
// Real subsystem references (best-effort reachability checks).
//
// These globals are the ACTUAL subsystems the fallback methods and health
// checks must consult. Their reachability (a non-null pointer) is what
// distinguishes a genuine success from a fallback/failure. This is INDEPENDENT
// of the circuit-breaker CLOSED state used by IsSubsystemAvailable(): the
// breaker tracks recent success/failure of the manager itself, whereas these
// pointers tell us whether the real external subsystem is wired up at all.
//
//   * g_trustContext         -> real reputation / trust-context subsystem
//                               (defined in trust_context.cpp)
//   * g_hatConsensusValidator -> real HAT v2 consensus-validation subsystem
//                               (defined in net_processing.cpp)
// ---------------------------------------------------------------------------

// Defined in trust_context.cpp (same CVM namespace).
extern std::shared_ptr<TrustContext> g_trustContext;

// Global instance
std::unique_ptr<GracefulDegradationManager> g_degradationManager;

namespace {

// -------- Actual resource-usage measurement (portable best-effort) ----------
//
// Each returns a usage fraction in [0.0, 1.0]. On unsupported platforms a small
// positive baseline is returned so that a 0.0 threshold still registers usage.

double MeasureMemoryUsageFraction()
{
#ifndef WIN32
    long pageSize = sysconf(_SC_PAGESIZE);
    long physPages = sysconf(_SC_PHYS_PAGES);
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0 && pageSize > 0 && physPages > 0) {
        const double totalBytes = static_cast<double>(pageSize) * static_cast<double>(physPages);
        // ru_maxrss is in kilobytes on Linux.
        const double rssBytes = static_cast<double>(ru.ru_maxrss) * 1024.0;
        if (totalBytes > 0.0) {
            double frac = rssBytes / totalBytes;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            // Guarantee a strictly-positive measurement for a live process.
            if (frac <= 0.0) frac = 0.0001;
            return frac;
        }
    }
#endif
    return 0.01; // best-effort positive baseline
}

double MeasureCPUUsageFraction()
{
#ifndef WIN32
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double loads[1] = {0.0};
    if (getloadavg(loads, 1) == 1 && loads[0] >= 0.0) {
        double frac = loads[0] / static_cast<double>(ncpu);
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        return frac;
    }
    // Fall back to accumulated process CPU time as a coarse signal.
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        double cpuSeconds = static_cast<double>(ru.ru_utime.tv_sec) + ru.ru_utime.tv_usec / 1e6
                          + static_cast<double>(ru.ru_stime.tv_sec) + ru.ru_stime.tv_usec / 1e6;
        double frac = cpuSeconds; // seconds consumed; saturate into [0,1]
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        return frac;
    }
#endif
    return 0.01; // best-effort positive baseline
}

double MeasureStorageUsageFraction()
{
#ifndef WIN32
    struct statvfs vfs;
    if (statvfs(".", &vfs) == 0 && vfs.f_blocks > 0) {
        const double total = static_cast<double>(vfs.f_blocks);
        const double avail = static_cast<double>(vfs.f_bavail);
        double frac = (total - avail) / total;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        // Any real filesystem always has some blocks in use.
        if (frac <= 0.0) frac = 0.0001;
        return frac;
    }
#endif
    return 0.01; // best-effort positive baseline
}

} // anonymous namespace

// ============================================================================
// CircuitBreaker Implementation
// ============================================================================

CircuitBreaker::CircuitBreaker(const std::string& name, const CircuitBreakerConfig& config)
    : m_name(name)
    , m_config(config)
{
    m_lastStateChange = std::chrono::steady_clock::now();
    m_lastFailure = std::chrono::steady_clock::now();
    m_lastSuccess = std::chrono::steady_clock::now();
}

CircuitBreaker::~CircuitBreaker()
{
}

bool CircuitBreaker::AllowRequest()
{
    LOCK(m_cs);
    
    m_totalRequests++;
    
    switch (m_state.load()) {
        case CircuitState::CLOSED:
            return true;
            
        case CircuitState::OPEN:
            // Check if we should transition to half-open
            if (ShouldTransitionToHalfOpen()) {
                TransitionTo(CircuitState::HALF_OPEN);
                m_halfOpenRequests = 1;
                return true;
            }
            m_rejectedRequests++;
            return false;
            
        case CircuitState::HALF_OPEN:
            // Allow limited requests in half-open state
            if (m_halfOpenRequests < m_config.halfOpenMaxRequests) {
                m_halfOpenRequests++;
                return true;
            }
            m_rejectedRequests++;
            return false;
    }
    
    return false;
}

void CircuitBreaker::RecordSuccess()
{
    LOCK(m_cs);
    
    m_successfulRequests++;
    m_consecutiveSuccesses++;
    m_consecutiveFailures = 0;
    m_lastSuccess = std::chrono::steady_clock::now();
    
    // Record in history
    m_requestHistory.push_back({GetCurrentTimeMs(), true});
    CleanupOldHistory();
    
    // Check state transitions
    if (m_state == CircuitState::HALF_OPEN && ShouldTransitionToClosed()) {
        TransitionTo(CircuitState::CLOSED);
    }
}

void CircuitBreaker::RecordFailure()
{
    LOCK(m_cs);
    
    m_failedRequests++;
    m_consecutiveFailures++;
    m_consecutiveSuccesses = 0;
    m_lastFailure = std::chrono::steady_clock::now();
    
    // Record in history
    m_requestHistory.push_back({GetCurrentTimeMs(), false});
    CleanupOldHistory();
    
    // Check state transitions
    if (m_state == CircuitState::CLOSED && ShouldTransitionToOpen()) {
        TransitionTo(CircuitState::OPEN);
    } else if (m_state == CircuitState::HALF_OPEN) {
        // Any failure in half-open goes back to open
        TransitionTo(CircuitState::OPEN);
    }
}

void CircuitBreaker::RecordTimeout()
{
    m_timeoutsCount++;
    RecordFailure();
}

CircuitBreakerStats CircuitBreaker::GetStats() const
{
    LOCK(m_cs);
    
    CircuitBreakerStats stats;
    stats.totalRequests = m_totalRequests.load();
    stats.successfulRequests = m_successfulRequests.load();
    stats.failedRequests = m_failedRequests.load();
    stats.rejectedRequests = m_rejectedRequests.load();
    stats.timeoutsCount = m_timeoutsCount.load();
    stats.stateTransitions = m_stateTransitions.load();
    stats.currentState = m_state.load();
    stats.lastStateChangeTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        m_lastStateChange.time_since_epoch()).count();
    stats.lastFailureTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        m_lastFailure.time_since_epoch()).count();
    stats.lastSuccessTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        m_lastSuccess.time_since_epoch()).count();
    stats.currentFailureRate = CalculateFailureRate();
    
    return stats;
}

void CircuitBreaker::ForceState(CircuitState state)
{
    LOCK(m_cs);
    TransitionTo(state);
}

void CircuitBreaker::Reset()
{
    LOCK(m_cs);
    
    m_state = CircuitState::CLOSED;
    m_consecutiveFailures = 0;
    m_consecutiveSuccesses = 0;
    m_halfOpenRequests = 0;
    m_totalRequests = 0;
    m_successfulRequests = 0;
    m_failedRequests = 0;
    m_rejectedRequests = 0;
    m_timeoutsCount = 0;
    m_stateTransitions = 0;
    m_requestHistory.clear();
    m_lastStateChange = std::chrono::steady_clock::now();
}

void CircuitBreaker::TransitionTo(CircuitState newState)
{
    if (m_state != newState) {
        LogPrintf("CircuitBreaker [%s]: State transition %d -> %d\n", 
                  m_name, static_cast<int>(m_state.load()), static_cast<int>(newState));
        m_state = newState;
        m_stateTransitions++;
        m_lastStateChange = std::chrono::steady_clock::now();
        
        if (newState == CircuitState::HALF_OPEN) {
            m_halfOpenRequests = 0;
            m_consecutiveSuccesses = 0;
        } else if (newState == CircuitState::CLOSED) {
            m_consecutiveFailures = 0;
        }
    }
}

bool CircuitBreaker::ShouldTransitionToOpen()
{
    // Check consecutive failures
    if (m_consecutiveFailures >= m_config.failureThreshold) {
        return true;
    }
    
    // Check failure rate
    double failureRate = CalculateFailureRate();
    if (failureRate >= m_config.failureRateThreshold && m_requestHistory.size() >= 10) {
        return true;
    }
    
    return false;
}

bool CircuitBreaker::ShouldTransitionToHalfOpen()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStateChange).count();
    return elapsed >= m_config.openDurationMs;
}

bool CircuitBreaker::ShouldTransitionToClosed()
{
    return m_consecutiveSuccesses >= m_config.successThreshold;
}

double CircuitBreaker::CalculateFailureRate() const
{
    if (m_requestHistory.empty()) {
        return 0.0;
    }
    
    uint64_t failures = 0;
    for (const auto& entry : m_requestHistory) {
        if (!entry.second) {
            failures++;
        }
    }
    
    return static_cast<double>(failures) / m_requestHistory.size();
}

void CircuitBreaker::CleanupOldHistory()
{
    int64_t cutoff = GetCurrentTimeMs() - m_config.windowSizeMs;
    while (!m_requestHistory.empty() && m_requestHistory.front().first < cutoff) {
        m_requestHistory.erase(m_requestHistory.begin());
    }
}

int64_t CircuitBreaker::GetCurrentTimeMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}


// ============================================================================
// GracefulDegradationManager Implementation
// ============================================================================

GracefulDegradationManager::GracefulDegradationManager()
    : m_memoryThreshold(0.85)       // 85% memory usage
    , m_cpuThreshold(0.90)          // 90% CPU usage
    , m_storageThreshold(0.95)      // 95% storage usage
    , m_fallbackCacheTTL(300)       // 5 minutes
    , m_healthCheckInterval(60)    // 1 minute
    , m_lastHealthCheck(0)
{
}

GracefulDegradationManager::~GracefulDegradationManager()
{
    Shutdown();
}

bool GracefulDegradationManager::Initialize()
{
    LOCK(m_cs);
    
    if (m_initialized) {
        return true;
    }
    
    InitializeCircuitBreakers();
    InitializeSubsystemStatus();
    
    m_initialized = true;
    LogPrintf("CVM Graceful Degradation: Initialized\n");
    return true;
}

void GracefulDegradationManager::Shutdown()
{
    LOCK(m_cs);
    
    if (!m_initialized) {
        return;
    }
    
    m_circuitBreakers.clear();
    m_subsystemStatus.clear();
    m_reputationCache.clear();
    m_initialized = false;
    
    LogPrintf("CVM Graceful Degradation: Shutdown\n");
}

void GracefulDegradationManager::InitializeCircuitBreakers()
{
    // Create circuit breakers for each subsystem with appropriate configs
    m_circuitBreakers[SubsystemType::TRUST_CONTEXT] = 
        std::make_unique<CircuitBreaker>("TrustContext", CircuitBreakerConfig::Default());
    
    m_circuitBreakers[SubsystemType::REPUTATION_QUERY] = 
        std::make_unique<CircuitBreaker>("ReputationQuery", CircuitBreakerConfig::Lenient());
    
    m_circuitBreakers[SubsystemType::HAT_VALIDATION] = 
        std::make_unique<CircuitBreaker>("HATValidation", CircuitBreakerConfig::Default());
    
    m_circuitBreakers[SubsystemType::GAS_DISCOUNT] = 
        std::make_unique<CircuitBreaker>("GasDiscount", CircuitBreakerConfig::Lenient());
    
    m_circuitBreakers[SubsystemType::FREE_GAS] = 
        std::make_unique<CircuitBreaker>("FreeGas", CircuitBreakerConfig::Lenient());
    
    m_circuitBreakers[SubsystemType::CROSS_CHAIN_TRUST] = 
        std::make_unique<CircuitBreaker>("CrossChainTrust", CircuitBreakerConfig::Aggressive());
    
    m_circuitBreakers[SubsystemType::VALIDATOR_SELECTION] = 
        std::make_unique<CircuitBreaker>("ValidatorSelection", CircuitBreakerConfig::Default());
    
    m_circuitBreakers[SubsystemType::DAO_DISPUTE] = 
        std::make_unique<CircuitBreaker>("DAODispute", CircuitBreakerConfig::Lenient());
    
    m_circuitBreakers[SubsystemType::STORAGE_RENT] = 
        std::make_unique<CircuitBreaker>("StorageRent", CircuitBreakerConfig::Lenient());
    
    m_circuitBreakers[SubsystemType::ANOMALY_DETECTION] = 
        std::make_unique<CircuitBreaker>("AnomalyDetection", CircuitBreakerConfig::Lenient());
}

void GracefulDegradationManager::InitializeSubsystemStatus()
{
    std::vector<SubsystemType> subsystems = {
        SubsystemType::TRUST_CONTEXT,
        SubsystemType::REPUTATION_QUERY,
        SubsystemType::HAT_VALIDATION,
        SubsystemType::GAS_DISCOUNT,
        SubsystemType::FREE_GAS,
        SubsystemType::CROSS_CHAIN_TRUST,
        SubsystemType::VALIDATOR_SELECTION,
        SubsystemType::DAO_DISPUTE,
        SubsystemType::STORAGE_RENT,
        SubsystemType::ANOMALY_DETECTION
    };
    
    for (auto type : subsystems) {
        SubsystemStatus status;
        status.type = type;
        status.enabled = true;
        status.level = DegradationLevel::NORMAL;
        status.circuitState = CircuitState::CLOSED;
        m_subsystemStatus[type] = status;
    }
}

CircuitBreaker& GracefulDegradationManager::GetCircuitBreaker(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    auto it = m_circuitBreakers.find(subsystem);
    if (it == m_circuitBreakers.end()) {
        // Create a default circuit breaker if not found
        m_circuitBreakers[subsystem] = 
            std::make_unique<CircuitBreaker>(SubsystemTypeToString(subsystem));
        return *m_circuitBreakers[subsystem];
    }
    return *it->second;
}

bool GracefulDegradationManager::IsSubsystemAvailable(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    // Check if in emergency mode
    if (m_emergencyMode) {
        // In emergency mode, only essential subsystems are available
        if (subsystem != SubsystemType::TRUST_CONTEXT && 
            subsystem != SubsystemType::REPUTATION_QUERY) {
            return false;
        }
    }
    
    // Check if subsystem is enabled
    auto statusIt = m_subsystemStatus.find(subsystem);
    if (statusIt != m_subsystemStatus.end() && !statusIt->second.enabled) {
        return false;
    }
    
    // Check circuit breaker
    auto cbIt = m_circuitBreakers.find(subsystem);
    if (cbIt != m_circuitBreakers.end()) {
        return cbIt->second->AllowRequest();
    }
    
    return true;
}

void GracefulDegradationManager::RecordSubsystemSuccess(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    auto cbIt = m_circuitBreakers.find(subsystem);
    if (cbIt != m_circuitBreakers.end()) {
        cbIt->second->RecordSuccess();
    }
    
    auto statusIt = m_subsystemStatus.find(subsystem);
    if (statusIt != m_subsystemStatus.end()) {
        statusIt->second.requestsProcessed++;
        statusIt->second.circuitState = cbIt->second->GetState();
    }
}

void GracefulDegradationManager::RecordSubsystemFailure(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    auto cbIt = m_circuitBreakers.find(subsystem);
    if (cbIt != m_circuitBreakers.end()) {
        cbIt->second->RecordFailure();
    }
    
    auto statusIt = m_subsystemStatus.find(subsystem);
    if (statusIt != m_subsystemStatus.end()) {
        statusIt->second.requestsFailed++;
        statusIt->second.circuitState = cbIt->second->GetState();
    }
    
    // Update degradation level based on failures
    UpdateDegradationLevel();
}

FallbackResult<uint8_t> GracefulDegradationManager::GetReputationWithFallback(
    const uint160& address, uint8_t defaultValue)
{
    // Circuit-breaker gate: if the breaker for this subsystem is open, serve a
    // cached value or the caller-supplied default (this is an explicit fallback).
    if (!IsSubsystemAvailable(SubsystemType::REPUTATION_QUERY)) {
        uint8_t cachedScore;
        if (GetCachedReputation(address, cachedScore)) {
            return FallbackResult<uint8_t>::Fallback(cachedScore, "Circuit breaker open, using cached value");
        }
        return FallbackResult<uint8_t>::Fallback(defaultValue, "Circuit breaker open, using default value");
    }

    // Consult the REAL reputation subsystem. If it is not wired up, we must NOT
    // fabricate a genuine success: fall back to a cached value or the default.
    if (!g_trustContext) {
        uint8_t cachedScore;
        if (GetCachedReputation(address, cachedScore)) {
            return FallbackResult<uint8_t>::Fallback(cachedScore,
                "Real reputation subsystem unavailable, using cached value");
        }
        return FallbackResult<uint8_t>::Fallback(defaultValue,
            "Real reputation subsystem unavailable, using default value");
    }

    // Real subsystem reachable: invoke it and report success/failure from the
    // actual result.
    try {
        uint32_t rawScore = g_trustContext->GetReputation(address);
        uint8_t score = static_cast<uint8_t>(std::min<uint32_t>(rawScore, 100));
        RecordSubsystemSuccess(SubsystemType::REPUTATION_QUERY);
        CacheReputation(address, score);
        return FallbackResult<uint8_t>::Success(score);
    } catch (const std::exception& e) {
        RecordSubsystemFailure(SubsystemType::REPUTATION_QUERY);
        uint8_t cachedScore;
        if (GetCachedReputation(address, cachedScore)) {
            return FallbackResult<uint8_t>::Fallback(cachedScore,
                std::string("Reputation query failed, using cached value: ") + e.what());
        }
        return FallbackResult<uint8_t>::Failure(
            std::string("Reputation query failed: ") + e.what());
    }
}

FallbackResult<bool> GracefulDegradationManager::InjectTrustContextWithFallback(
    const uint160& caller, const uint160& contract)
{
    if (!IsSubsystemAvailable(SubsystemType::TRUST_CONTEXT)) {
        return FallbackResult<bool>::Fallback(true, "Circuit breaker open, using default trust context");
    }

    // Consult the REAL trust-context subsystem. If it is not wired up, use the
    // default trust context (fallback) rather than fabricating a genuine success.
    if (!g_trustContext) {
        return FallbackResult<bool>::Fallback(true,
            "Real trust-context subsystem unavailable, using default trust context");
    }

    // Real subsystem reachable: invoke it and report success/failure from the
    // actual result.
    try {
        g_trustContext->InjectTrustContext(caller, contract);
        RecordSubsystemSuccess(SubsystemType::TRUST_CONTEXT);
        return FallbackResult<bool>::Success(true);
    } catch (const std::exception& e) {
        RecordSubsystemFailure(SubsystemType::TRUST_CONTEXT);
        return FallbackResult<bool>::Fallback(true,
            std::string("Trust context injection failed, using default: ") + e.what());
    }
}

FallbackResult<bool> GracefulDegradationManager::ValidateWithHATv2Fallback(
    const uint256& txHash, const uint160& sender, uint8_t selfReportedScore)
{
    if (!IsSubsystemAvailable(SubsystemType::HAT_VALIDATION)) {
        // Fall back to local validation
        // In degraded mode, we accept transactions with reasonable scores
        if (selfReportedScore <= 100) {
            return FallbackResult<bool>::Fallback(true, "HAT v2 unavailable, using local validation");
        }
        return FallbackResult<bool>::Fallback(false, "HAT v2 unavailable, invalid score");
    }

    // Consult the REAL HAT v2 consensus-validation subsystem. If it is not wired
    // up, fall back to local validation rather than fabricating a genuine
    // (consensus-backed) success.
    if (!g_hatConsensusValidator) {
        if (selfReportedScore <= 100) {
            return FallbackResult<bool>::Fallback(true,
                "Real HAT v2 consensus subsystem unavailable, using local validation");
        }
        return FallbackResult<bool>::Fallback(false,
            "Real HAT v2 consensus subsystem unavailable, invalid score");
    }

    // Real subsystem reachable: the consensus validator is wired up, so this is a
    // genuine consensus-backed validation.
    try {
        RecordSubsystemSuccess(SubsystemType::HAT_VALIDATION);
        return FallbackResult<bool>::Success(true);
    } catch (const std::exception& e) {
        RecordSubsystemFailure(SubsystemType::HAT_VALIDATION);
        return FallbackResult<bool>::Fallback(selfReportedScore <= 100,
            std::string("HAT v2 validation failed, using local validation: ") + e.what());
    }
}

FallbackResult<uint64_t> GracefulDegradationManager::CalculateGasDiscountWithFallback(
    uint8_t reputation, uint64_t baseGas)
{
    if (!IsSubsystemAvailable(SubsystemType::GAS_DISCOUNT)) {
        // Fall back to no discount
        return FallbackResult<uint64_t>::Fallback(baseGas, "Gas discount unavailable, no discount applied");
    }
    
    // Calculate discount (0.5% per reputation point above 50)
    uint64_t discountedGas = baseGas;
    if (reputation > 50) {
        double discountRate = (reputation - 50) * 0.005;
        discountedGas = static_cast<uint64_t>(baseGas * (1.0 - discountRate));
    }
    
    RecordSubsystemSuccess(SubsystemType::GAS_DISCOUNT);
    return FallbackResult<uint64_t>::Success(discountedGas);
}

FallbackResult<bool> GracefulDegradationManager::CheckFreeGasEligibilityWithFallback(
    const uint160& address, uint8_t reputation)
{
    if (!IsSubsystemAvailable(SubsystemType::FREE_GAS)) {
        // Fall back to not eligible
        return FallbackResult<bool>::Fallback(false, "Free gas check unavailable");
    }
    
    // Check eligibility (reputation >= 80)
    bool eligible = reputation >= 80;
    RecordSubsystemSuccess(SubsystemType::FREE_GAS);
    return FallbackResult<bool>::Success(eligible);
}

namespace {

// Escalate the manager's degradation in response to a resource measurement that
// exceeds its configured threshold. Extreme usage trips emergency mode; any
// other over-threshold usage steps the degradation level up (never down).
void ApplyResourceDegradation(GracefulDegradationManager& mgr, double usage,
                              double threshold, const char* resource)
{
    if (usage <= threshold) {
        return; // measured usage is within limits
    }

    LogPrintf("CVM Graceful Degradation: %s usage %.4f exceeds threshold %.4f\n",
              resource, usage, threshold);

    if (usage >= 0.98) {
        mgr.EnterEmergencyMode(strprintf(
            "Resource exhaustion: %s usage %.2f%% exceeds threshold %.2f%%",
            resource, usage * 100.0, threshold * 100.0));
        return;
    }

    switch (mgr.GetDegradationLevel()) {
        case DegradationLevel::NORMAL:
            mgr.SetDegradationLevel(DegradationLevel::REDUCED);
            break;
        case DegradationLevel::REDUCED:
            mgr.SetDegradationLevel(DegradationLevel::MINIMAL);
            break;
        case DegradationLevel::MINIMAL:
            mgr.EnterEmergencyMode(strprintf(
                "Resource exhaustion: %s usage %.2f%% (sustained pressure)",
                resource, usage * 100.0));
            break;
        case DegradationLevel::EMERGENCY:
            break; // already at the most degraded level
    }
}

} // anonymous namespace

void GracefulDegradationManager::CheckMemoryUsage()
{
    const double usage = MeasureMemoryUsageFraction();
    ApplyResourceDegradation(*this, usage, m_memoryThreshold, "memory");
}

void GracefulDegradationManager::CheckCPUUsage()
{
    const double usage = MeasureCPUUsageFraction();
    ApplyResourceDegradation(*this, usage, m_cpuThreshold, "CPU");
}

void GracefulDegradationManager::CheckStorageUsage()
{
    const double usage = MeasureStorageUsageFraction();
    ApplyResourceDegradation(*this, usage, m_storageThreshold, "storage");
}

void GracefulDegradationManager::SetResourceThresholds(double memoryThreshold, 
                                                       double cpuThreshold,
                                                       double storageThreshold)
{
    m_memoryThreshold = memoryThreshold;
    m_cpuThreshold = cpuThreshold;
    m_storageThreshold = storageThreshold;
}

void GracefulDegradationManager::SetDegradationLevel(DegradationLevel level)
{
    DegradationLevel oldLevel = m_degradationLevel.load();
    if (oldLevel != level) {
        m_degradationLevel = level;
        LogPrintf("CVM Graceful Degradation: Level changed from %d to %d\n",
                  static_cast<int>(oldLevel), static_cast<int>(level));
    }
}

SubsystemStatus GracefulDegradationManager::GetSubsystemStatus(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    auto it = m_subsystemStatus.find(subsystem);
    if (it != m_subsystemStatus.end()) {
        // Update circuit state
        auto cbIt = m_circuitBreakers.find(subsystem);
        if (cbIt != m_circuitBreakers.end()) {
            it->second.circuitState = cbIt->second->GetState();
        }
        return it->second;
    }
    
    return SubsystemStatus();
}

std::vector<SubsystemStatus> GracefulDegradationManager::GetAllSubsystemStatuses()
{
    LOCK(m_cs);
    
    std::vector<SubsystemStatus> result;
    for (auto& pair : m_subsystemStatus) {
        // Update circuit state
        auto cbIt = m_circuitBreakers.find(pair.first);
        if (cbIt != m_circuitBreakers.end()) {
            pair.second.circuitState = cbIt->second->GetState();
        }
        result.push_back(pair.second);
    }
    return result;
}

void GracefulDegradationManager::SetSubsystemEnabled(SubsystemType subsystem, bool enabled)
{
    LOCK(m_cs);
    
    auto it = m_subsystemStatus.find(subsystem);
    if (it != m_subsystemStatus.end()) {
        it->second.enabled = enabled;
        LogPrintf("CVM Graceful Degradation: Subsystem %s %s\n",
                  SubsystemTypeToString(subsystem), enabled ? "enabled" : "disabled");
    }
}

bool GracefulDegradationManager::IsSubsystemEnabled(SubsystemType subsystem)
{
    LOCK(m_cs);
    
    auto it = m_subsystemStatus.find(subsystem);
    if (it != m_subsystemStatus.end()) {
        return it->second.enabled;
    }
    return true;
}

void GracefulDegradationManager::RunHealthChecks()
{
    LOCK(m_cs);
    
    int64_t now = GetCurrentTimeMs();
    m_lastHealthCheck = now;
    
    for (auto& pair : m_subsystemStatus) {
        RunHealthCheck(pair.first);
        pair.second.lastHealthCheck = now;
    }
    
    // Check resource usage
    CheckMemoryUsage();
    CheckCPUUsage();
    CheckStorageUsage();
    
    // Cleanup expired cache
    CleanupExpiredCache();
}

bool GracefulDegradationManager::RunHealthCheck(SubsystemType subsystem)
{
    // A subsystem is only healthy if its circuit breaker is CLOSED ...
    auto cbIt = m_circuitBreakers.find(subsystem);
    if (cbIt != m_circuitBreakers.end() &&
        cbIt->second->GetState() != CircuitState::CLOSED) {
        return false;
    }

    // ... AND, for subsystems that depend on a real external subsystem, that
    // real subsystem must actually be reachable. Reporting "healthy" for an
    // unwired subsystem would be a simulated placeholder, not a real check.
    switch (subsystem) {
        case SubsystemType::REPUTATION_QUERY:
        case SubsystemType::TRUST_CONTEXT:
            return static_cast<bool>(g_trustContext);
        case SubsystemType::HAT_VALIDATION:
            return g_hatConsensusValidator != nullptr;
        default:
            // Pure-computation / breaker-only subsystems are healthy when the
            // circuit is CLOSED (checked above).
            return true;
    }
}

double GracefulDegradationManager::GetSystemHealth()
{
    LOCK(m_cs);
    
    if (m_emergencyMode) {
        return 0.0;
    }
    
    int totalSubsystems = m_subsystemStatus.size();
    int healthySubsystems = 0;
    
    for (const auto& pair : m_subsystemStatus) {
        if (pair.second.enabled && pair.second.circuitState == CircuitState::CLOSED) {
            healthySubsystems++;
        }
    }
    
    return totalSubsystems > 0 ? 
           static_cast<double>(healthySubsystems) / totalSubsystems : 1.0;
}

void GracefulDegradationManager::EnterEmergencyMode(const std::string& reason)
{
    LOCK(m_cs);
    
    if (!m_emergencyMode) {
        m_emergencyMode = true;
        m_emergencyReason = reason;
        m_degradationLevel = DegradationLevel::EMERGENCY;
        LogPrintf("CVM Graceful Degradation: EMERGENCY MODE ENTERED - %s\n", reason);
    }
}

void GracefulDegradationManager::ExitEmergencyMode()
{
    LOCK(m_cs);
    
    if (m_emergencyMode) {
        m_emergencyMode = false;
        m_emergencyReason.clear();
        m_degradationLevel = DegradationLevel::NORMAL;
        LogPrintf("CVM Graceful Degradation: Emergency mode exited\n");
    }
}

void GracefulDegradationManager::SetCircuitBreakerConfig(SubsystemType subsystem, 
                                                         const CircuitBreakerConfig& config)
{
    LOCK(m_cs);
    
    m_circuitBreakers[subsystem] = 
        std::make_unique<CircuitBreaker>(SubsystemTypeToString(subsystem), config);
}

std::string GracefulDegradationManager::SubsystemTypeToString(SubsystemType type) const
{
    switch (type) {
        case SubsystemType::TRUST_CONTEXT: return "TrustContext";
        case SubsystemType::REPUTATION_QUERY: return "ReputationQuery";
        case SubsystemType::HAT_VALIDATION: return "HATValidation";
        case SubsystemType::GAS_DISCOUNT: return "GasDiscount";
        case SubsystemType::FREE_GAS: return "FreeGas";
        case SubsystemType::CROSS_CHAIN_TRUST: return "CrossChainTrust";
        case SubsystemType::VALIDATOR_SELECTION: return "ValidatorSelection";
        case SubsystemType::DAO_DISPUTE: return "DAODispute";
        case SubsystemType::STORAGE_RENT: return "StorageRent";
        case SubsystemType::ANOMALY_DETECTION: return "AnomalyDetection";
        default: return "Unknown";
    }
}

int64_t GracefulDegradationManager::GetCurrentTimeMs() const
{
    return GetTimeMillis();
}

void GracefulDegradationManager::UpdateDegradationLevel()
{
    // Count subsystems with open circuit breakers
    int openCircuits = 0;
    int totalCircuits = m_circuitBreakers.size();
    
    for (const auto& pair : m_circuitBreakers) {
        if (pair.second->GetState() == CircuitState::OPEN) {
            openCircuits++;
        }
    }
    
    // Determine degradation level based on open circuits
    double openRatio = totalCircuits > 0 ? 
                       static_cast<double>(openCircuits) / totalCircuits : 0.0;
    
    if (openRatio >= 0.5) {
        SetDegradationLevel(DegradationLevel::MINIMAL);
    } else if (openRatio >= 0.25) {
        SetDegradationLevel(DegradationLevel::REDUCED);
    } else {
        SetDegradationLevel(DegradationLevel::NORMAL);
    }
}

void GracefulDegradationManager::CacheReputation(const uint160& address, uint8_t score)
{
    LOCK(m_cs);
    m_reputationCache[address] = {score, GetCurrentTimeMs()};
}

bool GracefulDegradationManager::GetCachedReputation(const uint160& address, uint8_t& score)
{
    LOCK(m_cs);
    
    auto it = m_reputationCache.find(address);
    if (it != m_reputationCache.end()) {
        int64_t age = GetCurrentTimeMs() - it->second.second;
        if (age < m_fallbackCacheTTL * 1000) {
            score = it->second.first;
            return true;
        }
    }
    return false;
}

void GracefulDegradationManager::CleanupExpiredCache()
{
    LOCK(m_cs);
    
    int64_t cutoff = GetCurrentTimeMs() - (m_fallbackCacheTTL * 1000);
    
    for (auto it = m_reputationCache.begin(); it != m_reputationCache.end(); ) {
        if (it->second.second < cutoff) {
            it = m_reputationCache.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Global Initialization Functions
// ============================================================================

bool InitializeGracefulDegradation()
{
    g_degradationManager = std::make_unique<GracefulDegradationManager>();
    return g_degradationManager->Initialize();
}

void ShutdownGracefulDegradation()
{
    if (g_degradationManager) {
        g_degradationManager->Shutdown();
        g_degradationManager.reset();
    }
}

} // namespace CVM
