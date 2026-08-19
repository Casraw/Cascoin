// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file l2_minter.cpp
 * @brief Implementation of L2 Token Minter for Burn-and-Mint Token Model
 * 
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 8.1, 8.3
 */

#include <l2/l2_minter.h>
#include <l2/l2_globals.h>
#include <l2/account_state.h>
#include <l2/burn_parser.h>
#include <l2/burn_registry.h>
#include <l2/burn_validator.h>
#include <l2/state_manager.h>
#include <hash.h>
#include <util.h>
#include <utiltime.h>
#include <dbwrapper.h>
#include <fs.h>
#include <crypto/common.h>
#include <script/script.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <l2/l2_block.h>
#include <validation.h>
#include <chain.h>
#include <chainparams.h>
#include <cvm/validator_keys.h>
#include <l2/leader_election.h>
#include <l2/sequencer_discovery.h>
#include <l2/fraud_proof.h>
#include <net.h>
#include <netmessagemaker.h>
#include <protocol.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace l2 {

// ============================================================================
// Global Instance Management
// ============================================================================

static std::unique_ptr<L2TokenMinter> g_l2TokenMinter;
static bool g_l2TokenMinterInitialized = false;

L2TokenMinter& GetL2TokenMinter() {
    assert(g_l2TokenMinterInitialized && "L2TokenMinter not initialized");
    return *g_l2TokenMinter;
}

void InitL2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry) {
    g_l2TokenMinter = std::make_unique<L2TokenMinter>(stateManager, burnRegistry);
    g_l2TokenMinterInitialized = true;
}

bool IsL2TokenMinterInitialized() {
    return g_l2TokenMinterInitialized;
}

// ============================================================================
// L2TokenMinter Implementation
// ============================================================================

L2TokenMinter::L2TokenMinter(L2StateManager& stateManager, BurnRegistry& burnRegistry)
    : stateManager_(stateManager)
    , burnRegistry_(burnRegistry)
    , totalSupply_(0)
    , totalMinted_(0)
    , currentBlockNumber_(0)
{
}

L2TokenMinter::~L2TokenMinter() {
}

MintResult L2TokenMinter::MintTokens(
    const uint256& l1TxHash,
    const uint160& recipient,
    CAmount amount)
{
    // Use default values for L1 block info when not provided
    return MintTokensWithDetails(l1TxHash, 0, uint256(), recipient, amount);
}

MintResult L2TokenMinter::MintTokensWithDetails(
    const uint256& l1TxHash,
    uint64_t l1BlockNumber,
    const uint256& l1BlockHash,
    const uint160& recipient,
    CAmount amount)
{
    LOCK(cs_minter_);
    
    // Validate inputs
    if (l1TxHash.IsNull()) {
        return MintResult::Failure("L1 transaction hash is null");
    }
    
    if (recipient.IsNull()) {
        return MintResult::Failure("Recipient address is null");
    }
    
    if (amount <= 0) {
        return MintResult::Failure("Mint amount must be positive");
    }
    
    // Check if burn was already processed (double-mint prevention)
    // Requirement 4.3: Mark burn as processed
    if (burnRegistry_.IsProcessed(l1TxHash)) {
        return MintResult::Failure("Burn transaction already processed");
    }
    
    // Get current block number
    uint64_t blockNumber = currentBlockNumber_;
    if (blockNumber == 0) {
        blockNumber = stateManager_.GetBlockNumber();
    }
    
    // Generate L2 transaction hash
    uint256 l2TxHash = GenerateL2TxHash(l1TxHash, recipient, amount, blockNumber);
    
    // Update state atomically
    // Requirement 4.5: Tokens immediately available in L2 state
    if (!UpdateState(recipient, amount)) {
        return MintResult::Failure("Failed to update L2 state");
    }
    
    // Record the burn in registry
    // Requirement 4.3: Mark burn as processed
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    BurnRecord record(
        l1TxHash,
        l1BlockNumber > 0 ? l1BlockNumber : 1,  // Default to 1 if not provided
        l1BlockHash.IsNull() ? l1TxHash : l1BlockHash,  // Use l1TxHash as placeholder if not provided
        recipient,
        amount,
        blockNumber,
        l2TxHash,
        timestamp
    );
    
    if (!burnRegistry_.RecordBurn(record)) {
        // This shouldn't happen since we checked IsProcessed above
        // But handle it gracefully
        return MintResult::Failure("Failed to record burn in registry");
    }
    
    // Update supply tracking
    // Requirement 4.6: Increase L2 total supply
    totalSupply_ += amount;
    totalMinted_ += amount;
    
    // Emit mint event
    // Requirement 4.4: Emit MintEvent
    MintEvent event(l1TxHash, recipient, amount, l2TxHash, blockNumber, timestamp);
    EmitMintEvent(event);
    
    return MintResult::Success(l2TxHash, blockNumber, amount);
}

CAmount L2TokenMinter::GetTotalSupply() const {
    LOCK(cs_minter_);
    return totalSupply_;
}

bool L2TokenMinter::VerifySupplyInvariant() const {
    LOCK(cs_minter_);
    
    // Requirement 8.1: Total L2 supply == Total CAS burned on L1
    CAmount totalBurned = burnRegistry_.GetTotalBurned();
    if (totalSupply_ != totalBurned) {
        LogPrintf("L2TokenMinter: Supply invariant violated - totalSupply (%lld) != totalBurned (%lld)\n",
                  totalSupply_, totalBurned);
        return false;
    }
    
    // Requirement 8.3: Sum of all L2 balances == Total supply.
    // Sum across ALL accounts (not just mint recipients) so the invariant is
    // correct after transfers move value between arbitrary accounts.
    CAmount sumOfBalances = stateManager_.GetTotalBalances();
    
    if (sumOfBalances != totalSupply_) {
        LogPrintf("L2TokenMinter: Supply invariant violated - sumOfBalances (%lld) != totalSupply (%lld)\n",
                  sumOfBalances, totalSupply_);
        return false;
    }
    
    return true;
}

CAmount L2TokenMinter::GetBalance(const uint160& address) const {
    uint256 key = AddressToKey(address);
    AccountState state = stateManager_.GetAccountState(key);
    return state.balance;
}

CAmount L2TokenMinter::GetTotalBurnedL1() const {
    return burnRegistry_.GetTotalBurned();
}

CAmount L2TokenMinter::GetTotalMintedL2() const {
    LOCK(cs_minter_);
    return totalMinted_;
}

void L2TokenMinter::RegisterMintEventCallback(MintEventCallback callback) {
    LOCK(cs_minter_);
    mintEventCallbacks_.push_back(std::move(callback));
}

std::vector<MintEvent> L2TokenMinter::GetMintEvents() const {
    LOCK(cs_minter_);
    return mintEvents_;
}

std::vector<MintEvent> L2TokenMinter::GetMintEventsForAddress(const uint160& recipient) const {
    LOCK(cs_minter_);
    
    std::vector<MintEvent> result;
    auto it = mintEventsByRecipient_.find(recipient);
    if (it != mintEventsByRecipient_.end()) {
        for (size_t idx : it->second) {
            if (idx < mintEvents_.size()) {
                result.push_back(mintEvents_[idx]);
            }
        }
    }
    return result;
}

std::optional<MintEvent> L2TokenMinter::GetMintEventByL1TxHash(const uint256& l1TxHash) const {
    LOCK(cs_minter_);
    
    auto it = mintEventsByL1TxHash_.find(l1TxHash);
    if (it != mintEventsByL1TxHash_.end() && it->second < mintEvents_.size()) {
        return mintEvents_[it->second];
    }
    return std::nullopt;
}

void L2TokenMinter::Clear() {
    LOCK(cs_minter_);
    
    totalSupply_ = 0;
    totalMinted_ = 0;
    currentBlockNumber_ = 0;
    mintEvents_.clear();
    mintEventsByL1TxHash_.clear();
    mintEventsByRecipient_.clear();
    // Note: Don't clear callbacks
}

void L2TokenMinter::LoadFromRegistry() {
    LOCK(cs_minter_);
    
    // Reset derived aggregates and rebuild them from the (already loaded) burn
    // registry. Each burn record corresponds 1:1 to a mint event.
    totalSupply_ = 0;
    totalMinted_ = 0;
    mintEvents_.clear();
    mintEventsByL1TxHash_.clear();
    mintEventsByRecipient_.clear();
    
    std::vector<BurnRecord> records = burnRegistry_.GetAllBurns();
    for (const auto& rec : records) {
        size_t idx = mintEvents_.size();
        mintEvents_.emplace_back(rec.l1TxHash, rec.l2Recipient, rec.amount,
                                 rec.l2MintTxHash, rec.l2MintBlock, rec.timestamp);
        mintEventsByL1TxHash_[rec.l1TxHash] = idx;
        mintEventsByRecipient_[rec.l2Recipient].push_back(idx);
        totalSupply_ += rec.amount;
        totalMinted_ += rec.amount;
    }
}

uint64_t L2TokenMinter::GetCurrentBlockNumber() const {
    LOCK(cs_minter_);
    return currentBlockNumber_;
}

void L2TokenMinter::SetCurrentBlockNumber(uint64_t blockNumber) {
    LOCK(cs_minter_);
    currentBlockNumber_ = blockNumber;
}

bool L2TokenMinter::UpdateState(const uint160& recipient, CAmount amount) {
    // Convert address to key for state manager
    uint256 key = AddressToKey(recipient);
    
    // Get current account state
    AccountState state = stateManager_.GetAccountState(key);
    
    // Add amount to balance
    // Requirement 4.2: Minted amount exactly equals burned amount (1:1)
    state.balance += amount;
    
    // Update last activity
    state.lastActivity = currentBlockNumber_ > 0 ? currentBlockNumber_ : stateManager_.GetBlockNumber();
    
    // Set updated state
    stateManager_.SetAccountState(key, state);
    
    return true;
}

void L2TokenMinter::EmitMintEvent(const MintEvent& event) {
    // Store event
    size_t idx = mintEvents_.size();
    mintEvents_.push_back(event);
    
    // Update indexes
    mintEventsByL1TxHash_[event.l1TxHash] = idx;
    mintEventsByRecipient_[event.recipient].push_back(idx);
    
    // Notify callbacks
    for (const auto& callback : mintEventCallbacks_) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LogPrintf("L2TokenMinter: Exception in mint event callback: %s\n", e.what());
        }
    }
}

uint256 L2TokenMinter::GenerateL2TxHash(
    const uint256& l1TxHash,
    const uint160& recipient,
    CAmount amount,
    uint64_t blockNumber) const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << std::string("L2MINT");
    ss << l1TxHash;
    ss << recipient;
    ss << amount;
    ss << blockNumber;
    return ss.GetHash();
}

// ============================================================================
// Shared L2 runtime singletons and burn-and-mint block processor
//
// The RPC layer (l2_getbalance, l2_gettotalsupply, l2_getburnstatus, ...) and
// the block-connection code below MUST share the same state manager, burn
// registry and minter so that mints are visible through the RPC interface.
// ============================================================================

L2StateManager& GetGlobalStateManager() {
    static L2StateManager stateManager(GetL2ChainId());
    return stateManager;
}

BurnRegistry& GetGlobalBurnRegistry() {
    static BurnRegistry burnRegistry;
    return burnRegistry;
}

L2TokenMinter& GetGlobalMinter() {
    static L2TokenMinter minter(GetGlobalStateManager(), GetGlobalBurnRegistry());
    return minter;
}

FraudProofSystem& GetGlobalFraudProofSystem() {
    static FraudProofSystem fps(GetL2ChainId());
    return fps;
}

namespace {
    /** A burn detected on L1 that is waiting for enough confirmations to mint. */
    struct TrackedBurn {
        BurnData burnData;
        int height;
        uint256 blockHash;
    };
    std::map<uint256, TrackedBurn> g_trackedBurns;
    CCriticalSection cs_trackedBurns;

    // ---- Persistence (LevelDB) ----
    // Key tags (see l2_globals.h for the schema).
    const char DB_L2_BURN    = 'B';  // ('B', l1TxHash)   -> BurnRecord
    const char DB_L2_ACCOUNT = 'A';  // ('A', addressKey) -> AccountState
    const char DB_L2_HEIGHT  = 'H';  // 'H'               -> int checkpoint

    const char DB_L2_BLOCK   = 'K';  // ('K', uint64 blockNumber) -> L2Block
    const char DB_L2_TIP     = 'T';  // 'T'                       -> uint64 tip number
    const char DB_L2_COMMIT  = 'C';  // ('C', uint64 l2BlockNumber) -> L2Commitment
    const char DB_L2_LATESTCOMMIT = 'L';  // 'L'                  -> uint64 latest committed L2 block

    // L2COMMIT OP_RETURN marker and payload layout.
    const char L2COMMIT_MARKER[] = "L2COMMIT";      // 8 bytes (no NUL)
    const size_t L2COMMIT_MARKER_SIZE = 8;
    const size_t L2COMMIT_PAYLOAD_SIZE = 8 + 4 + 8 + 32;  // marker+chainId+blockNum+stateRoot

    // L2FORCE OP_RETURN marker: a forced-inclusion transfer posted on L1 so the
    // sequencer cannot censor it. Layout: marker + serialized (signed) L2Transaction.
    // Because it carries a full signature it exceeds the default OP_RETURN size,
    // so L2-enabled nodes must run with a raised -datacarriersize.
    const char L2FORCE_MARKER[] = "L2FORCE";        // 7 bytes (no NUL)
    const size_t L2FORCE_MARKER_SIZE = 7;

    std::unique_ptr<CDBWrapper> g_l2db;
    int g_l2LastProcessedHeight = 0;
    CCriticalSection cs_l2db;

    // L2 block-chain tip cache (guarded by cs_l2Chain, defined below).
    uint64_t g_l2TipNumber = 0;
    uint256 g_l2TipHash;
    bool g_l2HasTip = false;

    /** Persist a single account's current state to the L2 DB. */
    void PersistAccountState(const uint160& addr) {
        LOCK(cs_l2db);
        if (!g_l2db) return;
        try {
            const uint256 k = AddressToKey(addr);
            const AccountState st = GetGlobalStateManager().GetAccountState(k);
            g_l2db->Write(std::make_pair(DB_L2_ACCOUNT, k), st);
        } catch (const std::exception& e) {
            LogPrintf("L2: PersistAccountState error: %s\n", e.what());
        }
    }

    /** Persist a produced L2 block and advance the persisted tip pointer. */
    void PersistL2BlockToDb(const L2Block& block) {
        LOCK(cs_l2db);
        if (!g_l2db) return;
        try {
            CDBBatch batch(*g_l2db);
            batch.Write(std::make_pair(DB_L2_BLOCK, (uint64_t)block.header.blockNumber), block);
            batch.Write(DB_L2_TIP, (uint64_t)block.header.blockNumber);
            g_l2db->WriteBatch(batch);
        } catch (const std::exception& e) {
            LogPrintf("L2: PersistL2BlockToDb error: %s\n", e.what());
        }
    }

    /** Persist a freshly minted burn: its BurnRecord and the recipient account. */
    void PersistMint(const uint256& l1TxHash, const uint160& recipient) {
        LOCK(cs_l2db);
        if (!g_l2db) return;
        try {
            auto rec = GetGlobalBurnRegistry().GetBurnRecord(l1TxHash);
            if (!rec) return;
            CDBBatch batch(*g_l2db);
            batch.Write(std::make_pair(DB_L2_BURN, l1TxHash), *rec);
            const uint256 accKey = AddressToKey(recipient);
            const AccountState st = GetGlobalStateManager().GetAccountState(accKey);
            batch.Write(std::make_pair(DB_L2_ACCOUNT, accKey), st);
            g_l2db->WriteBatch(batch);
        } catch (const std::exception& e) {
            LogPrintf("L2: PersistMint error: %s\n", e.what());
        }
    }

    /** Advance and persist the settled-height checkpoint (monotonic). */
    void PersistCheckpoint(int settledHeight) {
        LOCK(cs_l2db);
        if (!g_l2db) return;
        if (settledHeight <= g_l2LastProcessedHeight) return;
        try {
            g_l2db->Write(DB_L2_HEIGHT, settledHeight);
            g_l2LastProcessedHeight = settledHeight;
        } catch (const std::exception& e) {
            LogPrintf("L2: PersistCheckpoint error: %s\n", e.what());
        }
    }
} // namespace

void ProcessConnectedBlockForBurns(const CBlock& block, int height, int chainHeight) {
    if (!IsL2Enabled()) {
        return;
    }

    try {
        const uint32_t ourChainId = static_cast<uint32_t>(GetL2ChainId());
        BurnRegistry& registry = GetGlobalBurnRegistry();
        L2TokenMinter& minter = GetGlobalMinter();

        LOCK(cs_trackedBurns);

        // 1. Detect new burn transactions in this block.
        const uint256 blockHash = block.GetHash();
        for (const auto& tx : block.vtx) {
            auto burnOpt = BurnTransactionParser::ParseBurnTransaction(*tx);
            if (!burnOpt) {
                continue;
            }
            if (burnOpt->chainId != ourChainId) {
                continue;  // Burn destined for a different L2 chain
            }
            const uint256 txHash = tx->GetHash();
            if (registry.IsProcessed(txHash)) {
                continue;  // Already minted
            }
            if (g_trackedBurns.find(txHash) == g_trackedBurns.end()) {
                TrackedBurn tb;
                tb.burnData = *burnOpt;
                tb.height = height;
                tb.blockHash = blockHash;
                g_trackedBurns[txHash] = tb;
                LogPrintf("L2: Detected burn %s (amount=%d, chainId=%u) at height %d\n",
                          txHash.ToString().substr(0, 16), burnOpt->amount,
                          burnOpt->chainId, height);
            }
        }

        // 2. Mint any tracked burn that now has REQUIRED_CONFIRMATIONS.
        //    A confirmed L1 burn is an objective fact, so minting 1:1 is
        //    deterministic and needs no multi-sequencer voting.
        std::vector<uint256> done;
        for (auto& entry : g_trackedBurns) {
            const uint256& txHash = entry.first;
            const TrackedBurn& tb = entry.second;
            int confirmations = chainHeight - tb.height + 1;
            if (confirmations < REQUIRED_CONFIRMATIONS) {
                continue;
            }
            if (registry.IsProcessed(txHash)) {
                done.push_back(txHash);
                continue;
            }
            // Use the L1 height as the L2 mint block number so the burn record
            // is valid (l2MintBlock must be > 0).
            minter.SetCurrentBlockNumber(static_cast<uint64_t>(chainHeight));
            uint160 recipient = tb.burnData.GetRecipientAddress();
            MintResult result = minter.MintTokensWithDetails(
                txHash, static_cast<uint64_t>(tb.height), tb.blockHash,
                recipient, tb.burnData.amount);
            if (result.success) {
                LogPrintf("L2: Minted %d tokens to 0x%s for burn %s (confirmations=%d)\n",
                          tb.burnData.amount, recipient.GetHex(),
                          txHash.ToString().substr(0, 16), confirmations);
                // Persist the minted burn + recipient balance to LevelDB.
                PersistMint(txHash, recipient);
                done.push_back(txHash);
            } else {
                LogPrintf("L2: Mint failed for burn %s: %s\n",
                          txHash.ToString().substr(0, 16), result.errorMessage);
                // If it was already processed, stop tracking it.
                if (registry.IsProcessed(txHash)) {
                    done.push_back(txHash);
                }
            }
        }
        for (const uint256& txHash : done) {
            g_trackedBurns.erase(txHash);
        }

        // Advance the settled-height checkpoint. All burns at heights at or
        // below (chainHeight - REQUIRED_CONFIRMATIONS + 1) are guaranteed to be
        // matured and (if valid) minted, so the next startup can safely rescan
        // from checkpoint+1. The checkpoint lags the tip by REQUIRED_CONFIRMATIONS
        // so not-yet-matured burns are re-detected after a restart.
        int settled = chainHeight - REQUIRED_CONFIRMATIONS + 1;
        if (settled > 0) {
            PersistCheckpoint(settled);
        }
    } catch (const std::exception& e) {
        LogPrintf("L2: ProcessConnectedBlockForBurns exception: %s\n", e.what());
    } catch (...) {
        LogPrintf("L2: ProcessConnectedBlockForBurns unknown exception\n");
    }
}

void HandleBurnReorg(int height) {
    LOCK(cs_trackedBurns);
    std::vector<uint256> toRemove;
    for (const auto& entry : g_trackedBurns) {
        if (entry.second.height >= height) {
            toRemove.push_back(entry.first);
        }
    }
    for (const uint256& txHash : toRemove) {
        g_trackedBurns.erase(txHash);
    }
    if (!toRemove.empty()) {
        LogPrintf("L2: Dropped %u tracked burns at/above height %d due to reorg\n",
                  (unsigned)toRemove.size(), height);
    }
}

// ============================================================================
// Pending L2 transaction pool (Phase 1)
// ============================================================================

namespace {
    std::vector<L2Transaction> g_l2Mempool;
    std::set<uint256> g_l2MempoolHashes;
    CCriticalSection cs_l2Mempool;
    const size_t MAX_L2_MEMPOOL = 50000;
} // namespace

bool SubmitL2Transaction(const L2Transaction& tx, std::string& err) {
    if (!tx.ValidateStructure()) {
        err = "Invalid transaction structure";
        return false;
    }
    // Authentication: transfers must be signed by their declared sender.
    // (Defense in depth; ApplyL2Transaction re-checks at block application.)
    if (tx.type == L2TxType::TRANSFER && !tx.VerifySignature()) {
        err = "Invalid or missing signature";
        return false;
    }
    LOCK(cs_l2Mempool);
    if (g_l2Mempool.size() >= MAX_L2_MEMPOOL) {
        err = "L2 mempool full";
        return false;
    }
    const uint256 h = tx.GetHash();
    if (g_l2MempoolHashes.count(h)) {
        err = "Transaction already in pool";
        return false;
    }
    g_l2Mempool.push_back(tx);
    g_l2MempoolHashes.insert(h);
    return true;
}

std::vector<L2Transaction> GetPendingL2Transactions(size_t maxCount) {
    LOCK(cs_l2Mempool);
    std::vector<L2Transaction> out;
    for (const auto& tx : g_l2Mempool) {
        if (out.size() >= maxCount) break;
        out.push_back(tx);
    }
    return out;
}

void RemoveL2Transactions(const std::vector<uint256>& hashes) {
    LOCK(cs_l2Mempool);
    std::set<uint256> toRemove(hashes.begin(), hashes.end());
    std::vector<L2Transaction> kept;
    kept.reserve(g_l2Mempool.size());
    for (const auto& tx : g_l2Mempool) {
        const uint256 h = tx.GetHash();
        if (toRemove.count(h)) {
            g_l2MempoolHashes.erase(h);
        } else {
            kept.push_back(tx);
        }
    }
    g_l2Mempool.swap(kept);
}

size_t GetL2MempoolSize() {
    LOCK(cs_l2Mempool);
    return g_l2Mempool.size();
}

size_t GetPendingCountFromSender(const uint160& sender) {
    LOCK(cs_l2Mempool);
    size_t n = 0;
    for (const auto& tx : g_l2Mempool) {
        if (tx.from == sender) n++;
    }
    return n;
}

// ============================================================================
// Persistence: open/load, shutdown, checkpoint accessor
// ============================================================================

bool InitL2Persistence(const fs::path& dbPath, bool fWipe) {
    LOCK(cs_l2db);

    try {
        g_l2db.reset(new CDBWrapper(dbPath, 8 << 20, /*fMemory=*/false, /*fWipe=*/fWipe));
    } catch (const std::exception& e) {
        LogPrintf("L2: Failed to open persistence DB at %s: %s\n", dbPath.string(), e.what());
        g_l2db.reset();
        g_l2LastProcessedHeight = 0;
        return false;
    }

    g_l2LastProcessedHeight = 0;
    size_t nAccounts = 0, nBurns = 0;

    try {
        // Load the settled-height checkpoint.
        int storedHeight = 0;
        if (g_l2db->Read(DB_L2_HEIGHT, storedHeight) && storedHeight > 0) {
            g_l2LastProcessedHeight = storedHeight;
        }

        L2StateManager& sm = GetGlobalStateManager();
        BurnRegistry& reg = GetGlobalBurnRegistry();

        // Load persisted account states.
        {
            std::unique_ptr<CDBIterator> it(g_l2db->NewIterator());
            for (it->Seek(std::make_pair(DB_L2_ACCOUNT, uint256())); it->Valid(); it->Next()) {
                std::pair<char, uint256> key;
                if (!it->GetKey(key) || key.first != DB_L2_ACCOUNT) break;
                AccountState st;
                if (it->GetValue(st)) {
                    sm.SetAccountState(key.second, st);
                    nAccounts++;
                }
            }
        }

        // Load persisted burn records.
        {
            std::unique_ptr<CDBIterator> it(g_l2db->NewIterator());
            for (it->Seek(std::make_pair(DB_L2_BURN, uint256())); it->Valid(); it->Next()) {
                std::pair<char, uint256> key;
                if (!it->GetKey(key) || key.first != DB_L2_BURN) break;
                BurnRecord rec;
                if (it->GetValue(rec)) {
                    reg.RecordBurn(rec);
                    nBurns++;
                }
            }
        }

        // Rebuild the minter's derived aggregates from the loaded registry.
        GetGlobalMinter().LoadFromRegistry();

        // Load the L2 block-chain tip (for block queries and to continue
        // producing from the correct height/parent).
        uint64_t tipNum = 0;
        if (g_l2db->Read(DB_L2_TIP, tipNum)) {
            L2Block tipBlock;
            if (g_l2db->Read(std::make_pair(DB_L2_BLOCK, tipNum), tipBlock)) {
                g_l2TipNumber = tipNum;
                g_l2TipHash = tipBlock.GetHash();
                g_l2HasTip = true;
                LogPrintf("L2: Loaded L2 chain tip: block %llu (%s)\n",
                          (unsigned long long)tipNum, g_l2TipHash.ToString().substr(0, 16));
            }
        }
    } catch (const std::exception& e) {
        LogPrintf("L2: Error loading persisted state: %s\n", e.what());
        // Keep whatever loaded; rescan will fill any gap.
    }

    LogPrintf("L2: Persistence loaded %u accounts, %u burns (checkpoint height %d)\n",
              (unsigned)nAccounts, (unsigned)nBurns, g_l2LastProcessedHeight);
    return true;
}

void ShutdownL2Persistence() {
    LOCK(cs_l2db);
    g_l2db.reset();
}

int GetL2LastProcessedHeight() {
    LOCK(cs_l2db);
    return g_l2LastProcessedHeight;
}

// ============================================================================
// Sequencer block production (Phase 1: single sequencer)
// ============================================================================

namespace {
    CCriticalSection cs_l2Chain;              // guards the L2 chain tip + production
    std::atomic<bool> g_producerStop{true};
    std::atomic<bool> g_l2IsSequencer{false}; // whether this node produces blocks
    std::thread g_producerThread;

    const size_t MAX_TXS_PER_L2_BLOCK = 1000;

    /** Broadcast a produced/ingested L2 block to all L2-capable peers. */
    void BroadcastL2Block(const L2Block& block) {
        if (!g_connman) return;
        g_connman->ForEachNode([&](CNode* pnode) {
            if (pnode->fSuccessfullyConnected && (pnode->nServices & NODE_L2)) {
                g_connman->PushMessage(pnode,
                    CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::L2BLOCK, block));
            }
        });
    }

    /** Ask L2 peers for any blocks above our current tip (pull-based sync). */
    void RequestL2Blocks() {
        if (!g_connman) return;
        const uint64_t start = (uint64_t)GetL2BlockCount();  // next block we need
        const uint64_t end = start + 499;
        const uint64_t chainId = GetL2ChainId();
        g_connman->ForEachNode([&](CNode* pnode) {
            if (pnode->fSuccessfullyConnected && (pnode->nServices & NODE_L2)) {
                g_connman->PushMessage(pnode,
                    CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::L2GETBLOCKS, start, end, chainId));
            }
        });
    }

    /** This node's sequencer address (reuses the validator key). Null if none. */
    uint160 GetSequencerAddress() {
        if (CVM::g_validatorKeys && CVM::g_validatorKeys->HasValidatorKey()) {
            return CVM::g_validatorKeys->GetValidatorAddress();
        }
        return uint160();
    }

    uint64_t NowSeconds() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /** Produce a single L2 block from the pending pool, if there is work. */
    void ProduceOneBlock() {
        const uint160 seq = GetSequencerAddress();
        if (seq.IsNull()) {
            return;  // No sequencer key configured on this node.
        }

        // Read the L1 anchor first (own cs_main scope) to avoid nesting locks.
        uint64_t l1Height = 0;
        uint256 l1Hash;
        {
            LOCK(cs_main);
            if (chainActive.Tip()) {
                l1Height = (uint64_t)chainActive.Height();
                l1Hash = chainActive.Tip()->GetBlockHash();
            }
        }

        LOCK(cs_l2Chain);

        // Lazily create the genesis block if the L2 chain is empty.
        if (!g_l2HasTip) {
            L2Block genesis = CreateGenesisBlock(GetL2ChainId(), NowSeconds(), uint160());
            genesis.header.stateRoot = GetGlobalStateManager().GetStateRoot();
            PersistL2BlockToDb(genesis);
            g_l2TipNumber = 0;
            g_l2TipHash = genesis.GetHash();
            g_l2HasTip = true;
            LogPrintf("L2 producer: created genesis block (%s)\n",
                      g_l2TipHash.ToString().substr(0, 16));
        }

        // Leader gating (multi-sequencer): if more than one eligible sequencer
        // is known, only the elected leader for the next slot may produce. With
        // zero/one known sequencers (e.g. single-node regtest) we always produce.
        if (IsSequencerDiscoveryInitialized() && IsLeaderElectionInitialized()) {
            std::vector<SequencerInfo> eligible = GetSequencerDiscovery().GetEligibleSequencers();
            if (eligible.size() > 1) {
                LeaderElection& le = GetLeaderElection();
                le.SetLocalSequencerAddress(seq);
                uint64_t slot = le.GetSlotForBlock(g_l2TipNumber + 1);
                uint256 seed = le.GenerateElectionSeed(slot);
                LeaderElectionResult res = le.ElectLeader(slot, eligible, seed);
                if (res.isValid && res.leaderAddress != seq) {
                    return;  // Not our turn to produce this slot.
                }
            }
        }

        std::vector<L2Transaction> pending = GetPendingL2Transactions(MAX_TXS_PER_L2_BLOCK);
        if (pending.empty()) {
            return;
        }

        const uint64_t blockNum = g_l2TipNumber + 1;

        L2Block block;
        block.header.blockNumber = blockNum;
        block.header.parentHash = g_l2TipHash;
        block.header.l2ChainId = GetL2ChainId();
        block.header.sequencer = seq;
        block.header.timestamp = NowSeconds();
        block.header.gasLimit = 30000000;
        block.header.l1AnchorBlock = l1Height;
        block.header.l1AnchorHash = l1Hash;

        std::set<uint160> touched;
        std::vector<uint256> consumed;  // remove from pool (included or invalid)
        uint64_t gasUsed = 0;

        for (auto tx : pending) {
            consumed.push_back(tx.GetHash());
            TxExecutionResult r = GetGlobalStateManager().ApplyL2Transaction(tx, blockNum, seq);
            if (r.success) {
                tx.gasUsed = r.gasUsed;
                tx.success = true;
                block.transactions.push_back(tx);
                gasUsed += r.gasUsed;
                touched.insert(tx.from);
                touched.insert(tx.to);
                touched.insert(seq);
            } else {
                LogPrintf("L2 producer: dropping invalid tx %s: %s\n",
                          tx.GetHash().ToString().substr(0, 16), r.error);
            }
        }

        if (block.transactions.empty()) {
            // Everything was invalid; just drop them from the pool.
            RemoveL2Transactions(consumed);
            return;
        }

        block.header.gasUsed = gasUsed;
        block.header.stateRoot = GetGlobalStateManager().GetStateRoot();
        block.header.transactionsRoot = block.ComputeTransactionsRoot();

        // Sign the block with the sequencer key (enables Phase 2/3 verification).
        if (CVM::g_validatorKeys && CVM::g_validatorKeys->HasValidatorKey()) {
            std::vector<unsigned char> sig;
            const uint256 bh = block.GetHash();
            if (CVM::g_validatorKeys->Sign(bh, sig)) {
                block.AddSignature(SequencerSignature(seq, sig, NowSeconds()));
            }
        }
        // Single-sequencer: finalize immediately (multi-sequencer voting is Phase 2).
        block.isFinalized = true;

        // Persist the touched account balances and the block itself.
        for (const uint160& a : touched) {
            PersistAccountState(a);
        }
        PersistL2BlockToDb(block);

        g_l2TipNumber = blockNum;
        g_l2TipHash = block.GetHash();
        g_l2HasTip = true;

        RemoveL2Transactions(consumed);

        LogPrintf("L2 producer: produced block %llu with %u tx(s), stateRoot=%s, l1Anchor=%llu\n",
                  (unsigned long long)blockNum, (unsigned)block.transactions.size(),
                  block.header.stateRoot.ToString().substr(0, 16),
                  (unsigned long long)l1Height);

        // Gossip the new block to L2-capable peers.
        BroadcastL2Block(block);
    }

    void ProducerLoop(int intervalMs) {
        LogPrintf("L2 worker: thread started (interval=%dms, sequencer=%d)\n",
                  intervalMs, (int)g_l2IsSequencer.load());
        int tick = 0;
        while (!g_producerStop.load()) {
            try {
                // Sequencer: produce a block whenever there is pending work.
                if (g_l2IsSequencer.load() && GetL2MempoolSize() > 0) {
                    ProduceOneBlock();
                }
                // All L2 nodes: periodically request any blocks above our tip
                // from peers (pull-based sync). ~ every 3 seconds.
                if ((tick % (3000 / intervalMs == 0 ? 1 : 3000 / intervalMs)) == 0) {
                    RequestL2Blocks();
                }
            } catch (const std::exception& e) {
                LogPrintf("L2 worker: exception: %s\n", e.what());
            } catch (...) {
                LogPrintf("L2 worker: unknown exception\n");
            }
            tick++;
            int slept = 0;
            while (slept < intervalMs && !g_producerStop.load()) {
                MilliSleep(50);
                slept += 50;
            }
        }
        LogPrintf("L2 worker: thread stopped\n");
    }
} // namespace

void StartL2BlockProducer(bool isSequencer) {
    g_l2IsSequencer.store(isSequencer);
    if (g_producerThread.joinable()) {
        return;  // Already running.
    }
    g_producerStop.store(false);
    int intervalMs = (int)gArgs.GetArg("-l2blockintervalms", 1000);
    if (intervalMs < 100) intervalMs = 100;
    g_producerThread = std::thread(&ProducerLoop, intervalMs);
}

void StopL2BlockProducer() {
    g_producerStop.store(true);
    if (g_producerThread.joinable()) {
        g_producerThread.join();
    }
}

size_t GetL2BlockCount() {
    LOCK(cs_l2Chain);
    return g_l2HasTip ? (size_t)(g_l2TipNumber + 1) : 0;
}

bool GetL2BlockByNumber(uint64_t number, L2Block& out) {
    LOCK(cs_l2db);
    if (!g_l2db) return false;
    return g_l2db->Read(std::make_pair(DB_L2_BLOCK, number), out);
}

bool IngestL2Block(const L2Block& block, std::string& err) {
    LOCK(cs_l2Chain);
    const uint64_t num = block.header.blockNumber;

    // Genesis: adopt the sequencer's genesis if we have none yet.
    if (num == 0) {
        if (g_l2HasTip) return true;  // already have genesis (or beyond)
        PersistL2BlockToDb(block);
        g_l2TipNumber = 0;
        g_l2TipHash = block.GetHash();
        g_l2HasTip = true;
        LogPrintf("L2 sync: adopted genesis block (%s)\n", g_l2TipHash.ToString().substr(0, 16));
        return true;
    }

    if (!g_l2HasTip) { err = "no genesis yet"; return false; }
    if (num <= g_l2TipNumber) { return true; }          // already have it
    if (num != g_l2TipNumber + 1) { err = "gap"; return false; }
    if (block.header.parentHash != g_l2TipHash) { err = "parent mismatch"; return false; }

    // Apply the block's transactions to our local state. On a shared L1 the
    // mint-derived base state matches the sequencer's, so applying the same
    // transfers in order reproduces the balances.
    const uint160 seq = block.header.sequencer;
    std::set<uint160> touched;
    for (const auto& tx : block.transactions) {
        TxExecutionResult r = GetGlobalStateManager().ApplyL2Transaction(tx, num, seq);
        if (!r.success) {
            err = "tx apply failed: " + r.error;
            return false;  // cannot accept a block we cannot reproduce
        }
        touched.insert(tx.from);
        touched.insert(tx.to);
        touched.insert(seq);
    }

    for (const uint160& a : touched) {
        PersistAccountState(a);
    }
    PersistL2BlockToDb(block);
    g_l2TipNumber = num;
    g_l2TipHash = block.GetHash();
    g_l2HasTip = true;

    LogPrintf("L2 sync: ingested block %llu with %u tx(s)\n",
              (unsigned long long)num, (unsigned)block.transactions.size());
    return true;
}

// ============================================================================
// L1 anchoring: L2COMMIT commitments (Phase 1e)
// ============================================================================

CScript BuildL2CommitScript(uint32_t chainId, uint64_t l2BlockNumber, const uint256& stateRoot) {
    std::vector<unsigned char> payload;
    payload.reserve(L2COMMIT_PAYLOAD_SIZE);
    payload.insert(payload.end(), L2COMMIT_MARKER, L2COMMIT_MARKER + L2COMMIT_MARKER_SIZE);
    unsigned char b4[4];
    WriteLE32(b4, chainId);
    payload.insert(payload.end(), b4, b4 + 4);
    unsigned char b8[8];
    WriteLE64(b8, l2BlockNumber);
    payload.insert(payload.end(), b8, b8 + 8);
    payload.insert(payload.end(), stateRoot.begin(), stateRoot.end());

    CScript script;
    script << OP_RETURN << payload;
    return script;
}

std::optional<L2Commitment> ParseL2Commitment(const CTransaction& tx) {
    for (const auto& out : tx.vout) {
        const CScript& script = out.scriptPubKey;
        if (script.empty() || script[0] != OP_RETURN) {
            continue;
        }
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (!script.GetOp(pc, opcode) || opcode != OP_RETURN) continue;
        if (!script.GetOp(pc, opcode, data)) continue;
        if (data.size() != L2COMMIT_PAYLOAD_SIZE) continue;
        if (memcmp(data.data(), L2COMMIT_MARKER, L2COMMIT_MARKER_SIZE) != 0) continue;

        L2Commitment c;
        size_t off = L2COMMIT_MARKER_SIZE;
        c.chainId = ReadLE32(&data[off]); off += 4;
        c.l2BlockNumber = ReadLE64(&data[off]); off += 8;
        memcpy(c.stateRoot.begin(), &data[off], 32);
        return c;
    }
    return std::nullopt;
}

void ProcessConnectedBlockForCommits(const CBlock& block, int height) {
    if (!IsL2Enabled()) return;
    try {
        const uint32_t ourChainId = static_cast<uint32_t>(GetL2ChainId());
        for (const auto& tx : block.vtx) {
            auto c = ParseL2Commitment(*tx);
            if (!c) continue;
            if (c->chainId != ourChainId) continue;
            c->l1Height = (uint64_t)height;
            c->l1TxHash = tx->GetHash();

            LOCK(cs_l2db);
            if (!g_l2db) continue;
            CDBBatch batch(*g_l2db);
            batch.Write(std::make_pair(DB_L2_COMMIT, c->l2BlockNumber), *c);
            uint64_t latest = 0;
            g_l2db->Read(DB_L2_LATESTCOMMIT, latest);
            if (c->l2BlockNumber >= latest) {
                batch.Write(DB_L2_LATESTCOMMIT, c->l2BlockNumber);
            }
            g_l2db->WriteBatch(batch);
            LogPrintf("L2: Recorded L2COMMIT for L2 block %llu (stateRoot=%s) at L1 height %d\n",
                      (unsigned long long)c->l2BlockNumber,
                      c->stateRoot.ToString().substr(0, 16), height);

            // Open a fraud-proof challenge window for this committed state root.
            FraudProofSystem& fps = GetGlobalFraudProofSystem();
            uint64_t challengeSecs = (uint64_t)gArgs.GetArg("-l2challengeseconds", 3600);
            fps.RegisterStateRoot(c->stateRoot, c->l2BlockNumber, NowSeconds() + challengeSecs);
            L2Block committedBlock;
            if (GetL2BlockByNumber(c->l2BlockNumber, committedBlock) &&
                !committedBlock.header.sequencer.IsNull()) {
                // Register a nominal sequencer stake so slashing has effect.
                fps.SetSequencerStake(committedBlock.header.sequencer, 100 * COIN);
            }
        }
    } catch (const std::exception& e) {
        LogPrintf("L2: ProcessConnectedBlockForCommits exception: %s\n", e.what());
    } catch (...) {
        LogPrintf("L2: ProcessConnectedBlockForCommits unknown exception\n");
    }
}

bool GetL2Commitment(uint64_t l2BlockNumber, L2Commitment& out) {
    LOCK(cs_l2db);
    if (!g_l2db) return false;
    return g_l2db->Read(std::make_pair(DB_L2_COMMIT, l2BlockNumber), out);
}

bool GetLatestL2Commitment(L2Commitment& out) {
    LOCK(cs_l2db);
    if (!g_l2db) return false;
    uint64_t latest = 0;
    if (!g_l2db->Read(DB_L2_LATESTCOMMIT, latest)) return false;
    return g_l2db->Read(std::make_pair(DB_L2_COMMIT, latest), out);
}

// ============================================================================
// Forced inclusion (Phase 3b): censorship resistance via an L1 OP_RETURN.
// ============================================================================

CScript BuildL2ForceScript(const L2Transaction& l2tx) {
    std::vector<unsigned char> payload;
    payload.insert(payload.end(), L2FORCE_MARKER, L2FORCE_MARKER + L2FORCE_MARKER_SIZE);
    std::vector<unsigned char> ser = l2tx.Serialize();
    payload.insert(payload.end(), ser.begin(), ser.end());

    CScript script;
    script << OP_RETURN << payload;
    return script;
}

/** Parse an L2FORCE forced-transfer (a serialized signed L2Transaction). */
static std::optional<L2Transaction> ParseL2ForceTx(const CTransaction& tx) {
    for (const auto& out : tx.vout) {
        const CScript& script = out.scriptPubKey;
        if (script.empty() || script[0] != OP_RETURN) continue;
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (!script.GetOp(pc, opcode) || opcode != OP_RETURN) continue;
        if (!script.GetOp(pc, opcode, data)) continue;
        if (data.size() <= L2FORCE_MARKER_SIZE) continue;
        if (memcmp(data.data(), L2FORCE_MARKER, L2FORCE_MARKER_SIZE) != 0) continue;

        std::vector<unsigned char> ser(data.begin() + L2FORCE_MARKER_SIZE, data.end());
        L2Transaction l2tx;
        if (!l2tx.Deserialize(ser)) continue;
        l2tx.l1TxHash = tx.GetHash();
        return l2tx;
    }
    return std::nullopt;
}

void ProcessConnectedBlockForForced(const CBlock& block, int height) {
    if (!IsL2Enabled()) return;
    try {
        for (const auto& tx : block.vtx) {
            auto ftxOpt = ParseL2ForceTx(*tx);
            if (!ftxOpt) continue;
            L2Transaction ftx = *ftxOpt;
            // The forced tx is a signed L2Transaction delivered via L1; submit it
            // to the pool so the sequencer must include it. It is authenticated by
            // its own signature (verified when applied), so posting it on L1 does
            // not let anyone move another account's funds.
            std::string err;
            if (SubmitL2Transaction(ftx, err)) {
                LogPrintf("L2: Forced-inclusion tx from L1 %s queued (from=0x%s to=0x%s value=%d)\n",
                          tx->GetHash().ToString().substr(0, 16),
                          ftx.from.GetHex(), ftx.to.GetHex(), ftx.value);
            } else {
                LogPrintf("L2: Forced-inclusion tx from L1 %s rejected: %s\n",
                          tx->GetHash().ToString().substr(0, 16), err);
            }
        }
    } catch (const std::exception& e) {
        LogPrintf("L2: ProcessConnectedBlockForForced exception: %s\n", e.what());
    } catch (...) {
        LogPrintf("L2: ProcessConnectedBlockForForced unknown exception\n");
    }
}

// ============================================================================
// L1-reorg rollback of L2 state (Phase 3b)
//
// After processing each connected L1 block we snapshot the full L2 state
// (accounts + burn records + L2 chain tip), keyed by L1 height. On an L1 reorg
// (DisconnectBlock), we revert the L2 state to the snapshot at the fork point,
// undoing mints from orphaned L1 blocks and returning any orphaned L2-block
// transfers to the pool. The L1 chain remains the source of truth for mints.
// ============================================================================

namespace {
    struct L2ReorgSnapshot {
        std::map<uint256, AccountState> accounts;
        std::vector<BurnRecord> burns;
        std::map<uint256, TrackedBurn> trackedBurns;  // pending (not-yet-minted) burns
        uint64_t tipNumber = 0;
        uint256 tipHash;
        bool hasTip = false;
    };
    std::map<int, L2ReorgSnapshot> g_l2ReorgSnapshots;  // L1 height -> snapshot
    CCriticalSection cs_l2Reorg;
    const int MAX_L2_REORG_SNAPSHOTS = 300;

    /** Rewrite the persisted account/burn set + tip to exactly match a snapshot. */
    void ReconcilePersistenceToSnapshot(const L2ReorgSnapshot& snap) {
        LOCK(cs_l2db);
        if (!g_l2db) return;
        try {
            CDBBatch batch(*g_l2db);
            // Erase all persisted account entries.
            {
                std::unique_ptr<CDBIterator> it(g_l2db->NewIterator());
                for (it->Seek(std::make_pair(DB_L2_ACCOUNT, uint256())); it->Valid(); it->Next()) {
                    std::pair<char, uint256> k;
                    if (!it->GetKey(k) || k.first != DB_L2_ACCOUNT) break;
                    batch.Erase(k);
                }
            }
            // Erase all persisted burn entries.
            {
                std::unique_ptr<CDBIterator> it(g_l2db->NewIterator());
                for (it->Seek(std::make_pair(DB_L2_BURN, uint256())); it->Valid(); it->Next()) {
                    std::pair<char, uint256> k;
                    if (!it->GetKey(k) || k.first != DB_L2_BURN) break;
                    batch.Erase(k);
                }
            }
            // Rewrite from the snapshot.
            for (const auto& e : snap.accounts) {
                batch.Write(std::make_pair(DB_L2_ACCOUNT, e.first), e.second);
            }
            for (const auto& rec : snap.burns) {
                batch.Write(std::make_pair(DB_L2_BURN, rec.l1TxHash), rec);
            }
            if (snap.hasTip) {
                batch.Write(DB_L2_TIP, (uint64_t)snap.tipNumber);
            }
            g_l2db->WriteBatch(batch);
        } catch (const std::exception& e) {
            LogPrintf("L2: ReconcilePersistenceToSnapshot error: %s\n", e.what());
        }
    }
} // namespace

void SnapshotL2State(int l1Height) {
    if (!IsL2Enabled()) return;
    try {
        L2ReorgSnapshot snap;
        snap.accounts = GetGlobalStateManager().ExportAccounts();
        snap.burns = GetGlobalBurnRegistry().GetAllBurns();
        {
            LOCK(cs_trackedBurns);
            snap.trackedBurns = g_trackedBurns;
        }
        {
            LOCK(cs_l2Chain);
            snap.tipNumber = g_l2TipNumber;
            snap.tipHash = g_l2TipHash;
            snap.hasTip = g_l2HasTip;
        }
        LOCK(cs_l2Reorg);
        g_l2ReorgSnapshots[l1Height] = std::move(snap);
        while (g_l2ReorgSnapshots.size() > (size_t)MAX_L2_REORG_SNAPSHOTS) {
            g_l2ReorgSnapshots.erase(g_l2ReorgSnapshots.begin());
        }
    } catch (const std::exception& e) {
        LogPrintf("L2: SnapshotL2State error: %s\n", e.what());
    }
}

void HandleL1StateReorg(int forkHeight) {
    if (!IsL2Enabled()) return;
    if (forkHeight < 0) return;

    L2ReorgSnapshot snap;
    {
        LOCK(cs_l2Reorg);
        auto it = g_l2ReorgSnapshots.find(forkHeight);
        if (it == g_l2ReorgSnapshots.end()) {
            LogPrintf("L2: No L2 snapshot at L1 height %d; cannot precisely revert "
                      "(reorg deeper than snapshot window). L2 mint state will be "
                      "rebuilt from the new chain; transfers in that window are lost.\n",
                      forkHeight);
            return;
        }
        snap = it->second;
        // Drop snapshots above the fork point.
        for (auto i = g_l2ReorgSnapshots.begin(); i != g_l2ReorgSnapshots.end(); ) {
            if (i->first > forkHeight) i = g_l2ReorgSnapshots.erase(i); else ++i;
        }
    }

    // Collect transfers from L2 blocks that will be orphaned, to re-queue them.
    std::vector<L2Transaction> orphaned;
    {
        LOCK(cs_l2Chain);
        if (g_l2HasTip) {
            for (uint64_t n = snap.tipNumber + 1; n <= g_l2TipNumber; n++) {
                L2Block b;
                if (GetL2BlockByNumber(n, b)) {
                    for (const auto& tx : b.transactions) {
                        if (tx.type == L2TxType::TRANSFER) orphaned.push_back(tx);
                    }
                }
            }
        }
    }

    // Revert in-memory L2 state to the fork snapshot.
    GetGlobalStateManager().ImportAccounts(snap.accounts);
    {
        BurnRegistry& reg = GetGlobalBurnRegistry();
        reg.Clear();
        for (const auto& r : snap.burns) reg.RecordBurn(r);
    }
    GetGlobalMinter().LoadFromRegistry();
    {
        // Restore the pending (not-yet-minted) burn tracking so that, as the new
        // L1 chain reconnects blocks, matured burns are re-minted correctly.
        LOCK(cs_trackedBurns);
        g_trackedBurns = snap.trackedBurns;
    }
    {
        LOCK(cs_l2Chain);
        g_l2TipNumber = snap.tipNumber;
        g_l2TipHash = snap.tipHash;
        g_l2HasTip = snap.hasTip;
    }

    // Reconcile persistence to the snapshot so a restart is consistent.
    ReconcilePersistenceToSnapshot(snap);

    // Roll the burn checkpoint back so a restart re-scans the new chain's burns.
    {
        LOCK(cs_l2db);
        int newcp = forkHeight - REQUIRED_CONFIRMATIONS;
        if (newcp < 0) newcp = 0;
        if (g_l2db && newcp < g_l2LastProcessedHeight) {
            g_l2LastProcessedHeight = newcp;
            try { g_l2db->Write(DB_L2_HEIGHT, newcp); } catch (...) {}
        }
    }

    // Return orphaned transfers to the pool (the sequencer may re-include the
    // ones that are still valid against the reverted state).
    int requeued = 0;
    for (const auto& tx : orphaned) {
        std::string e;
        if (SubmitL2Transaction(tx, e)) requeued++;
    }

    LogPrintf("L2: Reverted L2 state to L1 fork height %d (L2 tip=%llu, %u accounts, "
              "%u burns); re-queued %d transfer(s)\n",
              forkHeight, (unsigned long long)snap.tipNumber,
              (unsigned)snap.accounts.size(), (unsigned)snap.burns.size(), requeued);
}

} // namespace l2
