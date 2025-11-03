// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/behaviormetrics.h>
#include <util.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace CVM {

BehaviorMetrics::BehaviorMetrics()
    : address(),
      total_trades(0),
      successful_trades(0),
      disputed_trades(0),
      total_volume(0),
      account_creation(GetTime()),
      last_activity(GetTime()),
      diversity_score(0.0),
      volume_score(0.0),
      pattern_score(1.0),
      base_reputation(0)
{
}

BehaviorMetrics::BehaviorMetrics(const uint160& addr)
    : address(addr),
      total_trades(0),
      successful_trades(0),
      disputed_trades(0),
      total_volume(0),
      account_creation(GetTime()),
      last_activity(GetTime()),
      diversity_score(0.0),
      volume_score(0.0),
      pattern_score(1.0),
      base_reputation(0)
{
}

void BehaviorMetrics::AddTrade(const TradeRecord& trade) {
    trade_history.push_back(trade);
    total_trades++;
    
    if (trade.success) {
        successful_trades++;
    }
    
    if (trade.disputed) {
        disputed_trades++;
    }
    
    total_volume += trade.volume;
    unique_partners.insert(trade.partner);
    
    last_activity = trade.timestamp;
    activity_timestamps.push_back(trade.timestamp);
    
    LogPrint(BCLog::ALL, "BehaviorMetrics: Added trade for %s (total: %d, partners: %d)\n",
             address.ToString(), total_trades, unique_partners.size());
}

void BehaviorMetrics::AddActivity(int64_t timestamp) {
    last_activity = timestamp;
    activity_timestamps.push_back(timestamp);
}

void BehaviorMetrics::UpdateScores() {
    diversity_score = CalculateDiversityScore();
    volume_score = CalculateVolumeScore();
    pattern_score = DetectSuspiciousPattern();
    base_reputation = CalculateBaseReputation();
    
    LogPrint(BCLog::ALL, "BehaviorMetrics: Updated scores for %s: diversity=%.2f, volume=%.2f, pattern=%.2f, base=%d\n",
             address.ToString(), diversity_score, volume_score, pattern_score, base_reputation);
}

double BehaviorMetrics::CalculateDiversityScore() const {
    if (total_trades == 0) return 0.0;
    
    double expected_partners = sqrt(static_cast<double>(total_trades));
    double actual_partners = static_cast<double>(unique_partners.size());
    
    double diversity = actual_partners / expected_partners;
    
    // Cap at 1.0
    double score = std::min(1.0, diversity);
    
    if (score < 0.3) {
        LogPrintf("BehaviorMetrics: LOW DIVERSITY WARNING for %s: %.2f (%d partners, %d trades)\n",
                 address.ToString(), score, unique_partners.size(), total_trades);
    }
    
    return score;
}

double BehaviorMetrics::CalculateVolumeScore() const {
    double volume_cas = static_cast<double>(total_volume) / COIN;
    
    // Logarithmic scaling: need exponentially more volume for higher scores
    // log10(1M + 1) / 6.0 = 1.0
    double score = log10(volume_cas + 1.0) / 6.0;
    
    return std::min(1.0, score);
}

double BehaviorMetrics::DetectSuspiciousPattern() const {
    if (trade_history.size() < 10) {
        return 1.0;  // Not enough data
    }
    
    // Calculate time intervals between trades
    std::vector<int64_t> intervals;
    for (size_t i = 1; i < trade_history.size(); i++) {
        int64_t interval = trade_history[i].timestamp - trade_history[i-1].timestamp;
        if (interval > 0) {  // Skip invalid intervals
            intervals.push_back(interval);
        }
    }
    
    if (intervals.empty()) {
        return 1.0;
    }
    
    // Calculate mean
    double mean = std::accumulate(intervals.begin(), intervals.end(), 0.0) 
                  / intervals.size();
    
    // Calculate variance
    double variance = 0.0;
    for (auto interval : intervals) {
        double diff = interval - mean;
        variance += diff * diff;
    }
    variance /= intervals.size();
    
    // Calculate Coefficient of Variation (CV)
    double std_dev = sqrt(variance);
    double cv = std_dev / mean;
    
    // CV < 0.5 = too regular (suspicious!)
    if (cv < 0.5) {
        LogPrintf("BehaviorMetrics: SUSPICIOUS PATTERN detected for %s: CV=%.2f (mean=%.0fs, stddev=%.0fs)\n",
                 address.ToString(), cv, mean, std_dev);
        return 0.5;  // 50% penalty!
    }
    
    return 1.0;
}

int16_t BehaviorMetrics::CalculateBaseReputation() const {
    if (total_trades == 0) {
        // New account with no trades
        return 50;  // Neutral
    }
    
    double score = 0.0;
    
    // 1. Trade Success Rate (40% weight)
    double success_rate = static_cast<double>(successful_trades) / total_trades;
    score += success_rate * 40.0;
    
    // 2. Account Age (20% weight)
    // Max score at 2 years
    int64_t account_age = GetTime() - account_creation;
    double age_years = account_age / (365.0 * 24 * 3600);
    double age_score = std::min(age_years / 2.0, 1.0);
    score += age_score * 20.0;
    
    // 3. Volume (15% weight)
    score += volume_score * 15.0;
    
    // 4. Activity Level (15% weight)
    // More recent activity = better
    int64_t inactive_time = GetTime() - last_activity;
    double activity_score = exp(-inactive_time / (90.0 * 24 * 3600));  // 90 days half-life
    score += activity_score * 15.0;
    
    // 5. Social Proof (10% weight)
    // Number of unique partners (up to 100)
    double social_score = std::min(unique_partners.size() / 100.0, 1.0);
    score += social_score * 10.0;
    
    // Penalty for disputes
    if (total_trades > 0) {
        double dispute_rate = static_cast<double>(disputed_trades) / total_trades;
        score *= (1.0 - dispute_rate);  // Multiply penalty
    }
    
    return static_cast<int16_t>(std::max(0.0, std::min(100.0, score)));
}

int16_t BehaviorMetrics::CalculateFinalReputation() const {
    double base = static_cast<double>(base_reputation);
    
    // Apply all penalties
    double final = base * diversity_score * volume_score * pattern_score;
    
    return static_cast<int16_t>(std::max(0.0, std::min(100.0, final)));
}

} // namespace CVM

