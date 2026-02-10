// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file encrypted_mempool.cpp
 * @brief Implementation of Encrypted Mempool for MEV Protection
 * 
 * Requirements: 16.1, 16.2, 16.3, 26.2
 */

#include <l2/encrypted_mempool.h>
#include <util.h>
#include <random.h>

#include <algorithm>
#include <chrono>
#include <numeric>

namespace l2 {

// Global encrypted mempool instance
static std::unique_ptr<EncryptedMempool> g_encryptedMempool;
static bool g_encryptedMempoolInitialized = false;

EncryptedMempool::EncryptedMempool(uint64_t chainId)
    : chainId_(chainId)
    , sequencerCount_(3)  // Default minimum
{
}

bool EncryptedMempool::SubmitEncryptedTx(const EncryptedTransaction& encTx)
{
    LOCK(cs_mempool_);

    // Validate structure
    if (!encTx.ValidateStructure()) {
        LogPrint(BCLog::L2, "EncryptedMempool: Invalid transaction structure\n");
        return false;
    }

    // Check chain ID
    if (encTx.l2ChainId != chainId_) {
        LogPrint(BCLog::L2, "EncryptedMempool: Wrong chain ID %lu, expected %lu\n",
                 encTx.l2ChainId, chainId_);
        return false;
    }

    // Check if expired
    uint64_t currentTime = GetCurrentTimeSeconds();
    if (encTx.IsExpired(currentTime)) {
        LogPrint(BCLog::L2, "EncryptedMempool: Transaction expired\n");
        return false;
    }

    // Check rate limit
    auto& rateLimit = rateLimits_[encTx.senderAddress];
    if (!rateLimit.CanSubmit(currentTime)) {
        LogPrint(BCLog::L2, "EncryptedMempool: Rate limit exceeded for %s\n",
                 encTx.senderAddress.ToString());
        return false;
    }

    // Check pool size
    if (encryptedPool_.size() >= MAX_POOL_SIZE) {
        LogPrint(BCLog::L2, "EncryptedMempool: Pool is full\n");
        return false;
    }

    // Check for duplicate
    uint256 txHash = encTx.GetHash();
    if (encryptedPool_.count(txHash) > 0) {
        LogPrint(BCLog::L2, "EncryptedMempool: Duplicate transaction\n");
        return false;
    }

    // Add to pool
    encryptedPool_[txHash] = encTx;
    
    // Record rate limit
    rateLimit.RecordSubmission(currentTime);

    LogPrint(BCLog::L2, "EncryptedMempool: Added transaction %s from %s\n",
             txHash.ToString(), encTx.senderAddress.ToString());

    return true;
}

std::vector<EncryptedTransaction> EncryptedMempool::GetTransactionsForBlock(
    uint64_t blockNumber,
    uint64_t gasLimit)
{
    LOCK(cs_mempool_);

    std::vector<EncryptedTransaction> result;
    uint64_t currentTime = GetCurrentTimeSeconds();
    uint64_t totalGas = 0;

    // Estimate gas per transaction (we don't know actual gas until decryption)
    // Use a conservative estimate based on max fee
    static constexpr uint64_t ESTIMATED_GAS_PER_TX = 21000;

    // Collect valid transactions
    std::vector<std::pair<CAmount, EncryptedTransaction>> candidates;
    for (const auto& [txHash, encTx] : encryptedPool_) {
        // Skip expired
        if (encTx.IsExpired(currentTime)) continue;
        
        // Skip if not valid for this block
        if (!encTx.IsValidForBlock(blockNumber)) continue;
        
        candidates.emplace_back(encTx.maxFee, encTx);
    }

    // Sort by fee (descending)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first;
              });

    // Group by fee tier and randomize within tiers
    // This prevents MEV extraction based on ordering
    std::vector<std::vector<EncryptedTransaction>> feeTiers;
    CAmount currentTierFee = -1;
    
    for (const auto& [fee, encTx] : candidates) {
        // Create new tier if fee differs significantly (>10%)
        if (currentTierFee < 0 || 
            (currentTierFee > 0 && fee < currentTierFee * 0.9)) {
            feeTiers.emplace_back();
            currentTierFee = fee;
        }
        feeTiers.back().push_back(encTx);
    }

    // Randomize within each tier using block number as seed
    CHashWriter ss(SER_GETHASH, 0);
    ss << blockNumber;
    uint256 seed = ss.GetHash();
    for (auto& tier : feeTiers) {
        RandomizeOrdering(tier, seed);
    }

    // Collect transactions up to gas limit
    for (const auto& tier : feeTiers) {
        for (const auto& encTx : tier) {
            if (totalGas + ESTIMATED_GAS_PER_TX > gasLimit) {
                break;
            }
            result.push_back(encTx);
            totalGas += ESTIMATED_GAS_PER_TX;
        }
        if (totalGas + ESTIMATED_GAS_PER_TX > gasLimit) {
            break;
        }
    }

    LogPrint(BCLog::L2, "EncryptedMempool: Selected %zu transactions for block %lu\n",
             result.size(), blockNumber);

    return result;
}

bool EncryptedMempool::ContributeDecryptionShare(
    const uint256& txHash,
    const DecryptionShare& share)
{
    LOCK(cs_mempool_);

    // Check if transaction exists
    if (encryptedPool_.count(txHash) == 0) {
        LogPrint(BCLog::L2, "EncryptedMempool: Transaction %s not found\n",
                 txHash.ToString());
        return false;
    }

    // Validate share
    if (!ValidateShare(share, txHash)) {
        LogPrint(BCLog::L2, "EncryptedMempool: Invalid share for %s\n",
                 txHash.ToString());
        return false;
    }

    // Check for duplicate share from same sequencer
    auto& shares = decryptionShares_[txHash];
    for (const auto& existingShare : shares) {
        if (existingShare.sequencerAddress == share.sequencerAddress) {
            LogPrint(BCLog::L2, "EncryptedMempool: Duplicate share from %s\n",
                     share.sequencerAddress.ToString());
            return false;
        }
    }

    // Check max shares
    if (shares.size() >= MAX_SHARES_PER_TX) {
        LogPrint(BCLog::L2, "EncryptedMempool: Max shares reached for %s\n",
                 txHash.ToString());
        return false;
    }

    // Add share
    shares.push_back(share);

    LogPrint(BCLog::L2, "EncryptedMempool: Added share %zu/%zu for %s\n",
             shares.size(), CalculateThreshold(), txHash.ToString());

    return true;
}

std::optional<L2Transaction> EncryptedMempool::DecryptTransaction(const uint256& txHash)
{
    LOCK(cs_mempool_);

    // Check if transaction exists
    auto txIt = encryptedPool_.find(txHash);
    if (txIt == encryptedPool_.end()) {
        LogPrint(BCLog::L2, "EncryptedMempool: Transaction %s not found\n",
                 txHash.ToString());
        return std::nullopt;
    }

    // Check if we have enough shares
    if (!CanDecrypt(txHash)) {
        LogPrint(BCLog::L2, "EncryptedMempool: Not enough shares for %s\n",
                 txHash.ToString());
        return std::nullopt;
    }

    const auto& encTx = txIt->second;
    const auto& shares = decryptionShares_[txHash];

    // Perform threshold decryption
    auto decryptedData = ThresholdDecrypt(
        shares,
        encTx.encryptedPayload,
        CalculateThreshold());

    if (!decryptedData) {
        LogPrint(BCLog::L2, "EncryptedMempool: Decryption failed for %s\n",
                 txHash.ToString());
        return std::nullopt;
    }

    // Verify commitment hash
    uint256 computedCommitment = EncryptedTransaction::ComputeCommitmentHash(*decryptedData);
    if (computedCommitment != encTx.commitmentHash) {
        LogPrint(BCLog::L2, "EncryptedMempool: Commitment mismatch for %s\n",
                 txHash.ToString());
        return std::nullopt;
    }

    // Deserialize transaction
    L2Transaction tx;
    if (!tx.Deserialize(*decryptedData)) {
        LogPrint(BCLog::L2, "EncryptedMempool: Failed to deserialize %s\n",
                 txHash.ToString());
        return std::nullopt;
    }

    LogPrint(BCLog::L2, "EncryptedMempool: Successfully decrypted %s\n",
             txHash.ToString());

    return tx;
}

bool EncryptedMempool::CanDecrypt(const uint256& txHash) const
{
    LOCK(cs_mempool_);

    auto it = decryptionShares_.find(txHash);
    if (it == decryptionShares_.end()) {
        return false;
    }

    return it->second.size() >= CalculateThreshold();
}

size_t EncryptedMempool::GetShareCount(const uint256& txHash) const
{
    LOCK(cs_mempool_);

    auto it = decryptionShares_.find(txHash);
    if (it == decryptionShares_.end()) {
        return 0;
    }

    return it->second.size();
}

size_t EncryptedMempool::GetDecryptionThreshold() const
{
    LOCK(cs_mempool_);
    return CalculateThreshold();
}

void EncryptedMempool::SetSequencerCount(size_t count)
{
    LOCK(cs_mempool_);
    sequencerCount_ = std::max(count, size_t(1));
}

std::optional<EncryptedTransaction> EncryptedMempool::GetEncryptedTx(const uint256& txHash) const
{
    LOCK(cs_mempool_);

    auto it = encryptedPool_.find(txHash);
    if (it == encryptedPool_.end()) {
        return std::nullopt;
    }

    return it->second;
}

bool EncryptedMempool::RemoveTransaction(const uint256& txHash)
{
    LOCK(cs_mempool_);

    auto it = encryptedPool_.find(txHash);
    if (it == encryptedPool_.end()) {
        return false;
    }

    encryptedPool_.erase(it);
    decryptionShares_.erase(txHash);

    return true;
}

size_t EncryptedMempool::PruneExpired(uint64_t currentTime)
{
    LOCK(cs_mempool_);

    size_t removed = 0;
    auto it = encryptedPool_.begin();
    while (it != encryptedPool_.end()) {
        if (it->second.IsExpired(currentTime)) {
            uint256 txHash = it->first;
            it = encryptedPool_.erase(it);
            decryptionShares_.erase(txHash);
            removed++;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        LogPrint(BCLog::L2, "EncryptedMempool: Pruned %zu expired transactions\n", removed);
    }

    return removed;
}

size_t EncryptedMempool::GetPoolSize() const
{
    LOCK(cs_mempool_);
    return encryptedPool_.size();
}

bool EncryptedMempool::CheckRateLimit(const uint160& address) const
{
    LOCK(cs_mempool_);

    auto it = rateLimits_.find(address);
    if (it == rateLimits_.end()) {
        return true;  // No limit set yet
    }

    return it->second.CanSubmit(GetCurrentTimeSeconds());
}

void EncryptedMempool::UpdateRateLimitForReputation(const uint160& address, uint32_t hatScore)
{
    LOCK(cs_mempool_);

    auto& rateLimit = rateLimits_[address];
    
    // High reputation users get higher rate limits
    if (hatScore >= HIGH_REPUTATION_THRESHOLD) {
        rateLimit.reputationMultiplier = HIGH_REPUTATION_MULTIPLIER;
    } else {
        // Linear scaling from 1.0 to HIGH_REPUTATION_MULTIPLIER
        double factor = static_cast<double>(hatScore) / HIGH_REPUTATION_THRESHOLD;
        rateLimit.reputationMultiplier = 1.0 + (HIGH_REPUTATION_MULTIPLIER - 1.0) * factor;
    }

    rateLimit.maxTxPerWindow = DEFAULT_RATE_LIMIT;
    rateLimit.windowDuration = RATE_LIMIT_WINDOW;
}

RateLimitInfo EncryptedMempool::GetRateLimitInfo(const uint160& address) const
{
    LOCK(cs_mempool_);

    auto it = rateLimits_.find(address);
    if (it == rateLimits_.end()) {
        RateLimitInfo defaultInfo;
        defaultInfo.maxTxPerWindow = DEFAULT_RATE_LIMIT;
        defaultInfo.windowDuration = RATE_LIMIT_WINDOW;
        return defaultInfo;
    }

    return it->second;
}

void EncryptedMempool::Clear()
{
    LOCK(cs_mempool_);
    encryptedPool_.clear();
    decryptionShares_.clear();
    rateLimits_.clear();
}


// Static helper functions

EncryptedTransaction EncryptedMempool::EncryptTransaction(
    const L2Transaction& tx,
    const std::vector<unsigned char>& encryptionKey)
{
    EncryptedTransaction encTx;
    
    // Serialize the transaction
    std::vector<unsigned char> plaintext = tx.Serialize();
    
    // Generate random nonce
    encTx.encryptionNonce.resize(16);
    GetRandBytes(encTx.encryptionNonce.data(), encTx.encryptionNonce.size());
    
    // Compute commitment hash before encryption
    encTx.commitmentHash = EncryptedTransaction::ComputeCommitmentHash(plaintext);
    
    // Encrypt the payload
    encTx.encryptedPayload = XorEncrypt(plaintext, encryptionKey, encTx.encryptionNonce);
    
    // Copy visible fields
    encTx.senderAddress = tx.from;
    encTx.nonce = tx.nonce;
    encTx.maxFee = tx.GetMaxFee();
    encTx.submissionTime = GetCurrentTimeSeconds();
    encTx.schemeVersion = 1;
    encTx.l2ChainId = tx.l2ChainId;
    
    return encTx;
}

void EncryptedMempool::RandomizeOrdering(
    std::vector<EncryptedTransaction>& txs,
    const uint256& seed)
{
    if (txs.size() <= 1) return;

    // Use seed to create deterministic shuffle
    std::vector<uint64_t> indices(txs.size());
    std::iota(indices.begin(), indices.end(), 0);

    // Fisher-Yates shuffle with deterministic random
    for (size_t i = txs.size() - 1; i > 0; --i) {
        // Generate deterministic random index
        CHashWriter ss(SER_GETHASH, 0);
        ss << seed;
        ss << i;
        uint256 hash = ss.GetHash();
        
        // Use first 8 bytes as random value
        uint64_t randVal = 0;
        memcpy(&randVal, hash.begin(), sizeof(randVal));
        
        size_t j = randVal % (i + 1);
        std::swap(txs[i], txs[j]);
    }
}

std::optional<std::vector<unsigned char>> EncryptedMempool::ThresholdDecrypt(
    const std::vector<DecryptionShare>& shares,
    const std::vector<unsigned char>& encryptedData,
    size_t threshold)
{
    if (shares.size() < threshold) {
        return std::nullopt;
    }

    // Combine shares to recover the key
    auto recoveredKey = CombineShares(shares, threshold);
    if (!recoveredKey) {
        return std::nullopt;
    }

    // For now, we need the nonce from somewhere
    // In a real implementation, the nonce would be stored with the encrypted data
    // Here we use a placeholder approach
    std::vector<unsigned char> nonce(16, 0);
    
    // Decrypt using the recovered key
    return XorDecrypt(encryptedData, *recoveredKey, nonce);
}

uint64_t EncryptedMempool::GetCurrentTimeSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

size_t EncryptedMempool::CalculateThreshold() const
{
    // 2/3 of sequencers required
    size_t threshold = static_cast<size_t>(sequencerCount_ * DECRYPTION_THRESHOLD);
    return std::max(threshold, size_t(1));
}

bool EncryptedMempool::ValidateShare(const DecryptionShare& share, const uint256& txHash) const
{
    // Check that share is for this transaction
    if (share.txHash != txHash) {
        return false;
    }

    // Check that share data is not empty
    if (share.share.empty()) {
        return false;
    }

    // Check that sequencer address is set
    if (share.sequencerAddress.IsNull()) {
        return false;
    }

    // In a real implementation, we would verify:
    // 1. The sequencer is eligible
    // 2. The signature is valid
    // 3. The share index is valid

    return true;
}

std::vector<unsigned char> EncryptedMempool::XorEncrypt(
    const std::vector<unsigned char>& data,
    const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& nonce)
{
    // Simple XOR encryption (placeholder for real threshold encryption)
    // In production, this would use a proper threshold encryption scheme
    // like BLS-based threshold encryption or ECIES with Shamir's Secret Sharing
    
    std::vector<unsigned char> result(data.size());
    
    // Create keystream from key and nonce
    std::vector<unsigned char> keystream;
    keystream.reserve(data.size());
    
    size_t keyIndex = 0;
    size_t nonceIndex = 0;
    
    while (keystream.size() < data.size()) {
        // Mix key and nonce bytes
        unsigned char byte = key[keyIndex % key.size()] ^ nonce[nonceIndex % nonce.size()];
        keystream.push_back(byte);
        keyIndex++;
        nonceIndex++;
    }
    
    // XOR data with keystream
    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ keystream[i];
    }
    
    return result;
}

std::vector<unsigned char> EncryptedMempool::XorDecrypt(
    const std::vector<unsigned char>& data,
    const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& nonce)
{
    // XOR decryption is the same as encryption
    return XorEncrypt(data, key, nonce);
}

std::optional<std::vector<unsigned char>> EncryptedMempool::CombineShares(
    const std::vector<DecryptionShare>& shares,
    size_t threshold)
{
    if (shares.size() < threshold) {
        return std::nullopt;
    }

    // Simplified Shamir's Secret Sharing reconstruction
    // In production, this would use proper polynomial interpolation
    // over a finite field (e.g., GF(2^256))
    
    // For this implementation, we use a simplified XOR-based combination
    // that demonstrates the concept but isn't cryptographically secure
    
    // Find the maximum share size
    size_t maxSize = 0;
    for (const auto& share : shares) {
        maxSize = std::max(maxSize, share.share.size());
    }
    
    if (maxSize == 0) {
        return std::nullopt;
    }
    
    // Combine shares using XOR (simplified)
    // Real implementation would use Lagrange interpolation
    std::vector<unsigned char> result(maxSize, 0);
    
    for (size_t i = 0; i < threshold && i < shares.size(); ++i) {
        const auto& share = shares[i];
        for (size_t j = 0; j < share.share.size() && j < maxSize; ++j) {
            result[j] ^= share.share[j];
        }
    }
    
    return result;
}

// Global instance management

EncryptedMempool& GetEncryptedMempool()
{
    if (!g_encryptedMempoolInitialized) {
        throw std::runtime_error("EncryptedMempool not initialized");
    }
    return *g_encryptedMempool;
}

void InitEncryptedMempool(uint64_t chainId)
{
    if (g_encryptedMempoolInitialized) {
        return;
    }
    g_encryptedMempool = std::make_unique<EncryptedMempool>(chainId);
    g_encryptedMempoolInitialized = true;
    LogPrint(BCLog::L2, "EncryptedMempool initialized for chain %lu\n", chainId);
}

bool IsEncryptedMempoolInitialized()
{
    return g_encryptedMempoolInitialized;
}

} // namespace l2
