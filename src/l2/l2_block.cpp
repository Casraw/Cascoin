// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_block.cpp
 * @brief Implementation of L2 Block structure
 * 
 * Requirements: 3.1, 3.5
 */

#include <l2/l2_block.h>
#include <tinyformat.h>

#include <algorithm>
#include <chrono>

namespace l2 {

uint256 L2Block::ComputeTransactionsRoot() const {
    if (transactions.empty()) {
        return uint256();
    }
    
    std::vector<uint256> hashes;
    hashes.reserve(transactions.size());
    for (const auto& tx : transactions) {
        hashes.push_back(tx.GetHash());
    }
    
    return ComputeMerkleRoot(hashes);
}

bool L2Block::ValidateStructure() const {
    // Validate header first
    if (!ValidateHeader()) {
        return false;
    }
    
    // Validate transactions
    if (!ValidateTransactions()) {
        return false;
    }
    
    // Verify transactions root matches
    uint256 computedRoot = ComputeTransactionsRoot();
    if (computedRoot != header.transactionsRoot) {
        return false;
    }
    
    // Check signature count limit
    if (signatures.size() > MAX_SIGNATURES_PER_BLOCK) {
        return false;
    }
    
    return true;
}

bool L2Block::ValidateHeader() const {
    // Genesis block special case
    if (header.blockNumber == 0) {
        // Genesis must have null parent hash
        if (!header.parentHash.IsNull()) {
            return false;
        }
    } else {
        // Non-genesis must have parent hash
        if (header.parentHash.IsNull()) {
            return false;
        }
    }
    
    // Timestamp validation - not too far in future
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (header.timestamp > static_cast<uint64_t>(now) + MAX_FUTURE_TIMESTAMP) {
        return false;
    }
    
    // Gas used cannot exceed gas limit
    if (header.gasUsed > header.gasLimit) {
        return false;
    }
    
    // Sequencer address must be set (except for genesis)
    if (header.blockNumber > 0 && header.sequencer.IsNull()) {
        return false;
    }
    
    // Extra data size limit
    if (header.extraData.size() > MAX_EXTRA_DATA_SIZE) {
        return false;
    }
    
    // Gas limit must be positive
    if (header.gasLimit == 0) {
        return false;
    }
    
    return true;
}

bool L2Block::ValidateTransactions() const {
    // Check transaction count limit
    if (transactions.size() > MAX_TRANSACTIONS_PER_BLOCK) {
        return false;
    }
    
    // Validate each transaction
    uint64_t totalGas = 0;
    for (const auto& tx : transactions) {
        if (!tx.ValidateStructure()) {
            return false;
        }
        totalGas += tx.gasLimit;
    }
    
    // Total gas cannot exceed block gas limit
    if (totalGas > header.gasLimit) {
        return false;
    }
    
    return true;
}

bool L2Block::ValidateSignatures(const std::map<uint160, CPubKey>& pubkeys) const {
    uint256 blockHash = GetHash();
    
    for (const auto& sig : signatures) {
        auto it = pubkeys.find(sig.sequencerAddress);
        if (it == pubkeys.end()) {
            // Unknown sequencer
            return false;
        }
        
        if (!sig.Verify(blockHash, it->second)) {
            return false;
        }
    }
    
    return true;
}

bool L2Block::AddSignature(const SequencerSignature& sig) {
    // Check if already signed by this sequencer
    if (HasSignature(sig.sequencerAddress)) {
        return false;
    }
    
    // Check signature limit
    if (signatures.size() >= MAX_SIGNATURES_PER_BLOCK) {
        return false;
    }
    
    signatures.push_back(sig);
    return true;
}

bool L2Block::Sign(const CKey& key, const uint160& sequencerAddr) {
    // Check if already signed
    if (HasSignature(sequencerAddr)) {
        return false;
    }
    
    uint256 blockHash = GetHash();
    std::vector<unsigned char> sig;
    
    if (!key.Sign(blockHash, sig)) {
        return false;
    }
    
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return AddSignature(SequencerSignature(sequencerAddr, sig, now));
}

bool L2Block::HasSignature(const uint160& sequencerAddr) const {
    return std::any_of(signatures.begin(), signatures.end(),
        [&sequencerAddr](const SequencerSignature& sig) {
            return sig.sequencerAddress == sequencerAddr;
        });
}

uint64_t L2Block::CalculateTotalGasUsed() const {
    uint64_t total = 0;
    for (const auto& tx : transactions) {
        total += tx.gasUsed;
    }
    return total;
}

std::string L2Block::ToString() const {
    return strprintf(
        "L2Block(number=%u, hash=%s, parent=%s, stateRoot=%s, txRoot=%s, "
        "sequencer=%s, timestamp=%u, gasUsed=%u/%u, txCount=%u, sigCount=%u, finalized=%s)",
        header.blockNumber,
        GetHash().ToString().substr(0, 16),
        header.parentHash.ToString().substr(0, 16),
        header.stateRoot.ToString().substr(0, 16),
        header.transactionsRoot.ToString().substr(0, 16),
        header.sequencer.ToString().substr(0, 16),
        header.timestamp,
        header.gasUsed,
        header.gasLimit,
        transactions.size(),
        signatures.size(),
        isFinalized ? "true" : "false"
    );
}

L2Block CreateGenesisBlock(uint64_t chainId, uint64_t timestamp, const uint160& sequencer) {
    L2Block genesis;
    
    genesis.header.blockNumber = 0;
    genesis.header.parentHash.SetNull();
    genesis.header.stateRoot.SetNull();  // Empty state
    genesis.header.transactionsRoot.SetNull();  // No transactions
    genesis.header.receiptsRoot.SetNull();
    genesis.header.sequencer = sequencer;
    genesis.header.timestamp = timestamp;
    genesis.header.gasLimit = 30000000;  // 30M gas
    genesis.header.gasUsed = 0;
    genesis.header.l2ChainId = chainId;
    genesis.header.l1AnchorBlock = 0;
    genesis.header.l1AnchorHash.SetNull();
    genesis.header.slotNumber = 0;
    genesis.isFinalized = true;  // Genesis is always finalized
    
    return genesis;
}

uint256 ComputeMerkleRoot(const std::vector<uint256>& hashes) {
    if (hashes.empty()) {
        return uint256();
    }
    
    if (hashes.size() == 1) {
        return hashes[0];
    }
    
    std::vector<uint256> level = hashes;
    
    while (level.size() > 1) {
        std::vector<uint256> nextLevel;
        nextLevel.reserve((level.size() + 1) / 2);
        
        for (size_t i = 0; i < level.size(); i += 2) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << level[i];
            
            if (i + 1 < level.size()) {
                ss << level[i + 1];
            } else {
                // Odd number of elements - duplicate last
                ss << level[i];
            }
            
            nextLevel.push_back(ss.GetHash());
        }
        
        level = std::move(nextLevel);
    }
    
    return level[0];
}

} // namespace l2
