// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/clustertrustquery.h>
#include <cvm/cvmdb.h>
#include <cvm/walletcluster.h>
#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <util.h>

#include <algorithm>
#include <limits>
#include <set>

namespace CVM {

//
// ClusterTrustQuery implementation
//

ClusterTrustQuery::ClusterTrustQuery(CVMDatabase& db, WalletClusterer& clust, 
                                     TrustGraph& tg, TrustPropagator& prop)
    : database(db)
    , clusterer(clust)
    , trustGraph(tg)
    , propagator(prop)
{
    LogPrint(BCLog::CVM, "ClusterTrustQuery: Initialized\n");
}

double ClusterTrustQuery::GetEffectiveTrust(const uint160& target, const uint160& viewer) const
{
    // Requirement 4.2: When `geteffectivetrust` is called, consider both direct and 
    // propagated trust edges in the calculation
    // 
    // Algorithm:
    // 1. Get all addresses in the target's wallet cluster
    // 2. Calculate trust score for each cluster member
    // 3. Return the minimum score (most conservative)
    //
    // This ensures that a scammer cannot escape negative reputation by using a 
    // different address from the same wallet.
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetEffectiveTrust for target %s (viewer: %s)\n",
             target.ToString(), viewer.IsNull() ? "global" : viewer.ToString());
    
    // Step 1: Get all addresses in the target's wallet cluster
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(target);
    
    // If no cluster found, treat as single-address cluster
    if (clusterMembers.empty()) {
        LogPrint(BCLog::CVM, "ClusterTrustQuery: No cluster found for %s, treating as single address\n",
                 target.ToString());
        clusterMembers.insert(target);
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: Found %zu cluster members for %s\n",
             clusterMembers.size(), target.ToString());
    
    // Step 2: Calculate trust score for each cluster member
    // Step 3: Track minimum score across all members
    double minScore = std::numeric_limits<double>::max();
    bool foundAnyScore = false;
    
    for (const auto& memberAddress : clusterMembers) {
        double memberScore = GetAddressTrustScore(memberAddress, viewer);
        
        LogPrint(BCLog::CVM, "ClusterTrustQuery: Member %s has trust score %.4f\n",
                 memberAddress.ToString(), memberScore);
        
        if (memberScore < minScore) {
            minScore = memberScore;
        }
        foundAnyScore = true;
    }
    
    // If no scores were found, return neutral score (0.0)
    if (!foundAnyScore) {
        LogPrint(BCLog::CVM, "ClusterTrustQuery: No trust scores found, returning 0.0\n");
        return 0.0;
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetEffectiveTrust returning minimum score %.4f for %s\n",
             minScore, target.ToString());
    
    return minScore;
}

double ClusterTrustQuery::GetAddressTrustScore(const uint160& target, const uint160& viewer) const
{
    // Calculate trust score for a single address without considering cluster membership
    // This combines:
    // 1. Direct trust edges from TrustGraph
    // 2. Propagated trust edges from TrustPropagator
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetAddressTrustScore for %s\n",
             target.ToString());
    
    // Get direct trust edges from TrustGraph
    std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(target);
    
    // Get propagated trust edges from TrustPropagator
    std::vector<PropagatedTrustEdge> propagatedEdges = propagator.GetPropagatedEdgesForAddress(target);
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: Found %zu direct edges and %zu propagated edges for %s\n",
             directEdges.size(), propagatedEdges.size(), target.ToString());
    
    // If viewer is specified, use weighted reputation from TrustGraph
    // This provides personalized trust based on the viewer's trust graph
    if (!viewer.IsNull()) {
        double weightedRep = trustGraph.GetWeightedReputation(viewer, target);
        LogPrint(BCLog::CVM, "ClusterTrustQuery: Using weighted reputation %.4f from viewer %s\n",
                 weightedRep, viewer.ToString());
        return weightedRep;
    }
    
    // For global view (no viewer), calculate aggregate score from all edges
    // Combine direct and propagated edges
    double totalWeight = 0.0;
    double totalBondWeight = 0.0;
    
    // Process direct edges
    for (const auto& edge : directEdges) {
        // Weight by bond amount (more stake = more influence)
        double bondWeight = static_cast<double>(edge.bondAmount) / COIN;
        if (bondWeight < 1.0) bondWeight = 1.0;  // Minimum weight of 1
        
        totalWeight += edge.trustWeight * bondWeight;
        totalBondWeight += bondWeight;
        
        LogPrint(BCLog::CVM, "ClusterTrustQuery: Direct edge from %s: weight=%d, bond=%.2f\n",
                 edge.fromAddress.ToString(), edge.trustWeight, bondWeight);
    }
    
    // Process propagated edges
    for (const auto& propEdge : propagatedEdges) {
        // Weight by bond amount (more stake = more influence)
        double bondWeight = static_cast<double>(propEdge.bondAmount) / COIN;
        if (bondWeight < 1.0) bondWeight = 1.0;  // Minimum weight of 1
        
        totalWeight += propEdge.trustWeight * bondWeight;
        totalBondWeight += bondWeight;
        
        LogPrint(BCLog::CVM, "ClusterTrustQuery: Propagated edge from %s: weight=%d, bond=%.2f\n",
                 propEdge.fromAddress.ToString(), propEdge.trustWeight, bondWeight);
    }
    
    // Calculate weighted average score
    double score = 0.0;
    if (totalBondWeight > 0.0) {
        score = totalWeight / totalBondWeight;
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetAddressTrustScore returning %.4f for %s\n",
             score, target.ToString());
    
    return score;
}

std::vector<TrustEdge> ClusterTrustQuery::GetAllClusterTrustEdges(const uint160& address) const
{
    // Requirement 3.3: Return all trust edges where any cluster member is the target
    // This includes both direct trust edges and propagated edges
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetAllClusterTrustEdges for %s\n",
             address.ToString());
    
    std::vector<TrustEdge> result;
    
    // Get all cluster members
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(address);
    
    // If no cluster found, treat as single-address cluster
    if (clusterMembers.empty()) {
        LogPrint(BCLog::CVM, "ClusterTrustQuery: No cluster found for %s, treating as single address\n",
                 address.ToString());
        clusterMembers.insert(address);
    }
    
    // Collect all edges for all cluster members
    for (const auto& memberAddress : clusterMembers) {
        // Get direct trust edges
        std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(memberAddress);
        for (const auto& edge : directEdges) {
            result.push_back(edge);
        }
        
        // Get propagated trust edges and convert to TrustEdge
        std::vector<PropagatedTrustEdge> propagatedEdges = propagator.GetPropagatedEdgesForAddress(memberAddress);
        for (const auto& propEdge : propagatedEdges) {
            result.push_back(PropagatedToTrustEdge(propEdge));
        }
    }
    
    // Deduplicate edges
    DeduplicateEdges(result);
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetAllClusterTrustEdges returning %zu edges\n",
             result.size());
    
    return result;
}

std::vector<TrustEdge> ClusterTrustQuery::GetClusterIncomingTrust(const uint160& address) const
{
    // Requirement 1.4: Return both direct and propagated trust edges targeting any cluster member
    // This is essentially the same as GetAllClusterTrustEdges for incoming trust
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetClusterIncomingTrust for %s\n",
             address.ToString());
    
    return GetAllClusterTrustEdges(address);
}

bool ClusterTrustQuery::HasNegativeClusterTrust(const uint160& address) const
{
    // Check if any address in the wallet cluster has received negative trust
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: HasNegativeClusterTrust for %s\n",
             address.ToString());
    
    // Get all cluster members
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(address);
    
    // If no cluster found, treat as single-address cluster
    if (clusterMembers.empty()) {
        clusterMembers.insert(address);
    }
    
    // Check each cluster member for negative trust
    for (const auto& memberAddress : clusterMembers) {
        // Check direct trust edges
        std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(memberAddress);
        for (const auto& edge : directEdges) {
            if (edge.trustWeight < 0) {
                LogPrint(BCLog::CVM, "ClusterTrustQuery: Found negative trust edge to %s (weight: %d)\n",
                         memberAddress.ToString(), edge.trustWeight);
                return true;
            }
        }
        
        // Check propagated trust edges
        std::vector<PropagatedTrustEdge> propagatedEdges = propagator.GetPropagatedEdgesForAddress(memberAddress);
        for (const auto& propEdge : propagatedEdges) {
            if (propEdge.trustWeight < 0) {
                LogPrint(BCLog::CVM, "ClusterTrustQuery: Found negative propagated trust edge to %s (weight: %d)\n",
                         memberAddress.ToString(), propEdge.trustWeight);
                return true;
            }
        }
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: No negative trust found for cluster containing %s\n",
             address.ToString());
    
    return false;
}

uint160 ClusterTrustQuery::GetWorstClusterMember(const uint160& address, double& worstScore) const
{
    // Find the cluster member with the lowest (worst) trust score
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: GetWorstClusterMember for %s\n",
             address.ToString());
    
    // Get all cluster members
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(address);
    
    // If no cluster found, treat as single-address cluster
    if (clusterMembers.empty()) {
        clusterMembers.insert(address);
    }
    
    uint160 worstMember;
    worstScore = std::numeric_limits<double>::max();
    
    for (const auto& memberAddress : clusterMembers) {
        double memberScore = GetAddressTrustScore(memberAddress);
        
        if (memberScore < worstScore) {
            worstScore = memberScore;
            worstMember = memberAddress;
        }
    }
    
    // If no members found (shouldn't happen), return the input address with score 0
    if (worstMember.IsNull()) {
        worstMember = address;
        worstScore = 0.0;
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: Worst cluster member is %s with score %.4f\n",
             worstMember.ToString(), worstScore);
    
    return worstMember;
}

TrustEdge ClusterTrustQuery::PropagatedToTrustEdge(const PropagatedTrustEdge& propEdge) const
{
    // Convert a PropagatedTrustEdge to a TrustEdge for unified handling
    
    TrustEdge edge;
    edge.fromAddress = propEdge.fromAddress;
    edge.toAddress = propEdge.toAddress;
    edge.trustWeight = propEdge.trustWeight;
    edge.timestamp = propEdge.propagatedAt;
    edge.bondAmount = propEdge.bondAmount;
    edge.bondTxHash = propEdge.sourceEdgeTx;
    edge.slashed = false;  // Propagated edges inherit slashed status from original
    edge.reason = "Propagated from " + propEdge.originalTarget.ToString();
    
    return edge;
}

void ClusterTrustQuery::DeduplicateEdges(std::vector<TrustEdge>& edges) const
{
    // Deduplicate trust edges by (from, to) pair
    // When the same truster has both direct and propagated edges to the same target,
    // keep only the direct edge (or the one with higher bond amount)
    
    if (edges.size() <= 1) {
        return;
    }
    
    // Use a map to track unique edges by (from, to) pair
    // Key: from_address + "_" + to_address
    // Value: index in edges vector
    std::map<std::string, size_t> uniqueEdges;
    std::vector<TrustEdge> deduped;
    
    for (size_t i = 0; i < edges.size(); ++i) {
        const TrustEdge& edge = edges[i];
        std::string key = edge.fromAddress.ToString() + "_" + edge.toAddress.ToString();
        
        auto it = uniqueEdges.find(key);
        if (it == uniqueEdges.end()) {
            // First time seeing this (from, to) pair
            uniqueEdges[key] = deduped.size();
            deduped.push_back(edge);
        } else {
            // Duplicate found - keep the one with higher bond amount
            TrustEdge& existing = deduped[it->second];
            if (edge.bondAmount > existing.bondAmount) {
                existing = edge;
            }
        }
    }
    
    LogPrint(BCLog::CVM, "ClusterTrustQuery: Deduplicated %zu edges to %zu unique edges\n",
             edges.size(), deduped.size());
    
    edges = std::move(deduped);
}

} // namespace CVM
