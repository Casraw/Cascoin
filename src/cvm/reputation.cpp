// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reputation.h>
#include <cvm/cvmdb.h>
#include <streams.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <amount.h>

#include <algorithm>

namespace CVM {

// Reputation transaction marker
static const std::string REP_MARKER = "REP";
static const uint8_t REP_VERSION = 0x01;

// Database key for reputation scores
static const char DB_REPUTATION = 'R';

// Generic-key prefix for the maintained reputation index (clause 2.49). Every
// address that has a reputation record is recorded under this prefix so the
// index accessor can enumerate them without scanning every DB key.
static const std::string REP_INDEX_PREFIX = "repidx_";

// Generic-key prefix for the per-address transaction-history index (clause
// 2.14). Stores the block heights at which the address transacted so
// DetectRapidFire can assess the rapid-fire pattern.
static const std::string TX_HISTORY_PREFIX = "txhist_";

// Rapid-fire pattern parameters: at least kRapidFireMinTxs transactions within
// a window of kRapidFireWindowBlocks blocks constitutes rapid-fire activity.
static const size_t kRapidFireMinTxs = 5;
static const int64_t kRapidFireWindowBlocks = 3;

// Exchange pattern thresholds: a very high transaction count or volume.
static const uint64_t kExchangeTxThreshold = 10000;
static const uint64_t kExchangeVolumeThreshold = 100000ULL * COIN;

// Maintain the reputation index: record that `address` has a reputation record.
static void IndexReputationAddress(CVMDatabase& database, const uint160& address) {
    std::string idxKey = REP_INDEX_PREFIX + address.ToString();
    std::vector<uint8_t> value(address.begin(), address.end());
    database.WriteGeneric(idxKey, value);
}

std::string ReputationScore::GetReputationLevel() const {
    if (score >= 7500) return "Excellent";
    if (score >= 5000) return "Very Good";
    if (score >= 2500) return "Good";
    if (score >= 0) return "Neutral";
    if (score >= -2500) return "Questionable";
    if (score >= -5000) return "Poor";
    if (score >= -7500) return "Very Poor";
    return "Dangerous";
}

std::vector<uint8_t> ReputationVoteTx::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool ReputationVoteTx::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> *this;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ReputationVoteTx::IsValid(std::string& error) const {
    if (voteValue < -100 || voteValue > 100) {
        error = "Vote value must be between -100 and 100";
        return false;
    }
    
    if (voteValue == 0) {
        error = "Vote value cannot be zero";
        return false;
    }
    
    if (reason.empty()) {
        error = "Vote must include a reason";
        return false;
    }
    
    if (reason.size() > 500) {
        error = "Reason is too long (max 500 characters)";
        return false;
    }
    
    return true;
}

bool ParseReputationVoteTx(const CTransaction& tx, ReputationVoteTx& voteTx) {
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey.size() > 0 && output.scriptPubKey[0] == OP_RETURN) {
            std::vector<uint8_t> data(output.scriptPubKey.begin() + 1, output.scriptPubKey.end());
            
            if (data.size() < REP_MARKER.size() + 2) {
                continue;
            }
            
            std::string marker(data.begin(), data.begin() + REP_MARKER.size());
            if (marker != REP_MARKER) {
                continue;
            }
            
            if (data[REP_MARKER.size()] != REP_VERSION) {
                continue;
            }
            
            // Extract vote data
            std::vector<uint8_t> voteData(data.begin() + REP_MARKER.size() + 1, data.end());
            return voteTx.Deserialize(voteData);
        }
    }
    
    return false;
}

bool IsReputationVoteTransaction(const CTransaction& tx) {
    ReputationVoteTx voteTx;
    return ParseReputationVoteTx(tx, voteTx);
}

// ReputationSystem implementation
ReputationSystem::ReputationSystem(CVMDatabase& db) : database(db) {
}

bool ReputationSystem::GetReputation(const uint160& address, ReputationScore& score) {
    std::string dbKey = std::string(1, DB_REPUTATION) + 
                       std::string((char*)address.begin(), 20);
    
    if (!database.GetDB().Read(dbKey, score)) {
        // Return default score for new addresses
        score = ReputationScore();
        score.address = address;
        score.category = "normal";
        return false;
    }
    
    return true;
}

bool ReputationSystem::UpdateReputation(const uint160& address, const ReputationScore& score) {
    std::string dbKey = std::string(1, DB_REPUTATION) + 
                       std::string((char*)address.begin(), 20);
    
    bool ok = database.GetDB().Write(dbKey, score, true);
    if (ok) {
        // Maintain the reputation index (clause 2.49).
        IndexReputationAddress(database, address);
    }
    return ok;
}

bool ReputationSystem::ApplyVote(const uint160& voterAddress, 
                                const ReputationVoteTx& vote, 
                                int64_t timestamp) {
    // Bugfix 2.18: a reputation vote must be attributed to a resolved voter.
    // Reject votes from a null/zero voter address (an unresolved voter) rather
    // than silently applying them to the zero address.
    if (voterAddress.IsNull()) {
        LogPrintf("Rejecting reputation vote from unresolved (zero) voter address\n");
        return false;
    }

    std::string error;
    if (!vote.IsValid(error)) {
        LogPrintf("Invalid reputation vote: %s\n", error);
        return false;
    }
    
    // Get current reputation score
    ReputationScore score;
    GetReputation(vote.targetAddress, score);
    
    // Calculate voting power
    int64_t votingPower = GetVotingPower(voterAddress);
    
    if (votingPower <= 0) {
        LogPrintf("Voter has insufficient voting power\n");
        return false;
    }
    
    // Calculate score change
    int64_t scoreChange = CalculateScoreChange(vote.voteValue, votingPower);
    
    // Apply decay to old score
    ApplyDecay(score, timestamp);
    
    // Update score
    score.score += scoreChange;
    score.voteCount++;
    score.lastUpdated = timestamp;
    
    // Clamp score to valid range
    if (score.score > 10000) score.score = 10000;
    if (score.score < -10000) score.score = -10000;
    
    // Update category based on score
    if (score.score < -5000) {
        score.category = "scam";
    } else if (score.suspiciousPatterns > 10) {
        score.category = "mixer";
    } else if (score.totalTransactions > 10000) {
        score.category = "exchange";
    } else {
        score.category = "normal";
    }
    
    // Write back to database
    std::string dbKey = std::string(1, DB_REPUTATION) + 
                       std::string((char*)score.address.begin(), 20);
    
    bool ok = database.GetDB().Write(dbKey, score);
    if (ok) {
        // Maintain the reputation index (clause 2.49).
        IndexReputationAddress(database, vote.targetAddress);
    }
    return ok;
}

void ReputationSystem::UpdateBehaviorScore(const uint160& address, 
                                          const CTransaction& tx, 
                                          int blockHeight) {
    ReputationScore score;
    GetReputation(address, score);
    
    score.totalTransactions++;
    
    // Analyze for suspicious patterns
    std::string reason;
    if (DetectSuspiciousPattern(tx, reason)) {
        score.suspiciousPatterns++;
        score.score -= 10; // Small penalty for suspicious behavior
        
        LogPrintf("Suspicious pattern detected for %s: %s\n", 
                 address.ToString(), reason);
    }
    
    // Update total volume
    for (const auto& output : tx.vout) {
        score.totalVolume += output.nValue;
    }
    
    score.lastUpdated = GetTime();
    
    // Write back
    std::string dbKey = std::string(1, DB_REPUTATION) + 
                       std::string((char*)score.address.begin(), 20);
    if (database.GetDB().Write(dbKey, score)) {
        // Maintain the reputation index (clause 2.49).
        IndexReputationAddress(database, address);
    }
}

int64_t ReputationSystem::GetVotingPower(const uint160& address) {
    // Base voting power is 1
    int64_t power = 1;
    
    // Get voter's reputation
    ReputationScore voterScore;
    if (GetReputation(address, voterScore)) {
        // Positive reputation increases voting power
        if (voterScore.score > 0) {
            power += voterScore.score / 1000; // +1 power per 1000 reputation
        }
        
        // Negative reputation decreases voting power
        if (voterScore.score < 0) {
            power += voterScore.score / 500; // Faster decrease for negative
        }
    }
    
    // Minimum power is 0 (can't vote)
    if (power < 0) power = 0;
    
    // Maximum power is 10
    if (power > 10) power = 10;
    
    return power;
}

std::vector<uint160> ReputationSystem::GetLowReputationAddresses(int64_t threshold) {
    std::vector<uint160> result;

    // Clause 2.49: enumerate the addresses that have reputation records via the
    // maintained reputation index, and return those at or below the threshold.
    std::vector<std::string> keys = database.ListKeysWithPrefix(REP_INDEX_PREFIX);
    for (const std::string& key : keys) {
        if (key.size() <= REP_INDEX_PREFIX.size()) {
            continue;
        }
        std::string addrStr = key.substr(REP_INDEX_PREFIX.size());
        uint160 address;
        address.SetHex(addrStr);
        if (address.IsNull()) {
            continue;
        }

        ReputationScore score;
        if (GetReputation(address, score)) {
            if (score.score <= threshold) {
                result.push_back(address);
            }
        }
    }

    return result;
}

bool ReputationSystem::DetectSuspiciousPattern(const CTransaction& tx, std::string& reason) {
    // Detect mixer-like behavior
    if (PatternDetector::DetectMixerPattern(tx)) {
        reason = "Mixer-like transaction pattern";
        return true;
    }
    
    // Detect dusting
    if (PatternDetector::DetectDusting(tx)) {
        reason = "Dusting attack pattern";
        return true;
    }
    
    return false;
}

int64_t ReputationSystem::CalculateScoreChange(int64_t voteValue, int64_t votingPower) {
    // Score change = vote value * voting power
    // Vote value is -100 to +100
    // Voting power is 0 to 10
    // So score change is -1000 to +1000
    return voteValue * votingPower;
}

void ReputationSystem::ApplyDecay(ReputationScore& score, int64_t currentTime) {
    if (score.lastUpdated == 0) {
        return;
    }
    
    // Apply time decay: 1% per 30 days
    int64_t timeDiff = currentTime - score.lastUpdated;
    int64_t days = timeDiff / (24 * 60 * 60);
    
    if (days > 30) {
        double decayFactor = 0.99; // 1% decay
        int64_t periods = days / 30;
        
        for (int64_t i = 0; i < periods; i++) {
            score.score = static_cast<int64_t>(score.score * decayFactor);
        }
    }
}

// PatternDetector implementations
void PatternDetector::RecordAddressActivity(const uint160& address, int blockHeight, CVMDatabase& db) {
    std::string key = TX_HISTORY_PREFIX + address.ToString();

    std::vector<int64_t> heights;
    std::vector<uint8_t> data;
    if (db.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            ss >> heights;
        } catch (const std::exception&) {
            heights.clear();
        }
    }

    heights.push_back(static_cast<int64_t>(blockHeight));

    CDataStream out(SER_DISK, CLIENT_VERSION);
    out << heights;
    std::vector<uint8_t> serialized(out.begin(), out.end());
    db.WriteGeneric(key, serialized);
}

bool PatternDetector::DetectRapidFire(const uint160& address, int blockHeight, CVMDatabase& db) {
    // Clause 2.14: consult the address's transaction-history index and return
    // true when its activity matches the rapid-fire pattern (at least
    // kRapidFireMinTxs transactions within a window of kRapidFireWindowBlocks
    // blocks, at or before the supplied block height).
    std::string key = TX_HISTORY_PREFIX + address.ToString();

    std::vector<uint8_t> data;
    if (!db.ReadGeneric(key, data)) {
        return false;
    }

    std::vector<int64_t> heights;
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> heights;
    } catch (const std::exception&) {
        return false;
    }

    // Only consider activity that has already occurred (at or before the height
    // being evaluated).
    heights.erase(std::remove_if(heights.begin(), heights.end(),
                                 [blockHeight](int64_t h) { return h > blockHeight; }),
                  heights.end());

    if (heights.size() < kRapidFireMinTxs) {
        return false;
    }

    std::sort(heights.begin(), heights.end());

    // Sliding window: any run of kRapidFireMinTxs transactions spanning no more
    // than kRapidFireWindowBlocks blocks is rapid-fire activity.
    for (size_t i = 0; i + kRapidFireMinTxs - 1 < heights.size(); ++i) {
        int64_t span = heights[i + kRapidFireMinTxs - 1] - heights[i];
        if (span <= kRapidFireWindowBlocks) {
            return true;
        }
    }

    return false;
}

bool PatternDetector::DetectMixerPattern(const CTransaction& tx) {
    // Mixer pattern: many inputs, many outputs, similar amounts
    if (tx.vin.size() < 3 || tx.vout.size() < 3) {
        return false;
    }
    
    // Check if outputs have similar values (within 10%)
    if (tx.vout.size() > 1) {
        CAmount firstValue = tx.vout[0].nValue;
        int similarCount = 0;
        
        for (size_t i = 1; i < tx.vout.size(); i++) {
            CAmount diff = abs(tx.vout[i].nValue - firstValue);
            CAmount threshold = firstValue / 10; // 10%
            
            if (diff < threshold) {
                similarCount++;
            }
        }
        
        // If more than 50% of outputs are similar, it's suspicious
        if (similarCount > static_cast<int>(tx.vout.size() / 2)) {
            return true;
        }
    }
    
    return false;
}

bool PatternDetector::DetectDusting(const CTransaction& tx) {
    // Dusting: sending very small amounts to many addresses
    int dustCount = 0;
    CAmount dustThreshold = 1000; // 0.00001 CAS (adjust as needed)
    
    for (const auto& output : tx.vout) {
        if (output.nValue < dustThreshold) {
            dustCount++;
        }
    }
    
    // If more than 50% of outputs are dust, it's a dusting attack
    if (dustCount > static_cast<int>(tx.vout.size() / 2) && dustCount > 2) {
        return true;
    }
    
    return false;
}

bool PatternDetector::DetectExchangePattern(const uint160& address, CVMDatabase& db) {
    // Clause 2.48: consult the address's committed reputation record and return
    // true when its transaction volume / count matches the exchange pattern.
    ReputationSystem rep(db);
    ReputationScore score;
    if (!rep.GetReputation(address, score)) {
        return false;
    }

    if (score.totalTransactions >= kExchangeTxThreshold) {
        return true;
    }
    if (score.totalVolume >= kExchangeVolumeThreshold) {
        return true;
    }
    // The reputation write path classifies very active addresses as "exchange".
    if (score.category == "exchange") {
        return true;
    }

    return false;
}

} // namespace CVM

