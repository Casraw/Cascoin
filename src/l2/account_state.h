// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_ACCOUNT_STATE_H
#define CASCOIN_L2_ACCOUNT_STATE_H

/**
 * @file account_state.h
 * @brief L2 Account State structure for state management
 * 
 * This file defines the AccountState structure that represents the state
 * of an account on the L2 chain. It includes balance, nonce, contract
 * code hash, storage root, HAT v2 reputation score, and activity tracking.
 * 
 * Requirements: 10.1, 20.1
 */

#include <uint256.h>
#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <hash.h>

#include <cstdint>
#include <vector>

namespace l2 {

/**
 * @brief Account state structure for L2 state management
 * 
 * Represents the complete state of an account on L2, including:
 * - Balance: Amount of CAS held on L2
 * - Nonce: Transaction counter for replay protection
 * - CodeHash: Hash of contract code (zero for EOA)
 * - StorageRoot: Merkle root of contract storage (zero for EOA)
 * - HatScore: Cached HAT v2 reputation score from L1
 * - LastActivity: Block number of last transaction
 * 
 * The structure supports serialization for storage in the Sparse Merkle Tree
 * and for transmission in proofs.
 */
struct AccountState {
    /** Account balance in satoshis */
    CAmount balance;
    
    /** Transaction nonce (incremented with each transaction) */
    uint64_t nonce;
    
    /** Hash of contract code (zero hash for externally owned accounts) */
    uint256 codeHash;
    
    /** Merkle root of contract storage tree (zero for EOA) */
    uint256 storageRoot;
    
    /** Cached HAT v2 reputation score (0-100) */
    uint32_t hatScore;
    
    /** Block number of last activity (for state rent calculation) */
    uint64_t lastActivity;

    /** Default constructor - creates empty account state */
    AccountState() 
        : balance(0)
        , nonce(0)
        , codeHash()
        , storageRoot()
        , hatScore(0)
        , lastActivity(0)
    {}

    /** Constructor with all fields */
    AccountState(CAmount balance_, uint64_t nonce_, const uint256& codeHash_,
                 const uint256& storageRoot_, uint32_t hatScore_, uint64_t lastActivity_)
        : balance(balance_)
        , nonce(nonce_)
        , codeHash(codeHash_)
        , storageRoot(storageRoot_)
        , hatScore(hatScore_)
        , lastActivity(lastActivity_)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(balance);
        READWRITE(nonce);
        READWRITE(codeHash);
        READWRITE(storageRoot);
        READWRITE(hatScore);
        READWRITE(lastActivity);
    }

    /**
     * @brief Check if this is an empty/non-existent account
     * @return true if account has no balance, nonce, or code
     */
    bool IsEmpty() const {
        return balance == 0 && nonce == 0 && codeHash.IsNull() && storageRoot.IsNull();
    }

    /**
     * @brief Check if this is a contract account
     * @return true if account has contract code
     */
    bool IsContract() const {
        return !codeHash.IsNull();
    }

    /**
     * @brief Check if this is an externally owned account (EOA)
     * @return true if account is not a contract
     */
    bool IsEOA() const {
        return codeHash.IsNull();
    }

    /**
     * @brief Compute the hash of this account state
     * @return SHA256 hash of serialized state
     */
    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << *this;
        return ss.GetHash();
    }

    /**
     * @brief Serialize to bytes for storage in Sparse Merkle Tree
     * @return Serialized bytes
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }

    /**
     * @brief Deserialize from bytes
     * @param data Serialized account state
     * @return true if deserialization succeeded
     */
    bool Deserialize(const std::vector<unsigned char>& data) {
        if (data.empty()) {
            *this = AccountState();
            return true;
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
     * @brief Equality comparison
     */
    bool operator==(const AccountState& other) const {
        return balance == other.balance &&
               nonce == other.nonce &&
               codeHash == other.codeHash &&
               storageRoot == other.storageRoot &&
               hatScore == other.hatScore &&
               lastActivity == other.lastActivity;
    }

    bool operator!=(const AccountState& other) const {
        return !(*this == other);
    }
};

/**
 * @brief State rent configuration
 * 
 * Defines parameters for state rent calculation per Requirement 20.1
 */
struct StateRentConfig {
    /** Rent rate in satoshis per byte per year */
    CAmount rentPerBytePerYear;
    
    /** Minimum balance to keep account active */
    CAmount minimumBalance;
    
    /** Blocks of inactivity before archiving (1 year default) */
    uint64_t archiveThresholdBlocks;
    
    /** Grace period blocks before rent is charged */
    uint64_t gracePeriodBlocks;

    StateRentConfig()
        : rentPerBytePerYear(1)  // 1 satoshi per byte per year
        , minimumBalance(1000)   // 1000 satoshis minimum
        , archiveThresholdBlocks(365 * 24 * 60 * 60 / 150)  // ~1 year at 2.5 min blocks
        , gracePeriodBlocks(1000)  // ~1.7 days grace period
    {}
};

/**
 * @brief Archived account state for restoration
 * 
 * When an account is archived due to inactivity (Requirement 20.2),
 * this structure stores the information needed for restoration.
 */
struct ArchivedAccountState {
    /** The archived account state */
    AccountState state;
    
    /** Block number when archived */
    uint64_t archivedAtBlock;
    
    /** Merkle proof of state at archive time */
    std::vector<unsigned char> archiveProof;
    
    /** State root at archive time */
    uint256 archiveStateRoot;

    ArchivedAccountState() : archivedAtBlock(0) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(state);
        READWRITE(archivedAtBlock);
        READWRITE(archiveProof);
        READWRITE(archiveStateRoot);
    }
};

/**
 * @brief Convert address (uint160) to uint256 key for Sparse Merkle Tree
 * @param address The 160-bit address
 * @return 256-bit key with address in lower bits
 */
inline uint256 AddressToKey(const uint160& address) {
    uint256 key;
    // Copy address bytes to lower 20 bytes of key
    memcpy(key.begin(), address.begin(), 20);
    return key;
}

/**
 * @brief Extract address from uint256 key
 * @param key The 256-bit key
 * @return 160-bit address from lower bits
 */
inline uint160 KeyToAddress(const uint256& key) {
    uint160 address;
    memcpy(address.begin(), key.begin(), 20);
    return address;
}

} // namespace l2

#endif // CASCOIN_L2_ACCOUNT_STATE_H
