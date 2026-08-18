// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/walletcluster.h>
#include <cvm/securehat.h>
#include <cvm/reputation.h>
#include <cvm/migration_observability.h>
#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <txmempool.h>
#include <util.h>
#include <script/standard.h>
#include <key.h>
#include <pubkey.h>
#include <keystore.h>
#include <base58.h>
#include <streams.h>
#include <algorithm>

namespace CVM {

// Current typed key prefixes. Identity segments are exactly ToKeyString().
static const std::string CLUSTER_PREFIX = "wc_";              // wc_<clusterTNI>
static const std::string ADDRESS_MAP_PREFIX = "wca_";         // wca_<memberTNI>
static const std::string TX_ADDR_INDEX_PREFIX = "txcluster_addr_"; // txcluster_addr_<TNI>
static const std::string TX_INPUT_INDEX_PREFIX = "txcluster_tx_";  // txcluster_tx_<txid64>

WalletClusterer::WalletClusterer(CVMDatabase& db) 
    : database(db), cache_valid(false)
{
    LoadClusters();
}

void WalletClusterer::BuildClusters()
{
    LogPrintf("WalletClusterer: Building address clusters from blockchain...\n");

    // Deterministic, idempotent rebuild from active-chain/index data. This is
    // the supported way to materialize clusters after the migration; no
    // automatic startup chain scan is introduced (clause 3.2, 5.3).
    RecordMigrationEvent(MigrationEvent::RebuildStart, CLUSTER_PREFIX);

    // Reset clusters
    address_to_cluster.clear();
    clusters.clear();
    
    // ---- Chain-based analysis (when a live chain is available) ------------
    // Iterate through all blocks and analyze transactions from the active chain.
    if (chainActive.Tip()) {
        // Get consensus params once (safer if Params() is not yet initialized)
        const Consensus::Params* consensusParams = nullptr;
        try {
            consensusParams = &(Params().GetConsensus());
        } catch (...) {
            consensusParams = nullptr;
            LogPrintf("WalletClusterer: Chain params not initialized yet, skipping chain analysis\n");
        }

        if (consensusParams) {
            CBlockIndex* pindex = chainActive.Genesis();
            int analyzed_count = 0;
            int block_count = 0;

            while (pindex) {
                try {
                    CBlock block;
                    if (ReadBlockFromDisk(block, pindex, *consensusParams)) {
                        block_count++;
                        for (const CTransactionRef& tx : block.vtx) {
                            // Skip coinbase
                            if (tx->IsCoinBase()) {
                                continue;
                            }

                            // Analyze transaction for clustering
                            AnalyzeTransaction(tx->GetHash());
                            analyzed_count++;
                        }
                    }
                } catch (const std::exception& e) {
                    LogPrintf("WalletClusterer: Error analyzing block: %s\n", e.what());
                }

                // Move to next block
                pindex = chainActive.Next(pindex);
            }

            LogPrintf("WalletClusterer: Analyzed %d blocks, %d transactions from chain\n",
                      block_count, analyzed_count);
        }
    } else {
        LogPrintf("WalletClusterer: No active chain; using transaction/address index only\n");
    }

    // ---- Index-based analysis (transaction/address index) ------------------
    // Cluster identities using the common-input-ownership heuristic over the
    // persisted transaction/address index. This is the data source that
    // GetAddressTransactions() reads from, letting clustering operate on real
    // data even without a full chain replay. Identity key segments are strictly
    // parsed as canonical TrustNodeId values; malformed/noncanonical segments
    // are skipped, never reinterpreted.
    {
        std::vector<std::string> addrKeys = database.ListKeysWithPrefix(TX_ADDR_INDEX_PREFIX);
        std::set<uint256> processedTxs;

        for (const std::string& key : addrKeys) {
            if (key.size() <= TX_ADDR_INDEX_PREFIX.size()) {
                continue;
            }
            std::string segment = key.substr(TX_ADDR_INDEX_PREFIX.size());
            TrustNodeId address;
            std::string err;
            if (!ParseKeyStringToTrustNode(segment, address, err)) {
                RecordMigrationEvent(MigrationEvent::MalformedRecord,
                                     TX_ADDR_INDEX_PREFIX, key);
                LogPrintf("WalletClusterer: skipping malformed address index key \"%s\": %s\n",
                          key, err);
                continue;
            }

            // Fetch the identity's transactions from the index.
            std::vector<uint256> txids = GetAddressTransactions(address);
            for (const uint256& txid : txids) {
                if (!processedTxs.insert(txid).second) {
                    continue;  // Already processed this transaction
                }

                // Common-input-ownership heuristic: all input identities of a
                // single transaction are grouped into the same cluster. Inputs
                // are exact typed identities (no low-20-byte projection).
                std::vector<TrustNodeId> inputs = GetTransactionInputAddresses(txid);
                if (inputs.size() > 1) {
                    for (size_t i = 1; i < inputs.size(); ++i) {
                        UnionClusters(inputs[0], inputs[i]);
                    }
                }
            }
        }
    }

    LogPrintf("WalletClusterer: Cluster building complete, found %d clusters\n",
              clusters.size());

    cache_valid = true;
    SaveClusters();

    RecordMigrationEvent(MigrationEvent::RebuildComplete, CLUSTER_PREFIX);
}

void WalletClusterer::AnalyzeTransaction(const uint256& txid)
{
    // Get consensus params (with safety check)
    const Consensus::Params* consensusParams = nullptr;
    try {
        consensusParams = &(Params().GetConsensus());
    } catch (...) {
        return;  // Chain params not initialized
    }
    
    // Get transaction
    CTransactionRef tx;
    uint256 hashBlock;
    
    if (!GetTransaction(txid, tx, *consensusParams, hashBlock)) {
        return;
    }
    
    // Extract all input identities. Every supported destination type
    // (P2PKH/P2SH/P2WPKH/P2WSH/quantum) participates; unresolved prevouts,
    // invalid indexes, CNoDestination and WitnessUnknown are ignored.
    // Deduplication uses exact typed identities.
    std::set<TrustNodeId> input_addresses;
    
    for (const CTxIn& txin : tx->vin) {
        // Get previous transaction to find input identity
        CTransactionRef prev_tx;
        uint256 prev_block;
        
        if (GetTransaction(txin.prevout.hash, prev_tx, *consensusParams, prev_block)) {
            if (txin.prevout.n < prev_tx->vout.size()) {
                const CTxOut& prev_out = prev_tx->vout[txin.prevout.n];
                
                CTxDestination dest;
                if (ExtractDestination(prev_out.scriptPubKey, dest)) {
                    TrustNodeId id;
                    if (TrustNodeId::FromDestination(dest, id)) {
                        input_addresses.insert(id);
                    }
                }
            }
        }
    }
    
    // HEURISTIC 1: Common Input Heuristic
    // If multiple identities are used as inputs in the same transaction,
    // they likely belong to the same wallet. Union when >= 2 remain after dedup.
    if (input_addresses.size() > 1) {
        auto it = input_addresses.begin();
        TrustNodeId first = *it;
        ++it;
        
        // Link all input identities together
        while (it != input_addresses.end()) {
            UnionClusters(first, *it);
            ++it;
        }
    }
    
    // HEURISTIC 2: Change Address Detection
    // For 2-output transactions, the smaller output is likely change. Applies
    // to any supported typed change destination.
    if (tx->vout.size() == 2 && input_addresses.size() > 0) {
        CAmount out0 = tx->vout[0].nValue;
        CAmount out1 = tx->vout[1].nValue;
        
        // Identify likely change output (smaller one)
        size_t change_idx = (out0 < out1) ? 0 : 1;
        
        CTxDestination change_dest;
        if (ExtractDestination(tx->vout[change_idx].scriptPubKey, change_dest)) {
            TrustNodeId change_id;
            if (TrustNodeId::FromDestination(change_dest, change_id)) {
                // Link change identity with input identities
                for (const TrustNodeId& input_addr : input_addresses) {
                    UnionClusters(input_addr, change_id);
                }
            }
        }
    }
}

TrustNodeId WalletClusterer::FindClusterRoot(const TrustNodeId& address)
{
    // Union-Find with path compression
    if (address_to_cluster.find(address) == address_to_cluster.end()) {
        // Identity not in any cluster yet, it is its own root
        address_to_cluster[address] = address;
        return address;
    }
    
    TrustNodeId root = address_to_cluster[address];
    
    // Path compression
    if (root != address) {
        root = FindClusterRoot(root);
        address_to_cluster[address] = root;
    }
    
    return root;
}

void WalletClusterer::UnionClusters(const TrustNodeId& addr1, const TrustNodeId& addr2)
{
    TrustNodeId root1 = FindClusterRoot(addr1);
    TrustNodeId root2 = FindClusterRoot(addr2);

    // Ensure each root has a cluster record that includes itself and the
    // originally referenced identity as members. This keeps member_addresses a
    // complete set (both endpoints and the root), independent of insertion or
    // union order, which is required for deterministic cluster IDs.
    if (clusters.find(root1) == clusters.end()) {
        clusters[root1].cluster_id = root1;
    }
    clusters[root1].member_addresses.insert(root1);
    clusters[root1].member_addresses.insert(addr1);

    if (clusters.find(root2) == clusters.end()) {
        clusters[root2].cluster_id = root2;
    }
    clusters[root2].member_addresses.insert(root2);
    clusters[root2].member_addresses.insert(addr2);

    if (root1 == root2) {
        return; // Already in same cluster
    }

    // Merge smaller cluster into larger one (existing near-linear union strategy)
    if (clusters[root1].member_addresses.size() < clusters[root2].member_addresses.size()) {
        std::swap(root1, root2);
    }

    // Merge root2's members into root1 and repoint them.
    address_to_cluster[root2] = root1;
    for (const TrustNodeId& member : clusters[root2].member_addresses) {
        clusters[root1].member_addresses.insert(member);
        address_to_cluster[member] = root1;
    }

    // Update timestamps
    if (clusters[root2].first_seen < clusters[root1].first_seen || clusters[root1].first_seen == 0) {
        clusters[root1].first_seen = clusters[root2].first_seen;
    }
    if (clusters[root2].last_activity > clusters[root1].last_activity) {
        clusters[root1].last_activity = clusters[root2].last_activity;
    }
    clusters[root1].transaction_count += clusters[root2].transaction_count;

    // Remove old cluster
    clusters.erase(root2);
}

TrustNodeId WalletClusterer::ExternalClusterId(const TrustNodeId& root)
{
    // The externally visible cluster ID is deterministic: the minimum member
    // under TrustNodeId::operator<. std::set keeps members sorted, so begin()
    // is the minimum. Lone identities are their own (minimum) cluster ID.
    auto it = clusters.find(root);
    if (it != clusters.end() && !it->second.member_addresses.empty()) {
        return *it->second.member_addresses.begin();
    }
    return root;
}

TrustNodeId WalletClusterer::GetClusterForAddress(const TrustNodeId& address)
{
    return ExternalClusterId(FindClusterRoot(address));
}

std::set<TrustNodeId> WalletClusterer::GetClusterMembers(const TrustNodeId& address)
{
    TrustNodeId root = FindClusterRoot(address);
    
    auto it = clusters.find(root);
    if (it != clusters.end()) {
        return it->second.member_addresses;
    }
    
    // Identity is alone
    return { address };
}

WalletClusterInfo WalletClusterer::GetClusterInfo(const TrustNodeId& cluster_id)
{
    TrustNodeId root = FindClusterRoot(cluster_id);
    auto it = clusters.find(root);
    if (it != clusters.end()) {
        WalletClusterInfo info = it->second;
        info.cluster_id = ExternalClusterId(root);
        return info;
    }
    
    // Single-identity cluster
    WalletClusterInfo info;
    info.cluster_id = cluster_id;
    info.member_addresses.insert(cluster_id);
    return info;
}

void WalletClusterer::LinkAddresses(const TrustNodeId& addr1, const TrustNodeId& addr2)
{
    UnionClusters(addr1, addr2);
    SaveClusters();
}

double WalletClusterer::CalculateClusterReputation(const TrustNodeId& cluster_id)
{
    std::set<TrustNodeId> members = GetClusterMembers(cluster_id);
    
    if (members.empty()) {
        return 0.0;
    }
    
    // Strategy: Use MINIMUM reputation (most conservative)
    // This prevents attackers from using "clean" addresses to boost reputation
    double min_reputation = 100.0;
    
    ReputationSystem rep_system(database);
    
    for (const TrustNodeId& member : members) {
        CVM::ReputationScore score;
        if (rep_system.GetReputation(member, score)) {
            // Normalize score from internal representation to 0-100 scale
            double normalized = ((double)score.score / 100.0);
            if (normalized < 0) normalized = 0;
            if (normalized > 100) normalized = 100;
            
            if (normalized < min_reputation) {
                min_reputation = normalized;
            }
        }
    }
    
    return min_reputation;
}

double WalletClusterer::GetEffectiveReputation(const TrustNodeId& address)
{
    TrustNodeId cluster_id = GetClusterForAddress(address);
    return CalculateClusterReputation(cluster_id);
}

double WalletClusterer::GetEffectiveHATScore(const TrustNodeId& address)
{
    TrustNodeId cluster_id = GetClusterForAddress(address);
    std::set<TrustNodeId> members = GetClusterMembers(cluster_id);
    
    if (members.empty()) {
        return 0.0;
    }
    
    // Use MINIMUM HAT v2 score across all identities in cluster
    double min_score = 100.0;
    
    SecureHAT hat(database);

    // Preserve the prior "global viewer" behavior: a zero P2PKH viewer.
    const TrustNodeId globalViewer = TrustNodeId::FromLegacyUint160(uint160());
    
    for (const TrustNodeId& member : members) {
        double score = hat.CalculateFinalTrust(member, globalViewer);
        if (score < min_score) {
            min_score = score;
        }
    }
    
    return min_score;
}

uint32_t WalletClusterer::GetTotalClusters() const
{
    return clusters.size();
}

uint32_t WalletClusterer::GetLargestClusterSize() const
{
    uint32_t max_size = 0;
    
    for (const auto& pair : clusters) {
        uint32_t size = pair.second.member_addresses.size();
        if (size > max_size) {
            max_size = size;
        }
    }
    
    return max_size;
}

std::map<uint160, uint32_t> WalletClusterer::GetClusterSizeMap() const
{
    // Legacy accessor: keyed by the low-20-byte projection of the cluster ID.
    std::map<uint160, uint32_t> size_map;
    
    for (const auto& pair : clusters) {
        TrustNodeId cid = pair.first;
        if (!pair.second.member_addresses.empty()) {
            cid = *pair.second.member_addresses.begin();
        }
        size_map[cid.ToUint160()] = pair.second.member_addresses.size();
    }
    
    return size_map;
}

void WalletClusterer::InvalidateCache()
{
    cache_valid = false;
}

void WalletClusterer::SaveClusters()
{
    // Save clusters to database.
    // Format: "wc_<clusterTNI>" -> WalletClusterInfo, where clusterTNI is the
    // deterministic cluster ID (minimum member). Members are stored in a
    // std::set, which is inherently sorted and deduplicated.
    
    for (auto& pair : clusters) {
        try {
            const std::set<TrustNodeId>& members = pair.second.member_addresses;
            TrustNodeId cid = members.empty() ? pair.first : *members.begin();

            WalletClusterInfo info = pair.second;
            info.cluster_id = cid;

            // Primary write (cluster record) precedes the secondary-index
            // writes (per-member address mappings) — clause 5.2.
            std::string key = CLUSTER_PREFIX + cid.ToKeyString();
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << info;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            if (!database.WriteGeneric(key, data)) {
                LogPrintf("WalletClusterer: failed to write cluster record \"%s\"\n", key);
                continue;
            }

            // Save the per-member address mapping -> deterministic cluster ID.
            // An index-write failure is reported (clause 5.2); a repeated
            // idempotent SaveClusters repairs the mapping without duplicating
            // logical state.
            for (const TrustNodeId& member : members) {
                std::string mkey = ADDRESS_MAP_PREFIX + member.ToKeyString();
                CDataStream ms(SER_DISK, CLIENT_VERSION);
                ms << cid;
                std::vector<uint8_t> mdata(ms.begin(), ms.end());
                if (!database.WriteGeneric(mkey, mdata)) {
                    RecordMigrationEvent(MigrationEvent::FailedIndexWrite,
                                         ADDRESS_MAP_PREFIX, member.ToKeyString());
                }
            }
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to serialize cluster: %s\n", e.what());
        }
    }
    
    LogPrintf("WalletClusterer: Saved %d clusters to database\n", clusters.size());
}

void WalletClusterer::LoadClusters()
{
    // Load clusters from database using ListKeysWithPrefix
    LogPrintf("WalletClusterer: Loading clusters from database...\n");
    
    // Clear existing data
    clusters.clear();
    address_to_cluster.clear();
    
    int clusterCount = 0;
    int mappingCount = 0;
    
    // Load cluster info records (keys starting with "wc_"). The stored cluster
    // ID is the deterministic minimum member; it becomes the union-find root on
    // load so that reload is idempotent and order-independent.
    std::vector<std::string> clusterKeys = database.ListKeysWithPrefix(CLUSTER_PREFIX);
    for (const std::string& key : clusterKeys) {
        try {
            std::vector<uint8_t> data;
            if (database.ReadGeneric(key, data)) {
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                WalletClusterInfo info;
                ss >> info;
                if (!ss.empty()) {
                    RecordMigrationEvent(MigrationEvent::MalformedRecord,
                                         CLUSTER_PREFIX, key);
                    LogPrintf("WalletClusterer: trailing bytes in cluster record \"%s\"; skipping\n",
                              key);
                    continue;
                }
                TrustNodeId root = info.cluster_id;
                clusters[root] = info;
                for (const TrustNodeId& member : info.member_addresses) {
                    address_to_cluster[member] = root;
                }
                address_to_cluster[root] = root;
                clusterCount++;
            }
        } catch (const std::exception& e) {
            LogPrintf("WalletClusterer: Failed to deserialize cluster from key %s: %s\n", 
                      key.c_str(), e.what());
        }
    }
    
    // Load address-to-cluster mappings (keys starting with "wca_"). Identity key
    // segments are strictly parsed; malformed/noncanonical segments are skipped.
    std::vector<std::string> mappingKeys = database.ListKeysWithPrefix(ADDRESS_MAP_PREFIX);
    for (const std::string& key : mappingKeys) {
        try {
            std::vector<uint8_t> data;
            if (database.ReadGeneric(key, data)) {
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                TrustNodeId clusterRoot;
                ss >> clusterRoot;
                
                // Extract identity from key (format: "wca_<memberTNI>")
                std::string segment = key.substr(ADDRESS_MAP_PREFIX.size());
                TrustNodeId address;
                std::string err;
                if (!ParseKeyStringToTrustNode(segment, address, err)) {
                    RecordMigrationEvent(MigrationEvent::MalformedRecord,
                                         ADDRESS_MAP_PREFIX, key);
                    LogPrintf("WalletClusterer: skipping malformed address mapping key \"%s\": %s\n",
                              key, err);
                    continue;
                }
                
                address_to_cluster[address] = clusterRoot;
                mappingCount++;
            }
        } catch (const std::exception& e) {
            LogPrintf("WalletClusterer: Failed to deserialize address mapping from key %s: %s\n", 
                      key.c_str(), e.what());
        }
    }
    
    if (clusterCount > 0 || mappingCount > 0) {
        cache_valid = true;
        LogPrintf("WalletClusterer: Loaded %d clusters and %d address mappings from database\n",
                  clusterCount, mappingCount);
    } else {
        // No data found, will rebuild on first use
        cache_valid = false;
        LogPrintf("WalletClusterer: No cluster data found in database, will rebuild on first use\n");
    }
}

std::vector<uint256> WalletClusterer::GetAddressTransactions(const TrustNodeId& address)
{
    std::vector<uint256> txids;

    // Return the identity's transactions from the persisted transaction/address
    // index so clustering can operate on real data.
    std::string key = TX_ADDR_INDEX_PREFIX + address.ToKeyString();
    std::vector<uint8_t> data;
    if (database.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            ss >> txids;
        } catch (const std::exception& e) {
            RecordMigrationEvent(MigrationEvent::MalformedRecord,
                                 TX_ADDR_INDEX_PREFIX, address.ToKeyString());
            LogPrintf("WalletClusterer: Failed to deserialize address tx index for %s: %s\n",
                      address.ToKeyString(), e.what());
            txids.clear();
        }
    }

    return txids;
}

std::vector<TrustNodeId> WalletClusterer::GetTransactionInputAddresses(const uint256& txid)
{
    std::vector<TrustNodeId> inputs;

    std::string key = TX_INPUT_INDEX_PREFIX + txid.ToString();
    std::vector<uint8_t> data;
    if (database.ReadGeneric(key, data)) {
        try {
            CDataStream ss(data, SER_DISK, CLIENT_VERSION);
            ss >> inputs;
        } catch (const std::exception& e) {
            RecordMigrationEvent(MigrationEvent::MalformedRecord,
                                 TX_INPUT_INDEX_PREFIX, txid.ToString());
            LogPrintf("WalletClusterer: Failed to deserialize tx input index for %s: %s\n",
                      txid.ToString().c_str(), e.what());
            inputs.clear();
        }
    }

    return inputs;
}

void WalletClusterer::RecordTransactionInputs(const uint256& txid,
                                              const std::vector<TrustNodeId>& inputAddresses)
{
    // Accept only strictly canonical identities; sort and deduplicate before
    // upsert so the record is deterministic and idempotent.
    std::set<TrustNodeId> uniqueInputs;
    for (const TrustNodeId& node : inputAddresses) {
        std::string err;
        if (!ValidateCanonicalTrustNode(node, err)) {
            LogPrintf("WalletClusterer: skipping noncanonical input identity for tx %s: %s\n",
                      txid.ToString(), err);
            continue;
        }
        uniqueInputs.insert(node);
    }

    std::vector<TrustNodeId> sortedInputs(uniqueInputs.begin(), uniqueInputs.end());

    // Persist tx -> input identities (primary write; used by the common-input
    // heuristic).
    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << sortedInputs;
        std::vector<uint8_t> data(ss.begin(), ss.end());
        if (!database.WriteGeneric(TX_INPUT_INDEX_PREFIX + txid.ToString(), data)) {
            LogPrintf("WalletClusterer: Failed to persist tx input index for %s\n",
                      txid.ToString());
            return;
        }
    } catch (const std::exception& e) {
        LogPrintf("WalletClusterer: Failed to persist tx input index: %s\n", e.what());
        return;
    }

    // Update per-identity -> tx-list secondary index for each input identity.
    // The tx list is sorted and deduplicated before upsert, so repeating the
    // same call is idempotent. An index-write failure is reported; a repeated
    // idempotent upsert repairs it without duplicating logical state (5.2).
    for (const TrustNodeId& node : sortedInputs) {
        std::vector<uint256> txids = GetAddressTransactions(node);
        if (std::find(txids.begin(), txids.end(), txid) == txids.end()) {
            txids.push_back(txid);
        } else {
            // The tx is already indexed for this identity: idempotent replay.
            RecordMigrationEvent(MigrationEvent::IdempotentSkip,
                                 TX_ADDR_INDEX_PREFIX, node.ToKeyString());
        }
        std::sort(txids.begin(), txids.end());
        txids.erase(std::unique(txids.begin(), txids.end()), txids.end());
        try {
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << txids;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            if (!database.WriteGeneric(TX_ADDR_INDEX_PREFIX + node.ToKeyString(), data)) {
                RecordMigrationEvent(MigrationEvent::FailedIndexWrite,
                                     TX_ADDR_INDEX_PREFIX, node.ToKeyString());
            }
        } catch (const std::exception& e) {
            LogPrintf("WalletClusterer: Failed to persist address tx index: %s\n", e.what());
        }
    }
}

bool WalletClusterer::IsLikelyChangeAddress(const TrustNodeId& address)
{
    // Heuristics for change address detection:
    // - Used only once (typical for change)
    // - Received in a 2-output transaction
    // - Smaller of two outputs
    
    // This is a placeholder for future implementation
    return false;
}

// ---------------------------------------------------------------------------
// Thin uint160 wrappers (legacy P2PKH callers). Each zero-extends the bare
// uint160 into a TrustNodeId{P2PKH} and forwards to the wide overload. Wave 8
// removes the remaining uint160 bridging at the RPC/block-processing sites.
// ---------------------------------------------------------------------------

uint160 WalletClusterer::GetClusterForAddress(const uint160& address)
{
    return GetClusterForAddress(TrustNodeId::FromLegacyUint160(address)).ToUint160();
}

std::set<uint160> WalletClusterer::GetClusterMembers(const uint160& address)
{
    std::set<uint160> result;
    for (const TrustNodeId& member : GetClusterMembers(TrustNodeId::FromLegacyUint160(address))) {
        result.insert(member.ToUint160());
    }
    return result;
}

WalletClusterInfo WalletClusterer::GetClusterInfo(const uint160& cluster_id)
{
    return GetClusterInfo(TrustNodeId::FromLegacyUint160(cluster_id));
}

void WalletClusterer::LinkAddresses(const uint160& addr1, const uint160& addr2)
{
    LinkAddresses(TrustNodeId::FromLegacyUint160(addr1),
                  TrustNodeId::FromLegacyUint160(addr2));
}

void WalletClusterer::RecordTransactionInputs(const uint256& txid,
                                              const std::vector<uint160>& inputAddresses)
{
    std::vector<TrustNodeId> typed;
    typed.reserve(inputAddresses.size());
    for (const uint160& addr : inputAddresses) {
        typed.push_back(TrustNodeId::FromLegacyUint160(addr));
    }
    RecordTransactionInputs(txid, typed);
}

double WalletClusterer::CalculateClusterReputation(const uint160& cluster_id)
{
    return CalculateClusterReputation(TrustNodeId::FromLegacyUint160(cluster_id));
}

double WalletClusterer::GetEffectiveReputation(const uint160& address)
{
    return GetEffectiveReputation(TrustNodeId::FromLegacyUint160(address));
}

double WalletClusterer::GetEffectiveHATScore(const uint160& address)
{
    return GetEffectiveHATScore(TrustNodeId::FromLegacyUint160(address));
}

} // namespace CVM
