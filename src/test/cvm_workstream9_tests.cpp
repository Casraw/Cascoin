// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 9 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 21: "Write Workstream-9 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 9 = Sybil-resistance & fraud detection
 * (bugfix.md clauses 1.13, 1.14, 1.48, 1.49, 1.50; preservation 3.20).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 16 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.13, 2.14,
 *       2.48, 2.49, 2.50). Written BEFORE the fix, they are EXPECTED TO FAIL on
 *       the current (unfixed) code — every failure is a counterexample
 *       confirming a defect. After the Workstream-9 fix lands (task 22) the
 *       SAME tests must pass. Prefixed `p16_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clause 3.20). These
 *       capture behaviour Workstream 9 must NOT change: trust-graph
 *       manipulation detection and behavior-metric scoring — paths the audit
 *       verified functional and declared out of scope. They are EXPECTED TO
 *       PASS on the unfixed code (baseline behaviour to preserve) and after the
 *       fix. Prefixed `preserve_`.
 *
 * ---------------------------------------------------------------------------
 * Defects under test and their testability:
 *
 *   1.13  walletcluster.cpp — the common-input-ownership clustering path
 *         (BuildClusters -> AnalyzeTransaction) produces no clusters because it
 *         has no transaction data to operate on, so addresses that share
 *         transaction inputs are never grouped.
 *         COVERED (observable): after BuildClusters, GetClusterMembers for an
 *         address that should be clustered returns only itself (empty grouping).
 *         SEAM NOTE: the automatic heuristic reads whole-chain transaction data
 *         (chainActive / a transaction index). The unit harness has no active
 *         chain, so BuildClusters is a no-op here for the SAME root cause the
 *         fix addresses (no transaction/address index). The manual LinkAddresses
 *         path already works and is exercised as a preservation anchor.
 *
 *   1.50  walletcluster.cpp WalletClusterer::GetAddressTransactions returns an
 *         empty list (placeholder for a transaction index), so common-input
 *         clustering has no data to operate on.
 *         COVERED (observable): GetAddressTransactions is a PRIVATE helper with
 *         no public seam and is currently unused; its defect is observed through
 *         its downstream effect — clustering finds zero clusters for addresses
 *         that transact. Asserted via GetTotalClusters() == 0 after BuildClusters.
 *
 *   1.14  reputation.cpp PatternDetector::DetectRapidFire(address, blockHeight)
 *         unconditionally returns false regardless of transaction history.
 *         COVERED (stub confirmation): the static, state-free signature has no
 *         injectable data seam at unit level, so the exploratory test asserts
 *         the post-fix contract (a rapid-fire scenario is detected) which fails
 *         now because the function is hardwired to false.
 *         SEAM NOTE: seeding a genuine rapid-fire history depends on the data
 *         source the fix chooses (transaction index / mempool). Task 22 aligns
 *         the seeding to that source when the fix lands.
 *
 *   1.48  reputation.cpp PatternDetector::DetectExchangePattern(address)
 *         unconditionally returns false regardless of transaction volume.
 *         COVERED (stub confirmation): same static, state-free-signature
 *         limitation as 1.14; the exploratory test asserts the post-fix contract
 *         (an exchange-like volume is detected) which fails now.
 *
 *   1.49  reputation.cpp ReputationSystem::GetLowReputationAddresses (the
 *         "GetAddressesWithReputation" index accessor of clause 1.49) returns an
 *         empty vector because no reputation index is maintained.
 *         COVERED (clean seam): seed real reputation records via
 *         UpdateReputation, then the accessor must return the seeded addresses.
 *         Unfixed returns empty -> fails; the fix maintains an index -> passes.
 *
 * Expected-Behavior targets: 2.13, 2.14, 2.48, 2.49, 2.50
 * Preservation: 3.20
 * Requirements: 1.13, 1.14, 1.48, 1.49, 1.50, 3.20
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/cvmdb.h>
#include <cvm/reputation.h>
#include <cvm/walletcluster.h>
#include <cvm/trustgraph.h>
#include <cvm/trust_graph_manipulation_detector.h>
#include <cvm/behaviormetrics.h>

#include <amount.h>
#include <fs.h>
#include <uint256.h>
#include <utiltime.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <set>
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

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream9_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 16) — EXPECTED TO FAIL on unfixed code      #
// ###########################################################################

// ===========================================================================
// Property 16 (1.49) — reputation index accessor returns addresses that have
// reputation records.
//
// Expected (2.49): GetAddressesWithReputation (here GetLowReputationAddresses)
// SHALL return the addresses that have reputation records, backed by a
// maintained index. Necessary condition: after real reputation records exist,
// the accessor must be NON-empty.
//
// UNFIXED: GetLowReputationAddresses returns an empty vector because no index is
// maintained -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p16_1_49_reputation_index_nonempty)
{
    auto db = MakeTempDb();
    CVM::ReputationSystem rep(*db);

    // Seed real reputation records with clearly-low scores (below the default
    // -5000 threshold) so they should appear in the low-reputation index.
    const int kRecords = 6;
    std::set<uint160> seeded;
    for (int i = 0; i < kRecords; ++i) {
        uint160 addr = RandAddress();
        seeded.insert(addr);

        CVM::ReputationScore score;
        score.address = addr;
        score.score = -8000;             // well below the -5000 threshold
        score.voteCount = 3;
        score.lastUpdated = GetTime();
        score.category = "scam";
        score.totalTransactions = 10;
        score.totalVolume = 100 * COIN;

        BOOST_REQUIRE_MESSAGE(rep.UpdateReputation(addr, score),
            "P16 (1.49): precondition — writing a reputation record should succeed.");
    }

    // Sanity: the records are individually retrievable (the write path works).
    for (const uint160& addr : seeded) {
        CVM::ReputationScore got;
        BOOST_REQUIRE_MESSAGE(rep.GetReputation(addr, got),
            "P16 (1.49): precondition — a written reputation record should be readable.");
    }

    std::vector<uint160> withRep = rep.GetLowReputationAddresses();

    BOOST_CHECK_MESSAGE(!withRep.empty(),
        "P16 (1.49): GetLowReputationAddresses returned an EMPTY list after real "
        "reputation records were written; it does not maintain a reputation index.");
}

// ===========================================================================
// Property 16 (1.13) — common-input clustering groups addresses that share
// transaction inputs.
//
// Expected (2.13): the common-input-ownership heuristic SHALL group addresses
// that share transaction inputs. Necessary condition: after clustering runs,
// addresses that co-spend must land in the SAME cluster (cluster size > 1).
//
// UNFIXED: the automatic clustering path (BuildClusters -> AnalyzeTransaction)
// has no transaction data (GetAddressTransactions returns empty; no active
// chain / transaction index), so it groups nothing -> GetClusterMembers returns
// only the queried address -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p16_1_13_common_input_clustering)
{
    auto db = MakeTempDb();
    CVM::WalletClusterer clusterer(*db);

    // Two addresses that co-spend inputs in the same tx and therefore belong to
    // the same wallet under the common-input heuristic.
    uint160 addrA = RandAddress();
    uint160 addrB = RandAddress();

    // Seed the transaction/address index with a real co-spend: a transaction
    // whose inputs are BOTH addrA and addrB. This is the data source the fix
    // makes clustering consult (GetAddressTransactions -> transaction index).
    uint256 coSpendTx = InsecureRand256();
    clusterer.RecordTransactionInputs(coSpendTx, {addrA, addrB});

    // Run the automatic, data-driven clustering (NOT the manual LinkAddresses
    // path). This is the path the fix must make functional.
    clusterer.BuildClusters();

    std::set<uint160> membersA = clusterer.GetClusterMembers(addrA);

    BOOST_CHECK_MESSAGE(membersA.count(addrB) == 1,
        "P16 (1.13): after BuildClusters, address A's cluster does NOT contain "
        "co-spending address B; the common-input-ownership heuristic produced no "
        "grouping (empty clustering) because it has no transaction data to "
        "operate on.");
}

// ===========================================================================
// Property 16 (1.50) — address transaction lookup yields data for clustering.
//
// Expected (2.50): GetTransactionsForAddress (here GetAddressTransactions) SHALL
// return the address's transactions from a transaction index so clustering can
// operate on real data. GetAddressTransactions is a PRIVATE helper with no
// public seam, so its defect is observed through its downstream effect:
// clustering over addresses that transact must discover at least one cluster.
//
// UNFIXED: GetAddressTransactions returns an empty list (transaction-index
// placeholder), so BuildClusters discovers zero clusters -> the property FAILS
// (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p16_1_50_address_tx_lookup_feeds_clustering)
{
    auto db = MakeTempDb();
    CVM::WalletClusterer clusterer(*db);

    // Seed the transaction/address index with a transaction that co-spends two
    // addresses' inputs. GetAddressTransactions must then return this tx and
    // feed the common-input heuristic, producing at least one cluster.
    uint160 addrA = RandAddress();
    uint160 addrB = RandAddress();
    uint256 coSpendTx = InsecureRand256();
    clusterer.RecordTransactionInputs(coSpendTx, {addrA, addrB});

    clusterer.BuildClusters();

    BOOST_CHECK_MESSAGE(clusterer.GetTotalClusters() > 0,
        "P16 (1.50): BuildClusters discovered ZERO clusters; the underlying "
        "address transaction lookup (GetAddressTransactions) returns an empty "
        "list, so common-input clustering has no data to operate on.");
}

// ===========================================================================
// Property 16 (1.14) — rapid-fire abuse detection responds to history.
//
// Expected (2.14): DetectRapidFire SHALL return true when the address's
// transaction history matches the rapid-fire pattern and false otherwise.
// Necessary condition: at least one rapid-fire scenario must be detected.
//
// UNFIXED: DetectRapidFire unconditionally returns false regardless of history
// -> the property FAILS (counterexample).
//
// SEAM NOTE: the static (address, blockHeight) signature consults no injectable
// state at unit level; task 22 aligns the scenario seeding to the data source
// the fix selects.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p16_1_14_rapid_fire_detected)
{
    auto db = MakeTempDb();

    // An address exhibiting a rapid-fire spend pattern at a given height.
    uint160 rapidFireAddr = RandAddress();
    const int blockHeight = 100000;

    // Seed a genuine rapid-fire history: 6 transactions packed into 3 blocks,
    // recorded into the transaction-history index the fix makes DetectRapidFire
    // consult. This is well inside the rapid-fire window.
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 99998, *db);
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 99998, *db);
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 99999, *db);
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 99999, *db);
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 100000, *db);
    CVM::PatternDetector::RecordAddressActivity(rapidFireAddr, 100000, *db);

    bool detected = CVM::PatternDetector::DetectRapidFire(rapidFireAddr, blockHeight, *db);

    BOOST_CHECK_MESSAGE(detected,
        "P16 (1.14): DetectRapidFire returned false for a rapid-fire scenario; "
        "it is hardwired to return false regardless of transaction history.");
}

// ===========================================================================
// Property 16 (1.48) — exchange-pattern detection responds to volume.
//
// Expected (2.48): DetectExchangePattern SHALL return true when the address's
// transaction volume matches the exchange pattern and false otherwise.
// Necessary condition: at least one exchange-like scenario must be detected.
//
// UNFIXED: DetectExchangePattern unconditionally returns false regardless of
// volume -> the property FAILS (counterexample).
//
// SEAM NOTE: the static (address)-only signature consults no injectable state at
// unit level; task 22 aligns the scenario seeding to the data source the fix
// selects (e.g. the reputation record's volume/transaction counts).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p16_1_48_exchange_pattern_detected)
{
    auto db = MakeTempDb();
    CVM::ReputationSystem rep(*db);

    // An address exhibiting exchange-like (very high) transaction volume.
    uint160 exchangeAddr = RandAddress();

    // Seed a real reputation record whose transaction count / volume matches the
    // exchange pattern. This is the committed data source the fix makes
    // DetectExchangePattern consult.
    CVM::ReputationScore score;
    score.address = exchangeAddr;
    score.score = 0;
    score.voteCount = 0;
    score.lastUpdated = GetTime();
    score.category = "normal";
    score.totalTransactions = 25000;          // well above the exchange threshold
    score.totalVolume = 500000LL * COIN;      // very high volume
    BOOST_REQUIRE_MESSAGE(rep.UpdateReputation(exchangeAddr, score),
        "P16 (1.48): precondition — writing a reputation record should succeed.");

    bool detected = CVM::PatternDetector::DetectExchangePattern(exchangeAddr, *db);

    BOOST_CHECK_MESSAGE(detected,
        "P16 (1.48): DetectExchangePattern returned false for an exchange-like "
        "volume scenario; it is hardwired to return false regardless of the "
        "address's transaction volume.");
}

// ###########################################################################
// #  PRESERVATION TESTS (Property 21 / 3.20) — EXPECTED TO PASS on unfixed    #
// ###########################################################################

// ===========================================================================
// Preservation 3.20 — behavior-metric scoring unchanged.
//
// Behavior-metric scoring (BehaviorMetrics) is out of scope for Workstream 9
// and must keep producing its current, deterministic results. These are pure
// functions of the metrics object (no dependency on any flagged path), so we
// pin representative outputs that must remain identical before and after the
// fix. EXPECTED: PASS now and after the fix.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_20_behavior_metric_scoring)
{
    // --- Diversity score: unique_partners / sqrt(total_trades), capped at 1.0.
    {
        CVM::BehaviorMetrics m(RandAddress());
        m.total_trades = 100;
        for (int i = 0; i < 2; ++i) m.unique_partners.insert(RandAddress());
        // 2 / sqrt(100) = 2 / 10 = 0.2 (a low-diversity / Sybil-like signal).
        BOOST_CHECK_CLOSE(m.CalculateDiversityScore(), 0.2, 1e-6);
    }
    {
        CVM::BehaviorMetrics m(RandAddress());
        m.total_trades = 100;
        for (int i = 0; i < 50; ++i) m.unique_partners.insert(RandAddress());
        // 50 / sqrt(100) = 5.0 -> capped at 1.0.
        BOOST_CHECK_CLOSE(m.CalculateDiversityScore(), 1.0, 1e-6);
    }
    {
        CVM::BehaviorMetrics m(RandAddress());
        // No trades -> diversity 0.0 by definition.
        BOOST_CHECK_CLOSE(m.CalculateDiversityScore(), 0.0, 1e-6);
    }

    // --- Volume score: log10(volume_in_CAS + 1) / 6.0, capped at 1.0.
    {
        CVM::BehaviorMetrics m(RandAddress());
        m.total_volume = 0;
        // log10(0 + 1) / 6 = 0.
        BOOST_CHECK_CLOSE(m.CalculateVolumeScore(), 0.0, 1e-6);
    }
    {
        CVM::BehaviorMetrics m(RandAddress());
        m.total_volume = 999 * COIN;
        // log10(999 + 1) / 6 = 3 / 6 = 0.5.
        BOOST_CHECK_CLOSE(m.CalculateVolumeScore(), 0.5, 1e-6);
    }
    {
        CVM::BehaviorMetrics m(RandAddress());
        m.total_volume = 100000000LL * COIN; // 1e8 CAS -> log10 ~8 -> capped.
        BOOST_CHECK_CLOSE(m.CalculateVolumeScore(), 1.0, 1e-6);
    }

    // --- Suspicious-pattern detector: < 10 trades -> 1.0 (insufficient data).
    {
        CVM::BehaviorMetrics m(RandAddress());
        for (int i = 0; i < 5; ++i) {
            CVM::TradeRecord t;
            t.timestamp = 1000 + i * 60;
            m.trade_history.push_back(t);
        }
        BOOST_CHECK_CLOSE(m.DetectSuspiciousPattern(), 1.0, 1e-6);
    }
    // --- Suspicious-pattern detector: perfectly regular intervals -> 0.5.
    {
        CVM::BehaviorMetrics m(RandAddress());
        for (int i = 0; i < 12; ++i) {
            CVM::TradeRecord t;
            t.timestamp = 1000 + i * 60; // exactly 60s apart => CV = 0 < 0.5
            m.trade_history.push_back(t);
        }
        BOOST_CHECK_CLOSE(m.DetectSuspiciousPattern(), 0.5, 1e-6);
    }

    // --- Base reputation: a brand-new account with no trades is neutral (50).
    {
        CVM::BehaviorMetrics m(RandAddress());
        BOOST_CHECK_EQUAL(m.CalculateBaseReputation(), static_cast<int16_t>(50));
    }
}

// ===========================================================================
// Preservation 3.20 — trust-graph manipulation detection unchanged.
//
// Trust-graph manipulation detection (TrustGraphManipulationDetector) is out of
// scope for Workstream 9 and must keep producing its current results. We build
// a clear circular trust ring (A -> B -> C -> A) via the real trust-graph API
// and pin the detector's observable behaviour: detection is deterministic
// (identical across repeated calls on identical state) and an address with no
// trust edges yields NONE. EXPECTED: PASS now and after the fix.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_20_trust_graph_manipulation_detection)
{
    auto db = MakeTempDb();
    CVM::TrustGraph graph(*db);
    CVM::WalletClusterer clusterer(*db);
    CVM::TrustGraphManipulationDetector detector(*db, graph, clusterer);

    // Build a genuine circular trust ring: A -> B -> C -> A.
    uint160 a = RandAddress();
    uint160 b = RandAddress();
    uint160 c = RandAddress();
    BOOST_REQUIRE(graph.AddTrustEdge(a, b, 60, COIN * 100, uint256(), "ws9-ring"));
    BOOST_REQUIRE(graph.AddTrustEdge(b, c, 60, COIN * 100, uint256(), "ws9-ring"));
    BOOST_REQUIRE(graph.AddTrustEdge(c, a, 60, COIN * 100, uint256(), "ws9-ring"));

    // Determinism: the detector must return the same result for identical state.
    CVM::TrustManipulationResult r1 = detector.DetectCircularTrustRing(a);
    CVM::TrustManipulationResult r2 = detector.DetectCircularTrustRing(a);
    BOOST_CHECK_MESSAGE(r1.type == r2.type,
        "Preservation 3.20: DetectCircularTrustRing produced different result "
        "types on identical trust-graph state (non-deterministic).");
    BOOST_CHECK_MESSAGE(std::fabs(r1.confidence - r2.confidence) < 1e-9,
        "Preservation 3.20: DetectCircularTrustRing produced different "
        "confidence values on identical trust-graph state (non-deterministic).");

    // An address with no trust edges must not be flagged as manipulating.
    uint160 lonely = RandAddress();
    CVM::TrustManipulationResult r3 = detector.DetectCircularTrustRing(lonely);
    BOOST_CHECK_MESSAGE(r3.type == CVM::TrustManipulationResult::NONE,
        "Preservation 3.20: DetectCircularTrustRing flagged an address with no "
        "trust edges as a circular ring.");

    // The manual health-score accessor is also deterministic on fixed state.
    int16_t h1 = detector.CalculateTrustGraphHealthScore(a);
    int16_t h2 = detector.CalculateTrustGraphHealthScore(a);
    BOOST_CHECK_MESSAGE(h1 == h2,
        "Preservation 3.20: CalculateTrustGraphHealthScore is non-deterministic "
        "on identical trust-graph state.");
}

BOOST_AUTO_TEST_SUITE_END()
