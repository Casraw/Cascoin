// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_DATA_AVAILABILITY_H
#define CASCOIN_L2_DATA_AVAILABILITY_H

/**
 * @file data_availability.h
 * @brief Data Availability Layer for Cascoin L2
 * 
 * This file implements the Data Availability (DA) layer for the Cascoin L2
 * system. It provides:
 * - Batch data structures for L1 submission
 * - DA commitments for data availability sampling
 * - Compression/decompression of transaction data
 * - Erasure coding for light client verification
 * 
 * Requirements: 3.2, 3.4, 7.1, 7.2, 7.3, 7.5, 11.6, 24.4, 41.2
 */

#include <l2/l2_common.h>
#include <uint256.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>
#include <primitives/transaction.h>
#include <sync.h>

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <map>

namespace l2 {

/** Default batch interval (L2 blocks between L1 submissions) */
static constexpr uint32_t DEFAULT_BATCH_INTERVAL = 100;

/** Maximum batch size in bytes (128 KB) */
static constexpr uint64_t MAX_BATCH_SIZE = 128 * 1024;

/** Minimum batch size in bytes (1 KB) */
static constexpr uint64_t MIN_BATCH_SIZE = 1024;

/** Default number of data shards for erasure coding */
static constexpr uint32_t DEFAULT_DATA_SHARDS = 4;

/** Default number of parity shards for erasure coding */
static constexpr uint32_t DEFAULT_PARITY_SHARDS = 2;

/** Number of samples for DA sampling (light clients) */
static constexpr uint32_t DA_SAMPLE_COUNT = 16;

/** Maximum compression ratio (for validation) */
static constexpr double MAX_COMPRESSION_RATIO = 0.95;

/** Compression level for zstd (1-22, higher = better compression but slower) */
static constexpr int ZSTD_COMPRESSION_LEVEL = 3;

// Forward declarations
struct DACommitment;

/**
 * @brief Batch data structure for L1 submission
 * 
 * Contains all information needed to submit a batch of L2 blocks to L1.
 * This includes the state transition, compressed transactions, and
 * sequencer signature.
 * 
 * Requirements: 3.2, 3.4, 7.1
 */
struct BatchData {
    /** First L2 block number in this batch */
    uint64_t startBlock;
    
    /** Last L2 block number in this batch (inclusive) */
    uint64_t endBlock;
    
    /** State root before this batch */
    uint256 preStateRoot;
    
    /** State root after this batch */
    uint256 postStateRoot;
    
    /** Compressed transaction data */
    std::vector<unsigned char> compressedTransactions;
    
    /** Merkle root of all transactions in the batch */
    uint256 transactionsRoot;
    
    /** Number of transactions in the batch */
    uint64_t transactionCount;
    
    /** Total gas used by all transactions */
    uint64_t totalGasUsed;
    
    /** L2 chain ID */
    uint64_t l2ChainId;
    
    /** L1 block number this batch references */
    uint64_t l1AnchorBlock;
    
    /** L1 block hash this batch references */
    uint256 l1AnchorHash;
    
    /** Address of the sequencer who created this batch */
    uint160 sequencerAddress;
    
    /** Sequencer's signature over the batch hash */
    std::vector<unsigned char> sequencerSignature;
    
    /** Timestamp when batch was created */
    uint64_t timestamp;
    
    /** Protocol version */
    uint32_t version;

    BatchData() 
        : startBlock(0), endBlock(0), transactionCount(0), 
          totalGasUsed(0), l2ChainId(DEFAULT_L2_CHAIN_ID),
          l1AnchorBlock(0), timestamp(0), version(L2_PROTOCOL_VERSION) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(version);
        READWRITE(startBlock);
        READWRITE(endBlock);
        READWRITE(preStateRoot);
        READWRITE(postStateRoot);
        READWRITE(compressedTransactions);
        READWRITE(transactionsRoot);
        READWRITE(transactionCount);
        READWRITE(totalGasUsed);
        READWRITE(l2ChainId);
        READWRITE(l1AnchorBlock);
        READWRITE(l1AnchorHash);
        READWRITE(sequencerAddress);
        READWRITE(sequencerSignature);
        READWRITE(timestamp);
    }

    /** Compute the hash of this batch */
    uint256 GetHash() const;

    /** Get the number of L2 blocks in this batch */
    uint64_t GetBlockCount() const {
        return (endBlock >= startBlock) ? (endBlock - startBlock + 1) : 0;
    }

    /** Get the size of compressed data */
    size_t GetCompressedSize() const {
        return compressedTransactions.size();
    }

    /** Check if batch is within size limits */
    bool IsWithinSizeLimit() const {
        return compressedTransactions.size() <= MAX_BATCH_SIZE;
    }

    /** Validate basic batch structure */
    bool ValidateStructure() const;

    /** Serialize batch to bytes */
    std::vector<unsigned char> Serialize() const;

    /** Deserialize batch from bytes */
    bool Deserialize(const std::vector<unsigned char>& data);

    /** Get string representation for logging */
    std::string ToString() const;

    bool operator==(const BatchData& other) const;
    bool operator!=(const BatchData& other) const { return !(*this == other); }
};

/**
 * @brief Data Availability Commitment for sampling
 * 
 * Contains the commitment data needed for light clients to verify
 * data availability through sampling without downloading full data.
 * 
 * Requirements: 7.2, 24.4
 */
struct DACommitment {
    /** Hash of the original data */
    uint256 dataHash;
    
    /** Size of the original data in bytes */
    uint64_t dataSize;
    
    /** Root of the erasure-coded data (for DA sampling) */
    uint256 erasureCodingRoot;
    
    /** Merkle roots of each column in the erasure-coded matrix */
    std::vector<uint256> columnRoots;
    
    /** Merkle roots of each row in the erasure-coded matrix */
    std::vector<uint256> rowRoots;
    
    /** Number of data shards */
    uint32_t dataShards;
    
    /** Number of parity shards */
    uint32_t parityShards;
    
    /** Shard size in bytes */
    uint64_t shardSize;
    
    /** Batch hash this commitment is for */
    uint256 batchHash;
    
    /** Timestamp when commitment was created */
    uint64_t timestamp;

    DACommitment() 
        : dataSize(0), dataShards(DEFAULT_DATA_SHARDS), 
          parityShards(DEFAULT_PARITY_SHARDS), shardSize(0), timestamp(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(dataHash);
        READWRITE(dataSize);
        READWRITE(erasureCodingRoot);
        READWRITE(columnRoots);
        READWRITE(rowRoots);
        READWRITE(dataShards);
        READWRITE(parityShards);
        READWRITE(shardSize);
        READWRITE(batchHash);
        READWRITE(timestamp);
    }

    /** Compute the hash of this commitment */
    uint256 GetHash() const;

    /** Get total number of shards (data + parity) */
    uint32_t GetTotalShards() const {
        return dataShards + parityShards;
    }

    /** Validate commitment structure */
    bool ValidateStructure() const;

    /** Serialize commitment to bytes */
    std::vector<unsigned char> Serialize() const;

    /** Deserialize commitment from bytes */
    bool Deserialize(const std::vector<unsigned char>& data);

    /** Get string representation for logging */
    std::string ToString() const;

    bool operator==(const DACommitment& other) const;
    bool operator!=(const DACommitment& other) const { return !(*this == other); }
};

/**
 * @brief DA Sample for light client verification
 */
struct DASample {
    /** Row index in the matrix */
    uint32_t row;
    
    /** Column index in the matrix */
    uint32_t column;
    
    /** The sample data */
    std::vector<unsigned char> data;
    
    /** Merkle proof for row verification */
    std::vector<uint256> rowProof;
    
    /** Merkle proof for column verification */
    std::vector<uint256> columnProof;

    DASample() : row(0), column(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(row);
        READWRITE(column);
        READWRITE(data);
        READWRITE(rowProof);
        READWRITE(columnProof);
    }

    /** Verify this sample against a DA commitment */
    bool Verify(const DACommitment& commitment) const;
};

/**
 * @brief DA Sampling Result
 */
struct DASamplingResult {
    bool success;
    uint32_t samplesRequested;
    uint32_t samplesVerified;
    double confidence;
    std::string error;
    std::vector<DASample> samples;

    DASamplingResult() 
        : success(false), samplesRequested(0), samplesVerified(0), confidence(0.0) {}

    bool HasSufficientConfidence(double minConfidence = 0.99) const {
        return success && confidence >= minConfidence;
    }
};

/**
 * @brief Erasure-coded shard
 */
struct ErasureShard {
    uint32_t index;
    bool isData;
    std::vector<unsigned char> data;
    uint256 hash;

    ErasureShard() : index(0), isData(true) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(index);
        READWRITE(isData);
        READWRITE(data);
        READWRITE(hash);
    }

    uint256 ComputeHash() const {
        return Hash(data.begin(), data.end());
    }

    bool VerifyHash() const {
        return hash == ComputeHash();
    }
};

/**
 * @brief Batch submission result
 */
struct BatchSubmissionResult {
    bool success;
    uint256 l1TxHash;
    uint256 batchHash;
    uint256 commitmentHash;
    std::string error;
    uint64_t gasCost;

    BatchSubmissionResult() : success(false), gasCost(0) {}

    static BatchSubmissionResult Success(
        const uint256& l1Tx, const uint256& batch, 
        const uint256& commitment, uint64_t gas);
    static BatchSubmissionResult Failure(const std::string& err);
};


/**
 * @brief Data Availability Layer
 * 
 * Manages data availability for the L2 system, including:
 * - Batch creation and submission to L1
 * - Transaction compression/decompression
 * - DA commitment generation
 * - Erasure coding for light client verification
 * - Data availability sampling
 * 
 * Requirements: 7.1, 7.2, 7.3, 7.5, 11.6, 24.4, 41.2
 */
class DataAvailabilityLayer {
public:
    explicit DataAvailabilityLayer(uint64_t chainId);
    ~DataAvailabilityLayer();

    // Batch Management (Requirements 3.2, 3.4, 7.1)
    BatchData CreateBatch(
        const std::vector<CTransaction>& transactions,
        uint64_t startBlock,
        uint64_t endBlock,
        const uint256& preStateRoot,
        const uint256& postStateRoot,
        const uint160& sequencerAddr);

    BatchSubmissionResult PublishBatch(const BatchData& batch);
    bool ValidateBatch(const BatchData& batch) const;
    std::optional<BatchData> GetBatch(const uint256& batchHash) const;

    // Compression (Requirement 7.5)
    std::vector<unsigned char> CompressTransactions(
        const std::vector<CTransaction>& transactions) const;
    std::vector<CTransaction> DecompressTransactions(
        const std::vector<unsigned char>& compressed) const;
    static double GetCompressionRatio(size_t original, size_t compressed);

    // DA Commitment and Sampling (Requirements 7.2, 24.4)
    DACommitment GenerateDACommitment(
        const std::vector<unsigned char>& data,
        const uint256& batchHash);
    bool VerifyDACommitment(
        const DACommitment& commitment,
        const std::vector<unsigned char>& data) const;
    DASamplingResult SampleDataAvailability(
        const DACommitment& commitment,
        uint32_t sampleCount = DA_SAMPLE_COUNT);
    std::optional<DASample> GetSample(
        const DACommitment& commitment,
        uint32_t row,
        uint32_t column) const;

    // Erasure Coding (Requirements 7.2, 7.3)
    std::vector<ErasureShard> ErasureEncode(
        const std::vector<unsigned char>& data,
        uint32_t dataShards = DEFAULT_DATA_SHARDS,
        uint32_t parityShards = DEFAULT_PARITY_SHARDS);
    std::vector<unsigned char> ErasureDecode(
        const std::vector<ErasureShard>& shards,
        uint32_t dataShards,
        uint32_t parityShards,
        uint64_t originalSize);
    static bool CanReconstruct(
        const std::vector<ErasureShard>& shards,
        uint32_t dataShards);

    // L1 Data Reconstruction (Requirements 7.3, 11.6, 41.2)
    std::optional<BatchData> ReconstructFromL1(
        uint64_t startBlock,
        uint64_t endBlock);
    bool IsDataAvailable(const uint256& batchHash) const;
    std::vector<uint256> GetBatchesInRange(
        uint64_t startBlock,
        uint64_t endBlock) const;

    // Configuration
    void SetBatchInterval(uint32_t interval);
    uint32_t GetBatchInterval() const;
    void SetL1Anchor(uint64_t blockNumber, const uint256& blockHash);
    uint64_t GetChainId() const { return chainId_; }
    std::string GetStatistics() const;

    // Static utility methods (public for verification)
    static bool VerifyMerkleProof(
        const uint256& root,
        const uint256& leaf,
        const std::vector<uint256>& proof,
        size_t index);

private:
    uint64_t chainId_;
    uint32_t batchInterval_;
    uint64_t l1AnchorBlock_;
    uint256 l1AnchorHash_;
    std::map<uint256, BatchData> batches_;
    std::map<uint256, DACommitment> commitments_;
    std::map<uint256, std::vector<ErasureShard>> shards_;
    mutable CCriticalSection cs_da_;

    uint256 ComputeTransactionsRoot(
        const std::vector<CTransaction>& transactions) const;
    uint256 ComputeMerkleRoot(const std::vector<uint256>& hashes) const;
    std::vector<uint256> GenerateMerkleProof(
        const std::vector<uint256>& hashes,
        size_t index) const;
    std::vector<unsigned char> PadToShardSize(
        const std::vector<unsigned char>& data,
        size_t shardSize) const;
    std::vector<std::vector<unsigned char>> ComputeParityShards(
        const std::vector<std::vector<unsigned char>>& dataShards,
        uint32_t parityCount) const;
};

} // namespace l2

#endif // CASCOIN_L2_DATA_AVAILABILITY_H
