// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_CVMDB_H
#define CASCOIN_CVM_CVMDB_H

#include <cvm/vmstate.h>
#include <cvm/contract.h>
#include <cvm/receipt.h>
#include <dbwrapper.h>
#include <uint256.h>
#include <map>
#include <memory>

namespace CVM {

/**
 * Database keys for CVM state storage
 */
static const char DB_CONTRACT = 'C';          // Contract code: 'C' + address -> Contract
static const char DB_STORAGE = 'S';           // Contract storage: 'S' + address + key -> value
static const char DB_NONCE = 'N';             // Account nonce: 'N' + address -> nonce
static const char DB_BALANCE = 'B';           // Contract balance: 'B' + address -> balance
static const char DB_CONTRACT_LIST = 'L';     // List of all contracts
static const char DB_RECEIPT = 'R';           // Transaction receipt: 'R' + txhash -> TransactionReceipt
static const char DB_RECEIPT_BLOCK = 'X';     // Block receipts index: 'X' + blockhash -> vector<txhash>

/**
 * CVMDatabase - LevelDB-backed storage for CVM state
 * 
 * Stores:
 * - Contract bytecode
 * - Contract storage (key-value pairs)
 * - Account nonces
 * - Contract balances
 */
class CVMDatabase : public ContractStorage {
public:
    CVMDatabase(const fs::path& dbPath, size_t nCacheSize = 8 << 20, 
                bool fMemory = false, bool fWipe = false);
    ~CVMDatabase();
    
    // ContractStorage interface
    bool Load(const uint160& contractAddr, const uint256& key, uint256& value) override;
    bool Store(const uint160& contractAddr, const uint256& key, const uint256& value) override;
    bool Exists(const uint160& contractAddr) override;
    
    // Contract management
    bool WriteContract(const uint160& address, const Contract& contract);
    bool ReadContract(const uint160& address, Contract& contract);
    bool LoadContract(const uint160& address, std::vector<uint8_t>& code);
    bool DeleteContract(const uint160& address);
    std::vector<uint160> ListContracts();
    
    // Convenience methods for RPC
    bool GetContractCode(const uint160& address, std::vector<uint8_t>& code) {
        return LoadContract(address, code);
    }
    
    bool GetContractMetadata(const uint160& address, Contract& metadata) {
        return ReadContract(address, metadata);
    }
    
    // Nonce management
    bool WriteNonce(const uint160& address, uint64_t nonce);
    bool ReadNonce(const uint160& address, uint64_t& nonce);
    uint64_t GetNextNonce(const uint160& address);
    
    // Balance management (for contracts that hold value)
    bool WriteBalance(const uint160& address, uint64_t balance);
    bool ReadBalance(const uint160& address, uint64_t& balance);
    
    // Receipt management
    bool WriteReceipt(const uint256& txHash, const TransactionReceipt& receipt);
    bool ReadReceipt(const uint256& txHash, TransactionReceipt& receipt);
    bool HasReceipt(const uint256& txHash);
    bool DeleteReceipt(const uint256& txHash);
    
    // Block receipt index (for efficient block-based queries)
    bool WriteBlockReceipts(const uint256& blockHash, const std::vector<uint256>& txHashes);
    bool ReadBlockReceipts(const uint256& blockHash, std::vector<uint256>& txHashes);
    
    // Receipt pruning (delete receipts older than specified block height)
    bool PruneReceipts(uint32_t beforeBlockNumber);
    
    // Generic key-value storage (for Web-of-Trust and other extensions)
    bool WriteGeneric(const std::string& key, const std::vector<uint8_t>& value);
    bool ReadGeneric(const std::string& key, std::vector<uint8_t>& value);
    bool ExistsGeneric(const std::string& key);
    bool EraseGeneric(const std::string& key);
    
    // Iterator for generic keys with prefix
    std::vector<std::string> ListKeysWithPrefix(const std::string& prefix);
    
    // Batch operations for atomic updates
    class Batch {
    public:
        Batch(CVMDatabase& db);
        ~Batch();
        
        void WriteContract(const uint160& address, const Contract& contract);
        void WriteStorage(const uint160& contractAddr, const uint256& key, const uint256& value);
        void WriteNonce(const uint160& address, uint64_t nonce);
        void WriteBalance(const uint160& address, uint64_t balance);
        bool Commit();
        
    private:
        CVMDatabase& database;
        CDBBatch batch;
    };
    
    // Get database instance
    CDBWrapper& GetDB() { return *db; }
    
    // Flush database
    bool Flush();
    
    // Get all keys with a given prefix
    void GetAllKeys(const std::string& prefix, std::vector<std::string>& keys);
    
private:
    std::unique_ptr<CDBWrapper> db;
    
    // Cache for frequently accessed data
    std::map<std::pair<uint160, uint256>, uint256> storageCache;
    std::map<uint160, uint64_t> nonceCache;
};

/**
 * Global CVM database instance
 */
extern std::unique_ptr<CVMDatabase> g_cvmdb;

/**
 * Initialize CVM database
 */
bool InitCVMDatabase(const fs::path& datadir, size_t nCacheSize = 8 << 20);

/**
 * Shutdown CVM database
 */
void ShutdownCVMDatabase();

} // namespace CVM

#endif // CASCOIN_CVM_CVMDB_H

