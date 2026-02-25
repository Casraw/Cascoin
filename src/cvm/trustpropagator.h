// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUSTPROPAGATOR_H
#define CASCOIN_CVM_TRUSTPROPAGATOR_H

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <functional>
#include <list>
#include <unordered_map>

namespace CVM {

// Forward declarations
class CVMDatabase;
class WalletClusterer;
class TrustGraph;
struct TrustEdge;

/**
 * PropagatedTrustEdge - A trust edge that has been automatically propagated
 * to all addresses in a wallet cluster.
 * 
 * When a trust edge is added to an address, it is propagated to all other
 * addresses in the same wallet cluster. This prevents reputation gaming
 * where malicious actors create new addresses to escape negative trust.
 * 
 * Storage key format: "trust_prop_{from}_{to}"
 * 
 * Requirements: 5.1, 5.5
 */
struct PropagatedTrustEdge {
    uint160 fromAddress;          // Original truster (who established trust)
    uint160 toAddress;            // Propagated target (cluster member receiving propagated trust)
    uint160 originalTarget;       // Original target address (the address trust was originally given to)
    uint256 sourceEdgeTx;         // Reference to original trust edge transaction hash
    int16_t trustWeight;          // Inherited trust weight (-100 to +100)
    uint32_t propagatedAt;        // Timestamp when propagation occurred
    uint32_t originalTimestamp;   // Original trust edge timestamp (for conflict resolution per Req 6.4)
    CAmount bondAmount;           // Inherited bond amount from original edge
    
    PropagatedTrustEdge() 
        : fromAddress()
        , toAddress()
        , originalTarget()
        , sourceEdgeTx()
        , trustWeight(0)
        , propagatedAt(0)
        , originalTimestamp(0)
        , bondAmount(0) 
    {}
    
    PropagatedTrustEdge(const uint160& from, const uint160& to, const uint160& origTarget,
                        const uint256& sourceTx, int16_t weight, uint32_t propTimestamp, 
                        uint32_t origTimestamp, CAmount bond)
        : fromAddress(from)
        , toAddress(to)
        , originalTarget(origTarget)
        , sourceEdgeTx(sourceTx)
        , trustWeight(weight)
        , propagatedAt(propTimestamp)
        , originalTimestamp(origTimestamp)
        , bondAmount(bond)
    {}
    
    // Legacy constructor for backward compatibility (uses propagatedAt as originalTimestamp)
    PropagatedTrustEdge(const uint160& from, const uint160& to, const uint160& origTarget,
                        const uint256& sourceTx, int16_t weight, uint32_t timestamp, CAmount bond)
        : fromAddress(from)
        , toAddress(to)
        , originalTarget(origTarget)
        , sourceEdgeTx(sourceTx)
        , trustWeight(weight)
        , propagatedAt(timestamp)
        , originalTimestamp(timestamp)  // Default to propagatedAt for legacy compatibility
        , bondAmount(bond)
    {}
    
    /**
     * Generate the database storage key for this propagated edge
     * Format: "trust_prop_{from}_{to}"
     */
    std::string GetStorageKey() const;
    
    /**
     * Generate the index key for source edge lookup
     * Format: "trust_prop_idx_{sourceEdgeTx}_{to}"
     */
    std::string GetIndexKey() const;
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(fromAddress);
        READWRITE(toAddress);
        READWRITE(originalTarget);
        READWRITE(sourceEdgeTx);
        READWRITE(trustWeight);
        READWRITE(propagatedAt);
        READWRITE(originalTimestamp);
        READWRITE(bondAmount);
    }
    
    bool operator==(const PropagatedTrustEdge& other) const {
        return fromAddress == other.fromAddress &&
               toAddress == other.toAddress &&
               originalTarget == other.originalTarget &&
               sourceEdgeTx == other.sourceEdgeTx &&
               trustWeight == other.trustWeight &&
               propagatedAt == other.propagatedAt &&
               originalTimestamp == other.originalTimestamp &&
               bondAmount == other.bondAmount;
    }
    
    bool operator!=(const PropagatedTrustEdge& other) const {
        return !(*this == other);
    }
};

/**
 * ClusterTrustSummary - Aggregated trust information for a wallet cluster
 * 
 * Provides a summary view of all trust relations affecting a wallet cluster,
 * including both direct and propagated trust edges.
 * 
 * Storage key format: "cluster_trust_{clusterId}"
 * 
 * Requirements: 3.2, 3.4
 */
struct ClusterTrustSummary {
    uint160 clusterId;                    // Canonical cluster address (primary identifier)
    std::set<uint160> memberAddresses;    // All addresses in the cluster
    int64_t totalIncomingTrust;           // Sum of positive incoming trust weights
    int64_t totalNegativeTrust;           // Sum of negative incoming trust weights
    double effectiveScore;                // Minimum trust score across all cluster members
    uint32_t edgeCount;                   // Total trust edges (direct + propagated)
    uint32_t lastUpdated;                 // Timestamp of last modification
    
    ClusterTrustSummary()
        : clusterId()
        , memberAddresses()
        , totalIncomingTrust(0)
        , totalNegativeTrust(0)
        , effectiveScore(0.0)
        , edgeCount(0)
        , lastUpdated(0)
    {}
    
    ClusterTrustSummary(const uint160& id)
        : clusterId(id)
        , memberAddresses()
        , totalIncomingTrust(0)
        , totalNegativeTrust(0)
        , effectiveScore(0.0)
        , edgeCount(0)
        , lastUpdated(0)
    {}
    
    /**
     * Generate the database storage key for this cluster summary
     * Format: "cluster_trust_{clusterId}"
     */
    std::string GetStorageKey() const;
    
    /**
     * Get the number of members in the cluster
     */
    size_t GetMemberCount() const { return memberAddresses.size(); }
    
    /**
     * Check if an address is a member of this cluster
     */
    bool HasMember(const uint160& address) const {
        return memberAddresses.find(address) != memberAddresses.end();
    }
    
    /**
     * Add a member address to the cluster
     */
    void AddMember(const uint160& address) {
        memberAddresses.insert(address);
    }
    
    /**
     * Calculate net trust (positive - negative)
     */
    int64_t GetNetTrust() const {
        return totalIncomingTrust + totalNegativeTrust; // totalNegativeTrust is already negative
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(clusterId);
        READWRITE(memberAddresses);
        READWRITE(totalIncomingTrust);
        READWRITE(totalNegativeTrust);
        READWRITE(effectiveScore);
        READWRITE(edgeCount);
        READWRITE(lastUpdated);
    }
    
    bool operator==(const ClusterTrustSummary& other) const {
        return clusterId == other.clusterId &&
               memberAddresses == other.memberAddresses &&
               totalIncomingTrust == other.totalIncomingTrust &&
               totalNegativeTrust == other.totalNegativeTrust &&
               effectiveScore == other.effectiveScore &&
               edgeCount == other.edgeCount &&
               lastUpdated == other.lastUpdated;
    }
    
    bool operator!=(const ClusterTrustSummary& other) const {
        return !(*this == other);
    }
};

/**
 * PropagationResult - Result of a trust propagation operation
 * 
 * Contains the count of propagated edges and a flag indicating
 * whether the operation was limited due to cluster size constraints.
 * 
 * Requirements: 7.2
 */
struct PropagationResult {
    uint32_t propagatedCount;     // Number of edges propagated
    bool wasLimited;              // True if cluster size exceeded MAX_CLUSTER_SIZE
    uint32_t originalClusterSize; // Original cluster size before limiting
    
    PropagationResult()
        : propagatedCount(0)
        , wasLimited(false)
        , originalClusterSize(0)
    {}
    
    PropagationResult(uint32_t count, bool limited, uint32_t origSize)
        : propagatedCount(count)
        , wasLimited(limited)
        , originalClusterSize(origSize)
    {}
    
    // Implicit conversion to uint32_t for backward compatibility
    operator uint32_t() const { return propagatedCount; }
};

/**
 * LRUCache - Least Recently Used cache with size limit
 * 
 * A thread-safe LRU cache that evicts least recently used entries
 * when the cache size exceeds the configured limit.
 * 
 * Requirements: 7.4, 7.5
 */
template<typename Key, typename Value>
class LRUCache {
public:
    using KeyValuePair = std::pair<Key, Value>;
    using ListIterator = typename std::list<KeyValuePair>::iterator;
    
    /**
     * Constructor
     * @param maxSizeBytes Maximum cache size in bytes (default 100MB)
     * @param estimatedEntrySize Estimated size per entry in bytes (for size tracking)
     */
    explicit LRUCache(size_t maxSizeBytes = 100 * 1024 * 1024, size_t estimatedEntrySize = 1024)
        : maxSize(maxSizeBytes)
        , entrySize(estimatedEntrySize)
        , currentSize(0)
    {}
    
    /**
     * Get a value from the cache
     * @param key The key to look up
     * @param value Output parameter for the value
     * @return true if found, false otherwise
     */
    bool Get(const Key& key, Value& value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            return false;
        }
        
        // Move accessed item to front of list (most recently used)
        cacheList.splice(cacheList.begin(), cacheList, it->second);
        value = it->second->second;
        return true;
    }
    
    /**
     * Put a value into the cache
     * @param key The key
     * @param value The value to cache
     */
    void Put(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            // Update existing entry and move to front
            it->second->second = value;
            cacheList.splice(cacheList.begin(), cacheList, it->second);
            return;
        }
        
        // Evict entries if we're at capacity (Requirement 7.5)
        while (currentSize + entrySize > maxSize && !cacheList.empty()) {
            // Remove least recently used (back of list)
            auto last = cacheList.end();
            --last;
            cacheMap.erase(last->first);
            cacheList.pop_back();
            currentSize -= entrySize;
        }
        
        // Add new entry at front (most recently used)
        cacheList.push_front({key, value});
        cacheMap[key] = cacheList.begin();
        currentSize += entrySize;
    }
    
    /**
     * Remove a specific entry from the cache
     * @param key The key to remove
     * @return true if removed, false if not found
     */
    bool Remove(const Key& key) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        
        auto it = cacheMap.find(key);
        if (it == cacheMap.end()) {
            return false;
        }
        
        cacheList.erase(it->second);
        cacheMap.erase(it);
        currentSize -= entrySize;
        return true;
    }
    
    /**
     * Clear all entries from the cache
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cacheList.clear();
        cacheMap.clear();
        currentSize = 0;
    }
    
    /**
     * Get current cache size in bytes
     */
    size_t GetCurrentSize() const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return currentSize;
    }
    
    /**
     * Get number of entries in cache
     */
    size_t GetEntryCount() const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return cacheMap.size();
    }
    
    /**
     * Get maximum cache size in bytes
     */
    size_t GetMaxSize() const {
        return maxSize;
    }
    
    /**
     * Check if cache contains a key
     */
    bool Contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(cacheMutex);
        return cacheMap.find(key) != cacheMap.end();
    }

private:
    size_t maxSize;           // Maximum cache size in bytes
    size_t entrySize;         // Estimated size per entry
    size_t currentSize;       // Current cache size in bytes
    
    std::list<KeyValuePair> cacheList;  // Ordered list (front = most recent)
    std::map<Key, ListIterator> cacheMap;  // Map for O(1) lookup
    mutable std::mutex cacheMutex;
};

/**
 * TrustPropagator - Manages trust propagation across wallet clusters
 * 
 * This class is responsible for:
 * 1. Propagating trust edges to all addresses in a wallet cluster
 * 2. Inheriting trust for new cluster members
 * 3. Handling cluster merges
 * 4. Managing propagated edge storage and indexing
 * 
 * Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 5.1, 5.2, 5.3, 6.1, 6.2
 */
class TrustPropagator {
public:
    // Maximum cluster size for propagation operations (Requirement 7.2)
    static constexpr uint32_t MAX_CLUSTER_SIZE = 10000;
    
    // Default batch size for processing large clusters (Requirement 7.3)
    static constexpr uint32_t DEFAULT_BATCH_SIZE = 1000;
    
    // Default cache size limit in bytes (Requirement 7.5: 100MB)
    static constexpr size_t DEFAULT_CACHE_SIZE = 100 * 1024 * 1024;
    
    // Estimated size per ClusterTrustSummary entry (for cache size tracking)
    static constexpr size_t ESTIMATED_SUMMARY_SIZE = 2048;
    
    explicit TrustPropagator(CVMDatabase& db, WalletClusterer& clusterer, TrustGraph& trustGraph);
    ~TrustPropagator() = default;
    
    /**
     * Propagate a trust edge to all addresses in target's wallet cluster
     * 
     * @param edge The original trust edge to propagate
     * @return Number of propagated edges created
     * 
     * Requirements: 1.1, 1.2, 1.3
     */
    uint32_t PropagateTrustEdge(const TrustEdge& edge);
    
    /**
     * Propagate a trust edge with detailed result including limit information
     * 
     * @param edge The original trust edge to propagate
     * @return PropagationResult with count and limit flag
     * 
     * Requirements: 1.1, 1.2, 1.3, 7.2
     */
    PropagationResult PropagateTrustEdgeWithResult(const TrustEdge& edge);
    
    /**
     * Propagate trust edge in batches for large clusters
     * 
     * @param edge The original trust edge to propagate
     * @param batchSize Number of addresses to process per batch
     * @param callback Optional callback called after each batch (returns false to stop)
     * @return PropagationResult with count and limit flag
     * 
     * Requirements: 7.2, 7.3
     */
    PropagationResult PropagateTrustEdgeBatched(const TrustEdge& edge, 
                                                 uint32_t batchSize = DEFAULT_BATCH_SIZE,
                                                 std::function<bool(uint32_t, uint32_t)> callback = nullptr);
    
    /**
     * Inherit existing trust relations for a new cluster member
     * 
     * @param newAddress The newly detected address
     * @param clusterId The cluster it belongs to
     * @return Number of trust edges inherited
     * 
     * Requirements: 2.1, 2.2
     */
    uint32_t InheritTrustForNewMember(const uint160& newAddress, const uint160& clusterId);
    
    /**
     * Handle cluster merge - combine trust relations
     * 
     * @param cluster1 First cluster ID
     * @param cluster2 Second cluster ID
     * @param mergedClusterId New merged cluster ID
     * @return true if merge successful
     * 
     * Requirements: 6.1, 6.2, 6.4
     */
    bool HandleClusterMerge(const uint160& cluster1, const uint160& cluster2, 
                           const uint160& mergedClusterId);
    
    /**
     * Delete propagated edges when original edge is removed
     * 
     * @param sourceEdgeTx Transaction hash of original edge
     * @return Number of propagated edges deleted
     * 
     * Requirement: 5.3
     */
    uint32_t DeletePropagatedEdges(const uint256& sourceEdgeTx);
    
    /**
     * Update propagated edges when original edge is modified
     * 
     * @param sourceEdgeTx Transaction hash of original edge
     * @param newWeight New trust weight
     * @return Number of propagated edges updated
     * 
     * Requirement: 5.3
     */
    uint32_t UpdatePropagatedEdges(const uint256& sourceEdgeTx, int16_t newWeight);
    
    /**
     * Get all propagated edges for a target address
     * 
     * @param target Address to query
     * @return Vector of propagated trust edges
     * 
     * Requirement: 1.4
     */
    std::vector<PropagatedTrustEdge> GetPropagatedEdgesForAddress(const uint160& target) const;
    
    /**
     * Get cluster trust summary
     * 
     * @param address Any address in the cluster
     * @return Aggregated trust summary for the cluster
     * 
     * Requirements: 3.2, 3.4
     */
    ClusterTrustSummary GetClusterTrustSummary(const uint160& address) const;
    
    /**
     * Get all propagated edges originating from a source edge
     * 
     * @param sourceEdgeTx Transaction hash of original edge
     * @return Vector of propagated edges created from this source
     * 
     * Requirements: 5.4, 5.5
     */
    std::vector<PropagatedTrustEdge> GetPropagatedEdgesBySource(const uint256& sourceEdgeTx) const;
    
    /**
     * Get cache statistics
     * 
     * @return Current cache size in bytes
     * 
     * Requirement: 7.5
     */
    size_t GetCacheSize() const { return summaryCache.GetCurrentSize(); }
    
    /**
     * Get number of cached entries
     * 
     * @return Number of entries in cache
     */
    size_t GetCacheEntryCount() const { return summaryCache.GetEntryCount(); }
    
    /**
     * Get maximum cache size
     * 
     * @return Maximum cache size in bytes
     */
    size_t GetCacheMaxSize() const { return summaryCache.GetMaxSize(); }
    
    /**
     * Clear the entire cache
     * 
     * Useful for testing or when major changes occur
     */
    void ClearCache() { summaryCache.Clear(); }

private:
    CVMDatabase& database;
    WalletClusterer& clusterer;
    TrustGraph& trustGraph;
    
    // LRU Cache for cluster trust summaries (Requirements 7.4, 7.5)
    // Uses LRU eviction when cache exceeds 100MB limit
    mutable LRUCache<uint160, ClusterTrustSummary> summaryCache;
    
    /**
     * Store a propagated edge in database
     * 
     * @param edge The propagated edge to store
     * @return true if successful
     * 
     * Requirement: 5.1
     */
    bool StorePropagatedEdge(const PropagatedTrustEdge& edge);
    
    /**
     * Build index entry for source edge -> propagated edges
     * 
     * @param sourceEdgeTx Transaction hash of source edge
     * @param propagatedTo Address that received propagated edge
     * @return true if successful
     * 
     * Requirement: 5.2
     */
    bool IndexPropagatedEdge(const uint256& sourceEdgeTx, const uint160& propagatedTo);
    
    /**
     * Remove index entry for source edge -> propagated edge
     * 
     * @param sourceEdgeTx Transaction hash of source edge
     * @param propagatedTo Address that had propagated edge
     * @return true if successful
     */
    bool RemoveIndexEntry(const uint256& sourceEdgeTx, const uint160& propagatedTo);
    
    /**
     * Delete a single propagated edge from database
     * 
     * @param fromAddress Source address of edge
     * @param toAddress Target address of edge
     * @return true if successful
     */
    bool DeletePropagatedEdge(const uint160& fromAddress, const uint160& toAddress);
    
    /**
     * Invalidate cache for a cluster
     * 
     * @param clusterId Cluster to invalidate
     */
    void InvalidateClusterCache(const uint160& clusterId);
    
    /**
     * Build cluster trust summary from database
     * 
     * @param clusterId Cluster to summarize
     * @return Computed trust summary
     */
    ClusterTrustSummary BuildClusterTrustSummary(const uint160& clusterId) const;
    
    /**
     * Calculate trust score for a single address
     * 
     * @param memberAddress Address to calculate score for
     * @return Weighted average trust score
     */
    double CalculateMemberScore(const uint160& memberAddress) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_TRUSTPROPAGATOR_H
