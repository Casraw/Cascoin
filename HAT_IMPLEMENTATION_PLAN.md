# 🚀 HAT v2 Implementation Plan

## Overview

Implementierung von **Hybrid Adaptive Trust v2** mit allen Defense Mechanisms.

**Ziel:** 99%+ manipulationssicher!

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    HAT v2 Calculator                     │
│                                                          │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────┐│
│  │ Behavior   │  │ Web-of-    │  │ Economic   │  │Temp││
│  │ (40%)      │  │ Trust(30%) │  │ (20%)      │  │(10%)││
│  │            │  │            │  │            │  │    ││
│  │ +Diversity │  │ +Cluster   │  │ +StakeLock │  │+Act││
│  │ +Volume    │  │ +Centrality│  │ +Cooldown  │  │+Gap││
│  │ +Pattern   │  │ +EntryPt   │  │ +Decay     │  │Chk ││
│  └────────────┘  └────────────┘  └────────────┘  └────┘│
│         ↓              ↓                ↓            ↓   │
│         └──────────────┴────────────────┴────────────┘   │
│                          ↓                                │
│                  Final Trust Score                        │
│                  (0-100, multi-layered)                   │
└─────────────────────────────────────────────────────────┘
```

---

## Phase 1: Behavior Metrics Enhancement

### Neue Files:
- `src/cvm/behaviormetrics.h`
- `src/cvm/behaviormetrics.cpp`

### Data Structures:

```cpp
struct TradeRecord {
    uint256 txid;
    uint160 partner;
    CAmount volume;
    int64_t timestamp;
    bool success;
    bool disputed;
    
    SERIALIZE_METHODS(TradeRecord, obj) {
        READWRITE(obj.txid, obj.partner, obj.volume, 
                  obj.timestamp, obj.success, obj.disputed);
    }
};

struct BehaviorMetrics {
    uint160 address;
    
    // Trade Metrics
    std::vector<TradeRecord> trade_history;
    uint64_t total_trades;
    uint64_t successful_trades;
    uint64_t disputed_trades;
    CAmount total_volume;
    
    // Diversity Metrics
    std::set<uint160> unique_partners;
    
    // Temporal Metrics
    int64_t account_creation;
    int64_t last_activity;
    std::vector<int64_t> activity_timestamps;
    
    // Calculated Scores (cached)
    double diversity_score;
    double volume_score;
    double pattern_score;
    double base_reputation;
    
    // Methods
    void AddTrade(const TradeRecord& trade);
    void UpdateScores();
    
    double CalculateDiversityScore();
    double CalculateVolumeScore();
    double DetectSuspiciousPattern();
    int16_t CalculateFinalReputation();
    
    SERIALIZE_METHODS(BehaviorMetrics, obj) {
        READWRITE(obj.address, obj.trade_history, obj.total_trades,
                  obj.successful_trades, obj.disputed_trades,
                  obj.total_volume, obj.account_creation,
                  obj.last_activity, obj.activity_timestamps);
    }
};
```

### Implementation Details:

#### Diversity Score:
```cpp
double BehaviorMetrics::CalculateDiversityScore() {
    if (total_trades == 0) return 0.0;
    
    double expected_partners = sqrt(total_trades);
    double actual_partners = unique_partners.size();
    
    double diversity = actual_partners / expected_partners;
    
    // Examples:
    // 100 trades, 2 partners: 2/10 = 0.2
    // 100 trades, 50 partners: 50/10 = 5.0 → cap at 1.0
    
    return std::min(1.0, diversity);
}
```

#### Volume Score:
```cpp
double BehaviorMetrics::CalculateVolumeScore() {
    double volume_cas = total_volume / COIN;
    return log10(volume_cas + 1) / 6.0;  // Max at 1M CAS
}
```

#### Pattern Detection:
```cpp
double BehaviorMetrics::DetectSuspiciousPattern() {
    if (trade_history.size() < 10) return 1.0;
    
    std::vector<int64_t> intervals;
    for (size_t i = 1; i < trade_history.size(); i++) {
        intervals.push_back(
            trade_history[i].timestamp - trade_history[i-1].timestamp
        );
    }
    
    double mean = std::accumulate(intervals.begin(), intervals.end(), 0.0) 
                  / intervals.size();
    
    double variance = 0.0;
    for (auto interval : intervals) {
        variance += (interval - mean) * (interval - mean);
    }
    variance /= intervals.size();
    
    double cv = sqrt(variance) / mean;
    
    if (cv < 0.5) {
        return 0.5;  // Suspicious regularity!
    }
    
    return 1.0;
}
```

### Storage:
```cpp
// Key: "behavior_" + address
// Value: Serialized BehaviorMetrics

bool StoreBehaviorMetrics(const BehaviorMetrics& metrics) {
    std::string key = "behavior_" + metrics.address.ToString();
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << metrics;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    return g_cvmdb->WriteGeneric(key, data);
}
```

---

## Phase 2: Web-of-Trust Security

### Neue Files:
- `src/cvm/graphanalysis.h`
- `src/cvm/graphanalysis.cpp`

### Data Structures:

```cpp
struct GraphMetrics {
    uint160 address;
    
    // Cluster Detection
    bool in_suspicious_cluster;
    double mutual_trust_ratio;
    std::set<uint160> cluster_members;
    
    // Centrality Metrics
    double betweenness_centrality;
    double degree_centrality;
    double closeness_centrality;
    
    // Entry Point Analysis
    uint160 main_entry_point;
    int64_t entry_point_age;
    int nodes_through_entry;
    
    SERIALIZE_METHODS(GraphMetrics, obj) {
        READWRITE(obj.address, obj.in_suspicious_cluster,
                  obj.mutual_trust_ratio, obj.betweenness_centrality);
    }
};

class GraphAnalyzer {
public:
    GraphAnalyzer(CVMDatabase& db) : database(db) {}
    
    // Cluster Detection
    std::set<uint160> DetectSuspiciousClusters();
    double CalculateMutualTrustRatio(const uint160& address);
    
    // Centrality Analysis
    double CalculateBetweennessCentrality(const uint160& address);
    double CalculateDegreeCentrality(const uint160& address);
    
    // Entry Point Detection
    void DetectSuspiciousEntryPoints();
    uint160 FindMainEntryPoint(const uint160& address);
    
private:
    CVMDatabase& database;
};
```

### Implementation:

#### Cluster Detection:
```cpp
std::set<uint160> GraphAnalyzer::DetectSuspiciousClusters() {
    std::set<uint160> suspicious;
    
    std::vector<std::string> keys = database.ListKeysWithPrefix("trust_");
    std::set<uint160> all_nodes;
    
    // Collect all nodes
    for (const auto& key : keys) {
        if (key.find("trust_in_") != std::string::npos) continue;
        
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            TrustEdge edge;
            ss >> edge;
            all_nodes.insert(edge.fromAddress);
            all_nodes.insert(edge.toAddress);
        }
    }
    
    // Check each node for tight clustering
    for (const auto& node : all_nodes) {
        double mutual_ratio = CalculateMutualTrustRatio(node);
        
        if (mutual_ratio > 0.9) {  // 90%+ mutual
            suspicious.insert(node);
            LogPrintf("SUSPICIOUS CLUSTER: %s (mutual: %.2f)\n",
                     node.ToString(), mutual_ratio);
        }
    }
    
    return suspicious;
}
```

#### Betweenness Centrality:
```cpp
double GraphAnalyzer::CalculateBetweennessCentrality(const uint160& node) {
    // Simplified: Sample-based estimation
    const int SAMPLE_SIZE = 100;
    int paths_through = 0;
    int total_paths = 0;
    
    TrustGraph graph(database);
    std::vector<uint160> all_nodes = GetAllNodes();
    
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        uint160 source = all_nodes[rand() % all_nodes.size()];
        uint160 target = all_nodes[rand() % all_nodes.size()];
        
        if (source == target || source == node || target == node) continue;
        
        auto paths = graph.FindTrustPaths(source, target, 5);
        
        for (const auto& path : paths) {
            total_paths++;
            
            for (const auto& addr : path.addresses) {
                if (addr == node) {
                    paths_through++;
                    break;
                }
            }
        }
    }
    
    return total_paths > 0 ? (double)paths_through / total_paths : 0.0;
}
```

---

## Phase 3: Economic Trust Enhancement

### Updates zu existierenden Files:
- `src/cvm/trustgraph.h`
- `src/cvm/trustgraph.cpp`

### Neue Data Structures:

```cpp
struct StakeInfo {
    CAmount amount;
    int64_t stake_start;
    int64_t min_lock_duration = 180 * 24 * 3600;  // 6 months
    
    bool CanUnstake() const {
        return GetTime() >= stake_start + min_lock_duration;
    }
    
    double GetTimeWeight() const {
        int64_t staked_for = GetTime() - stake_start;
        double years = staked_for / (365.0 * 24 * 3600);
        return sqrt(years);  // Diminishing returns
    }
    
    SERIALIZE_METHODS(StakeInfo, obj) {
        READWRITE(obj.amount, obj.stake_start, obj.min_lock_duration);
    }
};

struct UnstakeRequest {
    uint160 address;
    CAmount amount;
    int64_t request_time;
    const int64_t COOLDOWN = 30 * 24 * 3600;  // 30 days
    
    bool CanWithdraw() const {
        return GetTime() >= request_time + COOLDOWN;
    }
    
    SERIALIZE_METHODS(UnstakeRequest, obj) {
        READWRITE(obj.address, obj.amount, obj.request_time);
    }
};
```

### New Methods:

```cpp
// In TrustGraph class
bool RequestUnstake(const uint160& address, CAmount amount);
bool ProcessUnstakeWithdrawal(const uint160& address);
void ApplyUnstakePenalty(const uint160& address, CAmount unstaked);
```

---

## Phase 4: Temporal Security

### Updates:

```cpp
struct TemporalMetrics {
    int64_t account_creation;
    int64_t last_activity;
    std::vector<int64_t> activity_timestamps;
    
    double CalculateActivityScore() {
        if (activity_timestamps.empty()) return 0.0;
        
        int64_t account_age = GetTime() - account_creation;
        int64_t inactive_time = GetTime() - last_activity;
        
        // Inactivity penalty
        double inactivity_penalty = exp(-inactive_time / (90.0 * 24 * 3600));
        
        // Activity ratio
        int active_months = CountActiveMonths();
        int total_months = std::max(1, (int)(account_age / (30 * 24 * 3600)));
        double activity_ratio = active_months / (double)total_months;
        
        return activity_ratio * inactivity_penalty;
    }
    
    int CountActiveMonths() {
        std::set<int> active_months;
        for (auto ts : activity_timestamps) {
            int month = ts / (30 * 24 * 3600);
            active_months.insert(month);
        }
        return active_months.size();
    }
    
    bool HasSuspiciousGaps() {
        for (size_t i = 1; i < activity_timestamps.size(); i++) {
            int64_t gap = activity_timestamps[i] - activity_timestamps[i-1];
            if (gap > 180 * 24 * 3600) {  // > 6 months
                return true;
            }
        }
        return false;
    }
};
```

---

## Phase 5: HAT v2 Integration

### New File:
- `src/cvm/securehat.h`
- `src/cvm/securehat.cpp`

### Main Structure:

```cpp
class SecureHAT {
public:
    SecureHAT(CVMDatabase& db) : database(db), analyzer(db) {}
    
    // Main calculation
    int16_t CalculateFinalTrust(
        const uint160& target,
        const uint160& viewer
    );
    
    // Component scores (for debugging/display)
    struct TrustBreakdown {
        double behavior_score;
        double diversity_penalty;
        double volume_penalty;
        double pattern_penalty;
        double secure_behavior;
        
        double wot_score;
        double cluster_penalty;
        double centrality_bonus;
        double secure_wot;
        
        double economic_score;
        double stake_time_weight;
        double secure_economic;
        
        double temporal_score;
        double activity_penalty;
        double secure_temporal;
        
        int16_t final_score;
    };
    
    TrustBreakdown CalculateWithBreakdown(
        const uint160& target,
        const uint160& viewer
    );
    
private:
    CVMDatabase& database;
    GraphAnalyzer analyzer;
    
    BehaviorMetrics GetBehaviorMetrics(const uint160& address);
    GraphMetrics GetGraphMetrics(const uint160& address);
    StakeInfo GetStakeInfo(const uint160& address);
    TemporalMetrics GetTemporalMetrics(const uint160& address);
};
```

### Implementation:

```cpp
int16_t SecureHAT::CalculateFinalTrust(
    const uint160& target,
    const uint160& viewer
) {
    // 1. Get all metrics
    BehaviorMetrics behavior = GetBehaviorMetrics(target);
    GraphMetrics graph = GetGraphMetrics(target);
    StakeInfo stake = GetStakeInfo(target);
    TemporalMetrics temporal = GetTemporalMetrics(target);
    
    // 2. Calculate secure behavior score
    double behavior_base = behavior.CalculateFinalReputation() / 100.0;
    double diversity = behavior.CalculateDiversityScore();
    double volume = behavior.CalculateVolumeScore();
    double pattern = behavior.DetectSuspiciousPattern();
    
    double secure_behavior = behavior_base * diversity * volume * pattern;
    
    // 3. Calculate secure WoT score
    TrustGraph tg(database);
    double wot_base = tg.GetWeightedReputation(viewer, target, 3) / 100.0;
    double cluster_penalty = graph.in_suspicious_cluster ? 0.3 : 1.0;
    double centrality = std::max(0.5, graph.betweenness_centrality * 2.0);
    
    double secure_wot = wot_base * cluster_penalty * centrality;
    
    // 4. Calculate secure economic score
    double economic_base = log10(stake.amount / COIN + 1) / 4.0;
    double stake_time = stake.GetTimeWeight();
    
    double secure_economic = economic_base * stake_time;
    
    // 5. Calculate secure temporal score
    double temporal_base = (GetTime() - temporal.account_creation) 
                          / (730.0 * 24 * 3600);  // 2 years max
    temporal_base = std::min(1.0, temporal_base);
    double activity = temporal.CalculateActivityScore();
    
    double secure_temporal = temporal_base * activity;
    
    // 6. Final weighted combination
    double final = 0.40 * secure_behavior +
                   0.30 * secure_wot +
                   0.20 * secure_economic +
                   0.10 * secure_temporal;
    
    return static_cast<int16_t>(final * 100.0);
}
```

---

## Phase 6: RPC Commands

### New RPCs:

```cpp
// Get behavior metrics
UniValue getbehaviormetrics(const JSONRPCRequest& request);

// Detect suspicious clusters
UniValue detectclusters(const JSONRPCRequest& request);

// Get secure HAT score
UniValue getsecuretrust(const JSONRPCRequest& request);

// Get trust breakdown (detailed)
UniValue gettrustbreakdown(const JSONRPCRequest& request);
```

---

## Phase 7: Testing Strategy

### Unit Tests:

1. **Diversity Detection:**
   - 100 trades, 2 partners → Score 0.2
   - 100 trades, 50 partners → Score 1.0

2. **Pattern Detection:**
   - Regular intervals → Score 0.5
   - Random intervals → Score 1.0

3. **Cluster Detection:**
   - Complete subgraph → Flagged
   - Normal connections → OK

4. **Stake Lock:**
   - Can't unstake before 6 months
   - Cooldown works

### Integration Tests:

1. **Fake Trade Attack:**
   - Create 2 accounts
   - Trade 100x between them
   - Check: Low diversity score

2. **Fake Cluster Attack:**
   - Create 10 accounts
   - All trust each other
   - Check: Flagged as suspicious

3. **Temporary Stake Attack:**
   - Stake 100 CAS
   - Vote high
   - Try unstake immediately
   - Check: Locked

### E2E Tests:

Full attack simulation with all vectors!

---

## Timeline

- **Phase 1-2:** 4-6 hours
- **Phase 3-4:** 3-4 hours  
- **Phase 5-6:** 2-3 hours
- **Phase 7-8:** 3-4 hours

**Total:** ~15-20 hours intensive work

---

## Success Criteria

✅ All unit tests pass
✅ Attack success rate < 1%
✅ RPC commands work
✅ Dashboard shows breakdown
✅ Documentation complete

---

Bereit? Los geht's! 🚀

