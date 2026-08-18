// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 10 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 23: "Write Workstream-10 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 10 = Storage / state sync & miscellaneous
 * (bugfix.md clauses 1.51–1.58; preservation 3.19, 3.20).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 17 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.51–2.58).
 *       Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 *       (unfixed) code — every failure is a counterexample confirming a defect.
 *       After the Workstream-10 fix lands (task 24) the SAME tests must pass.
 *       Prefixed `p17_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clauses 3.19, 3.20).
 *       These capture behaviour Workstream 10 must NOT change: already-loaded
 *       persisted state exposed unchanged (3.19) and out-of-scope detectors
 *       (bytecode-format detection) unchanged (3.20). EXPECTED TO PASS on the
 *       unfixed code and after the fix. Prefixed `preserve_`.
 *
 * ---------------------------------------------------------------------------
 * Defects under test and their testability:
 *
 *   1.51  cvmdb.cpp PruneReceipts — iterates receipts but reads keys as a
 *         std::pair<char,uint256> while WriteReceipt stores them as the string
 *         'R' + txhash.ToString(); the mismatched key decode means the derived
 *         delete key never matches a stored receipt, so nothing is deleted.
 *         COVERED (clean seam): write a receipt in a low block, PruneReceipts
 *         above it, assert HasReceipt == false. Unfixed: still present -> FAIL.
 *
 *   1.52  access_control_audit.cpp LoadBlacklist — a no-op that does not iterate
 *         the DB, so persisted blacklist entries are not restored on reload.
 *         COVERED (clean seam): persist an entry via one auditor, construct a
 *         second auditor on the SAME db, Initialize (calls LoadBlacklist),
 *         assert IsBlacklisted == true. Unfixed: false -> FAIL.
 *
 *   1.53  contract_state_sync.cpp HandleMetadataRequest — hardcodes
 *         storageSize = 0 (and chunkCount = 1) instead of counting real
 *         storage entries.
 *         COVERED (clean seam): deploy a contract, write N storage entries,
 *         request metadata, assert storageSize > 0. Unfixed: 0 -> FAIL.
 *
 *   1.54  metrics.cpp RecordOpcodeExecution — a no-op ("we skip detailed opcode
 *         tracking"), so per-opcode counts are never recorded.
 *         COVERED (clean seam): record an opcode N times, assert the per-opcode
 *         map reflects it. Unfixed: empty map -> FAIL.
 *
 *   1.55  backward_compat.cpp ReputationCompatChecker::VerifyScorePreservation —
 *         unconditionally returns true without querying data or comparing
 *         against tolerance.
 *         COVERED (seam-free logical contradiction): a single address cannot
 *         have a preserved score exactly equal to two different values with
 *         tolerance 0; a correct implementation returns true for at most one.
 *         Unfixed returns true for BOTH -> FAIL.
 *
 *   1.56  tx_priority.cpp ExtractSenderAddress — hashes the first input's
 *         prevout to synthesize a pseudo-address instead of extracting the real
 *         sender.
 *         COVERED (seam-free negative observable via CalculatePriority): seed a
 *         distinctive reputation at the prevout-hash pseudo-address; a correct
 *         implementation must NOT attribute that pseudo-address's reputation to
 *         the transaction. Unfixed uses the pseudo-address -> the seeded
 *         reputation drives priority -> FAIL.
 *         SEAM NOTE: the real fix resolves the sender via the UTXO set, which is
 *         unavailable at unit level; when the real address can't be resolved the
 *         fixed code returns no sender (reputation 0), which still satisfies the
 *         "not the prevout pseudo-address" assertion.
 *
 *   1.57  commit_reveal.cpp GetDisputeInfo — uses the dispute's createdTime as
 *         the commit-phase start instead of the explicit commitPhaseStart field.
 *         COVERED (clean seam via the virtual, test-overridable
 *         GetCurrentBlockHeight): store a dispute whose commitPhaseStart differs
 *         from createdTime and pin a height inside the explicit commit window;
 *         assert IsCommitPhase == true. Unfixed uses createdTime -> false -> FAIL.
 *
 *   1.58  clusterupdatehandler.cpp ProcessBlock — passes the first cluster's
 *         canonical address (a cluster id) as the merge linking address instead
 *         of the actual linking address.
 *         DEFERRED (no clean unit seam): the only path that reaches the merge is
 *         ProcessBlock -> DetectClusterMerges -> ExtractInputAddresses, which
 *         resolves input addresses via GetTransaction against the chain/txindex.
 *         BasicTestingSetup has no chain data, so no merges are detected and the
 *         buggy line is never reached. ProcessClusterMerge is private. This
 *         clause is deferred to the fix-verification / functional integration
 *         tests (tasks 24.2 / 30) where a real chain is available. See the
 *         placeholder documentation case below.
 *
 * Expected-Behavior targets: 2.51, 2.52, 2.53, 2.54, 2.55, 2.56, 2.57, 2.58
 * Preservation: 3.19, 3.20
 * Requirements: 1.51, 1.52, 1.53, 1.54, 1.55, 1.56, 1.57, 1.58, 3.19, 3.20
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/cvmdb.h>
#include <cvm/receipt.h>
#include <cvm/contract.h>
#include <cvm/access_control_audit.h>
#include <cvm/contract_state_sync.h>
#include <cvm/metrics.h>
#include <cvm/backward_compat.h>
#include <cvm/tx_priority.h>
#include <cvm/reputation.h>
#include <cvm/commit_reveal.h>
#include <cvm/trustgraph.h>
#include <cvm/bytecode_detector.h>

#include <amount.h>
#include <clientversion.h>
#include <fs.h>
#include <hash.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <utiltime.h>
#include <version.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// Fresh in-memory CVM database for a single test.
std::unique_ptr<CVM::CVMDatabase> MakeTempDb()
{
    fs::path testPath = fs::temp_directory_path() / fs::unique_path();
    return std::unique_ptr<CVM::CVMDatabase>(
        new CVM::CVMDatabase(testPath, 8 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

// Build a random non-null 20-byte address.
uint160 RandAddress()
{
    uint160 addr;
    uint256 r = InsecureRand256();
    std::memcpy(addr.begin(), r.begin(), 20);
    if (addr.IsNull()) *addr.begin() = 0x01;
    return addr;
}

// Test double that pins the "current block height" the commit-reveal manager
// sees. GetCurrentBlockHeight() is declared virtual precisely so tests can
// control the phase calculations deterministically.
class TestCommitRevealManager : public CVM::CommitRevealManager {
public:
    TestCommitRevealManager(CVM::CVMDatabase& db, const CVM::WoTConfig& cfg, uint32_t height)
        : CVM::CommitRevealManager(db, cfg), m_height(height) {}
    uint32_t GetCurrentBlockHeight() const override { return m_height; }
private:
    uint32_t m_height;
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream10_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 17) — EXPECTED TO FAIL on unfixed code      #
// ###########################################################################

// ===========================================================================
// Property 17 (1.51) — PruneReceipts actually deletes old receipts.
//
// Expected (2.51): after PruneReceipts(beforeBlock), every receipt whose
// blockNumber < beforeBlock SHALL be gone from the database.
//
// UNFIXED: PruneReceipts decodes the iterated key as std::pair<char,uint256>
// although WriteReceipt stores it as the string 'R' + txHash.ToString(); the
// mismatched key never matches a stored receipt, so nothing is deleted and the
// receipt survives -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_51_prune_receipts_deletes_old)
{
    auto db = MakeTempDb();

    // A receipt recorded at a low block height.
    uint256 txHash = InsecureRand256();
    CVM::TransactionReceipt receipt;
    receipt.transactionHash = txHash;
    receipt.blockNumber = 10;      // well below the prune horizon
    receipt.status = 1;
    receipt.gasUsed = 21000;
    BOOST_REQUIRE_MESSAGE(db->WriteReceipt(txHash, receipt),
        "P17 (1.51): precondition — writing a receipt should succeed.");
    BOOST_REQUIRE_MESSAGE(db->HasReceipt(txHash),
        "P17 (1.51): precondition — the receipt should be present before pruning.");

    // Prune everything below block 100 — this receipt (block 10) must go.
    BOOST_REQUIRE(db->PruneReceipts(100));

    BOOST_CHECK_MESSAGE(!db->HasReceipt(txHash),
        "P17 (1.51): a receipt from block 10 is STILL present after "
        "PruneReceipts(100); pruning does not actually delete old receipts.");
}

// ===========================================================================
// Property 17 (1.52) — LoadBlacklist restores persisted entries on reload.
//
// Expected (2.52): after the load routine completes, blacklist entries that
// were persisted to the database SHALL be restored and observable.
//
// UNFIXED: LoadBlacklist is a no-op that does not iterate the DB, so a fresh
// auditor over the same database does not see the persisted entry -> the
// property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_52_load_blacklist_restores_entries)
{
    auto db = MakeTempDb();

    uint160 badActor = RandAddress();

    // First auditor persists a permanent blacklist entry to the database.
    {
        CVM::AccessControlAuditor auditor1(*db);
        BOOST_REQUIRE(auditor1.Initialize(/*currentBlockHeight=*/1000));
        auditor1.AddToBlacklist(badActor, "sybil-abuse");
        BOOST_REQUIRE_MESSAGE(auditor1.IsBlacklisted(badActor),
            "P17 (1.52): precondition — the entry should be live in the first "
            "auditor immediately after AddToBlacklist.");
    }

    // A second auditor over the SAME database must restore the entry on load.
    CVM::AccessControlAuditor auditor2(*db);
    BOOST_REQUIRE(auditor2.Initialize(/*currentBlockHeight=*/1001));

    BOOST_CHECK_MESSAGE(auditor2.IsBlacklisted(badActor),
        "P17 (1.52): a persisted blacklist entry was NOT restored after "
        "re-initialising the auditor; LoadBlacklist does not iterate the "
        "database to restore persisted entries.");
}

// ===========================================================================
// Property 17 (1.53) — contract metadata reports actual storage size.
//
// Expected (2.53): a metadata response for a contract that has storage entries
// SHALL report the real storageSize (non-zero), not a hardcoded placeholder.
//
// UNFIXED: HandleMetadataRequest hardcodes storageSize = 0 (and chunkCount = 1)
// regardless of the contract's real storage -> the property FAILS
// (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_53_metadata_reports_storage_size)
{
    auto db = MakeTempDb();

    // Deploy a contract and give it real storage entries.
    uint160 contractAddr = RandAddress();
    CVM::Contract contract;
    contract.address = contractAddr;
    contract.deployer = RandAddress();
    contract.code = {0x01, 0x02, 0x03, 0x04};
    contract.deploymentHeight = 42;
    BOOST_REQUIRE_MESSAGE(db->WriteContract(contractAddr, contract),
        "P17 (1.53): precondition — writing the contract should succeed.");

    const int kEntries = 5;
    for (int i = 0; i < kEntries; ++i) {
        uint256 key = uint256S(std::to_string(1000 + i));
        uint256 value = uint256S(std::to_string(9000 + i));
        BOOST_REQUIRE_MESSAGE(db->Store(contractAddr, key, value),
            "P17 (1.53): precondition — writing a storage entry should succeed.");
    }

    CVM::ContractStateSyncManager mgr(db.get());
    CVM::ContractStateResponse response = mgr.HandleMetadataRequest({contractAddr});

    BOOST_REQUIRE_MESSAGE(response.metadata.size() == 1,
        "P17 (1.53): precondition — metadata response should contain the one "
        "requested contract.");

    BOOST_CHECK_MESSAGE(response.metadata[0].storageSize > 0,
        "P17 (1.53): contract metadata reports storageSize = 0 for a contract "
        "that has real storage entries; storage sizing is a hardcoded "
        "placeholder rather than an actual count.");
}

// ===========================================================================
// Property 17 (1.54) — RecordOpcodeExecution tracks per-opcode counts.
//
// Expected (2.54): recording an opcode execution SHALL increment that opcode's
// per-opcode counter.
//
// UNFIXED: RecordOpcodeExecution is a no-op ("we skip detailed opcode
// tracking"), so the per-opcode map stays empty -> the property FAILS
// (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_54_opcode_metrics_recorded)
{
    CVM::PrometheusMetricsExporter exporter;

    const uint8_t kOpcode = 0x51;   // representative opcode
    const int kTimes = 4;
    for (int i = 0; i < kTimes; ++i) {
        exporter.RecordOpcodeExecution(kOpcode);
    }

    const CVM::EVMExecutionMetrics& m = exporter.GetEVMMetrics();

    BOOST_CHECK_MESSAGE(m.opcodeExecutions.count(kOpcode) == 1,
        "P17 (1.54): RecordOpcodeExecution left the per-opcode map empty; "
        "per-opcode tracking is skipped entirely.");

    if (m.opcodeExecutions.count(kOpcode) == 1) {
        BOOST_CHECK_MESSAGE(m.opcodeExecutions.at(kOpcode).load() == (uint64_t)kTimes,
            "P17 (1.54): the recorded per-opcode count does not match the number "
            "of RecordOpcodeExecution calls.");
    }
}

// ===========================================================================
// Property 17 (1.55) — backward-compat score preservation actually compares.
//
// Expected (2.55): VerifyScorePreservation SHALL compare the address's real
// score against the expected value within tolerance, returning false when they
// differ beyond tolerance.
//
// Seam-free logical contradiction: a single address has exactly one preserved
// score X. With tolerance 0, "preserved == 10" requires X == 10 and
// "preserved == 90" requires X == 90; both cannot hold. A correct
// implementation returns true for AT MOST one of them.
//
// UNFIXED: VerifyScorePreservation returns true unconditionally, so it returns
// true for BOTH mutually-exclusive claims -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_55_backward_compat_compares_scores)
{
    CVM::ReputationCompatChecker checker;

    uint160 addr = RandAddress();

    bool preservedAt10 = checker.VerifyScorePreservation(addr, /*expected=*/10, /*tolerance=*/0);
    bool preservedAt90 = checker.VerifyScorePreservation(addr, /*expected=*/90, /*tolerance=*/0);

    BOOST_CHECK_MESSAGE(!(preservedAt10 && preservedAt90),
        "P17 (1.55): VerifyScorePreservation reported the SAME address's score "
        "as preserved-equal to two different values (10 and 90) with tolerance "
        "0; it returns true without querying data or comparing against "
        "tolerance.");
}

// ===========================================================================
// Property 17 (1.56) — sender extraction does not use a prevout-hash pseudo.
//
// Expected (2.56): the extracted sender SHALL be the real address, never a
// hash of the input's prevout. Observed through CalculatePriority, which looks
// up reputation for the extracted sender.
//
// Seam-free negative observable: seed a distinctive reputation for the exact
// prevout-hash pseudo-address the buggy code would synthesize. A correct
// implementation must not attribute that pseudo-address's reputation to the
// transaction.
//
// UNFIXED: ExtractSenderAddress returns the prevout-hash pseudo-address, so the
// seeded reputation drives the priority -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_56_sender_not_prevout_pseudo_address)
{
    auto db = MakeTempDb();

    // A transaction with a single input.
    CMutableTransaction mtx;
    CTxIn vin;
    vin.prevout = COutPoint(InsecureRand256(), 0);
    mtx.vin.push_back(vin);
    CTransaction tx(mtx);

    // Reproduce EXACTLY the pseudo-address the buggy code synthesizes:
    // hash(prevout)[0:20].
    CHashWriter hw(SER_GETHASH, 0);
    hw << tx.vin[0].prevout;
    uint256 pseudoHash = hw.GetHash();
    uint160 pseudoAddr;
    std::memcpy(pseudoAddr.begin(), pseudoHash.begin(), 20);

    // Seed a distinctive high reputation at the pseudo-address only.
    // score.score / 100 => priority.reputation, so 9500 => 95 (CRITICAL).
    CVM::ReputationSystem rep(*db);
    CVM::ReputationScore score;
    score.address = CVM::TrustNodeId::FromLegacyUint160(pseudoAddr);
    score.score = 9500;
    score.voteCount = 5;
    score.lastUpdated = GetTime();
    score.category = "seeded-pseudo";
    BOOST_REQUIRE_MESSAGE(rep.UpdateReputation(pseudoAddr, score),
        "P17 (1.56): precondition — seeding reputation at the pseudo-address "
        "should succeed.");

    CVM::TransactionPriorityManager mgr;
    CVM::TransactionPriorityManager::TransactionPriority priority =
        mgr.CalculatePriority(tx, *db);

    BOOST_CHECK_MESSAGE(priority.reputation != 95,
        "P17 (1.56): the transaction's priority reputation matches the "
        "reputation seeded at the prevout-hash pseudo-address (95); the sender "
        "is being derived from a prevout hash rather than the real address.");
}

// ===========================================================================
// Property 17 (1.57) — commit-phase start comes from the explicit field.
//
// Expected (2.57): the commit phase SHALL be computed from the dispute's
// explicit commitPhaseStart field, not its createdTime.
//
// UNFIXED: GetDisputeInfo sets commitPhaseStart = dispute.createdTime, so a
// dispute whose explicit commitPhaseStart brackets the current height is NOT
// recognised as in-commit-phase -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_57_commit_phase_uses_explicit_field)
{
    auto db = MakeTempDb();

    CVM::WoTConfig config;                 // commitPhaseDuration = 720, enableCommitReveal = true
    const uint32_t kHeight = 1000;

    // Dispute whose EXPLICIT commit window [800, 800+720) contains height 1000,
    // but whose createdTime (500000) is far away.
    CVM::DAODispute dispute;
    dispute.disputeId = InsecureRand256();
    dispute.createdTime = 500000;          // stand-in the buggy code (wrongly) uses
    dispute.commitPhaseStart = 800;        // the explicit field the fix must use
    dispute.revealPhaseStart = 0;
    dispute.useCommitReveal = true;

    // Persist under the exact key GetDisputeInfo reads.
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << dispute;
    std::vector<uint8_t> data(ss.begin(), ss.end());
    std::string key = "dispute_" + dispute.disputeId.GetHex();
    BOOST_REQUIRE_MESSAGE(db->WriteGeneric(key, data),
        "P17 (1.57): precondition — persisting the dispute should succeed.");

    TestCommitRevealManager mgr(*db, config, kHeight);

    BOOST_CHECK_MESSAGE(mgr.IsCommitPhase(dispute.disputeId),
        "P17 (1.57): the dispute is NOT recognised as in commit phase even "
        "though height 1000 is inside its explicit commit window [800, 1520); "
        "the commit-phase start is taken from createdTime instead of the "
        "explicit commitPhaseStart field.");
}

// ===========================================================================
// Property 17 (1.58) — cluster-merge linking address is the real one.
//
// DEFERRED: no clean unit seam. The buggy line (linkingAddress = the first
// cluster's canonical id) is only reachable through
// ClusterUpdateHandler::ProcessBlock -> DetectClusterMerges ->
// ExtractInputAddresses, which resolves input addresses via GetTransaction
// against the chain / transaction index. BasicTestingSetup provides no chain
// data, so ExtractInputAddresses returns nothing, no merge is detected, and the
// buggy line is never executed. ProcessClusterMerge (which consumes the linking
// address) is private, so it cannot be exercised directly either.
//
// This clause is validated by the fix-verification / functional integration
// tests (tasks 24.2 / 30) where a real chain with spendable UTXOs makes the
// merge path reachable and the emitted CLUSTER_MERGE event's linking address
// observable. The check below documents the deferral (and confirms the handler
// constructs cleanly) without asserting the un-testable behaviour.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p17_1_58_cluster_merge_linking_address_deferred)
{
    // Documentation-only: 1.58 has no clean unit seam (see comment above).
    // Deferred to fix-verification / functional integration.
    BOOST_CHECK_MESSAGE(true,
        "P17 (1.58): DEFERRED — cluster-merge linking address requires a chain "
        "with spendable UTXOs (ExtractInputAddresses -> GetTransaction) to reach "
        "the merge path; validated in functional integration (task 30).");
}

// ###########################################################################
// #  PRESERVATION TESTS (Property 21 / 3.19, 3.20) — PASS on unfixed          #
// ###########################################################################

// ===========================================================================
// Preservation 3.19 — already-loaded persisted state exposed unchanged.
//
// Reputation, trust-graph, and blacklist state that is written and read within
// an already-loaded path must keep working exactly as before. Workstream 10
// only fixes the RELOAD-from-disk routines; the live write/read paths are out
// of scope and must remain unchanged. EXPECTED: PASS now and after the fix.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_19_loaded_state_unchanged)
{
    auto db = MakeTempDb();

    // --- Reputation: a written record is read back unchanged.
    {
        CVM::ReputationSystem rep(*db);
        uint160 addr = RandAddress();

        CVM::ReputationScore score;
        score.address = CVM::TrustNodeId::FromLegacyUint160(addr);
        score.score = 4200;
        score.voteCount = 7;
        score.lastUpdated = GetTime();
        score.category = "normal";
        score.totalTransactions = 12;
        score.totalVolume = 34 * COIN;
        BOOST_REQUIRE(rep.UpdateReputation(addr, score));

        CVM::ReputationScore got;
        BOOST_REQUIRE_MESSAGE(rep.GetReputation(addr, got),
            "Preservation 3.19: a written reputation record must be readable.");
        BOOST_CHECK_EQUAL(got.score, (int64_t)4200);
        BOOST_CHECK_EQUAL(got.voteCount, (uint64_t)7);
    }

    // --- Trust graph: a written edge is exposed unchanged via GetTrustEdge.
    {
        CVM::TrustGraph graph(*db);
        uint160 from = RandAddress();
        uint160 to = RandAddress();
        BOOST_REQUIRE(graph.AddTrustEdge(from, to, 55, COIN * 10, uint256(), "ws10-preserve"));

        CVM::TrustEdge edge;
        BOOST_REQUIRE_MESSAGE(graph.GetTrustEdge(from, to, edge),
            "Preservation 3.19: a written trust edge must be readable.");
        BOOST_CHECK_MESSAGE(edge.trustWeight == 55,
            "Preservation 3.19: a written trust edge must be exposed unchanged "
            "through GetTrustEdge.");
    }

    // --- Blacklist: a live entry within an already-initialised auditor is
    //     exposed unchanged (this path does NOT depend on the LoadBlacklist fix).
    {
        CVM::AccessControlAuditor auditor(*db);
        BOOST_REQUIRE(auditor.Initialize(/*currentBlockHeight=*/2000));
        uint160 addr = RandAddress();
        auditor.AddToBlacklist(addr, "test-preserve");
        BOOST_CHECK_MESSAGE(auditor.IsBlacklisted(addr),
            "Preservation 3.19: a live blacklist entry must be exposed unchanged "
            "within the same, already-loaded auditor instance.");
    }
}

// ===========================================================================
// Preservation 3.20 — out-of-scope bytecode-format detection unchanged.
//
// Bytecode-format detection (BytecodeDetector) is explicitly out of scope for
// Workstream 10 and must keep producing its current, deterministic results.
// We pin determinism (identical result on identical input) and the stable
// classification of an empty input as UNKNOWN. EXPECTED: PASS now and after the
// fix.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_20_bytecode_format_detection_unchanged)
{
    CVM::BytecodeDetector detector;

    // Empty bytecode is not a valid, classifiable program.
    {
        std::vector<uint8_t> empty;
        CVM::BytecodeDetectionResult r = detector.DetectFormat(empty);
        BOOST_CHECK_MESSAGE(r.format == CVM::BytecodeFormat::UNKNOWN,
            "Preservation 3.20: empty bytecode must classify as UNKNOWN.");
    }

    // Determinism: repeated detection on identical input yields identical
    // format and confidence.
    {
        // A representative EVM-style prologue (PUSH1 0x60 PUSH1 0x40 MSTORE ...).
        std::vector<uint8_t> code = {0x60, 0x60, 0x60, 0x40, 0x52, 0x34, 0x80, 0x15};
        CVM::BytecodeDetectionResult r1 = detector.DetectFormat(code);
        CVM::BytecodeDetectionResult r2 = detector.DetectFormat(code);
        BOOST_CHECK_MESSAGE(r1.format == r2.format,
            "Preservation 3.20: bytecode-format detection is non-deterministic "
            "in its format classification on identical input.");
        BOOST_CHECK_MESSAGE(r1.confidence == r2.confidence,
            "Preservation 3.20: bytecode-format detection is non-deterministic "
            "in its confidence on identical input.");
    }
}

BOOST_AUTO_TEST_SUITE_END()
