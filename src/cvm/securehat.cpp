// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/securehat.h>
#include <util.h>
#include <tinyformat.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace CVM {

// TemporalMetrics Implementation

double TemporalMetrics::CalculateActivityScore() const {
    if (activity_timestamps.empty()) return 0.0;
    
    int64_t account_age = GetTime() - account_creation;
    int64_t inactive_time = GetTime() - last_activity;
    
    // Penalty for long inactivity (90 days half-life)
    double inactivity_penalty = exp(-inactive_time / (90.0 * 24 * 3600));
    
    // Activity ratio: active months vs total months
    int active_months = CountActiveMonths();
    int total_months = std::max(1, (int)(account_age / (30 * 24 * 3600)));
    double activity_ratio = active_months / (double)total_months;
    
    // Need both: account age AND high activity
    return activity_ratio * inactivity_penalty;
}

int TemporalMetrics::CountActiveMonths() const {
    if (activity_timestamps.empty()) return 0;
    
    std::set<int> active_months;
    for (auto ts : activity_timestamps) {
        // Divide by 30 days to get month buckets
        int month = ts / (30 * 24 * 3600);
        active_months.insert(month);
    }
    return active_months.size();
}

bool TemporalMetrics::HasSuspiciousGaps() const {
    if (activity_timestamps.size() < 2) return false;
    
    // Check for gaps > 6 months
    for (size_t i = 1; i < activity_timestamps.size(); i++) {
        int64_t gap = activity_timestamps[i] - activity_timestamps[i-1];
        if (gap > 180 * 24 * 3600) {  // > 6 months
            return true;
        }
    }
    return false;
}

// SecureHAT Implementation

SecureHAT::SecureHAT(CVMDatabase& db) 
    : database(db), analyzer(db) {
}

int16_t SecureHAT::CalculateFinalTrust(
    const TrustNodeId& target,
    const TrustNodeId& viewer
) {
    TrustBreakdown breakdown = CalculateWithBreakdown(target, viewer);
    return breakdown.final_score;
}

int16_t SecureHAT::CalculateFinalTrust(
    const uint160& target,
    const uint160& viewer
) {
    // Legacy P2PKH wrapper: a bare uint160 is a legacy P2PKH-shaped identity.
    return CalculateFinalTrust(TrustNodeId::FromLegacyUint160(target),
                               TrustNodeId::FromLegacyUint160(viewer));
}

TrustBreakdown SecureHAT::CalculateWithBreakdown(
    const TrustNodeId& target,
    const TrustNodeId& viewer
) {
    TrustBreakdown breakdown;
    
    // 1. Get all metrics
    BehaviorMetrics behavior = GetBehaviorMetrics(target);
    GraphMetrics graph = GetGraphMetrics(target);
    StakeInfo stake = GetStakeInfo(target);
    TemporalMetrics temporal = GetTemporalMetrics(target);
    
    // ═══════════════════════════════════════════════════════════════
    // BEHAVIOR COMPONENT (40%) - Objective metrics
    // ═══════════════════════════════════════════════════════════════
    
    // Base behavior score (0-1)
    breakdown.behavior_base = behavior.CalculateFinalReputation() / 100.0;
    
    // Diversity penalty: low partner diversity = suspicious
    breakdown.diversity_penalty = behavior.CalculateDiversityScore();
    
    // Volume penalty: logarithmic scaling prevents pumping
    breakdown.volume_penalty = behavior.CalculateVolumeScore();
    
    // Pattern penalty: too regular intervals = bot
    breakdown.pattern_penalty = behavior.DetectSuspiciousPattern();
    
    // Combine all behavior factors (multiplicative penalties)
    breakdown.secure_behavior = breakdown.behavior_base * 
                                breakdown.diversity_penalty * 
                                breakdown.volume_penalty * 
                                breakdown.pattern_penalty;
    
    // ═══════════════════════════════════════════════════════════════
    // WEB-OF-TRUST COMPONENT (30%) - Subjective, personalized
    // ═══════════════════════════════════════════════════════════════
    
    // Base WoT score from viewer's perspective
    TrustGraph tg(database);
    breakdown.wot_base = tg.GetWeightedReputation(viewer, target, 3) / 100.0;
    
    // Cluster penalty: if in suspicious cluster, reduce trust
    breakdown.cluster_penalty = graph.in_suspicious_cluster ? 0.3 : 1.0;
    
    // Centrality bonus: well-connected nodes get bonus
    // Low centrality = isolated = suspicious
    breakdown.centrality_bonus = std::max(0.5, graph.betweenness_centrality * 2.0);
    breakdown.centrality_bonus = std::min(1.5, breakdown.centrality_bonus);
    
    // Combine WoT factors
    breakdown.secure_wot = breakdown.wot_base * 
                          breakdown.cluster_penalty * 
                          breakdown.centrality_bonus;
    
    // ═══════════════════════════════════════════════════════════════
    // ECONOMIC COMPONENT (20%) - Skin in the game
    // ═══════════════════════════════════════════════════════════════
    
    // Base economic score: logarithmic stake amount
    // Need 10,000 CAS for max score (1.0)
    breakdown.economic_base = 0.0;
    breakdown.stake_time_weight = 0.0;
    
    if (stake.amount > 0) {
        double cas_amount = stake.amount / (double)COIN;
        breakdown.economic_base = log10(cas_amount + 1) / 4.0;
        breakdown.economic_base = std::min(1.0, breakdown.economic_base);
        
        // Stake time weight: longer stake = higher weight
        breakdown.stake_time_weight = stake.GetTimeWeight();
    }
    
    // Combine economic factors
    breakdown.secure_economic = breakdown.economic_base * 
                               breakdown.stake_time_weight;
    
    // ═══════════════════════════════════════════════════════════════
    // TEMPORAL COMPONENT (10%) - Time & activity
    // ═══════════════════════════════════════════════════════════════
    
    // Base temporal score: account age (max 2 years)
    int64_t account_age = GetTime() - temporal.account_creation;
    breakdown.temporal_base = account_age / (730.0 * 24 * 3600);  // 2 years
    breakdown.temporal_base = std::min(1.0, breakdown.temporal_base);
    
    // Activity penalty: inactive accounts penalized
    breakdown.activity_penalty = temporal.CalculateActivityScore();
    
    // Gap penalty: long inactive periods suspicious
    if (temporal.HasSuspiciousGaps()) {
        breakdown.activity_penalty *= 0.5;
    }
    
    // Combine temporal factors
    breakdown.secure_temporal = breakdown.temporal_base * 
                               breakdown.activity_penalty;
    
    // ═══════════════════════════════════════════════════════════════
    // FINAL WEIGHTED COMBINATION
    // ═══════════════════════════════════════════════════════════════
    
    double final_trust = 0.40 * breakdown.secure_behavior +
                        0.30 * breakdown.secure_wot +
                        0.20 * breakdown.secure_economic +
                        0.10 * breakdown.secure_temporal;
    
    breakdown.final_score = static_cast<int16_t>(final_trust * 100.0);
    breakdown.final_score = std::max(int16_t(0), std::min(int16_t(100), breakdown.final_score));
    
    LogPrintf("SecureHAT: %s -> %d (B:%.2f W:%.2f E:%.2f T:%.2f)\n",
              target.ToKeyString(), breakdown.final_score,
              breakdown.secure_behavior, breakdown.secure_wot,
              breakdown.secure_economic, breakdown.secure_temporal);
    
    return breakdown;
}

TrustBreakdown SecureHAT::CalculateWithBreakdown(
    const uint160& target,
    const uint160& viewer
) {
    // Legacy P2PKH wrapper.
    return CalculateWithBreakdown(TrustNodeId::FromLegacyUint160(target),
                                  TrustNodeId::FromLegacyUint160(viewer));
}

// ═══════════════════════════════════════════════════════════════
// Component Getters
// ═══════════════════════════════════════════════════════════════

BehaviorMetrics SecureHAT::GetBehaviorMetrics(const TrustNodeId& address) {
    // One current typed layout keyed by the canonical ToKeyString() segment.
    std::string key = "behavior_" + address.ToKeyString();
    std::vector<uint8_t> data;
    
    if (database.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            BehaviorMetrics metrics;
            ss >> metrics;
            // Require complete stream consumption: reject trailing bytes.
            if (!ss.empty()) {
                LogPrintf("ERROR: Trailing bytes in BehaviorMetrics record for %s\n",
                          address.ToKeyString());
            } else {
                return metrics;
            }
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to deserialize BehaviorMetrics: %s\n", e.what());
        }
    }
    
    // Return default metrics if not found
    BehaviorMetrics metrics;
    metrics.address = address;
    return metrics;
}

GraphMetrics SecureHAT::GetGraphMetrics(const TrustNodeId& address) {
    // Calculate on-demand (could be cached later)
    GraphMetrics metrics;
    metrics.address = address;
    
    // Cluster detection
    std::set<TrustNodeId> suspicious = analyzer.DetectSuspiciousClusters();
    metrics.in_suspicious_cluster = suspicious.count(address) > 0;
    metrics.mutual_trust_ratio = analyzer.CalculateMutualTrustRatio(address);
    
    // Centrality metrics
    metrics.betweenness_centrality = analyzer.CalculateBetweennessCentrality(address);
    metrics.degree_centrality = analyzer.CalculateDegreeCentrality(address);
    metrics.closeness_centrality = analyzer.CalculateClosenessCentrality(address);
    
    return metrics;
}

StakeInfo SecureHAT::GetStakeInfo(const TrustNodeId& address) {
    std::string key = "stake_" + address.ToKeyString();
    std::vector<uint8_t> data;
    
    if (database.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            StakeInfo info;
            ss >> info;
            if (!ss.empty()) {
                LogPrintf("ERROR: Trailing bytes in StakeInfo record for %s\n",
                          address.ToKeyString());
            } else {
                return info;
            }
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to deserialize StakeInfo: %s\n", e.what());
        }
    }
    
    // Return default stake info
    return StakeInfo();
}

TemporalMetrics SecureHAT::GetTemporalMetrics(const TrustNodeId& address) {
    std::string key = "temporal_" + address.ToKeyString();
    std::vector<uint8_t> data;
    
    if (database.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            TemporalMetrics metrics;
            ss >> metrics;
            if (!ss.empty()) {
                LogPrintf("ERROR: Trailing bytes in TemporalMetrics record for %s\n",
                          address.ToKeyString());
            } else {
                return metrics;
            }
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to deserialize TemporalMetrics: %s\n", e.what());
        }
    }
    
    // Return default temporal metrics
    TemporalMetrics metrics;
    metrics.account_creation = GetTime();
    metrics.last_activity = GetTime();
    return metrics;
}

// ───────────────────────────────────────────────────────────────
// Legacy uint160 wrappers (P2PKH callers). Wave 8 removes the remaining
// uint160 bridging at RPC sites.
// ───────────────────────────────────────────────────────────────

BehaviorMetrics SecureHAT::GetBehaviorMetrics(const uint160& address) {
    return GetBehaviorMetrics(TrustNodeId::FromLegacyUint160(address));
}

GraphMetrics SecureHAT::GetGraphMetrics(const uint160& address) {
    return GetGraphMetrics(TrustNodeId::FromLegacyUint160(address));
}

StakeInfo SecureHAT::GetStakeInfo(const uint160& address) {
    return GetStakeInfo(TrustNodeId::FromLegacyUint160(address));
}

TemporalMetrics SecureHAT::GetTemporalMetrics(const uint160& address) {
    return GetTemporalMetrics(TrustNodeId::FromLegacyUint160(address));
}

// ═══════════════════════════════════════════════════════════════
// Storage Methods
// ═══════════════════════════════════════════════════════════════

bool SecureHAT::StoreBehaviorMetrics(const BehaviorMetrics& metrics) {
    std::string key = "behavior_" + metrics.address.ToKeyString();
    
    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << metrics;
        std::vector<uint8_t> data(ss.begin(), ss.end());
        return database.WriteGeneric(key, data);
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to serialize BehaviorMetrics: %s\n", e.what());
        return false;
    }
}

bool SecureHAT::StoreStakeInfo(const TrustNodeId& address, const StakeInfo& info) {
    std::string key = "stake_" + address.ToKeyString();
    
    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << info;
        std::vector<uint8_t> data(ss.begin(), ss.end());
        return database.WriteGeneric(key, data);
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to serialize StakeInfo: %s\n", e.what());
        return false;
    }
}

bool SecureHAT::StoreTemporalMetrics(const TrustNodeId& address, const TemporalMetrics& metrics) {
    std::string key = "temporal_" + address.ToKeyString();
    
    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << metrics;
        std::vector<uint8_t> data(ss.begin(), ss.end());
        return database.WriteGeneric(key, data);
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to serialize TemporalMetrics: %s\n", e.what());
        return false;
    }
}

// Legacy uint160 wrappers (P2PKH callers).
bool SecureHAT::StoreStakeInfo(const uint160& address, const StakeInfo& info) {
    return StoreStakeInfo(TrustNodeId::FromLegacyUint160(address), info);
}

bool SecureHAT::StoreTemporalMetrics(const uint160& address, const TemporalMetrics& metrics) {
    return StoreTemporalMetrics(TrustNodeId::FromLegacyUint160(address), metrics);
}

} // namespace CVM

