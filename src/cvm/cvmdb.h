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
#include <serialize.h>
#include <map>
#include <memory>
#include <vector>
#include <functional>

// Forward declaration for ValidatorEligibilityRecord (defined in validator_attestation.h)
// Note: This struct is NOT in the CVM namespace
struct ValidatorEligibilityRecord;

namespace CVM {

/**
 * Record of validators who participated in transaction validation
 * Used for calculating validator compensation (30% of gas fees)
 */
struct TransactionValidationRecord {
    uint256 txHash;                    // Transaction hash
    std::vector<uint160> validators;   // Validators who provided responses
    uint64_t blockHeight;              // Block height where transaction was included
    
    TransactionValidationRecord() : blockHeight(0) {}
    
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txHash);
        READWRITE(validators);
        READWRITE(blockHeight);
    }
};

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
static const char DB_VALIDATOR_PARTICIPATION = 'V';  // Validator participation: 'V' + txhash -> TransactionValidationRecord
static const char DB_VALIDATOR_RECORD = 'E';  // Validator eligibility: 'E' + address -> ValidatorEligibilityRecord
static const char DB_CONTRACT_RECEIPTS = 'T'; // Contract receipts: 'T' + contractAddr -> vector<uint256>
static const char DB_UNDO = 'U';              // Block undo journal: 'U' + blockhash -> CVMUndoRecord
static const char DB_UNDO_INDEX = 'u';        // Undo journal index: 'u' -> vector<pair<height, blockhash>>

/**
 * Number of recent blocks for which a CVM undo journal is retained.
 *
 * A disconnect can only ever target a block within the active chain's recent
 * history, so retaining a bounded window keeps the journal from growing without
 * limit while still covering any realistic reorg depth.
 */
static constexpr int CVM_UNDO_KEEP_BLOCKS = 288;

/**
 * A single journaled prior value.
 *
 * `existed == false` records that the key was ABSENT before the block mutated
 * it, so undoing the block must erase the key rather than restore a value.
 */
template <typename T>
struct CVMUndoValue {
    bool existed;
    T value;

    CVMUndoValue() : existed(false), value() {}
    CVMUndoValue(bool exists, const T& v) : existed(exists), value(v) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(existed);
        READWRITE(value);
    }
};

/**
 * CVMUndoRecord - per-block reverse journal for CVM state.
 *
 * ConnectBlock records the prior value of every CVM key it mutates (contract
 * code/metadata, contract storage, nonces, balances, and the generic keyspace
 * that backs the Web-of-Trust: trust edges, bonded votes, DAO disputes and
 * reputation records). DisconnectBlock replays the journal to restore the exact
 * pre-block CVM state, so a reorg can no longer leave committed CVM/WoT state
 * behind that diverges from the canonical chain.
 *
 * Only the FIRST touch of a key in a block is journaled, which is exactly the
 * pre-block value.
 */
struct CVMUndoRecord {
    int32_t height;

    std::map<uint160, CVMUndoValue<Contract>> contracts;
    std::map<std::pair<uint160, uint256>, CVMUndoValue<uint256>> storage;
    std::map<uint160, CVMUndoValue<uint64_t>> nonces;
    std::map<uint160, CVMUndoValue<uint64_t>> balances;
    std::map<std::string, CVMUndoValue<std::vector<uint8_t>>> generic;

    // The contract index list is rewritten wholesale by WriteContract, so it is
    // journaled as a single prior snapshot rather than per entry.
    // `contractListCaptured` distinguishes "not journaled this block" from
    // "journaled, and was absent".
    bool contractListCaptured;
    CVMUndoValue<std::vector<uint160>> contractList;

    CVMUndoRecord() : height(0), contractListCaptured(false) {}

    bool IsEmpty() const {
        return contracts.empty() && storage.empty() && nonces.empty() &&
               balances.empty() && generic.empty() && !contractListCaptured;
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(height);
        READWRITE(contracts);
        READWRITE(storage);
        READWRITE(nonces);
        READWRITE(balances);
        READWRITE(generic);
        READWRITE(contractListCaptured);
        READWRITE(contractList);
    }
};

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
    // Exposes persisted contract bytecode to the VM so OP_CALL can execute a
    // callee through the ContractStorage interface.
    bool LoadCode(const uint160& contractAddr, std::vector<uint8_t>& code) override;
    
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
    
    // Contract receipt index (for efficient contract-based receipt queries)
    bool WriteContractReceiptIndex(const uint160& contractAddr, const std::vector<uint256>& txHashes);
    bool ReadContractReceiptIndex(const uint160& contractAddr, std::vector<uint256>& txHashes);
    bool AppendContractReceiptIndex(const uint160& contractAddr, const uint256& txHash);
    
    // Receipt pruning (delete receipts older than specified block height)
    bool PruneReceipts(uint32_t beforeBlockNumber);
    
    // Validator participation tracking (for gas fee distribution)
    bool WriteValidatorParticipation(const uint256& txHash, const TransactionValidationRecord& record);
    bool ReadValidatorParticipation(const uint256& txHash, TransactionValidationRecord& record);
    bool GetValidatorParticipation(const uint256& txHash, TransactionValidationRecord& record);
    bool HasValidatorParticipation(const uint256& txHash);
    
    // Validator eligibility record persistence (for validator pool management)
    bool WriteValidatorRecord(const ValidatorEligibilityRecord& record);
    bool ReadValidatorRecord(const uint160& address, ValidatorEligibilityRecord& record);
    bool IterateValidatorRecords(std::function<bool(const ValidatorEligibilityRecord&)> callback);
    bool DeleteValidatorRecord(const uint160& address);
    
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
    
    // ===== Block undo journal (reorg safety) =====
    
    /**
     * Begin recording a reverse journal of every CVM mutation.
     *
     * Called by ConnectBlock before any CVM work for the block. Any journal that
     * was already recording is discarded first (it belongs to an abandoned
     * attempt), so a failed connect can never leak entries into the next block.
     */
    void BeginBlockUndo(int height);
    
    /**
     * Persist the journal for `blockHash` and stop recording.
     *
     * Also prunes journals older than CVM_UNDO_KEEP_BLOCKS.
     *
     * @return true on success
     */
    bool CommitBlockUndo(const uint256& blockHash);
    
    /**
     * Apply the in-memory journal immediately and stop recording.
     *
     * Used for validation-only passes (fJustCheck) and for failed connects, so
     * neither leaves CVM state behind.
     */
    void AbortBlockUndo();
    
    /**
     * Revert all CVM state written by `blockHash`.
     *
     * Called from DisconnectBlock. Erases the journal afterwards. Returns true
     * if a journal was found and applied; false when no journal exists (for
     * example a block connected by an older node build).
     */
    bool UndoBlock(const uint256& blockHash);
    
    /** True while a journal is recording. */
    bool IsRecordingUndo() const { return m_undoRecording; }
    
    /** Drop cached reads. Required after any out-of-band state restore. */
    void ClearCaches();
    
private:
    std::unique_ptr<CDBWrapper> db;
    
    // Cache for frequently accessed data
    std::map<std::pair<uint160, uint256>, uint256> storageCache;
    std::map<uint160, uint64_t> nonceCache;
    
    // ===== Undo journal state =====
    bool m_undoRecording = false;
    CVMUndoRecord m_undo;
    
    // Journal the pre-block value of a key on first touch. No-ops when the
    // journal is not recording or the key was already captured for this block.
    void JournalContract(const uint160& address);
    void JournalStorage(const uint160& contractAddr, const uint256& key);
    void JournalNonce(const uint160& address);
    void JournalBalance(const uint160& address);
    void JournalGeneric(const std::string& key);
    void JournalContractList();
    
    // Restore every entry of `rec` into the database.
    bool ApplyUndoRecord(const CVMUndoRecord& rec);
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

