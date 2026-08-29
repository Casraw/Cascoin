// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CLUSTERUPDATEHANDLER_H
#define CASCOIN_CVM_CLUSTERUPDATEHANDLER_H

#include <uint256.h>
#include <serialize.h>
#include <cvm/trustnodeid.h>
#include <primitives/transaction.h>
#include <map>
#include <set>
#include <vector>
#include <deque>
#include <mutex>

namespace CVM {

// Forward declarations
class CVMDatabase;
class WalletClusterer;
class TrustPropagator;

/**
 * ClusterMergeCandidate - A detected merge of two clusters plus the actual
 * address that links them.
 *
 * When a transaction spends inputs belonging to two different clusters, those
 * clusters are merged. The linkingAddress is one of that transaction's real
 * input addresses that belongs to one of the merged clusters — i.e. the actual
 * address that connects them — rather than a cluster canonical id placeholder.
 */
struct ClusterMergeCandidate {
    TrustNodeId cluster1;        // First cluster (absorbing cluster)
    TrustNodeId cluster2;        // Second cluster (merged into cluster1)
    TrustNodeId linkingAddress;  // Real identity connecting the two clusters
};

/**
 * ClusterUpdateEvent - Event emitted when cluster membership changes
 * 
 * This struct represents events that occur when wallet cluster membership
 * changes, such as new addresses joining a cluster, clusters merging, or
 * trust being inherited by new members.
 * 
 * Storage key format: "cluster_event_{timestamp}_{eventId}"
 * 
 * Requirements: 2.3, 6.3
 */
struct ClusterUpdateEvent {
    /**
     * Event type enumeration
     * 
     * NEW_MEMBER: A new address has been detected as belonging to an existing cluster
     * CLUSTER_MERGE: Two previously separate clusters have been identified as the same wallet
     * TRUST_INHERITED: Trust edges have been propagated to a new cluster member
     */
    enum class Type : uint8_t {
        NEW_MEMBER = 0,      // New address joined cluster
        CLUSTER_MERGE = 1,   // Two clusters merged
        TRUST_INHERITED = 2  // Trust was inherited by new member
    };
    
    Type eventType;              // Type of cluster update event
    TrustNodeId clusterId;       // The cluster that was affected
    TrustNodeId affectedAddress; // The identity that triggered or was affected by the event
    TrustNodeId mergedFromCluster; // For CLUSTER_MERGE: the cluster that was merged into clusterId
    uint32_t blockHeight;        // Block height when the event occurred
    uint32_t timestamp;          // Unix timestamp when the event was processed
    uint32_t inheritedEdgeCount; // For TRUST_INHERITED: number of edges inherited
    
    ClusterUpdateEvent()
        : eventType(Type::NEW_MEMBER)
        , blockHeight(0)
        , timestamp(0)
        , inheritedEdgeCount(0)
    {}
    
    // Primary typed constructor.
    ClusterUpdateEvent(Type type, const TrustNodeId& cluster, const TrustNodeId& affected,
                       uint32_t height, uint32_t ts)
        : eventType(type)
        , clusterId(cluster)
        , affectedAddress(affected)
        , blockHeight(height)
        , timestamp(ts)
        , inheritedEdgeCount(0)
    {}

    // Thin uint160 wrapper constructor (legacy P2PKH callers).
    ClusterUpdateEvent(Type type, const uint160& cluster, const uint160& affected,
                       uint32_t height, uint32_t ts)
        : ClusterUpdateEvent(type, TrustNodeId::FromLegacyUint160(cluster),
                             TrustNodeId::FromLegacyUint160(affected), height, ts)
    {}
    
    /**
     * Create a NEW_MEMBER event (typed identities).
     */
    static ClusterUpdateEvent NewMember(const TrustNodeId& cluster, const TrustNodeId& newMember,
                                        uint32_t height, uint32_t ts) {
        ClusterUpdateEvent event(Type::NEW_MEMBER, cluster, newMember, height, ts);
        return event;
    }
    
    /**
     * Create a CLUSTER_MERGE event (typed identities).
     */
    static ClusterUpdateEvent ClusterMerge(const TrustNodeId& targetCluster, const TrustNodeId& sourceCluster,
                                           const TrustNodeId& linkingAddress, uint32_t height, uint32_t ts) {
        ClusterUpdateEvent event(Type::CLUSTER_MERGE, targetCluster, linkingAddress, height, ts);
        event.mergedFromCluster = sourceCluster;
        return event;
    }
    
    /**
     * Create a TRUST_INHERITED event (typed identities).
     */
    static ClusterUpdateEvent TrustInherited(const TrustNodeId& cluster, const TrustNodeId& newMember,
                                             uint32_t edgeCount, uint32_t height, uint32_t ts) {
        ClusterUpdateEvent event(Type::TRUST_INHERITED, cluster, newMember, height, ts);
        event.inheritedEdgeCount = edgeCount;
        return event;
    }

    // --- Thin uint160 wrapper factory methods (legacy P2PKH callers) ------
    static ClusterUpdateEvent NewMember(const uint160& cluster, const uint160& newMember,
                                        uint32_t height, uint32_t ts) {
        return NewMember(TrustNodeId::FromLegacyUint160(cluster),
                         TrustNodeId::FromLegacyUint160(newMember), height, ts);
    }
    static ClusterUpdateEvent ClusterMerge(const uint160& targetCluster, const uint160& sourceCluster,
                                           const uint160& linkingAddress, uint32_t height, uint32_t ts) {
        return ClusterMerge(TrustNodeId::FromLegacyUint160(targetCluster),
                            TrustNodeId::FromLegacyUint160(sourceCluster),
                            TrustNodeId::FromLegacyUint160(linkingAddress), height, ts);
    }
    static ClusterUpdateEvent TrustInherited(const uint160& cluster, const uint160& newMember,
                                             uint32_t edgeCount, uint32_t height, uint32_t ts) {
        return TrustInherited(TrustNodeId::FromLegacyUint160(cluster),
                              TrustNodeId::FromLegacyUint160(newMember), edgeCount, height, ts);
    }
    
    /**
     * Generate the database storage key for this event
     * Format: "cluster_event_{timestamp}_{blockHeight}_{eventType}"
     */
    std::string GetStorageKey() const;
    
    /**
     * Get a human-readable description of the event type
     */
    std::string GetEventTypeName() const {
        switch (eventType) {
            case Type::NEW_MEMBER: return "NEW_MEMBER";
            case Type::CLUSTER_MERGE: return "CLUSTER_MERGE";
            case Type::TRUST_INHERITED: return "TRUST_INHERITED";
            default: return "UNKNOWN";
        }
    }
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint8_t typeVal = static_cast<uint8_t>(eventType);
        READWRITE(typeVal);
        if (ser_action.ForRead()) {
            eventType = static_cast<Type>(typeVal);
        }
        READWRITE(clusterId);
        READWRITE(affectedAddress);
        READWRITE(mergedFromCluster);
        READWRITE(blockHeight);
        READWRITE(timestamp);
        READWRITE(inheritedEdgeCount);
    }
    
    bool operator==(const ClusterUpdateEvent& other) const {
        return eventType == other.eventType &&
               clusterId == other.clusterId &&
               affectedAddress == other.affectedAddress &&
               mergedFromCluster == other.mergedFromCluster &&
               blockHeight == other.blockHeight &&
               timestamp == other.timestamp &&
               inheritedEdgeCount == other.inheritedEdgeCount;
    }
    
    bool operator!=(const ClusterUpdateEvent& other) const {
        return !(*this == other);
    }
};

/**
 * ClusterUpdateHandler - Processes cluster changes during block processing
 * 
 * This class is responsible for:
 * 1. Detecting new cluster members from transaction inputs
 * 2. Triggering trust inheritance for new members
 * 3. Detecting and handling cluster merges
 * 4. Emitting events for monitoring and audit purposes
 * 
 * Integration:
 * - Called by CVMBlockProcessor after processing CVM transactions
 * - Uses WalletClusterer to identify cluster membership
 * - Uses TrustPropagator to inherit trust for new members
 * 
 * Requirements: 2.3, 2.4, 6.3
 */
class ClusterUpdateHandler {
public:
    // Maximum number of recent events to keep in memory
    static constexpr uint32_t MAX_RECENT_EVENTS = 1000;
    
    // Maximum retry attempts for trust inheritance (Requirement 2.5)
    static constexpr uint32_t MAX_INHERITANCE_RETRIES = 3;
    
    /**
     * Constructor
     * 
     * @param db Reference to CVM database for persistence
     * @param clusterer Reference to WalletClusterer for cluster identification
     * @param propagator Reference to TrustPropagator for trust inheritance
     */
    ClusterUpdateHandler(CVMDatabase& db, WalletClusterer& clusterer, 
                        TrustPropagator& propagator);
    
    ~ClusterUpdateHandler() = default;
    
    /**
     * Process a new block for cluster updates
     * 
     * Analyzes all transactions in the block to detect:
     * - New addresses joining existing clusters
     * - Cluster merges (addresses linking previously separate clusters)
     * 
     * For each detected change, triggers trust inheritance and emits events.
     * 
     * @param blockHeight Height of the block being processed
     * @param transactions Transactions in the block
     * @return Number of cluster updates processed
     * 
     * Requirements: 2.3, 2.4
     */
    uint32_t ProcessBlock(int blockHeight, const std::vector<CTransaction>& transactions);
    
    /**
     * Check if an address is new to its cluster
     * 
     * Compares current cluster membership against known memberships
     * to determine if an address was recently added.
     * 
     * @param address Address to check
     * @param clusterId Cluster it belongs to
     * @return true if identity is newly detected in cluster
     */
    bool IsNewClusterMember(const TrustNodeId& address, const TrustNodeId& clusterId) const;
    
    /**
     * Get recent cluster update events
     * 
     * Returns the most recent events for monitoring and audit purposes.
     * Events are ordered from newest to oldest.
     * 
     * @param maxCount Maximum events to return (default: 100)
     * @return Vector of recent events
     * 
     * Requirement: 6.3
     */
    std::vector<ClusterUpdateEvent> GetRecentEvents(uint32_t maxCount = 100) const;
    
    /**
     * Get events for a specific cluster
     * 
     * @param clusterId Cluster to query
     * @param maxCount Maximum events to return
     * @return Vector of events affecting the cluster
     */
    std::vector<ClusterUpdateEvent> GetEventsForCluster(const TrustNodeId& clusterId, 
                                                        uint32_t maxCount = 100) const;
    
    /**
     * Get events for a specific identity
     * 
     * @param address Identity to query
     * @param maxCount Maximum events to return
     * @return Vector of events affecting the identity
     */
    std::vector<ClusterUpdateEvent> GetEventsForAddress(const TrustNodeId& address,
                                                        uint32_t maxCount = 100) const;

    // --- Thin uint160 wrappers (legacy P2PKH callers) ---------------------
    bool IsNewClusterMember(const uint160& address, const uint160& clusterId) const;
    std::vector<ClusterUpdateEvent> GetEventsForCluster(const uint160& clusterId,
                                                        uint32_t maxCount = 100) const;
    std::vector<ClusterUpdateEvent> GetEventsForAddress(const uint160& address,
                                                        uint32_t maxCount = 100) const;
    
    /**
     * Get events by type
     * 
     * @param eventType Type of events to retrieve
     * @param maxCount Maximum events to return
     * @return Vector of events of the specified type
     */
    std::vector<ClusterUpdateEvent> GetEventsByType(ClusterUpdateEvent::Type eventType,
                                                    uint32_t maxCount = 100) const;
    
    /**
     * Get total number of events processed
     * 
     * @return Total event count since handler creation
     */
    uint64_t GetTotalEventCount() const { return totalEventCount; }
    
    /**
     * Clear the known memberships cache
     * 
     * Forces re-detection of all cluster memberships on next block.
     * Useful for testing or after database recovery.
     */
    void ClearMembershipCache();
    
    /**
     * Load known memberships from database
     * 
     * Restores the membership cache from persistent storage.
     * Called during initialization.
     */
    void LoadKnownMemberships();
    
    /**
     * Save known memberships to database
     * 
     * Persists the membership cache for recovery.
     */
    void SaveKnownMemberships();

private:
    CVMDatabase& database;
    WalletClusterer& clusterer;
    TrustPropagator& propagator;
    
    // Track known cluster memberships: identity -> clusterId
    // Used to detect when an identity joins a new cluster
    std::map<TrustNodeId, TrustNodeId> knownMemberships;
    
    // Recent events for quick access (bounded by MAX_RECENT_EVENTS)
    mutable std::deque<ClusterUpdateEvent> recentEvents;
    mutable std::mutex eventsMutex;
    
    // Statistics
    uint64_t totalEventCount;
    
    /**
     * Detect new cluster members from transaction inputs
     * 
     * Analyzes transaction inputs to find addresses that are:
     * 1. Part of a cluster (multiple inputs from same wallet)
     * 2. Not previously known to be in that cluster
     * 
     * @param transactions Transactions to analyze
     * @return Vector of (identity, clusterId) pairs for new members
     */
    std::vector<std::pair<TrustNodeId, TrustNodeId>> DetectNewMembers(
        const std::vector<CTransaction>& transactions);
    
    /**
     * Detect cluster merges from transaction inputs
     * 
     * When a transaction has inputs from addresses in different clusters,
     * those clusters should be merged.
     * 
     * @param transactions Transactions to analyze
     * @return Vector of merge candidates, each carrying the two clusters and the
     *         real address that links them
     */
    std::vector<ClusterMergeCandidate> DetectClusterMerges(
        const std::vector<CTransaction>& transactions);
    
    /**
     * Process a new cluster member
     * 
     * Triggers trust inheritance and emits appropriate events.
     * 
     * @param newMember The new member address
     * @param clusterId The cluster it joined
     * @param blockHeight Current block height
     * @param timestamp Current timestamp
     * @return true if processed successfully
     */
    bool ProcessNewMember(const TrustNodeId& newMember, const TrustNodeId& clusterId,
                         uint32_t blockHeight, uint32_t timestamp);
    
    /**
     * Process a cluster merge
     * 
     * Combines trust relations and emits merge event.
     * 
     * @param cluster1 First cluster ID
     * @param cluster2 Second cluster ID
     * @param linkingAddress Address that caused the merge
     * @param blockHeight Current block height
     * @param timestamp Current timestamp
     * @return true if processed successfully
     */
    bool ProcessClusterMerge(const TrustNodeId& cluster1, const TrustNodeId& cluster2,
                            const TrustNodeId& linkingAddress,
                            uint32_t blockHeight, uint32_t timestamp);
    
    /**
     * Emit cluster update event
     * 
     * Stores the event in the database and adds to recent events cache.
     * 
     * @param event Event to emit
     * 
     * Requirement: 6.3
     */
    void EmitEvent(const ClusterUpdateEvent& event);
    
    /**
     * Store event in database
     * 
     * @param event Event to store
     * @return true if successful
     */
    bool StoreEvent(const ClusterUpdateEvent& event);
    
    /**
     * Update known membership for an address
     * 
     * @param address Identity to update
     * @param clusterId New cluster ID
     */
    void UpdateKnownMembership(const TrustNodeId& address, const TrustNodeId& clusterId);
    
    /**
     * Extract identities from transaction inputs.
     *
     * Every supported destination type (P2PKH/P2SH/P2WPKH/P2WSH/quantum) is
     * resolved via TrustNodeId::FromDestination; unresolved prevouts, invalid
     * indexes, CNoDestination and WitnessUnknown are ignored.
     * 
     * @param tx Transaction to analyze
     * @return Set of identities used as inputs
     */
    std::set<TrustNodeId> ExtractInputAddresses(const CTransaction& tx) const;
};

} // namespace CVM

#endif // CASCOIN_CVM_CLUSTERUPDATEHANDLER_H
