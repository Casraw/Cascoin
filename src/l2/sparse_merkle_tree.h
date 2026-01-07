// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_SPARSE_MERKLE_TREE_H
#define CASCOIN_L2_SPARSE_MERKLE_TREE_H

/**
 * @file sparse_merkle_tree.h
 * @brief 256-bit Sparse Merkle Tree implementation for L2 state management
 * 
 * This file implements a Sparse Merkle Tree (SMT) optimized for the Cascoin L2
 * state representation. The SMT provides efficient proofs of inclusion and
 * exclusion for state elements, enabling fraud proof verification.
 * 
 * Key features:
 * - 256-bit key space (full address space coverage)
 * - Efficient storage using lazy evaluation of empty subtrees
 * - Inclusion and exclusion proof generation
 * - Proof verification for L1 fraud proof system
 * 
 * Requirements: 37.1, 37.2, 37.3, 37.4, 37.5, 3.1
 */

#include <uint256.h>
#include <hash.h>
#include <serialize.h>
#include <streams.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace l2 {

/** Maximum tree depth (256 bits for full address space) */
static constexpr uint32_t SMT_TREE_DEPTH = 256;

/** Maximum proof size in bytes (requirement 37.4: <10KB) */
static constexpr size_t SMT_MAX_PROOF_SIZE = 10 * 1024;

/**
 * @brief Merkle proof structure for state verification
 * 
 * Contains all information needed to verify that a key-value pair
 * exists (inclusion) or doesn't exist (exclusion) in the tree.
 */
struct MerkleProof {
    /** Sibling hashes along the path from leaf to root */
    std::vector<uint256> siblings;
    
    /** Direction at each level: 0=left, 1=right (stored as bytes for serialization) */
    std::vector<unsigned char> path;
    
    /** Hash of the leaf value (or empty hash for exclusion proofs) */
    uint256 leafHash;
    
    /** The key this proof is for */
    uint256 key;
    
    /** The value at this key (empty for exclusion proofs) */
    std::vector<unsigned char> value;
    
    /** Whether this is an inclusion (true) or exclusion (false) proof */
    bool isInclusion;

    MerkleProof() : isInclusion(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(siblings);
        READWRITE(path);
        READWRITE(leafHash);
        READWRITE(key);
        READWRITE(value);
        READWRITE(isInclusion);
    }

    /** Get the serialized size of this proof */
    size_t GetSerializedSize() const {
        return siblings.size() * 32 + path.size() + 32 + 32 + value.size() + 1;
    }

    /** Check if proof size is within limits */
    bool IsWithinSizeLimit() const {
        return GetSerializedSize() <= SMT_MAX_PROOF_SIZE;
    }

    /** Serialize proof to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize proof from bytes */
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
};

/**
 * @brief 256-bit Sparse Merkle Tree for L2 state management
 * 
 * A Sparse Merkle Tree is a Merkle tree where most leaves are empty.
 * Instead of storing all 2^256 leaves, we use lazy evaluation:
 * - Empty subtrees are represented by precomputed "default" hashes
 * - Only non-empty paths are stored
 * 
 * This enables efficient proofs of both inclusion AND exclusion,
 * which is critical for the fraud proof system.
 */
class SparseMerkleTree {
public:
    /** Tree depth constant */
    static constexpr uint32_t TREE_DEPTH = SMT_TREE_DEPTH;

    /**
     * @brief Construct an empty Sparse Merkle Tree
     */
    SparseMerkleTree();

    /**
     * @brief Get value at key
     * @param key The 256-bit key to look up
     * @return The value stored at key, or empty vector if not found
     */
    std::vector<unsigned char> Get(const uint256& key) const;

    /**
     * @brief Set value at key
     * @param key The 256-bit key
     * @param value The value to store
     */
    void Set(const uint256& key, const std::vector<unsigned char>& value);

    /**
     * @brief Delete key from tree
     * @param key The 256-bit key to delete
     * @return true if key existed and was deleted, false if key didn't exist
     */
    bool Delete(const uint256& key);

    /**
     * @brief Check if key exists in tree
     * @param key The 256-bit key to check
     * @return true if key exists with non-empty value
     */
    bool Exists(const uint256& key) const;

    /**
     * @brief Get the root hash of the tree
     * @return The 256-bit root hash
     */
    uint256 GetRoot() const;

    /**
     * @brief Generate an inclusion proof for a key
     * @param key The key to generate proof for
     * @return MerkleProof that can verify the key's value
     * 
     * If the key doesn't exist, generates an exclusion proof instead.
     */
    MerkleProof GenerateInclusionProof(const uint256& key) const;

    /**
     * @brief Generate an exclusion proof for a key
     * @param key The key to prove doesn't exist
     * @return MerkleProof that can verify the key doesn't exist
     * 
     * If the key exists, generates an inclusion proof instead.
     */
    MerkleProof GenerateExclusionProof(const uint256& key) const;

    /**
     * @brief Verify a Merkle proof against a root
     * @param proof The proof to verify
     * @param root The expected root hash
     * @param key The key the proof is for
     * @param value The expected value (empty for exclusion proofs)
     * @return true if proof is valid
     * 
     * This is a static method so it can be used for L1 verification
     * without needing the full tree.
     */
    static bool VerifyProof(
        const MerkleProof& proof,
        const uint256& root,
        const uint256& key,
        const std::vector<unsigned char>& value
    );

    /**
     * @brief Get the number of non-empty leaves in the tree
     * @return Count of stored key-value pairs
     */
    size_t Size() const;

    /**
     * @brief Check if tree is empty
     * @return true if no keys are stored
     */
    bool Empty() const;

    /**
     * @brief Clear all entries from the tree
     */
    void Clear();

    /**
     * @brief Get the default hash for an empty subtree at given depth
     * @param depth The depth (0 = leaf level, TREE_DEPTH = root level)
     * @return The default hash for empty subtree at this depth
     */
    static uint256 GetDefaultHash(uint32_t depth);

private:
    /**
     * @brief Internal node structure
     * 
     * Uses a map-based representation for efficiency with sparse data.
     * Key is the path prefix (bits from root to this node).
     */
    struct Node {
        uint256 hash;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        
        // For leaf nodes only
        bool isLeaf;
        uint256 leafKey;
        std::vector<unsigned char> leafValue;

        Node() : isLeaf(false) {}
    };

    /** Root node of the tree */
    std::unique_ptr<Node> root_;

    /** Cached default hashes for empty subtrees at each depth */
    static std::array<uint256, TREE_DEPTH + 1> defaultHashes_;
    
    /** Flag to track if default hashes have been initialized */
    static bool defaultHashesInitialized_;

    /** Storage for leaf values (key -> value mapping) */
    std::map<uint256, std::vector<unsigned char>> leaves_;

    /** Cached root hash (invalidated on modifications) */
    mutable uint256 cachedRoot_;
    mutable bool rootCacheValid_;

    /**
     * @brief Initialize the default hashes array
     * 
     * Computes H(empty) for leaf, then H(H_n, H_n) for each level up.
     */
    static void InitializeDefaultHashes();

    /**
     * @brief Hash two child nodes together
     * @param left Left child hash
     * @param right Right child hash
     * @return Parent hash
     */
    static uint256 HashNodes(const uint256& left, const uint256& right);

    /**
     * @brief Hash a leaf value
     * @param key The leaf key
     * @param value The leaf value
     * @return Leaf hash
     */
    static uint256 HashLeaf(const uint256& key, const std::vector<unsigned char>& value);

    /**
     * @brief Get bit at position in key (for path traversal)
     * @param key The 256-bit key
     * @param position Bit position (0 = MSB, 255 = LSB)
     * @return true if bit is 1, false if 0
     */
    static bool GetBit(const uint256& key, uint32_t position);

    /**
     * @brief Compute root hash from current leaves
     * @return The computed root hash
     */
    uint256 ComputeRoot() const;

    /**
     * @brief Optimized subtree hash computation for sparse trees
     * @param bitPosition Current bit position in the key (0 = MSB)
     * @return Hash of the subtree rooted at this position
     */
    uint256 ComputeSubtreeHashOptimized(uint32_t bitPosition) const;

    /**
     * @brief Recursively compute hash for a subtree
     * @param depth Current depth (0 = root, TREE_DEPTH = leaves)
     * @param prefix Path prefix to this subtree
     * @param prefixLen Length of prefix in bits
     * @return Hash of this subtree
     */
    uint256 ComputeSubtreeHash(uint32_t depth, const uint256& prefix, uint32_t prefixLen) const;

    /**
     * @brief Generate proof for a key (internal implementation)
     * @param key The key to generate proof for
     * @return MerkleProof structure
     */
    MerkleProof GenerateProof(const uint256& key) const;

    /**
     * @brief Compute the hash of a sibling subtree at a given depth
     * @param key The key we're generating a proof for
     * @param depth The depth level (0 = closest to leaves)
     * @return Hash of the sibling subtree
     */
    uint256 ComputeSiblingHash(const uint256& key, uint32_t depth) const;

    /**
     * @brief Compute subtree hash from a set of leaves
     * @param leaves The leaves in this subtree
     * @param startBit The starting bit position for partitioning
     * @param depth The depth of this subtree (0 = leaf level)
     * @return Hash of the subtree
     */
    uint256 ComputeSubtreeHashFromLeaves(
        const std::vector<std::pair<uint256, std::vector<unsigned char>>>& leaves,
        uint32_t startBit,
        uint32_t depth) const;

    /**
     * @brief Invalidate the cached root hash
     */
    void InvalidateCache();
};

} // namespace l2

#endif // CASCOIN_L2_SPARSE_MERKLE_TREE_H
