// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CLUSTERTRUSTQUERY_H
#define CASCOIN_CVM_CLUSTERTRUSTQUERY_H

#include <uint256.h>
#include <cvm/trustgraph.h>
#include <vector>

namespace CVM {

// Forward declarations
class CVMDatabase;
class WalletClusterer;
class TrustPropagator;

/**
 * ClusterTrustQuery - Query trust considering wallet clusters
 * 
 * This class provides cluster-aware trust queries, ensuring that trust
 * evaluations consider all addresses in a wallet cluster. This prevents
 * reputation gaming where malicious actors create new addresses to escape
 * negative trust scores.
 * 
 * Key Features:
 * - Cluster-aware effective trust calculation (minimum across cluster)
 * - Aggregated trust edge queries for entire clusters
 * - Negative trust detection across cluster members
 * 
 * Requirements: 4.2, 4.4
 */
class ClusterTrustQuery {
public:
    /**
     * Construct a ClusterTrustQuery
     * 
     * @param db Reference to CVM database
     * @param clusterer Reference to wallet clustering system
     * @param trustGraph Reference to trust graph
     * @param propagator Reference to trust propagator
     */
    ClusterTrustQuery(CVMDatabase& db, WalletClusterer& clusterer, 
                      TrustGraph& trustGraph, TrustPropagator& propagator);
    
    ~ClusterTrustQuery() = default;
    
    /**
     * Get effective trust score for an address (cluster-aware)
     * 
     * Returns the minimum trust score across all cluster members.
     * This ensures that a scammer cannot escape negative reputation
     * by using a different address from the same wallet.
     * 
     * Algorithm:
     * 1. Get all addresses in the target's wallet cluster
     * 2. Calculate trust score for each cluster member
     * 3. Return the minimum score (most conservative)
     * 
     * @param target Address to evaluate
     * @param viewer Optional viewer for personalized trust (if empty, uses global view)
     * @return Effective trust score (minimum across cluster)
     * 
     * Requirements: 4.2, 4.4
     */
    double GetEffectiveTrust(const uint160& target, const uint160& viewer = uint160()) const;
    
    /**
     * Get all trust relations affecting a cluster
     * 
     * Returns the union of all direct and propagated trust edges
     * targeting any address in the wallet cluster.
     * 
     * @param address Any address in the cluster
     * @return Vector of all trust edges (direct + propagated)
     * 
     * Requirement: 3.3
     */
    std::vector<TrustEdge> GetAllClusterTrustEdges(const uint160& address) const;
    
    /**
     * Get incoming trust for entire cluster
     * 
     * Returns all trust edges where any cluster member is the target.
     * This includes both direct trust edges and propagated edges.
     * 
     * @param address Any address in the cluster
     * @return Vector of incoming trust edges
     * 
     * Requirement: 1.4
     */
    std::vector<TrustEdge> GetClusterIncomingTrust(const uint160& address) const;
    
    /**
     * Check if an address has negative trust in its cluster
     * 
     * Returns true if any address in the wallet cluster has received
     * negative trust (weight < 0). This is useful for quick checks
     * before engaging in transactions.
     * 
     * @param address Address to check
     * @return true if any cluster member has negative trust
     */
    bool HasNegativeClusterTrust(const uint160& address) const;
    
    /**
     * Get the address with the worst (lowest) trust score in a cluster
     * 
     * Useful for identifying which cluster member is dragging down
     * the effective trust score.
     * 
     * @param address Any address in the cluster
     * @param worstScore Output parameter for the worst score found
     * @return Address with the lowest trust score
     */
    uint160 GetWorstClusterMember(const uint160& address, double& worstScore) const;
    
    /**
     * Get trust score for a specific address (non-cluster-aware)
     * 
     * Helper method to calculate trust score for a single address
     * without considering cluster membership.
     * 
     * @param target Address to evaluate
     * @param viewer Optional viewer for personalized trust
     * @return Trust score for the specific address
     */
    double GetAddressTrustScore(const uint160& target, const uint160& viewer = uint160()) const;

private:
    CVMDatabase& database;
    WalletClusterer& clusterer;
    TrustGraph& trustGraph;
    TrustPropagator& propagator;
    
    /**
     * Convert a PropagatedTrustEdge to a TrustEdge for unified handling
     * 
     * @param propEdge Propagated trust edge
     * @return Equivalent TrustEdge
     */
    TrustEdge PropagatedToTrustEdge(const struct PropagatedTrustEdge& propEdge) const;
    
    /**
     * Deduplicate trust edges by (from, to) pair
     * 
     * When the same truster has both direct and propagated edges to
     * the same target, keep only the direct edge.
     * 
     * @param edges Vector of trust edges (modified in place)
     */
    void DeduplicateEdges(std::vector<TrustEdge>& edges) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_CLUSTERTRUSTQUERY_H
