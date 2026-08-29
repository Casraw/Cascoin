// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_TOKEN_H
#define CASCOIN_L2_TOKEN_H

/**
 * @file l2_token.h
 * @brief L2 Independent Token Model structures
 * 
 * This file defines the data structures for the L2 independent token model.
 * Each L2 chain has its own native token that is independent from L1-CAS.
 * 
 * Key components:
 * - L2TokenConfig: Token configuration (name, symbol, rewards, fees)
 * - L2TokenSupply: Supply tracking with invariant verification
 * - MintingRecord: Audit trail for sequencer reward minting
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4, 2.2, 3.1, 3.2, 3.7, 8.5
 */

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>

#include <cstdint>
#include <string>
#include <map>
#include <vector>

namespace l2 {

// ============================================================================
// Constants
// ============================================================================

/** Minimum token name length */
static constexpr size_t MIN_TOKEN_NAME_LENGTH = 3;

/** Maximum token name length */
static constexpr size_t MAX_TOKEN_NAME_LENGTH = 32;

/** Minimum token symbol length */
static constexpr size_t MIN_TOKEN_SYMBOL_LENGTH = 2;

/** Maximum token symbol length */
static constexpr size_t MAX_TOKEN_SYMBOL_LENGTH = 8;

/** Default sequencer reward per block (10 tokens) */
static constexpr CAmount DEFAULT_SEQUENCER_REWARD = 10 * COIN;

/** Default minting fee in CAS on L1 (0.01 CAS) */
static constexpr CAmount DEFAULT_MINTING_FEE = COIN / 100;

/** Default maximum genesis supply (1,000,000 tokens) */
static constexpr CAmount DEFAULT_MAX_GENESIS_SUPPLY = 1000000 * COIN;

/** Default minimum transfer fee (0.0001 tokens) */
static constexpr CAmount DEFAULT_MIN_TRANSFER_FEE = COIN / 10000;

// ============================================================================
// L2TokenConfig
// ============================================================================

/**
 * @brief Token configuration for an L2 chain
 * 
 * Stores the token identity (name, symbol) and economic parameters
 * (sequencer rewards, minting fees, transfer fees).
 * 
 * Requirements: 1.1, 1.2, 1.3, 1.4
 */
struct L2TokenConfig {
    /** Token name (e.g., "CasLayer", "FastCoin") - 3-32 characters */
    std::string tokenName;
    
    /** Token symbol (e.g., "CLAY", "FAST") - 2-8 characters */
    std::string tokenSymbol;
    
    /** Tokens minted per block as sequencer reward */
    CAmount sequencerReward;
    
    /** CAS fee required on L1 to mint L2 tokens */
    CAmount mintingFee;
    
    /** Maximum tokens that can be distributed at genesis */
    CAmount maxGenesisSupply;
    
    /** Minimum fee required per transfer */
    CAmount minTransferFee;

    /** Default constructor with default values */
    L2TokenConfig()
        : tokenName("L2Token")
        , tokenSymbol("L2T")
        , sequencerReward(DEFAULT_SEQUENCER_REWARD)
        , mintingFee(DEFAULT_MINTING_FEE)
        , maxGenesisSupply(DEFAULT_MAX_GENESIS_SUPPLY)
        , minTransferFee(DEFAULT_MIN_TRANSFER_FEE)
    {}

    /** Constructor with custom name and symbol */
    L2TokenConfig(const std::string& name, const std::string& symbol)
        : tokenName(name)
        , tokenSymbol(symbol)
        , sequencerReward(DEFAULT_SEQUENCER_REWARD)
        , mintingFee(DEFAULT_MINTING_FEE)
        , maxGenesisSupply(DEFAULT_MAX_GENESIS_SUPPLY)
        , minTransferFee(DEFAULT_MIN_TRANSFER_FEE)
    {}

    /** Full constructor */
    L2TokenConfig(const std::string& name, const std::string& symbol,
                  CAmount reward, CAmount fee, CAmount maxGenesis, CAmount minFee)
        : tokenName(name)
        , tokenSymbol(symbol)
        , sequencerReward(reward)
        , mintingFee(fee)
        , maxGenesisSupply(maxGenesis)
        , minTransferFee(minFee)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tokenName);
        READWRITE(tokenSymbol);
        READWRITE(sequencerReward);
        READWRITE(mintingFee);
        READWRITE(maxGenesisSupply);
        READWRITE(minTransferFee);
    }

    /**
     * @brief Validate the entire token configuration
     * @return true if all fields are valid
     */
    bool IsValid() const {
        return ValidateTokenName(tokenName) &&
               ValidateTokenSymbol(tokenSymbol) &&
               sequencerReward >= 0 &&
               mintingFee >= 0 &&
               maxGenesisSupply >= 0 &&
               minTransferFee >= 0;
    }

    /**
     * @brief Validate a token name
     * @param name The token name to validate
     * @return true if name is between 3 and 32 characters
     * 
     * Requirement 1.3: Token name must be 3-32 characters
     */
    static bool ValidateTokenName(const std::string& name) {
        return name.length() >= MIN_TOKEN_NAME_LENGTH &&
               name.length() <= MAX_TOKEN_NAME_LENGTH;
    }

    /**
     * @brief Validate a token symbol
     * @param symbol The token symbol to validate
     * @return true if symbol is between 2 and 8 characters
     * 
     * Requirement 1.4: Token symbol must be 2-8 characters
     */
    static bool ValidateTokenSymbol(const std::string& symbol) {
        return symbol.length() >= MIN_TOKEN_SYMBOL_LENGTH &&
               symbol.length() <= MAX_TOKEN_SYMBOL_LENGTH;
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool operator==(const L2TokenConfig& other) const {
        return tokenName == other.tokenName &&
               tokenSymbol == other.tokenSymbol &&
               sequencerReward == other.sequencerReward &&
               mintingFee == other.mintingFee &&
               maxGenesisSupply == other.maxGenesisSupply &&
               minTransferFee == other.minTransferFee;
    }

    bool operator!=(const L2TokenConfig& other) const {
        return !(*this == other);
    }
};


// ============================================================================
// L2TokenSupply
// ============================================================================

/**
 * @brief Token supply tracking for an L2 chain
 * 
 * Tracks the total supply and its components to ensure the supply
 * invariant is maintained: totalSupply == genesisSupply + mintedSupply - burnedSupply
 * 
 * Requirements: 2.2, 8.5
 */
struct L2TokenSupply {
    /** Total supply (sum of all balances) */
    CAmount totalSupply;
    
    /** Tokens distributed at genesis */
    CAmount genesisSupply;
    
    /** Tokens minted through sequencer rewards */
    CAmount mintedSupply;
    
    /** Tokens burned (fees, etc.) */
    CAmount burnedSupply;
    
    /** Number of blocks that received sequencer rewards */
    uint64_t totalBlocksRewarded;

    /** Default constructor */
    L2TokenSupply()
        : totalSupply(0)
        , genesisSupply(0)
        , mintedSupply(0)
        , burnedSupply(0)
        , totalBlocksRewarded(0)
    {}

    /** Constructor with initial values */
    L2TokenSupply(CAmount total, CAmount genesis, CAmount minted, 
                  CAmount burned, uint64_t blocksRewarded)
        : totalSupply(total)
        , genesisSupply(genesis)
        , mintedSupply(minted)
        , burnedSupply(burned)
        , totalBlocksRewarded(blocksRewarded)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(totalSupply);
        READWRITE(genesisSupply);
        READWRITE(mintedSupply);
        READWRITE(burnedSupply);
        READWRITE(totalBlocksRewarded);
    }

    /**
     * @brief Verify the supply invariant
     * @return true if totalSupply == genesisSupply + mintedSupply - burnedSupply
     * 
     * Requirement 8.5: Sum of all balances SHALL equal total supply
     */
    bool VerifyInvariant() const {
        return totalSupply == (genesisSupply + mintedSupply - burnedSupply);
    }

    /**
     * @brief Calculate expected total supply from components
     * @return Expected total supply based on genesis, minted, and burned
     */
    CAmount CalculateExpectedTotal() const {
        return genesisSupply + mintedSupply - burnedSupply;
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool operator==(const L2TokenSupply& other) const {
        return totalSupply == other.totalSupply &&
               genesisSupply == other.genesisSupply &&
               mintedSupply == other.mintedSupply &&
               burnedSupply == other.burnedSupply &&
               totalBlocksRewarded == other.totalBlocksRewarded;
    }

    bool operator!=(const L2TokenSupply& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// MintingRecord
// ============================================================================

/**
 * @brief Record of a token minting event
 * 
 * Each time a sequencer receives a block reward, a MintingRecord is created
 * to provide an audit trail. The record links the L2 block to the L1 fee
 * transaction that authorized the minting.
 * 
 * Requirements: 3.1, 3.2, 3.7
 */
struct MintingRecord {
    /** Hash of the L2 block that triggered minting */
    uint256 l2BlockHash;
    
    /** L2 block number */
    uint64_t l2BlockNumber;
    
    /** Address of the sequencer who received the reward */
    uint160 sequencerAddress;
    
    /** Amount of tokens minted */
    CAmount rewardAmount;
    
    /** L1 transaction hash that paid the minting fee */
    uint256 l1TxHash;
    
    /** L1 block number containing the fee transaction */
    uint64_t l1BlockNumber;
    
    /** CAS fee paid on L1 */
    CAmount feePaid;
    
    /** Timestamp when minting occurred */
    uint64_t timestamp;

    /** Default constructor */
    MintingRecord()
        : l2BlockNumber(0)
        , rewardAmount(0)
        , l1BlockNumber(0)
        , feePaid(0)
        , timestamp(0)
    {}

    /** Full constructor */
    MintingRecord(const uint256& l2Hash, uint64_t l2Block, const uint160& sequencer,
                  CAmount reward, const uint256& l1Hash, uint64_t l1Block,
                  CAmount fee, uint64_t ts)
        : l2BlockHash(l2Hash)
        , l2BlockNumber(l2Block)
        , sequencerAddress(sequencer)
        , rewardAmount(reward)
        , l1TxHash(l1Hash)
        , l1BlockNumber(l1Block)
        , feePaid(fee)
        , timestamp(ts)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l2BlockHash);
        READWRITE(l2BlockNumber);
        READWRITE(sequencerAddress);
        READWRITE(rewardAmount);
        READWRITE(l1TxHash);
        READWRITE(l1BlockNumber);
        READWRITE(feePaid);
        READWRITE(timestamp);
    }

    /**
     * @brief Compute unique hash for this minting record
     * @return SHA256 hash of the record
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << l2BlockHash;
        ss << l2BlockNumber;
        ss << sequencerAddress;
        ss << rewardAmount;
        ss << l1TxHash;
        ss << l1BlockNumber;
        ss << feePaid;
        ss << timestamp;
        return ss.GetHash();
    }

    /** Serialize to bytes */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /** Deserialize from bytes */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) return false;
        try {
            CDataStream ss(data, SER_DISK, 0);
            ss >> *this;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool operator==(const MintingRecord& other) const {
        return l2BlockHash == other.l2BlockHash &&
               l2BlockNumber == other.l2BlockNumber &&
               sequencerAddress == other.sequencerAddress &&
               rewardAmount == other.rewardAmount &&
               l1TxHash == other.l1TxHash &&
               l1BlockNumber == other.l1BlockNumber &&
               feePaid == other.feePaid &&
               timestamp == other.timestamp;
    }

    bool operator!=(const MintingRecord& other) const {
        return !(*this == other);
    }
};

} // namespace l2

#endif // CASCOIN_L2_TOKEN_H
