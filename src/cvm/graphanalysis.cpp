// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/graphanalysis.h>
#include <util.h>
#include <random.h>
#include <algorithm>
#include <queue>

namespace CVM {

GraphAnalyzer::GraphAnalyzer(CVMDatabase& db) 
    : database(db), trust_graph(db), cache_valid(false) {}

std::vector<uint160> GraphAnalyzer::GetAllNodes() {
    std::set<uint160> nodes_set;
    
    std::vector<std::string> keys = database.ListKeysWithPrefix("trust_");
    
    for (const auto& key : keys) {
        if (key.find("trust_in_") != std::string::npos) continue;
        
        std::vector<uint8_t> data;
        if (database.ReadGeneric(key, data)) {
            try {
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                TrustEdge edge;
                ss >> edge;
                nodes_set.insert(edge.fromAddress);
                nodes_set.insert(edge.toAddress);
            } catch (...) {
                continue;
            }
        }
    }
    
    return std::vector<uint160>(nodes_set.begin(), nodes_set.end());
}

bool GraphAnalyzer::HasEdge(const uint160& from, const uint160& to) {
    TrustEdge edge;
    return trust_graph.GetTrustEdge(from, to, edge);
}

std::set<uint160> GraphAnalyzer::DetectSuspiciousClusters() {
    std::set<uint160> suspicious;
    std::vector<uint160> all_nodes = GetAllNodes();
    
    LogPrintf("GraphAnalyzer: Analyzing %d nodes for suspicious clusters\n", 
              all_nodes.size());
    
    for (const auto& node : all_nodes) {
        double mutual_ratio = CalculateMutualTrustRatio(node);
        
        // Threshold: 90%+ mutual trust is suspicious
        if (mutual_ratio > 0.9) {
            suspicious.insert(node);
            LogPrintf("GraphAnalyzer: SUSPICIOUS CLUSTER detected at %s (mutual ratio: %.2f)\n",
                     node.ToString(), mutual_ratio);
        }
    }
    
    LogPrintf("GraphAnalyzer: Found %d suspicious nodes\n", suspicious.size());
    return suspicious;
}

double GraphAnalyzer::CalculateMutualTrustRatio(const uint160& address) {
    std::vector<TrustEdge> outgoing = trust_graph.GetOutgoingTrust(address);
    
    if (outgoing.empty()) {
        return 0.0;
    }
    
    int mutual_count = 0;
    
    for (const auto& edge : outgoing) {
        if (HasEdge(edge.toAddress, address)) {
            mutual_count++;
        }
    }
    
    return static_cast<double>(mutual_count) / outgoing.size();
}

std::set<uint160> GraphAnalyzer::FindClusterMembers(const uint160& address) {
    std::set<uint160> cluster;
    cluster.insert(address);
    
    // BFS to find tightly connected nodes
    std::queue<uint160> to_check;
    to_check.push(address);
    
    while (!to_check.empty() && cluster.size() < 100) {  // Max cluster size
        uint160 current = to_check.front();
        to_check.pop();
        
        std::vector<TrustEdge> outgoing = trust_graph.GetOutgoingTrust(current);
        
        for (const auto& edge : outgoing) {
            if (cluster.count(edge.toAddress)) continue;
            
            // Check if this node also trusts back (mutual)
            if (HasEdge(edge.toAddress, current)) {
                cluster.insert(edge.toAddress);
                to_check.push(edge.toAddress);
            }
        }
    }
    
    return cluster;
}

double GraphAnalyzer::CalculateBetweennessCentrality(const uint160& address) {
    // Simplified: Sample-based estimation for performance
    const int SAMPLE_SIZE = 100;
    std::vector<uint160> all_nodes = GetAllNodes();
    
    if (all_nodes.size() < 3) {
        return 0.0;  // Not enough nodes
    }
    
    int paths_through = 0;
    int total_paths = 0;
    
    // Sample random node pairs
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        uint160 source = all_nodes[GetRand(all_nodes.size())];
        uint160 target = all_nodes[GetRand(all_nodes.size())];
        
        if (source == target || source == address || target == address) {
            continue;
        }
        
        // Find shortest path
        std::vector<TrustPath> paths = trust_graph.FindTrustPaths(source, target, 5);
        
        if (!paths.empty()) {
            total_paths++;
            
            // Check if address is in any path
            for (const auto& path : paths) {
                bool found = false;
                for (const auto& addr : path.addresses) {
                    if (addr == address) {
                        paths_through++;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
    }
    
    return total_paths > 0 ? static_cast<double>(paths_through) / total_paths : 0.0;
}

double GraphAnalyzer::CalculateDegreeCentrality(const uint160& address) {
    std::vector<TrustEdge> outgoing = trust_graph.GetOutgoingTrust(address);
    std::vector<TrustEdge> incoming = trust_graph.GetIncomingTrust(address);
    
    int total_connections = outgoing.size() + incoming.size();
    
    std::vector<uint160> all_nodes = GetAllNodes();
    int max_possible = all_nodes.size() - 1;  // Exclude self
    
    if (max_possible == 0) return 0.0;
    
    return static_cast<double>(total_connections) / max_possible;
}

double GraphAnalyzer::CalculateClosenessCentrality(const uint160& address) {
    // Average shortest path distance to all other nodes
    std::vector<uint160> all_nodes = GetAllNodes();
    
    if (all_nodes.size() <= 1) return 0.0;
    
    double total_distance = 0.0;
    int reachable_count = 0;
    
    for (const auto& target : all_nodes) {
        if (target == address) continue;
        
        std::vector<TrustPath> paths = trust_graph.FindTrustPaths(address, target, 5);
        
        if (!paths.empty()) {
            // Use shortest path
            int distance = paths[0].Length();
            total_distance += distance;
            reachable_count++;
        }
    }
    
    if (reachable_count == 0) return 0.0;
    
    double avg_distance = total_distance / reachable_count;
    
    // Invert: lower distance = higher centrality
    return 1.0 / (avg_distance + 1.0);
}

void GraphAnalyzer::DetectSuspiciousEntryPoints() {
    std::map<uint160, int> entry_usage = GetEntryPointUsage();
    
    for (const auto& [entry, count] : entry_usage) {
        if (count > 20) {  // More than 20 nodes through this entry
            // Check entry point age
            std::string key = "behavior_" + entry.ToString();
            std::vector<uint8_t> data;
            
            if (database.ReadGeneric(key, data)) {
                // Parse behavior metrics to get account age
                // For now, just log suspicious entry
                LogPrintf("GraphAnalyzer: SUSPICIOUS ENTRY POINT: %s (%d nodes)\n",
                         entry.ToString(), count);
            }
        }
    }
}

uint160 GraphAnalyzer::FindMainEntryPoint(const uint160& address) {
    // Find the node that provides shortest path to "mainnet"
    // Simplified: return node with highest betweenness in our paths
    
    std::vector<TrustPath> paths_to_mainnet;
    std::vector<uint160> all_nodes = GetAllNodes();
    
    // Sample some "mainnet" nodes (high degree)
    std::vector<std::pair<uint160, int>> node_degrees;
    for (const auto& node : all_nodes) {
        int degree = trust_graph.GetOutgoingTrust(node).size() + 
                    trust_graph.GetIncomingTrust(node).size();
        node_degrees.push_back({node, degree});
    }
    
    // Sort by degree
    std::sort(node_degrees.begin(), node_degrees.end(),
             [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Find paths to top connected nodes
    std::map<uint160, int> entry_count;
    
    for (size_t i = 0; i < std::min(size_t(10), node_degrees.size()); i++) {
        uint160 target = node_degrees[i].first;
        if (target == address) continue;
        
        std::vector<TrustPath> paths = trust_graph.FindTrustPaths(address, target, 5);
        
        for (const auto& path : paths) {
            if (path.addresses.size() > 1) {
                // First hop is entry point
                uint160 entry = path.addresses[1];
                entry_count[entry]++;
            }
        }
    }
    
    // Return most common entry point
    uint160 main_entry;
    int max_count = 0;
    
    for (const auto& [entry, count] : entry_count) {
        if (count > max_count) {
            max_count = count;
            main_entry = entry;
        }
    }
    
    return main_entry;
}

std::map<uint160, int> GraphAnalyzer::GetEntryPointUsage() {
    std::map<uint160, int> usage;
    std::vector<uint160> all_nodes = GetAllNodes();
    
    for (const auto& node : all_nodes) {
        uint160 entry = FindMainEntryPoint(node);
        if (!entry.IsNull()) {
            usage[entry]++;
        }
    }
    
    return usage;
}

GraphMetrics GraphAnalyzer::GetMetrics(const uint160& address) {
    // Check cache
    if (cache_valid && metrics_cache.count(address)) {
        return metrics_cache[address];
    }
    
    GraphMetrics metrics;
    metrics.address = address;
    
    // Calculate all metrics
    metrics.mutual_trust_ratio = CalculateMutualTrustRatio(address);
    metrics.in_suspicious_cluster = (metrics.mutual_trust_ratio > 0.9);
    
    if (metrics.in_suspicious_cluster) {
        metrics.cluster_members = FindClusterMembers(address);
    }
    
    metrics.betweenness_centrality = CalculateBetweennessCentrality(address);
    metrics.degree_centrality = CalculateDegreeCentrality(address);
    metrics.closeness_centrality = CalculateClosenessCentrality(address);
    
    metrics.main_entry_point = FindMainEntryPoint(address);
    
    // Cache result
    metrics_cache[address] = metrics;
    
    LogPrint(BCLog::ALL, "GraphAnalyzer: Metrics for %s: mutual=%.2f, betweenness=%.2f, degree=%.2f\n",
             address.ToString(), metrics.mutual_trust_ratio, 
             metrics.betweenness_centrality, metrics.degree_centrality);
    
    return metrics;
}

void GraphAnalyzer::InvalidateCache() {
    metrics_cache.clear();
    cache_valid = false;
}

} // namespace CVM

