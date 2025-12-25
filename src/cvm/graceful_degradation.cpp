// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/graceful_degradation.h>
#include <util.h>
#include <utiltime.h>
#include <tinyformat.h>

namespace CVM {

// Global instance
std::unique_ptr<GracefulDegradationManager> g_degradationManager;

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
    // Check if subsystem is available
    if (!IsSubsystemAvailable(SubsystemType::REPUTATION_QUERY)) {
        // Try cache first
        uint8_t cachedScore;
        if (GetCachedReputation(address, cachedScore)) {
            return FallbackResult<uint8_t>::Fallback(cachedScore, "Circuit breaker open, using cached value");
        }
        return FallbackResult<uint8_t>::Fallback(defaultValue, "Circuit breaker open, using default value");
    }
    
    // Try to get actual reputation
    // Note: In real implementation, this would call the actual reputation system
    // For now, we simulate success and return a placeholder
    RecordSubsystemSuccess(SubsystemType::REPUTATION_QUERY);
    
    // Cache the result
    CacheReputation(address, defaultValue);
    
    return FallbackResult<uint8_t>::Success(defaultValue);
}

FallbackResult<bool> GracefulDegradationManager::InjectTrustContextWithFallback(
    const uint160& caller, const uint160& contract)
{
    if (!IsSubsystemAvailable(SubsystemType::TRUST_CONTEXT)) {
        return FallbackResult<bool>::Fallback(true, "Circuit breaker open, using default trust context");
    }
    
    // In real implementation, this would call the actual trust context injection
    RecordSubsystemSuccess(SubsystemType::TRUST_CONTEXT);
    return FallbackResult<bool>::Success(true);
}

FallbackResult<bool> GracefulDegradationManager::ValidateWithHATv2Fallback(
    const uint256& txHash, const uint160& sender, uint8_t selfReportedScore)
{
    if (!IsSubsystemAvailable(SubsystemType::HAT_VALIDATION)) {
        // Fall back to local validation
        // In degraded mode, we accept transactions with reasonable scores
        if (selfReportedScore <= 100 && selfReportedScore >= 0) {
            return FallbackResult<bool>::Fallback(true, "HAT v2 unavailable, using local validation");
        }
        return FallbackResult<bool>::Fallback(false, "HAT v2 unavailable, invalid score");
    }
    
    // In real implementation, this would call the actual HAT v2 validation
    RecordSubsystemSuccess(SubsystemType::HAT_VALIDATION);
    return FallbackResult<bool>::Success(true);
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

void GracefulDegradationManager::CheckMemoryUsage()
{
    // In real implementation, this would check actual memory usage
    // For now, we simulate normal operation
}

void GracefulDegradationManager::CheckCPUUsage()
{
    // In real implementation, this would check actual CPU usage
}

void GracefulDegradationManager::CheckStorageUsage()
{
    // In real implementation, this would check actual storage usage
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
    // In real implementation, this would perform actual health checks
    // For now, we check the circuit breaker state
    auto cbIt = m_circuitBreakers.find(subsystem);
    if (cbIt != m_circuitBreakers.end()) {
        return cbIt->second->GetState() == CircuitState::CLOSED;
    }
    return true;
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
