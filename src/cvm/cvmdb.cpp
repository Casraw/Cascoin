// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cvmdb.h>
#include <util.h>
#include <clientversion.h>

namespace CVM {

std::unique_ptr<CVMDatabase> g_cvmdb;

CVMDatabase::CVMDatabase(const fs::path& dbPath, size_t nCacheSize, 
                         bool fMemory, bool fWipe) {
    db = std::make_unique<CDBWrapper>(dbPath, nCacheSize, fMemory, fWipe);
}

CVMDatabase::~CVMDatabase() {
    Flush();
}

bool CVMDatabase::Load(const uint160& contractAddr, const uint256& key, uint256& value) {
    // Check cache first
    auto cacheKey = std::make_pair(contractAddr, key);
    auto it = storageCache.find(cacheKey);
    if (it != storageCache.end()) {
        value = it->second;
        return true;
    }
    
    // Read from database
    std::string dbKey = std::string(1, DB_STORAGE) + 
                       std::string((char*)contractAddr.begin(), 20) + 
                       std::string((char*)key.begin(), 32);
    
    bool result = db->Read(dbKey, value);
    
    // Cache the result
    if (result) {
        storageCache[cacheKey] = value;
    }
    
    return result;
}

bool CVMDatabase::Store(const uint160& contractAddr, const uint256& key, const uint256& value) {
    // Update cache
    auto cacheKey = std::make_pair(contractAddr, key);
    storageCache[cacheKey] = value;
    
    // Write to database
    std::string dbKey = std::string(1, DB_STORAGE) + 
                       std::string((char*)contractAddr.begin(), 20) + 
                       std::string((char*)key.begin(), 32);
    
    return db->Write(dbKey, value);
}

bool CVMDatabase::Exists(const uint160& contractAddr) {
    Contract contract;
    return ReadContract(contractAddr, contract);
}

bool CVMDatabase::WriteContract(const uint160& address, const Contract& contract) {
    std::string dbKey = std::string(1, DB_CONTRACT) + 
                       std::string((char*)address.begin(), 20);
    
    if (!db->Write(dbKey, contract)) {
        return false;
    }
    
    // Add to contract list
    std::string listKey = std::string(1, DB_CONTRACT_LIST);
    std::vector<uint160> contracts = ListContracts();
    
    // Check if already in list
    bool found = false;
    for (const auto& addr : contracts) {
        if (addr == address) {
            found = true;
            break;
        }
    }
    
    if (!found) {
        contracts.push_back(address);
        db->Write(listKey, contracts);
    }
    
    return true;
}

bool CVMDatabase::ReadContract(const uint160& address, Contract& contract) {
    std::string dbKey = std::string(1, DB_CONTRACT) + 
                       std::string((char*)address.begin(), 20);
    
    return db->Read(dbKey, contract);
}

bool CVMDatabase::DeleteContract(const uint160& address) {
    std::string dbKey = std::string(1, DB_CONTRACT) + 
                       std::string((char*)address.begin(), 20);
    
    return db->Erase(dbKey);
}

std::vector<uint160> CVMDatabase::ListContracts() {
    std::string listKey = std::string(1, DB_CONTRACT_LIST);
    std::vector<uint160> contracts;
    
    db->Read(listKey, contracts);
    
    return contracts;
}

bool CVMDatabase::WriteNonce(const uint160& address, uint64_t nonce) {
    nonceCache[address] = nonce;
    
    std::string dbKey = std::string(1, DB_NONCE) + 
                       std::string((char*)address.begin(), 20);
    
    return db->Write(dbKey, nonce);
}

bool CVMDatabase::ReadNonce(const uint160& address, uint64_t& nonce) {
    // Check cache
    auto it = nonceCache.find(address);
    if (it != nonceCache.end()) {
        nonce = it->second;
        return true;
    }
    
    std::string dbKey = std::string(1, DB_NONCE) + 
                       std::string((char*)address.begin(), 20);
    
    bool result = db->Read(dbKey, nonce);
    
    if (result) {
        nonceCache[address] = nonce;
    } else {
        nonce = 0;
    }
    
    return result;
}

uint64_t CVMDatabase::GetNextNonce(const uint160& address) {
    uint64_t nonce;
    if (!ReadNonce(address, nonce)) {
        nonce = 0;
    }
    
    nonce++;
    WriteNonce(address, nonce);
    
    return nonce;
}

bool CVMDatabase::WriteBalance(const uint160& address, uint64_t balance) {
    std::string dbKey = std::string(1, DB_BALANCE) + 
                       std::string((char*)address.begin(), 20);
    
    return db->Write(dbKey, balance);
}

bool CVMDatabase::ReadBalance(const uint160& address, uint64_t& balance) {
    std::string dbKey = std::string(1, DB_BALANCE) + 
                       std::string((char*)address.begin(), 20);
    
    if (!db->Read(dbKey, balance)) {
        balance = 0;
        return false;
    }
    
    return true;
}

// Generic key-value storage for extensions (Web-of-Trust, etc.)
bool CVMDatabase::WriteGeneric(const std::string& key, const std::vector<uint8_t>& value) {
    return db->Write(key, value);
}

bool CVMDatabase::ReadGeneric(const std::string& key, std::vector<uint8_t>& value) {
    return db->Read(key, value);
}

bool CVMDatabase::ExistsGeneric(const std::string& key) {
    return db->Exists(key);
}

bool CVMDatabase::EraseGeneric(const std::string& key) {
    return db->Erase(key);
}

std::vector<std::string> CVMDatabase::ListKeysWithPrefix(const std::string& prefix) {
    std::vector<std::string> keys;
    
    // Create iterator
    std::unique_ptr<CDBIterator> pcursor(db->NewIterator());
    
    // Start from the beginning since string serialization adds length prefix
    // which makes direct prefix seeking unreliable
    pcursor->SeekToFirst();
    
    // Iterate through all keys and filter by prefix
    while (pcursor->Valid()) {
        std::string key;
        // Try to deserialize as string
        if (pcursor->GetKey(key)) {
            // Check if this key matches our prefix
            if (key.compare(0, prefix.size(), prefix) == 0) {
                keys.push_back(key);
            }
        }
        pcursor->Next();
    }
    
    return keys;
}

bool CVMDatabase::Flush() {
    return db->Flush();
}

// Batch operations
CVMDatabase::Batch::Batch(CVMDatabase& db) 
    : database(db), batch(db.GetDB()) {
}

CVMDatabase::Batch::~Batch() {
}

void CVMDatabase::Batch::WriteContract(const uint160& address, const Contract& contract) {
    std::string dbKey = std::string(1, DB_CONTRACT) + 
                       std::string((char*)address.begin(), 20);
    batch.Write(dbKey, contract);
}

void CVMDatabase::Batch::WriteStorage(const uint160& contractAddr, 
                                      const uint256& key, const uint256& value) {
    std::string dbKey = std::string(1, DB_STORAGE) + 
                       std::string((char*)contractAddr.begin(), 20) + 
                       std::string((char*)key.begin(), 32);
    batch.Write(dbKey, value);
    
    // Update cache
    auto cacheKey = std::make_pair(contractAddr, key);
    database.storageCache[cacheKey] = value;
}

void CVMDatabase::Batch::WriteNonce(const uint160& address, uint64_t nonce) {
    std::string dbKey = std::string(1, DB_NONCE) + 
                       std::string((char*)address.begin(), 20);
    batch.Write(dbKey, nonce);
    
    // Update cache
    database.nonceCache[address] = nonce;
}

void CVMDatabase::Batch::WriteBalance(const uint160& address, uint64_t balance) {
    std::string dbKey = std::string(1, DB_BALANCE) + 
                       std::string((char*)address.begin(), 20);
    batch.Write(dbKey, balance);
}

bool CVMDatabase::Batch::Commit() {
    return database.GetDB().WriteBatch(batch);
}

// Global functions
bool InitCVMDatabase(const fs::path& datadir, size_t nCacheSize) {
    try {
        fs::path cvmDbPath = datadir / "cvm";
        g_cvmdb = std::make_unique<CVMDatabase>(cvmDbPath, nCacheSize);
        return true;
    } catch (const std::exception& e) {
        LogPrintf("Error initializing CVM database: %s\n", e.what());
        return false;
    }
}

void ShutdownCVMDatabase() {
    if (g_cvmdb) {
        g_cvmdb->Flush();
        g_cvmdb.reset();
    }
}

} // namespace CVM

