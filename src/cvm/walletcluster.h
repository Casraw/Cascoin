// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_WALLETCLUSTER_H
#define CASCOIN_CVM_WALLETCLUSTER_H

#include <uint256.h>
#include <cvm/cvmdb.h>
#include <cvm/trustnodeid.h>
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
 *
 * Identity representation
 * -----------------------
 * Cluster IDs and members are wide TrustNodeId values: they represent every
 * supported destination type (P2PKH/P2SH/P2WPKH/P2WSH/quantum) without
 * truncation and never collide when they differ by type. Deduplication uses
 * exact typed identities, not low-20-byte projections. The externally visible
 * cluster ID is deterministic: the minimum member under TrustNodeId::operator<.
 * Transaction and source-edge identifiers remain uint256.
 */

namespace CVM {

/**
 * WalletClusterInfo - Information about a cluster of addresses belonging to same wallet
 */
struct WalletClusterInfo {
    TrustNodeId cluster_id;                      // Deterministic cluster ID (minimum member under operator<)
    std::set<TrustNodeId> member_addresses;      // All addresses in this cluster (sorted/deduplicated)
    int64_t first_seen;                          // Timestamp of oldest address
    int64_t last_activity;                       // Last transaction time
    uint32_t transaction_count;                  // Total transactions across all addresses
    double shared_reputation;                    // Aggregated reputation score
    
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

    // --- Typed core API (wide user identities) ----------------------------

    /**
     * Find which cluster an identity belongs to.
     * Returns the deterministic cluster ID (minimum member under operator<) or
     * the identity itself if it is alone.
     *
     * Virtual so test doubles can override the typed query surface (the primary
     * API used by ClusterTrustQuery).
     */
    virtual TrustNodeId GetClusterForAddress(const TrustNodeId& address);

    /**
     * Get all identities in the same cluster as the given identity.
     *
     * Virtual so test doubles can override the typed query surface.
     */
    virtual std::set<TrustNodeId> GetClusterMembers(const TrustNodeId& address);

    /**
     * Get complete cluster information for a typed cluster ID.
     */
    WalletClusterInfo GetClusterInfo(const TrustNodeId& cluster_id);

    /**
     * Manually link two identities as belonging to the same wallet.
     */
    void LinkAddresses(const TrustNodeId& addr1, const TrustNodeId& addr2);

    /**
     * Record a transaction's input identities into the transaction/address
     * index that feeds common-input-ownership clustering. Records are sorted
     * and deduplicated before upsert.
     */
    void RecordTransactionInputs(const uint256& txid,
                                 const std::vector<TrustNodeId>& inputAddresses);

    /**
     * Calculate shared reputation for a typed cluster (MINIMUM, conservative).
     */
    double CalculateClusterReputation(const TrustNodeId& cluster_id);

    /**
     * Get effective reputation for a typed identity (considering cluster).
     */
    double GetEffectiveReputation(const TrustNodeId& address);

    /**
     * Get effective HAT v2 score for a typed identity (considering cluster).
     */
    double GetEffectiveHATScore(const TrustNodeId& address);

    // --- Thin uint160 wrappers (legacy P2PKH callers) ---------------------
    //
    // Each wraps the bare uint160 as TrustNodeId{P2PKH, zero-extended} and
    // forwards to the typed overload above. They are kept virtual so existing
    // mocks that override the uint160 surface (e.g. TrustPropagator tests)
    // keep working until Wave 8 removes the remaining uint160 bridging at the
    // RPC/block-processing sites.

    virtual uint160 GetClusterForAddress(const uint160& address);
    virtual std::set<uint160> GetClusterMembers(const uint160& address);
    virtual WalletClusterInfo GetClusterInfo(const uint160& cluster_id);
    virtual void LinkAddresses(const uint160& addr1, const uint160& addr2);
    virtual void RecordTransactionInputs(const uint256& txid,
                                         const std::vector<uint160>& inputAddresses);
    virtual double CalculateClusterReputation(const uint160& cluster_id);
    virtual double GetEffectiveReputation(const uint160& address);
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
    
    // Cluster mappings (typed identities)
    std::map<TrustNodeId, TrustNodeId> address_to_cluster;     // member -> union-find parent
    std::map<TrustNodeId, WalletClusterInfo> clusters;         // union-find root -> info
    
    bool cache_valid;
    
    // Helper: Union-Find for efficient clustering
    TrustNodeId FindClusterRoot(const TrustNodeId& address);
    void UnionClusters(const TrustNodeId& addr1, const TrustNodeId& addr2);

    // Helper: deterministic externally-visible cluster ID for a union-find root
    // (minimum member under TrustNodeId::operator<).
    TrustNodeId ExternalClusterId(const TrustNodeId& root);
    
    // Helper: Analyze transaction for clustering hints
    void AnalyzeTransaction(const uint256& txid);
    
    // Helper: Detect change addresses
    bool IsLikelyChangeAddress(const TrustNodeId& address);
    
    // Helper: Get all transactions involving an identity
    std::vector<uint256> GetAddressTransactions(const TrustNodeId& address);

    // Helper: Get the input identities recorded for a transaction in the index
    std::vector<TrustNodeId> GetTransactionInputAddresses(const uint256& txid);
};

} // namespace CVM

#endif // CASCOIN_CVM_WALLETCLUSTER_H
