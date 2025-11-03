// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BEHAVIORMETRICS_H
#define CASCOIN_CVM_BEHAVIORMETRICS_H

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <set>
#include <vector>

namespace CVM {

/**
 * TradeRecord - Records a single trade for behavior analysis
 */
struct TradeRecord {
    uint256 txid;
    uint160 partner;
    CAmount volume;
    int64_t timestamp;
    bool success;
    bool disputed;
    
    TradeRecord() : txid(), partner(), volume(0), timestamp(0), 
                    success(true), disputed(false) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(partner);
        READWRITE(volume);
        READWRITE(timestamp);
        READWRITE(success);
        READWRITE(disputed);
    }
};

/**
 * BehaviorMetrics - Comprehensive behavior analysis for reputation
 * 
 * Tracks user behavior to detect fake trades, Sybil attacks, and
 * calculate objective reputation scores.
 */
class BehaviorMetrics {
public:
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
    
    // Cached scores (updated by UpdateScores())
    double diversity_score;
    double volume_score;
    double pattern_score;
    double base_reputation;
    
    BehaviorMetrics();
    explicit BehaviorMetrics(const uint160& addr);
    
    // Trade Management
    void AddTrade(const TradeRecord& trade);
    void AddActivity(int64_t timestamp);
    
    // Score Calculations
    void UpdateScores();
    
    /**
     * Calculate Diversity Score
     * 
     * Detects fake trades by checking trade partner diversity.
     * Low diversity (few unique partners) indicates Sybil attack.
     * 
     * Formula: unique_partners / sqrt(total_trades)
     * 
     * Examples:
     *   100 trades, 2 partners: 2/10 = 0.2 (SUSPICIOUS!)
     *   100 trades, 50 partners: 50/10 = 5.0 → capped at 1.0 (GOOD)
     * 
     * @return 0.0-1.0 (lower = more suspicious)
     */
    double CalculateDiversityScore() const;
    
    /**
     * Calculate Volume Score
     * 
     * Higher volume = more established user.
     * Logarithmic scaling prevents volume pumping.
     * 
     * Formula: log10(volume_in_CAS + 1) / 6.0
     * 
     * Examples:
     *   1 CAS: 0.05
     *   100 CAS: 0.33
     *   1000 CAS: 0.5
     *   1M CAS: 1.0
     * 
     * @return 0.0-1.0
     */
    double CalculateVolumeScore() const;
    
    /**
     * Detect Suspicious Trade Pattern
     * 
     * Detects automated/scripted trading by analyzing time intervals.
     * Regular intervals = suspicious (likely bot).
     * Random intervals = normal (human behavior).
     * 
     * Uses Coefficient of Variation (CV):
     *   CV = std_dev / mean
     *   CV < 0.5 = suspicious
     *   CV > 1.0 = normal
     * 
     * @return 0.5 if suspicious, 1.0 if normal
     */
    double DetectSuspiciousPattern() const;
    
    /**
     * Calculate Base Reputation (before penalties)
     * 
     * Weighted combination of:
     *   - Success rate (40%)
     *   - Account age (20%)
     *   - Volume (15%)
     *   - Stake (15%)
     *   - Social (10%)
     * 
     * Penalty for disputes.
     * 
     * @return 0-100
     */
    int16_t CalculateBaseReputation() const;
    
    /**
     * Calculate Final Reputation (with all penalties)
     * 
     * Base reputation multiplied by:
     *   - Diversity penalty
     *   - Volume penalty
     *   - Pattern penalty
     * 
     * @return 0-100
     */
    int16_t CalculateFinalReputation() const;
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(trade_history);
        READWRITE(total_trades);
        READWRITE(successful_trades);
        READWRITE(disputed_trades);
        READWRITE(total_volume);
        READWRITE(account_creation);
        READWRITE(last_activity);
        READWRITE(activity_timestamps);
        READWRITE(diversity_score);
        READWRITE(volume_score);
        READWRITE(pattern_score);
        READWRITE(base_reputation);
    }
};

} // namespace CVM

#endif // CASCOIN_CVM_BEHAVIORMETRICS_H

