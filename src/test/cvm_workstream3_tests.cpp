// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 3 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 9: "Write Workstream-3 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 3 = Reputation signatures & merkle proofs
 * (bugfix.md clauses 1.5, 1.6, 1.7; preservation 3.6).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 10 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.5, 2.6, 2.7).
 *       Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 *       (unfixed) code — every failure is a counterexample confirming a defect.
 *       After the Workstream-3 fix lands (task 10) the SAME tests must pass.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clause 3.6). These
 *       capture the merkle-verification MATH for genuinely committed leaves,
 *       which is already correct and must remain unchanged. They are EXPECTED
 *       TO PASS on the unfixed code (baseline behaviour to preserve).
 *
 * Defects under test (design.md Fix Implementation → Workstream 3):
 *   1.5  CreateStateProof fills the signature with the first 32 bytes of the
 *        proof hash (placeholder) and derives the state root from a fixed
 *        string + current time rather than from the committed reputation state.
 *   1.6  Signature verification only checks the signature is >= 64 bytes; a
 *        forged signature of sufficient length passes (no ECDSA verification).
 *   1.7  BuildMerkleProof fabricates a deterministic sibling hash instead of
 *        querying the real reputation state tree, so the proof does not attest
 *        to any committed state (it does not verify against the committed root).
 *
 * Expected-Behavior targets: 2.5, 2.6, 2.7   Preservation: 3.6
 * Requirements: 1.5, 1.6, 1.7, 3.6
 *
 * Labelling: fix-property cases are prefixed `p10_` and are EXPECTED TO FAIL on
 * unfixed code. Preservation cases are prefixed `preserve_3_6_` and are EXPECTED
 * TO PASS on unfixed code.
 */

#include <cvm/reputation_signature.h>

#include <hash.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <utiltime.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace {

static constexpr int kSamples = 128;

// Build a random non-null 20-byte address.
uint160 RandAddress()
{
    uint160 addr;
    uint256 r = InsecureRand256();
    std::memcpy(addr.begin(), r.begin(), 20);
    if (addr.IsNull()) *addr.begin() = 0x01;
    return addr;
}

// Replicate EXACTLY the pair-combination used by the reputation merkle
// verifier (ReputationMerkleUtils::VerifyMerkleProofWithLeaf): the smaller hash
// is serialised first, then the larger, and the pair is hashed with a
// CHashWriter. Used to hand-build genuinely-committed merkle trees for the
// preservation (3.6) tests.
uint256 CombineMerkle(const uint256& a, const uint256& b)
{
    CHashWriter hasher(SER_GETHASH, 0);
    if (a < b) {
        hasher << a;
        hasher << b;
    } else {
        hasher << b;
        hasher << a;
    }
    return hasher.GetHash();
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream3_tests, BasicTestingSetup)

// ===========================================================================
// Property 10 (1.5) — A created state proof carries a REAL signature, not the
// first 32 bytes of the proof hash.
//
// Expected (2.5): the system SHALL produce a real validator signature over the
// proof data. UNFIXED: CreateStateProof does
//   proof.signature.resize(65);
//   memcpy(proof.signature.data(), proof_hash.begin(), 32);
// i.e. the first 32 signature bytes equal the first 32 bytes of the proof hash
// (and bytes 32..64 are zero). We assert that this placeholder pattern does NOT
// hold. On unfixed code it DOES hold -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p10_state_proof_signature_not_placeholder)
{
    CVM::ReputationSignatureManager mgr;

    for (int i = 0; i < kSamples; ++i) {
        uint160 addr = RandAddress();
        uint32_t score = static_cast<uint32_t>(InsecureRandRange(101)); // [0,100]
        int height = static_cast<int>(InsecureRandRange(1000000));

        CVM::ReputationStateProof proof = mgr.CreateStateProof(addr, score, height);

        // GetHash() does not depend on the signature/merkle fields, so we can
        // recompute the exact proof hash the placeholder was copied from.
        uint256 proofHash = proof.GetHash();

        BOOST_REQUIRE_GE(proof.signature.size(), size_t(32));

        // Does the signature start with the first 32 bytes of the proof hash?
        bool placeholderPattern =
            std::memcmp(proof.signature.data(), proofHash.begin(), 32) == 0;

        // EXPECTED (post-fix): a real signature is NOT a copy of the proof hash.
        // UNFIXED: placeholderPattern == true.
        BOOST_CHECK_MESSAGE(!placeholderPattern,
            "P10 (1.5): CreateStateProof produced a placeholder signature whose "
            "first 32 bytes equal the first 32 bytes of the proof hash (" +
            proofHash.GetHex() + "); no real validator signature is produced "
            "(sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 10 (1.5) — The state root derives from the committed reputation
// state, NOT from a fixed string + current time.
//
// Expected (2.5): the state root SHALL derive from the committed reputation
// state. Therefore two proofs for the SAME committed state (same address,
// reputation, height) MUST have the SAME state root, regardless of wall-clock
// time. UNFIXED: ComputeStateRoot() returns Hash("reputation_state_root" ||
// GetTime()), so advancing (mock) time between the two calls yields DIFFERENT
// roots -> the property FAILS (counterexample), confirming the time-based root.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p10_state_root_from_committed_state_not_time)
{
    CVM::ReputationSignatureManager mgr;

    for (int i = 0; i < 32; ++i) {
        uint160 addr = RandAddress();
        uint32_t score = static_cast<uint32_t>(InsecureRandRange(101));
        int height = 1000 + i;

        // Two snapshots of the SAME committed state, taken at two different
        // wall-clock times.
        SetMockTime(1700000000 + i);
        CVM::ReputationStateProof p1 = mgr.CreateStateProof(addr, score, height);

        SetMockTime(1700000000 + i + 500);
        CVM::ReputationStateProof p2 = mgr.CreateStateProof(addr, score, height);

        SetMockTime(0); // restore

        // EXPECTED (post-fix): the state root reflects the committed state, which
        // is identical, so the roots match. UNFIXED: root depends on GetTime().
        BOOST_CHECK_MESSAGE(p1.state_root == p2.state_root,
            "P10 (1.5): state root changed between two snapshots of the SAME "
            "committed state (" + p1.state_root.GetHex() + " vs " +
            p2.state_root.GetHex() + "); it is derived from a fixed string + "
            "current time rather than the committed reputation state (sample #" +
            std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 10 (1.6) — Signature verification rejects a forged signature of
// valid length.
//
// Expected (2.6): verification SHALL perform ECDSA verification against the
// signer's public key and reject signatures that do not verify, even if they
// are of valid length. UNFIXED: ReputationSignature::Verify only checks
// ecdsa_signature.size() >= 64 (and non-empty), so a forged >= 64-byte
// signature that could never have been produced over the message is ACCEPTED
// -> the property FAILS (counterexample), confirming the length-only check.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p10_verify_rejects_forged_signature)
{
    for (int i = 0; i < kSamples; ++i) {
        uint256 messageHash = InsecureRand256();

        // A forged "signature" of a plausible ECDSA length (64..72 bytes). No key
        // could have produced these random bytes over the message, so real ECDSA
        // verification must fail.
        size_t sigLen = 64 + InsecureRandRange(9); // [64,72]
        std::vector<uint8_t> forged(sigLen);
        for (auto& b : forged) b = static_cast<uint8_t>(InsecureRandRange(256));

        CVM::ReputationSignature sig;
        sig.ecdsa_signature = forged;
        sig.signer_address = RandAddress();
        sig.signer_reputation = static_cast<uint32_t>(InsecureRandRange(101));
        sig.signature_timestamp = 1700000000;
        sig.reputation_proof_hash = InsecureRand256();

        bool accepted = sig.Verify(messageHash);

        // EXPECTED (post-fix): a forged signature does not verify -> rejected.
        // UNFIXED: the length-only check accepts it.
        BOOST_CHECK_MESSAGE(!accepted,
            "P10 (1.6): ReputationSignature::Verify accepted a forged " +
            std::to_string(sigLen) + "-byte signature that cannot verify against "
            "any key; verification is a length-only check with no ECDSA "
            "verification (sample #" + std::to_string(i) + ").");
    }
}

// A state-proof-level variant of 1.6: a state proof whose signature is forged
// (>= 64 random bytes) must be rejected by VerifyStateProof. The proof is built
// with a valid score/timestamp/height and no merkle proof so the ONLY reason to
// reject is the (forged) signature. UNFIXED: the >= 64-byte length check passes.
BOOST_AUTO_TEST_CASE(p10_state_proof_rejects_forged_signature)
{
    CVM::ReputationSignatureManager mgr;

    for (int i = 0; i < 32; ++i) {
        CVM::ReputationStateProof proof;
        proof.address = RandAddress();
        proof.reputation_score = static_cast<uint32_t>(InsecureRandRange(101));
        proof.timestamp = GetTime();
        proof.block_height = 100 + i;
        proof.state_root = InsecureRand256();
        // No merkle_proof: isolate the signature-verification path.

        size_t sigLen = 64 + InsecureRandRange(9);
        proof.signature.resize(sigLen);
        for (auto& b : proof.signature) b = static_cast<uint8_t>(InsecureRandRange(256));

        bool accepted = mgr.VerifyStateProof(proof);

        BOOST_CHECK_MESSAGE(!accepted,
            "P10 (1.6): VerifyStateProof accepted a proof carrying a forged " +
            std::to_string(sigLen) + "-byte signature; the signature is checked "
            "by length only (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 10 (1.7) — A merkle proof built for a genuinely committed entry
// verifies against the committed state root.
//
// Expected (2.7): the proof SHALL derive from the actual reputation state tree
// so that verification against the committed state root succeeds for genuinely
// committed entries. UNFIXED: BuildMerkleProof fabricates a deterministic
// sibling hash unrelated to ComputeStateRoot()'s output, so the built proof
// does NOT verify against the proof's own state root -> the property FAILS
// (counterexample), confirming the proof attests to no committed state.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p10_merkle_proof_verifies_for_committed_entry)
{
    CVM::ReputationSignatureManager mgr;

    for (int i = 0; i < 32; ++i) {
        uint160 addr = RandAddress();
        uint32_t score = static_cast<uint32_t>(InsecureRandRange(101));
        int height = 500 + i;

        CVM::ReputationStateProof proof = mgr.CreateStateProof(addr, score, height);
        BOOST_REQUIRE_MESSAGE(!proof.merkle_proof.empty(),
            "P10 (1.7): expected CreateStateProof to attach a merkle proof (#" +
            std::to_string(i) + ")");

        // Recompute the committed leaf and verify the built proof against the
        // proof's own (committed) state root using the unchanged merkle math.
        uint256 leaf = CVM::ReputationMerkleUtils::ComputeReputationLeafHash(
            addr, score, proof.timestamp);
        bool verifies = CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(
            proof.state_root, leaf, proof.merkle_proof);

        // EXPECTED (post-fix): a proof for a committed entry verifies against the
        // committed root. UNFIXED: fabricated sibling + time-based root are
        // disconnected, so verification fails.
        BOOST_CHECK_MESSAGE(verifies,
            "P10 (1.7): the merkle proof built for a genuinely committed entry "
            "does NOT verify against its own committed state root (" +
            proof.state_root.GetHex() + "); the sibling hash is fabricated and "
            "does not derive from the reputation state tree (sample #" +
            std::to_string(i) + ").");
    }
}

// ===========================================================================
// Preservation 3.6 — Merkle verification MATH for genuinely committed leaves.
//
// The generic merkle-verification math (smaller-hash-first pair combination) is
// already correct and is NOT changed by Workstream 3. We hand-build genuine
// merkle trees using the SAME combination rule the verifier uses and assert a
// correctly-constructed proof for a committed leaf verifies against the root.
// EXPECTED: PASS on unfixed code (baseline behaviour to preserve).
// ===========================================================================

// Two-leaf tree: root = Combine(leaf, sibling); proof for leaf = [sibling].
BOOST_AUTO_TEST_CASE(preserve_3_6_merkle_math_two_leaf_golden)
{
    uint256 leaf = InsecureRand256();
    uint256 sibling = InsecureRand256();
    uint256 root = CombineMerkle(leaf, sibling);

    std::vector<uint256> proof{sibling};

    BOOST_CHECK_MESSAGE(
        CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(root, leaf, proof),
        "Preservation 3.6: a correctly-constructed 2-leaf proof for a committed "
        "leaf must verify against the root.");

    // The member verifier on ReputationStateProof uses the same math.
    CVM::ReputationStateProof p;
    BOOST_CHECK_MESSAGE(
        p.VerifyReputationMerkleProof(root, leaf, proof),
        "Preservation 3.6: ReputationStateProof::VerifyReputationMerkleProof must "
        "verify the same 2-leaf committed proof.");

    // Empty proof => leaf must equal root (single-element tree).
    BOOST_CHECK(CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(
        leaf, leaf, std::vector<uint256>{}));
    BOOST_CHECK(!CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(
        root, leaf, std::vector<uint256>{}));
}

// Property 21 (3.6): for a random 4-leaf tree, the correctly-constructed proof
// for EACH committed leaf verifies against the root; a tampered proof does not.
BOOST_AUTO_TEST_CASE(preserve_3_6_merkle_math_four_leaf_property)
{
    for (int i = 0; i < kSamples; ++i) {
        uint256 l0 = InsecureRand256();
        uint256 l1 = InsecureRand256();
        uint256 l2 = InsecureRand256();
        uint256 l3 = InsecureRand256();

        uint256 n01 = CombineMerkle(l0, l1);
        uint256 n23 = CombineMerkle(l2, l3);
        uint256 root = CombineMerkle(n01, n23);

        // Proofs (sibling hashes from leaf to root).
        std::vector<std::pair<uint256, std::vector<uint256>>> cases = {
            {l0, {l1, n23}},
            {l1, {l0, n23}},
            {l2, {l3, n01}},
            {l3, {l2, n01}},
        };

        for (const auto& c : cases) {
            BOOST_REQUIRE_MESSAGE(
                CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(
                    root, c.first, c.second),
                "Preservation 3.6 (property): committed-leaf proof failed to "
                "verify at sample #" + std::to_string(i));
        }

        // Tamper with a sibling hash: verification must fail.
        std::vector<uint256> tampered = {l1, n23};
        *tampered[0].begin() ^= 0xFF;
        BOOST_REQUIRE_MESSAGE(
            !CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(root, l0, tampered),
            "Preservation 3.6 (property): a tampered proof must NOT verify at "
            "sample #" + std::to_string(i));
    }
}

// Property 21 (3.6): variable-depth trees (2^d committed leaves) — the proof for
// a random committed leaf verifies against the root. Exercises the multi-step
// walk of the unchanged verifier for depths 1..6.
BOOST_AUTO_TEST_CASE(preserve_3_6_merkle_math_variable_depth_property)
{
    for (int depth = 1; depth <= 6; ++depth) {
        const size_t n = size_t(1) << depth;

        for (int rep = 0; rep < 8; ++rep) {
            // Random committed leaves.
            std::vector<uint256> level(n);
            for (auto& h : level) h = InsecureRand256();

            // Pick a target leaf and build its proof while folding up the tree.
            size_t idx = static_cast<size_t>(InsecureRandRange(n));
            uint256 targetLeaf = level[idx];
            std::vector<uint256> proof;

            size_t pos = idx;
            std::vector<uint256> cur = level;
            while (cur.size() > 1) {
                size_t sibling = (pos ^ 1);
                proof.push_back(cur[sibling]);

                std::vector<uint256> next(cur.size() / 2);
                for (size_t k = 0; k < next.size(); ++k) {
                    next[k] = CombineMerkle(cur[2 * k], cur[2 * k + 1]);
                }
                cur = next;
                pos /= 2;
            }
            uint256 root = cur[0];

            BOOST_REQUIRE_MESSAGE(
                CVM::ReputationMerkleUtils::VerifyMerkleProofWithLeaf(
                    root, targetLeaf, proof),
                "Preservation 3.6 (property): committed-leaf proof failed at depth "
                + std::to_string(depth) + ", rep " + std::to_string(rep));
        }
    }
}

// Preservation 3.6: the reputation-specific verifier (leaf built from
// address||reputation||timestamp) verifies a correctly-constructed 2-leaf proof
// for a committed entry. This is the leaf-derivation + math combination that
// must remain correct for genuinely committed leaves.
BOOST_AUTO_TEST_CASE(preserve_3_6_reputation_leaf_committed_verifies)
{
    for (int i = 0; i < 64; ++i) {
        uint160 addr = RandAddress();
        uint32_t reputation = static_cast<uint32_t>(InsecureRandRange(101));
        int64_t timestamp = 1700000000 + i;

        uint256 leaf = CVM::ReputationMerkleUtils::ComputeReputationLeafHash(
            addr, reputation, timestamp);
        uint256 sibling = InsecureRand256();
        uint256 root = CombineMerkle(leaf, sibling);

        std::vector<uint256> proof{sibling};

        BOOST_REQUIRE_MESSAGE(
            CVM::ReputationMerkleUtils::VerifyReputationMerkleProof(
                root, addr, reputation, timestamp, proof),
            "Preservation 3.6: reputation merkle proof for a committed leaf must "
            "verify (sample #" + std::to_string(i) + ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
