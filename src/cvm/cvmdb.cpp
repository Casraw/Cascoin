// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/cvmdb.h>
#include <cvm/validator_attestation.h>
#include <util.h>
#include <clientversion.h>

#include <algorithm>

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
    // Capture the pre-block value so a disconnect can restore it.
    JournalStorage(contractAddr, key);

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
    // Capture the pre-block contract record so a disconnect can restore it.
    JournalContract(address);

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
        // The index is rewritten wholesale, so journal its prior snapshot.
        JournalContractList();
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

bool CVMDatabase::LoadContract(const uint160& address, std::vector<uint8_t>& code) {
    Contract contract;
    if (ReadContract(address, contract)) {
        code = contract.code;
        return true;
    }
    return false;
}

bool CVMDatabase::DeleteContract(const uint160& address) {
    // Capture the pre-block contract record so a disconnect can restore it.
    JournalContract(address);

    std::string dbKey = std::string(1, DB_CONTRACT) + 
                       std::string((char*)address.begin(), 20);
    
    return db->Erase(dbKey);
}

bool CVMDatabase::LoadCode(const uint160& contractAddr, std::vector<uint8_t>& code) {
    return LoadContract(contractAddr, code);
}

std::vector<uint160> CVMDatabase::ListContracts() {
    std::string listKey = std::string(1, DB_CONTRACT_LIST);
    std::vector<uint160> contracts;
    
    db->Read(listKey, contracts);
    
    return contracts;
}

bool CVMDatabase::WriteNonce(const uint160& address, uint64_t nonce) {
    // Capture the pre-block nonce so a disconnect can restore it.
    JournalNonce(address);

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
    // Capture the pre-block balance so a disconnect can restore it.
    JournalBalance(address);

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
    // The generic keyspace backs the Web-of-Trust (trust edges, bonded votes,
    // DAO disputes, reputation), so journaling it is what makes WoT state
    // reorg-safe.
    JournalGeneric(key);

    return db->Write(key, value);
}

bool CVMDatabase::ReadGeneric(const std::string& key, std::vector<uint8_t>& value) {
    return db->Read(key, value);
}

bool CVMDatabase::ExistsGeneric(const std::string& key) {
    return db->Exists(key);
}

bool CVMDatabase::EraseGeneric(const std::string& key) {
    JournalGeneric(key);

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

// ===== Block undo journal (reorg safety) =====
//
// ConnectBlock brackets all CVM work for a block between BeginBlockUndo and
// CommitBlockUndo. Every mutating accessor records the key's pre-block value on
// first touch, so DisconnectBlock can restore the exact prior CVM state. Undo
// application writes through `db` directly so restores are never re-journaled.

void CVMDatabase::ClearCaches() {
    storageCache.clear();
    nonceCache.clear();
}

void CVMDatabase::JournalContract(const uint160& address) {
    if (!m_undoRecording) return;
    if (m_undo.contracts.count(address)) return;  // first touch already captured

    Contract prior;
    const bool existed = ReadContract(address, prior);
    m_undo.contracts.emplace(address,
        CVMUndoValue<Contract>(existed, existed ? prior : Contract()));
}

void CVMDatabase::JournalStorage(const uint160& contractAddr, const uint256& key) {
    if (!m_undoRecording) return;

    const auto cacheKey = std::make_pair(contractAddr, key);
    if (m_undo.storage.count(cacheKey)) return;

    uint256 prior;
    const bool existed = Load(contractAddr, key, prior);
    m_undo.storage.emplace(cacheKey,
        CVMUndoValue<uint256>(existed, existed ? prior : uint256()));
}

void CVMDatabase::JournalNonce(const uint160& address) {
    if (!m_undoRecording) return;
    if (m_undo.nonces.count(address)) return;

    uint64_t prior = 0;
    const bool existed = ReadNonce(address, prior);
    m_undo.nonces.emplace(address,
        CVMUndoValue<uint64_t>(existed, existed ? prior : 0));
}

void CVMDatabase::JournalBalance(const uint160& address) {
    if (!m_undoRecording) return;
    if (m_undo.balances.count(address)) return;

    uint64_t prior = 0;
    const bool existed = ReadBalance(address, prior);
    m_undo.balances.emplace(address,
        CVMUndoValue<uint64_t>(existed, existed ? prior : 0));
}

void CVMDatabase::JournalGeneric(const std::string& key) {
    if (!m_undoRecording) return;
    if (m_undo.generic.count(key)) return;

    std::vector<uint8_t> prior;
    const bool existed = db->Read(key, prior);
    m_undo.generic.emplace(key,
        CVMUndoValue<std::vector<uint8_t>>(existed, existed ? prior : std::vector<uint8_t>()));
}

void CVMDatabase::JournalContractList() {
    if (!m_undoRecording) return;
    if (m_undo.contractListCaptured) return;

    const std::string listKey(1, DB_CONTRACT_LIST);
    std::vector<uint160> prior;
    const bool existed = db->Read(listKey, prior);

    m_undo.contractListCaptured = true;
    m_undo.contractList = CVMUndoValue<std::vector<uint160>>(
        existed, existed ? prior : std::vector<uint160>());
}

bool CVMDatabase::ApplyUndoRecord(const CVMUndoRecord& rec) {
    bool ok = true;

    for (const auto& entry : rec.contracts) {
        const std::string dbKey = std::string(1, DB_CONTRACT) +
                                  std::string((const char*)entry.first.begin(), 20);
        ok = (entry.second.existed ? db->Write(dbKey, entry.second.value)
                                   : db->Erase(dbKey)) && ok;
    }

    for (const auto& entry : rec.storage) {
        const std::string dbKey = std::string(1, DB_STORAGE) +
                                  std::string((const char*)entry.first.first.begin(), 20) +
                                  std::string((const char*)entry.first.second.begin(), 32);
        ok = (entry.second.existed ? db->Write(dbKey, entry.second.value)
                                   : db->Erase(dbKey)) && ok;
    }

    for (const auto& entry : rec.nonces) {
        const std::string dbKey = std::string(1, DB_NONCE) +
                                  std::string((const char*)entry.first.begin(), 20);
        ok = (entry.second.existed ? db->Write(dbKey, entry.second.value)
                                   : db->Erase(dbKey)) && ok;
    }

    for (const auto& entry : rec.balances) {
        const std::string dbKey = std::string(1, DB_BALANCE) +
                                  std::string((const char*)entry.first.begin(), 20);
        ok = (entry.second.existed ? db->Write(dbKey, entry.second.value)
                                   : db->Erase(dbKey)) && ok;
    }

    // Restores the Web-of-Trust keyspace (trust edges, bonded votes, DAO
    // disputes, reputation records).
    for (const auto& entry : rec.generic) {
        ok = (entry.second.existed ? db->Write(entry.first, entry.second.value)
                                   : db->Erase(entry.first)) && ok;
    }

    if (rec.contractListCaptured) {
        const std::string listKey(1, DB_CONTRACT_LIST);
        ok = (rec.contractList.existed ? db->Write(listKey, rec.contractList.value)
                                       : db->Erase(listKey)) && ok;
    }

    // Cached reads may now be stale relative to the restored state.
    ClearCaches();

    return ok;
}

void CVMDatabase::BeginBlockUndo(int height) {
    if (m_undoRecording) {
        // A previous journal was never committed or aborted (interrupted
        // connect). Revert its entries so they cannot leak into this block.
        LogPrintf("CVM: undo journal for height %d left open; reverting before height %d\n",
                  m_undo.height, height);
        ApplyUndoRecord(m_undo);
    }

    m_undo = CVMUndoRecord();
    m_undo.height = height;
    m_undoRecording = true;
}

bool CVMDatabase::CommitBlockUndo(const uint256& blockHash) {
    if (!m_undoRecording) {
        return true;
    }
    m_undoRecording = false;

    CVMUndoRecord rec = m_undo;
    m_undo = CVMUndoRecord();

    // A block that touched no CVM state needs no journal.
    if (rec.IsEmpty()) {
        return true;
    }

    const std::string undoKey = std::string(1, DB_UNDO) +
                                std::string((const char*)blockHash.begin(), 32);
    if (!db->Write(undoKey, rec)) {
        LogPrintf("CVM: ERROR - failed to persist undo journal for block %s\n",
                  blockHash.ToString());
        return false;
    }

    // Bounded index so journals outside the retention window can be pruned.
    const std::string idxKey(1, DB_UNDO_INDEX);
    std::vector<std::pair<int32_t, uint256>> index;
    db->Read(idxKey, index);
    index.emplace_back(rec.height, blockHash);

    while (index.size() > (size_t)CVM_UNDO_KEEP_BLOCKS) {
        const std::string staleKey = std::string(1, DB_UNDO) +
                                     std::string((const char*)index.front().second.begin(), 32);
        db->Erase(staleKey);
        index.erase(index.begin());
    }

    return db->Write(idxKey, index);
}

void CVMDatabase::AbortBlockUndo() {
    if (!m_undoRecording) {
        return;
    }
    m_undoRecording = false;

    ApplyUndoRecord(m_undo);
    m_undo = CVMUndoRecord();
}

bool CVMDatabase::UndoBlock(const uint256& blockHash) {
    const std::string undoKey = std::string(1, DB_UNDO) +
                                std::string((const char*)blockHash.begin(), 32);

    CVMUndoRecord rec;
    if (!db->Read(undoKey, rec)) {
        // No journal: either the block touched no CVM state, or it was
        // connected by a build that predates the journal.
        return false;
    }

    const bool ok = ApplyUndoRecord(rec);

    db->Erase(undoKey);

    const std::string idxKey(1, DB_UNDO_INDEX);
    std::vector<std::pair<int32_t, uint256>> index;
    if (db->Read(idxKey, index)) {
        index.erase(std::remove_if(index.begin(), index.end(),
                        [&blockHash](const std::pair<int32_t, uint256>& e) {
                            return e.second == blockHash;
                        }),
                    index.end());
        db->Write(idxKey, index);
    }

    Flush();

    LogPrintf("CVM: reverted state for disconnected block %s (height %d)\n",
              blockHash.ToString(), rec.height);

    return ok;
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

void CVMDatabase::GetAllKeys(const std::string& prefix, std::vector<std::string>& keys) {
    std::unique_ptr<CDBIterator> pcursor(db->NewIterator());
    pcursor->Seek(prefix);
    
    while (pcursor->Valid()) {
        std::string key;
        if (!pcursor->GetKey(key)) {
            break;
        }
        
        // Check if key starts with prefix
        if (key.compare(0, prefix.length(), prefix) != 0) {
            break;  // No more keys with this prefix
        }
        
        keys.push_back(key);
        pcursor->Next();
    }
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

// Receipt management
bool CVMDatabase::WriteReceipt(const uint256& txHash, const TransactionReceipt& receipt) {
    std::string key = std::string(1, DB_RECEIPT) + txHash.ToString();
    if (!db->Write(key, receipt)) {
        return false;
    }

    // Update contract receipt index
    // Use contractAddress for contract creation, otherwise use 'to'
    uint160 contractAddr = receipt.IsContractCreation() ? receipt.contractAddress : receipt.to;
    if (!contractAddr.IsNull()) {
        AppendContractReceiptIndex(contractAddr, txHash);
    }

    return true;
}

bool CVMDatabase::ReadReceipt(const uint256& txHash, TransactionReceipt& receipt) {
    std::string key = std::string(1, DB_RECEIPT) + txHash.ToString();
    return db->Read(key, receipt);
}

bool CVMDatabase::HasReceipt(const uint256& txHash) {
    std::string key = std::string(1, DB_RECEIPT) + txHash.ToString();
    return db->Exists(key);
}

bool CVMDatabase::DeleteReceipt(const uint256& txHash) {
    std::string key = std::string(1, DB_RECEIPT) + txHash.ToString();
    return db->Erase(key);
}

bool CVMDatabase::WriteBlockReceipts(const uint256& blockHash, const std::vector<uint256>& txHashes) {
    std::string key = std::string(1, DB_RECEIPT_BLOCK) + blockHash.ToString();
    return db->Write(key, txHashes);
}

bool CVMDatabase::ReadBlockReceipts(const uint256& blockHash, std::vector<uint256>& txHashes) {
    std::string key = std::string(1, DB_RECEIPT_BLOCK) + blockHash.ToString();
    return db->Read(key, txHashes);
}

bool CVMDatabase::WriteContractReceiptIndex(const uint160& contractAddr, const std::vector<uint256>& txHashes) {
    std::string key = std::string(1, DB_CONTRACT_RECEIPTS) +
                     std::string((char*)contractAddr.begin(), 20);
    return db->Write(key, txHashes);
}

bool CVMDatabase::ReadContractReceiptIndex(const uint160& contractAddr, std::vector<uint256>& txHashes) {
    std::string key = std::string(1, DB_CONTRACT_RECEIPTS) +
                     std::string((char*)contractAddr.begin(), 20);
    return db->Read(key, txHashes);
}

bool CVMDatabase::AppendContractReceiptIndex(const uint160& contractAddr, const uint256& txHash) {
    std::vector<uint256> txHashes;
    ReadContractReceiptIndex(contractAddr, txHashes);

    // Check if already present to avoid duplicates
    for (const auto& hash : txHashes) {
        if (hash == txHash) {
            return true;
        }
    }

    txHashes.push_back(txHash);
    return WriteContractReceiptIndex(contractAddr, txHashes);
}

bool CVMDatabase::PruneReceipts(uint32_t beforeBlockNumber) {
    LogPrintf("CVM: Receipt pruning requested for blocks before %d\n", beforeBlockNumber);

    int prunedCount = 0;
    std::vector<std::string> keysToDelete;

    // Receipts are stored under keys serialized as the std::string
    // 'R' + txHash.ToString() (see WriteReceipt). The keys must therefore be
    // read back as strings (NOT as a std::pair<char, uint256>, which never
    // matches the stored layout and caused pruning to be a no-op).
    const std::string receiptPrefix = std::string(1, DB_RECEIPT);

    std::unique_ptr<CDBIterator> pcursor(db->NewIterator());
    // String keys are serialized with a length prefix, which makes direct
    // prefix seeking unreliable, so scan from the start and filter by prefix.
    pcursor->SeekToFirst();

    while (pcursor->Valid()) {
        std::string key;
        if (pcursor->GetKey(key) &&
            key.size() == receiptPrefix.size() + 64 &&  // 'R' + 64-char hex tx hash
            key.compare(0, receiptPrefix.size(), receiptPrefix) == 0) {

            // Read receipt and check whether it is old enough to prune.
            TransactionReceipt receipt;
            if (pcursor->GetValue(receipt) && receipt.blockNumber < beforeBlockNumber) {
                keysToDelete.push_back(key);
            }
        }

        pcursor->Next();
    }

    // Delete old receipts using their exact stored keys.
    for (const auto& dbKey : keysToDelete) {
        if (db->Erase(dbKey)) {
            prunedCount++;
        }
    }

    LogPrintf("CVM: Pruned %d receipts from blocks before %d\n", prunedCount, beforeBlockNumber);

    return true;
}

// Validator participation tracking
bool CVMDatabase::WriteValidatorParticipation(const uint256& txHash, const TransactionValidationRecord& record) {
    std::string dbKey = std::string(1, DB_VALIDATOR_PARTICIPATION) + 
                       std::string((char*)txHash.begin(), 32);
    
    return db->Write(dbKey, record);
}

bool CVMDatabase::ReadValidatorParticipation(const uint256& txHash, TransactionValidationRecord& record) {
    std::string dbKey = std::string(1, DB_VALIDATOR_PARTICIPATION) + 
                       std::string((char*)txHash.begin(), 32);
    
    return db->Read(dbKey, record);
}

bool CVMDatabase::GetValidatorParticipation(const uint256& txHash, TransactionValidationRecord& record) {
    return ReadValidatorParticipation(txHash, record);
}

bool CVMDatabase::HasValidatorParticipation(const uint256& txHash) {
    TransactionValidationRecord record;
    return ReadValidatorParticipation(txHash, record);
}

// Validator eligibility record persistence
bool CVMDatabase::WriteValidatorRecord(const ValidatorEligibilityRecord& record) {
    // Key format: DB_VALIDATOR_RECORD + "validator_" + address.ToString()
    // This matches the key format used in AutomaticValidatorManager::StoreEligibilityRecord
    std::string dbKey = std::string(1, DB_VALIDATOR_RECORD) + "validator_" + record.validatorAddress.ToString();
    
    // Serialize using CDataStream with SER_DISK
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << record;
    
    // Convert to vector for storage
    std::vector<uint8_t> data(ss.begin(), ss.end());
    
    bool result = db->Write(dbKey, data);
    
    if (result) {
        LogPrint(BCLog::CVM, "CVMDatabase: Wrote validator record for %s\n",
                 record.validatorAddress.ToString());
    } else {
        LogPrintf("CVMDatabase: Failed to write validator record for %s\n",
                  record.validatorAddress.ToString());
    }
    
    return result;
}

bool CVMDatabase::ReadValidatorRecord(const uint160& address, ValidatorEligibilityRecord& record) {
    // Key format: DB_VALIDATOR_RECORD + "validator_" + address.ToString()
    std::string dbKey = std::string(1, DB_VALIDATOR_RECORD) + "validator_" + address.ToString();
    
    std::vector<uint8_t> data;
    if (!db->Read(dbKey, data)) {
        return false;
    }
    
    // Deserialize using CDataStream with SER_DISK
    try {
        CDataStream ss(data, SER_DISK, CLIENT_VERSION);
        ss >> record;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CVMDatabase: Failed to deserialize validator record for %s: %s\n",
                  address.ToString(), e.what());
        return false;
    }
}

bool CVMDatabase::IterateValidatorRecords(std::function<bool(const ValidatorEligibilityRecord&)> callback) {
    // Create iterator
    std::unique_ptr<CDBIterator> pcursor(db->NewIterator());
    
    // Seek to the first validator record key
    // Key prefix: DB_VALIDATOR_RECORD + "validator_"
    std::string prefix = std::string(1, DB_VALIDATOR_RECORD) + "validator_";
    pcursor->Seek(prefix);
    
    int count = 0;
    int errors = 0;
    
    while (pcursor->Valid()) {
        // Get the key
        std::string key;
        if (!pcursor->GetKey(key)) {
            break;
        }
        
        // Check if key starts with our prefix
        if (key.compare(0, prefix.length(), prefix) != 0) {
            break;  // No more validator records
        }
        
        // Get the value
        std::vector<uint8_t> data;
        if (pcursor->GetValue(data)) {
            try {
                // Deserialize the record
                CDataStream ss(data, SER_DISK, CLIENT_VERSION);
                ValidatorEligibilityRecord record;
                ss >> record;
                
                // Call the callback
                if (!callback(record)) {
                    // Callback returned false, stop iteration
                    break;
                }
                count++;
            } catch (const std::exception& e) {
                LogPrintf("CVMDatabase: Failed to deserialize validator record: %s\n", e.what());
                errors++;
            }
        }
        
        pcursor->Next();
    }
    
    LogPrint(BCLog::CVM, "CVMDatabase: Iterated %d validator records (%d errors)\n", count, errors);
    return errors == 0;
}

bool CVMDatabase::DeleteValidatorRecord(const uint160& address) {
    std::string dbKey = std::string(1, DB_VALIDATOR_RECORD) + "validator_" + address.ToString();
    return db->Erase(dbKey);
}

} // namespace CVM
