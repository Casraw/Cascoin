// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/clusterupdatehandler.h>
#include <cvm/cvmdb.h>
#include <cvm/walletcluster.h>
#include <cvm/trustpropagator.h>
#include <streams.h>
#include <clientversion.h>
#include <util.h>
#include <script/standard.h>
#include <pubkey.h>
#include <validation.h>
#include <chainparams.h>

namespace CVM {

// Storage key prefix for cluster update events
// Format: "cluster_event_{timestamp}_{blockHeight}_{eventType}"
static const std::string CLUSTER_EVENT_PREFIX = "cluster_event_";

// Storage key prefix for known cluster memberships
// Format: "cluster_member_{address}"
static const std::string CLUSTER_MEMBER_PREFIX = "cluster_member_";

//
// ClusterUpdateEvent helper methods
//

std::string ClusterUpdateEvent::GetStorageKey() const
{
    // Format: "cluster_event_{timestamp}_{blockHeight}_{eventType}"
    // Using timestamp first for chronological ordering
    char buf[128];
    snprintf(buf, sizeof(buf), "%s%010u_%010u_%u",
             CLUSTER_EVENT_PREFIX.c_str(),
             timestamp,
             blockHeight,
             static_cast<uint8_t>(eventType));
    return std::string(buf);
}

//
// ClusterUpdateHandler implementation
//

ClusterUpdateHandler::ClusterUpdateHandler(CVMDatabase& db, WalletClusterer& clust, 
                                           TrustPropagator& prop)
    : database(db)
    , clusterer(clust)
    , propagator(prop)
    , totalEventCount(0)
{
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Initialized\n");
    
    // Load known memberships from database on startup
    LoadKnownMemberships();
}

uint32_t ClusterUpdateHandler::ProcessBlock(int blockHeight, const std::vector<CTransaction>& transactions)
{
    // Requirements: 2.3, 2.4
    // 2.3: When a new address inherits trust, emit an event for audit purposes
    // 2.4: While processing new blocks, check for new addresses joining existing clusters
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: ProcessBlock at height %d with %zu transactions\n",
             blockHeight, transactions.size());
    
    if (transactions.empty()) {
        return 0;
    }
    
    uint32_t timestamp = static_cast<uint32_t>(GetTime());
    uint32_t updateCount = 0;
    
    // Step 1: Detect new cluster members from transaction inputs
    // Requirement 2.4: Check for new addresses joining existing clusters
    std::vector<std::pair<uint160, uint160>> newMembers = DetectNewMembers(transactions);
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Detected %zu new cluster members\n",
             newMembers.size());
    
    // Step 2: Process each new member - inherit trust and emit events
    for (const auto& memberPair : newMembers) {
        const uint160& newAddress = memberPair.first;
        const uint160& clusterId = memberPair.second;
        
        if (ProcessNewMember(newAddress, clusterId, blockHeight, timestamp)) {
            updateCount++;
        }
    }
    
    // Step 3: Detect and process cluster merges
    std::vector<std::pair<uint160, uint160>> merges = DetectClusterMerges(transactions);
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Detected %zu cluster merges\n",
             merges.size());
    
    for (const auto& mergePair : merges) {
        // For merges, we need to find the linking address
        // Use the first cluster's canonical address as the linking address for now
        uint160 linkingAddress = mergePair.first;
        
        if (ProcessClusterMerge(mergePair.first, mergePair.second, linkingAddress, 
                               blockHeight, timestamp)) {
            updateCount++;
        }
    }
    
    // Save updated memberships periodically
    if (updateCount > 0) {
        SaveKnownMemberships();
    }
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: ProcessBlock completed - %u updates processed\n",
             updateCount);
    
    return updateCount;
}

bool ClusterUpdateHandler::IsNewClusterMember(const uint160& address, const uint160& clusterId) const
{
    // Check if we've seen this address in this cluster before
    auto it = knownMemberships.find(address);
    
    if (it == knownMemberships.end()) {
        // Address not in any known cluster - it's new
        return true;
    }
    
    // Address is known, check if it's in a different cluster
    // (which would indicate a cluster merge scenario)
    return it->second != clusterId;
}

std::vector<ClusterUpdateEvent> ClusterUpdateHandler::GetRecentEvents(uint32_t maxCount) const
{
    std::lock_guard<std::mutex> lock(eventsMutex);
    
    std::vector<ClusterUpdateEvent> result;
    result.reserve(std::min(maxCount, static_cast<uint32_t>(recentEvents.size())));
    
    uint32_t count = 0;
    for (const auto& event : recentEvents) {
        if (count >= maxCount) break;
        result.push_back(event);
        count++;
    }
    
    return result;
}

std::vector<ClusterUpdateEvent> ClusterUpdateHandler::GetEventsForCluster(const uint160& clusterId, 
                                                                          uint32_t maxCount) const
{
    std::lock_guard<std::mutex> lock(eventsMutex);
    
    std::vector<ClusterUpdateEvent> result;
    
    for (const auto& event : recentEvents) {
        if (result.size() >= maxCount) break;
        
        if (event.clusterId == clusterId || event.mergedFromCluster == clusterId) {
            result.push_back(event);
        }
    }
    
    return result;
}

std::vector<ClusterUpdateEvent> ClusterUpdateHandler::GetEventsForAddress(const uint160& address,
                                                                          uint32_t maxCount) const
{
    std::lock_guard<std::mutex> lock(eventsMutex);
    
    std::vector<ClusterUpdateEvent> result;
    
    for (const auto& event : recentEvents) {
        if (result.size() >= maxCount) break;
        
        if (event.affectedAddress == address) {
            result.push_back(event);
        }
    }
    
    return result;
}

std::vector<ClusterUpdateEvent> ClusterUpdateHandler::GetEventsByType(ClusterUpdateEvent::Type eventType,
                                                                      uint32_t maxCount) const
{
    std::lock_guard<std::mutex> lock(eventsMutex);
    
    std::vector<ClusterUpdateEvent> result;
    
    for (const auto& event : recentEvents) {
        if (result.size() >= maxCount) break;
        
        if (event.eventType == eventType) {
            result.push_back(event);
        }
    }
    
    return result;
}

void ClusterUpdateHandler::ClearMembershipCache()
{
    knownMemberships.clear();
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Membership cache cleared\n");
}

void ClusterUpdateHandler::LoadKnownMemberships()
{
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Loading known memberships from database\n");
    
    // Get all keys with the cluster_member_ prefix
    std::vector<std::string> keys = database.ListKeysWithPrefix(CLUSTER_MEMBER_PREFIX);
    
    knownMemberships.clear();
    
    for (const std::string& key : keys) {
        // Read the cluster ID for this address
        std::vector<uint8_t> data;
        if (!database.ReadGeneric(key, data)) {
            continue;
        }
        
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            uint160 address;
            uint160 clusterId;
            ss >> address;
            ss >> clusterId;
            
            knownMemberships[address] = clusterId;
        } catch (const std::exception& e) {
            LogPrintf("ClusterUpdateHandler: Failed to deserialize membership from key %s: %s\n",
                      key, e.what());
        }
    }
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Loaded %zu known memberships\n",
             knownMemberships.size());
}

void ClusterUpdateHandler::SaveKnownMemberships()
{
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Saving %zu known memberships to database\n",
             knownMemberships.size());
    
    for (const auto& pair : knownMemberships) {
        const uint160& address = pair.first;
        const uint160& clusterId = pair.second;
        
        std::string key = CLUSTER_MEMBER_PREFIX + address.ToString();
        
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << address;
        ss << clusterId;
        
        std::vector<uint8_t> data(ss.begin(), ss.end());
        
        if (!database.WriteGeneric(key, data)) {
            LogPrintf("ClusterUpdateHandler: Failed to save membership for address %s\n",
                      address.ToString());
        }
    }
}

std::vector<std::pair<uint160, uint160>> ClusterUpdateHandler::DetectNewMembers(
    const std::vector<CTransaction>& transactions)
{
    // Detect new cluster members from transaction inputs
    // When multiple addresses are used as inputs in the same transaction,
    // they belong to the same wallet cluster
    
    std::vector<std::pair<uint160, uint160>> newMembers;
    std::set<uint160> processedAddresses;  // Avoid duplicates within this block
    
    for (const CTransaction& tx : transactions) {
        // Skip coinbase transactions
        if (tx.IsCoinBase()) {
            continue;
        }
        
        // Extract all input addresses from this transaction
        std::set<uint160> inputAddresses = ExtractInputAddresses(tx);
        
        // For each input address, check if it's new to its cluster
        for (const uint160& address : inputAddresses) {
            // Skip if we've already processed this address in this block
            if (processedAddresses.count(address) > 0) {
                continue;
            }
            processedAddresses.insert(address);
            
            // Get the cluster for this address from the clusterer
            // The clusterer maintains cluster information based on transaction analysis
            uint160 clusterId = clusterer.GetClusterForAddress(address);
            
            // If the cluster ID equals the address itself, it might be a single-address cluster
            // or a new address. Check if it's part of a larger cluster.
            if (clusterId.IsNull() || clusterId == address) {
                // Check if this address is part of a multi-input transaction
                // which would indicate it belongs to a cluster
                if (inputAddresses.size() <= 1) {
                    // Single input, not part of a multi-address wallet
                    continue;
                }
                // Multiple inputs - the clusterer should have linked them
                // Use the first address as the cluster ID if not set
                clusterId = address;
            }
            
            // Check if this is a new member of the cluster
            if (IsNewClusterMember(address, clusterId)) {
                newMembers.push_back(std::make_pair(address, clusterId));
                
                LogPrint(BCLog::CVM, "ClusterUpdateHandler: Detected new cluster member %s in cluster %s\n",
                         address.ToString(), clusterId.ToString());
            }
        }
    }
    
    return newMembers;
}

std::vector<std::pair<uint160, uint160>> ClusterUpdateHandler::DetectClusterMerges(
    const std::vector<CTransaction>& transactions)
{
    // Detect cluster merges from transaction inputs
    // When a transaction has inputs from addresses in different clusters,
    // those clusters should be merged
    
    std::vector<std::pair<uint160, uint160>> merges;
    std::set<std::pair<uint160, uint160>> processedMerges;  // Avoid duplicate merge pairs
    
    for (const CTransaction& tx : transactions) {
        // Extract all input addresses from this transaction
        std::set<uint160> inputAddresses = ExtractInputAddresses(tx);
        
        if (inputAddresses.size() < 2) {
            // Need at least 2 addresses to potentially merge clusters
            continue;
        }
        
        // Get clusters for all input addresses
        std::set<uint160> involvedClusters;
        std::map<uint160, uint160> addressToCluster;
        
        for (const uint160& address : inputAddresses) {
            uint160 clusterId = clusterer.GetClusterForAddress(address);
            
            if (!clusterId.IsNull()) {
                involvedClusters.insert(clusterId);
                addressToCluster[address] = clusterId;
            }
        }
        
        // If multiple clusters are involved, they should be merged
        if (involvedClusters.size() > 1) {
            // Create merge pairs for all cluster combinations
            std::vector<uint160> clusterList(involvedClusters.begin(), involvedClusters.end());
            
            for (size_t i = 0; i < clusterList.size(); i++) {
                for (size_t j = i + 1; j < clusterList.size(); j++) {
                    uint160 cluster1 = clusterList[i];
                    uint160 cluster2 = clusterList[j];
                    
                    // Normalize the pair (smaller first) to avoid duplicates
                    if (cluster2 < cluster1) {
                        std::swap(cluster1, cluster2);
                    }
                    
                    auto mergePair = std::make_pair(cluster1, cluster2);
                    
                    if (processedMerges.count(mergePair) == 0) {
                        processedMerges.insert(mergePair);
                        merges.push_back(mergePair);
                        
                        LogPrint(BCLog::CVM, "ClusterUpdateHandler: Detected cluster merge: %s + %s\n",
                                 cluster1.ToString(), cluster2.ToString());
                    }
                }
            }
        }
    }
    
    return merges;
}

bool ClusterUpdateHandler::ProcessNewMember(const uint160& newMember, const uint160& clusterId,
                                           uint32_t blockHeight, uint32_t timestamp)
{
    // Process a new cluster member:
    // 1. Emit NEW_MEMBER event
    // 2. Inherit trust from existing cluster members
    // 3. Emit TRUST_INHERITED event if trust was inherited
    // 4. Update known memberships
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Processing new member %s in cluster %s\n",
             newMember.ToString(), clusterId.ToString());
    
    // Requirement 6.3: Emit event for monitoring
    ClusterUpdateEvent newMemberEvent = ClusterUpdateEvent::NewMember(
        clusterId, newMember, blockHeight, timestamp);
    EmitEvent(newMemberEvent);
    
    // Requirement 2.1: Propagate all existing trust edges to the new address
    // Requirement 2.5: Retry up to 3 times before logging an error
    uint32_t inheritedCount = 0;
    bool inheritSuccess = false;
    
    for (uint32_t attempt = 0; attempt < MAX_INHERITANCE_RETRIES; attempt++) {
        try {
            inheritedCount = propagator.InheritTrustForNewMember(newMember, clusterId);
            inheritSuccess = true;
            break;
        } catch (const std::exception& e) {
            LogPrintf("ClusterUpdateHandler: Trust inheritance attempt %u failed for %s: %s\n",
                      attempt + 1, newMember.ToString(), e.what());
            
            if (attempt + 1 >= MAX_INHERITANCE_RETRIES) {
                LogPrintf("ClusterUpdateHandler: ERROR - Trust inheritance failed after %u retries for %s\n",
                          MAX_INHERITANCE_RETRIES, newMember.ToString());
            }
        }
    }
    
    // Requirement 2.3: Emit event when trust is inherited
    if (inheritSuccess && inheritedCount > 0) {
        ClusterUpdateEvent trustEvent = ClusterUpdateEvent::TrustInherited(
            clusterId, newMember, inheritedCount, blockHeight, timestamp);
        EmitEvent(trustEvent);
        
        LogPrint(BCLog::CVM, "ClusterUpdateHandler: New member %s inherited %u trust edges\n",
                 newMember.ToString(), inheritedCount);
    }
    
    // Update known memberships
    UpdateKnownMembership(newMember, clusterId);
    
    return true;
}

bool ClusterUpdateHandler::ProcessClusterMerge(const uint160& cluster1, const uint160& cluster2,
                                              const uint160& linkingAddress,
                                              uint32_t blockHeight, uint32_t timestamp)
{
    // Process a cluster merge:
    // 1. Emit CLUSTER_MERGE event
    // 2. Call TrustPropagator to handle trust combination
    // 3. Update known memberships for all affected addresses
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Processing cluster merge: %s + %s (linking: %s)\n",
             cluster1.ToString(), cluster2.ToString(), linkingAddress.ToString());
    
    // Requirement 6.3: Emit event for monitoring
    // The first cluster absorbs the second
    ClusterUpdateEvent mergeEvent = ClusterUpdateEvent::ClusterMerge(
        cluster1, cluster2, linkingAddress, blockHeight, timestamp);
    EmitEvent(mergeEvent);
    
    // Requirements 6.1, 6.2: Combine trust relations from both clusters
    // The merged cluster ID is typically the first cluster (or the one with more members)
    uint160 mergedClusterId = cluster1;
    
    bool mergeSuccess = propagator.HandleClusterMerge(cluster1, cluster2, mergedClusterId);
    
    if (!mergeSuccess) {
        LogPrintf("ClusterUpdateHandler: Warning - HandleClusterMerge returned false for %s + %s\n",
                  cluster1.ToString(), cluster2.ToString());
        // Continue anyway - the merge event has been recorded
    }
    
    // Update known memberships for all addresses in the merged cluster
    std::set<uint160> mergedMembers = clusterer.GetClusterMembers(mergedClusterId);
    
    for (const uint160& member : mergedMembers) {
        UpdateKnownMembership(member, mergedClusterId);
    }
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Cluster merge completed - %zu members in merged cluster\n",
             mergedMembers.size());
    
    return true;
}

void ClusterUpdateHandler::EmitEvent(const ClusterUpdateEvent& event)
{
    // Requirement 6.3: Emit events for monitoring
    
    // Store event in database for persistence
    StoreEvent(event);
    
    // Add to recent events cache (bounded by MAX_RECENT_EVENTS)
    {
        std::lock_guard<std::mutex> lock(eventsMutex);
        
        // Add to front (newest first)
        recentEvents.push_front(event);
        
        // Trim if exceeds max size
        while (recentEvents.size() > MAX_RECENT_EVENTS) {
            recentEvents.pop_back();
        }
    }
    
    totalEventCount++;
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Emitted %s event for cluster %s (total: %lu)\n",
             event.GetEventTypeName(), event.clusterId.ToString(), totalEventCount);
}

bool ClusterUpdateHandler::StoreEvent(const ClusterUpdateEvent& event)
{
    std::string key = event.GetStorageKey();
    
    // Serialize the event
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << event;
    
    // Convert to vector for storage
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    bool result = database.WriteGeneric(key, data);
    
    if (!result) {
        LogPrintf("ClusterUpdateHandler: Failed to store event with key: %s\n", key);
    }
    
    return result;
}

void ClusterUpdateHandler::UpdateKnownMembership(const uint160& address, const uint160& clusterId)
{
    knownMemberships[address] = clusterId;
    
    LogPrint(BCLog::CVM, "ClusterUpdateHandler: Updated membership for %s -> cluster %s\n",
             address.ToString(), clusterId.ToString());
}

std::set<uint160> ClusterUpdateHandler::ExtractInputAddresses(const CTransaction& tx) const
{
    std::set<uint160> addresses;
    
    // Skip coinbase transactions - they have no real inputs
    if (tx.IsCoinBase()) {
        return addresses;
    }
    
    // Get consensus params for transaction lookup
    const Consensus::Params* consensusParams = nullptr;
    try {
        consensusParams = &(Params().GetConsensus());
    } catch (...) {
        LogPrint(BCLog::CVM, "ClusterUpdateHandler: Chain params not initialized, cannot extract addresses\n");
        return addresses;
    }
    
    // For each input, look up the previous transaction's output to get the address
    for (const CTxIn& txin : tx.vin) {
        // Skip null prevouts (shouldn't happen for non-coinbase)
        if (txin.prevout.IsNull()) {
            continue;
        }
        
        // Get the previous transaction
        CTransactionRef prevTx;
        uint256 hashBlock;
        
        if (GetTransaction(txin.prevout.hash, prevTx, *consensusParams, hashBlock)) {
            // Check that the output index is valid
            if (txin.prevout.n < prevTx->vout.size()) {
                const CTxOut& prevOut = prevTx->vout[txin.prevout.n];
                
                // Extract the destination address from the scriptPubKey
                CTxDestination dest;
                if (ExtractDestination(prevOut.scriptPubKey, dest)) {
                    // Handle different destination types
                    if (const CKeyID* keyId = boost::get<CKeyID>(&dest)) {
                        // P2PKH address
                        addresses.insert(uint160(*keyId));
                        LogPrint(BCLog::CVM, "ClusterUpdateHandler: Extracted P2PKH address %s from input\n",
                                 uint160(*keyId).ToString());
                    } else if (const CScriptID* scriptId = boost::get<CScriptID>(&dest)) {
                        // P2SH address
                        addresses.insert(uint160(*scriptId));
                        LogPrint(BCLog::CVM, "ClusterUpdateHandler: Extracted P2SH address %s from input\n",
                                 uint160(*scriptId).ToString());
                    } else if (const WitnessV0KeyHash* witnessKeyHash = boost::get<WitnessV0KeyHash>(&dest)) {
                        // P2WPKH (native SegWit) address
                        uint160 addr;
                        memcpy(addr.begin(), witnessKeyHash->begin(), 20);
                        addresses.insert(addr);
                        LogPrint(BCLog::CVM, "ClusterUpdateHandler: Extracted P2WPKH address %s from input\n",
                                 addr.ToString());
                    }
                    // Note: We skip WitnessV0ScriptHash and WitnessUnknown as they are 32 bytes
                    // and don't fit in uint160
                }
            }
        } else {
            LogPrint(BCLog::CVM, "ClusterUpdateHandler: Could not find previous tx %s for input\n",
                     txin.prevout.hash.ToString().substr(0, 16));
        }
    }
    
    return addresses;
}

} // namespace CVM
