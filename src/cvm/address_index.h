// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_ADDRESS_INDEX_H
#define CASCOIN_CVM_ADDRESS_INDEX_H

#include <uint256.h>
#include <amount.h>
#include <primitives/transaction.h>
#include <coins.h>
#include <dbwrapper.h>
#include <map>
#include <vector>
#include <memory>

namespace CVM {

/**
 * UTXO reference for address indexing
 */
struct AddressUTXO {
    COutPoint outpoint;     // Transaction hash and output index
    CAmount value;          // Value in satoshis
    int height;             // Block height where UTXO was created
    
    AddressUTXO() : value(0), height(0) {}
    AddressUTXO(const COutPoint& op, CAmount val, int h)
        : outpoint(op), value(val), height(h) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(outpoint);
        READWRITE(value);
        READWRITE(height);
    }
};

/**
 * Address balance information
 */
struct AddressBalance {
    uint160 address;
    CAmount balance;
    uint32_t utxoCount;
    int lastUpdateHeight;
    
    AddressBalance() : balance(0), utxoCount(0), lastUpdateHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(balance);
        READWRITE(utxoCount);
        READWRITE(lastUpdateHeight);
    }
};

/**
 * Address Index - Maps addresses to their UTXOs
 * 
 * Enables efficient balance queries for EVM-compatible RPC methods.
 * Updated during block connection/disconnection.
 */
class AddressIndex {
public:
    AddressIndex(CDBWrapper* db);
    ~AddressIndex();
    
    /**
     * Add UTXO to address index
     * Called when a new UTXO is created
     */
    bool AddUTXO(const uint160& address, const COutPoint& outpoint, 
                 CAmount value, int height);
    
    /**
     * Remove UTXO from address index
     * Called when a UTXO is spent
     */
    bool RemoveUTXO(const uint160& address, const COutPoint& outpoint);
    
    /**
     * Get all UTXOs for an address
     */
    std::vector<AddressUTXO> GetAddressUTXOs(const uint160& address);
    
    /**
     * Get balance for an address
     * Calculates from UTXO set or returns cached value
     */
    CAmount GetAddressBalance(const uint160& address);
    
    /**
     * Get balance info with caching
     */
    bool GetBalanceInfo(const uint160& address, AddressBalance& balance);
    
    /**
     * Update address balance cache
     * Called after UTXO changes
     */
    void UpdateBalanceCache(const uint160& address, CAmount balance, 
                           uint32_t utxoCount, int height);
    
    /**
     * Clear balance cache for address
     */
    void InvalidateCache(const uint160& address);
    
    /**
     * Batch update for block connection
     */
    class Batch {
    public:
        Batch(AddressIndex& index);
        ~Batch();
        
        void AddUTXO(const uint160& address, const COutPoint& outpoint,
                    CAmount value, int height);
        void RemoveUTXO(const uint160& address, const COutPoint& outpoint);
        bool Commit();
        
    private:
        AddressIndex& m_index;
        CDBBatch m_batch;
        std::map<uint160, CAmount> m_balanceChanges;
        std::map<uint160, int> m_utxoCountChanges;
    };
    
    /**
     * Flush database
     */
    bool Flush();
    
private:
    CDBWrapper* m_db;
    
    // Cache for frequently accessed balances
    std::map<uint160, AddressBalance> m_balanceCache;
    
    // Database key prefixes
    static const char DB_ADDRESS_UTXO = 'U';      // 'U' + address + outpoint -> AddressUTXO
    static const char DB_ADDRESS_BALANCE = 'A';   // 'A' + address -> AddressBalance
    
    // Helper methods
    std::string MakeUTXOKey(const uint160& address, const COutPoint& outpoint);
    std::string MakeBalanceKey(const uint160& address);
};

/**
 * Global address index instance
 */
extern std::unique_ptr<AddressIndex> g_addressIndex;

/**
 * Initialize address index
 */
bool InitAddressIndex(CDBWrapper* db);

/**
 * Shutdown address index
 */
void ShutdownAddressIndex();

/**
 * Update address index for block connection
 * Called from ConnectBlock()
 */
bool UpdateAddressIndexForBlock(const CBlock& block, int height, 
                               const CCoinsViewCache& view);

/**
 * Update address index for block disconnection
 * Called from DisconnectBlock()
 */
bool RevertAddressIndexForBlock(const CBlock& block, int height,
                               const CCoinsViewCache& view);

} // namespace CVM

#endif // CASCOIN_CVM_ADDRESS_INDEX_H
