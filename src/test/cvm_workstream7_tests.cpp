// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 7 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 17: "Write Workstream-7 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 7 = Cross-chain bridging & oracle trust
 * (bugfix.md clauses 1.33, 1.34, 1.35, 1.36; preservation 3.15).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 14 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.33, 2.34,
 *       2.35, 2.36). Written BEFORE the fix, they are EXPECTED TO FAIL on the
 *       current (unfixed) code — every failure is a counterexample confirming a
 *       defect. After the Workstream-7 fix lands (task 18) the SAME tests must
 *       pass. Prefixed `p14_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clause 3.15). These
 *       capture behaviour Workstream 7 must NOT change: a cross-chain proof or
 *       attestation with a genuinely valid signature and a genuinely committed
 *       (merkle-consistent) source state is still accepted. They are EXPECTED
 *       TO PASS on the unfixed code (baseline behaviour to preserve). Prefixed
 *       `preserve_`.
 *
 * ---------------------------------------------------------------------------
 * Defects under test and their testability:
 *
 *   1.33  cross_chain_bridge.cpp ReputationProof::Verify only checks that the
 *         proof bytes are non-empty (plus timestamp/range sanity) and does NOT
 *         verify the proof against the source chain's committed state, so a
 *         non-empty but INVALID proof is accepted.
 *         COVERED: ReputationProof::Verify is a public struct method — a
 *         non-empty garbage proof must be REJECTED after the fix.
 *
 *   1.34  cross_chain_bridge.cpp SendTrustAttestation (LayerZero) only logs the
 *         intent and stores locally, then reports success unconditionally, even
 *         when no bridge endpoint is configured (nothing is dispatched).
 *         COVERED: SendTrustAttestation returns bool — sending to an active but
 *         endpoint-less chain must NOT report success after the fix.
 *         PARTIAL/DEFERRED: SendReputationProofViaCCIP returns `void`, so there
 *         is no return value to observe; its dispatch-success semantics are
 *         covered by the Workstream-7 fix (task 18) and multi-node integration
 *         (task 30). No un-compilable code is emitted for it here.
 *
 *   1.35  cross_chain_bridge.cpp GenerateTrustStateProof leaves `merkleProof`
 *         empty (the "simplified hash" stateRoot is never accompanied by a
 *         path), so VerifyTrustStateProof of a freshly generated proof fails;
 *         and GetAttestations returns an empty list (cached-only placeholder)
 *         even for attestations committed to the database.
 *         COVERED: (i) a freshly generated proof must verify against its own
 *         real state root; (ii) a freshly committed (uncached) attestation must
 *         be returned by GetAttestations after the fix.
 *
 *   1.36  trust_context.cpp IsKnownLayerZeroOracle accepts ANY valid public key
 *         instead of checking a per-chain trusted-oracle registry.
 *         DEFERRED: IsKnownLayerZeroOracle is a PRIVATE method whose only caller
 *         (VerifyLayerZeroAttestation) is itself private and reachable only
 *         through the private VerifyCrossChainAttestation/VerifyCrossChainTrust
 *         chain. The public AddCrossChainAttestation path sets `verified=false`
 *         and never triggers verification. Observing the accept-any behaviour
 *         would require constructing a full 130-byte LayerZero proof with
 *         genuinely recoverable secp256k1 oracle+relayer signatures AND an
 *         initialised global bridge — i.e. the full P2P attestation flow. There
 *         is no unit-level seam, so 1.36 is covered by the Workstream-7 fix
 *         (task 18) and multi-node integration (task 30). Adding a public
 *         accessor purely to observe it would change the surface under test and
 *         is intentionally avoided.
 *
 * Expected-Behavior targets: 2.33, 2.34, 2.35, 2.36
 * Preservation: 3.15
 * Requirements: 1.33, 1.34, 1.35, 1.36, 3.15
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/cross_chain_bridge.h>
#include <cvm/trust_attestation.h>
#include <cvm/cvmdb.h>

#include <fs.h>
#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>
#include <vector>

namespace {

static constexpr int kSamples = 64;

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

// A TrustAttestation that passes ValidateAttestation (range + fresh timestamp)
// but carries NO signature material (used where only validation, not signature,
// matters — e.g. the local LayerZero send path).
CVM::TrustAttestation MakeValidAttestation(const uint160& address, int16_t score)
{
    CVM::TrustAttestation att;
    att.address = address;
    att.trustScore = score;                       // in [0,100]
    att.source = CVM::AttestationSource::OTHER;    // no per-chain maxAge override
    att.timestamp = static_cast<uint64_t>(GetTime());
    return att;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream7_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 14) — EXPECTED TO FAIL on unfixed code     #
// ###########################################################################

// ===========================================================================
// Property 14 (1.33) — inbound proof verified against source chain state.
//
// Expected (2.33): ReputationProof::Verify SHALL verify the proof against the
// source chain's committed state. A necessary condition: a non-empty but
// otherwise INVALID (garbage) proof must be REJECTED.
//
// UNFIXED: Verify only checks proof/signature non-empty + timestamp/range, then
// `return !proof.empty();` — so a garbage proof of valid length is accepted ->
// the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p14_1_33_reputation_proof_rejects_invalid)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::ReputationProof proof;
        proof.address = RandAddress();
        proof.reputation = static_cast<uint8_t>(InsecureRandRange(101)); // valid 0..100
        proof.timestamp = static_cast<uint64_t>(GetTime());              // fresh
        proof.sourceChainSelector = 1;

        // Non-empty but meaningless proof + signature bytes (not tied to any
        // committed source-chain state).
        uint256 g1 = InsecureRand256();
        uint256 g2 = InsecureRand256();
        proof.proof.assign(g1.begin(), g1.end());
        proof.signature.assign(g2.begin(), g2.end());

        BOOST_CHECK_MESSAGE(!proof.Verify(),
            "P14 (1.33): ReputationProof::Verify accepted a non-empty but INVALID "
            "proof (sample #" + std::to_string(i) + "); it only checks the proof "
            "bytes are non-empty and does not verify against the source chain's "
            "committed state.");
    }
}

// ===========================================================================
// Property 14 (1.34) — LayerZero send reports success only when dispatched.
//
// Expected (2.34): SendTrustAttestation SHALL transmit via the corresponding
// endpoint and report success ONLY when the message is dispatched. With no
// bridge endpoint configured for the destination chain, nothing can be
// dispatched, so it must NOT report success.
//
// UNFIXED: the send path only logs the intent and stores locally, then always
// `return true`, even though the default Ethereum chain (id 1) has an EMPTY
// bridgeEndpoint -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p14_1_34_send_without_endpoint_reports_failure)
{
    // No database needed: the local StoreAttestation is best-effort and its
    // result is ignored by the send path; we observe only the returned success.
    CVM::CrossChainTrustBridge bridge(nullptr);

    // Sanity: the default Ethereum chain is supported, active, endpoint-less.
    const CVM::ChainConfig* eth = bridge.GetChainConfig(1);
    BOOST_REQUIRE_MESSAGE(eth != nullptr && eth->isActive,
        "P14 (1.34): precondition — default Ethereum chain (id 1) must be "
        "supported and active.");
    BOOST_REQUIRE_MESSAGE(eth->bridgeEndpoint.empty(),
        "P14 (1.34): precondition — default Ethereum chain must have no bridge "
        "endpoint configured.");

    for (int i = 0; i < kSamples; ++i) {
        uint160 addr = RandAddress();
        CVM::TrustAttestation att =
            MakeValidAttestation(addr, static_cast<int16_t>(InsecureRandRange(101)));

        bool sent = bridge.SendTrustAttestation(/*dstChainId=*/1, addr, att);

        BOOST_CHECK_MESSAGE(!sent,
            "P14 (1.34): SendTrustAttestation reported success (sample #" +
            std::to_string(i) + ") for a chain with no bridge endpoint; it only "
            "logs the intent and stores locally without dispatching a cross-chain "
            "message, so it must not report success.");
    }
}

// ===========================================================================
// Property 14 (1.35a) — a freshly generated proof verifies against its root.
//
// Expected (2.35): merkle proofs SHALL be derived from the actual state trie so
// that verification against the committed state root succeeds for genuinely
// committed entries. A necessary condition: a proof produced by
// GenerateTrustStateProof must verify via VerifyTrustStateProof.
//
// UNFIXED: GenerateTrustStateProof computes a "simplified hash" stateRoot but
// leaves `merkleProof` EMPTY; VerifyMerkleProof returns false for an empty path,
// so VerifyTrustStateProof of the fresh proof fails -> the property FAILS.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p14_1_35_generated_proof_verifies_against_root)
{
    // db=nullptr keeps GenerateTrustStateProof off SecureHAT (uses default
    // score) while still exercising the proof/root construction path.
    CVM::CrossChainTrustBridge bridge(nullptr);

    // Chain 0 (Cascoin, self) is a default-supported source chain.
    BOOST_REQUIRE(bridge.IsChainSupported(0));

    for (int i = 0; i < 16; ++i) {
        uint160 addr = RandAddress();
        CVM::TrustStateProof proof = bridge.GenerateTrustStateProof(addr);

        BOOST_CHECK_MESSAGE(bridge.VerifyTrustStateProof(proof, /*sourceChain=*/0),
            "P14 (1.35): a freshly generated trust-state proof did not verify "
            "against its own state root (sample #" + std::to_string(i) + "); "
            "GenerateTrustStateProof leaves the merkle path empty instead of "
            "deriving it from the actual state trie.");
    }
}

// ===========================================================================
// Property 14 (1.35b) — reads return ALL committed attestations, not just cache.
//
// Expected (2.35): reading trust attestations SHALL return all committed
// attestations (iterate committed state), not only cached ones. A freshly
// committed attestation (never placed in the in-memory cache) must be returned.
//
// UNFIXED: GetAttestations returns an empty vector (the DB-iteration body is a
// placeholder that "returns what's in the cache") even after StoreAttestation
// has persisted the record -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p14_1_35_committed_attestation_is_returned)
{
    auto db = MakeTempDb();
    CVM::CrossChainTrustBridge bridge(db.get());

    uint160 addr = RandAddress();
    CVM::TrustAttestation att = MakeValidAttestation(addr, /*score=*/73);
    att.source = CVM::AttestationSource::ETHEREUM_MAINNET;

    BOOST_REQUIRE_MESSAGE(bridge.StoreAttestation(att),
        "P14 (1.35): precondition — committing an attestation to the database "
        "should succeed.");

    std::vector<CVM::TrustAttestation> got = bridge.GetAttestations(addr);

    BOOST_CHECK_MESSAGE(!got.empty(),
        "P14 (1.35): GetAttestations returned no attestations for an address "
        "whose attestation was just committed to the database; the read only "
        "returns cached attestations instead of iterating committed state.");
}

// NOTE — DEFERRED fix-property clauses in this suite (documented in the header):
//   1.34 (CCIP send, void return — no observable bool) and
//   1.36 (IsKnownLayerZeroOracle — private, reachable only through the full
//   private cross-chain verification chain / P2P attestation flow).
//   Both are covered by the Workstream-7 fix (task 18) and multi-node
//   integration (task 30). No un-compilable code is emitted for them here.

// ###########################################################################
// #  PRESERVATION TESTS (Property 21 / 3.15) — EXPECTED TO PASS on unfixed    #
// ###########################################################################

// ===========================================================================
// Preservation 3.15 — genuinely valid attestation signature still accepted.
//
// A cross-chain attestation carrying a genuinely valid secp256k1 signature over
// its own hash must be accepted by TrustAttestation::VerifySignature, and a
// forged / wrong-key signature must be rejected. This real signature
// verification is the behaviour Workstream 7 must preserve ("accept it once
// real verification is in place"). EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_15_valid_attestation_signature_accepted)
{
    for (int i = 0; i < 16; ++i) {
        CKey key;
        key.MakeNewKey(/*fCompressed=*/true);
        CPubKey pub = key.GetPubKey();
        BOOST_REQUIRE(pub.IsValid());

        CVM::TrustAttestation att = MakeValidAttestation(RandAddress(),
            static_cast<int16_t>(InsecureRandRange(101)));
        att.source = CVM::AttestationSource::ETHEREUM_MAINNET;
        att.attestorPubKey.assign(pub.begin(), pub.end());

        uint256 hash = att.GetHash();
        std::vector<uint8_t> sig;
        BOOST_REQUIRE(key.Sign(hash, sig));
        att.signature = sig;

        BOOST_CHECK_MESSAGE(att.VerifySignature(),
            "Preservation 3.15: a genuinely valid attestation signature was "
            "rejected (sample #" + std::to_string(i) + ").");

        // A wrong-key signature over the same hash must NOT verify.
        CKey wrongKey;
        wrongKey.MakeNewKey(true);
        std::vector<uint8_t> wrongSig;
        BOOST_REQUIRE(wrongKey.Sign(hash, wrongSig));
        CVM::TrustAttestation forged = att;
        forged.signature = wrongSig;
        BOOST_CHECK_MESSAGE(!forged.VerifySignature(),
            "Preservation 3.15: a wrong-key attestation signature was accepted "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Preservation 3.15 — genuinely committed merkle leaf still accepted.
//
// TrustStateProof::VerifyMerkleProof performs real merkle-path verification.
// A proof whose leaf and sibling path hash up to the declared state root must
// be accepted, and tampering the leaf (trust score) must break it. This
// verification math is the "committed source state" behaviour Workstream 7 must
// preserve. EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_15_committed_merkle_leaf_accepted)
{
    for (int i = 0; i < 16; ++i) {
        CVM::TrustStateProof proof;
        proof.address = RandAddress();
        proof.trustScore = static_cast<uint8_t>(InsecureRandRange(101));
        proof.blockHeight = InsecureRandRange(1000000ULL);

        // Leaf hash exactly as VerifyMerkleProof computes it.
        CHashWriter leafWriter(SER_GETHASH, 0);
        leafWriter << proof.address;
        leafWriter << proof.trustScore;
        leafWriter << proof.blockHeight;
        uint256 leaf = leafWriter.GetHash();

        // One genuine sibling; fold it in with the same ordering rule.
        uint256 sibling = InsecureRand256();
        CHashWriter rootWriter(SER_GETHASH, 0);
        if (leaf < sibling) {
            rootWriter << leaf << sibling;
        } else {
            rootWriter << sibling << leaf;
        }
        uint256 root = rootWriter.GetHash();

        proof.merkleProof = {sibling};
        proof.stateRoot = root;

        BOOST_CHECK_MESSAGE(proof.VerifyMerkleProof(),
            "Preservation 3.15: a genuinely committed merkle leaf failed "
            "verification (sample #" + std::to_string(i) + ").");

        // Tamper the committed value: the same path must no longer verify.
        CVM::TrustStateProof tampered = proof;
        tampered.trustScore = static_cast<uint8_t>((proof.trustScore + 1) % 101);
        BOOST_CHECK_MESSAGE(!tampered.VerifyMerkleProof(),
            "Preservation 3.15: a tampered merkle leaf still verified against the "
            "original root (sample #" + std::to_string(i) + ").");
    }
}

BOOST_AUTO_TEST_SUITE_END()
