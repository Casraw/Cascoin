// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trustgraph.h>
#include <cvm/cvmdb.h>
#include <util.h>
#include <tinyformat.h>
#include <streams.h>
#include <algorithm>
#include <cmath>

namespace CVM {

// Global configuration
WoTConfig g_wotConfig;

TrustGraph::TrustGraph(CVMDatabase& db) : database(db) {}

bool TrustGraph::AddTrustEdge(
    const uint160& from,
    const uint160& to,
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
    
    // Create trust edge
    TrustEdge edge;
    edge.fromAddress = from;
    edge.toAddress = to;
    edge.trustWeight = weight;
    edge.timestamp = GetTime();
    edge.bondAmount = bondAmount;
    edge.bondTxHash = bondTx;
    edge.slashed = false;
    edge.reason = reason;
    
    // Store in database
    // Key: "trust_" + from + "_" + to
    std::string key = "trust_" + from.ToString() + "_" + to.ToString();
    
    // Serialize edge
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << edge;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    if (!database.WriteGeneric(key, data)) {
        LogPrintf("TrustGraph: Failed to write trust edge to database\n");
        return false;
    }
    
    // Also store in reverse index for incoming trust queries
    std::string reverseKey = "trust_in_" + to.ToString() + "_" + from.ToString();
    if (!database.WriteGeneric(reverseKey, data)) {
        LogPrintf("TrustGraph: Failed to write reverse trust edge to database\n");
        return false;
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Added edge %s -> %s: %d (bond: %d)\n",
             from.ToString(), to.ToString(), weight, bondAmount);
    
    return true;
}

bool TrustGraph::GetTrustEdge(const uint160& from, const uint160& to, TrustEdge& edge) const {
    std::string key = "trust_" + from.ToString() + "_" + to.ToString();
    std::vector<uint8_t> data;
    
    if (!database.ReadGeneric(key, data)) {
        return false; // Not found
    }
    
    // Deserialize edge
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> edge;
    } catch (const std::exception& e) {
        LogPrintf("TrustGraph: Failed to deserialize trust edge: %s\n", e.what());
        return false;
    }
    
    return true;
}

std::vector<TrustEdge> TrustGraph::GetOutgoingTrust(const uint160& from) const {
    std::vector<TrustEdge> edges;
    
    // Search for all keys with prefix "trust_{from}_"
    std::string prefix = "trust_" + from.ToString() + "_";
    std::vector<std::string> keys = database.ListKeysWithPrefix(prefix);
    
    for (const std::string& key : keys) {
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            try {
                TrustEdge edge;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> edge;
                edges.push_back(edge);
            } catch (const std::exception& e) {
                LogPrintf("TrustGraph: Failed to deserialize edge for key %s: %s\n", 
                         key, e.what());
            }
        }
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d outgoing trust edges from %s\n", 
             edges.size(), from.ToString());
    return edges;
}

std::vector<TrustEdge> TrustGraph::GetIncomingTrust(const uint160& to) const {
    std::vector<TrustEdge> edges;
    
    // Search for all keys with prefix "trust_in_{to}_"
    std::string prefix = "trust_in_" + to.ToString() + "_";
    std::vector<std::string> keys = database.ListKeysWithPrefix(prefix);
    
    for (const std::string& key : keys) {
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            try {
                TrustEdge edge;
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ss >> edge;
                edges.push_back(edge);
            } catch (const std::exception& e) {
                LogPrintf("TrustGraph: Failed to deserialize edge for key %s: %s\n",
                         key, e.what());
            }
        }
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d incoming trust edges to %s\n",
             edges.size(), to.ToString());
    return edges;
}

double TrustGraph::GetWeightedReputation(
    const uint160& viewer,
    const uint160& target,
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
    
    // Find all trust paths from viewer to target
    std::vector<TrustPath> paths = FindTrustPaths(viewer, target, maxDepth);
    
    if (paths.empty()) {
        // No trust path found - use unweighted global reputation
        // (similar to current system)
        std::vector<TrustEdge> incoming = GetIncomingTrust(target);
        if (incoming.empty()) {
            return 0.0;
        }
        
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
    
    // Calculate weighted reputation based on trust paths
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    
    for (const auto& path : paths) {
        // Get reputation votes at target
        std::vector<BondedVote> votes = GetVotesForAddress(target);
        
        for (const auto& vote : votes) {
            if (!vote.slashed) {
                // Weight this vote by the trust path strength
                double pathWeight = path.totalWeight;
                weightedSum += vote.voteValue * pathWeight;
                totalWeight += pathWeight;
            }
        }
    }
    
    // Return weighted average
    return totalWeight > 0.0 ? (weightedSum / totalWeight) : 0.0;
}

std::vector<TrustPath> TrustGraph::FindTrustPaths(
    const uint160& from,
    const uint160& to,
    int maxDepth
) const {
    std::vector<TrustPath> results;
    TrustPath currentPath;
    std::set<uint160> visited;
    
    // Start recursive search
    FindPathsRecursive(from, to, maxDepth, currentPath, visited, results);
    
    // Sort paths by total weight (strongest first)
    std::sort(results.begin(), results.end(),
        [](const TrustPath& a, const TrustPath& b) {
            return a.totalWeight > b.totalWeight;
        });
    
    LogPrint(BCLog::ALL, "TrustGraph: Found %d paths from %s to %s (max depth %d)\n",
             results.size(), from.ToString(), to.ToString(), maxDepth);
    
    return results;
}

void TrustGraph::FindPathsRecursive(
    const uint160& current,
    const uint160& target,
    int remainingDepth,
    TrustPath& currentPath,
    std::set<uint160>& visited,
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
    
    // Store in target's vote list
    std::string targetKey = "votes_" + vote.target.ToString() + "_" + vote.bondTxHash.ToString();
    if (!database.WriteGeneric(targetKey, data)) {
        LogPrintf("TrustGraph: Failed to write vote to target index\n");
        return false;
    }
    
    LogPrint(BCLog::ALL, "TrustGraph: Recorded bonded vote: %s -> %s: %d (bond: %d)\n",
             vote.voter.ToString(), vote.target.ToString(), 
             vote.voteValue, vote.bondAmount);
    
    return true;
}

std::vector<BondedVote> TrustGraph::GetVotesForAddress(const uint160& target) const {
    std::vector<BondedVote> votes;
    
    // Search for all keys with prefix "votes_{target}_"
    std::string prefix = "votes_" + target.ToString() + "_";
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
             votes.size(), target.ToString());
    return votes;
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
    
    // Also update in target's vote list
    std::string targetKey = "votes_" + vote.target.ToString() + "_" + voteTxHash.ToString();
    if (!database.WriteGeneric(targetKey, updatedData)) {
        LogPrintf("TrustGraph: Failed to update slashed vote in target index\n");
        return false;
    }
    
    LogPrintf("TrustGraph: Slashed vote %s (slash tx: %s)\n",
              voteTxHash.ToString(), slashTxHash.ToString());
    
    return true;
}

bool TrustGraph::CreateDispute(const DAODispute& dispute) {
    // Store dispute (placeholder)
    LogPrint(BCLog::ALL, "TrustGraph: Would store dispute in database\n");
    
    LogPrintf("TrustGraph: Created dispute %s for vote %s\n",
              dispute.disputeId.ToString(), dispute.originalVoteTx.ToString());
    
    return true;
}

bool TrustGraph::GetDispute(const uint256& disputeId, DAODispute& dispute) const {
    // Placeholder: In production, would read from database
    LogPrint(BCLog::ALL, "TrustGraph: Would read dispute from database\n");
    return false; // Not found for now
}

bool TrustGraph::VoteOnDispute(
    const uint256& disputeId,
    const uint160& daoMember,
    bool support,
    CAmount stake
) {
    // Check if DAO member
    if (!IsDAOMember(daoMember)) {
        LogPrintf("TrustGraph: %s is not a DAO member\n", daoMember.ToString());
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
    
    // Update in database (placeholder)
    LogPrint(BCLog::ALL, "TrustGraph: Would update dispute in database\n");
    
    LogPrint(BCLog::ALL, "TrustGraph: DAO vote recorded: %s on dispute %s (support: %d, stake: %d)\n",
             daoMember.ToString(), disputeId.ToString(), support, stake);
    
    return true;
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
    
    // Store updated dispute (placeholder)
    LogPrint(BCLog::ALL, "TrustGraph: Would store updated dispute in database\n");
    
    // If slash decision, slash the vote
    if (slashDecision) {
        SlashVote(dispute.originalVoteTx, disputeId);
    }
    
    LogPrintf("TrustGraph: Resolved dispute %s: %s (support: %d, oppose: %d)\n",
              disputeId.ToString(), slashDecision ? "SLASH" : "KEEP",
              totalStakeSupport, totalStakeOppose);
    
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
    
    // Count trust edges (keys starting with "trust_" but not "trust_in_")
    std::vector<std::string> trustKeys = database.ListKeysWithPrefix("trust_");
    for (const auto& key : trustKeys) {
        if (key.find("trust_in_") == std::string::npos) {
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

bool TrustGraph::IsDAOMember(const uint160& address) const {
    // In production, check DAO membership
    // For now, placeholder that checks if address has sufficient stake
    
    // Could check:
    // 1. Minimum CAS balance
    // 2. Minimum time holding
    // 3. Explicit DAO member list
    // 4. Governance token holding
    
    return true; // Placeholder: everyone can be DAO for testing
}

} // namespace CVM

