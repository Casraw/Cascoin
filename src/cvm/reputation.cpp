// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/reputation.h>
#include <cvm/cvmdb.h>
#include <cvm/migration_observability.h>
#include <streams.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <amount.h>

#include <algorithm>

namespace CVM {

// Reputation transaction marker
static const std::string REP_MARKER = "REP";
static const uint8_t REP_VERSION = 0x01;

// Generic-key prefix for the reputation primary record. This is the one
// intentional namespace rename for the migration: the old raw 'R'+<20 bytes>
// key is retired in favour of "reputation_<TNI>" where <TNI> is exactly
// TrustNodeId::ToKeyString(). The rename materially avoids accidental overlap
// with stale development raw-'R' records; no old reputation namespace is ever
// queried.
static const std::string REPUTATION_PREFIX = "reputation_";

// Generic-key prefix for the maintained reputation index (clause 2.49). Every
// identity that has a reputation record is recorded under this prefix so the
// index accessor can enumerate them without scanning every DB key.
static const std::string REP_INDEX_PREFIX = "repidx_";

// Generic-key prefix for the per-identity transaction-history index (clause
// 2.14). Stores the block heights at which the identity transacted so
// DetectRapidFire can assess the rapid-fire pattern.
static const std::string TX_HISTORY_PREFIX = "txhist_";

// Rapid-fire pattern parameters: at least kRapidFireMinTxs transactions within
// a window of kRapidFireWindowBlocks blocks constitutes rapid-fire activity.
static const size_t kRapidFireMinTxs = 5;
static const int64_t kRapidFireWindowBlocks = 3;

// Exchange pattern thresholds: a very high transaction count or volume.
static const uint64_t kExchangeTxThreshold = 10000;
static const uint64_t kExchangeVolumeThreshold = 100000ULL * COIN;

// Current typed primary key for a reputation record: "reputation_<TNI>".
static std::string ReputationKey(const TrustNodeId& node) {
    return REPUTATION_PREFIX + node.ToKeyString();
}

// Current typed transaction-history key for an identity: "txhist_<TNI>".
static std::string TxHistoryKey(const TrustNodeId& node) {
    return TX_HISTORY_PREFIX + node.ToKeyString();
}

// Maintain the reputation index: record that `node` has a reputation record.
// The identity is captured in the canonical key segment; the stored value is
// the serialized TrustNodeId so the record is self-describing.
static bool IndexReputationAddress(CVMDatabase& database, const TrustNodeId& node) {
    std::string idxKey = REP_INDEX_PREFIX + node.ToKeyString();
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << node;
    std::vector<uint8_t> value(ss.begin(), ss.end());
    if (!database.WriteGeneric(idxKey, value)) {
        // Secondary-index write failed after the primary record was written
        // (clause 5.2). Report it; a repeated idempotent UpdateReputation
        // repairs the index without duplicating logical state.
        RecordMigrationEvent(MigrationEvent::FailedIndexWrite, REP_INDEX_PREFIX,
                             node.ToKeyString());
        return false;
    }
    return true;
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

bool ReputationSystem::GetReputation(const TrustNodeId& address, ReputationScore& score) {
    // Only a strictly canonical identity may key a reputation record. An
    // invalid identity is never reinterpreted; it simply has no record.
    std::string err;
    if (!ValidateCanonicalTrustNode(address, err)) {
        score = ReputationScore();
        score.address = address;
        score.category = "normal";
        return false;
    }

    std::vector<uint8_t> data;
    if (!database.ReadGeneric(ReputationKey(address), data)) {
        // Return default score for new identities.
        score = ReputationScore();
        score.address = address;
        score.category = "normal";
        return false;
    }

    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> score;
        if (!ss.empty()) {
            // Malformed current record: reject rather than partially apply.
            RecordMigrationEvent(MigrationEvent::MalformedRecord, REPUTATION_PREFIX,
                                 address.ToKeyString());
            LogPrintf("ERROR: Trailing bytes in ReputationScore record for %s\n",
                      address.ToKeyString());
            score = ReputationScore();
            score.address = address;
            score.category = "normal";
            return false;
        }
    } catch (const std::exception&) {
        RecordMigrationEvent(MigrationEvent::MalformedRecord, REPUTATION_PREFIX,
                             address.ToKeyString());
        LogPrintf("ERROR: Failed to deserialize ReputationScore record for %s\n",
                  address.ToKeyString());
        score = ReputationScore();
        score.address = address;
        score.category = "normal";
        return false;
    }

    return true;
}

bool ReputationSystem::UpdateReputation(const TrustNodeId& address, const ReputationScore& score) {
    std::string err;
    if (!ValidateCanonicalTrustNode(address, err)) {
        LogPrintf("Rejecting reputation update for noncanonical identity: %s\n", err);
        return false;
    }

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << score;
    std::vector<uint8_t> data(ss.begin(), ss.end());

    // Primary write precedes the secondary-index write (clause 5.2).
    if (!database.WriteGeneric(ReputationKey(address), data)) {
        return false;
    }
    // Maintain the reputation index (clause 2.49).
    IndexReputationAddress(database, address);
    return true;
}

bool ReputationSystem::ApplyVote(const TrustNodeId& voterAddress,
                                const ReputationVoteTx& vote,
                                int64_t timestamp) {
    // Bugfix 2.18: a reputation vote must be attributed to a resolved voter.
    // Reject votes from a null/zero voter identity (an unresolved voter) rather
    // than silently applying them to the zero address.
    if (voterAddress.data.IsNull()) {
        LogPrintf("Rejecting reputation vote from unresolved (zero) voter identity\n");
        return false;
    }

    std::string error;
    if (!ValidateCanonicalTrustNode(voterAddress, error)) {
        LogPrintf("Rejecting reputation vote from noncanonical voter: %s\n", error);
        return false;
    }

    if (!vote.IsValid(error)) {
        LogPrintf("Invalid reputation vote: %s\n", error);
        return false;
    }

    // The vote target must also be a strictly canonical identity.
    if (!ValidateCanonicalTrustNode(vote.targetAddress, error)) {
        LogPrintf("Rejecting reputation vote for noncanonical target: %s\n", error);
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

    // Ensure the record self-describes its (typed) identity and persist it,
    // keyed by the vote target, maintaining the index in one place.
    score.address = vote.targetAddress;
    return UpdateReputation(vote.targetAddress, score);
}

void ReputationSystem::UpdateBehaviorScore(const TrustNodeId& address,
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
                 address.ToKeyString(), reason);
    }
    
    // Update total volume
    for (const auto& output : tx.vout) {
        score.totalVolume += output.nValue;
    }
    
    score.lastUpdated = GetTime();

    // Persist the updated record keyed by the identity, maintaining the index.
    score.address = address;
    UpdateReputation(address, score);
}

int64_t ReputationSystem::GetVotingPower(const TrustNodeId& address) {
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

std::vector<TrustNodeId> ReputationSystem::GetLowReputationAddresses(int64_t threshold) {
    std::vector<TrustNodeId> result;

    // Clause 2.49: enumerate the identities that have reputation records via the
    // maintained reputation index, and return those at or below the threshold.
    // The identity is recovered by strictly parsing the canonical key segment;
    // malformed/noncanonical segments are skipped, never reinterpreted.
    std::vector<std::string> keys = database.ListKeysWithPrefix(REP_INDEX_PREFIX);
    for (const std::string& key : keys) {
        if (key.size() <= REP_INDEX_PREFIX.size()) {
            continue;
        }
        std::string segment = key.substr(REP_INDEX_PREFIX.size());

        TrustNodeId node;
        std::string err;
        if (!ParseKeyStringToTrustNode(segment, node, err)) {
            RecordMigrationEvent(MigrationEvent::MalformedRecord, REP_INDEX_PREFIX, key);
            LogPrintf("WARN: skipping malformed reputation index key \"%s\": %s\n",
                      key, err);
            continue;
        }

        ReputationScore score;
        if (GetReputation(node, score)) {
            if (score.score <= threshold) {
                result.push_back(node);
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

// ---------------------------------------------------------------------------
// Thin uint160 wrappers (legacy P2PKH callers). Each zero-extends the bare
// uint160 into a TrustNodeId{P2PKH} and forwards to the wide overload. Wave 8
// removes the remaining uint160 bridging at the RPC/block-processing sites.
// ---------------------------------------------------------------------------

bool ReputationSystem::GetReputation(const uint160& address, ReputationScore& score) {
    return GetReputation(TrustNodeId::FromLegacyUint160(address), score);
}

bool ReputationSystem::UpdateReputation(const uint160& address, const ReputationScore& score) {
    return UpdateReputation(TrustNodeId::FromLegacyUint160(address), score);
}

bool ReputationSystem::ApplyVote(const uint160& voterAddress,
                                const ReputationVoteTx& vote,
                                int64_t timestamp) {
    return ApplyVote(TrustNodeId::FromLegacyUint160(voterAddress), vote, timestamp);
}

void ReputationSystem::UpdateBehaviorScore(const uint160& address,
                                          const CTransaction& tx,
                                          int blockHeight) {
    UpdateBehaviorScore(TrustNodeId::FromLegacyUint160(address), tx, blockHeight);
}

int64_t ReputationSystem::GetVotingPower(const uint160& address) {
    return GetVotingPower(TrustNodeId::FromLegacyUint160(address));
}

// PatternDetector implementations
void PatternDetector::RecordAddressActivity(const TrustNodeId& address, int blockHeight, CVMDatabase& db) {
    std::string key = TxHistoryKey(address);

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

bool PatternDetector::DetectRapidFire(const TrustNodeId& address, int blockHeight, CVMDatabase& db) {
    // Clause 2.14: consult the identity's transaction-history index and return
    // true when its activity matches the rapid-fire pattern (at least
    // kRapidFireMinTxs transactions within a window of kRapidFireWindowBlocks
    // blocks, at or before the supplied block height).
    std::string key = TxHistoryKey(address);

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

bool PatternDetector::DetectExchangePattern(const TrustNodeId& address, CVMDatabase& db) {
    // Clause 2.48: consult the identity's committed reputation record and return
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

// Thin uint160 wrappers for PatternDetector (legacy P2PKH callers).
bool PatternDetector::DetectRapidFire(const uint160& address, int blockHeight, CVMDatabase& db) {
    return DetectRapidFire(TrustNodeId::FromLegacyUint160(address), blockHeight, db);
}

bool PatternDetector::DetectExchangePattern(const uint160& address, CVMDatabase& db) {
    return DetectExchangePattern(TrustNodeId::FromLegacyUint160(address), db);
}

void PatternDetector::RecordAddressActivity(const uint160& address, int blockHeight, CVMDatabase& db) {
    RecordAddressActivity(TrustNodeId::FromLegacyUint160(address), blockHeight, db);
}

} // namespace CVM
