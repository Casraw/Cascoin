// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_WALLETCLUSTER_H
#define CASCOIN_CVM_WALLETCLUSTER_H

#include <uint256.h>
#include <cvm/cvmdb.h>
#include <set>
#include <map>
#include <vector>

/**
 * Wallet Clustering System
 * 
 * Prevents reputation gaming by linking addresses that belong to the same wallet.
 * 
 * Key Features:
 * - Transaction-based clustering (addresses used as inputs together)
 * - Change address detection
 * - Reputation sharing across wallet cluster
 * - Prevents "fresh start" attacks (creating new addresses to escape bad reputation)
 * 
 * Security Benefits:
 * - Scammer cannot escape negative reputation by creating new address
 * - All addresses in wallet share lowest reputation (conservative approach)
 * - Chain analysis links wallet ownership
 */

namespace CVM {

/**
 * WalletClusterInfo - Information about a cluster of addresses belonging to same wallet
 */
struct WalletClusterInfo {
    uint160 cluster_id;                      // Primary address (oldest or most active)
    std::set<uint160> member_addresses;       // All addresses in this cluster
    int64_t first_seen;                       // Timestamp of oldest address
    int64_t last_activity;                    // Last transaction time
    uint32_t transaction_count;               // Total transactions across all addresses
    double shared_reputation;                 // Aggregated reputation score
    
    WalletClusterInfo() : cluster_id(), first_seen(0), last_activity(0), 
                          transaction_count(0), shared_reputation(0.0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(cluster_id);
        READWRITE(member_addresses);
        READWRITE(first_seen);
        READWRITE(last_activity);
        READWRITE(transaction_count);
        READWRITE(shared_reputation);
    }
};

/**
 * WalletClusterer - Identifies addresses belonging to same wallet
 * 
 * Clustering Heuristics:
 * 1. Common Input Heuristic: If multiple addresses are inputs to same transaction,
 *    they likely belong to same wallet
 * 2. Change Address Heuristic: One output is change, belongs to same wallet
 * 3. Temporal Clustering: Addresses created/used close in time
 * 4. Behavioral Patterns: Similar transaction patterns indicate same owner
 */
class WalletClusterer {
public:
    explicit WalletClusterer(CVMDatabase& db);
    virtual ~WalletClusterer() = default;
    
    // Core Clustering Functions
    /**
     * Analyze all transactions and build address clusters
     * This should be called periodically or when significant new transactions occur
     */
    virtual void BuildClusters();
    
    /**
     * Find which cluster an address belongs to
     * Returns cluster_id (primary address of cluster) or null if address is alone
     */
    virtual uint160 GetClusterForAddress(const uint160& address);
    
    /**
     * Get all addresses in the same cluster as given address
     */
    virtual std::set<uint160> GetClusterMembers(const uint160& address);
    
    /**
     * Get complete cluster information
     */
    virtual WalletClusterInfo GetClusterInfo(const uint160& cluster_id);
    
    /**
     * Manually link two addresses as belonging to same wallet
     * Useful for user-provided information or external chain analysis
     */
    virtual void LinkAddresses(const uint160& addr1, const uint160& addr2);
    
    /**
     * Calculate shared reputation for a cluster
     * Strategy: Use MINIMUM reputation (most conservative)
     * Alternative: Could use weighted average, but minimum is safer
     */
    virtual double CalculateClusterReputation(const uint160& cluster_id);
    
    /**
     * Get effective reputation for an address (considering cluster)
     * This is what should be used instead of individual address reputation
     */
    virtual double GetEffectiveReputation(const uint160& address);
    
    /**
     * Get effective HAT v2 score for an address (considering cluster)
     */
    virtual double GetEffectiveHATScore(const uint160& address);
    
    // Statistics
    virtual uint32_t GetTotalClusters() const;
    virtual uint32_t GetLargestClusterSize() const;
    virtual std::map<uint160, uint32_t> GetClusterSizeMap() const;
    
    // Cache management
    virtual void InvalidateCache();
    virtual void SaveClusters();
    virtual void LoadClusters();
    
private:
    CVMDatabase& database;
    
    // Cluster mappings
    std::map<uint160, uint160> address_to_cluster;     // address -> cluster_id
    std::map<uint160, WalletClusterInfo> clusters;     // cluster_id -> info
    
    bool cache_valid;
    
    // Helper: Union-Find for efficient clustering
    uint160 FindClusterRoot(const uint160& address);
    void UnionClusters(const uint160& addr1, const uint160& addr2);
    
    // Helper: Analyze transaction for clustering hints
    void AnalyzeTransaction(const uint256& txid);
    
    // Helper: Detect change addresses
    bool IsLikelyChangeAddress(const uint160& address);
    
    // Helper: Get all transactions involving an address
    std::vector<uint256> GetAddressTransactions(const uint160& address);
};

} // namespace CVM

#endif // CASCOIN_CVM_WALLETCLUSTER_H

