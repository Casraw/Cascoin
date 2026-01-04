// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cross_chain_bridge.h>
#include <cvm/securehat.h>
#include <hash.h>
#include <pubkey.h>
#include <util.h>
#include <timedata.h>
#include <validation.h>
#include <chain.h>

namespace CVM {

// Global cross-chain bridge instance
std::unique_ptr<CrossChainTrustBridge> g_crossChainBridge;

// ========== TrustStateProof Implementation ==========

bool TrustStateProof::VerifyMerkleProof() const {
    if (merkleProof.empty()) {
        return false;
    }
    
    // Calculate leaf hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << trustScore;
    ss << blockHeight;
    uint256 currentHash = ss.GetHash();
    
    // Walk up the merkle tree
    for (const auto& proofElement : merkleProof) {
        CHashWriter hashWriter(SER_GETHASH, 0);
        if (currentHash < proofElement) {
            hashWriter << currentHash << proofElement;
        } else {
            hashWriter << proofElement << currentHash;
        }
        currentHash = hashWriter.GetHash();
    }
    
    // Compare with state root
    return currentHash == stateRoot;
}

uint256 TrustStateProof::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << trustScore;
    ss << blockHeight;
    ss << blockHash;
    ss << stateRoot;
    return ss.GetHash();
}

// ========== ReputationProof Implementation ==========

bool ReputationProof::Verify() const {
    if (proof.empty() || signature.empty()) {
        return false;
    }
    
    // Verify timestamp is not too old (max 24 hours)
    uint64_t currentTime = GetTime();
    if (currentTime - timestamp > 86400) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Reputation proof expired\n");
        return false;
    }
    
    // Verify reputation is in valid range
    if (reputation > 100) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid reputation value %d\n", reputation);
        return false;
    }
    
    // Basic proof validation (in production, would verify against source chain)
    return !proof.empty();
}

uint256 ReputationProof::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << reputation;
    ss << timestamp;
    ss << sourceChainSelector;
    return ss.GetHash();
}

// ========== CrossChainTrustBridge Implementation ==========

CrossChainTrustBridge::CrossChainTrustBridge() : database(nullptr) {
    InitializeDefaultChains();
}

CrossChainTrustBridge::CrossChainTrustBridge(CVMDatabase* db) : database(db) {
    InitializeDefaultChains();
}

CrossChainTrustBridge::~CrossChainTrustBridge() {
}

void CrossChainTrustBridge::InitializeDefaultChains() {
    // Cascoin Mainnet (self)
    ChainConfig cascoin;
    cascoin.chainId = 0;
    cascoin.chainName = "Cascoin Mainnet";
    cascoin.isActive = true;
    cascoin.minConfirmations = 6;
    cascoin.maxAttestationAge = 86400; // 24 hours
    supportedChains[0] = cascoin;
    chainWeights[0] = 1.0; // Full weight for local chain
    
    // Ethereum Mainnet
    ChainConfig ethereum;
    ethereum.chainId = 1;
    ethereum.chainName = "Ethereum Mainnet";
    ethereum.chainSelector.SetHex("5009297550715157269"); // CCIP selector
    ethereum.isActive = true;
    ethereum.minConfirmations = 12;
    ethereum.maxAttestationAge = 86400;
    supportedChains[1] = ethereum;
    chainWeights[1] = 0.9; // High weight for Ethereum
    
    // Polygon
    ChainConfig polygon;
    polygon.chainId = 2;
    polygon.chainName = "Polygon";
    polygon.chainSelector.SetHex("4051577828743386545");
    polygon.isActive = true;
    polygon.minConfirmations = 128;
    polygon.maxAttestationAge = 86400;
    supportedChains[2] = polygon;
    chainWeights[2] = 0.7;
    
    // Arbitrum
    ChainConfig arbitrum;
    arbitrum.chainId = 3;
    arbitrum.chainName = "Arbitrum One";
    arbitrum.chainSelector.SetHex("4949039107694359620");
    arbitrum.isActive = true;
    arbitrum.minConfirmations = 1;
    arbitrum.maxAttestationAge = 86400;
    supportedChains[3] = arbitrum;
    chainWeights[3] = 0.8;
    
    // Optimism
    ChainConfig optimism;
    optimism.chainId = 4;
    optimism.chainName = "Optimism";
    optimism.chainSelector.SetHex("3734403246176062136");
    optimism.isActive = true;
    optimism.minConfirmations = 1;
    optimism.maxAttestationAge = 86400;
    supportedChains[4] = optimism;
    chainWeights[4] = 0.8;
    
    // Base
    ChainConfig base;
    base.chainId = 5;
    base.chainName = "Base";
    base.chainSelector.SetHex("15971525489660198786");
    base.isActive = true;
    base.minConfirmations = 1;
    base.maxAttestationAge = 86400;
    supportedChains[5] = base;
    chainWeights[5] = 0.7;
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Initialized with %d supported chains\n", 
             supportedChains.size());
}

// ========== LayerZero Integration ==========

bool CrossChainTrustBridge::SendTrustAttestation(uint16_t dstChainId, const uint160& address, 
                                                 const TrustAttestation& attestation) {
    if (!IsChainSupported(dstChainId)) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Destination chain %d not supported\n", dstChainId);
        return false;
    }
    
    const ChainConfig* config = GetChainConfig(dstChainId);
    if (!config || !config->isActive) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Destination chain %d not active\n", dstChainId);
        return false;
    }
    
    // Validate attestation
    if (!ValidateAttestation(attestation)) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid attestation for %s\n", 
                 address.ToString());
        return false;
    }
    
    // In production, this would:
    // 1. Encode the attestation for LayerZero
    // 2. Call the LayerZero endpoint to send the message
    // 3. Pay the required fees
    // For now, we log the intent and store locally
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Sending attestation for %s to chain %d (score=%d)\n",
             address.ToString(), dstChainId, attestation.trustScore);
    
    // Store the attestation locally
    StoreAttestation(attestation);
    
    return true;
}

void CrossChainTrustBridge::ReceiveTrustAttestation(uint16_t srcChainId, 
                                                    const TrustAttestation& attestation) {
    if (!IsChainSupported(srcChainId)) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Source chain %d not supported, ignoring\n", srcChainId);
        return;
    }
    
    // Validate attestation
    if (!ValidateAttestation(attestation)) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid attestation from chain %d\n", srcChainId);
        return;
    }
    
    // Verify signature
    if (!attestation.VerifySignature()) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid signature on attestation from chain %d\n", 
                 srcChainId);
        return;
    }
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Received attestation for %s from chain %d (score=%d)\n",
             attestation.address.ToString(), srcChainId, attestation.trustScore);
    
    // Store the attestation
    StoreAttestation(attestation);
    
    // Update trust cache
    ChainTrustScore score;
    score.chainId = srcChainId;
    score.trustScore = static_cast<uint8_t>(std::max(0, std::min(100, (int)attestation.trustScore)));
    score.timestamp = attestation.timestamp;
    score.isVerified = true;
    score.proofHash = attestation.GetHash();
    
    UpdateTrustCache(attestation.address, score);
}

// ========== Chainlink CCIP Integration ==========

bool CrossChainTrustBridge::VerifyReputationViaCCIP(uint64_t sourceChainSelector, 
                                                    const uint160& address,
                                                    const ReputationProof& proof) {
    // Verify the proof
    if (!proof.Verify()) {
        LogPrint(BCLog::CVM, "CrossChainBridge: CCIP proof verification failed for %s\n",
                 address.ToString());
        return false;
    }
    
    // Verify address matches
    if (proof.address != address) {
        LogPrint(BCLog::CVM, "CrossChainBridge: CCIP proof address mismatch\n");
        return false;
    }
    
    // Verify source chain selector matches
    if (proof.sourceChainSelector != sourceChainSelector) {
        LogPrint(BCLog::CVM, "CrossChainBridge: CCIP source chain selector mismatch\n");
        return false;
    }
    
    LogPrint(BCLog::CVM, "CrossChainBridge: CCIP verification successful for %s (reputation=%d)\n",
             address.ToString(), proof.reputation);
    
    // Find chain ID from selector
    uint16_t chainId = 0;
    for (const auto& pair : supportedChains) {
        // Compare chain selectors
        uint256 selector;
        selector.SetHex(std::to_string(sourceChainSelector));
        if (pair.second.chainSelector == selector) {
            chainId = pair.first;
            break;
        }
    }
    
    // Update trust cache
    ChainTrustScore score;
    score.chainId = chainId;
    score.trustScore = proof.reputation;
    score.timestamp = proof.timestamp;
    score.isVerified = true;
    score.proofHash = proof.GetHash();
    
    UpdateTrustCache(address, score);
    
    return true;
}

void CrossChainTrustBridge::SendReputationProofViaCCIP(uint64_t destChainSelector, 
                                                       const ReputationProof& proof) {
    // In production, this would:
    // 1. Encode the proof for CCIP
    // 2. Call the CCIP router to send the message
    // 3. Pay the required fees
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Sending CCIP reputation proof for %s to selector %lu\n",
             proof.address.ToString(), destChainSelector);
}

// ========== Trust State Proofs ==========

TrustStateProof CrossChainTrustBridge::GenerateTrustStateProof(const uint160& address) {
    TrustStateProof proof;
    proof.address = address;
    
    // Get current trust score
    if (database) {
        SecureHAT secureHat(*database);
        uint160 defaultViewer;
        proof.trustScore = static_cast<uint8_t>(
            std::max(0, std::min(100, (int)secureHat.CalculateFinalTrust(address, defaultViewer))));
    } else {
        proof.trustScore = 50; // Default
    }
    
    // Get current block info
    {
        LOCK(cs_main);
        if (chainActive.Tip()) {
            proof.blockHeight = chainActive.Height();
            proof.blockHash = chainActive.Tip()->GetBlockHash();
        }
    }
    
    // Generate merkle proof (simplified - in production would use actual state trie)
    CHashWriter ss(SER_GETHASH, 0);
    ss << address;
    ss << proof.trustScore;
    ss << proof.blockHeight;
    proof.stateRoot = ss.GetHash();
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Generated trust state proof for %s (score=%d, height=%d)\n",
             address.ToString(), proof.trustScore, proof.blockHeight);
    
    return proof;
}

bool CrossChainTrustBridge::VerifyTrustStateProof(const TrustStateProof& proof, uint16_t sourceChain) {
    if (!IsChainSupported(sourceChain)) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Source chain %d not supported\n", sourceChain);
        return false;
    }
    
    // Verify merkle proof
    if (!proof.VerifyMerkleProof()) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Merkle proof verification failed\n");
        return false;
    }
    
    // Verify trust score is in valid range
    if (proof.trustScore > 100) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid trust score %d\n", proof.trustScore);
        return false;
    }
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Trust state proof verified for %s from chain %d\n",
             proof.address.ToString(), sourceChain);
    
    // Update trust cache
    ChainTrustScore score;
    score.chainId = sourceChain;
    score.trustScore = proof.trustScore;
    score.timestamp = GetTime();
    score.isVerified = true;
    score.proofHash = proof.GetHash();
    
    UpdateTrustCache(proof.address, score);
    
    return true;
}

// ========== Trust Score Aggregation ==========

uint8_t CrossChainTrustBridge::AggregateCrossChainTrust(const uint160& address, 
                                                        const std::vector<ChainTrustScore>& scores) {
    if (scores.empty()) {
        return 0;
    }
    
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    uint64_t currentTime = GetTime();
    
    for (const auto& score : scores) {
        // Skip unverified scores
        if (!score.isVerified) {
            continue;
        }
        
        // Skip expired scores (older than 24 hours)
        if (currentTime - score.timestamp > 86400) {
            continue;
        }
        
        // Get chain weight
        double weight = GetChainWeight(score.chainId);
        
        // Apply time decay (scores lose weight as they age)
        double ageHours = static_cast<double>(currentTime - score.timestamp) / 3600.0;
        double timeDecay = std::max(0.5, 1.0 - (ageHours / 48.0)); // 50% weight at 24 hours
        
        weight *= timeDecay;
        
        weightedSum += score.trustScore * weight;
        totalWeight += weight;
    }
    
    if (totalWeight == 0.0) {
        return 0;
    }
    
    uint8_t aggregated = static_cast<uint8_t>(std::round(weightedSum / totalWeight));
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Aggregated trust for %s: %d (from %d scores)\n",
             address.ToString(), aggregated, scores.size());
    
    return aggregated;
}

std::vector<ChainTrustScore> CrossChainTrustBridge::GetCrossChainTrustScores(const uint160& address) const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto it = crossChainTrustCache.find(address);
    if (it != crossChainTrustCache.end()) {
        return it->second;
    }
    
    return {};
}

uint8_t CrossChainTrustBridge::GetAggregatedTrustScore(const uint160& address) const {
    std::vector<ChainTrustScore> scores = GetCrossChainTrustScores(address);
    
    // Need to cast away const for AggregateCrossChainTrust call
    // This is safe because AggregateCrossChainTrust doesn't modify state
    return const_cast<CrossChainTrustBridge*>(this)->AggregateCrossChainTrust(address, scores);
}

// ========== Chain Reorganization Handling ==========

void CrossChainTrustBridge::HandleChainReorg(uint16_t chainId, 
                                             const std::vector<uint256>& invalidatedBlocks) {
    LogPrint(BCLog::CVM, "CrossChainBridge: Handling reorg on chain %d (%d blocks invalidated)\n",
             chainId, invalidatedBlocks.size());
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    // Remove attestations that reference invalidated blocks
    for (auto& pair : crossChainTrustCache) {
        auto& scores = pair.second;
        scores.erase(
            std::remove_if(scores.begin(), scores.end(),
                [chainId, &invalidatedBlocks](const ChainTrustScore& score) {
                    if (score.chainId != chainId) {
                        return false;
                    }
                    // Check if proof hash matches any invalidated block
                    for (const auto& blockHash : invalidatedBlocks) {
                        if (score.proofHash == blockHash) {
                            return true;
                        }
                    }
                    return false;
                }),
            scores.end()
        );
    }
}

// ========== Chain Configuration ==========

void CrossChainTrustBridge::AddSupportedChain(const ChainConfig& config) {
    supportedChains[config.chainId] = config;
    
    // Set default weight if not already set
    if (chainWeights.find(config.chainId) == chainWeights.end()) {
        chainWeights[config.chainId] = 0.5; // Default weight
    }
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Added supported chain %d (%s)\n",
             config.chainId, config.chainName);
}

const ChainConfig* CrossChainTrustBridge::GetChainConfig(uint16_t chainId) const {
    auto it = supportedChains.find(chainId);
    if (it != supportedChains.end()) {
        return &it->second;
    }
    return nullptr;
}

bool CrossChainTrustBridge::IsChainSupported(uint16_t chainId) const {
    return supportedChains.find(chainId) != supportedChains.end();
}

std::vector<uint16_t> CrossChainTrustBridge::GetSupportedChains() const {
    std::vector<uint16_t> chains;
    for (const auto& pair : supportedChains) {
        chains.push_back(pair.first);
    }
    return chains;
}

// ========== Attestation Storage ==========

bool CrossChainTrustBridge::StoreAttestation(const TrustAttestation& attestation) {
    if (!database) {
        LogPrint(BCLog::CVM, "CrossChainBridge: No database available for attestation storage\n");
        return false;
    }
    
    // Validate attestation
    if (!ValidateAttestation(attestation)) {
        return false;
    }
    
    // Store in database
    std::string key = "trust_attest_" + attestation.address.ToString() + "_" + 
                      std::to_string(static_cast<int>(attestation.source)) + "_" +
                      std::to_string(attestation.timestamp);
    
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << attestation;
    
    std::vector<uint8_t> data(ss.begin(), ss.end());
    database->WriteGeneric(key, data);
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Stored attestation for %s (source=%d, score=%d)\n",
             attestation.address.ToString(), static_cast<int>(attestation.source), 
             attestation.trustScore);
    
    return true;
}

std::vector<TrustAttestation> CrossChainTrustBridge::GetAttestations(const uint160& address) const {
    std::vector<TrustAttestation> attestations;
    
    if (!database) {
        return attestations;
    }
    
    // Query attestations for this address from all sources
    for (int source = 0; source <= static_cast<int>(AttestationSource::OTHER); ++source) {
        std::string keyPrefix = "trust_attest_" + address.ToString() + "_" + std::to_string(source);
        
        // In a full implementation, we would iterate over all keys with this prefix
        // For now, we return what's in the cache
    }
    
    return attestations;
}

TrustAttestation CrossChainTrustBridge::GetLatestAttestation(const uint160& address, 
                                                             uint16_t chainId) const {
    TrustAttestation latest;
    
    auto attestations = GetAttestations(address);
    for (const auto& attestation : attestations) {
        if (static_cast<uint16_t>(attestation.source) == chainId) {
            if (attestation.timestamp > latest.timestamp) {
                latest = attestation;
            }
        }
    }
    
    return latest;
}

size_t CrossChainTrustBridge::PruneOldAttestations(uint64_t maxAge) {
    size_t pruned = 0;
    uint64_t currentTime = GetTime();
    
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    for (auto& pair : crossChainTrustCache) {
        auto& scores = pair.second;
        size_t before = scores.size();
        
        scores.erase(
            std::remove_if(scores.begin(), scores.end(),
                [currentTime, maxAge](const ChainTrustScore& score) {
                    return currentTime - score.timestamp > maxAge;
                }),
            scores.end()
        );
        
        pruned += before - scores.size();
    }
    
    LogPrint(BCLog::CVM, "CrossChainBridge: Pruned %d old attestations\n", pruned);
    
    return pruned;
}

// ========== Statistics ==========

size_t CrossChainTrustBridge::GetAttestationCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    size_t count = 0;
    for (const auto& pair : crossChainTrustCache) {
        count += pair.second.size();
    }
    return count;
}

std::map<uint16_t, size_t> CrossChainTrustBridge::GetAttestationCountByChain() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    std::map<uint16_t, size_t> counts;
    for (const auto& pair : crossChainTrustCache) {
        for (const auto& score : pair.second) {
            counts[score.chainId]++;
        }
    }
    return counts;
}

// ========== Private Methods ==========

void CrossChainTrustBridge::UpdateTrustCache(const uint160& address, const ChainTrustScore& score) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    
    auto& scores = crossChainTrustCache[address];
    
    // Update existing score for this chain or add new one
    bool found = false;
    for (auto& existing : scores) {
        if (existing.chainId == score.chainId) {
            if (score.timestamp > existing.timestamp) {
                existing = score;
            }
            found = true;
            break;
        }
    }
    
    if (!found) {
        scores.push_back(score);
    }
    
    // Limit cache size per address
    if (scores.size() > 20) {
        // Remove oldest scores
        std::sort(scores.begin(), scores.end(),
            [](const ChainTrustScore& a, const ChainTrustScore& b) {
                return a.timestamp > b.timestamp;
            });
        scores.resize(20);
    }
}

bool CrossChainTrustBridge::ValidateAttestation(const TrustAttestation& attestation) const {
    // Check trust score range
    if (attestation.trustScore < 0 || attestation.trustScore > 100) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Invalid trust score %d\n", attestation.trustScore);
        return false;
    }
    
    // Check timestamp
    uint64_t currentTime = GetTime();
    if (attestation.timestamp > currentTime + 300) { // Max 5 minutes in future
        LogPrint(BCLog::CVM, "CrossChainBridge: Attestation timestamp in future\n");
        return false;
    }
    
    // Check if attestation is too old
    const ChainConfig* config = GetChainConfig(static_cast<uint16_t>(attestation.source));
    uint64_t maxAge = config ? config->maxAttestationAge : 86400;
    if (currentTime - attestation.timestamp > maxAge) {
        LogPrint(BCLog::CVM, "CrossChainBridge: Attestation too old\n");
        return false;
    }
    
    return true;
}

double CrossChainTrustBridge::GetChainWeight(uint16_t chainId) const {
    auto it = chainWeights.find(chainId);
    if (it != chainWeights.end()) {
        return it->second;
    }
    return 0.5; // Default weight
}

// ========== Global Functions ==========

void InitializeCrossChainBridge(CVMDatabase* db) {
    g_crossChainBridge = std::make_unique<CrossChainTrustBridge>(db);
    LogPrint(BCLog::CVM, "CrossChainBridge: Initialized global cross-chain bridge\n");
}

void ShutdownCrossChainBridge() {
    g_crossChainBridge.reset();
    LogPrint(BCLog::CVM, "CrossChainBridge: Shutdown global cross-chain bridge\n");
}

} // namespace CVM
