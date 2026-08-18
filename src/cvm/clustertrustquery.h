// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CLUSTERTRUSTQUERY_H
#define CASCOIN_CVM_CLUSTERTRUSTQUERY_H

#include <uint256.h>
#include <cvm/trustgraph.h>
#include <cvm/trustnodeid.h>
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
 * Identities are wide TrustNodeId values carried end to end; the query never
 * narrows an identity to uint160. Thin uint160 wrappers are retained for legacy
 * P2PKH callers until Wave 8 removes the remaining RPC bridging.
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

    // --- Typed core API (wide user identities) ----------------------------

    /**
     * Get effective trust score for an identity (cluster-aware).
     *
     * Returns the minimum trust score across all cluster members. A null
     * viewer (`viewer.data.IsNull()`) selects the global view.
     * 
     * Requirements: 4.2, 4.4
     */
    double GetEffectiveTrust(const TrustNodeId& target,
                             const TrustNodeId& viewer = TrustNodeId()) const;

    /**
     * Get all trust relations affecting a cluster (direct + propagated).
     * Requirement: 3.3
     */
    std::vector<TrustEdge> GetAllClusterTrustEdges(const TrustNodeId& address) const;

    /**
     * Get incoming trust for the entire cluster.
     * Requirement: 1.4
     */
    std::vector<TrustEdge> GetClusterIncomingTrust(const TrustNodeId& address) const;

    /**
     * Check if any identity in the cluster has negative trust.
     */
    bool HasNegativeClusterTrust(const TrustNodeId& address) const;

    /**
     * Get the identity with the worst (lowest) trust score in a cluster.
     */
    TrustNodeId GetWorstClusterMember(const TrustNodeId& address, double& worstScore) const;

    /**
     * Get trust score for a specific identity (non-cluster-aware).
     */
    double GetAddressTrustScore(const TrustNodeId& target,
                                const TrustNodeId& viewer = TrustNodeId()) const;

    // --- Thin uint160 wrappers (legacy P2PKH callers) ---------------------
    //
    // Each wraps the bare uint160 as TrustNodeId{P2PKH, zero-extended} and
    // forwards to the typed overload. Wave 8 removes the remaining uint160
    // bridging at the RPC sites.

    double GetEffectiveTrust(const uint160& target, const uint160& viewer = uint160()) const;
    std::vector<TrustEdge> GetAllClusterTrustEdges(const uint160& address) const;
    std::vector<TrustEdge> GetClusterIncomingTrust(const uint160& address) const;
    bool HasNegativeClusterTrust(const uint160& address) const;
    uint160 GetWorstClusterMember(const uint160& address, double& worstScore) const;
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
