// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_ENCRYPTED_MEMPOOL_H
#define CASCOIN_L2_ENCRYPTED_MEMPOOL_H

/**
 * @file encrypted_mempool.h
 * @brief Encrypted Mempool for MEV Protection in Cascoin L2
 * 
 * This file implements the encrypted mempool system that protects users
 * from front-running and sandwich attacks. Transactions are encrypted
 * using threshold encryption and can only be decrypted when 2/3+ of
 * sequencers provide their decryption shares.
 * 
 * Requirements: 16.1, 16.2, 16.3, 26.2
 */

#include <l2/l2_common.h>
#include <l2/l2_transaction.h>
#include <uint256.h>
#include <amount.h>
#include <key.h>
#include <pubkey.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <sync.h>

#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <optional>
#include <random>

namespace l2 {

/**
 * @brief Rate limiting information per address
 * 
 * Tracks transaction submission rate to prevent spam.
 * Rate limits are adjusted based on reputation score.
 * 
 * Requirement: 26.2
 */
struct RateLimitInfo {
    /** Number of transactions in current window */
    uint32_t txCount;
    
    /** Window start timestamp */
    uint64_t windowStart;
    
    /** Maximum transactions allowed per window */
    uint32_t maxTxPerWindow;
    
    /** Window duration in seconds */
    uint64_t windowDuration;
    
    /** Last transaction timestamp */
    uint64_t lastTxTime;
    
    /** Reputation-based multiplier (1.0 = base rate) */
    double reputationMultiplier;

    RateLimitInfo()
        : txCount(0)
        , windowStart(0)
        , maxTxPerWindow(100)  // Default: 100 tx per window
        , windowDuration(60)    // Default: 60 second window
        , lastTxTime(0)
        , reputationMultiplier(1.0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txCount);
        READWRITE(windowStart);
        READWRITE(maxTxPerWindow);
        READWRITE(windowDuration);
        READWRITE(lastTxTime);
        // Note: reputationMultiplier is not serialized (computed at runtime)
    }

    /**
     * @brief Check if rate limit allows another transaction
     * @param currentTime Current timestamp
     * @return true if transaction is allowed
     */
    bool CanSubmit(uint64_t currentTime) const {
        // Check if we're in a new window
        if (currentTime >= windowStart + windowDuration) {
            return true;  // New window, always allowed
        }
        // Check if under limit (adjusted by reputation)
        uint32_t effectiveLimit = static_cast<uint32_t>(maxTxPerWindow * reputationMultiplier);
        return txCount < effectiveLimit;
    }

    /**
     * @brief Record a transaction submission
     * @param currentTime Current timestamp
     */
    void RecordSubmission(uint64_t currentTime) {
        // Reset window if expired
        if (currentTime >= windowStart + windowDuration) {
            windowStart = currentTime;
            txCount = 0;
        }
        txCount++;
        lastTxTime = currentTime;
    }
};

/**
 * @brief Encrypted transaction wrapper for MEV protection
 * 
 * Wraps a transaction with threshold encryption to prevent
 * front-running and sandwich attacks. The transaction can only
 * be decrypted when 2/3+ of sequencers provide their shares.
 * 
 * Requirements: 16.1, 26.2
 */
struct EncryptedTransaction {
    /** Encrypted transaction payload */
    std::vector<unsigned char> encryptedPayload;
    
    /** Commitment hash of plaintext for ordering (H(plaintext)) */
    uint256 commitmentHash;
    
    /** Sender address (visible for rate limiting) */
    uint160 senderAddress;
    
    /** Sender nonce (visible for ordering) */
    uint64_t nonce;
    
    /** Maximum fee (visible for prioritization) */
    CAmount maxFee;
    
    /** Submission timestamp */
    uint64_t submissionTime;
    
    /** Encryption nonce/IV */
    std::vector<unsigned char> encryptionNonce;
    
    /** Encryption scheme version */
    uint8_t schemeVersion;
    
    /** L2 chain ID */
    uint64_t l2ChainId;
    
    /** Signature proving sender owns the address */
    std::vector<unsigned char> senderSignature;
    
    /** Block number when this tx should be included (0 = any) */
    uint64_t targetBlock;
    
    /** Expiry timestamp (0 = no expiry) */
    uint64_t expiryTime;

    EncryptedTransaction()
        : nonce(0)
        , maxFee(0)
        , submissionTime(0)
        , schemeVersion(1)
        , l2ChainId(DEFAULT_L2_CHAIN_ID)
        , targetBlock(0)
        , expiryTime(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(encryptedPayload);
        READWRITE(commitmentHash);
        READWRITE(senderAddress);
        READWRITE(nonce);
        READWRITE(maxFee);
        READWRITE(submissionTime);
        READWRITE(encryptionNonce);
        READWRITE(schemeVersion);
        READWRITE(l2ChainId);
        READWRITE(senderSignature);
        READWRITE(targetBlock);
        READWRITE(expiryTime);
    }

    /**
     * @brief Compute the hash of this encrypted transaction
     * @return SHA256 hash
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << encryptedPayload;
        ss << commitmentHash;
        ss << senderAddress;
        ss << nonce;
        ss << maxFee;
        ss << submissionTime;
        ss << encryptionNonce;
        ss << schemeVersion;
        ss << l2ChainId;
        return ss.GetHash();
    }

    /**
     * @brief Compute commitment hash from plaintext transaction
     * @param plaintext The plaintext transaction data
     * @return Commitment hash
     */
    static uint256 ComputeCommitmentHash(const std::vector<unsigned char>& plaintext) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << plaintext;
        return ss.GetHash();
    }

    /**
     * @brief Check if this encrypted transaction is expired
     * @param currentTime Current timestamp
     * @return true if expired
     */
    bool IsExpired(uint64_t currentTime) const {
        if (expiryTime == 0) return false;
        return currentTime > expiryTime;
    }

    /**
     * @brief Check if this transaction is valid for a given block
     * @param blockNumber The block number to check
     * @return true if valid for this block
     */
    bool IsValidForBlock(uint64_t blockNumber) const {
        if (targetBlock == 0) return true;  // Any block
        return blockNumber >= targetBlock;
    }

    /**
     * @brief Validate the basic structure
     * @return true if structure is valid
     */
    bool ValidateStructure() const {
        // Must have encrypted payload
        if (encryptedPayload.empty()) return false;
        
        // Must have commitment hash
        if (commitmentHash.IsNull()) return false;
        
        // Must have sender address
        if (senderAddress.IsNull()) return false;
        
        // Must have encryption nonce
        if (encryptionNonce.empty()) return false;
        
        // Scheme version must be supported
        if (schemeVersion == 0 || schemeVersion > 1) return false;
        
        // Max fee must be positive
        if (maxFee <= 0) return false;
        
        return true;
    }

    /**
     * @brief Get the hash for signing (proves sender ownership)
     * @return Hash to be signed
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << commitmentHash;
        ss << senderAddress;
        ss << nonce;
        ss << maxFee;
        ss << l2ChainId;
        return ss.GetHash();
    }

    /**
     * @brief Sign the encrypted transaction
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetSigningHash();
        return key.Sign(hash, senderSignature);
    }

    /**
     * @brief Verify the sender signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (senderSignature.empty()) return false;
        uint256 hash = GetSigningHash();
        return pubkey.Verify(hash, senderSignature);
    }

    bool operator==(const EncryptedTransaction& other) const {
        return encryptedPayload == other.encryptedPayload &&
               commitmentHash == other.commitmentHash &&
               senderAddress == other.senderAddress &&
               nonce == other.nonce &&
               maxFee == other.maxFee &&
               submissionTime == other.submissionTime &&
               encryptionNonce == other.encryptionNonce &&
               schemeVersion == other.schemeVersion &&
               l2ChainId == other.l2ChainId;
    }
};

/**
 * @brief Decryption share from a sequencer
 * 
 * Each sequencer provides a share of the decryption key.
 * When 2/3+ shares are collected, the transaction can be decrypted.
 * 
 * Requirement: 16.2
 */
struct DecryptionShare {
    /** Address of the sequencer providing the share */
    uint160 sequencerAddress;
    
    /** The decryption share data */
    std::vector<unsigned char> share;
    
    /** Share index (for Shamir's Secret Sharing) */
    uint32_t shareIndex;
    
    /** Signature proving the sequencer created this share */
    std::vector<unsigned char> signature;
    
    /** Timestamp when share was created */
    uint64_t timestamp;
    
    /** Hash of the encrypted transaction this share is for */
    uint256 txHash;

    DecryptionShare()
        : shareIndex(0)
        , timestamp(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(share);
        READWRITE(shareIndex);
        READWRITE(signature);
        READWRITE(timestamp);
        READWRITE(txHash);
    }

    /**
     * @brief Get the hash for signing
     * @return Hash to be signed
     */
    uint256 GetSigningHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << sequencerAddress;
        ss << share;
        ss << shareIndex;
        ss << timestamp;
        ss << txHash;
        return ss.GetHash();
    }

    /**
     * @brief Sign the decryption share
     * @param key The signing key
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key) {
        uint256 hash = GetSigningHash();
        return key.Sign(hash, signature);
    }

    /**
     * @brief Verify the signature
     * @param pubkey The public key to verify against
     * @return true if signature is valid
     */
    bool VerifySignature(const CPubKey& pubkey) const {
        if (signature.empty()) return false;
        uint256 hash = GetSigningHash();
        return pubkey.Verify(hash, signature);
    }

    bool operator==(const DecryptionShare& other) const {
        return sequencerAddress == other.sequencerAddress &&
               share == other.share &&
               shareIndex == other.shareIndex &&
               txHash == other.txHash;
    }
};


/**
 * @brief Encrypted Mempool for MEV Protection
 * 
 * Manages encrypted transactions to prevent front-running and
 * sandwich attacks. Transactions are encrypted using threshold
 * encryption and can only be decrypted when 2/3+ of sequencers
 * provide their decryption shares.
 * 
 * Requirements: 16.1, 16.2, 16.3, 26.2
 */
class EncryptedMempool {
public:
    /**
     * @brief Construct a new Encrypted Mempool
     * @param chainId The L2 chain ID
     */
    explicit EncryptedMempool(uint64_t chainId);

    /**
     * @brief Submit an encrypted transaction to the mempool
     * 
     * Validates the transaction structure and rate limits before
     * adding to the pool.
     * 
     * @param encTx The encrypted transaction
     * @return true if transaction was accepted
     * 
     * Requirement: 16.1
     */
    bool SubmitEncryptedTx(const EncryptedTransaction& encTx);

    /**
     * @brief Get transactions for inclusion in a block
     * 
     * Returns encrypted transactions ordered by fee and randomized
     * within fee tiers to prevent MEV extraction.
     * 
     * @param blockNumber The block number being built
     * @param gasLimit Maximum gas for the block
     * @return Vector of encrypted transactions
     * 
     * Requirement: 16.3
     */
    std::vector<EncryptedTransaction> GetTransactionsForBlock(
        uint64_t blockNumber,
        uint64_t gasLimit);

    /**
     * @brief Contribute a decryption share for a transaction
     * 
     * Sequencers call this to provide their share of the decryption key.
     * 
     * @param txHash Hash of the encrypted transaction
     * @param share The decryption share
     * @return true if share was accepted
     * 
     * Requirement: 16.2
     */
    bool ContributeDecryptionShare(
        const uint256& txHash,
        const DecryptionShare& share);

    /**
     * @brief Decrypt a transaction when threshold is reached
     * 
     * Combines decryption shares using Shamir's Secret Sharing
     * to recover the plaintext transaction.
     * 
     * @param txHash Hash of the encrypted transaction
     * @return Decrypted transaction if successful
     * 
     * Requirement: 16.2
     */
    std::optional<L2Transaction> DecryptTransaction(const uint256& txHash);

    /**
     * @brief Check if a transaction can be decrypted
     * 
     * Returns true if enough decryption shares have been collected.
     * 
     * @param txHash Hash of the encrypted transaction
     * @return true if decryption is possible
     */
    bool CanDecrypt(const uint256& txHash) const;

    /**
     * @brief Get the number of decryption shares for a transaction
     * @param txHash Hash of the encrypted transaction
     * @return Number of shares collected
     */
    size_t GetShareCount(const uint256& txHash) const;

    /**
     * @brief Get the required number of shares for decryption
     * @return Threshold number of shares needed
     */
    size_t GetDecryptionThreshold() const;

    /**
     * @brief Set the total number of sequencers (for threshold calculation)
     * @param count Total sequencer count
     */
    void SetSequencerCount(size_t count);

    /**
     * @brief Get an encrypted transaction by hash
     * @param txHash The transaction hash
     * @return Optional containing the transaction if found
     */
    std::optional<EncryptedTransaction> GetEncryptedTx(const uint256& txHash) const;

    /**
     * @brief Remove a transaction from the pool
     * @param txHash The transaction hash
     * @return true if transaction was removed
     */
    bool RemoveTransaction(const uint256& txHash);

    /**
     * @brief Remove expired transactions
     * @param currentTime Current timestamp
     * @return Number of transactions removed
     */
    size_t PruneExpired(uint64_t currentTime);

    /**
     * @brief Get the number of transactions in the pool
     * @return Transaction count
     */
    size_t GetPoolSize() const;

    /**
     * @brief Check if rate limit allows a transaction from an address
     * @param address The sender address
     * @return true if transaction is allowed
     */
    bool CheckRateLimit(const uint160& address) const;

    /**
     * @brief Update rate limit based on reputation
     * @param address The address to update
     * @param hatScore The HAT v2 score
     */
    void UpdateRateLimitForReputation(const uint160& address, uint32_t hatScore);

    /**
     * @brief Get rate limit info for an address
     * @param address The address
     * @return Rate limit info
     */
    RateLimitInfo GetRateLimitInfo(const uint160& address) const;

    /**
     * @brief Clear all transactions (for testing)
     */
    void Clear();

    /**
     * @brief Get the L2 chain ID
     * @return Chain ID
     */
    uint64_t GetChainId() const { return chainId_; }

    /**
     * @brief Encrypt a transaction for submission
     * 
     * Helper function to encrypt a plaintext transaction using
     * the current encryption scheme.
     * 
     * @param tx The plaintext transaction
     * @param encryptionKey The encryption key (from threshold setup)
     * @return Encrypted transaction
     */
    static EncryptedTransaction EncryptTransaction(
        const L2Transaction& tx,
        const std::vector<unsigned char>& encryptionKey);

    /**
     * @brief Randomize transaction ordering within a set
     * 
     * Shuffles transactions to prevent MEV extraction based on
     * ordering. Uses a deterministic seed for reproducibility.
     * 
     * @param txs Transactions to randomize
     * @param seed Random seed for deterministic shuffling
     * 
     * Requirement: 16.3
     */
    static void RandomizeOrdering(
        std::vector<EncryptedTransaction>& txs,
        const uint256& seed);

    /**
     * @brief Perform threshold decryption using Shamir's Secret Sharing
     * 
     * Combines decryption shares to recover the encryption key,
     * then decrypts the payload.
     * 
     * @param shares The decryption shares
     * @param encryptedData The encrypted data
     * @param threshold Minimum shares required
     * @return Decrypted data if successful
     * 
     * Requirement: 16.2
     */
    static std::optional<std::vector<unsigned char>> ThresholdDecrypt(
        const std::vector<DecryptionShare>& shares,
        const std::vector<unsigned char>& encryptedData,
        size_t threshold);

private:
    /** L2 chain ID */
    uint64_t chainId_;

    /** Encrypted transaction pool (txHash -> transaction) */
    std::map<uint256, EncryptedTransaction> encryptedPool_;

    /** Decryption shares per transaction (txHash -> shares) */
    std::map<uint256, std::vector<DecryptionShare>> decryptionShares_;

    /** Rate limiting per address */
    mutable std::map<uint160, RateLimitInfo> rateLimits_;

    /** Total number of sequencers (for threshold calculation) */
    size_t sequencerCount_;

    /** Mutex for thread safety */
    mutable CCriticalSection cs_mempool_;

    /** Decryption threshold (2/3 of sequencers) */
    static constexpr double DECRYPTION_THRESHOLD = 0.67;

    /** Maximum pool size */
    static constexpr size_t MAX_POOL_SIZE = 10000;

    /** Maximum shares per transaction */
    static constexpr size_t MAX_SHARES_PER_TX = 100;

    /** Default rate limit per window */
    static constexpr uint32_t DEFAULT_RATE_LIMIT = 100;

    /** Rate limit window duration (seconds) */
    static constexpr uint64_t RATE_LIMIT_WINDOW = 60;

    /** High reputation threshold for increased rate limit */
    static constexpr uint32_t HIGH_REPUTATION_THRESHOLD = 70;

    /** Rate limit multiplier for high reputation */
    static constexpr double HIGH_REPUTATION_MULTIPLIER = 5.0;

    /**
     * @brief Get current timestamp
     * @return Unix timestamp in seconds
     */
    static uint64_t GetCurrentTimeSeconds();

    /**
     * @brief Calculate the decryption threshold
     * @return Number of shares required
     */
    size_t CalculateThreshold() const;

    /**
     * @brief Validate a decryption share
     * @param share The share to validate
     * @param txHash The transaction hash
     * @return true if share is valid
     */
    bool ValidateShare(const DecryptionShare& share, const uint256& txHash) const;

    /**
     * @brief Simple XOR-based encryption (placeholder for real threshold encryption)
     * @param data Data to encrypt
     * @param key Encryption key
     * @param nonce Encryption nonce
     * @return Encrypted data
     */
    static std::vector<unsigned char> XorEncrypt(
        const std::vector<unsigned char>& data,
        const std::vector<unsigned char>& key,
        const std::vector<unsigned char>& nonce);

    /**
     * @brief Simple XOR-based decryption (placeholder for real threshold decryption)
     * @param data Data to decrypt
     * @param key Decryption key
     * @param nonce Encryption nonce
     * @return Decrypted data
     */
    static std::vector<unsigned char> XorDecrypt(
        const std::vector<unsigned char>& data,
        const std::vector<unsigned char>& key,
        const std::vector<unsigned char>& nonce);

    /**
     * @brief Combine shares using Lagrange interpolation (Shamir's Secret Sharing)
     * @param shares The shares to combine
     * @param threshold Minimum shares required
     * @return Recovered secret if successful
     */
    static std::optional<std::vector<unsigned char>> CombineShares(
        const std::vector<DecryptionShare>& shares,
        size_t threshold);
};

/**
 * @brief Global encrypted mempool instance
 */
EncryptedMempool& GetEncryptedMempool();

/**
 * @brief Initialize the global encrypted mempool
 * @param chainId The L2 chain ID
 */
void InitEncryptedMempool(uint64_t chainId);

/**
 * @brief Check if encrypted mempool is initialized
 * @return true if initialized
 */
bool IsEncryptedMempoolInitialized();

} // namespace l2

#endif // CASCOIN_L2_ENCRYPTED_MEMPOOL_H
