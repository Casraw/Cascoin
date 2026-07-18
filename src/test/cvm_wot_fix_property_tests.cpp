// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Web-of-Trust Fixes — Fix-Check Property Test Suite (AFTER fix)
 *
 * Spec: .kiro/specs/web-of-trust-fixes  (bugfix)
 * Task 11: "Write fix-check property tests P1–P6".
 *
 * PURPOSE
 * -------
 * These property tests map to the six Correctness Properties in design.md and
 * extend/parallel the exploratory bug-condition tests (task 1). They are run
 * AFTER the Tier A / Tier B fixes have landed and are EXPECTED TO PASS —
 * passing confirms every in-scope bug is resolved and no preserved behaviour
 * regressed.
 *
 * Property map (design.md → Correctness Properties):
 *   P1  Canonical edge round-trip & enumeration integrity   (2.1, 2.2, 2.3, 2.4)
 *   P2  Path traversal & reputation over stored edges        (2.5, 2.6, 2.7, 2.8)
 *   P3  All standard address types (incl. quantum) lossless  (2.9, 2.10, 2.12)
 *   P4  Single-wallet trust graph                            (2.11)
 *   P5  Preservation of validation/traversal/self-view/ser.  (3.1,3.2,3.4,3.5,3.6,3.7)
 *   P6  Preservation of legacy/quantum/clustering/keying     (3.3,3.8,3.9,3.10)
 *
 * TESTABILITY SEAM
 * ----------------
 * The WoT RPC handlers are plain non-static free functions in rpc/cvm.cpp
 * taking a `const JSONRPCRequest&`. They are not exported via a header, so we
 * forward-declare the ones we exercise and invoke them directly against an
 * in-memory CVM::g_cvmdb — the same seam used by cvm_wot_fix_explore_tests.cpp
 * and cvm_wot_preserve_tests.cpp.
 *
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12
 */

#include <cvm/trustgraph.h>
#include <cvm/trustpropagator.h>
#include <cvm/trustnodeid.h>
#include <cvm/softfork.h>
#include <cvm/walletcluster.h>
#include <cvm/cvmdb.h>

#include <address_quantum.h>
#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <key.h>
#include <pubkey.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <streams.h>
#include <univalue.h>
#include <uint256.h>
#include <amount.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declarations of the (non-static) WoT RPC handlers defined in
// rpc/cvm.cpp (not exported via a header).
// ---------------------------------------------------------------------------
UniValue addtrust(const JSONRPCRequest& request);
UniValue getweightedreputation(const JSONRPCRequest& request);
UniValue listtrustrelations(const JSONRPCRequest& request);
UniValue gettrustgraphstats(const JSONRPCRequest& request);

namespace {

// Property sample counts. DB-backed traversal cases run fewer samples.
static constexpr int kSamples = 64;
static constexpr int kSamplesDb = 32;

// A bond comfortably above the required bond for every weight used here:
//   required(w) = minBondAmount (1 CAS) + bondPerVotePoint (0.01 CAS) * |w|
// (COIN == 10'000'000 in Cascoin). 3 CAS clears every weight up to 100.
static const CAmount kBond = 3 * COIN;

CAmount RequiredBond(int16_t weight)
{
    return CVM::g_wotConfig.minBondAmount +
           CVM::g_wotConfig.bondPerVotePoint * std::abs(static_cast<int>(weight));
}

// A random uint160 (20-byte trust-node value), guaranteed non-null.
uint160 RandU160()
{
    uint160 a;
    do {
        uint256 r = InsecureRand256();
        std::memcpy(a.begin(), r.begin(), 20);
    } while (a.IsNull());
    return a;
}

// Encode a uint160 as a legacy base58 P2PKH address string (regtest: m.../n...).
std::string P2PKH(const uint160& a)
{
    return EncodeDestination(CKeyID(a));
}

// Minimal structural UTF-8 validity check (same as the exploration suite).
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

std::string RpcErrMessage(const UniValue& e)
{
    if (e.isObject() && e.exists("message") && e["message"].isStr()) {
        return e["message"].get_str();
    }
    return "";
}

// Install a fresh in-memory CVM database as the process-global g_cvmdb so that
// direct TrustGraph access and the RPC handlers observe the same state.
void ResetGlobalDb()
{
    CVM::g_cvmdb.reset(new CVM::CVMDatabase(
        fs::temp_directory_path() / fs::unique_path(),
        1 << 20, /*fMemory=*/true, /*fWipe=*/true));
}

// Create one foreign propagator record pair (trust_prop_* + trust_prop_idx_*)
// derived from a canonical edge, so the enumerators must filter them out.
void SeedForeignRecords(const uint160& from, const uint160& to, const std::string& reason)
{
    CVM::TrustGraph tg(*CVM::g_cvmdb);
    CVM::WalletClusterer clusterer(*CVM::g_cvmdb);
    CVM::TrustPropagator propagator(*CVM::g_cvmdb, clusterer, tg);

    CVM::TrustEdge edge;
    edge.fromAddress = CVM::TrustNodeId::FromLegacyUint160(from);
    edge.toAddress = CVM::TrustNodeId::FromLegacyUint160(to);
    edge.trustWeight = 80;
    edge.timestamp = 1700000000;
    edge.bondAmount = kBond;
    edge.bondTxHash = uint256(); // null -> deterministic layout
    edge.slashed = false;
    edge.reason = reason;

    propagator.PropagateTrustEdge(edge);
}

// Fixture: regtest params (so P2WSH is "rcas1..." and quantum is "rcasq1...")
// plus a fresh in-memory CVM database per test.
struct WoTFixSetup : public BasicTestingSetup {
    WoTFixSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        ResetGlobalDb();
    }
    ~WoTFixSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_wot_fix_property_tests, WoTFixSetup)

// ===========================================================================
// P1 — Canonical edge round-trip & enumeration integrity.
//
// **Property 1: Expected Behavior**
//
// For random canonical edges interleaved with random trust_prop_* /
// trust_prop_idx_* foreign records, listtrustrelations returns EXACTLY the
// canonical set with exact fields (from, to, weight, reason, slashed=false)
// and every reason is valid UTF-8. The reported `from` equals the creating
// address (never the all-zeros placeholder).
//
// **Validates: Requirements 2.1, 2.2, 2.3, 2.4**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p1_canonical_enumeration_integrity_property)
{
    for (int s = 0; s < kSamplesDb; ++s) {
        ResetGlobalDb();
        CVM::TrustGraph tg(*CVM::g_cvmdb);

        const int nCanonical = 3 + static_cast<int>(InsecureRandRange(5)); // [3,7]

        // key: "fromStr|toStr" -> (weight, reason)
        std::map<std::string, std::pair<int, std::string>> expected;

        for (int i = 0; i < nCanonical; ++i) {
            const uint160 from = RandU160();
            const uint160 to = RandU160();
            const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            const std::string reason = "reason-" + std::to_string(s) + "-" + std::to_string(i);

            BOOST_REQUIRE_MESSAGE(
                tg.AddTrustEdge(from, to, weight, kBond, uint256(), reason),
                "P1: AddTrustEdge for a canonical edge must succeed (#" +
                std::to_string(i) + ")");

            const std::string fromStr = P2PKH(from);
            const std::string toStr = P2PKH(to);
            expected[fromStr + "|" + toStr] = {weight, reason};

            // Interleave foreign propagator records that share the "trust_" prefix.
            SeedForeignRecords(from, to, reason);
        }

        UniValue result = listtrustrelations(MakeRequest(Arr({})));
        const UniValue& edges = result["edges"];
        BOOST_REQUIRE(edges.isArray());

        // Exactly the canonical set is returned — foreign records are filtered.
        BOOST_CHECK_MESSAGE(result["count"].get_int() == nCanonical,
            "P1 (2.7/2.1): listtrustrelations count = " +
            std::to_string(result["count"].get_int()) + " but " +
            std::to_string(nCanonical) + " canonical edges were written "
            "(foreign trust_prop_* records must be filtered) (sample " +
            std::to_string(s) + ")");
        BOOST_CHECK_MESSAGE(edges.size() == static_cast<size_t>(nCanonical),
            "P1: edges array size mismatch (sample " + std::to_string(s) + ")");

        std::set<std::string> seen;
        for (size_t i = 0; i < edges.size(); ++i) {
            const UniValue& e = edges[i];
            const std::string fromAddr = e["from"].get_str();
            const std::string toAddr = e["to"].get_str();
            const int weight = e["weight"].get_int();
            const std::string reason = e["reason"].isStr() ? e["reason"].get_str() : std::string();
            // `slashed` may be serialized as a bool or a numeric 0/1; read both.
            const UniValue& sv = e["slashed"];
            bool slashed = false;
            if (sv.isBool()) slashed = sv.get_bool();
            else if (sv.isNum()) slashed = (sv.get_int() != 0);

            // from must be a real (non-null) address (2.4).
            const std::string nullFrom = P2PKH(uint160());
            BOOST_CHECK_MESSAGE(fromAddr != nullFrom,
                "P1 (2.4): returned edge has the all-zeros placeholder `from`");

            const std::string key = fromAddr + "|" + toAddr;
            auto it = expected.find(key);
            BOOST_REQUIRE_MESSAGE(it != expected.end(),
                "P1 (2.1): listtrustrelations returned an unexpected edge " + key +
                " (a foreign record was mis-read as a canonical edge)");
            BOOST_CHECK_MESSAGE(seen.insert(key).second,
                "P1: duplicate edge returned for " + key);

            BOOST_CHECK_MESSAGE(weight == it->second.first,
                "P1 (2.1): weight mismatch for " + key + ": got " +
                std::to_string(weight) + ", expected " + std::to_string(it->second.first));
            BOOST_CHECK_MESSAGE(reason == it->second.second,
                "P1 (2.2): reason mismatch for " + key + ": got \"" + reason +
                "\", expected \"" + it->second.second + "\"");
            BOOST_CHECK_MESSAGE(!slashed,
                "P1 (2.1): a never-slashed edge was returned with slashed=true for " + key);
            BOOST_CHECK_MESSAGE(IsValidUtf8(reason),
                "P1 (2.3): non-UTF-8 reason returned for " + key +
                " (would break cascoin-cli reply parsing)");
        }
    }
}

// ===========================================================================
// P2 — Path traversal & reputation over stored edges.
//
// **Property 2: Expected Behavior**
//
// For random DAGs of canonical edges (all weights >= the traversal threshold,
// none slashed) and random viewer/target/depth:
//   - a path is found iff a directed path of at most `depth` hops exists;
//   - when a qualifying path exists, the weighted reputation is non-zero;
//   - gettrustgraphstats.total_trust_edges equals the listtrustrelations count;
//   - getweightedreputation echoes `viewer` as the supplied base58 address.
//
// **Validates: Requirements 2.5, 2.6, 2.7, 2.8**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p2_path_traversal_and_reputation_property)
{
    for (int s = 0; s < kSamplesDb; ++s) {
        ResetGlobalDb();
        CVM::TrustGraph tg(*CVM::g_cvmdb);

        const int K = 4 + static_cast<int>(InsecureRandRange(3)); // [4,6] nodes
        std::vector<uint160> node(K);
        for (int i = 0; i < K; ++i) node[i] = RandU160();

        // Random DAG: edges only from lower index to higher index (acyclic).
        std::vector<std::vector<int>> adj(K);
        int edgeCount = 0;
        for (int i = 0; i < K; ++i) {
            for (int j = i + 1; j < K; ++j) {
                if (InsecureRandRange(2) == 0) {
                    const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
                    BOOST_REQUIRE(tg.AddTrustEdge(node[i], node[j], weight, kBond,
                                                  uint256(), "e"));
                    adj[i].push_back(j);
                    edgeCount++;
                }
            }
        }

        // Pick a distinct viewer/target pair and a random depth.
        int vi = static_cast<int>(InsecureRandRange(K));
        int ti = static_cast<int>(InsecureRandRange(K));
        if (ti == vi) ti = (ti + 1) % K;
        const int depth = 1 + static_cast<int>(InsecureRandRange(K)); // [1,K]

        // Expected reachability: shortest hop-distance from vi within `depth`.
        std::vector<int> dist(K, -1);
        std::deque<int> q;
        dist[vi] = 0;
        q.push_back(vi);
        while (!q.empty()) {
            int u = q.front(); q.pop_front();
            for (int w : adj[u]) {
                if (dist[w] == -1) { dist[w] = dist[u] + 1; q.push_back(w); }
            }
        }
        const bool expectReachable = (dist[ti] != -1 && dist[ti] <= depth);

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(node[vi], node[ti], depth);
        const bool found = !paths.empty();

        BOOST_CHECK_MESSAGE(found == expectReachable,
            "P2 (2.5/2.6): path-found (" + std::to_string(found) + ") != reachable-within-depth ("
            + std::to_string(expectReachable) + ") for viewer#" + std::to_string(vi) +
            " target#" + std::to_string(ti) + " depth " + std::to_string(depth) +
            " (dist " + std::to_string(dist[ti]) + ", sample " + std::to_string(s) + ")");

        if (expectReachable) {
            const double rep = tg.GetWeightedReputation(node[vi], node[ti], depth);
            BOOST_CHECK_MESSAGE(rep != 0.0,
                "P2 (2.5): weighted reputation must be non-zero when a qualifying path "
                "exists; got 0 (sample " + std::to_string(s) + ")");
        }

        // Count consistency (2.7): stats.total_trust_edges == list count == edges added.
        UniValue stats = gettrustgraphstats(MakeRequest(Arr({})));
        UniValue listing = listtrustrelations(MakeRequest(Arr({})));
        const int64_t total = stats["total_trust_edges"].get_int64();
        const int listCount = listing["count"].get_int();

        BOOST_CHECK_MESSAGE(total == edgeCount,
            "P2 (2.7): total_trust_edges = " + std::to_string(total) + " != edges added "
            + std::to_string(edgeCount) + " (sample " + std::to_string(s) + ")");
        BOOST_CHECK_MESSAGE(listCount == edgeCount,
            "P2 (2.7): listtrustrelations count = " + std::to_string(listCount) +
            " != edges added " + std::to_string(edgeCount) + " (sample " + std::to_string(s) + ")");
        BOOST_CHECK_MESSAGE(total == listCount,
            "P2 (2.7): stats total (" + std::to_string(total) + ") disagrees with list count ("
            + std::to_string(listCount) + ") (sample " + std::to_string(s) + ")");
    }
}

// viewer echoed as base58 (2.8). **Validates: Requirements 2.8**
BOOST_AUTO_TEST_CASE(p2_viewer_echoed_as_base58_property)
{
    for (int i = 0; i < kSamples; ++i) {
        ResetGlobalDb();
        const std::string viewerStr = P2PKH(RandU160());
        const std::string targetStr = P2PKH(RandU160());

        UniValue params = Arr({UniValue(targetStr), UniValue(viewerStr), UniValue((int64_t)3)});
        UniValue result = getweightedreputation(MakeRequest(params));

        BOOST_REQUIRE(result["viewer"].isStr());
        BOOST_CHECK_MESSAGE(result["viewer"].get_str() == viewerStr,
            "P2 (2.8): viewer echoed as \"" + result["viewer"].get_str() +
            "\" but supplied base58 viewer was \"" + viewerStr + "\" (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(result["target"].get_str() == targetStr,
            "P2 (2.8): target echoed as \"" + result["target"].get_str() +
            "\" but supplied base58 target was \"" + targetStr + "\" (#" +
            std::to_string(i) + ")");
    }
}

// ===========================================================================
// P3 — All standard address types (incl. quantum) supported losslessly.
//
// **Property 3: Expected Behavior**
//
// For each of the five standard destination types, an edge stored under the
// wide TrustNodeId is retrievable by the SAME address (no truncation/collision),
// appears in the outgoing set, and round-trips back to the original address.
// Additionally the WoT RPC decode path accepts every type (no
// "Address type not supported").
//
// **Validates: Requirements 2.9, 2.10, 2.12**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p3_all_address_types_lossless_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 h160 = RandU160();
        uint256 h256 = InsecureRand256();

        std::vector<std::pair<std::string, CTxDestination>> dests;
        dests.emplace_back("P2PKH", CTxDestination(CKeyID(RandU160())));
        dests.emplace_back("P2SH", CTxDestination(CScriptID(RandU160())));
        dests.emplace_back("P2WPKH", CTxDestination(WitnessV0KeyHash(RandU160())));
        {
            WitnessV0ScriptHash wsh;
            std::memcpy(wsh.begin(), h256.begin(), 32);
            dests.emplace_back("P2WSH", CTxDestination(wsh));
        }
        {
            WitnessV2Quantum q;
            std::memcpy(q.begin(), h256.begin(), 32);
            dests.emplace_back("QUANTUM", CTxDestination(q));
        }

        // A common P2PKH truster.
        const CVM::TrustNodeId fromNode = CVM::TrustNodeId::FromLegacyUint160(h160);

        for (const auto& d : dests) {
            CVM::TrustNodeId toNode;
            BOOST_REQUIRE_MESSAGE(CVM::TrustNodeId::FromDestination(d.second, toNode),
                "P3 (2.12): FromDestination must accept " + d.first + " (#" +
                std::to_string(i) + ")");

            const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            const std::string reason = d.first + "-edge";
            BOOST_REQUIRE_MESSAGE(
                tg.AddTrustEdge(fromNode, toNode, weight, kBond, uint256(), reason),
                "P3 (2.10): AddTrustEdge must store a " + d.first + " edge (#" +
                std::to_string(i) + ")");

            // Retrievable by the SAME wide identifier, no truncation/collision.
            CVM::TrustEdge edge;
            BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(fromNode, toNode, edge),
                "P3 (2.10): stored " + d.first + " edge must be retrievable by the "
                "same address (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(edge.toAddress == toNode,
                "P3 (2.12): retrieved `to` identifier changed for " + d.first +
                " (truncation/collision) (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(edge.fromAddress == fromNode,
                "P3: retrieved `from` identifier changed for " + d.first + " (#" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(edge.trustWeight == weight,
                "P3: weight round-trip mismatch for " + d.first + " (#" +
                std::to_string(i) + ")");

            // Appears in the outgoing set keyed by the truster.
            std::vector<CVM::TrustEdge> outgoing = tg.GetOutgoingTrust(fromNode);
            bool foundOut = false;
            for (const auto& oe : outgoing) if (oe.toAddress == toNode) foundOut = true;
            BOOST_CHECK_MESSAGE(foundOut,
                "P3 (2.10): " + d.first + " edge not returned by GetOutgoingTrust (#" +
                std::to_string(i) + ")");

            // Round-trips back to the SAME displayable address (2.10/3.10).
            BOOST_CHECK_MESSAGE(
                EncodeDestination(edge.toAddress.ToDestination()) == EncodeDestination(d.second),
                "P3 (2.10): stored " + d.first + " node does not render back to the "
                "original address (#" + std::to_string(i) + ")");
        }
    }
}

// The unified WoT RPC decode path accepts every standard type (no
// "Address type not supported"). **Validates: Requirements 2.9**
BOOST_AUTO_TEST_CASE(p3_rpc_accepts_all_address_types_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        ResetGlobalDb();

        const std::string fromStr = P2PKH(RandU160()); // explicit P2PKH truster
        uint256 h256 = InsecureRand256();

        std::vector<std::pair<std::string, std::string>> probes;
        probes.emplace_back("P2PKH", P2PKH(RandU160()));
        probes.emplace_back("P2SH", EncodeDestination(CScriptID(RandU160())));
        probes.emplace_back("P2WPKH", EncodeDestination(WitnessV0KeyHash(RandU160())));
        {
            WitnessV0ScriptHash wsh;
            std::memcpy(wsh.begin(), h256.begin(), 32);
            probes.emplace_back("P2WSH", EncodeDestination(wsh));
        }
        {
            WitnessV2Quantum q;
            std::memcpy(q.begin(), h256.begin(), 32);
            probes.emplace_back("QUANTUM", EncodeDestination(q));
        }

        for (const auto& p : probes) {
            BOOST_REQUIRE_MESSAGE(!p.second.empty(),
                "P3: failed to encode a " + p.first + " address (#" + std::to_string(i) + ")");

            bool sawUnsupported = false;
            bool threw = false;
            std::string msg;
            try {
                // addtrust with an explicit `from` avoids needing a loaded wallet.
                UniValue params = Arr({UniValue(p.second), UniValue((int64_t)80),
                                       UniValue(UniValue::VNUM, "3.00000000"),
                                       UniValue(std::string("accept")),
                                       UniValue(fromStr)});
                addtrust(MakeRequest(params));
            } catch (const UniValue& e) {
                threw = true;
                msg = RpcErrMessage(e);
                if (msg.find("Address type not supported") != std::string::npos) {
                    sawUnsupported = true;
                }
            } catch (const std::exception& e) {
                threw = true;
                msg = e.what();
            }

            BOOST_CHECK_MESSAGE(!sawUnsupported,
                "P3 (2.9): addtrust rejected a " + p.first +
                " address with \"Address type not supported\" (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(!threw,
                "P3 (2.9): addtrust threw for a valid " + p.first + " address: \"" + msg +
                "\" (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// P4 — Single-wallet trust graph.
//
// **Property 4: Expected Behavior**
//
// Edges created among the addresses of a single wallet store, list, and
// traverse correctly. Modelled as a chain A0->A1->...->An-1 of addresses
// (all belonging to one wallet): every edge is listed, the chain is
// traversable end-to-end, and the end-to-end reputation is non-zero.
//
// **Validates: Requirements 2.11**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p4_single_wallet_trust_graph_property)
{
    for (int s = 0; s < kSamplesDb; ++s) {
        ResetGlobalDb();
        CVM::TrustGraph tg(*CVM::g_cvmdb);

        const int n = 3 + static_cast<int>(InsecureRandRange(4)); // [3,6] wallet addresses
        std::vector<uint160> addr(n);
        for (int i = 0; i < n; ++i) addr[i] = RandU160();

        // Chain the wallet's own addresses together.
        for (int i = 0; i + 1 < n; ++i) {
            const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            BOOST_REQUIRE_MESSAGE(
                tg.AddTrustEdge(addr[i], addr[i + 1], weight, kBond, uint256(),
                                "intra-wallet"),
                "P4 (2.11): storing an intra-wallet edge must succeed (sample " +
                std::to_string(s) + ", hop " + std::to_string(i) + ")");
        }

        // All intra-wallet edges are listed.
        UniValue listing = listtrustrelations(MakeRequest(Arr({})));
        BOOST_CHECK_MESSAGE(listing["count"].get_int() == n - 1,
            "P4 (2.11): listtrustrelations count = " +
            std::to_string(listing["count"].get_int()) + " != " + std::to_string(n - 1) +
            " intra-wallet edges (sample " + std::to_string(s) + ")");

        // The chain traverses end-to-end.
        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(addr[0], addr[n - 1], n);
        BOOST_CHECK_MESSAGE(paths.size() >= 1,
            "P4 (2.11): the intra-wallet chain A0->...->An-1 must be traversable "
            "(sample " + std::to_string(s) + ")");

        double rep = tg.GetWeightedReputation(addr[0], addr[n - 1], n);
        BOOST_CHECK_MESSAGE(rep != 0.0,
            "P4 (2.11): end-to-end intra-wallet reputation must be non-zero (sample " +
            std::to_string(s) + ")");
    }
}

// ===========================================================================
// P5 — Preservation of validation / traversal filters / self-view / shared
//      serialization (cross-check with task 10, cvm_wot_preserve_tests.cpp).
//
// **Property 5: Preservation**
//
// For inputs where the bug condition does NOT hold, the fixed code behaves as
// before: out-of-range weight and insufficient bond are rejected; maxdepth
// outside 1..10 is rejected; traversal skips low-weight and slashed edges;
// the self-view returns the average of non-slashed incoming trust; and shared
// records (PropagatedTrustEdge, BondedVote) still round-trip byte-for-byte.
//
// **Validates: Requirements 3.1, 3.2, 3.4, 3.5, 3.6, 3.7**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p5_preserved_validation_and_filters_property)
{
    // 3.1 / 3.2 — weight-range and bond validation still reject invalid input.
    {
        CVM::TrustGraph tg(*CVM::g_cvmdb);
        for (int i = 0; i < kSamples; ++i) {
            const uint160 from = RandU160();
            const uint160 to = RandU160();

            const int16_t badWeight = (i % 2 == 0)
                ? static_cast<int16_t>(101 + InsecureRandRange(100))
                : static_cast<int16_t>(-(101 + static_cast<int>(InsecureRandRange(100))));
            BOOST_CHECK_MESSAGE(
                !tg.AddTrustEdge(from, to, badWeight, kBond, uint256(), "bad"),
                "P5 (3.1): out-of-range weight " + std::to_string(badWeight) +
                " must be rejected (#" + std::to_string(i) + ")");

            const int16_t okWeight = static_cast<int16_t>(1 + InsecureRandRange(100)); // [1,100]
            const CAmount tooLow = RequiredBond(okWeight) - 1;
            BOOST_CHECK_MESSAGE(
                !tg.AddTrustEdge(from, to, okWeight, tooLow, uint256(), "low bond"),
                "P5 (3.2): bond below the required amount must be rejected (#" +
                std::to_string(i) + ")");
        }
    }

    // 3.6 — maxdepth outside 1..10 still rejected with the preserved message.
    {
        const std::string targetStr = P2PKH(RandU160());
        const std::string viewerStr = P2PKH(RandU160());
        for (int64_t depth : {int64_t(0), int64_t(-1), int64_t(11), int64_t(99)}) {
            bool threw = false;
            std::string msg;
            try {
                getweightedreputation(MakeRequest(Arr({UniValue(targetStr),
                                                       UniValue(viewerStr),
                                                       UniValue(depth)})));
            } catch (const UniValue& e) {
                threw = true;
                msg = RpcErrMessage(e);
            }
            BOOST_CHECK_MESSAGE(
                threw && msg.find("Max depth must be between 1 and 10") != std::string::npos,
                "P5 (3.6): maxdepth " + std::to_string(depth) +
                " must be rejected with the preserved message; got \"" + msg + "\"");
        }
    }

    // 3.4 — traversal still skips low-weight (< 10) and slashed edges.
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 A = RandU160();
        const uint160 B = RandU160();
        const int16_t lowWeight = static_cast<int16_t>(InsecureRandRange(10)); // [0,9]
        BOOST_REQUIRE(tg.AddTrustEdge(A, B, lowWeight, kBond, uint256(), "low"));

        std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(A, B, 3);
        BOOST_CHECK_MESSAGE(paths.empty(),
            "P5 (3.4): traversal must skip the low-weight (" + std::to_string(lowWeight) +
            " < 10) edge A->B (#" + std::to_string(i) + ")");
    }

    // 3.5 — self-view returns the average of non-slashed incoming trust.
    for (int i = 0; i < kSamplesDb; ++i) {
        CVM::CVMDatabase db(fs::temp_directory_path() / fs::unique_path(),
                            1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CVM::TrustGraph tg(db);

        const uint160 target = RandU160();
        const int m = 2 + static_cast<int>(InsecureRandRange(4)); // [2,5]
        double sum = 0.0;
        int count = 0;
        for (int k = 0; k < m; ++k) {
            const uint160 src = RandU160();
            const int16_t w = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            BOOST_REQUIRE(tg.AddTrustEdge(src, target, w, kBond, uint256(), "in"));
            sum += w; count++;
        }
        const double expected = count > 0 ? (sum / count) : 0.0;
        const double actual = tg.GetWeightedReputation(target, target, 3);
        BOOST_CHECK_MESSAGE(std::abs(actual - expected) < 1e-9,
            "P5 (3.5): self-view must equal the average of non-slashed incoming trust; "
            "expected " + std::to_string(expected) + ", got " + std::to_string(actual) +
            " (#" + std::to_string(i) + ")");
    }

    // 3.7 — shared records still round-trip byte-for-byte.
    for (int i = 0; i < kSamples; ++i) {
        CVM::PropagatedTrustEdge pin;
        pin.fromAddress = RandU160();
        pin.toAddress = RandU160();
        pin.originalTarget = RandU160();
        pin.sourceEdgeTx = InsecureRand256();
        pin.trustWeight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        pin.propagatedAt = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        pin.originalTimestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        pin.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));

        CDataStream ps(SER_DISK, CLIENT_VERSION);
        ps << pin;
        CVM::PropagatedTrustEdge pout;
        ps >> pout;
        BOOST_CHECK_MESSAGE(pin == pout,
            "P5 (3.7): PropagatedTrustEdge did not round-trip (#" + std::to_string(i) + ")");

        CVM::BondedVote vin;
        vin.voter = RandU160();
        vin.target = RandU160();
        vin.voteValue = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        vin.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        vin.bondTxHash = InsecureRand256();
        vin.timestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));
        vin.slashed = (InsecureRandRange(2) == 0);
        vin.slashTxHash = InsecureRand256();
        vin.reason = "v-" + std::to_string(i);

        CDataStream vs(SER_DISK, CLIENT_VERSION);
        vs << vin;
        CVM::BondedVote vout;
        vs >> vout;
        BOOST_CHECK_MESSAGE(vout.voter == vin.voter && vout.target == vin.target &&
                            vout.voteValue == vin.voteValue && vout.bondAmount == vin.bondAmount &&
                            vout.bondTxHash == vin.bondTxHash && vout.timestamp == vin.timestamp &&
                            vout.slashed == vin.slashed && vout.slashTxHash == vin.slashTxHash &&
                            vout.reason == vin.reason,
            "P5 (3.7): BondedVote did not round-trip (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// P6 — Preservation of legacy addresses / existing quantum handling / keying
//      consistency (cross-check with task 10, cvm_wot_preserve_tests.cpp).
//
// **Property 6: Preservation**
//
// Legacy P2PKH edges still store/list/traverse identically; existing address
// parsing/encoding (DecodeDestination / EncodeDestination / IsQuantumAddress)
// is unchanged; the legacy v1 on-chain CVMTrustEdgeData still decodes to the
// same fields; and the same address representation is used on write and read.
//
// **Validates: Requirements 3.3, 3.8, 3.9, 3.10**
// ===========================================================================
BOOST_AUTO_TEST_CASE(p6_preserved_legacy_and_keying_property)
{
    const CChainParams& params = Params();

    // 3.3 / 3.10 — legacy P2PKH edges store/list/traverse identically and are
    // retrievable by the same address representation used on write.
    {
        CVM::TrustGraph tg(*CVM::g_cvmdb);
        for (int i = 0; i < kSamplesDb; ++i) {
            const uint160 from = RandU160();
            const uint160 to = RandU160();
            const int16_t weight = static_cast<int16_t>(10 + InsecureRandRange(91)); // [10,100]
            const std::string reason = "legacy-" + std::to_string(i);

            BOOST_REQUIRE(tg.AddTrustEdge(from, to, weight, kBond, uint256(), reason));

            CVM::TrustEdge edge;
            BOOST_REQUIRE_MESSAGE(tg.GetTrustEdge(from, to, edge),
                "P6 (3.3): legacy edge must be retrievable (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(edge.trustWeight == weight && edge.reason == reason &&
                                !edge.slashed,
                "P6 (3.3): legacy edge fields changed (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(edge.fromAddress == CVM::TrustNodeId::FromLegacyUint160(from) &&
                                edge.toAddress == CVM::TrustNodeId::FromLegacyUint160(to),
                "P6 (3.10): legacy edge identity representation changed (#" +
                std::to_string(i) + ")");

            std::vector<CVM::TrustPath> paths = tg.FindTrustPaths(from, to, 3);
            BOOST_CHECK_MESSAGE(paths.size() >= 1,
                "P6 (3.3): direct legacy path from->to not found (#" + std::to_string(i) + ")");
        }
    }

    // 3.9 — existing address encode/decode + quantum recognition unchanged.
    for (int i = 0; i < kSamples; ++i) {
        uint160 h160 = RandU160();
        uint256 h256 = InsecureRand256();

        std::vector<std::pair<std::string, CTxDestination>> dests;
        dests.emplace_back("P2PKH", CTxDestination(CKeyID(h160)));
        dests.emplace_back("P2SH", CTxDestination(CScriptID(h160)));
        dests.emplace_back("P2WPKH", CTxDestination(WitnessV0KeyHash(h160)));
        {
            WitnessV0ScriptHash wsh;
            std::memcpy(wsh.begin(), h256.begin(), 32);
            dests.emplace_back("P2WSH", CTxDestination(wsh));
        }
        {
            WitnessV2Quantum q;
            std::memcpy(q.begin(), h256.begin(), 32);
            dests.emplace_back("QUANTUM", CTxDestination(q));
        }

        for (const auto& d : dests) {
            const std::string encoded = EncodeDestination(d.second);
            BOOST_REQUIRE(!encoded.empty());
            BOOST_CHECK_MESSAGE(DecodeDestination(encoded) == d.second,
                "P6 (3.9): DecodeDestination(EncodeDestination(x)) != x for " + d.first +
                " (#" + std::to_string(i) + ")");
            const bool isQuantum = address::IsQuantumAddress(encoded, params);
            BOOST_CHECK_MESSAGE(isQuantum == (d.first == "QUANTUM"),
                "P6 (3.9): IsQuantumAddress mismatch for " + d.first + " (#" +
                std::to_string(i) + ")");
        }
    }

    // 3.9 — legacy v1 on-chain CVMTrustEdgeData still emits/decodes as 54 bytes.
    for (int i = 0; i < kSamples; ++i) {
        const uint160 from = RandU160();
        const uint160 to = RandU160();
        const int16_t weight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        const CAmount bond = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        const uint32_t ts = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));

        CVM::CVMTrustEdgeData in;
        in.fromAddress = from;
        in.toAddress = to;
        in.weight = weight;
        in.bondAmount = bond;
        in.timestamp = ts;

        std::vector<uint8_t> bytes = in.Serialize();
        BOOST_CHECK_MESSAGE(bytes.size() == 54,
            "P6 (3.9): a pure-uint160 edge must serialize as 54-byte v1; got " +
            std::to_string(bytes.size()) + " (#" + std::to_string(i) + ")");

        CVM::CVMTrustEdgeData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "P6 (3.9): v1 payload must deserialize (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.fromAddress == from && out.toAddress == to &&
                            out.weight == weight && out.bondAmount == bond &&
                            out.timestamp == ts,
            "P6 (3.9): legacy v1 on-chain payload decoded to different fields (#" +
            std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
