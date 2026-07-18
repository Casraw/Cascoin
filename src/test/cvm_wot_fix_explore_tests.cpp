// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Web-of-Trust Fixes — Exploratory Bug-Condition Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/web-of-trust-fixes  (bugfix)
 * Task 1: "Write exploratory bug-condition tests that reproduce ALL defects on
 *          the UNFIXED code".
 *
 * PURPOSE
 * -------
 * These are the authoritative exploratory bug-condition tests for the four
 * in-scope Web-of-Trust defects. Each test encodes the EXPECTED (post-fix, per
 * Expected-Behavior clause 2.x) behaviour and is EXPECTED TO FAIL on the current
 * (unfixed) code — every failure is a concrete counterexample confirming the
 * defect. After the fixes land, the SAME tests must pass unchanged (spec task 12).
 *
 * DO NOT fix the production code or these tests when they fail here. Surfacing
 * the counterexamples is the whole point of this task.
 *
 * TESTABILITY SEAM
 * ----------------
 * The WoT RPC handlers (`addtrust`, `getweightedreputation`, `listtrustrelations`,
 * `gettrustgraphstats`) are plain non-static free functions in `rpc/cvm.cpp`
 * taking a `const JSONRPCRequest&`. They are not exported via a header, so we
 * forward-declare the four we exercise and invoke them directly with a
 * hand-built `JSONRPCRequest` against an in-memory `CVM::g_cvmdb` — no running
 * node, HTTP layer, or RPC dispatch table is required. This is the same seam
 * used by `cvm_workstream11_tests.cpp`.
 *
 * The wallet-signed on-chain identity path (`sendtrustrelation` /
 * `BuildTrustTransaction`, bugfix 2.4 second half) requires a funded wallet and
 * is deferred to the end-to-end functional regtest (spec task 14). The
 * `addtrust` placeholder-`from` half of the same defect is fully reproducible
 * here (test case 3).
 *
 * Bug-condition coverage (bugfix.md / design.md):
 *   Case 1  Bug 1  Foreign-record corruption on enumeration       (2.1, 2.2, 2.3)
 *   Case 2  Bug 2  Edge-count mismatch (stats vs enumeration)      (2.7)
 *   Case 3  Bug 1  Placeholder / wrong `from` on the write path    (2.4)
 *   Case 4  Bug 2  Zero reputation across a stored chain           (2.5)
 *   Case 5  Bug 2  Zero reputation despite a single direct path    (2.5, 2.6)
 *   Case 6  Bug 3  Non-uint160 (P2WSH / quantum) address rejection (2.9, 2.12)
 *   Case 7  Bug 2  Viewer echoed as uint160 hex, not base58        (2.8)
 *
 * Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 1.10, 1.12
 */

#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/walletcluster.h>
#include <cvm/cvmdb.h>

#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <key.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>
#include <uint256.h>
#include <amount.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declarations of the (non-static) WoT RPC handlers defined in
// rpc/cvm.cpp. They are not exported via a header, so we declare them here to
// invoke them directly at the unit seam.
// ---------------------------------------------------------------------------
UniValue addtrust(const JSONRPCRequest& request);
UniValue getweightedreputation(const JSONRPCRequest& request);
UniValue listtrustrelations(const JSONRPCRequest& request);
UniValue gettrustgraphstats(const JSONRPCRequest& request);

namespace {

// Reason string used across the round-trip tests. It is short, ASCII, and
// unambiguously valid UTF-8 so any corruption is obvious.
static const std::string kReason = "C trusts D";

// A bond comfortably above the required bond for a weight-80 edge:
//   required = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * 80 = 1.8 CAS
// (COIN == 10'000'000 in Cascoin). 2 CAS clears it.
static const CAmount kBond = 2 * COIN;

// Build a fresh, well-formed request carrying the given positional params.
JSONRPCRequest MakeRequest(const UniValue& params)
{
    JSONRPCRequest request;
    request.params = params;
    request.fHelp = false;
    return request;
}

UniValue Arr(const std::vector<UniValue>& items)
{
    UniValue arr(UniValue::VARR);
    for (const auto& it : items) arr.push_back(it);
    return arr;
}

// A random uint160 (20-byte trust-node value).
uint160 RandU160()
{
    uint160 a;
    uint256 r = InsecureRand256();
    std::memcpy(a.begin(), r.begin(), 20);
    return a;
}

// Encode a uint160 as a legacy base58 P2PKH address string (regtest: m.../n...).
std::string P2PKH(const uint160& a)
{
    return EncodeDestination(CKeyID(a));
}

// Minimal UTF-8 validity check (structural). Returns false for byte sequences
// that cannot be valid UTF-8 (the corrupted `reason` produces such bytes).
bool IsValidUtf8(const std::string& s)
{
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t extra;
        if (c < 0x80) { extra = 0; }
        else if ((c >> 5) == 0x06) { extra = 1; }
        else if ((c >> 4) == 0x0e) { extra = 2; }
        else if ((c >> 3) == 0x1e) { extra = 3; }
        else { return false; }
        if (i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 0x02) return false;
        }
        i += extra + 1;
    }
    return true;
}

// Read the "message" field of a thrown JSONRPCError (a UniValue object).
std::string RpcErrMessage(const UniValue& e)
{
    if (e.isObject() && e.exists("message") && e["message"].isStr()) {
        return e["message"].get_str();
    }
    return "";
}

// Fixture: regtest params (so bech32 P2WSH is "rcas1..." and quantum is
// "rcasq1...") plus a fresh in-memory CVM database per test.
struct WoTExploreSetup : public BasicTestingSetup {
    WoTExploreSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~WoTExploreSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

// Write one canonical forward edge and then create the foreign propagator
// records (trust_prop_* / trust_prop_idx_*) that share the "trust_" prefix.
//
// The canonical edge uses a NULL bond-tx hash on purpose: the propagated
// record's `sourceEdgeTx` therefore becomes all-zeros, which makes the
// mis-read of a PropagatedTrustEdge (110 bytes) as a TrustEdge deterministic —
// the `reason` CompactSize length lands on a zero byte, so the mis-read
// succeeds and produces an included garbage edge instead of throwing. This
// pins down the counterexample rather than leaving it to random bytes.
//
// The target address's first two bytes are fixed (0xAB, 0xCD) so that, when a
// PropagatedTrustEdge is mis-read as a TrustEdge, the two bytes of
// `originalTarget` reinterpreted as `trustWeight` are clearly NOT 80.
void SeedCanonicalAndForeignRecords(const uint160& from, uint160& toOut)
{
    uint160 to = RandU160();
    to.begin()[0] = 0xAB;
    to.begin()[1] = 0xCD;
    toOut = to;

    CVM::TrustGraph tg(*CVM::g_cvmdb);
    BOOST_REQUIRE_MESSAGE(
        tg.AddTrustEdge(from, to, /*weight=*/80, kBond, /*bondTx=*/uint256(), kReason),
        "setup: AddTrustEdge for the canonical edge must succeed");

    // Create the foreign propagator records via TrustPropagator (single-address
    // cluster -> exactly one trust_prop_ record + one trust_prop_idx_ record).
    CVM::WalletClusterer clusterer(*CVM::g_cvmdb);
    CVM::TrustPropagator propagator(*CVM::g_cvmdb, clusterer, tg);

    CVM::TrustEdge edge;
    // TrustEdge endpoints are now the wide TrustNodeId; wrap the legacy uint160
    // test inputs (mechanical type adaptation only — test intent unchanged).
    edge.fromAddress = CVM::TrustNodeId::FromLegacyUint160(from);
    edge.toAddress = CVM::TrustNodeId::FromLegacyUint160(to);
    edge.trustWeight = 80;
    edge.timestamp = 1700000000;
    edge.bondAmount = kBond;
    edge.bondTxHash = uint256(); // null -> deterministic mis-read (see above)
    edge.slashed = false;
    edge.reason = kReason;

    uint32_t propagated = propagator.PropagateTrustEdge(edge);
    BOOST_REQUIRE_MESSAGE(propagated >= 1,
        "setup: TrustPropagator must create at least one foreign trust_prop_ record");
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_wot_fix_explore_tests, WoTExploreSetup)

// ===========================================================================
// Case 1 — Foreign-record corruption on enumeration.          (Bug 1: 2.1-2.3)
//
// Expected (2.1/2.2/2.3): `listtrustrelations` SHALL enumerate ONLY canonical
// forward edges, so every returned edge equals exactly what was written
// (weight 80, reason "C trusts D", not slashed) and every `reason` is valid
// UTF-8 (parseable CLI reply).
//
// UNFIXED: the enumerator matches foreign trust_prop_ records under the shared
// "trust_" prefix and mis-reads their bytes as a TrustEdge, so a garbage edge
// (weight reinterpreted from originalTarget bytes, empty/binary reason) is
// returned alongside the real one -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case1_foreign_record_corruption)
{
    const uint160 from = RandU160();
    uint160 to;
    SeedCanonicalAndForeignRecords(from, to);

    UniValue result = listtrustrelations(MakeRequest(Arr({})));
    const UniValue& edges = result["edges"];
    BOOST_REQUIRE(edges.isArray());
    BOOST_REQUIRE_MESSAGE(edges.size() >= 1,
        "listtrustrelations returned no edges for a stored canonical edge");

    for (size_t i = 0; i < edges.size(); ++i) {
        const UniValue& e = edges[i];
        int weight = e["weight"].get_int();
        std::string reason = e["reason"].isStr() ? e["reason"].get_str() : std::string();
        bool slashed = e["slashed"].isBool() ? e["slashed"].get_bool() : false;

        // Every field of every returned edge must equal the single canonical
        // edge that was written.
        BOOST_CHECK_MESSAGE(weight == 80,
            "Case 1 (2.1): listtrustrelations returned an edge with weight "
            + std::to_string(weight) + " (expected 80). A foreign trust_prop_ "
            "record was mis-read as a TrustEdge (edge index "
            + std::to_string(i) + ").");
        BOOST_CHECK_MESSAGE(reason == kReason,
            "Case 1 (2.2): listtrustrelations returned reason \"" + reason +
            "\" (expected \"" + kReason + "\") for edge index "
            + std::to_string(i) + ".");
        BOOST_CHECK_MESSAGE(!slashed,
            "Case 1 (2.1): listtrustrelations returned slashed=true for a never-"
            "slashed edge (edge index " + std::to_string(i) + ").");
        BOOST_CHECK_MESSAGE(IsValidUtf8(reason),
            "Case 1 (2.3): listtrustrelations returned a non-UTF-8 reason for "
            "edge index " + std::to_string(i) + " — this breaks cascoin-cli "
            "with \"couldn't parse reply from server\".");
    }
}

// ===========================================================================
// Case 2 — Edge-count mismatch (stats vs enumeration).            (Bug 2: 2.7)
//
// Expected (2.7): `gettrustgraphstats.total_trust_edges` and the number of
// forward edges enumerated by `listtrustrelations` SHALL both equal the number
// of canonical forward edges (here: exactly 1) and SHALL be consistent.
//
// UNFIXED: GetGraphStats counts every "trust_" key that is not "trust_in_",
// including trust_prop_ and trust_prop_idx_ (-> 3), while listtrustrelations
// returns a different number -> the counts disagree and neither equals the true
// canonical count -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case2_edge_count_mismatch)
{
    const uint160 from = RandU160();
    uint160 to;
    SeedCanonicalAndForeignRecords(from, to);

    const int kCanonical = 1; // exactly one canonical forward edge was written

    UniValue stats = gettrustgraphstats(MakeRequest(Arr({})));
    int64_t total = stats["total_trust_edges"].get_int64();

    UniValue listing = listtrustrelations(MakeRequest(Arr({})));
    int listCount = listing["count"].get_int();

    BOOST_CHECK_MESSAGE(total == kCanonical,
        "Case 2 (2.7): gettrustgraphstats.total_trust_edges = "
        + std::to_string(total) + " but only " + std::to_string(kCanonical)
        + " canonical forward edge exists; foreign trust_prop_/trust_prop_idx_ "
        "records are being counted.");
    BOOST_CHECK_MESSAGE(listCount == kCanonical,
        "Case 2 (2.7): listtrustrelations count = " + std::to_string(listCount)
        + " but only " + std::to_string(kCanonical) + " canonical forward edge "
        "exists.");
    BOOST_CHECK_MESSAGE(total == listCount,
        "Case 2 (2.7): gettrustgraphstats.total_trust_edges (" + std::to_string(total)
        + ") disagrees with the listtrustrelations count (" + std::to_string(listCount)
        + ") for the same graph state.");
}

// ===========================================================================
// Case 3 — Placeholder / wrong `from` on the write path.          (Bug 1: 2.4)
//
// Expected (2.4): the edge stored by `addtrust` SHALL be keyed under the
// creating address, NOT the all-zeros placeholder. Therefore the stored edge
// must be retrievable under the supplied creating `from` address and must NOT
// be retrievable under the null (all-zeros) `from`.
//
// The creating identity is supplied explicitly via the `from` argument (the
// fifth positional parameter documented in the addtrust RPC signature). This
// is the unit-seam equivalent of "derive from the loaded wallet": with no
// running node there is no wallet, so the caller provides the from identity
// directly. Per the fix (Change A2 / task 3.3), `addtrust` resolves the from
// identity from this argument (or a loaded wallet) and REJECTS rather than
// storing an all-zeros placeholder when it cannot be resolved.
//
// UNFIXED: `addtrust` wrote `uint160 fromAddress; // Placeholder` (all-zeros),
// so the edge was stored under trust_0000…_<to> and was retrievable by the
// null address -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case3_placeholder_from)
{
    const uint160 fromRaw = RandU160();
    const std::string fromStr = P2PKH(fromRaw);
    const uint160 toRaw = RandU160();
    const std::string toStr = P2PKH(toRaw);

    // Supply the creating identity explicitly via the `from` argument (arg 5).
    UniValue params = Arr({UniValue(toStr), UniValue((int64_t)80),
                           UniValue(UniValue::VNUM, "2.00000000"),
                           UniValue(kReason), UniValue(fromStr)});
    BOOST_REQUIRE_NO_THROW(addtrust(MakeRequest(params)));

    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::TrustEdge edge;

    // The edge must NOT be retrievable under the all-zeros placeholder `from`.
    bool foundUnderZero = tg.GetTrustEdge(uint160() /*null from*/, toRaw, edge);
    BOOST_CHECK_MESSAGE(!foundUnderZero,
        "Case 3 (2.4): addtrust stored the edge under the all-zeros placeholder "
        "`from` address (trust_0000…_<to>); the stored `from` does not equal the "
        "creating address.");

    // The edge MUST be retrievable under the supplied creating address, i.e. the
    // stored `from` equals the address that created the edge.
    bool foundUnderFrom = tg.GetTrustEdge(fromRaw, toRaw, edge);
    BOOST_CHECK_MESSAGE(foundUnderFrom,
        "Case 3 (2.4): addtrust did not store the edge under the supplied creating "
        "address; the stored `from` does not equal the creating address.");
}

// ===========================================================================
// Case 4 — Zero reputation across a stored trust chain.           (Bug 2: 2.5)
//
// Expected (2.5): for a stored canonical chain A -> B -> C -> D (weights 80),
// `GetWeightedReputation(A, D, 3)` SHALL find at least one path AND return a
// NON-ZERO weighted reputation derived from the trust-path weights.
//
// UNFIXED: the path IS found (traversal over correctly-keyed canonical edges
// works), but the score is derived only from BondedVote records at the target
// (there are none), so the reputation is 0 -> the non-zero property FAILS. The
// paths-found check is retained to document that the path exists yet the score
// is still zero.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case4_zero_reputation_over_chain)
{
    const uint160 A = RandU160();
    const uint160 B = RandU160();
    const uint160 C = RandU160();
    const uint160 D = RandU160();

    CVM::TrustGraph tg(*CVM::g_cvmdb);
    BOOST_REQUIRE(tg.AddTrustEdge(A, B, 80, kBond, uint256(), "A trusts B"));
    BOOST_REQUIRE(tg.AddTrustEdge(B, C, 80, kBond, uint256(), "B trusts C"));
    BOOST_REQUIRE(tg.AddTrustEdge(C, D, 80, kBond, uint256(), kReason));

    std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, D, 3);
    BOOST_CHECK_MESSAGE(paths.size() >= 1,
        "Case 4 (2.5): no trust path found for the canonical chain A->B->C->D.");

    double rep = tg.GetWeightedReputation(A, D, 3);
    BOOST_CHECK_MESSAGE(rep != 0.0,
        "Case 4 (2.5): GetWeightedReputation(A, D, 3) returned 0 for the stored "
        "chain A->B->C->D even though a trust path exists; the score is derived "
        "only from (absent) BondedVote records instead of the trust-path weights.");
}

// ===========================================================================
// Case 5 — Zero reputation despite a single direct path.     (Bug 2: 2.5, 2.6)
//
// Expected (2.5/2.6): for a single stored canonical edge B -> C at maxdepth 1
// (viewer = B, target = C), the system SHALL find that path AND return a
// non-zero weighted reputation.
//
// UNFIXED: the path is found but GetWeightedReputation returns 0 (no BondedVote
// records at C) -> the non-zero property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case5_zero_reputation_direct_edge)
{
    const uint160 B = RandU160();
    const uint160 C = RandU160();

    CVM::TrustGraph tg(*CVM::g_cvmdb);
    BOOST_REQUIRE(tg.AddTrustEdge(B, C, 80, kBond, uint256(), "B trusts C"));

    std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(B, C, 1);
    BOOST_CHECK_MESSAGE(paths.size() >= 1,
        "Case 5 (2.6): no trust path found for the single stored edge B->C.");

    double rep = tg.GetWeightedReputation(B, C, 1);
    BOOST_CHECK_MESSAGE(rep != 0.0,
        "Case 5 (2.5/2.6): GetWeightedReputation(B, C, 1) returned 0 for a single "
        "stored direct edge B->C even though the direct path exists.");
}

// ===========================================================================
// Case 6 — Non-uint160 address rejection.                   (Bug 3: 2.9, 2.12)
//
// Expected (2.9/2.12): the WoT RPCs SHALL accept every standard Cascoin address
// type, including bech32 P2WSH (WitnessV0ScriptHash) and quantum/Falcon
// (WitnessV2Quantum) addresses — no "Address type not supported" error.
//
// UNFIXED: the RPCs only accept CKeyID / CScriptID / WitnessV0KeyHash and throw
// "Address type not supported" for P2WSH and quantum addresses -> the property
// FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case6_address_type_rejection)
{
    // A bech32 P2WSH address (WitnessV0ScriptHash, 32-byte program).
    WitnessV0ScriptHash p2wsh;
    {
        uint256 r = InsecureRand256();
        std::memcpy(p2wsh.begin(), r.begin(), 32);
    }
    const std::string p2wshStr = EncodeDestination(p2wsh);

    // A quantum/Falcon address (WitnessV2Quantum, 32-byte program -> rcasq1...).
    WitnessV2Quantum quantum;
    {
        uint256 r = InsecureRand256();
        std::memcpy(quantum.begin(), r.begin(), 32);
    }
    const std::string quantumStr = EncodeDestination(quantum);

    BOOST_REQUIRE_MESSAGE(!p2wshStr.empty() && !quantumStr.empty(),
        "setup: failed to encode P2WSH/quantum addresses on regtest");

    struct Probe { const char* label; std::string addr; };
    const std::vector<Probe> probes = {
        {"P2WSH (WitnessV0ScriptHash)", p2wshStr},
        {"quantum (WitnessV2Quantum)", quantumStr},
    };

    for (const Probe& p : probes) {
        // addtrust decode path.
        {
            bool sawUnsupported = false;
            std::string msg;
            try {
                UniValue params = Arr({UniValue(p.addr), UniValue((int64_t)80),
                                       UniValue(UniValue::VNUM, "2.00000000"),
                                       UniValue(kReason)});
                addtrust(MakeRequest(params));
            } catch (const UniValue& e) {
                msg = RpcErrMessage(e);
                if (msg.find("Address type not supported") != std::string::npos) {
                    sawUnsupported = true;
                }
            } catch (const std::exception&) {
                // Any other error is acceptable for this bug condition.
            }
            BOOST_CHECK_MESSAGE(!sawUnsupported,
                std::string("Case 6 (2.9): addtrust rejected a ") + p.label +
                " address with \"Address type not supported\".");
        }

        // getweightedreputation decode path.
        {
            bool sawUnsupported = false;
            std::string msg;
            try {
                UniValue params = Arr({UniValue(p.addr)});
                getweightedreputation(MakeRequest(params));
            } catch (const UniValue& e) {
                msg = RpcErrMessage(e);
                if (msg.find("Address type not supported") != std::string::npos) {
                    sawUnsupported = true;
                }
            } catch (const std::exception&) {
                // Any other error is acceptable for this bug condition.
            }
            BOOST_CHECK_MESSAGE(!sawUnsupported,
                std::string("Case 6 (2.9/2.12): getweightedreputation rejected a ")
                + p.label + " address with \"Address type not supported\".");
        }
    }
}

// ===========================================================================
// Case 7 — Viewer echoed as uint160 hex instead of base58.        (Bug 2: 2.8)
//
// Expected (2.8): `getweightedreputation` SHALL echo the `viewer` field as the
// base58 address that was supplied.
//
// UNFIXED: it echoes `viewerAddress.ToString()` (a 40-char hash160 hex string)
// -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(explore_case7_viewer_hex_echo)
{
    const uint160 viewer = RandU160();
    const uint160 target = RandU160();
    const std::string viewerStr = P2PKH(viewer);
    const std::string targetStr = P2PKH(target);

    UniValue params = Arr({UniValue(targetStr), UniValue(viewerStr),
                           UniValue((int64_t)3)});
    UniValue result = getweightedreputation(MakeRequest(params));

    BOOST_REQUIRE(result["viewer"].isStr());
    std::string echoed = result["viewer"].get_str();

    BOOST_CHECK_MESSAGE(echoed == viewerStr,
        "Case 7 (2.8): getweightedreputation echoed viewer=\"" + echoed +
        "\" but the supplied base58 viewer was \"" + viewerStr +
        "\" (the RPC emits uint160 hex instead of base58).");
}

BOOST_AUTO_TEST_SUITE_END()
