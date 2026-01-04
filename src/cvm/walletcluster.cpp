// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/walletcluster.h>
#include <cvm/securehat.h>
#include <cvm/reputation.h>
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

namespace CVM {

WalletClusterer::WalletClusterer(CVMDatabase& db) 
    : database(db), cache_valid(false)
{
    LoadClusters();
}

void WalletClusterer::BuildClusters()
{
    LogPrintf("WalletClusterer: Building address clusters from blockchain...\n");
    
    // Reset clusters
    address_to_cluster.clear();
    clusters.clear();
    
    // Check if blockchain is initialized
    if (!chainActive.Tip()) {
        LogPrintf("WalletClusterer: No blockchain data yet, skipping cluster building\n");
        return;
    }
    
    // Iterate through all blocks and analyze transactions
    CBlockIndex* pindex = chainActive.Genesis();
    int analyzed_count = 0;
    int block_count = 0;
    
    // Get consensus params once (safer if Params() is not yet initialized)
    const Consensus::Params* consensusParams = nullptr;
    try {
        consensusParams = &(Params().GetConsensus());
    } catch (...) {
        LogPrintf("WalletClusterer: Chain params not initialized yet, cannot build clusters\n");
        return;
    }
    
    while (pindex) {
        try {
            CBlock block;
            if (ReadBlockFromDisk(block, pindex, *consensusParams)) {
                block_count++;
                for (const CTransactionRef& tx : block.vtx) {
                    // Skip coinbase
                    if (tx->IsCoinBase()) {
                        continue;  // Don't increment pindex here!
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
    
    LogPrintf("WalletClusterer: Analyzed %d blocks, %d transactions, found %d clusters\n", 
              block_count, analyzed_count, clusters.size());
    
    cache_valid = true;
    SaveClusters();
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
    
    // Extract all input addresses
    std::set<uint160> input_addresses;
    
    for (const CTxIn& txin : tx->vin) {
        // Get previous transaction to find input address
        CTransactionRef prev_tx;
        uint256 prev_block;
        
        if (GetTransaction(txin.prevout.hash, prev_tx, *consensusParams, prev_block)) {
            if (txin.prevout.n < prev_tx->vout.size()) {
                const CTxOut& prev_out = prev_tx->vout[txin.prevout.n];
                
                CTxDestination dest;
                if (ExtractDestination(prev_out.scriptPubKey, dest)) {
                    if (boost::get<CKeyID>(&dest)) {
                        input_addresses.insert(uint160(boost::get<CKeyID>(dest)));
                    }
                }
            }
        }
    }
    
    // HEURISTIC 1: Common Input Heuristic
    // If multiple addresses are used as inputs in same transaction,
    // they likely belong to the same wallet
    if (input_addresses.size() > 1) {
        auto it = input_addresses.begin();
        uint160 first = *it;
        ++it;
        
        // Link all input addresses together
        while (it != input_addresses.end()) {
            UnionClusters(first, *it);
            ++it;
        }
    }
    
    // HEURISTIC 2: Change Address Detection
    // For 2-output transactions, the smaller output is likely change
    if (tx->vout.size() == 2 && input_addresses.size() > 0) {
        CAmount out0 = tx->vout[0].nValue;
        CAmount out1 = tx->vout[1].nValue;
        
        // Identify likely change output (smaller one)
        size_t change_idx = (out0 < out1) ? 0 : 1;
        
        CTxDestination change_dest;
        if (ExtractDestination(tx->vout[change_idx].scriptPubKey, change_dest)) {
            if (boost::get<CKeyID>(&change_dest)) {
                uint160 change_addr(boost::get<CKeyID>(change_dest));
                
                // Link change address with input addresses
                for (const uint160& input_addr : input_addresses) {
                    UnionClusters(input_addr, change_addr);
                }
            }
        }
    }
}

uint160 WalletClusterer::FindClusterRoot(const uint160& address)
{
    // Union-Find with path compression
    if (address_to_cluster.find(address) == address_to_cluster.end()) {
        // Address not in any cluster yet, it is its own root
        address_to_cluster[address] = address;
        return address;
    }
    
    uint160 root = address_to_cluster[address];
    
    // Path compression
    if (root != address) {
        root = FindClusterRoot(root);
        address_to_cluster[address] = root;
    }
    
    return root;
}

void WalletClusterer::UnionClusters(const uint160& addr1, const uint160& addr2)
{
    uint160 root1 = FindClusterRoot(addr1);
    uint160 root2 = FindClusterRoot(addr2);
    
    if (root1 == root2) {
        return; // Already in same cluster
    }
    
    // Merge smaller cluster into larger one
    if (clusters[root1].member_addresses.size() < clusters[root2].member_addresses.size()) {
        std::swap(root1, root2);
    }
    
    // Update cluster mapping
    address_to_cluster[root2] = root1;
    
    // Merge cluster info
    if (clusters.find(root1) == clusters.end()) {
        clusters[root1].cluster_id = root1;
        clusters[root1].member_addresses.insert(root1);
    }
    
    if (clusters.find(root2) != clusters.end()) {
        // Merge members
        for (const uint160& member : clusters[root2].member_addresses) {
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
    
    clusters[root1].member_addresses.insert(addr2);
}

uint160 WalletClusterer::GetClusterForAddress(const uint160& address)
{
    return FindClusterRoot(address);
}

std::set<uint160> WalletClusterer::GetClusterMembers(const uint160& address)
{
    uint160 cluster_id = GetClusterForAddress(address);
    
    if (clusters.find(cluster_id) != clusters.end()) {
        return clusters[cluster_id].member_addresses;
    }
    
    // Address is alone
    return {address};
}

WalletClusterInfo WalletClusterer::GetClusterInfo(const uint160& cluster_id)
{
    if (clusters.find(cluster_id) != clusters.end()) {
        return clusters[cluster_id];
    }
    
    // Single-address cluster
    WalletClusterInfo info;
    info.cluster_id = cluster_id;
    info.member_addresses.insert(cluster_id);
    return info;
}

void WalletClusterer::LinkAddresses(const uint160& addr1, const uint160& addr2)
{
    UnionClusters(addr1, addr2);
    SaveClusters();
}

double WalletClusterer::CalculateClusterReputation(const uint160& cluster_id)
{
    std::set<uint160> members = GetClusterMembers(cluster_id);
    
    if (members.empty()) {
        return 0.0;
    }
    
    // Strategy: Use MINIMUM reputation (most conservative)
    // This prevents attackers from using "clean" addresses to boost reputation
    double min_reputation = 100.0;
    
    ReputationSystem rep_system(database);
    
    for (const uint160& member : members) {
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

double WalletClusterer::GetEffectiveReputation(const uint160& address)
{
    uint160 cluster_id = GetClusterForAddress(address);
    return CalculateClusterReputation(cluster_id);
}

double WalletClusterer::GetEffectiveHATScore(const uint160& address)
{
    uint160 cluster_id = GetClusterForAddress(address);
    std::set<uint160> members = GetClusterMembers(cluster_id);
    
    if (members.empty()) {
        return 0.0;
    }
    
    // Use MINIMUM HAT v2 score across all addresses in cluster
    double min_score = 100.0;
    
    SecureHAT hat(database);
    
    for (const uint160& member : members) {
        double score = hat.CalculateFinalTrust(member, uint160());
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
    std::map<uint160, uint32_t> size_map;
    
    for (const auto& pair : clusters) {
        size_map[pair.first] = pair.second.member_addresses.size();
    }
    
    return size_map;
}

void WalletClusterer::InvalidateCache()
{
    cache_valid = false;
}

void WalletClusterer::SaveClusters()
{
    // Save clusters to database
    // Format: "wc-<address>" -> WalletClusterInfo
    
    for (const auto& pair : clusters) {
        try {
            std::string key = "wc_" + pair.first.ToString();
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << pair.second;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            database.WriteGeneric(key, data);
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to serialize cluster: %s\n", e.what());
        }
    }
    
    // Save address mappings
    for (const auto& pair : address_to_cluster) {
        try {
            std::string key = "wca_" + pair.first.ToString();
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << pair.second;
            std::vector<uint8_t> data(ss.begin(), ss.end());
            database.WriteGeneric(key, data);
        } catch (const std::exception& e) {
            LogPrintf("ERROR: Failed to serialize address mapping: %s\n", e.what());
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
    
    // Load cluster info records (keys starting with "wc_")
    std::vector<std::string> clusterKeys = database.ListKeysWithPrefix("wc_");
    for (const std::string& key : clusterKeys) {
        try {
            std::vector<uint8_t> data;
            if (database.ReadGeneric(key, data)) {
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                WalletClusterInfo info;
                ss >> info;
                clusters[info.cluster_id] = info;
                clusterCount++;
            }
        } catch (const std::exception& e) {
            LogPrintf("WalletClusterer: Failed to deserialize cluster from key %s: %s\n", 
                      key.c_str(), e.what());
        }
    }
    
    // Load address-to-cluster mappings (keys starting with "wca_")
    std::vector<std::string> mappingKeys = database.ListKeysWithPrefix("wca_");
    for (const std::string& key : mappingKeys) {
        try {
            std::vector<uint8_t> data;
            if (database.ReadGeneric(key, data)) {
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                uint160 clusterRoot;
                ss >> clusterRoot;
                
                // Extract address from key (format: "wca_<address>")
                std::string addrStr = key.substr(4);  // Skip "wca_"
                uint160 address;
                address.SetHex(addrStr);
                
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

std::vector<uint256> WalletClusterer::GetAddressTransactions(const uint160& address)
{
    std::vector<uint256> txids;
    
    // This would need to query a transaction index
    // For now, return empty - this is a placeholder for future implementation
    
    return txids;
}

bool WalletClusterer::IsLikelyChangeAddress(const uint160& address)
{
    // Heuristics for change address detection:
    // - Used only once (typical for change)
    // - Received in a 2-output transaction
    // - Smaller of two outputs
    
    // This is a placeholder for future implementation
    return false;
}

} // namespace CVM

