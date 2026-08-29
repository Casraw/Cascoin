// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_BURN_REGISTRY_H
#define CASCOIN_L2_BURN_REGISTRY_H

/**
 * @file burn_registry.h
 * @brief Burn Registry for L2 Burn-and-Mint Token Model
 * 
 * This file implements the BurnRegistry class that tracks all processed
 * burn transactions to prevent double-minting. It provides:
 * - Persistent storage of processed burns
 * - Double-mint prevention via IsProcessed() check
 * - Query methods for burn history
 * - L2 reorg handling
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <amount.h>
#include <serialize.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace l2 {

// ============================================================================
// BurnRecord Structure
// ============================================================================

/**
 * @brief Record of a processed burn transaction
 * 
 * This structure stores all information about a burn transaction that has
 * been processed and resulted in L2 token minting. It is used for:
 * - Double-mint prevention (checking if L1 TX was already processed)
 * - Audit trail (tracking all burns and their corresponding mints)
 * - Query support (getting burn history for addresses)
 * 
 * Requirements: 5.1, 5.2
 */
struct BurnRecord {
    /** L1 burn transaction hash - unique identifier */
    uint256 l1TxHash;
    
    /** L1 block number containing the burn transaction */
    uint64_t l1BlockNumber;
    
    /** L1 block hash containing the burn transaction */
    uint256 l1BlockHash;
    
    /** L2 recipient address (Hash160 of public key) */
    uint160 l2Recipient;
    
    /** Amount burned/minted in satoshis */
    CAmount amount;
    
    /** L2 block number where tokens were minted */
    uint64_t l2MintBlock;
    
    /** L2 mint transaction hash */
    uint256 l2MintTxHash;
    
    /** Timestamp when the burn was processed (Unix time) */
    uint64_t timestamp;
    
    /** Default constructor */
    BurnRecord() 
        : l1BlockNumber(0)
        , amount(0)
        , l2MintBlock(0)
        , timestamp(0) {}
    
    /** Full constructor */
    BurnRecord(
        const uint256& l1Hash,
        uint64_t l1Block,
        const uint256& l1BlkHash,
        const uint160& recipient,
        CAmount amt,
        uint64_t l2Block,
        const uint256& l2Hash,
        uint64_t ts)
        : l1TxHash(l1Hash)
        , l1BlockNumber(l1Block)
        , l1BlockHash(l1BlkHash)
        , l2Recipient(recipient)
        , amount(amt)
        , l2MintBlock(l2Block)
        , l2MintTxHash(l2Hash)
        , timestamp(ts) {}
    
    /**
     * @brief Check if the record is valid
     * @return true if all required fields are set
     */
    bool IsValid() const {
        return !l1TxHash.IsNull() &&
               l1BlockNumber > 0 &&
               !l1BlockHash.IsNull() &&
               !l2Recipient.IsNull() &&
               amount > 0 &&
               l2MintBlock > 0 &&
               !l2MintTxHash.IsNull() &&
               timestamp > 0;
    }
    
    /**
     * @brief Serialize the record to bytes
     * @return Serialized byte vector
     */
    std::vector<unsigned char> Serialize() const {
        CDataStream ss(SER_DISK, 0);
        ss << *this;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    }
    
    /**
     * @brief Deserialize record from bytes
     * @param data The serialized data
     * @return true if deserialization succeeded
     */
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
    
    /** Serialization support */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(l1TxHash);
        READWRITE(l1BlockNumber);
        READWRITE(l1BlockHash);
        READWRITE(l2Recipient);
        READWRITE(amount);
        READWRITE(l2MintBlock);
        READWRITE(l2MintTxHash);
        READWRITE(timestamp);
    }
    
    /** Equality operator */
    bool operator==(const BurnRecord& other) const {
        return l1TxHash == other.l1TxHash &&
               l1BlockNumber == other.l1BlockNumber &&
               l1BlockHash == other.l1BlockHash &&
               l2Recipient == other.l2Recipient &&
               amount == other.amount &&
               l2MintBlock == other.l2MintBlock &&
               l2MintTxHash == other.l2MintTxHash &&
               timestamp == other.timestamp;
    }
    
    bool operator!=(const BurnRecord& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// BurnRegistry Class
// ============================================================================

/**
 * @brief Registry for tracking processed burn transactions
 * 
 * The BurnRegistry maintains a persistent record of all burn transactions
 * that have been processed and resulted in L2 token minting. Its primary
 * purpose is to prevent double-minting by ensuring each L1 burn transaction
 * can only be used once.
 * 
 * Storage Keys (LevelDB):
 * - "burn_record_<l1TxHash>" -> BurnRecord (serialized)
 * - "burn_by_addr_<address>_<l1TxHash>" -> uint8_t (index for address lookup)
 * - "burn_total_amount" -> CAmount (total burned)
 * - "burn_count" -> uint64_t (number of burns)
 * - "burn_by_l2block_<l2Block>_<l1TxHash>" -> uint8_t (index for reorg handling)
 * 
 * Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */
class BurnRegistry {
public:
    /**
     * @brief Construct a BurnRegistry
     * 
     * Creates an in-memory registry. For production use, this should
     * be backed by LevelDB for persistence.
     */
    BurnRegistry();
    
    /**
     * @brief Destructor
     */
    ~BurnRegistry();
    
    /**
     * @brief Check if a burn transaction was already processed
     * @param l1TxHash The L1 transaction hash
     * @return true if the burn was already processed
     * 
     * This is the primary method for double-mint prevention.
     * 
     * Requirements: 5.3, 5.4
     */
    bool IsProcessed(const uint256& l1TxHash) const;
    
    /**
     * @brief Record a processed burn transaction
     * @param record The burn record to store
     * @return true if recording succeeded, false if already exists
     * 
     * Stores the burn record and updates all indexes.
     * Returns false if the L1 TX hash was already recorded.
     * 
     * Requirements: 5.1, 5.2
     */
    bool RecordBurn(const BurnRecord& record);
    
    /**
     * @brief Get a burn record by L1 transaction hash
     * @param l1TxHash The L1 transaction hash
     * @return The burn record if found, nullopt otherwise
     * 
     * Requirements: 5.5
     */
    std::optional<BurnRecord> GetBurnRecord(const uint256& l1TxHash) const;
    
    /**
     * @brief Get all burns for a specific L2 address
     * @param address The L2 recipient address
     * @return Vector of burn records for that address
     * 
     * Requirements: 5.5
     */
    std::vector<BurnRecord> GetBurnsForAddress(const uint160& address) const;
    
    /**
     * @brief Get the total amount of CAS burned
     * @return Total burned amount in satoshis
     * 
     * Requirements: 5.1
     */
    CAmount GetTotalBurned() const;
    
    /**
     * @brief Get burn history within a block range
     * @param fromBlock Starting L2 block number (inclusive)
     * @param toBlock Ending L2 block number (inclusive)
     * @return Vector of burn records in the range
     * 
     * Requirements: 5.5
     */
    std::vector<BurnRecord> GetBurnHistory(uint64_t fromBlock, uint64_t toBlock) const;
    
    /**
     * @brief Handle L2 chain reorg by removing burns from reverted blocks
     * @param reorgFromBlock The L2 block number from which reorg occurred
     * @return Number of burn records removed
     * 
     * This method removes all burn records that were minted in blocks
     * >= reorgFromBlock, allowing them to be re-processed.
     * 
     * Requirements: 5.6
     */
    size_t HandleReorg(uint64_t reorgFromBlock);
    
    /**
     * @brief Get the total number of processed burns
     * @return Count of burn records
     */
    size_t GetBurnCount() const;
    
    /**
     * @brief Clear all records (for testing)
     */
    void Clear();
    
    /**
     * @brief Get all burn records (for testing/debugging)
     * @return Vector of all burn records
     */
    std::vector<BurnRecord> GetAllBurns() const;

private:
    /** Map of L1 TX hash -> BurnRecord */
    std::map<uint256, BurnRecord> burnRecords_;
    
    /** Index: L2 address -> set of L1 TX hashes */
    std::map<uint160, std::set<uint256>> addressIndex_;
    
    /** Index: L2 block number -> set of L1 TX hashes (for reorg handling) */
    std::map<uint64_t, std::set<uint256>> blockIndex_;
    
    /** Total amount burned */
    CAmount totalBurned_;
    
    /** Mutex for thread safety */
    mutable CCriticalSection cs_registry_;
    
    /**
     * @brief Add indexes for a burn record
     * @param record The burn record to index
     */
    void AddIndexes(const BurnRecord& record);
    
    /**
     * @brief Remove indexes for a burn record
     * @param record The burn record to unindex
     */
    void RemoveIndexes(const BurnRecord& record);
};

} // namespace l2

#endif // CASCOIN_L2_BURN_REGISTRY_H
