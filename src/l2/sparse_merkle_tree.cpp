// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file sparse_merkle_tree.cpp
 * @brief Implementation of 256-bit Sparse Merkle Tree for L2 state
 * 
 * Requirements: 37.1, 37.2, 37.3, 37.4, 37.5, 3.1
 */

#include <l2/sparse_merkle_tree.h>
#include <crypto/sha256.h>

#include <algorithm>
#include <cassert>

namespace l2 {

// Static member initialization
std::array<uint256, SparseMerkleTree::TREE_DEPTH + 1> SparseMerkleTree::defaultHashes_;
bool SparseMerkleTree::defaultHashesInitialized_ = false;

void SparseMerkleTree::InitializeDefaultHashes()
{
    if (defaultHashesInitialized_) {
        return;
    }

    // Level 0 (leaf level): hash of empty value
    // H(empty) = SHA256(SHA256(""))
    std::vector<unsigned char> empty;
    defaultHashes_[0] = HashLeaf(uint256(), empty);

    // Each level up: H(H_n, H_n)
    for (uint32_t i = 1; i <= TREE_DEPTH; ++i) {
        defaultHashes_[i] = HashNodes(defaultHashes_[i - 1], defaultHashes_[i - 1]);
    }

    defaultHashesInitialized_ = true;
}

uint256 SparseMerkleTree::GetDefaultHash(uint32_t depth)
{
    InitializeDefaultHashes();
    if (depth > TREE_DEPTH) {
        depth = TREE_DEPTH;
    }
    return defaultHashes_[depth];
}

uint256 SparseMerkleTree::HashNodes(const uint256& left, const uint256& right)
{
    // H(left || right) using double SHA256 (Bitcoin-style)
    unsigned char data[64];
    memcpy(data, left.begin(), 32);
    memcpy(data + 32, right.begin(), 32);
    
    uint256 result;
    CHash256().Write(data, 64).Finalize(result.begin());
    return result;
}

uint256 SparseMerkleTree::HashLeaf(const uint256& key, const std::vector<unsigned char>& value)
{
    // H(0x00 || key || value) - prefix 0x00 distinguishes leaf from internal node
    CHash256 hasher;
    unsigned char prefix = 0x00;
    hasher.Write(&prefix, 1);
    hasher.Write(key.begin(), 32);
    if (!value.empty()) {
        hasher.Write(value.data(), value.size());
    }
    
    uint256 result;
    hasher.Finalize(result.begin());
    return result;
}

bool SparseMerkleTree::GetBit(const uint256& key, uint32_t position)
{
    if (position >= 256) {
        return false;
    }
    // Position 0 is MSB (most significant bit)
    // key.begin() points to the least significant byte
    // So we need to reverse the byte order for bit extraction
    uint32_t byteIndex = position / 8;
    uint32_t bitIndex = 7 - (position % 8);
    
    // Access from the end (MSB first)
    const unsigned char* data = key.begin();
    unsigned char byte = data[31 - byteIndex];
    
    return (byte >> bitIndex) & 1;
}

SparseMerkleTree::SparseMerkleTree()
    : rootCacheValid_(false)
{
    InitializeDefaultHashes();
}

std::vector<unsigned char> SparseMerkleTree::Get(const uint256& key) const
{
    auto it = leaves_.find(key);
    if (it != leaves_.end()) {
        return it->second;
    }
    return std::vector<unsigned char>();
}

void SparseMerkleTree::Set(const uint256& key, const std::vector<unsigned char>& value)
{
    if (value.empty()) {
        Delete(key);
        return;
    }
    
    leaves_[key] = value;
    InvalidateCache();
}

bool SparseMerkleTree::Delete(const uint256& key)
{
    auto it = leaves_.find(key);
    if (it != leaves_.end()) {
        leaves_.erase(it);
        InvalidateCache();
        return true;
    }
    return false;
}

bool SparseMerkleTree::Exists(const uint256& key) const
{
    return leaves_.find(key) != leaves_.end();
}

uint256 SparseMerkleTree::GetRoot() const
{
    if (rootCacheValid_) {
        return cachedRoot_;
    }
    
    cachedRoot_ = ComputeRoot();
    rootCacheValid_ = true;
    return cachedRoot_;
}

size_t SparseMerkleTree::Size() const
{
    return leaves_.size();
}

bool SparseMerkleTree::Empty() const
{
    return leaves_.empty();
}

void SparseMerkleTree::Clear()
{
    leaves_.clear();
    InvalidateCache();
}

void SparseMerkleTree::InvalidateCache()
{
    rootCacheValid_ = false;
}

uint256 SparseMerkleTree::ComputeRoot() const
{
    if (leaves_.empty()) {
        return GetDefaultHash(TREE_DEPTH);
    }

    // Efficient sparse tree root computation using path-based hashing
    // For each leaf, we compute its hash and then hash up to the root
    // combining with default hashes for empty siblings
    
    // For a single leaf, this is O(256) - just hash up the path
    // For multiple leaves, we need to handle shared paths
    
    if (leaves_.size() == 1) {
        // Single leaf optimization - just hash up the path
        const auto& leaf = *leaves_.begin();
        uint256 currentHash = HashLeaf(leaf.first, leaf.second);
        
        for (uint32_t level = 0; level < TREE_DEPTH; ++level) {
            bool isRight = GetBit(leaf.first, TREE_DEPTH - 1 - level);
            if (isRight) {
                currentHash = HashNodes(GetDefaultHash(level), currentHash);
            } else {
                currentHash = HashNodes(currentHash, GetDefaultHash(level));
            }
        }
        return currentHash;
    }

    // Multiple leaves - use recursive subtree computation
    // This is still efficient for sparse trees because we only recurse
    // into subtrees that contain actual leaves
    return ComputeSubtreeHashOptimized(0);
}

uint256 SparseMerkleTree::ComputeSubtreeHashOptimized(uint32_t bitPosition) const
{
    if (bitPosition >= TREE_DEPTH) {
        // Should not happen, but handle gracefully
        return GetDefaultHash(0);
    }

    // Partition leaves into left (bit=0) and right (bit=1) subtrees
    std::vector<std::pair<uint256, std::vector<unsigned char>>> leftLeaves, rightLeaves;
    
    for (const auto& leaf : leaves_) {
        if (GetBit(leaf.first, bitPosition)) {
            rightLeaves.push_back(leaf);
        } else {
            leftLeaves.push_back(leaf);
        }
    }

    uint256 leftHash, rightHash;
    uint32_t remainingDepth = TREE_DEPTH - bitPosition - 1;

    // Compute left subtree hash
    if (leftLeaves.empty()) {
        leftHash = GetDefaultHash(remainingDepth);
    } else if (leftLeaves.size() == 1 && bitPosition == TREE_DEPTH - 1) {
        // Leaf level
        leftHash = HashLeaf(leftLeaves[0].first, leftLeaves[0].second);
    } else if (leftLeaves.size() == 1) {
        // Single leaf in subtree - hash up from leaf
        const auto& leaf = leftLeaves[0];
        uint256 h = HashLeaf(leaf.first, leaf.second);
        for (uint32_t level = 0; level < remainingDepth; ++level) {
            bool isRight = GetBit(leaf.first, TREE_DEPTH - 1 - level);
            if (isRight) {
                h = HashNodes(GetDefaultHash(level), h);
            } else {
                h = HashNodes(h, GetDefaultHash(level));
            }
        }
        leftHash = h;
    } else {
        // Multiple leaves - recurse (create temporary tree for recursion)
        SparseMerkleTree leftTree;
        for (const auto& leaf : leftLeaves) {
            leftTree.leaves_[leaf.first] = leaf.second;
        }
        leftHash = leftTree.ComputeSubtreeHashOptimized(bitPosition + 1);
    }

    // Compute right subtree hash
    if (rightLeaves.empty()) {
        rightHash = GetDefaultHash(remainingDepth);
    } else if (rightLeaves.size() == 1 && bitPosition == TREE_DEPTH - 1) {
        // Leaf level
        rightHash = HashLeaf(rightLeaves[0].first, rightLeaves[0].second);
    } else if (rightLeaves.size() == 1) {
        // Single leaf in subtree - hash up from leaf
        const auto& leaf = rightLeaves[0];
        uint256 h = HashLeaf(leaf.first, leaf.second);
        for (uint32_t level = 0; level < remainingDepth; ++level) {
            bool isRight = GetBit(leaf.first, TREE_DEPTH - 1 - level);
            if (isRight) {
                h = HashNodes(GetDefaultHash(level), h);
            } else {
                h = HashNodes(h, GetDefaultHash(level));
            }
        }
        rightHash = h;
    } else {
        // Multiple leaves - recurse
        SparseMerkleTree rightTree;
        for (const auto& leaf : rightLeaves) {
            rightTree.leaves_[leaf.first] = leaf.second;
        }
        rightHash = rightTree.ComputeSubtreeHashOptimized(bitPosition + 1);
    }

    return HashNodes(leftHash, rightHash);
}

uint256 SparseMerkleTree::ComputeSubtreeHash(uint32_t depth, const uint256& prefix, uint32_t prefixLen) const
{
    if (depth == 0) {
        // Leaf level - check if this exact key exists
        auto it = leaves_.find(prefix);
        if (it != leaves_.end()) {
            return HashLeaf(prefix, it->second);
        }
        return GetDefaultHash(0);
    }

    // Check if any leaves exist in this subtree
    bool hasLeaves = false;
    for (const auto& leaf : leaves_) {
        // Check if leaf key starts with our prefix
        bool matches = true;
        for (uint32_t i = 0; i < prefixLen && matches; ++i) {
            if (GetBit(leaf.first, i) != GetBit(prefix, i)) {
                matches = false;
            }
        }
        if (matches) {
            hasLeaves = true;
            break;
        }
    }

    if (!hasLeaves) {
        return GetDefaultHash(depth);
    }

    // Recursively compute left and right subtrees
    uint256 leftPrefix = prefix;
    
    // Set the next bit for right subtree
    // This is complex with uint256, so we use a simpler approach
    // by checking each leaf individually
    
    uint256 leftHash = ComputeSubtreeHash(depth - 1, leftPrefix, prefixLen + 1);
    
    // For right prefix, we need to set bit at position prefixLen
    // Since uint256 doesn't have easy bit manipulation, we track this differently
    uint256 rightHash = GetDefaultHash(depth - 1);
    
    // Check for leaves in right subtree
    for (const auto& leaf : leaves_) {
        bool inRightSubtree = true;
        for (uint32_t i = 0; i < prefixLen && inRightSubtree; ++i) {
            if (GetBit(leaf.first, i) != GetBit(prefix, i)) {
                inRightSubtree = false;
            }
        }
        if (inRightSubtree && GetBit(leaf.first, prefixLen)) {
            // This leaf is in the right subtree
            rightHash = ComputeSubtreeHash(depth - 1, leaf.first, prefixLen + 1);
            break;
        }
    }

    return HashNodes(leftHash, rightHash);
}

MerkleProof SparseMerkleTree::GenerateInclusionProof(const uint256& key) const
{
    MerkleProof proof = GenerateProof(key);
    // For inclusion proofs, verify the key actually exists
    if (!Exists(key)) {
        // Key doesn't exist, but we still return a valid proof structure
        // that proves the key is NOT in the tree (exclusion proof)
        proof.isInclusion = false;
    }
    return proof;
}

MerkleProof SparseMerkleTree::GenerateExclusionProof(const uint256& key) const
{
    MerkleProof proof = GenerateProof(key);
    // For exclusion proofs, verify the key doesn't exist
    if (Exists(key)) {
        // Key exists, so this becomes an inclusion proof
        proof.isInclusion = true;
    } else {
        proof.isInclusion = false;
    }
    return proof;
}

MerkleProof SparseMerkleTree::GenerateProof(const uint256& key) const
{
    MerkleProof proof;
    proof.key = key;
    proof.path.resize(TREE_DEPTH);
    proof.siblings.resize(TREE_DEPTH);

    // Check if key exists
    auto it = leaves_.find(key);
    if (it != leaves_.end()) {
        proof.isInclusion = true;
        proof.value = it->second;
        proof.leafHash = HashLeaf(key, it->second);
    } else {
        proof.isInclusion = false;
        proof.value.clear();
        proof.leafHash = GetDefaultHash(0);
    }

    // Efficient sibling computation: partition leaves at each level
    // and compute sibling subtree hashes only for non-empty subtrees
    
    // Start with all leaves except the target key
    std::vector<std::pair<uint256, std::vector<unsigned char>>> otherLeaves;
    for (const auto& leaf : leaves_) {
        if (leaf.first != key) {
            otherLeaves.push_back(leaf);
        }
    }

    // Process each level from root (bit 0) to leaf (bit 255)
    for (uint32_t bitPos = 0; bitPos < TREE_DEPTH; ++bitPos) {
        bool goRight = GetBit(key, bitPos);
        proof.path[TREE_DEPTH - 1 - bitPos] = goRight ? 1 : 0;

        // Partition other leaves into same-side and sibling-side
        std::vector<std::pair<uint256, std::vector<unsigned char>>> sameSide, siblingLeaves;
        
        for (const auto& leaf : otherLeaves) {
            bool leafBit = GetBit(leaf.first, bitPos);
            if (leafBit == goRight) {
                sameSide.push_back(leaf);
            } else {
                siblingLeaves.push_back(leaf);
            }
        }

        // Compute sibling subtree hash
        uint32_t siblingDepth = TREE_DEPTH - 1 - bitPos;
        proof.siblings[TREE_DEPTH - 1 - bitPos] = ComputeSubtreeHashFromLeaves(siblingLeaves, bitPos + 1, siblingDepth);

        // Continue with leaves on same side as target
        otherLeaves = std::move(sameSide);
    }

    return proof;
}

uint256 SparseMerkleTree::ComputeSubtreeHashFromLeaves(
    const std::vector<std::pair<uint256, std::vector<unsigned char>>>& leaves,
    uint32_t startBit,
    uint32_t depth) const
{
    if (leaves.empty()) {
        return GetDefaultHash(depth);
    }

    if (depth == 0) {
        // Leaf level - should have exactly one leaf
        if (leaves.size() == 1) {
            return HashLeaf(leaves[0].first, leaves[0].second);
        }
        // Multiple leaves at same position - shouldn't happen
        return GetDefaultHash(0);
    }

    if (leaves.size() == 1) {
        // Single leaf - hash up from leaf level
        const auto& leaf = leaves[0];
        uint256 h = HashLeaf(leaf.first, leaf.second);
        
        // Hash up from leaf to this subtree root
        for (uint32_t d = 0; d < depth; ++d) {
            uint32_t bitPos = TREE_DEPTH - 1 - d;
            bool isRight = GetBit(leaf.first, bitPos);
            if (isRight) {
                h = HashNodes(GetDefaultHash(d), h);
            } else {
                h = HashNodes(h, GetDefaultHash(d));
            }
        }
        return h;
    }

    // Multiple leaves - partition and recurse
    std::vector<std::pair<uint256, std::vector<unsigned char>>> leftLeaves, rightLeaves;
    
    for (const auto& leaf : leaves) {
        if (GetBit(leaf.first, startBit)) {
            rightLeaves.push_back(leaf);
        } else {
            leftLeaves.push_back(leaf);
        }
    }

    uint256 leftHash = ComputeSubtreeHashFromLeaves(leftLeaves, startBit + 1, depth - 1);
    uint256 rightHash = ComputeSubtreeHashFromLeaves(rightLeaves, startBit + 1, depth - 1);

    return HashNodes(leftHash, rightHash);
}

bool SparseMerkleTree::VerifyProof(
    const MerkleProof& proof,
    const uint256& root,
    const uint256& key,
    const std::vector<unsigned char>& value)
{
    // Verify the proof matches the expected key
    if (proof.key != key) {
        return false;
    }

    // For inclusion proofs, verify value matches
    if (proof.isInclusion && proof.value != value) {
        return false;
    }

    // For exclusion proofs, value should be empty
    if (!proof.isInclusion && !value.empty()) {
        return false;
    }

    // Verify proof structure
    if (proof.path.size() != TREE_DEPTH || proof.siblings.size() != TREE_DEPTH) {
        return false;
    }

    // Compute the root from the proof
    uint256 currentHash;
    if (proof.isInclusion) {
        currentHash = HashLeaf(key, value);
    } else {
        currentHash = GetDefaultHash(0);
    }

    // Verify leaf hash matches
    if (currentHash != proof.leafHash) {
        return false;
    }

    // Walk up the tree, combining with siblings
    for (uint32_t depth = 0; depth < TREE_DEPTH; ++depth) {
        uint256 left, right;
        
        if (proof.path[depth] != 0) {
            // We went right, so sibling is on left
            left = proof.siblings[depth];
            right = currentHash;
        } else {
            // We went left, so sibling is on right
            left = currentHash;
            right = proof.siblings[depth];
        }
        
        currentHash = HashNodes(left, right);
    }

    // Final hash should match the root
    return currentHash == root;
}

} // namespace l2
