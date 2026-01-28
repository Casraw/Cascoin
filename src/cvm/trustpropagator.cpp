// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trustpropagator.h>
#include <cvm/cvmdb.h>
#include <cvm/walletcluster.h>
#include <cvm/trustgraph.h>
#include <streams.h>
#include <clientversion.h>
#include <util.h>

#include <limits>
#include <functional>

namespace CVM {

// Storage key prefix for propagated trust edges
// Format: "trust_prop_{from}_{to}"
static const std::string TRUST_PROP_PREFIX = "trust_prop_";

// Index key prefix for source edge -> propagated edges lookup
// Format: "trust_prop_idx_{sourceEdgeTx}_{to}"
static const std::string TRUST_PROP_IDX_PREFIX = "trust_prop_idx_";

// Cluster trust summary key prefix
// Format: "cluster_trust_{clusterId}"
static const std::string CLUSTER_TRUST_PREFIX = "cluster_trust_";

//
// PropagatedTrustEdge helper methods
//

std::string PropagatedTrustEdge::GetStorageKey() const
{
    // Format: "trust_prop_{from}_{to}"
    return TRUST_PROP_PREFIX + fromAddress.ToString() + "_" + toAddress.ToString();
}

std::string PropagatedTrustEdge::GetIndexKey() const
{
    // Format: "trust_prop_idx_{sourceEdgeTx}_{to}"
    return TRUST_PROP_IDX_PREFIX + sourceEdgeTx.ToString() + "_" + toAddress.ToString();
}

//
// ClusterTrustSummary helper methods
//

std::string ClusterTrustSummary::GetStorageKey() const
{
    // Format: "cluster_trust_{clusterId}"
    return CLUSTER_TRUST_PREFIX + clusterId.ToString();
}

//
// TrustPropagator implementation
//

TrustPropagator::TrustPropagator(CVMDatabase& db, WalletClusterer& clust, TrustGraph& tg)
    : database(db)
    , clusterer(clust)
    , trustGraph(tg)
    , summaryCache(DEFAULT_CACHE_SIZE, ESTIMATED_SUMMARY_SIZE)  // Initialize LRU cache with 100MB limit
{
    LogPrint(BCLog::CVM, "TrustPropagator: Initialized with cache size limit %zu bytes\n", DEFAULT_CACHE_SIZE);
}

bool TrustPropagator::StorePropagatedEdge(const PropagatedTrustEdge& edge)
{
    // Requirement 5.1: Store propagated trust edges with distinct key prefix (trust_prop_)
    
    std::string key = edge.GetStorageKey();
    
    // Serialize the edge
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << edge;
    
    // Convert to vector for storage
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    bool result = database.WriteGeneric(key, data);
    
    if (result) {
        LogPrint(BCLog::CVM, "TrustPropagator: Stored propagated edge from %s to %s (source tx: %s)\n",
                 edge.fromAddress.ToString(), edge.toAddress.ToString(), 
                 edge.sourceEdgeTx.ToString().substr(0, 16));
    } else {
        LogPrintf("TrustPropagator: Failed to store propagated edge from %s to %s\n",
                  edge.fromAddress.ToString(), edge.toAddress.ToString());
    }
    
    return result;
}

bool TrustPropagator::IndexPropagatedEdge(const uint256& sourceEdgeTx, const uint160& propagatedTo)
{
    // Requirement 5.2: Maintain cluster-to-trust index for efficient lookups
    // Index format: "trust_prop_idx_{sourceEdgeTx}_{to}" -> propagatedTo address
    
    std::string key = TRUST_PROP_IDX_PREFIX + sourceEdgeTx.ToString() + "_" + propagatedTo.ToString();
    
    // Serialize the target address
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << propagatedTo;
    
    // Convert to vector for storage
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    bool result = database.WriteGeneric(key, data);
    
    if (result) {
        LogPrint(BCLog::CVM, "TrustPropagator: Indexed propagated edge for source tx %s -> %s\n",
                 sourceEdgeTx.ToString().substr(0, 16), propagatedTo.ToString());
    } else {
        LogPrintf("TrustPropagator: Failed to index propagated edge for source tx %s\n",
                  sourceEdgeTx.ToString().substr(0, 16));
    }
    
    return result;
}

bool TrustPropagator::RemoveIndexEntry(const uint256& sourceEdgeTx, const uint160& propagatedTo)
{
    std::string key = TRUST_PROP_IDX_PREFIX + sourceEdgeTx.ToString() + "_" + propagatedTo.ToString();
    
    bool result = database.EraseGeneric(key);
    
    if (result) {
        LogPrint(BCLog::CVM, "TrustPropagator: Removed index entry for source tx %s -> %s\n",
                 sourceEdgeTx.ToString().substr(0, 16), propagatedTo.ToString());
    }
    
    return result;
}

bool TrustPropagator::DeletePropagatedEdge(const uint160& fromAddress, const uint160& toAddress)
{
    std::string key = TRUST_PROP_PREFIX + fromAddress.ToString() + "_" + toAddress.ToString();
    
    bool result = database.EraseGeneric(key);
    
    if (result) {
        LogPrint(BCLog::CVM, "TrustPropagator: Deleted propagated edge from %s to %s\n",
                 fromAddress.ToString(), toAddress.ToString());
    }
    
    return result;
}

void TrustPropagator::InvalidateClusterCache(const uint160& clusterId)
{
    // Remove the cluster from the LRU cache (Requirement 7.4: Invalidate cache on cluster updates)
    if (summaryCache.Remove(clusterId)) {
        LogPrint(BCLog::CVM, "TrustPropagator: Invalidated cache for cluster %s\n",
                 clusterId.ToString());
    }
}

uint32_t TrustPropagator::PropagateTrustEdge(const TrustEdge& edge)
{
    // Delegate to the detailed version and return just the count for backward compatibility
    PropagationResult result = PropagateTrustEdgeWithResult(edge);
    return result.propagatedCount;
}

PropagationResult TrustPropagator::PropagateTrustEdgeWithResult(const TrustEdge& edge)
{
    // Requirements: 1.1, 1.2, 1.3, 7.2
    // 1.1: Identify the wallet cluster containing the target address
    // 1.2: Create propagated trust edges to all member addresses in the cluster
    // 1.3: Store them with a reference to the original trust edge
    // 7.2: Limit cluster size processing to maximum of 10,000 addresses per operation
    
    LogPrint(BCLog::CVM, "TrustPropagator: PropagateTrustEdgeWithResult from %s to %s (weight: %d)\n",
             edge.fromAddress.ToString(), edge.toAddress.ToString(), edge.trustWeight);
    
    PropagationResult result;
    
    // Requirement 1.1: Identify the wallet cluster containing the target address
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(edge.toAddress);
    
    // Requirement 1.5: If clustering fails to identify a cluster, apply trust only to specified address
    if (clusterMembers.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No cluster found for address %s, treating as single-address cluster\n",
                 edge.toAddress.ToString());
        clusterMembers.insert(edge.toAddress);
    }
    
    // Store original cluster size before any limiting
    result.originalClusterSize = static_cast<uint32_t>(clusterMembers.size());
    
    // Requirement 7.2: Limit cluster size processing to maximum of 10,000 addresses per operation
    if (clusterMembers.size() > MAX_CLUSTER_SIZE) {
        LogPrintf("TrustPropagator: Cluster size %zu exceeds MAX_CLUSTER_SIZE (%u), limiting propagation\n",
                  clusterMembers.size(), MAX_CLUSTER_SIZE);
        // Create a limited set with first MAX_CLUSTER_SIZE members
        std::set<uint160> limitedMembers;
        size_t count = 0;
        for (const auto& member : clusterMembers) {
            if (count >= MAX_CLUSTER_SIZE) break;
            limitedMembers.insert(member);
            count++;
        }
        clusterMembers = std::move(limitedMembers);
        result.wasLimited = true;
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: Propagating trust to %zu cluster members%s\n",
             clusterMembers.size(), result.wasLimited ? " (LIMITED)" : "");
    
    // Get current timestamp for propagation
    uint32_t propagationTime = static_cast<uint32_t>(GetTime());
    
    // Requirement 1.2: Create propagated trust edges to all member addresses in the cluster
    for (const auto& memberAddress : clusterMembers) {
        // Create PropagatedTrustEdge for this cluster member
        // Requirement 1.3: Store with reference to original trust edge (sourceEdgeTx)
        PropagatedTrustEdge propagatedEdge(
            edge.fromAddress,       // Original truster
            memberAddress,          // Propagated target (cluster member)
            edge.toAddress,         // Original target address
            edge.bondTxHash,        // Reference to original trust edge transaction
            edge.trustWeight,       // Inherited weight
            propagationTime,        // When propagation occurred
            edge.timestamp,         // Original edge timestamp (for conflict resolution per Req 6.4)
            edge.bondAmount         // Inherited bond amount
        );
        
        // Store the propagated edge in database (Requirement 5.1)
        if (StorePropagatedEdge(propagatedEdge)) {
            // Build index entry for source edge -> propagated edges (Requirement 5.2)
            if (IndexPropagatedEdge(edge.bondTxHash, memberAddress)) {
                result.propagatedCount++;
                LogPrint(BCLog::CVM, "TrustPropagator: Propagated edge to %s (count: %u)\n",
                         memberAddress.ToString(), result.propagatedCount);
            } else {
                // Index failed but edge was stored - log warning but continue
                // Index can be rebuilt later if needed
                LogPrintf("TrustPropagator: Warning - failed to index propagated edge to %s\n",
                          memberAddress.ToString());
                result.propagatedCount++;
            }
        } else {
            LogPrintf("TrustPropagator: Failed to store propagated edge to %s\n",
                      memberAddress.ToString());
        }
    }
    
    // Invalidate cache for the cluster since trust relations changed
    uint160 clusterId = clusterer.GetClusterForAddress(edge.toAddress);
    if (!clusterId.IsNull()) {
        InvalidateClusterCache(clusterId);
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: PropagateTrustEdgeWithResult completed - %u edges propagated%s\n",
             result.propagatedCount, result.wasLimited ? " (LIMITED)" : "");
    
    return result;
}

PropagationResult TrustPropagator::PropagateTrustEdgeBatched(const TrustEdge& edge, 
                                                             uint32_t batchSize,
                                                             std::function<bool(uint32_t, uint32_t)> callback)
{
    // Requirements: 7.2, 7.3
    // Process large clusters in batches to avoid memory issues
    
    LogPrint(BCLog::CVM, "TrustPropagator: PropagateTrustEdgeBatched from %s to %s (batch size: %u)\n",
             edge.fromAddress.ToString(), edge.toAddress.ToString(), batchSize);
    
    PropagationResult result;
    
    // Get cluster members
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(edge.toAddress);
    
    if (clusterMembers.empty()) {
        clusterMembers.insert(edge.toAddress);
    }
    
    result.originalClusterSize = static_cast<uint32_t>(clusterMembers.size());
    
    // Requirement 7.2: Limit cluster size processing
    if (clusterMembers.size() > MAX_CLUSTER_SIZE) {
        LogPrintf("TrustPropagator: Cluster size %zu exceeds MAX_CLUSTER_SIZE (%u), limiting\n",
                  clusterMembers.size(), MAX_CLUSTER_SIZE);
        std::set<uint160> limitedMembers;
        size_t count = 0;
        for (const auto& member : clusterMembers) {
            if (count >= MAX_CLUSTER_SIZE) break;
            limitedMembers.insert(member);
            count++;
        }
        clusterMembers = std::move(limitedMembers);
        result.wasLimited = true;
    }
    
    uint32_t propagationTime = static_cast<uint32_t>(GetTime());
    uint32_t totalMembers = static_cast<uint32_t>(clusterMembers.size());
    uint32_t processedInBatch = 0;
    uint32_t batchNumber = 0;
    
    // Requirement 7.3: Use batch database operations for large clusters
    for (const auto& memberAddress : clusterMembers) {
        PropagatedTrustEdge propagatedEdge(
            edge.fromAddress,
            memberAddress,
            edge.toAddress,
            edge.bondTxHash,
            edge.trustWeight,
            propagationTime,
            edge.timestamp,
            edge.bondAmount
        );
        
        if (StorePropagatedEdge(propagatedEdge)) {
            IndexPropagatedEdge(edge.bondTxHash, memberAddress);
            result.propagatedCount++;
        }
        
        processedInBatch++;
        
        // Call callback at end of each batch
        if (processedInBatch >= batchSize) {
            batchNumber++;
            if (callback) {
                bool shouldContinue = callback(result.propagatedCount, totalMembers);
                if (!shouldContinue) {
                    LogPrint(BCLog::CVM, "TrustPropagator: Batch processing stopped by callback at batch %u\n",
                             batchNumber);
                    break;
                }
            }
            processedInBatch = 0;
        }
    }
    
    // Invalidate cache
    uint160 clusterId = clusterer.GetClusterForAddress(edge.toAddress);
    if (!clusterId.IsNull()) {
        InvalidateClusterCache(clusterId);
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: PropagateTrustEdgeBatched completed - %u edges in %u batches%s\n",
             result.propagatedCount, batchNumber + 1, result.wasLimited ? " (LIMITED)" : "");
    
    return result;
}

uint32_t TrustPropagator::InheritTrustForNewMember(const uint160& newAddress, const uint160& clusterId)
{
    // Requirements: 2.1, 2.2
    // 2.1: When a new address is detected in an existing wallet cluster,
    //      propagate all existing trust edges to the new address
    // 2.2: Preserve the original trust weight, bond amount, and timestamp
    
    LogPrint(BCLog::CVM, "TrustPropagator: InheritTrustForNewMember for new address %s in cluster %s\n",
             newAddress.ToString(), clusterId.ToString());
    
    // Get all members of the cluster
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(clusterId);
    
    if (clusterMembers.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: Cluster %s has no members, nothing to inherit\n",
                 clusterId.ToString());
        return 0;
    }
    
    // Track all unique trust edges we need to propagate to the new address
    // Key: sourceEdgeTx, Value: PropagatedTrustEdge (we'll use this as template)
    std::map<uint256, PropagatedTrustEdge> edgesToInherit;
    
    // Iterate through all existing cluster members (excluding the new address)
    // and collect all trust edges targeting them
    for (const auto& existingMember : clusterMembers) {
        // Skip the new address itself
        if (existingMember == newAddress) {
            continue;
        }
        
        // Get all propagated trust edges targeting this existing member
        std::vector<PropagatedTrustEdge> existingPropEdges = GetPropagatedEdgesForAddress(existingMember);
        
        for (const auto& propEdge : existingPropEdges) {
            // Only add if we haven't seen this source edge yet
            // (same trust edge may have been propagated to multiple members)
            if (edgesToInherit.find(propEdge.sourceEdgeTx) == edgesToInherit.end()) {
                edgesToInherit[propEdge.sourceEdgeTx] = propEdge;
                LogPrint(BCLog::CVM, "TrustPropagator: Found trust edge to inherit from %s (source tx: %s)\n",
                         propEdge.fromAddress.ToString(), propEdge.sourceEdgeTx.ToString().substr(0, 16));
            }
        }
        
        // Also check direct trust edges from TrustGraph targeting this member
        // These may not have been propagated yet if they were added before clustering
        std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(existingMember);
        
        for (const auto& directEdge : directEdges) {
            // Only add if we haven't seen this source edge yet
            if (edgesToInherit.find(directEdge.bondTxHash) == edgesToInherit.end()) {
                // Create a PropagatedTrustEdge template from the direct edge
                PropagatedTrustEdge templateEdge(
                    directEdge.fromAddress,
                    existingMember,  // Will be replaced with newAddress when creating
                    directEdge.toAddress,  // Original target
                    directEdge.bondTxHash,
                    directEdge.trustWeight,
                    directEdge.timestamp,  // Preserve original timestamp (Requirement 2.2)
                    directEdge.bondAmount  // Preserve original bond amount (Requirement 2.2)
                );
                edgesToInherit[directEdge.bondTxHash] = templateEdge;
                LogPrint(BCLog::CVM, "TrustPropagator: Found direct trust edge to inherit from %s (tx: %s)\n",
                         directEdge.fromAddress.ToString(), directEdge.bondTxHash.ToString().substr(0, 16));
            }
        }
    }
    
    if (edgesToInherit.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No trust edges to inherit for new address %s\n",
                 newAddress.ToString());
        return 0;
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: Found %zu unique trust edges to inherit\n",
             edgesToInherit.size());
    
    // Get current timestamp for propagation record
    uint32_t propagationTime = static_cast<uint32_t>(GetTime());
    
    uint32_t inheritedCount = 0;
    
    // Create propagated edges for the new address
    // Requirement 2.2: Preserve original weight, bond, timestamp
    for (const auto& pair : edgesToInherit) {
        const PropagatedTrustEdge& templateEdge = pair.second;
        
        // Create new propagated edge for the new address
        PropagatedTrustEdge newPropEdge(
            templateEdge.fromAddress,      // Original truster
            newAddress,                    // New target (the new cluster member)
            templateEdge.originalTarget,   // Original target address
            templateEdge.sourceEdgeTx,     // Reference to original trust edge transaction
            templateEdge.trustWeight,      // Preserved weight (Requirement 2.2)
            propagationTime,               // When this propagation occurred
            templateEdge.originalTimestamp, // Preserved original timestamp (Requirement 2.2)
            templateEdge.bondAmount        // Preserved bond amount (Requirement 2.2)
        );
        
        // Store the propagated edge in database
        if (StorePropagatedEdge(newPropEdge)) {
            // Build index entry for source edge -> propagated edges
            if (IndexPropagatedEdge(templateEdge.sourceEdgeTx, newAddress)) {
                inheritedCount++;
                LogPrint(BCLog::CVM, "TrustPropagator: Inherited trust edge from %s to new address %s (count: %u)\n",
                         templateEdge.fromAddress.ToString(), newAddress.ToString(), inheritedCount);
            } else {
                // Index failed but edge was stored - log warning but count as success
                LogPrintf("TrustPropagator: Warning - failed to index inherited edge to %s\n",
                          newAddress.ToString());
                inheritedCount++;
            }
        } else {
            LogPrintf("TrustPropagator: Failed to store inherited edge to %s\n",
                      newAddress.ToString());
        }
    }
    
    // Invalidate cache for the cluster since trust relations changed
    if (!clusterId.IsNull()) {
        InvalidateClusterCache(clusterId);
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: InheritTrustForNewMember completed - %u edges inherited for %s\n",
             inheritedCount, newAddress.ToString());
    
    return inheritedCount;
}

bool TrustPropagator::HandleClusterMerge(const uint160& cluster1, const uint160& cluster2, 
                                         const uint160& mergedClusterId)
{
    // Requirements: 6.1, 6.2, 6.4
    // 6.1: WHEN two clusters merge THEN combine their trust relations
    // 6.2: WHEN a cluster merge occurs THEN propagate trust from both original clusters to all merged addresses
    // 6.4: IF conflicting trust edges exist after a merge THEN use the most recent edge as authoritative
    
    LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - merging cluster %s and %s into %s\n",
             cluster1.ToString().substr(0, 16), cluster2.ToString().substr(0, 16), 
             mergedClusterId.ToString().substr(0, 16));
    
    // Get members from both original clusters
    std::set<uint160> members1 = clusterer.GetClusterMembers(cluster1);
    std::set<uint160> members2 = clusterer.GetClusterMembers(cluster2);
    
    // If either cluster is empty, try to get members using the cluster ID directly
    // (the cluster ID itself might be a member)
    if (members1.empty() && !cluster1.IsNull()) {
        members1.insert(cluster1);
    }
    if (members2.empty() && !cluster2.IsNull()) {
        members2.insert(cluster2);
    }
    
    // Combine all members into the merged cluster set
    std::set<uint160> mergedMembers;
    mergedMembers.insert(members1.begin(), members1.end());
    mergedMembers.insert(members2.begin(), members2.end());
    
    LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - cluster1 has %zu members, cluster2 has %zu members, merged has %zu members\n",
             members1.size(), members2.size(), mergedMembers.size());
    
    if (mergedMembers.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - no members in either cluster, nothing to merge\n");
        return true;  // Nothing to do, but not an error
    }
    
    // Requirement 7.2: Limit cluster size processing to maximum of 10,000 addresses per operation
    if (mergedMembers.size() > MAX_CLUSTER_SIZE) {
        LogPrintf("TrustPropagator: HandleClusterMerge - merged cluster size %zu exceeds MAX_CLUSTER_SIZE (%u), limiting\n",
                  mergedMembers.size(), MAX_CLUSTER_SIZE);
        std::set<uint160> limitedMembers;
        size_t count = 0;
        for (const auto& member : mergedMembers) {
            if (count >= MAX_CLUSTER_SIZE) break;
            limitedMembers.insert(member);
            count++;
        }
        mergedMembers = std::move(limitedMembers);
    }
    
    // Requirement 6.1: Combine trust relations from both clusters
    // Collect all trust edges targeting any member of either original cluster
    // Key: fromAddress only - we want ONE edge per truster after conflict resolution
    // Requirement 6.4: Use most recent timestamp, with sourceEdgeTx as deterministic tie-breaker
    std::map<uint160, PropagatedTrustEdge> combinedEdges;
    
    // Helper lambda to determine if edge1 should win over edge2
    // Returns true if edge1 is "better" (newer timestamp, or same timestamp with larger sourceEdgeTx)
    auto edgeWins = [](const PropagatedTrustEdge& edge1, const PropagatedTrustEdge& edge2) -> bool {
        if (edge1.originalTimestamp > edge2.originalTimestamp) {
            return true;  // edge1 is newer
        }
        if (edge1.originalTimestamp < edge2.originalTimestamp) {
            return false;  // edge2 is newer
        }
        // Equal timestamps - use deterministic tie-breaker (lexicographic comparison of sourceEdgeTx)
        // Compare returns -1, 0, or 1
        return edge1.sourceEdgeTx.Compare(edge2.sourceEdgeTx) > 0;
    };
    
    // Helper lambda to collect edges from a set of members
    auto collectEdgesFromMembers = [this, &combinedEdges, &edgeWins](const std::set<uint160>& members) {
        for (const auto& member : members) {
            // Get propagated edges targeting this member
            std::vector<PropagatedTrustEdge> propEdges = GetPropagatedEdgesForAddress(member);
            
            for (const auto& edge : propEdges) {
                // Key is just fromAddress - one edge per truster
                uint160 key = edge.fromAddress;
                
                auto it = combinedEdges.find(key);
                if (it == combinedEdges.end()) {
                    // First time seeing this truster, add the edge
                    combinedEdges[key] = edge;
                } else {
                    // Requirement 6.4: Handle conflicts - use edgeWins for deterministic resolution
                    if (edgeWins(edge, it->second)) {
                        LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - conflict detected for edge from %s, using winning edge (ts=%u vs %u)\n",
                                 edge.fromAddress.ToString().substr(0, 16), edge.originalTimestamp, it->second.originalTimestamp);
                        combinedEdges[key] = edge;
                    }
                }
            }
            
            // Also check direct trust edges from TrustGraph targeting this member
            std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(member);
            
            for (const auto& directEdge : directEdges) {
                uint160 key = directEdge.fromAddress;
                
                // Create a PropagatedTrustEdge from the direct edge
                PropagatedTrustEdge propEdge(
                    directEdge.fromAddress,
                    member,
                    directEdge.toAddress,
                    directEdge.bondTxHash,
                    directEdge.trustWeight,
                    directEdge.timestamp,
                    directEdge.bondAmount
                );
                
                auto it = combinedEdges.find(key);
                if (it == combinedEdges.end()) {
                    combinedEdges[key] = propEdge;
                } else {
                    // Requirement 6.4: Handle conflicts - use edgeWins for deterministic resolution
                    if (edgeWins(propEdge, it->second)) {
                        LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - conflict detected for direct edge from %s, using winning edge (ts=%u vs %u)\n",
                                 directEdge.fromAddress.ToString().substr(0, 16), directEdge.timestamp, it->second.originalTimestamp);
                        combinedEdges[key] = propEdge;
                    }
                }
            }
        }
    };
    
    // Collect edges from both original clusters
    collectEdgesFromMembers(members1);
    collectEdgesFromMembers(members2);
    
    LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - collected %zu unique trust edges to propagate\n",
             combinedEdges.size());
    
    if (combinedEdges.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - no trust edges to propagate\n");
        // Invalidate caches for both original clusters and merged cluster
        InvalidateClusterCache(cluster1);
        InvalidateClusterCache(cluster2);
        InvalidateClusterCache(mergedClusterId);
        return true;
    }
    
    // Requirement 6.2: Propagate trust from both original clusters to all merged addresses
    // Get current timestamp for propagation
    uint32_t propagationTime = static_cast<uint32_t>(GetTime());
    
    uint32_t totalPropagated = 0;
    uint32_t totalFailed = 0;
    
    // For each unique trust edge (one per truster), propagate to ALL members of the merged cluster
    // This ensures all members have the same edge from each truster (deterministic)
    for (const auto& edgePair : combinedEdges) {
        const PropagatedTrustEdge& sourceEdge = edgePair.second;
        
        // Propagate this edge to ALL members of the merged cluster (unconditionally)
        for (const auto& memberAddress : mergedMembers) {
            // Create new propagated edge for this member
            PropagatedTrustEdge newPropEdge(
                sourceEdge.fromAddress,      // Original truster
                memberAddress,               // Target (merged cluster member)
                sourceEdge.originalTarget,   // Original target address
                sourceEdge.sourceEdgeTx,     // Reference to original trust edge transaction
                sourceEdge.trustWeight,      // Preserved weight
                propagationTime,             // When this propagation occurred
                sourceEdge.originalTimestamp, // Preserved original timestamp (Req 6.4)
                sourceEdge.bondAmount        // Preserved bond amount
            );
            
            // Store the propagated edge (overwrites any existing edge from this truster)
            if (StorePropagatedEdge(newPropEdge)) {
                // Build index entry
                if (IndexPropagatedEdge(sourceEdge.sourceEdgeTx, memberAddress)) {
                    totalPropagated++;
                } else {
                    // Index failed but edge was stored - count as partial success
                    LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge - failed to index edge to %s\n",
                              memberAddress.ToString().substr(0, 16));
                    totalPropagated++;
                }
            } else {
                LogPrintf("TrustPropagator: HandleClusterMerge - failed to store edge to %s\n",
                          memberAddress.ToString().substr(0, 16));
                totalFailed++;
            }
        }
    }
    
    // Invalidate caches for both original clusters and merged cluster
    InvalidateClusterCache(cluster1);
    InvalidateClusterCache(cluster2);
    InvalidateClusterCache(mergedClusterId);
    
    LogPrint(BCLog::CVM, "TrustPropagator: HandleClusterMerge completed - %u edges propagated, %u failed\n",
             totalPropagated, totalFailed);
    
    // Return success if we propagated at least some edges or there were no edges to propagate
    return totalFailed == 0 || totalPropagated > 0;
}

uint32_t TrustPropagator::DeletePropagatedEdges(const uint256& sourceEdgeTx)
{
    // Requirement 5.3: When a trust edge is deleted or modified, update all propagated edges accordingly
    // This method deletes all propagated edges that were created from the given source edge
    
    LogPrint(BCLog::CVM, "TrustPropagator: DeletePropagatedEdges for source tx %s\n",
             sourceEdgeTx.ToString().substr(0, 16));
    
    // First, get all propagated edges for this source edge using the index
    std::vector<PropagatedTrustEdge> propagatedEdges = GetPropagatedEdgesBySource(sourceEdgeTx);
    
    if (propagatedEdges.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No propagated edges found for source tx %s\n",
                 sourceEdgeTx.ToString().substr(0, 16));
        return 0;
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: Found %zu propagated edges to delete\n",
             propagatedEdges.size());
    
    uint32_t deletedCount = 0;
    uint160 clusterId;  // Track cluster ID for cache invalidation
    
    // Delete each propagated edge and its index entry
    for (const auto& edge : propagatedEdges) {
        // Delete the propagated edge from database
        if (DeletePropagatedEdge(edge.fromAddress, edge.toAddress)) {
            // Remove the index entry
            RemoveIndexEntry(sourceEdgeTx, edge.toAddress);
            deletedCount++;
            
            LogPrint(BCLog::CVM, "TrustPropagator: Deleted propagated edge from %s to %s\n",
                     edge.fromAddress.ToString(), edge.toAddress.ToString());
            
            // Track cluster ID for cache invalidation (all edges should be in same cluster)
            if (clusterId.IsNull()) {
                clusterId = clusterer.GetClusterForAddress(edge.toAddress);
            }
        } else {
            LogPrintf("TrustPropagator: Failed to delete propagated edge from %s to %s\n",
                      edge.fromAddress.ToString(), edge.toAddress.ToString());
        }
    }
    
    // Invalidate cache for the affected cluster
    if (!clusterId.IsNull()) {
        InvalidateClusterCache(clusterId);
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: DeletePropagatedEdges completed - %u edges deleted\n",
             deletedCount);
    
    return deletedCount;
}

uint32_t TrustPropagator::UpdatePropagatedEdges(const uint256& sourceEdgeTx, int16_t newWeight)
{
    // Requirement 5.3: When a trust edge is deleted or modified, update all propagated edges accordingly
    // This method updates the weight of all propagated edges that were created from the given source edge
    
    LogPrint(BCLog::CVM, "TrustPropagator: UpdatePropagatedEdges for source tx %s with new weight %d\n",
             sourceEdgeTx.ToString().substr(0, 16), newWeight);
    
    // First, get all propagated edges for this source edge using the index
    std::vector<PropagatedTrustEdge> propagatedEdges = GetPropagatedEdgesBySource(sourceEdgeTx);
    
    if (propagatedEdges.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No propagated edges found for source tx %s\n",
                 sourceEdgeTx.ToString().substr(0, 16));
        return 0;
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: Found %zu propagated edges to update\n",
             propagatedEdges.size());
    
    uint32_t updatedCount = 0;
    uint160 clusterId;  // Track cluster ID for cache invalidation
    
    // Update each propagated edge with the new weight
    for (auto& edge : propagatedEdges) {
        // Update the weight
        int16_t oldWeight = edge.trustWeight;
        edge.trustWeight = newWeight;
        
        // Store the updated edge (overwrites existing)
        if (StorePropagatedEdge(edge)) {
            updatedCount++;
            
            LogPrint(BCLog::CVM, "TrustPropagator: Updated propagated edge from %s to %s (weight: %d -> %d)\n",
                     edge.fromAddress.ToString(), edge.toAddress.ToString(), oldWeight, newWeight);
            
            // Track cluster ID for cache invalidation (all edges should be in same cluster)
            if (clusterId.IsNull()) {
                clusterId = clusterer.GetClusterForAddress(edge.toAddress);
            }
        } else {
            LogPrintf("TrustPropagator: Failed to update propagated edge from %s to %s\n",
                      edge.fromAddress.ToString(), edge.toAddress.ToString());
        }
    }
    
    // Invalidate cache for the affected cluster
    if (!clusterId.IsNull()) {
        InvalidateClusterCache(clusterId);
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: UpdatePropagatedEdges completed - %u edges updated\n",
             updatedCount);
    
    return updatedCount;
}

std::vector<PropagatedTrustEdge> TrustPropagator::GetPropagatedEdgesForAddress(const uint160& target) const
{
    // Requirement 1.4: When querying trust for any address in a cluster,
    // return both direct and propagated trust edges
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetPropagatedEdgesForAddress for %s\n",
             target.ToString());
    
    std::vector<PropagatedTrustEdge> result;
    
    // Get all keys with the trust_prop_ prefix
    // Key format: "trust_prop_{from}_{to}"
    std::vector<std::string> allKeys = database.ListKeysWithPrefix(TRUST_PROP_PREFIX);
    
    LogPrint(BCLog::CVM, "TrustPropagator: Found %zu keys with trust_prop_ prefix\n",
             allKeys.size());
    
    // Target address as string for comparison
    std::string targetStr = target.ToString();
    
    // Iterate through all propagated edge keys and filter by target address
    for (const std::string& key : allKeys) {
        // Key format: "trust_prop_{from}_{to}"
        // We need to check if the key ends with the target address
        
        // Skip index keys (they have "trust_prop_idx_" prefix)
        if (key.find(TRUST_PROP_IDX_PREFIX) == 0) {
            continue;
        }
        
        // Extract the "to" address from the key
        // Key format: "trust_prop_{from}_{to}"
        // The "to" address is after the last underscore
        size_t lastUnderscore = key.rfind('_');
        if (lastUnderscore == std::string::npos || lastUnderscore <= TRUST_PROP_PREFIX.length()) {
            LogPrint(BCLog::CVM, "TrustPropagator: Skipping malformed key: %s\n", key);
            continue;
        }
        
        std::string toAddressStr = key.substr(lastUnderscore + 1);
        
        // Check if this edge targets our address
        if (toAddressStr != targetStr) {
            continue;
        }
        
        // Read the propagated edge from database
        std::vector<uint8_t> data;
        if (!database.ReadGeneric(key, data)) {
            LogPrintf("TrustPropagator: Failed to read propagated edge with key: %s\n", key);
            continue;
        }
        
        // Deserialize the PropagatedTrustEdge
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            PropagatedTrustEdge edge;
            ss >> edge;
            
            // Verify the edge targets the correct address (double-check)
            if (edge.toAddress == target) {
                result.push_back(edge);
                LogPrint(BCLog::CVM, "TrustPropagator: Found propagated edge from %s to %s (weight: %d)\n",
                         edge.fromAddress.ToString(), edge.toAddress.ToString(), edge.trustWeight);
            }
        } catch (const std::exception& e) {
            LogPrintf("TrustPropagator: Failed to deserialize propagated edge from key %s: %s\n",
                      key, e.what());
            continue;
        }
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetPropagatedEdgesForAddress returning %zu edges for %s\n",
             result.size(), target.ToString());
    
    return result;
}

std::vector<PropagatedTrustEdge> TrustPropagator::GetPropagatedEdgesBySource(const uint256& sourceEdgeTx) const
{
    // Requirements: 5.4, 5.5
    // Query the index to find all propagated edges created from this source edge
    // Index key format: "trust_prop_idx_{sourceEdgeTx}_{to}"
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetPropagatedEdgesBySource for tx %s\n",
             sourceEdgeTx.ToString().substr(0, 16));
    
    std::vector<PropagatedTrustEdge> result;
    
    // Build the index prefix for this source edge
    // Format: "trust_prop_idx_{sourceEdgeTx}_"
    std::string indexPrefix = TRUST_PROP_IDX_PREFIX + sourceEdgeTx.ToString() + "_";
    
    // Get all index keys for this source edge
    std::vector<std::string> indexKeys = database.ListKeysWithPrefix(indexPrefix);
    
    LogPrint(BCLog::CVM, "TrustPropagator: Found %zu index entries for source tx %s\n",
             indexKeys.size(), sourceEdgeTx.ToString().substr(0, 16));
    
    // For each index entry, read the corresponding propagated edge
    for (const std::string& indexKey : indexKeys) {
        // Read the target address from the index entry
        std::vector<uint8_t> indexData;
        if (!database.ReadGeneric(indexKey, indexData)) {
            LogPrintf("TrustPropagator: Failed to read index entry: %s\n", indexKey);
            continue;
        }
        
        // Deserialize the target address
        uint160 targetAddress;
        try {
            CDataStream ss(indexData, SER_DISK, CLIENT_VERSION);
            ss >> targetAddress;
        } catch (const std::exception& e) {
            LogPrintf("TrustPropagator: Failed to deserialize target address from index key %s: %s\n",
                      indexKey, e.what());
            continue;
        }
        
        // Now we need to find the propagated edge for this target
        // We need to search for edges where toAddress == targetAddress and sourceEdgeTx matches
        // Since we don't have the fromAddress in the index, we need to search
        
        // Get all propagated edges for this target address
        std::vector<PropagatedTrustEdge> targetEdges = GetPropagatedEdgesForAddress(targetAddress);
        
        // Find the edge that matches our source transaction
        for (const auto& edge : targetEdges) {
            if (edge.sourceEdgeTx == sourceEdgeTx) {
                result.push_back(edge);
                LogPrint(BCLog::CVM, "TrustPropagator: Found propagated edge from %s to %s for source tx\n",
                         edge.fromAddress.ToString(), edge.toAddress.ToString());
                break;  // Only one edge per target for a given source tx
            }
        }
    }
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetPropagatedEdgesBySource returning %zu edges\n",
             result.size());
    
    return result;
}

ClusterTrustSummary TrustPropagator::GetClusterTrustSummary(const uint160& address) const
{
    // Requirements: 3.2, 3.4, 7.4, 7.5
    // 3.2: Return aggregated trust information for the entire wallet cluster
    // 3.4: Include cluster_id and member_count in the response
    // 7.4: Cache cluster membership for frequently accessed addresses
    // 7.5: When cache size exceeds 100MB, evict least-recently-used entries
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetClusterTrustSummary for address %s\n",
             address.ToString());
    
    // Get the cluster ID for this address
    uint160 clusterId = clusterer.GetClusterForAddress(address);
    
    // If no cluster found, treat the address as its own cluster
    if (clusterId.IsNull()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No cluster found for %s, treating as single-address cluster\n",
                 address.ToString());
        clusterId = address;
    }
    
    // Check LRU cache first (Requirement 7.4)
    ClusterTrustSummary cachedSummary;
    if (summaryCache.Get(clusterId, cachedSummary)) {
        LogPrint(BCLog::CVM, "TrustPropagator: Returning cached summary for cluster %s (cache size: %zu bytes)\n",
                 clusterId.ToString(), summaryCache.GetCurrentSize());
        return cachedSummary;
    }
    
    // Build the summary from database
    ClusterTrustSummary summary = BuildClusterTrustSummary(clusterId);
    
    // Cache the result using LRU cache (Requirements 7.4, 7.5)
    // The LRU cache automatically handles eviction when size exceeds 100MB
    summaryCache.Put(clusterId, summary);
    
    LogPrint(BCLog::CVM, "TrustPropagator: GetClusterTrustSummary returning summary for cluster %s "
             "(members: %zu, edges: %u, effective_score: %.4f, cache entries: %zu)\n",
             clusterId.ToString(), summary.memberAddresses.size(), 
             summary.edgeCount, summary.effectiveScore, summaryCache.GetEntryCount());
    
    return summary;
}

ClusterTrustSummary TrustPropagator::BuildClusterTrustSummary(const uint160& clusterId) const
{
    // Build a complete trust summary for a cluster by aggregating all trust information
    // Requirements: 3.2 (aggregated trust information)
    
    LogPrint(BCLog::CVM, "TrustPropagator: BuildClusterTrustSummary for cluster %s\n",
             clusterId.ToString());
    
    ClusterTrustSummary summary(clusterId);
    
    // Get all cluster members
    std::set<uint160> clusterMembers = clusterer.GetClusterMembers(clusterId);
    
    // If no members found, treat clusterId as the only member
    if (clusterMembers.empty()) {
        LogPrint(BCLog::CVM, "TrustPropagator: No members found for cluster %s, using cluster ID as sole member\n",
                 clusterId.ToString());
        clusterMembers.insert(clusterId);
    }
    
    summary.memberAddresses = clusterMembers;
    
    // Track unique trust edges to avoid double-counting
    // Key: fromAddress (we count one edge per truster, regardless of how many cluster members they trust)
    std::set<uint160> uniqueTrusters;
    
    // Track minimum score across all cluster members for effective score calculation
    double minScore = std::numeric_limits<double>::max();
    bool foundAnyScore = false;
    
    // Aggregate trust information from all cluster members
    for (const auto& memberAddress : clusterMembers) {
        // Get direct trust edges from TrustGraph
        std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(memberAddress);
        
        for (const auto& edge : directEdges) {
            // Track unique trusters
            uniqueTrusters.insert(edge.fromAddress);
            
            // Aggregate trust weights
            if (edge.trustWeight > 0) {
                summary.totalIncomingTrust += edge.trustWeight;
            } else if (edge.trustWeight < 0) {
                summary.totalNegativeTrust += edge.trustWeight;  // Already negative
            }
            
            LogPrint(BCLog::CVM, "TrustPropagator: Direct edge from %s to %s (weight: %d)\n",
                     edge.fromAddress.ToString(), memberAddress.ToString(), edge.trustWeight);
        }
        
        // Get propagated trust edges
        std::vector<PropagatedTrustEdge> propagatedEdges = GetPropagatedEdgesForAddress(memberAddress);
        
        for (const auto& propEdge : propagatedEdges) {
            // Track unique trusters
            uniqueTrusters.insert(propEdge.fromAddress);
            
            // Aggregate trust weights
            if (propEdge.trustWeight > 0) {
                summary.totalIncomingTrust += propEdge.trustWeight;
            } else if (propEdge.trustWeight < 0) {
                summary.totalNegativeTrust += propEdge.trustWeight;  // Already negative
            }
            
            LogPrint(BCLog::CVM, "TrustPropagator: Propagated edge from %s to %s (weight: %d)\n",
                     propEdge.fromAddress.ToString(), memberAddress.ToString(), propEdge.trustWeight);
        }
        
        // Calculate individual member score for minimum calculation
        // Score = weighted average of all incoming trust
        double memberScore = CalculateMemberScore(memberAddress);
        
        if (memberScore < minScore) {
            minScore = memberScore;
        }
        foundAnyScore = true;
        
        LogPrint(BCLog::CVM, "TrustPropagator: Member %s has score %.4f\n",
                 memberAddress.ToString(), memberScore);
    }
    
    // Set edge count (unique trusters)
    summary.edgeCount = static_cast<uint32_t>(uniqueTrusters.size());
    
    // Set effective score (minimum across all members)
    // Requirement 4.2: Return minimum score across ALL addresses in wallet cluster
    if (foundAnyScore && minScore != std::numeric_limits<double>::max()) {
        summary.effectiveScore = minScore;
    } else {
        summary.effectiveScore = 0.0;  // Neutral score if no trust edges found
    }
    
    // Set last updated timestamp
    summary.lastUpdated = static_cast<uint32_t>(GetTime());
    
    LogPrint(BCLog::CVM, "TrustPropagator: BuildClusterTrustSummary completed - "
             "members: %zu, edges: %u, positive: %ld, negative: %ld, effective: %.4f\n",
             summary.memberAddresses.size(), summary.edgeCount,
             summary.totalIncomingTrust, summary.totalNegativeTrust, summary.effectiveScore);
    
    return summary;
}

double TrustPropagator::CalculateMemberScore(const uint160& memberAddress) const
{
    // Calculate trust score for a single address
    // This is a helper method for BuildClusterTrustSummary
    // Score = weighted average of all incoming trust edges (weighted by bond amount)
    
    double totalWeight = 0.0;
    double totalBondWeight = 0.0;
    
    // Get direct trust edges
    std::vector<TrustEdge> directEdges = trustGraph.GetIncomingTrust(memberAddress);
    
    for (const auto& edge : directEdges) {
        // Weight by bond amount (more stake = more influence)
        double bondWeight = static_cast<double>(edge.bondAmount) / COIN;
        if (bondWeight < 1.0) bondWeight = 1.0;  // Minimum weight of 1
        
        totalWeight += edge.trustWeight * bondWeight;
        totalBondWeight += bondWeight;
    }
    
    // Get propagated trust edges
    std::vector<PropagatedTrustEdge> propagatedEdges = GetPropagatedEdgesForAddress(memberAddress);
    
    for (const auto& propEdge : propagatedEdges) {
        // Weight by bond amount (more stake = more influence)
        double bondWeight = static_cast<double>(propEdge.bondAmount) / COIN;
        if (bondWeight < 1.0) bondWeight = 1.0;  // Minimum weight of 1
        
        totalWeight += propEdge.trustWeight * bondWeight;
        totalBondWeight += bondWeight;
    }
    
    // Calculate weighted average score
    if (totalBondWeight > 0.0) {
        return totalWeight / totalBondWeight;
    }
    
    return 0.0;  // Neutral score if no trust edges
}

} // namespace CVM
