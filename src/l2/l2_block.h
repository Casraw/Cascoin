// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BLOCK_H
#define CASCOIN_L2_BLOCK_H

/**
 * @file l2_block.h
 * @brief L2 Block structure for Cascoin Layer 2
 * 
 * This file defines the L2Block structure that represents a block on the
 * L2 chain. It includes the block header with stateRoot and transactionsRoot,
 * sequencer signatures for consensus, and validation methods.
 * 
 * Requirements: 3.1, 3.5
 */

#include <l2/l2_common.h>
#include <l2/l2_transaction.h>
#include <uint256.h>
#include <key.h>
#include <pubkey.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

namespace l2 {

/**
 * @brief Sequencer signature for L2 block consensus
 * 
 * Each sequencer that votes ACCEPT on a block provides their signature.
 * A block requires 2/3+ sequencer signatures to be considered finalized.
 */
struct SequencerSignature {
    /** Address of the signing sequencer */
    uint160 sequencerAddress;
    
    /** Cryptographic signature over the block hash */
    std::vector<unsigned char> signature;
    
    /** Timestamp when signature was created */
    uint64_t timestamp = 0;

    SequencerSignature() = default;
    
    SequencerSignature(const uint160& addr, const std::vector<unsigned char>& sig, uint64_t ts)
        : sequencerAddress(addr), signature(sig), timestamp(ts) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(sequencerAddress);
        READWRITE(signature);
        READWRITE(timestamp);
    }

    /**
     * @brief Verify this signature against a block hash
     * @param blockHash The hash of the block
     * @param pubkey The public key of the sequencer
     * @return true if signature is valid
     */
    bool Verify(const uint256& blockHash, const CPubKey& pubkey) const {
        if (signature.empty()) {
            return false;
        }
        return pubkey.Verify(blockHash, signature);
    }

    bool operator==(const SequencerSignature& other) const {
        return sequencerAddress == other.sequencerAddress &&
               signature == other.signature &&
               timestamp == other.timestamp;
    }

    bool operator!=(const SequencerSignature& other) const {
        return !(*this == other);
    }
};

/**
 * @brief L2 Block Header
 * 
 * Contains all header fields for an L2 block, including state roots,
 * transaction roots, and L1 anchoring information.
 */
struct L2BlockHeader {
    /** L2 block number (height) */
    uint64_t blockNumber = 0;
    
    /** Hash of the parent block */
    uint256 parentHash;
    
    /** Merkle root of the L2 state after this block */
    uint256 stateRoot;
    
    /** Merkle root of all transactions in this block */
    uint256 transactionsRoot;
    
    /** Merkle root of transaction receipts */
    uint256 receiptsRoot;
    
    /** Address of the sequencer who produced this block */
    uint160 sequencer;
    
    /** Block timestamp (Unix time in seconds) */
    uint64_t timestamp = 0;
    
    /** Maximum gas allowed in this block */
    uint64_t gasLimit = 30000000;  // 30M gas default
    
    /** Total gas used by all transactions */
    uint64_t gasUsed = 0;
    
    /** L2 chain ID */
    uint64_t l2ChainId = DEFAULT_L2_CHAIN_ID;
    
    /** L1 block number this L2 block references */
    uint64_t l1AnchorBlock = 0;
    
    /** L1 block hash this L2 block references */
    uint256 l1AnchorHash;
    
    /** Slot number for sequencer rotation */
    uint64_t slotNumber = 0;
    
    /** Extra data (max 32 bytes) */
    std::vector<unsigned char> extraData;

    L2BlockHeader() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockNumber);
        READWRITE(parentHash);
        READWRITE(stateRoot);
        READWRITE(transactionsRoot);
        READWRITE(receiptsRoot);
        READWRITE(sequencer);
        READWRITE(timestamp);
        READWRITE(gasLimit);
        READWRITE(gasUsed);
        READWRITE(l2ChainId);
        READWRITE(l1AnchorBlock);
        READWRITE(l1AnchorHash);
        READWRITE(slotNumber);
        READWRITE(extraData);
    }

    /**
     * @brief Compute the hash of this block header
     * @return SHA256 hash of the serialized header
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << blockNumber;
        ss << parentHash;
        ss << stateRoot;
        ss << transactionsRoot;
        ss << receiptsRoot;
        ss << sequencer;
        ss << timestamp;
        ss << gasLimit;
        ss << gasUsed;
        ss << l2ChainId;
        ss << l1AnchorBlock;
        ss << l1AnchorHash;
        ss << slotNumber;
        ss << extraData;
        return ss.GetHash();
    }

    /**
     * @brief Check if this is the genesis block
     * @return true if block number is 0
     */
    bool IsGenesis() const {
        return blockNumber == 0;
    }

    bool operator==(const L2BlockHeader& other) const {
        return blockNumber == other.blockNumber &&
               parentHash == other.parentHash &&
               stateRoot == other.stateRoot &&
               transactionsRoot == other.transactionsRoot &&
               receiptsRoot == other.receiptsRoot &&
               sequencer == other.sequencer &&
               timestamp == other.timestamp &&
               gasLimit == other.gasLimit &&
               gasUsed == other.gasUsed &&
               l2ChainId == other.l2ChainId &&
               l1AnchorBlock == other.l1AnchorBlock &&
               l1AnchorHash == other.l1AnchorHash &&
               slotNumber == other.slotNumber &&
               extraData == other.extraData;
    }
};


/**
 * @brief Complete L2 Block structure
 * 
 * Contains the block header, transactions, L1 messages, and sequencer
 * signatures for consensus. This is the main block structure used
 * throughout the L2 system.
 * 
 * Requirements: 3.1, 3.5
 */
struct L2Block {
    /** Block header */
    L2BlockHeader header;
    
    /** Transactions included in this block */
    std::vector<L2Transaction> transactions;
    
    /** L1 to L2 messages processed in this block */
    std::vector<uint256> l1MessageHashes;
    
    /** Sequencer signatures (2/3+ required for finalization) */
    std::vector<SequencerSignature> signatures;
    
    /** Whether this block has been finalized (has 2/3+ signatures) */
    bool isFinalized = false;

    L2Block() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(transactions);
        READWRITE(l1MessageHashes);
        READWRITE(signatures);
        READWRITE(isFinalized);
    }

    /**
     * @brief Get the block hash (hash of header)
     * @return Block hash
     */
    uint256 GetHash() const {
        return header.GetHash();
    }

    /**
     * @brief Get the block number
     * @return Block number (height)
     */
    uint64_t GetBlockNumber() const {
        return header.blockNumber;
    }

    /**
     * @brief Get the state root
     * @return State root hash
     */
    const uint256& GetStateRoot() const {
        return header.stateRoot;
    }

    /**
     * @brief Get the transactions root
     * @return Transactions Merkle root
     */
    const uint256& GetTransactionsRoot() const {
        return header.transactionsRoot;
    }

    /**
     * @brief Get the sequencer address
     * @return Sequencer who produced this block
     */
    const uint160& GetSequencer() const {
        return header.sequencer;
    }

    /**
     * @brief Get the block timestamp
     * @return Unix timestamp in seconds
     */
    uint64_t GetTimestamp() const {
        return header.timestamp;
    }

    /**
     * @brief Get the number of transactions
     * @return Transaction count
     */
    size_t GetTransactionCount() const {
        return transactions.size();
    }

    /**
     * @brief Get the number of signatures
     * @return Signature count
     */
    size_t GetSignatureCount() const {
        return signatures.size();
    }

    /**
     * @brief Check if this is the genesis block
     * @return true if block number is 0
     */
    bool IsGenesis() const {
        return header.IsGenesis();
    }

    /**
     * @brief Compute the Merkle root of transactions
     * @return Merkle root of transaction hashes
     */
    uint256 ComputeTransactionsRoot() const;

    /**
     * @brief Validate the basic structure of this block
     * 
     * Checks:
     * - Block number consistency (non-genesis must have parent)
     * - Timestamp is reasonable (not too far in future)
     * - Gas used does not exceed gas limit
     * - Sequencer address is set
     * - Transactions root matches computed root
     * - Extra data size is within limits
     * 
     * @return true if structure is valid
     */
    bool ValidateStructure() const;

    /**
     * @brief Validate block header fields
     * @return true if header is valid
     */
    bool ValidateHeader() const;

    /**
     * @brief Validate all transactions in the block
     * @return true if all transactions are valid
     */
    bool ValidateTransactions() const;

    /**
     * @brief Validate sequencer signatures
     * @param pubkeys Map of sequencer address to public key
     * @return true if signatures are valid
     */
    bool ValidateSignatures(const std::map<uint160, CPubKey>& pubkeys) const;

    /**
     * @brief Add a sequencer signature to this block
     * @param sig The signature to add
     * @return true if signature was added (not duplicate)
     */
    bool AddSignature(const SequencerSignature& sig);

    /**
     * @brief Sign this block with a sequencer key
     * @param key The signing key
     * @param sequencerAddr The sequencer's address
     * @return true if signing succeeded
     */
    bool Sign(const CKey& key, const uint160& sequencerAddr);

    /**
     * @brief Check if a sequencer has signed this block
     * @param sequencerAddr The sequencer's address
     * @return true if sequencer has signed
     */
    bool HasSignature(const uint160& sequencerAddr) const;

    /**
     * @brief Calculate total gas used by all transactions
     * @return Total gas used
     */
    uint64_t CalculateTotalGasUsed() const;

    /**
     * @brief Serialize block to bytes
     * @return Serialized block data
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /**
     * @brief Deserialize block from bytes
     * @param data Serialized block data
     * @return true if deserialization succeeded
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
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

    /**
     * @brief Get a string representation for logging
     * @return String description of the block
     */
    std::string ToString() const;

    bool operator==(const L2Block& other) const {
        return header == other.header &&
               transactions == other.transactions &&
               l1MessageHashes == other.l1MessageHashes &&
               signatures == other.signatures &&
               isFinalized == other.isFinalized;
    }

    bool operator!=(const L2Block& other) const {
        return !(*this == other);
    }
};

/** Maximum extra data size in block header */
static constexpr size_t MAX_EXTRA_DATA_SIZE = 32;

/** Maximum timestamp drift into the future (seconds) */
static constexpr uint64_t MAX_FUTURE_TIMESTAMP = 60;

/** Maximum transactions per block */
static constexpr size_t MAX_TRANSACTIONS_PER_BLOCK = 10000;

/** Maximum signatures per block */
static constexpr size_t MAX_SIGNATURES_PER_BLOCK = 1000;

/**
 * @brief Create a genesis block for an L2 chain
 * @param chainId The L2 chain ID
 * @param timestamp Genesis timestamp
 * @param sequencer Initial sequencer address
 * @return Genesis block
 */
L2Block CreateGenesisBlock(uint64_t chainId, uint64_t timestamp, const uint160& sequencer);

/**
 * @brief Compute Merkle root from a list of hashes
 * @param hashes List of transaction hashes
 * @return Merkle root
 */
uint256 ComputeMerkleRoot(const std::vector<uint256>& hashes);

} // namespace l2

#endif // CASCOIN_L2_BLOCK_H
