// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reputation.h>
#include <cvm/cvmdb.h>
#include <streams.h>
#include <utilstrencodings.h>
#include <hash.h>

namespace CVM {

// Reputation transaction marker
static const std::string REP_MARKER = "REP";
static const uint8_t REP_VERSION = 0x01;

// Database key for reputation scores
static const char DB_REPUTATION = 'R';

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
    
    return database.GetDB().Write(dbKey, score, true);
}

bool ReputationSystem::ApplyVote(const uint160& voterAddress, 
                                const ReputationVoteTx& vote, 
                                int64_t timestamp) {
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
    
    return database.GetDB().Write(dbKey, score);
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
    database.GetDB().Write(dbKey, score);
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
    
    // This would require iterating the database
    // For now, return empty vector
    // A real implementation would maintain an index
    
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
bool PatternDetector::DetectRapidFire(const uint160& address, int blockHeight) {
    // Would need to track transaction history
    // Simplified: return false for now
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

bool PatternDetector::DetectExchangePattern(const uint160& address) {
    // Exchange pattern: very high transaction volume
    // Would need to query reputation database
    return false;
}

} // namespace CVM

