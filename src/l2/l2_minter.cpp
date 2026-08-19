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
#include <dbwrapper.h>
#include <fs.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <chrono>
#include <map>
#include <memory>
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
    
    // Requirement 8.3: Sum of all L2 balances == Total supply
    // Calculate sum of all balances from state manager
    CAmount sumOfBalances = 0;
    
    // Iterate through all mint events to get all recipients
    // and sum their balances
    std::set<uint160> allRecipients;
    for (const auto& event : mintEvents_) {
        allRecipients.insert(event.recipient);
    }
    
    for (const uint160& addr : allRecipients) {
        uint256 key = AddressToKey(addr);
        AccountState state = stateManager_.GetAccountState(key);
        sumOfBalances += state.balance;
    }
    
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

    std::unique_ptr<CDBWrapper> g_l2db;
    int g_l2LastProcessedHeight = 0;
    CCriticalSection cs_l2db;

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

} // namespace l2
