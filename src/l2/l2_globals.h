// Copyright (c) 2024 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_L2_GLOBALS_H
#define CASCOIN_L2_GLOBALS_H

#include <fs.h>

/**
 * @file l2_globals.h
 * @brief Shared, process-wide L2 runtime singletons and the block-driven
 *        burn-and-mint processor.
 *
 * The RPC layer and the block-connection code must operate on the SAME
 * L2StateManager / BurnRegistry / L2TokenMinter instances, otherwise a mint
 * performed while connecting a block would be invisible to l2_getbalance /
 * l2_gettotalsupply (which historically each created their own file-static
 * singletons). These accessors centralize that state.
 *
 * ProcessConnectedBlockForBurns() implements the burn-and-mint pipeline:
 * it detects OP_RETURN "L2BURN" outputs, waits for REQUIRED_CONFIRMATIONS L1
 * confirmations, and then deterministically mints 1:1 L2 tokens to the burn
 * recipient. Because a confirmed L1 burn is an objective on-chain fact, the
 * mint is fully deterministic and does not require multi-sequencer voting;
 * this makes single-node / regtest operation work correctly.
 */

#include <l2/l2_transaction.h>
#include <l2/l2_block.h>
#include <uint256.h>
#include <serialize.h>
#include <script/script.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CBlock;
class CTransaction;

namespace l2 {

class L2StateManager;
class BurnRegistry;
class L2TokenMinter;
class FraudProofSystem;

/** Get the process-wide L2 state manager (balances / nonces). */
L2StateManager& GetGlobalStateManager();

/** Get the process-wide burn registry (processed burns / total burned). */
BurnRegistry& GetGlobalBurnRegistry();

/** Get the process-wide token minter, backed by the two singletons above. */
L2TokenMinter& GetGlobalMinter();

/**
 * @brief Process a freshly connected L1 block for burn-and-mint.
 *
 * Detects burn transactions in @p block and mints L2 tokens for any tracked
 * burn that has reached REQUIRED_CONFIRMATIONS confirmations relative to
 * @p chainHeight. Safe to call on every connected block; minting is idempotent
 * (guarded by the burn registry). Never throws.
 *
 * @param block        The block that was just connected.
 * @param height       The height of @p block.
 * @param chainHeight  The current chain tip height (== height on connect).
 */
void ProcessConnectedBlockForBurns(const CBlock& block, int height, int chainHeight);

/**
 * @brief Drop tracked (not-yet-minted) burns at or above @p height on reorg.
 *
 * Already-minted burns are intentionally left in place; full unwinding of
 * minted balances on deep reorgs is out of scope for this processor.
 */
void HandleBurnReorg(int height);

// ----------------------------------------------------------------------------
// Persistence (LevelDB)
//
// The burn-and-mint state is also persisted to a dedicated LevelDB so it does
// not have to be fully rebuilt from genesis on every restart. Persisted data:
//   ('B', l1TxHash)  -> BurnRecord     (one per minted burn)
//   ('A', addressKey)-> AccountState   (one per account with a balance)
//   'H'              -> int            (checkpoint: highest fully-settled height)
//
// The L1 chain remains the source of truth: on startup we load the persisted
// state and then replay only the blocks after the checkpoint (incremental
// rescan). The checkpoint lags the tip by REQUIRED_CONFIRMATIONS so that burns
// which had not yet matured at shutdown are re-detected on restart.
// ----------------------------------------------------------------------------

/**
 * @brief Open the L2 persistence DB and load persisted state into the shared
 *        state manager / burn registry / minter.
 * @param dbPath  Directory for the L2 LevelDB (e.g. <datadir>/l2).
 * @param fWipe   Wipe existing data (used with -reindex to rebuild from chain).
 * @return true on success. On failure persistence is disabled but the node can
 *         still operate (state is rebuilt purely by rescan).
 */
bool InitL2Persistence(const fs::path& dbPath, bool fWipe);

/**
 * @brief Close the L2 persistence DB (flushes LevelDB on destruction).
 */
void ShutdownL2Persistence();

/**
 * @brief Highest L1 height whose burns are fully settled and persisted.
 *        The startup rescan should begin at GetL2LastProcessedHeight() + 1.
 */
int GetL2LastProcessedHeight();

// ----------------------------------------------------------------------------
// Pending L2 transaction pool (Phase 1)
//
// A simple in-memory pool of pending native L2 transactions (transfers). The
// sequencer block producer drains it to build L2 blocks. Submitted via the
// l2_transfer / l2_sendtransaction RPCs and (later) via P2P.
// ----------------------------------------------------------------------------

/**
 * @brief Submit an L2 transaction to the pending pool.
 * @param tx   The transaction (must pass ValidateStructure()).
 * @param err  Set to a human-readable reason on failure.
 * @return true if accepted into the pool.
 */
bool SubmitL2Transaction(const L2Transaction& tx, std::string& err);

/** Broadcast a locally-originated L2 transaction to all NODE_L2 peers so it can
 *  reach a sequencer. (Peers relay it further on receipt.) Call this after
 *  SubmitL2Transaction for RPC-submitted transactions on non-sequencer nodes. */
void BroadcastL2Transaction(const L2Transaction& tx);

/**
 * @brief Re-gossip all still-pending L2 transactions to NODE_L2 peers.
 *
 * Called periodically (see -l2rebroadcastinterval) so a transaction submitted
 * while no sequencer was reachable is still delivered once connectivity is
 * restored - L2 transactions are otherwise only gossiped once, at submit time.
 * Transactions that have already been applied on L2 (nonce below the sender's
 * current account nonce) are pruned from the pool instead of being rebroadcast.
 *
 * @return the number of transactions rebroadcast.
 */
size_t RebroadcastL2Mempool();

/**
 * @brief Get up to @p maxCount pending transactions (FIFO order) for block
 *        production. Does not remove them from the pool.
 */
std::vector<L2Transaction> GetPendingL2Transactions(size_t maxCount);

/**
 * @brief Remove transactions (by hash) from the pool after inclusion in a block.
 */
void RemoveL2Transactions(const std::vector<uint256>& hashes);

/**
 * @brief Number of transactions currently pending.
 */
size_t GetL2MempoolSize();

/**
 * @brief Count pending transactions originating from @p sender (used to compute
 *        the correct next nonce when queueing several transfers before a block).
 */
size_t GetPendingCountFromSender(const uint160& sender);

// ----------------------------------------------------------------------------
// Sequencer block production + L2 block store (Phase 1)
// ----------------------------------------------------------------------------

/** Start the background L2 worker thread (produces blocks if @p isSequencer,
 *  and always performs pull-based block sync with peers). */
void StartL2BlockProducer(bool isSequencer);

/** Stop and join the worker thread. */
void StopL2BlockProducer();

/** Number of L2 blocks (tip height + 1), 0 if the chain is empty. */
size_t GetL2BlockCount();

/** Read an L2 block by number from the persistent store. */
bool GetL2BlockByNumber(uint64_t number, L2Block& out);

/** Verify that @p block carries a valid recoverable signature from the
 *  sequencer named in its header (header.sequencer). Recovers the signer's
 *  public key from the signature over the block hash and checks that its
 *  Hash160 equals header.sequencer. Genesis (block 0) needs no signature. */
bool VerifyL2BlockSignature(const L2Block& block);

/** Ingest an L2 block received from a peer (validate parent, verify the
 *  sequencer signature, re-execute transactions and verify the state root,
 *  then persist). Returns false (with @p err) if it does not extend our tip,
 *  has a bad signature/state root, or a transaction cannot be applied;
 *  err=="gap" signals missing ancestors. */
bool IngestL2Block(const L2Block& block, std::string& err);

// ----------------------------------------------------------------------------
// L1 anchoring: L2COMMIT commitments (Phase 1e)
//
// The sequencer periodically posts an L2 state-root commitment to L1 as an
// OP_RETURN output: "L2COMMIT" <chainId:4> <l2BlockNumber:8> <stateRoot:32>.
// These commitments anchor the L2 chain to L1 and are the basis for later
// fraud proofs / finalization.
// ----------------------------------------------------------------------------

/** A state-root commitment recorded from an L1 OP_RETURN. */
struct L2Commitment {
    uint32_t chainId = 0;
    uint64_t l2BlockNumber = 0;
    uint256 stateRoot;
    uint64_t l1Height = 0;   // L1 block height the commitment was mined at
    uint256 l1TxHash;        // L1 transaction hash carrying the commitment

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(chainId);
        READWRITE(l2BlockNumber);
        READWRITE(stateRoot);
        READWRITE(l1Height);
        READWRITE(l1TxHash);
    }
};

/** An on-chain sequencer registration recorded from an L1 L2SEQREG output.
 *  In the exclusion-based (burn-only) model the stake is CAS burned into the
 *  registration output; it is never returned. It provides Sybil resistance and
 *  a magnitude for prioritising honest sequencers, not a slashable deposit. */
struct SequencerRegistration {
    uint160 address;         // sequencer address (Hash160 of its pubkey)
    CAmount stake = 0;       // cumulative CAS burned into registrations
    bool active = false;     // false once deregistered or excluded
    bool excluded = false;   // true if excluded for provable misbehaviour (M3/M4)
    uint64_t l1Height = 0;   // L1 height of the (latest) registration
    uint256 l1TxHash;        // L1 tx hash of the (latest) registration

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(stake);
        READWRITE(active);
        READWRITE(excluded);
        READWRITE(l1Height);
        READWRITE(l1TxHash);
    }
};

/** Build the OP_RETURN commitment script for a given L2 block/state root. */
CScript BuildL2CommitScript(uint32_t chainId, uint64_t l2BlockNumber, const uint256& stateRoot);

// ----------------------------------------------------------------------------
// M2: On-chain sequencer registry (L2SEQREG)
//
// A sequencer registers by broadcasting an L1 transaction with an L2SEQREG
// OP_RETURN output whose value is CAS burned as stake (Sybil resistance). The
// payload proves control of the sequencer key. Deregistration / exclusion are
// also recorded on-chain, so the active sequencer set is objective and derived
// from L1 by every node.
// ----------------------------------------------------------------------------

/** Registry action encoded in an L2SEQREG output. */
enum class L2SeqRegAction : uint8_t { REGISTER = 0, DEREGISTER = 1 };

/** Build the OP_RETURN script for a sequencer registration/deregistration.
 *  The stake (burned) value is set on the CTxOut carrying this script. */
CScript BuildL2SeqRegScript(uint32_t chainId, L2SeqRegAction action,
                            const uint160& address, const CPubKey& pubkey,
                            const std::vector<unsigned char>& sig);

/** Hash signed by the sequencer to authorise a registry action. */
uint256 GetL2SeqRegSigHash(uint32_t chainId, L2SeqRegAction action, const uint160& address);

/** Scan a connected L1 block for L2SEQREG outputs and update the registry. */
void ProcessConnectedBlockForSeqReg(const CBlock& block, int height);

/** Read a sequencer's on-chain registration (returns false if never seen). */
bool GetSequencerRegistration(const uint160& address, SequencerRegistration& out);

/** All active (registered, not deregistered/excluded) sequencers. */
std::vector<SequencerRegistration> GetActiveSequencers();

/** True if @p address is a currently active on-chain sequencer. */
bool IsRegisteredSequencer(const uint160& address);

/** On-chain burned stake for @p address (0 if none / excluded). */
CAmount GetOnChainSequencerStake(const uint160& address);

/** Mark a sequencer excluded for provable misbehaviour (M3/M4). Persisted. */
void ExcludeSequencer(const uint160& address, const std::string& reason);

/** Parse an L2COMMIT commitment from a transaction (l1Height/l1TxHash unset). */
std::optional<L2Commitment> ParseL2Commitment(const CTransaction& tx);

/** Scan a connected L1 block for L2COMMIT commitments and record them. */
void ProcessConnectedBlockForCommits(const CBlock& block, int height);

/** Read a recorded commitment for an L2 block number. */
bool GetL2Commitment(uint64_t l2BlockNumber, L2Commitment& out);

/** Read the latest recorded commitment (highest committed L2 block). */
bool GetLatestL2Commitment(L2Commitment& out);

// ----------------------------------------------------------------------------
// M1: Data Availability (L2DATA)
//
// The sequencer posts each L2 block's full serialized bytes to L1 as one or
// more "L2DATA" OP_RETURN chunks so any node can reconstruct and independently
// verify the L2 chain from L1 alone (no data-withholding trust assumption).
// ----------------------------------------------------------------------------

/** Build the OP_RETURN scripts (one per chunk) carrying a block's data. */
std::vector<CScript> BuildL2DataScripts(const L2Block& block);

/** Scan a connected L1 block for L2DATA chunks; reassemble and, in L2-block
 *  order, reconstruct+apply any L2 blocks we are missing. */
void ProcessConnectedBlockForData(const CBlock& block, int height);

/** The next L2 block number whose data should be posted to L1 (sequencer). */
uint64_t GetNextL2DataPostBlock();

/** Persist the next-to-post pointer after posting a block's data to L1. */
void SetNextL2DataPostBlock(uint64_t nextBlock);

// ----------------------------------------------------------------------------
// Fraud proofs (Phase 3): process-wide fraud-proof system.
// Each recorded L1 commitment opens a challenge window (RegisterStateRoot).
// A challenger can prove a committed state root is invalid (differs from the
// honest re-execution), slashing the sequencer.
// ----------------------------------------------------------------------------

/** Get the process-wide fraud-proof system (lazily constructed). */
FraudProofSystem& GetGlobalFraudProofSystem();

// ----------------------------------------------------------------------------
// Forced inclusion (Phase 3b): censorship resistance via an L1 OP_RETURN.
// A user posts "L2FORCE" <from><to><value><nonce> on L1; every node queues it
// into the L2 pool on block connect, so the sequencer cannot censor it.
// ----------------------------------------------------------------------------

/** Build the OP_RETURN script embedding a signed forced-inclusion L2Transaction. */
CScript BuildL2ForceScript(const L2Transaction& l2tx);

/** Scan a connected L1 block for L2FORCE forced-inclusion transfers and queue them. */
void ProcessConnectedBlockForForced(const CBlock& block, int height);

// ----------------------------------------------------------------------------
// L1-reorg rollback (Phase 3b): snapshot L2 state per L1 height and revert on
// an L1 reorg so mints from orphaned L1 blocks are undone.
// ----------------------------------------------------------------------------

/** Snapshot the full L2 state after connecting the L1 block at @p l1Height. */
void SnapshotL2State(int l1Height);

/** Revert L2 state to the snapshot taken at L1 @p forkHeight (on L1 reorg). */
void HandleL1StateReorg(int forkHeight);

} // namespace l2

#endif // CASCOIN_L2_GLOBALS_H
