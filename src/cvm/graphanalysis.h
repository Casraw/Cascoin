// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_GRAPHANALYSIS_H
#define CASCOIN_CVM_GRAPHANALYSIS_H

#include <uint256.h>
#include <cvm/cvmdb.h>
#include <cvm/trustgraph.h>
#include <cvm/trustnodeid.h>
#include <set>
#include <map>
#include <vector>

namespace CVM {

/**
 * GraphMetrics - Security metrics for trust graph analysis
 *
 * All identity-bearing fields (`address`, `cluster_members`,
 * `main_entry_point`) are wide TrustNodeId user identities so every supported
 * destination type is preserved without truncation or type erasure.
 */
struct GraphMetrics {
    TrustNodeId address;
    
    // Cluster Detection
    bool in_suspicious_cluster;
    double mutual_trust_ratio;      // 0-1, high = suspicious
    std::set<TrustNodeId> cluster_members;
    
    // Centrality Metrics
    double betweenness_centrality;  // 0-1, how often in shortest paths
    double degree_centrality;       // 0-1, ratio of connections
    double closeness_centrality;    // 0-1, average distance to others
    
    // Entry Point Analysis
    TrustNodeId main_entry_point;   // Primary connection to mainnet
    int64_t entry_point_age;        // Age of entry point account
    int nodes_through_entry;        // How many nodes through this entry
    
    GraphMetrics() : address(), in_suspicious_cluster(false), 
                     mutual_trust_ratio(0.0), betweenness_centrality(0.0),
                     degree_centrality(0.0), closeness_centrality(0.0),
                     main_entry_point(), entry_point_age(0), 
                     nodes_through_entry(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(in_suspicious_cluster);
        READWRITE(mutual_trust_ratio);
        READWRITE(betweenness_centrality);
        READWRITE(degree_centrality);
        READWRITE(closeness_centrality);
        READWRITE(main_entry_point);
        READWRITE(entry_point_age);
        READWRITE(nodes_through_entry);
    }
};

/**
 * GraphAnalyzer - Detects suspicious patterns in trust network
 * 
 * Security features:
 * - Cluster Detection: Finds tightly connected fake networks
 * - Centrality Analysis: Measures importance and isolation
 * - Entry Point Detection: Finds bridge nodes exploiting network access
 */
class GraphAnalyzer {
public:
    explicit GraphAnalyzer(CVMDatabase& db);
    
    // Cluster Detection
    std::set<TrustNodeId> DetectSuspiciousClusters();
    double CalculateMutualTrustRatio(const TrustNodeId& address);
    std::set<TrustNodeId> FindClusterMembers(const TrustNodeId& address);
    
    // Centrality Analysis
    double CalculateBetweennessCentrality(const TrustNodeId& address);
    double CalculateDegreeCentrality(const TrustNodeId& address);
    double CalculateClosenessCentrality(const TrustNodeId& address);
    
    // Entry Point Detection
    void DetectSuspiciousEntryPoints();
    TrustNodeId FindMainEntryPoint(const TrustNodeId& address);
    std::map<TrustNodeId, int> GetEntryPointUsage();
    
    // Get complete metrics for address
    GraphMetrics GetMetrics(const TrustNodeId& address);
    
    // Cache management
    void InvalidateCache();
    
private:
    CVMDatabase& database;
    TrustGraph trust_graph;
    
    // Cache for expensive calculations
    std::map<TrustNodeId, GraphMetrics> metrics_cache;
    bool cache_valid;
    
    // Helper: Get all nodes in graph (wide TrustNodeId identities; endpoints
    // are never narrowed).
    std::vector<TrustNodeId> GetAllNodes();
    
    // Helper: Check if edge exists
    bool HasEdge(const TrustNodeId& from, const TrustNodeId& to);
};

} // namespace CVM

#endif // CASCOIN_CVM_GRAPHANALYSIS_H

