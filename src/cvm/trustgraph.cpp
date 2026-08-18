// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trustgraph.h>
#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/reward_distributor.h>
#include <cvm/commit_reveal.h>
#include <util.h>
#include <tinyformat.h>
#include <streams.h>
#include <amount.h>
#include <chain.h>
#include <algorithm>
#include <cctype>
#include <cmath>

extern CChain chainActive;

namespace CVM {

// Global configuration
WoTConfig g_wotConfig;

namespace {

//
// Trust-edge DB key helpers (task 8.2)
// ------------------------------------
// Keys embed one node identifier per segment. Two shapes coexist for backward
// compatibility (see IsLegacyKeySegment in the header):
//   - Current v2 (tagged):  "trust_<from.ToKeyString()>_<to.ToKeyString()>"
//                           "trust_in_<to.ToKeyString()>_<from.ToKeyString()>"
//   - Legacy v1 (40-hex):   "trust_<from.hex40>_<to.hex40>"
//                           "trust_in_<to.hex40>_<from.hex40>"
// New writes always use the tagged v2 shape; reads accept BOTH so legacy
// records written before this change still resolve (Property 6).
//

//! Canonical v2 forward-edge key: "trust_<from>_<to>".
std::string ForwardEdgeKey(const TrustNodeId& from, const TrustNodeId& to)
{
    return "trust_" + from.ToKeyString() + "_" + to.ToKeyString();
}

//! Canonical v2 reverse-index key: "trust_in_<to>_<from>".
std::string ReverseEdgeKey(const TrustNodeId& from, const TrustNodeId& to)
{
    return "trust_in_" + to.ToKeyString() + "_" + from.ToKeyString();
}

//! Canonical v2 outgoing-edge enumeration prefix: "trust_<from>_".
std::string OutgoingPrefix(const TrustNodeId& from)
{
    return "trust_" + from.ToKeyString() + "_";
}

//! Canonical v2 incoming-edge enumeration prefix: "trust_in_<to>_".
std::string IncomingPrefix(const TrustNodeId& to)
{
    return "trust_in_" + to.ToKeyString() + "_";
}

//! Legacy v1 forward-edge key (40-hex segments), for back-compat reads.
std::string LegacyForwardEdgeKey(const uint160& from, const uint160& to)
{
    return "trust_" + from.ToString() + "_" + to.ToString();
}

//! Legacy v1 outgoing-edge enumeration prefix (40-hex), for back-compat reads.
std::string LegacyOutgoingPrefix(const uint160& from)
{
    return "trust_" + from.ToString() + "_";
}

//! Legacy v1 incoming-edge enumeration prefix (40-hex), for back-compat reads.
std::string LegacyIncomingPrefix(const uint160& to)
{
    return "trust_in_" + to.ToString() + "_";
}

//! Extract the first node key segment from a trust key.
//! Handles both forward keys ("trust_<a>_<b>") and reverse-index keys
//! ("trust_in_<a>_<b>"). Node segments never contain '_' (legacy segments are
//! hex; tagged segments use '-'), so splitting on '_' is unambiguous.
//! Returns an empty string if the key does not have a recognised trust prefix.
std::string FirstNodeSegment(const std::string& key)
{
    size_t start;
    if (key.rfind("trust_in_", 0) == 0) {
        start = 9; // strlen("trust_in_")
    } else if (key.rfind("trust_", 0) == 0) {
        start = 6; // strlen("trust_")
    } else {
        return std::string();
    }
    const size_t end = key.find('_', start);
    if (end == std::string::npos) {
        return key.substr(start);
    }
    return key.substr(start, end - start);
}

//! Deserialize a TrustEdge record, selecting the legacy v1 or the canonical v2
//! path from the shape of the DB key (see IsLegacyKeySegment). A legacy key uses
//! 40-hex uint160 segments and is read via UnserializeLegacyV1 (which migrates
//! from/to to TrustNodeId{P2PKH, zero-extended}); a tagged key is read via the
//! canonical v2 operator (ss >> edge).
bool DeserializeTrustEdgeForKey(const std::string& key,
                                const std::vector<uint8_t>& data,
                                TrustEdge& edge)
{
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        if (IsLegacyKeySegment(FirstNodeSegment(key))) {
            edge.UnserializeLegacyV1(ss);
        } else {
            ss >> edge;
        }
        return true;
    } catch (const std::exception& e) {
        LogPrintf("TrustGraph: Failed to deserialize trust edge for key %s: %s\n",
                  key, e.what());
        return false;
    }
}

} // anonymous namespace

bool IsLegacyKeySegment(const std::string& segment)
{
    // A legacy uint160 segment is exactly 40 hexadecimal characters and has no
    // separator. A tagged TrustNodeId segment is "<type:02x>-<64hex>" (67 chars
    // containing a '-'), so the length check alone already discriminates the
    // two, and the hex check rejects any other 40-char shape defensively.
    if (segment.size() != 40) {
        return false;
    }
    for (const char c : segment) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool IsCanonicalForwardEdgeKey(const std::string& key)
{
    // Must be a "trust_" key. Prefer prefix checks (rfind(prefix, 0) == 0)
    // over find(...) != npos to avoid substring false positives.
    if (key.rfind("trust_", 0) != 0) {
        return false;
    }
    // Exclude the reverse index keys ("trust_in_...").
    if (key.find("trust_in_") != std::string::npos) {
        return false;
    }
    // Exclude foreign propagator records ("trust_prop_..." and
    // "trust_prop_idx_..."), which share the "trust_" prefix.
    if (key.rfind("trust_prop_", 0) == 0) {
        return false;
    }
    return true;
}

TrustGraph::TrustGraph(CVMDatabase& db) 
    : database(db)
    , rewardDistributor(std::make_unique<RewardDistributor>(db, g_wotConfig))
    , commitRevealManager(std::make_unique<CommitRevealManager>(db, g_wotConfig))
{
}

TrustGraph::~TrustGraph() = default;

bool TrustGraph::AddTrustEdge(
    const TrustNodeId& from,
    const TrustNodeId& to,
    int16_t weight,
    CAmount bondAmount,
    const uint256& bondTx,
    const std::string& reason
) {
    // Validate weight range
    if (weight < -100 || weight > 100) {
        LogPrintf("TrustGraph: Invalid trust weight %d\n", weight);
        return false;
    }
    
    // Check bond requirement
    CAmount requiredBond = CalculateRequiredBond(weight);
    if (bondAmount < requiredBond) {
        LogPrintf("TrustGraph: Insufficient bond: have %d, need %d\n", 
                  bondAmount, requiredBond);
        return false;
    }

    // Create trust edge keyed by the wide TrustNodeId identifiers directly, so
    // P2WSH / quantum destinations are stored losslessly.
    TrustEdge edge;
    edge.fromAddress = from;
    edge.toAddress = to;
    edge.trustWeight = weight;
    edge.timestamp = GetTime();
    edge.bondAmount = bondAmount;
    edge.bondTxHash = bondTx;
    edge.slashed = false;
    edge.reason = reason;
    
    // Store in database under the canonical v2 (tagged) key:
    //   "trust_" + from.ToKeyString() + "_" + to.ToKeyString()
    std::string key = ForwardEdgeKey(from, to);
    
    // Serialize edge (canonical v2 layout via ADD_SERIALIZE_METHODS)
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << edge;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    if (!database.WriteGeneric(key, data)) {
        LogPrintf("TrustGraph: Failed to write trust edge to database\n");
        return false;
    }
    
    // Also store in reverse index for incoming trust queries
    std::string reverseKey = ReverseEdgeKey(from, to);
    if (!database.WriteGeneric(reverseKey, data)) {
        LogPrintf("TrustGraph: Failed to write reverse trust edge to database\n");
        return false;
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Added edge %s -> %s: %d (bond: %d)\n",
             from.ToKeyString(), to.ToKeyString(), weight, bondAmount);
    
    return true;
}

bool TrustGraph::AddTrustEdge(
    const uint160& from,
    const uint160& to,
    int16_t weight,
    CAmount bondAmount,
    const uint256& bondTx,
    const std::string& reason
) {
    // Legacy P2PKH wrapper: a bare uint160 is always a legacy P2PKH-shaped
    // identifier, so forward to the TrustNodeId overload with zero-extended ids.
    return AddTrustEdge(TrustNodeId::FromLegacyUint160(from),
                        TrustNodeId::FromLegacyUint160(to),
                        weight, bondAmount, bondTx, reason);
}

bool TrustGraph::GetTrustEdge(const TrustNodeId& from, const TrustNodeId& to, TrustEdge& edge) const {
    // Prefer the canonical v2 (tagged) key. Fall back to the legacy v1 (40-hex)
    // key so edges written before this change still resolve (Property 6). The
    // legacy fallback is only meaningful for uint160-shaped (P2PKH) nodes.
    std::string key = ForwardEdgeKey(from, to);
    std::vector<uint8_t> data;
    if (!database.ReadGeneric(key, data)) {
        if (from.type == static_cast<uint8_t>(TrustNodeType::P2PKH) &&
            to.type == static_cast<uint8_t>(TrustNodeType::P2PKH)) {
            key = LegacyForwardEdgeKey(from.ToUint160(), to.ToUint160());
            if (!database.ReadGeneric(key, data)) {
                return false; // Not found under either shape
            }
        } else {
            return false; // Not found (no legacy shape for wide identifiers)
        }
    }

    return DeserializeTrustEdgeForKey(key, data, edge);
}

bool TrustGraph::GetTrustEdge(const uint160& from, const uint160& to, TrustEdge& edge) const {
    return GetTrustEdge(TrustNodeId::FromLegacyUint160(from),
                        TrustNodeId::FromLegacyUint160(to), edge);
}

std::vector<TrustEdge> TrustGraph::GetOutgoingTrust(const TrustNodeId& from) const {
    std::vector<TrustEdge> edges;

    // Enumerate the canonical v2 (tagged) prefix. For legacy P2PKH nodes also
    // enumerate the legacy v1 (40-hex) prefix so edges written before this
    // change still resolve (Property 6). Each record is deserialized via the
    // key-shape-aware path.
    std::vector<std::string> keys = database.ListKeysWithPrefix(OutgoingPrefix(from));
    if (from.type == static_cast<uint8_t>(TrustNodeType::P2PKH)) {
        std::vector<std::string> legacyKeys = database.ListKeysWithPrefix(LegacyOutgoingPrefix(from.ToUint160()));
        keys.insert(keys.end(), legacyKeys.begin(), legacyKeys.end());
    }

    for (const std::string& key : keys) {
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            TrustEdge edge;
            if (DeserializeTrustEdgeForKey(key, data, edge)) {
                edges.push_back(edge);
            }
        }
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d outgoing trust edges from %s\n", 
             edges.size(), from.ToKeyString());
    return edges;
}

std::vector<TrustEdge> TrustGraph::GetOutgoingTrust(const uint160& from) const {
    return GetOutgoingTrust(TrustNodeId::FromLegacyUint160(from));
}

std::vector<TrustEdge> TrustGraph::GetIncomingTrust(const TrustNodeId& to) const {
    std::vector<TrustEdge> edges;

    // Enumerate the canonical v2 (tagged) reverse prefix. For legacy P2PKH nodes
    // also enumerate the legacy v1 (40-hex) reverse prefix so pre-existing edges
    // still resolve (Property 6).
    std::vector<std::string> keys = database.ListKeysWithPrefix(IncomingPrefix(to));
    if (to.type == static_cast<uint8_t>(TrustNodeType::P2PKH)) {
        std::vector<std::string> legacyKeys = database.ListKeysWithPrefix(LegacyIncomingPrefix(to.ToUint160()));
        keys.insert(keys.end(), legacyKeys.begin(), legacyKeys.end());
    }

    for (const std::string& key : keys) {
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            TrustEdge edge;
            if (DeserializeTrustEdgeForKey(key, data, edge)) {
                edges.push_back(edge);
            }
        }
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d incoming trust edges to %s\n",
             edges.size(), to.ToKeyString());
    return edges;
}

std::vector<TrustEdge> TrustGraph::GetIncomingTrust(const uint160& to) const {
    return GetIncomingTrust(TrustNodeId::FromLegacyUint160(to));
}

double TrustGraph::GetWeightedReputation(
    const TrustNodeId& viewer,
    const TrustNodeId& target,
    int maxDepth
) const {
    // If viewer is viewing themselves, return direct reputation
    if (viewer == target) {
        // Get all incoming trust edges
        std::vector<TrustEdge> incoming = GetIncomingTrust(target);
        if (incoming.empty()) {
            return 0.0; // No reputation yet
        }
        
        // Simple average for self-view
        double sum = 0.0;
        int count = 0;
        for (const auto& edge : incoming) {
            if (!edge.slashed) {
                sum += edge.trustWeight;
                count++;
            }
        }
        return count > 0 ? (sum / count) : 0.0;
    }
    
    // viewer != target: personalized, transitive reputation.
    //
    // Model: the viewer's trust of the target is the trust the viewer places in
    // each *intermediary* that has a direct opinion (trust edge) about the
    // target, multiplied by that intermediary's signed opinion of the target,
    // aggregated over every intermediary the viewer can reach through a POSITIVE
    // trust path:
    //
    //   score(viewer -> target)
    //       = sum_X [ opinion(X -> target) * confidence(viewer -> X) ]
    //         / sum_X [ confidence(viewer -> X) ]
    //
    // where confidence(viewer -> X) in (0, 1] is the strength of the strongest
    // positive trust path from the viewer to X (the product of the normalized
    // hop weights), and opinion(X -> target) is the signed weight (-100..100) of
    // X's direct trust edge to the target.
    //
    // Web-of-Trust consequences:
    //   * A trusted intermediary's NEGATIVE opinion lowers the viewer's trust of
    //     the target: if A trusts B and B has a bad opinion of C (B -> C < 0),
    //     then A's trust of C is worse.
    //   * Trust is never routed *through* a distrusted node: the viewer -> X
    //     sub-paths traverse positive edges only (FindTrustPaths drops edges with
    //     weight < 10), so an intermediary the viewer does not trust contributes
    //     nothing.
    //   * A direct edge from the viewer to the target counts at full confidence.
    //
    // This is deliberately confined to the reputation *score*. The path-finding
    // primitive (FindTrustPaths) keeps its positive-only semantics, so consumers
    // that rely on trust-path existence (e.g. HAT WoT-connection detection) are
    // unaffected.
    std::vector<TrustEdge> incoming = GetIncomingTrust(target);
    if (incoming.empty()) {
        return 0.0; // Nobody has expressed an opinion about the target yet.
    }

    // Fetch the target's bonded votes once (may be empty). BondedVote records
    // are keyed by the wide TrustNodeId target identity.
    std::vector<BondedVote> votes = GetVotesForAddress(target);

    double weightedSum = 0.0;
    double totalConfidence = 0.0;

    for (const auto& edge : incoming) {
        if (edge.slashed) {
            continue;
        }

        // How strongly does the viewer trust this intermediary (edge.fromAddress)?
        double confidence = 0.0;
        if (edge.fromAddress == viewer) {
            // Direct opinion expressed by the viewer about the target.
            confidence = 1.0;
        } else if (maxDepth > 1) {
            // Indirect: strongest positive trust path viewer -> intermediary.
            // Bound the sub-path length so the full viewer -> X -> target chain
            // stays within maxDepth.
            std::vector<TrustPath> subPaths =
                FindTrustPaths(viewer, edge.fromAddress, maxDepth - 1);
            for (const auto& sp : subPaths) {
                if (sp.totalWeight > confidence) {
                    confidence = sp.totalWeight; // strongest confidence in (0, 1]
                }
            }
        }

        // The viewer cannot reach this intermediary through positive trust, so
        // it contributes nothing (you do not inherit opinions you cannot vouch
        // for).
        if (confidence <= 0.0) {
            continue;
        }

        // Signed opinion of the intermediary about the target (-100..100).
        double opinion = static_cast<double>(edge.trustWeight);
        weightedSum += opinion * confidence;
        totalConfidence += confidence;
    }

    if (totalConfidence <= 0.0) {
        // No intermediary reachable through positive trust has an opinion about
        // the target. Fall back to the unweighted global reputation so the score
        // still reflects the crowd's view (matching the previous behaviour when
        // no trust path existed).
        double sum = 0.0;
        int count = 0;
        for (const auto& edge : incoming) {
            if (!edge.slashed) {
                sum += edge.trustWeight;
                count++;
            }
        }
        return count > 0 ? (sum / count) : 0.0;
    }

    // Incorporate bonded votes at the target, when present. Each non-slashed
    // vote is a staked opinion about the target; add it as one further sample at
    // full weight so it is comparable to a single trusted opinion.
    for (const auto& vote : votes) {
        if (!vote.slashed) {
            weightedSum += static_cast<double>(vote.voteValue);
            totalConfidence += 1.0;
        }
    }

    // Confidence-weighted average on the -100..100 scale.
    return totalConfidence > 0.0 ? (weightedSum / totalConfidence) : 0.0;
}

double TrustGraph::GetWeightedReputation(
    const uint160& viewer,
    const uint160& target,
    int maxDepth
) const {
    return GetWeightedReputation(TrustNodeId::FromLegacyUint160(viewer),
                                 TrustNodeId::FromLegacyUint160(target), maxDepth);
}

std::vector<TrustPath> TrustGraph::FindTrustPaths(
    const TrustNodeId& from,
    const TrustNodeId& to,
    int maxDepth
) const {
    std::vector<TrustPath> results;
    TrustPath currentPath;
    std::set<TrustNodeId> visited;
    
    // Start recursive search
    FindPathsRecursive(from, to, maxDepth, currentPath, visited, results);
    
    // Sort paths by total weight (strongest first)
    std::sort(results.begin(), results.end(),
        [](const TrustPath& a, const TrustPath& b) {
            return a.totalWeight > b.totalWeight;
        });
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d paths from %s to %s (max depth %d)\n",
             results.size(), from.ToKeyString(), to.ToKeyString(), maxDepth);
    
    return results;
}

std::vector<TrustPath> TrustGraph::FindTrustPaths(
    const uint160& from,
    const uint160& to,
    int maxDepth
) const {
    return FindTrustPaths(TrustNodeId::FromLegacyUint160(from),
                          TrustNodeId::FromLegacyUint160(to), maxDepth);
}

void TrustGraph::FindPathsRecursive(
    const TrustNodeId& current,
    const TrustNodeId& target,
    int remainingDepth,
    TrustPath& currentPath,
    std::set<TrustNodeId>& visited,
    std::vector<TrustPath>& results
) const {
    // Base case: reached target
    if (current == target) {
        results.push_back(currentPath);
        return;
    }
    
    // Base case: max depth reached
    if (remainingDepth <= 0) {
        return;
    }
    
    // Mark as visited
    visited.insert(current);
    
    // Get all outgoing trust edges
    std::vector<TrustEdge> outgoing = GetOutgoingTrust(current);
    
    // Explore each edge
    for (const auto& edge : outgoing) {
        // Skip if already visited (avoid cycles)
        if (visited.count(edge.toAddress) > 0) {
            continue;
        }
        
        // Skip if slashed
        if (edge.slashed) {
            continue;
        }
        
        // Skip if trust weight is too low (< 10%)
        if (edge.trustWeight < 10) {
            continue;
        }
        
        // Add to path and recurse
        currentPath.AddHop(edge.toAddress, edge.trustWeight);
        FindPathsRecursive(edge.toAddress, target, remainingDepth - 1, 
                          currentPath, visited, results);
        
        // Backtrack
        currentPath.addresses.pop_back();
        currentPath.weights.pop_back();
        if (!currentPath.weights.empty()) {
            // Recalculate total weight
            currentPath.totalWeight = 1.0;
            for (int16_t w : currentPath.weights) {
                currentPath.totalWeight *= (w / 100.0);
            }
        } else {
            currentPath.totalWeight = 1.0;
        }
    }
    
    // Unmark visited
    visited.erase(current);
}

bool TrustGraph::RecordBondedVote(const BondedVote& vote) {
    // Validate vote range
    if (vote.voteValue < -100 || vote.voteValue > 100) {
        LogPrintf("TrustGraph: Invalid vote value %d\n", vote.voteValue);
        return false;
    }
    
    // Check bond requirement
    CAmount requiredBond = CalculateRequiredBond(vote.voteValue);
    if (vote.bondAmount < requiredBond) {
        LogPrintf("TrustGraph: Insufficient bond for vote: have %d, need %d\n",
                  vote.bondAmount, requiredBond);
        return false;
    }
    
    // Store vote
    // Key: "vote_" + bondTxHash
    std::string key = "vote_" + vote.bondTxHash.ToString();
    
    // Serialize vote
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << vote;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    if (!database.WriteGeneric(key, data)) {
        LogPrintf("TrustGraph: Failed to write bonded vote to database\n");
        return false;
    }
    
    // Store in target's vote list under the canonical tagged key:
    //   "votes_" + target.ToKeyString() + "_" + bondTxHash
    std::string targetKey = "votes_" + vote.target.ToKeyString() + "_" + vote.bondTxHash.ToString();
    if (!database.WriteGeneric(targetKey, data)) {
        LogPrintf("TrustGraph: Failed to write vote to target index\n");
        return false;
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Recorded bonded vote: %s -> %s: %d (bond: %d)\n",
             vote.voter.ToKeyString(), vote.target.ToKeyString(), 
             vote.voteValue, vote.bondAmount);
    
    return true;
}

std::vector<BondedVote> TrustGraph::GetVotesForAddress(const TrustNodeId& target) const {
    std::vector<BondedVote> votes;
    
    // Search for all keys with prefix "votes_<target.ToKeyString()>_"
    std::string prefix = "votes_" + target.ToKeyString() + "_";
    std::vector<std::string> keys = database.ListKeysWithPrefix(prefix);
    
    for (const std::string& key : keys) {
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            try {
                BondedVote vote;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> vote;
                votes.push_back(vote);
            } catch (const std::exception& e) {
                LogPrintf("TrustGraph: Failed to deserialize vote for key %s: %s\n",
                         key, e.what());
            }
        }
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d votes for %s\n",
             votes.size(), target.ToKeyString());
    return votes;
}

std::vector<BondedVote> TrustGraph::GetVotesForAddress(const uint160& target) const {
    return GetVotesForAddress(TrustNodeId::FromLegacyUint160(target));
}

bool TrustGraph::SlashVote(const uint256& voteTxHash, const uint256& slashTxHash) {
    // Get original vote
    std::string key = "vote_" + voteTxHash.ToString();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        LogPrintf("TrustGraph: Vote not found: %s\n", voteTxHash.ToString());
        return false;
    }
    
    // Deserialize vote
    BondedVote vote;
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> vote;
    } catch (const std::exception& e) {
        LogPrintf("TrustGraph: Failed to deserialize vote: %s\n", e.what());
        return false;
    }
    
    // Mark as slashed
    vote.slashed = true;
    vote.slashTxHash = slashTxHash;
    
    // Serialize and update in database
    CDataStream ssOut(SER_DISK, CLIENT_VERSION);
    ssOut << vote;
    std::vector<uint8_t> updatedData(ssOut.begin(), ssOut.end());
    
    if (!database.WriteGeneric(key, updatedData)) {
        LogPrintf("TrustGraph: Failed to update slashed vote\n");
        return false;
    }
    
    // Also update in target's vote list (canonical tagged key)
    std::string targetKey = "votes_" + vote.target.ToKeyString() + "_" + voteTxHash.ToString();
    if (!database.WriteGeneric(targetKey, updatedData)) {
        LogPrintf("TrustGraph: Failed to update slashed vote in target index\n");
        return false;
    }
    
    LogPrintf("TrustGraph: Slashed vote %s (slash tx: %s)\n",
              voteTxHash.ToString(), slashTxHash.ToString());
    
    return true;
}

bool TrustGraph::CreateDispute(const DAODispute& dispute) {
    // Serialize dispute
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());

    // Primary key
    std::string key = std::string("dispute_") + dispute.disputeId.ToString();
    if (!database.WriteGeneric(key, bytes)) {
        LogPrintf("TrustGraph: Failed to store dispute %s\n", dispute.disputeId.ToString());
        return false;
    }

    // Index by vote tx for quick lookup (optional)
    std::string idx = std::string("dispute_by_vote_") + dispute.originalVoteTx.ToString();
    database.WriteGeneric(idx, bytes);

    LogPrintf("TrustGraph: Created dispute %s for vote %s\n",
              dispute.disputeId.ToString(), dispute.originalVoteTx.ToString());
    return true;
}

bool TrustGraph::GetDispute(const uint256& disputeId, DAODispute& dispute) const {
    std::string key = std::string("dispute_") + disputeId.ToString();
    std::vector<uint8_t> data;
    if (!database.ReadGeneric(key, data)) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> dispute;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("TrustGraph: Failed to deserialize dispute: %s\n", e.what());
        return false;
    }
}

bool TrustGraph::VoteOnDispute(
    const uint256& disputeId,
    const TrustNodeId& daoMember,
    bool support,
    CAmount stake
) {
    // Check if DAO member
    if (!IsDAOMember(daoMember)) {
        LogPrintf("TrustGraph: %s is not a DAO member\n", daoMember.ToKeyString());
        return false;
    }
    
    // Get dispute
    DAODispute dispute;
    if (!GetDispute(disputeId, dispute)) {
        LogPrintf("TrustGraph: Dispute not found: %s\n", disputeId.ToString());
        return false;
    }
    
    // Check if already resolved
    if (dispute.resolved) {
        LogPrintf("TrustGraph: Dispute already resolved: %s\n", disputeId.ToString());
        return false;
    }
    
    // Record vote
    dispute.daoVotes[daoMember] = support;
    dispute.daoStakes[daoMember] = stake;
    
    // Update in database
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = std::string("dispute_") + disputeId.ToString();
    if (!database.WriteGeneric(key, bytes)) {
        LogPrintf("TrustGraph: Failed to update dispute %s\n", disputeId.ToString());
        return false;
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: DAO vote recorded: %s on dispute %s (support: %d, stake: %d)\n",
             daoMember.ToKeyString(), disputeId.ToString(), support, stake);
    
    return true;
}

bool TrustGraph::VoteOnDispute(
    const uint256& disputeId,
    const uint160& daoMember,
    bool support,
    CAmount stake
) {
    return VoteOnDispute(disputeId, TrustNodeId::FromLegacyUint160(daoMember), support, stake);
}

bool TrustGraph::ResolveDispute(const uint256& disputeId) {
    // Get dispute
    DAODispute dispute;
    if (!GetDispute(disputeId, dispute)) {
        LogPrintf("TrustGraph: Dispute not found: %s\n", disputeId.ToString());
        return false;
    }
    
    // Check if already resolved
    if (dispute.resolved) {
        LogPrintf("TrustGraph: Dispute already resolved: %s\n", disputeId.ToString());
        return false;
    }
    
    // Check if minimum votes reached
    if (dispute.daoVotes.size() < (size_t)g_wotConfig.minDAOVotesForResolution) {
        LogPrintf("TrustGraph: Not enough DAO votes: have %d, need %d\n",
                  dispute.daoVotes.size(), g_wotConfig.minDAOVotesForResolution);
        return false;
    }
    
    // For commit-reveal disputes, forfeit unrevealed stakes first (Requirement 8.5, 8.6)
    if (dispute.useCommitReveal && commitRevealManager) {
        CAmount forfeited = commitRevealManager->ForfeitUnrevealedStakes(disputeId);
        if (forfeited > 0) {
            LogPrint(BCLog::CVM, "TrustGraph: Forfeited %lld from unrevealed votes\n", forfeited);
        }
    }
    
    // Calculate weighted vote
    CAmount totalStakeSupport = 0;
    CAmount totalStakeOppose = 0;
    
    for (const auto& vote : dispute.daoVotes) {
        CAmount stake = dispute.daoStakes[vote.first];
        if (vote.second) {
            totalStakeSupport += stake;
        } else {
            totalStakeOppose += stake;
        }
    }
    
    CAmount totalStake = totalStakeSupport + totalStakeOppose;
    
    // Check quorum
    // (In production, compare against total DAO stake)
    
    // Determine outcome
    bool slashDecision = totalStakeSupport > totalStakeOppose;
    
    // Update dispute
    dispute.resolved = true;
    dispute.slashDecision = slashDecision;
    dispute.resolvedTime = GetTime();
    
    // Store updated dispute
    if (!UpdateDispute(dispute)) {
        LogPrintf("TrustGraph: Failed to store resolved dispute %s\n", disputeId.ToString());
        return false;
    }
    
    // If slash decision, slash the vote
    if (slashDecision) {
        SlashVote(dispute.originalVoteTx, disputeId);
    }
    
    LogPrintf("TrustGraph: Resolved dispute %s: %s (support: %d, oppose: %d)\n",
              disputeId.ToString(), slashDecision ? "SLASH" : "KEEP",
              totalStakeSupport, totalStakeOppose);
    
    // Distribute rewards (Requirements: 9.1, 9.3)
    if (rewardDistributor && !dispute.rewardsDistributed) {
        bool rewardSuccess = false;
        
        if (slashDecision) {
            // Get the slashed bond amount from the original vote
            BondedVote originalVote;
            CAmount slashedBond = 0;
            
            if (GetDisputedVote(disputeId, originalVote)) {
                slashedBond = originalVote.bondAmount;
            }
            
            // Distribute slash rewards (Requirement 9.1)
            rewardSuccess = rewardDistributor->DistributeSlashRewards(dispute, slashedBond);
            
            if (rewardSuccess) {
                LogPrint(BCLog::CVM, "TrustGraph: Distributed slash rewards for dispute %s\n",
                         disputeId.ToString());
            } else {
                LogPrintf("TrustGraph: Failed to distribute slash rewards for dispute %s\n",
                          disputeId.ToString());
            }
        } else {
            // Get the original voter who was wrongly accused
            BondedVote originalVote;
            TrustNodeId originalVoter;
            
            if (GetDisputedVote(disputeId, originalVote)) {
                originalVoter = originalVote.voter;
            }
            
            // Distribute failed challenge rewards (Requirement 9.1)
            rewardSuccess = rewardDistributor->DistributeFailedChallengeRewards(dispute, originalVoter);
            
            if (rewardSuccess) {
                LogPrint(BCLog::CVM, "TrustGraph: Distributed failed challenge rewards for dispute %s\n",
                         disputeId.ToString());
            } else {
                LogPrintf("TrustGraph: Failed to distribute failed challenge rewards for dispute %s\n",
                          disputeId.ToString());
            }
        }
        
        // Mark rewards as distributed
        if (rewardSuccess) {
            dispute.rewardsDistributed = true;
            dispute.rewardDistributionId = disputeId;  // Use dispute ID as distribution ID
            UpdateDispute(dispute);
        }
    }
    
    return true;
}

bool TrustGraph::GetDisputedVote(const uint256& disputeId, BondedVote& vote) const {
    // Get the dispute first
    DAODispute dispute;
    if (!GetDispute(disputeId, dispute)) {
        return false;
    }
    
    // Get the original vote using the vote transaction hash
    std::string key = "vote_" + dispute.originalVoteTx.ToString();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        LogPrintf("TrustGraph: Original vote not found for dispute %s\n", disputeId.ToString());
        return false;
    }
    
    // Deserialize vote
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> vote;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("TrustGraph: Failed to deserialize original vote: %s\n", e.what());
        return false;
    }
}

bool TrustGraph::UpdateDispute(const DAODispute& dispute) {
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> bytes(ss.begin(), ss.end());
    std::string key = std::string("dispute_") + dispute.disputeId.ToString();
    
    if (!database.WriteGeneric(key, bytes)) {
        LogPrintf("TrustGraph: Failed to update dispute %s\n", dispute.disputeId.ToString());
        return false;
    }
    
    // Also update the index by vote tx
    std::string idx = std::string("dispute_by_vote_") + dispute.originalVoteTx.ToString();
    database.WriteGeneric(idx, bytes);
    
    return true;
}

std::map<std::string, uint64_t> TrustGraph::GetGraphStats() const {
    std::map<std::string, uint64_t> stats;
    
    // Count by iterating database keys
    uint64_t trustEdgeCount = 0;
    uint64_t voteCount = 0;
    uint64_t disputeCount = 0;
    uint64_t activeDisputeCount = 0;
    uint64_t slashedVoteCount = 0;
    
    // Count only canonical forward trust edges (trust_<from>_<to>), excluding
    // reverse-index keys and foreign propagator records (trust_prop_* / trust_prop_idx_*).
    std::vector<std::string> trustKeys = database.ListKeysWithPrefix("trust_");
    for (const auto& key : trustKeys) {
        if (IsCanonicalForwardEdgeKey(key)) {
            trustEdgeCount++;
        }
    }
    
    // Count votes (keys starting with "vote_")
    std::vector<std::string> voteKeys = database.ListKeysWithPrefix("vote_");
    for (const auto& key : voteKeys) {
        voteCount++;
        
        // Check if slashed
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            BondedVote vote;
            ss >> vote;
            if (vote.slashed) {
                slashedVoteCount++;
            }
        }
    }
    
    // Count disputes (keys starting with "dispute_")
    std::vector<std::string> disputeKeys = database.ListKeysWithPrefix("dispute_");
    for (const auto& key : disputeKeys) {
        disputeCount++;
        
        // Check if active
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            DAODispute dispute;
            ss >> dispute;
            if (!dispute.resolved) {
                activeDisputeCount++;
            }
        }
    }
    
    stats["total_trust_edges"] = trustEdgeCount;
    stats["total_votes"] = voteCount;
    stats["total_disputes"] = disputeCount;
    stats["active_disputes"] = activeDisputeCount;
    stats["slashed_votes"] = slashedVoteCount;
    
    return stats;
}

CAmount TrustGraph::CalculateRequiredBond(int16_t voteValue) const {
    // Minimum bond + bond per vote point
    CAmount base = g_wotConfig.minBondAmount;
    CAmount perPoint = g_wotConfig.bondPerVotePoint * std::abs(voteValue);
    return base + perPoint;
}

bool TrustGraph::IsDAOMember(const TrustNodeId& address) const {
    // DAO membership requirements:
    // 1. Minimum reputation score (70+)
    // 2. Minimum stake (100 CAS bonded)
    // 3. Active participation (voted in last 10,000 blocks)
    
    // Check reputation (typed end to end)
    ReputationSystem repSystem(const_cast<CVMDatabase&>(database));
    ReputationScore repScore;
    bool hasRep = repSystem.GetReputation(address, repScore);
    if (!hasRep || repScore.score < 70) {
        return false;
    }
    
    // Check bonded stake
    std::vector<TrustEdge> outgoing = GetOutgoingTrust(address);
    CAmount totalBonded = 0;
    for (const auto& edge : outgoing) {
        if (!edge.slashed) {
            totalBonded += edge.bondAmount;
        }
    }
    
    // Minimum 100 CAS (100 * COIN satoshis) bonded
    const CAmount MIN_DAO_STAKE = 100 * COIN;
    if (totalBonded < MIN_DAO_STAKE) {
        return false;
    }
    
    // Check recent activity (voted in last 10,000 blocks).
    // The "dao_activity_" record is an activation-era, uint160-keyed record that
    // is OUT OF SCOPE for this wave (it is not one of the converted bonded-vote /
    // DAO keys and is enumerated as a 40-hex uint160 segment elsewhere, e.g. the
    // HAT consensus DAO-member list). It is intentionally keyed by the low-20-byte
    // value so existing readers/writers stay consistent; only P2PKH members have
    // such records today.
    std::string activityKey = "dao_activity_" + address.ToUint160().ToString();
    std::vector<uint8_t> activityData;
    if (database.ReadGeneric(activityKey, activityData)) {
        // Deserialize last activity block
        if (activityData.size() >= 4) {
            int lastActivityBlock = 0;
            memcpy(&lastActivityBlock, activityData.data(), 4);
            
            // Check if active in last 10,000 blocks
            int currentHeight = ::chainActive.Height();
            if (currentHeight - lastActivityBlock > 10000) {
                return false;
            }
        }
    } else {
        // No activity record - not a DAO member
        return false;
    }
    
    return true;
}

bool TrustGraph::IsDAOMember(const uint160& address) const {
    return IsDAOMember(TrustNodeId::FromLegacyUint160(address));
}

} // namespace CVM

