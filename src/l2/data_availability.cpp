// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file data_availability.cpp
 * @brief Data Availability Layer implementation for Cascoin L2
 * 
 * Requirements: 3.2, 3.4, 7.1, 7.2, 7.3, 7.5, 11.6, 24.4, 41.2
 */

#include <l2/data_availability.h>
#include <hash.h>
#include <random.h>
#include <util.h>
#include <utiltime.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace l2 {

// ============================================================================
// BatchData Implementation
// ============================================================================

uint256 BatchData::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << version;
    ss << startBlock;
    ss << endBlock;
    ss << preStateRoot;
    ss << postStateRoot;
    ss << transactionsRoot;
    ss << transactionCount;
    ss << totalGasUsed;
    ss << l2ChainId;
    ss << l1AnchorBlock;
    ss << l1AnchorHash;
    ss << sequencerAddress;
    ss << timestamp;
    return ss.GetHash();
}

bool BatchData::ValidateStructure() const
{
    // Check block range
    if (endBlock < startBlock) {
        return false;
    }
    
    // Check size limits
    if (!IsWithinSizeLimit()) {
        return false;
    }
    
    // Check version
    if (version == 0 || version > L2_PROTOCOL_VERSION) {
        return false;
    }
    
    // Check sequencer address is set
    if (sequencerAddress.IsNull()) {
        return false;
    }
    
    return true;
}

std::vector<unsigned char> BatchData::Serialize() const
{
    CDataStream ss(SER_DISK, 0);
    ss << *this;
    return std::vector<unsigned char>(ss.begin(), ss.end());
}

bool BatchData::Deserialize(const std::vector<unsigned char>& data)
{
    if (data.empty()) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, 0);
        ss >> *this;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string BatchData::ToString() const
{
    std::ostringstream ss;
    ss << "BatchData(blocks=" << startBlock << "-" << endBlock
       << ", txCount=" << transactionCount
       << ", gasUsed=" << totalGasUsed
       << ", compressedSize=" << compressedTransactions.size()
       << ", hash=" << GetHash().ToString().substr(0, 16) << "...)";
    return ss.str();
}

bool BatchData::operator==(const BatchData& other) const
{
    return version == other.version &&
           startBlock == other.startBlock &&
           endBlock == other.endBlock &&
           preStateRoot == other.preStateRoot &&
           postStateRoot == other.postStateRoot &&
           compressedTransactions == other.compressedTransactions &&
           transactionsRoot == other.transactionsRoot &&
           transactionCount == other.transactionCount &&
           totalGasUsed == other.totalGasUsed &&
           l2ChainId == other.l2ChainId &&
           l1AnchorBlock == other.l1AnchorBlock &&
           l1AnchorHash == other.l1AnchorHash &&
           sequencerAddress == other.sequencerAddress &&
           sequencerSignature == other.sequencerSignature &&
           timestamp == other.timestamp;
}

// ============================================================================
// DACommitment Implementation
// ============================================================================

uint256 DACommitment::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << dataHash;
    ss << dataSize;
    ss << erasureCodingRoot;
    ss << dataShards;
    ss << parityShards;
    ss << shardSize;
    ss << batchHash;
    ss << timestamp;
    return ss.GetHash();
}

bool DACommitment::ValidateStructure() const
{
    // Check shard counts
    if (dataShards == 0 || parityShards == 0) {
        return false;
    }
    
    // Check roots match shard counts
    if (columnRoots.size() != GetTotalShards() ||
        rowRoots.size() != GetTotalShards()) {
        return false;
    }
    
    // Check data size consistency
    if (dataSize > 0 && shardSize == 0) {
        return false;
    }
    
    return true;
}

std::vector<unsigned char> DACommitment::Serialize() const
{
    CDataStream ss(SER_DISK, 0);
    ss << *this;
    return std::vector<unsigned char>(ss.begin(), ss.end());
}

bool DACommitment::Deserialize(const std::vector<unsigned char>& data)
{
    if (data.empty()) {
        return false;
    }
    try {
        CDataStream ss(data, SER_DISK, 0);
        ss >> *this;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string DACommitment::ToString() const
{
    std::ostringstream ss;
    ss << "DACommitment(dataSize=" << dataSize
       << ", shards=" << dataShards << "+" << parityShards
       << ", shardSize=" << shardSize
       << ", hash=" << GetHash().ToString().substr(0, 16) << "...)";
    return ss.str();
}

bool DACommitment::operator==(const DACommitment& other) const
{
    return dataHash == other.dataHash &&
           dataSize == other.dataSize &&
           erasureCodingRoot == other.erasureCodingRoot &&
           columnRoots == other.columnRoots &&
           rowRoots == other.rowRoots &&
           dataShards == other.dataShards &&
           parityShards == other.parityShards &&
           shardSize == other.shardSize &&
           batchHash == other.batchHash &&
           timestamp == other.timestamp;
}

// ============================================================================
// DASample Implementation
// ============================================================================

bool DASample::Verify(const DACommitment& commitment) const
{
    // Check bounds
    if (row >= commitment.GetTotalShards() || 
        column >= commitment.GetTotalShards()) {
        return false;
    }
    
    // Compute hash of sample data
    uint256 leafHash = Hash(data.begin(), data.end());
    
    // In our simplified DA model, we use a direct hash verification approach:
    // - rowRoots[i] contains the hash of shard i's data
    // - The proof contains the expected hash for verification
    // 
    // For a full 2D DA sampling implementation, this would use proper
    // Merkle proofs with sibling hashes. For now, we verify that:
    // 1. The sample data hashes to the expected value in the proof
    // 2. The proof matches the commitment's row/column roots
    
    // Verify row proof - check that sample data hash matches the row root
    if (row < commitment.rowRoots.size()) {
        // In simplified model: rowRoots[row] should equal hash of shard data
        if (leafHash != commitment.rowRoots[row]) {
            return false;
        }
        // Also verify the proof contains the correct hash
        if (!rowProof.empty() && rowProof[0] != leafHash) {
            return false;
        }
    }
    
    // Verify column proof - in simplified model, column roots mirror row roots
    // since we treat each shard as both a row and column element
    if (column < commitment.columnRoots.size() && row < commitment.columnRoots.size()) {
        // The column proof should also match the data hash
        if (!columnProof.empty() && columnProof[0] != leafHash) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// BatchSubmissionResult Implementation
// ============================================================================

BatchSubmissionResult BatchSubmissionResult::Success(
    const uint256& l1Tx, 
    const uint256& batch, 
    const uint256& commitment,
    uint64_t gas)
{
    BatchSubmissionResult result;
    result.success = true;
    result.l1TxHash = l1Tx;
    result.batchHash = batch;
    result.commitmentHash = commitment;
    result.gasCost = gas;
    return result;
}

BatchSubmissionResult BatchSubmissionResult::Failure(const std::string& err)
{
    BatchSubmissionResult result;
    result.success = false;
    result.error = err;
    return result;
}

// ============================================================================
// DataAvailabilityLayer Implementation
// ============================================================================

DataAvailabilityLayer::DataAvailabilityLayer(uint64_t chainId)
    : chainId_(chainId)
    , batchInterval_(DEFAULT_BATCH_INTERVAL)
    , l1AnchorBlock_(0)
{
}

DataAvailabilityLayer::~DataAvailabilityLayer() = default;

// ----------------------------------------------------------------------------
// Batch Management
// ----------------------------------------------------------------------------

BatchData DataAvailabilityLayer::CreateBatch(
    const std::vector<CTransaction>& transactions,
    uint64_t startBlock,
    uint64_t endBlock,
    const uint256& preStateRoot,
    const uint256& postStateRoot,
    const uint160& sequencerAddr)
{
    BatchData batch;
    batch.version = L2_PROTOCOL_VERSION;
    batch.startBlock = startBlock;
    batch.endBlock = endBlock;
    batch.preStateRoot = preStateRoot;
    batch.postStateRoot = postStateRoot;
    batch.l2ChainId = chainId_;
    batch.sequencerAddress = sequencerAddr;
    batch.timestamp = GetTime();
    batch.transactionCount = transactions.size();
    
    // Compute transactions root
    batch.transactionsRoot = ComputeTransactionsRoot(transactions);
    
    // Compress transactions
    batch.compressedTransactions = CompressTransactions(transactions);
    
    // Calculate total gas used (simplified - in real impl would sum from receipts)
    batch.totalGasUsed = 0;
    for (const auto& tx : transactions) {
        // Estimate gas based on transaction size
        batch.totalGasUsed += 21000 + tx.GetTotalSize() * 16;
    }
    
    // Set L1 anchor
    batch.l1AnchorBlock = l1AnchorBlock_;
    batch.l1AnchorHash = l1AnchorHash_;
    
    return batch;
}

BatchSubmissionResult DataAvailabilityLayer::PublishBatch(const BatchData& batch)
{
    LOCK(cs_da_);
    
    // Validate batch
    if (!ValidateBatch(batch)) {
        return BatchSubmissionResult::Failure("Invalid batch structure");
    }
    
    // Check size limits
    if (!batch.IsWithinSizeLimit()) {
        return BatchSubmissionResult::Failure("Batch exceeds size limit");
    }
    
    uint256 batchHash = batch.GetHash();
    
    // Generate DA commitment
    DACommitment commitment = GenerateDACommitment(
        batch.compressedTransactions, batchHash);
    
    // Store batch and commitment
    batches_[batchHash] = batch;
    commitments_[batchHash] = commitment;
    
    // In a real implementation, this would submit to L1
    // For now, we simulate success
    uint256 l1TxHash = Hash(batchHash.begin(), batchHash.end());
    
    // Estimate gas cost (simplified)
    uint64_t gasCost = 21000 + batch.compressedTransactions.size() * 16;
    
    return BatchSubmissionResult::Success(
        l1TxHash, batchHash, commitment.GetHash(), gasCost);
}

bool DataAvailabilityLayer::ValidateBatch(const BatchData& batch) const
{
    return batch.ValidateStructure();
}

std::optional<BatchData> DataAvailabilityLayer::GetBatch(const uint256& batchHash) const
{
    LOCK(cs_da_);
    auto it = batches_.find(batchHash);
    if (it != batches_.end()) {
        return it->second;
    }
    return std::nullopt;
}

// ----------------------------------------------------------------------------
// Compression
// ----------------------------------------------------------------------------

std::vector<unsigned char> DataAvailabilityLayer::CompressTransactions(
    const std::vector<CTransaction>& transactions) const
{
    // Serialize transactions
    CDataStream ss(SER_DISK, 0);
    ss << VARINT(static_cast<uint64_t>(transactions.size()));
    for (const auto& tx : transactions) {
        ss << tx;
    }
    std::vector<unsigned char> serialized(ss.begin(), ss.end());
    
    if (serialized.empty()) {
        return serialized;
    }
    
    // Simple RLE-like compression for demonstration
    // In production, use zstd or similar
    std::vector<unsigned char> compressed;
    compressed.reserve(serialized.size());
    
    // Add header: original size (8 bytes)
    uint64_t originalSize = serialized.size();
    for (int i = 0; i < 8; ++i) {
        compressed.push_back(static_cast<unsigned char>((originalSize >> (i * 8)) & 0xFF));
    }
    
    // Simple compression: store runs of repeated bytes
    size_t i = 0;
    while (i < serialized.size()) {
        unsigned char byte = serialized[i];
        size_t runLength = 1;
        
        // Count consecutive identical bytes
        while (i + runLength < serialized.size() && 
               serialized[i + runLength] == byte && 
               runLength < 255) {
            ++runLength;
        }
        
        if (runLength >= 4) {
            // Encode as run: 0xFF, byte, length
            compressed.push_back(0xFF);
            compressed.push_back(byte);
            compressed.push_back(static_cast<unsigned char>(runLength));
            i += runLength;
        } else {
            // Store literal (escape 0xFF as 0xFF 0xFF 0x01)
            if (byte == 0xFF) {
                compressed.push_back(0xFF);
                compressed.push_back(0xFF);
                compressed.push_back(0x01);
            } else {
                compressed.push_back(byte);
            }
            ++i;
        }
    }
    
    // If compression didn't help, return original with header
    if (compressed.size() >= serialized.size() + 8) {
        compressed.clear();
        compressed.reserve(serialized.size() + 9);
        // Header: 0x00 means uncompressed
        compressed.push_back(0x00);
        for (int j = 0; j < 8; ++j) {
            compressed.push_back(static_cast<unsigned char>((originalSize >> (j * 8)) & 0xFF));
        }
        compressed.insert(compressed.end(), serialized.begin(), serialized.end());
    }
    
    return compressed;
}

std::vector<CTransaction> DataAvailabilityLayer::DecompressTransactions(
    const std::vector<unsigned char>& compressed) const
{
    std::vector<CTransaction> transactions;
    
    if (compressed.size() < 9) {
        return transactions;
    }
    
    std::vector<unsigned char> decompressed;
    
    // Check if uncompressed (first byte is 0x00)
    if (compressed[0] == 0x00) {
        // Read original size
        uint64_t originalSize = 0;
        for (int i = 0; i < 8; ++i) {
            originalSize |= static_cast<uint64_t>(compressed[1 + i]) << (i * 8);
        }
        
        // Copy data directly
        decompressed.assign(compressed.begin() + 9, compressed.end());
    } else {
        // Read original size from header
        uint64_t originalSize = 0;
        for (int i = 0; i < 8; ++i) {
            originalSize |= static_cast<uint64_t>(compressed[i]) << (i * 8);
        }
        
        decompressed.reserve(originalSize);
        
        // Decompress
        size_t i = 8;
        while (i < compressed.size()) {
            if (compressed[i] == 0xFF && i + 2 < compressed.size()) {
                unsigned char byte = compressed[i + 1];
                unsigned char length = compressed[i + 2];
                
                if (byte == 0xFF && length == 0x01) {
                    // Escaped 0xFF
                    decompressed.push_back(0xFF);
                } else {
                    // Run of bytes
                    for (unsigned char j = 0; j < length; ++j) {
                        decompressed.push_back(byte);
                    }
                }
                i += 3;
            } else {
                decompressed.push_back(compressed[i]);
                ++i;
            }
        }
    }
    
    // Deserialize transactions using mutable transactions
    try {
        CDataStream ss(decompressed, SER_DISK, 0);
        uint64_t count = 0;
        ss >> VARINT(count);
        
        for (uint64_t i = 0; i < count; ++i) {
            CMutableTransaction mtx;
            ss >> mtx;
            transactions.push_back(CTransaction(mtx));
        }
    } catch (const std::exception&) {
        transactions.clear();
    }
    
    return transactions;
}

double DataAvailabilityLayer::GetCompressionRatio(size_t original, size_t compressed)
{
    if (original == 0) {
        return 1.0;
    }
    return static_cast<double>(compressed) / static_cast<double>(original);
}


// ----------------------------------------------------------------------------
// DA Commitment and Sampling
// ----------------------------------------------------------------------------

DACommitment DataAvailabilityLayer::GenerateDACommitment(
    const std::vector<unsigned char>& data,
    const uint256& batchHash)
{
    DACommitment commitment;
    commitment.batchHash = batchHash;
    commitment.dataHash = Hash(data.begin(), data.end());
    commitment.dataSize = data.size();
    commitment.dataShards = DEFAULT_DATA_SHARDS;
    commitment.parityShards = DEFAULT_PARITY_SHARDS;
    commitment.timestamp = GetTime();
    
    if (data.empty()) {
        commitment.shardSize = 0;
        commitment.columnRoots.resize(commitment.GetTotalShards());
        commitment.rowRoots.resize(commitment.GetTotalShards());
        return commitment;
    }
    
    // Erasure encode the data
    std::vector<ErasureShard> erasureShards = ErasureEncode(
        data, commitment.dataShards, commitment.parityShards);
    
    if (!erasureShards.empty()) {
        commitment.shardSize = erasureShards[0].data.size();
    }
    
    // Store shards for later sampling
    {
        LOCK(cs_da_);
        shards_[batchHash] = erasureShards;
    }
    
    // Compute row and column roots
    uint32_t totalShards = commitment.GetTotalShards();
    commitment.rowRoots.resize(totalShards);
    commitment.columnRoots.resize(totalShards);
    
    // For simplicity, treat each shard as a row
    // In a full implementation, this would be a 2D matrix
    for (uint32_t i = 0; i < totalShards && i < erasureShards.size(); ++i) {
        commitment.rowRoots[i] = erasureShards[i].hash;
        commitment.columnRoots[i] = erasureShards[i].hash;
    }
    
    // Compute erasure coding root
    std::vector<uint256> allHashes;
    for (const auto& shard : erasureShards) {
        allHashes.push_back(shard.hash);
    }
    commitment.erasureCodingRoot = ComputeMerkleRoot(allHashes);
    
    return commitment;
}

bool DataAvailabilityLayer::VerifyDACommitment(
    const DACommitment& commitment,
    const std::vector<unsigned char>& data) const
{
    // Verify data hash
    uint256 computedHash = Hash(data.begin(), data.end());
    if (computedHash != commitment.dataHash) {
        return false;
    }
    
    // Verify data size
    if (data.size() != commitment.dataSize) {
        return false;
    }
    
    // Verify structure
    if (!commitment.ValidateStructure()) {
        return false;
    }
    
    return true;
}

DASamplingResult DataAvailabilityLayer::SampleDataAvailability(
    const DACommitment& commitment,
    uint32_t sampleCount)
{
    DASamplingResult result;
    result.samplesRequested = sampleCount;
    result.samplesVerified = 0;
    
    if (!commitment.ValidateStructure()) {
        result.error = "Invalid commitment structure";
        return result;
    }
    
    uint32_t totalShards = commitment.GetTotalShards();
    if (totalShards == 0) {
        result.error = "No shards in commitment";
        return result;
    }
    
    // Generate random sample positions
    FastRandomContext rng(true);
    std::set<std::pair<uint32_t, uint32_t>> sampledPositions;
    
    while (sampledPositions.size() < sampleCount && 
           sampledPositions.size() < static_cast<size_t>(totalShards * totalShards)) {
        uint32_t row = rng.rand32() % totalShards;
        uint32_t col = rng.rand32() % totalShards;
        sampledPositions.insert({row, col});
    }
    
    // Try to get and verify each sample
    for (const auto& pos : sampledPositions) {
        auto sample = GetSample(commitment, pos.first, pos.second);
        if (sample && sample->Verify(commitment)) {
            result.samples.push_back(*sample);
            result.samplesVerified++;
        }
    }
    
    // Calculate confidence
    // Probability that data is unavailable given k successful samples out of n total cells
    // with at least 50% of data missing: P(unavailable) = (0.5)^k
    if (result.samplesVerified > 0) {
        result.confidence = 1.0 - std::pow(0.5, result.samplesVerified);
    } else {
        result.confidence = 0.0;
    }
    
    result.success = (result.samplesVerified == result.samplesRequested);
    
    if (!result.success && result.error.empty()) {
        result.error = "Some samples could not be verified";
    }
    
    return result;
}

std::optional<DASample> DataAvailabilityLayer::GetSample(
    const DACommitment& commitment,
    uint32_t row,
    uint32_t column) const
{
    LOCK(cs_da_);
    
    auto it = shards_.find(commitment.batchHash);
    if (it == shards_.end()) {
        return std::nullopt;
    }
    
    const auto& erasureShards = it->second;
    
    // For our simplified model, row == shard index
    if (row >= erasureShards.size()) {
        return std::nullopt;
    }
    
    DASample sample;
    sample.row = row;
    sample.column = column;
    sample.data = erasureShards[row].data;
    
    // Generate proofs (simplified - in full impl would be proper Merkle proofs)
    // For now, just include the hash as a single-element proof
    sample.rowProof.push_back(erasureShards[row].hash);
    sample.columnProof.push_back(erasureShards[row].hash);
    
    return sample;
}

// ----------------------------------------------------------------------------
// Erasure Coding
// ----------------------------------------------------------------------------

std::vector<ErasureShard> DataAvailabilityLayer::ErasureEncode(
    const std::vector<unsigned char>& data,
    uint32_t dataShards,
    uint32_t parityShards)
{
    std::vector<ErasureShard> result;
    
    if (data.empty() || dataShards == 0) {
        return result;
    }
    
    // Calculate shard size (round up)
    size_t shardSize = (data.size() + dataShards - 1) / dataShards;
    
    // Pad data to be evenly divisible
    std::vector<unsigned char> paddedData = PadToShardSize(data, shardSize * dataShards);
    
    // Create data shards
    std::vector<std::vector<unsigned char>> dataShardData;
    for (uint32_t i = 0; i < dataShards; ++i) {
        size_t start = i * shardSize;
        size_t end = std::min(start + shardSize, paddedData.size());
        
        ErasureShard shard;
        shard.index = i;
        shard.isData = true;
        shard.data.assign(paddedData.begin() + start, paddedData.begin() + end);
        
        // Ensure all shards are same size
        if (shard.data.size() < shardSize) {
            shard.data.resize(shardSize, 0);
        }
        
        shard.hash = shard.ComputeHash();
        result.push_back(shard);
        dataShardData.push_back(shard.data);
    }
    
    // Compute parity shards
    auto parityData = ComputeParityShards(dataShardData, parityShards);
    
    for (uint32_t i = 0; i < parityShards && i < parityData.size(); ++i) {
        ErasureShard shard;
        shard.index = dataShards + i;
        shard.isData = false;
        shard.data = parityData[i];
        shard.hash = shard.ComputeHash();
        result.push_back(shard);
    }
    
    return result;
}

std::vector<unsigned char> DataAvailabilityLayer::ErasureDecode(
    const std::vector<ErasureShard>& shards,
    uint32_t dataShards,
    uint32_t parityShards,
    uint64_t originalSize)
{
    std::vector<unsigned char> result;
    
    if (!CanReconstruct(shards, dataShards)) {
        return result;
    }
    
    // Sort shards by index
    std::vector<ErasureShard> sortedShards = shards;
    std::sort(sortedShards.begin(), sortedShards.end(),
              [](const ErasureShard& a, const ErasureShard& b) {
                  return a.index < b.index;
              });
    
    // For simple case: if we have all data shards, just concatenate
    std::map<uint32_t, const ErasureShard*> shardMap;
    for (const auto& shard : sortedShards) {
        shardMap[shard.index] = &shard;
    }
    
    // Check if we have all data shards
    bool haveAllData = true;
    for (uint32_t i = 0; i < dataShards; ++i) {
        if (shardMap.find(i) == shardMap.end()) {
            haveAllData = false;
            break;
        }
    }
    
    if (haveAllData) {
        // Simple case: concatenate data shards
        for (uint32_t i = 0; i < dataShards; ++i) {
            const auto& shard = *shardMap[i];
            result.insert(result.end(), shard.data.begin(), shard.data.end());
        }
    } else {
        // Need to reconstruct from parity
        // For this simplified implementation, we use XOR reconstruction
        // A full implementation would use Reed-Solomon or similar
        
        // Find which data shard is missing
        uint32_t missingIndex = 0;
        for (uint32_t i = 0; i < dataShards; ++i) {
            if (shardMap.find(i) == shardMap.end()) {
                missingIndex = i;
                break;
            }
        }
        
        // Check if we have the first parity shard
        if (shardMap.find(dataShards) == shardMap.end()) {
            return result; // Can't reconstruct
        }
        
        // Reconstruct missing shard using XOR
        const auto& parityShard = *shardMap[dataShards];
        std::vector<unsigned char> reconstructed = parityShard.data;
        
        for (uint32_t i = 0; i < dataShards; ++i) {
            if (i != missingIndex && shardMap.find(i) != shardMap.end()) {
                const auto& shard = *shardMap[i];
                for (size_t j = 0; j < reconstructed.size() && j < shard.data.size(); ++j) {
                    reconstructed[j] ^= shard.data[j];
                }
            }
        }
        
        // Now concatenate all data shards
        for (uint32_t i = 0; i < dataShards; ++i) {
            if (i == missingIndex) {
                result.insert(result.end(), reconstructed.begin(), reconstructed.end());
            } else {
                const auto& shard = *shardMap[i];
                result.insert(result.end(), shard.data.begin(), shard.data.end());
            }
        }
    }
    
    // Trim to original size
    if (result.size() > originalSize) {
        result.resize(originalSize);
    }
    
    return result;
}

bool DataAvailabilityLayer::CanReconstruct(
    const std::vector<ErasureShard>& shards,
    uint32_t dataShards)
{
    if (shards.size() < dataShards) {
        return false;
    }
    
    // Count available data shards
    uint32_t availableDataShards = 0;
    uint32_t availableParityShards = 0;
    
    for (const auto& shard : shards) {
        if (shard.isData) {
            availableDataShards++;
        } else {
            availableParityShards++;
        }
    }
    
    // Can reconstruct if we have at least dataShards total
    return (availableDataShards + availableParityShards) >= dataShards;
}


// ----------------------------------------------------------------------------
// L1 Data Reconstruction
// ----------------------------------------------------------------------------

std::optional<BatchData> DataAvailabilityLayer::ReconstructFromL1(
    uint64_t startBlock,
    uint64_t endBlock)
{
    LOCK(cs_da_);
    
    // Find batch that covers this range
    for (const auto& pair : batches_) {
        const BatchData& batch = pair.second;
        if (batch.startBlock <= startBlock && batch.endBlock >= endBlock) {
            return batch;
        }
    }
    
    // In a real implementation, this would:
    // 1. Query L1 for batch data in the block range
    // 2. Decompress and verify the data
    // 3. Return the reconstructed batch
    
    return std::nullopt;
}

bool DataAvailabilityLayer::IsDataAvailable(const uint256& batchHash) const
{
    LOCK(cs_da_);
    return batches_.find(batchHash) != batches_.end();
}

std::vector<uint256> DataAvailabilityLayer::GetBatchesInRange(
    uint64_t startBlock,
    uint64_t endBlock) const
{
    LOCK(cs_da_);
    
    std::vector<uint256> result;
    
    for (const auto& pair : batches_) {
        const BatchData& batch = pair.second;
        // Check if batch overlaps with range
        if (batch.endBlock >= startBlock && batch.startBlock <= endBlock) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

// ----------------------------------------------------------------------------
// Configuration
// ----------------------------------------------------------------------------

void DataAvailabilityLayer::SetBatchInterval(uint32_t interval)
{
    LOCK(cs_da_);
    batchInterval_ = interval;
}

uint32_t DataAvailabilityLayer::GetBatchInterval() const
{
    LOCK(cs_da_);
    return batchInterval_;
}

void DataAvailabilityLayer::SetL1Anchor(uint64_t blockNumber, const uint256& blockHash)
{
    LOCK(cs_da_);
    l1AnchorBlock_ = blockNumber;
    l1AnchorHash_ = blockHash;
}

std::string DataAvailabilityLayer::GetStatistics() const
{
    LOCK(cs_da_);
    
    std::ostringstream ss;
    ss << "DataAvailabilityLayer Statistics:\n"
       << "  Chain ID: " << chainId_ << "\n"
       << "  Batch Interval: " << batchInterval_ << " blocks\n"
       << "  L1 Anchor Block: " << l1AnchorBlock_ << "\n"
       << "  Stored Batches: " << batches_.size() << "\n"
       << "  Stored Commitments: " << commitments_.size() << "\n"
       << "  Stored Shard Sets: " << shards_.size() << "\n";
    
    // Calculate total data size
    uint64_t totalDataSize = 0;
    uint64_t totalCompressedSize = 0;
    for (const auto& pair : batches_) {
        totalCompressedSize += pair.second.compressedTransactions.size();
    }
    
    ss << "  Total Compressed Data: " << totalCompressedSize << " bytes\n";
    
    return ss.str();
}

// ----------------------------------------------------------------------------
// Private Helper Methods
// ----------------------------------------------------------------------------

uint256 DataAvailabilityLayer::ComputeTransactionsRoot(
    const std::vector<CTransaction>& transactions) const
{
    std::vector<uint256> hashes;
    hashes.reserve(transactions.size());
    
    for (const auto& tx : transactions) {
        hashes.push_back(tx.GetHash());
    }
    
    return ComputeMerkleRoot(hashes);
}

uint256 DataAvailabilityLayer::ComputeMerkleRoot(const std::vector<uint256>& hashes) const
{
    if (hashes.empty()) {
        return uint256();
    }
    
    if (hashes.size() == 1) {
        return hashes[0];
    }
    
    std::vector<uint256> level = hashes;
    
    while (level.size() > 1) {
        std::vector<uint256> nextLevel;
        
        for (size_t i = 0; i < level.size(); i += 2) {
            if (i + 1 < level.size()) {
                // Hash pair
                CHashWriter ss(SER_GETHASH, 0);
                ss << level[i] << level[i + 1];
                nextLevel.push_back(ss.GetHash());
            } else {
                // Odd element - hash with itself
                CHashWriter ss(SER_GETHASH, 0);
                ss << level[i] << level[i];
                nextLevel.push_back(ss.GetHash());
            }
        }
        
        level = std::move(nextLevel);
    }
    
    return level[0];
}

std::vector<uint256> DataAvailabilityLayer::GenerateMerkleProof(
    const std::vector<uint256>& hashes,
    size_t index) const
{
    std::vector<uint256> proof;
    
    if (hashes.empty() || index >= hashes.size()) {
        return proof;
    }
    
    std::vector<uint256> level = hashes;
    size_t idx = index;
    
    while (level.size() > 1) {
        // Add sibling to proof
        size_t siblingIdx = (idx % 2 == 0) ? idx + 1 : idx - 1;
        if (siblingIdx < level.size()) {
            proof.push_back(level[siblingIdx]);
        } else {
            proof.push_back(level[idx]); // Use self if no sibling
        }
        
        // Move to next level
        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < level.size(); i += 2) {
            if (i + 1 < level.size()) {
                CHashWriter ss(SER_GETHASH, 0);
                ss << level[i] << level[i + 1];
                nextLevel.push_back(ss.GetHash());
            } else {
                CHashWriter ss(SER_GETHASH, 0);
                ss << level[i] << level[i];
                nextLevel.push_back(ss.GetHash());
            }
        }
        
        level = std::move(nextLevel);
        idx /= 2;
    }
    
    return proof;
}

bool DataAvailabilityLayer::VerifyMerkleProof(
    const uint256& root,
    const uint256& leaf,
    const std::vector<uint256>& proof,
    size_t index)
{
    uint256 current = leaf;
    size_t idx = index;
    
    for (const auto& sibling : proof) {
        CHashWriter ss(SER_GETHASH, 0);
        if (idx % 2 == 0) {
            ss << current << sibling;
        } else {
            ss << sibling << current;
        }
        current = ss.GetHash();
        idx /= 2;
    }
    
    return current == root;
}

std::vector<unsigned char> DataAvailabilityLayer::PadToShardSize(
    const std::vector<unsigned char>& data,
    size_t targetSize) const
{
    std::vector<unsigned char> padded = data;
    if (padded.size() < targetSize) {
        padded.resize(targetSize, 0);
    }
    return padded;
}

std::vector<std::vector<unsigned char>> DataAvailabilityLayer::ComputeParityShards(
    const std::vector<std::vector<unsigned char>>& dataShards,
    uint32_t parityCount) const
{
    std::vector<std::vector<unsigned char>> parityShards;
    
    if (dataShards.empty() || parityCount == 0) {
        return parityShards;
    }
    
    size_t shardSize = dataShards[0].size();
    
    // Simple XOR parity for first parity shard
    // In production, use Reed-Solomon encoding
    for (uint32_t p = 0; p < parityCount; ++p) {
        std::vector<unsigned char> parity(shardSize, 0);
        
        for (const auto& dataShard : dataShards) {
            for (size_t i = 0; i < shardSize && i < dataShard.size(); ++i) {
                // For first parity: simple XOR
                // For additional parities: rotate and XOR (simplified)
                parity[i] ^= dataShard[(i + p) % dataShard.size()];
            }
        }
        
        parityShards.push_back(parity);
    }
    
    return parityShards;
}

} // namespace l2
