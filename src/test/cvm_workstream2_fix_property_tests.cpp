// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 2 Exploratory Fix-Property Test Suite
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 6: "Write Workstream-2 exploratory fix-property tests (BEFORE fix)"
 *
 * Workstream 2 = Core-VM opcode handlers
 * (bugfix.md clauses 1.23, 1.24, 1.25, 1.26).
 *
 * PURPOSE
 * -------
 * These are the authoritative *fix-property* tests for Workstream 2. Each test
 * encodes the EXPECTED (post-fix, per Expected-Behavior clause 2.x) behaviour as
 * a property, primarily property-based (randomised) per the design's Scoped PBT
 * approach. Written BEFORE the fix, they are EXPECTED TO FAIL on the current
 * (unfixed) code — every failure is a counterexample confirming the defect.
 * After the Workstream-2 fixes land (task 8.3) the SAME tests must pass
 * unchanged.
 *
 * Properties covered (design.md Correctness Properties):
 *   P8  Bug Condition — OP_VERIFY_SIG family enforces real verification   (1.23)
 *   P9  Bug Condition — OP_BALANCE / OP_CALL / OP_LOG functional  (1.24,1.25,1.26)
 *
 * Scoped PBT approach (design):
 *   - random (msg, key) pairs -> OP_VERIFY_SIG pushes 1 IFF the signature
 *     genuinely verifies. A forged signature / wrong key MUST push 0 (P8).
 *
 * Cover (per task 6):
 *   - forged signature pushes 1 on unfixed code                          (1.23)
 *   - OP_BALANCE pushes 0 for a funded account                           (1.24)
 *   - OP_CALL / CallContract returns "not fully implemented"             (1.25)
 *   - OP_LOG consumes nothing / emits nothing                            (1.26)
 *
 * Requirements: 1.23, 1.24, 1.25, 1.26
 *
 * INTERFACE NOTE
 * --------------
 * The current core VM carries stack values as 256-bit `arith_uint256` words, so
 * a full 64–72 byte ECDSA signature and 33/65 byte public key cannot be encoded
 * into a single stack slot. The unfixed `OP_VERIFY_SIG` family exploits this by
 * treating the signature slot as a mere *size indicator* and hardcoding
 * `verifyResult = true`. This suite therefore surfaces the defect through the
 * robustly-expressible direction required by clause 1.23: a signature that does
 * NOT genuinely verify (forged bytes / wrong key) MUST make the opcode push 0.
 * On the unfixed code it pushes 1 — the counterexample. The complementary
 * acceptance direction (genuinely valid signatures still push 1, clause 3.11) is
 * covered by the Workstream-2 preservation suite (task 7). Genuine and forged
 * signatures here are produced with the project's real secp256k1 CKey/CPubKey.
 */

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>

#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <arith_uint256.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Cheap opcode-level properties can afford many samples.
static constexpr int kSamples = 256;

// Append a PUSH<size> immediate carrying the given bytes (size must be 1..32).
// The VM reads the bytes big-endian into an arith_uint256 stack word.
void EmitPush(std::vector<uint8_t>& code, const std::vector<uint8_t>& bytes)
{
    BOOST_REQUIRE(!bytes.empty() && bytes.size() <= 32);
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
    code.push_back(static_cast<uint8_t>(bytes.size()));
    code.insert(code.end(), bytes.begin(), bytes.end());
}

// Convenience: push a single-byte value.
void EmitPush1(std::vector<uint8_t>& code, uint8_t value)
{
    EmitPush(code, std::vector<uint8_t>{value});
}

// Push a full 32-byte word.
void EmitPush32(std::vector<uint8_t>& code, const uint256& word)
{
    EmitPush(code, std::vector<uint8_t>(word.begin(), word.end()));
}

// Emit a length-prefixed variable-length byte field for the OP_VERIFY_SIG
// family, matching the encoding decoded by PopVarBytes in cvm.cpp:
//   push word_0, word_1, ... word_{W-1}, then the byte length (W=ceil(L/32)).
// Each word_c carries field bytes [c*32, min((c+1)*32, L)) big-endian, exactly
// as OP_PUSH folds them; the length word ends up on top of the stack.
void EmitPushVarBytes(std::vector<uint8_t>& code, const std::vector<uint8_t>& field)
{
    BOOST_REQUIRE(!field.empty() && field.size() <= 255);
    const size_t L = field.size();
    const size_t W = (L + 31) / 32;
    for (size_t c = 0; c < W; ++c) {
        const size_t start = c * 32;
        const size_t chunkLen = std::min<size_t>(32, L - start);
        std::vector<uint8_t> chunk(field.begin() + start, field.begin() + start + chunkLen);
        EmitPush(code, chunk);
    }
    EmitPush1(code, static_cast<uint8_t>(L));
}

// Build a VERIFY-SIG program presenting (message, signature, pubkey) to
// `verifyOp` in the multi-word encoding defined by task 8.1 (decoded by
// PopVarBytes in cvm.cpp). Pop order in the handler is pubkey, signature,
// message — so we push the 32-byte message word first, then the signature
// field, then the pubkey field (the pubkey ends up on top). The full signature
// and public-key bytes are supplied so the handler performs real secp256k1
// verification against them.
std::vector<uint8_t> MakeVerifySigProgram(const uint256& messageHash,
                                          const std::vector<uint8_t>& signature,
                                          const std::vector<uint8_t>& pubkey,
                                          CVM::OpCode verifyOp)
{
    std::vector<uint8_t> code;
    EmitPush32(code, messageHash);
    EmitPushVarBytes(code, signature);
    EmitPushVarBytes(code, pubkey);
    code.push_back(static_cast<uint8_t>(verifyOp));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));
    return code;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream2_fix_property_tests, BasicTestingSetup)

// ===========================================================================
// Property 8 — OP_VERIFY_SIG family enforces real verification.         (1.23)
//
// Expected (2.23): OP_VERIFY_SIG / OP_VERIFY_SIG_ECDSA / OP_VERIFY_SIG_QUANTUM
// SHALL verify the signature against the message and public key and push 1 only
// for a genuinely valid signature.
//
// Scoped PBT: for random (msg, key) pairs we build, with the project's real
// secp256k1 CKey/CPubKey:
//   - a GENUINE signature (sanity: it verifies against its own pubkey), and
//   - a FORGED case (a valid signature checked against the WRONG public key, or
//     random garbage of a valid length) that genuinely does NOT verify.
// The opcode is then driven with the FORGED case and MUST push 0.
//
// UNFIXED: HandleCrypto hardcodes `verifyResult = true` on the ECDSA/auto path,
// so the forged case pushes 1 -> the property FAILS (counterexample), confirming
// the signature-verification bypass (1.23).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p8_verify_sig_rejects_forged_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // --- Real secp256k1 key material (genuine vs. wrong key). ---
        CKey signingKey;
        signingKey.MakeNewKey(/*fCompressed=*/true);
        CPubKey signingPub = signingKey.GetPubKey();
        BOOST_REQUIRE(signingPub.IsValid());

        CKey wrongKey;
        wrongKey.MakeNewKey(/*fCompressed=*/true);
        CPubKey wrongPub = wrongKey.GetPubKey();
        BOOST_REQUIRE(wrongPub.IsValid());

        uint256 messageHash = InsecureRand256();

        std::vector<uint8_t> genuineSig;
        BOOST_REQUIRE(signingKey.Sign(messageHash, genuineSig));
        BOOST_REQUIRE(!genuineSig.empty() && genuineSig.size() <= 72);

        // Sanity (precondition, not the bug assertion): the genuine signature
        // verifies against its own pubkey, and the forged pairing does not.
        BOOST_REQUIRE_MESSAGE(signingPub.Verify(messageHash, genuineSig),
            "P8: genuine signature must verify against its own pubkey (#" +
            std::to_string(i) + ")");
        BOOST_REQUIRE_MESSAGE(!wrongPub.Verify(messageHash, genuineSig),
            "P8: a genuine signature must NOT verify against the wrong pubkey (#" +
            std::to_string(i) + ")");

        // Present the GENUINE signature but against the WRONG public key — i.e.
        // the (msg, sig, pubkey) triple supplied to the opcode does not
        // genuinely verify. The full signature and (wrong) pubkey bytes are
        // encoded so the handler performs real secp256k1 verification.
        std::vector<uint8_t> sigBytes = genuineSig;
        std::vector<uint8_t> wrongPubBytes(wrongPub.begin(), wrongPub.end());

        // Alternate between the auto-detect and the explicit-ECDSA opcode; both
        // hardcoded success on the unfixed ECDSA path.
        CVM::OpCode verifyOp = (i % 2 == 0) ? CVM::OpCode::OP_VERIFY_SIG
                                            : CVM::OpCode::OP_VERIFY_SIG_ECDSA;

        std::vector<uint8_t> code =
            MakeVerifySigProgram(messageHash, sigBytes, wrongPubBytes, verifyOp);

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok,
            "P8: verify-sig bytecode did not complete (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 result = state.Peek(0);

        // EXPECTED (post-fix): a forged / wrong-key signature does not verify, so
        // the opcode pushes 0. UNFIXED: verifyResult is hardcoded true -> 1.
        BOOST_CHECK_MESSAGE(result == arith_uint256(),
            "P8 (1.23): " + std::string(CVM::GetOpCodeName(verifyOp)) +
            " pushed " + result.GetHex() + " (1) for a signature that does NOT "
            "verify against the supplied public key; the handler hardcodes "
            "verifyResult=true and performs no real secp256k1 verification "
            "(sample #" + std::to_string(i) + ").");
    }
}

// Additional forged variant: random garbage of a valid ECDSA length. This makes
// the counterexample explicit — no key could ever have produced these bytes over
// the message, yet the unfixed opcode still reports "valid".
BOOST_AUTO_TEST_CASE(p8_verify_sig_rejects_garbage_property)
{
    for (int i = 0; i < kSamples; ++i) {
        uint256 messageHash = InsecureRand256();

        // Garbage "signature" of a plausible ECDSA length (64..72 bytes) so the
        // length-based detection takes the ECDSA/auto path, plus a garbage
        // 33-byte "public key". No key could have produced these bytes over the
        // message, so real secp256k1 verification must fail.
        size_t sigLen = 64 + InsecureRandRange(9); // [64,72]
        std::vector<uint8_t> garbageSig(sigLen);
        for (auto& b : garbageSig) b = static_cast<uint8_t>(InsecureRandRange(256));
        std::vector<uint8_t> garbagePub(33);
        for (auto& b : garbagePub) b = static_cast<uint8_t>(InsecureRandRange(256));

        CVM::OpCode verifyOp = (i % 2 == 0) ? CVM::OpCode::OP_VERIFY_SIG
                                            : CVM::OpCode::OP_VERIFY_SIG_ECDSA;

        std::vector<uint8_t> code =
            MakeVerifySigProgram(messageHash, garbageSig, garbagePub, verifyOp);

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok,
            "P8: verify-sig bytecode did not complete (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 result = state.Peek(0);

        // EXPECTED (post-fix): garbage does not verify -> push 0.
        // UNFIXED: hardcoded true -> push 1.
        BOOST_CHECK_MESSAGE(result == arith_uint256(),
            "P8 (1.23): " + std::string(CVM::GetOpCodeName(verifyOp)) +
            " pushed " + result.GetHex() + " (1) for a garbage signature that "
            "cannot verify; no real verification is performed (sample #" +
            std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 9a — OP_BALANCE pushes the account's actual balance.         (1.24)
//
// Expected (2.24): OP_BALANCE SHALL push the account's actual balance.
//
// The executing contract is given a concrete address (a "funded" account in the
// post-fix world reports a non-zero balance). The fixed handler must query the
// account balance (UTXO/state) rather than returning a hardcoded 0.
//
// UNFIXED: HandleContext hardcodes `value = 0` for OP_BALANCE, so a funded
// account still reports 0 -> the property FAILS (counterexample) confirming 1.24.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p9a_balance_reports_actual_balance_property)
{
    // A set of representative "funded" contract accounts. (Balance funding is
    // wired by the Workstream-2 fix, task 8.2; the exploratory test surfaces the
    // hardcoded-zero defect regardless.)
    for (int i = 0; i < 32; ++i) {
        uint160 addr;
        uint256 r = InsecureRand256();
        std::memcpy(addr.begin(), r.begin(), 20);
        if (addr.IsNull()) *addr.begin() = 0x01;

        std::vector<uint8_t> code;
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_BALANCE));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        state.SetContractAddress(addr);
        // Fund the account through the VM's balance API (wired by the
        // Workstream-2 fix, task 8.2). A funded account must report a non-zero
        // balance from OP_BALANCE. The funding is part of the test SETUP; the
        // assertion below (balance != 0) is unchanged.
        uint64_t funded = 1 + static_cast<uint64_t>(InsecureRandRange(0xFFFFFFFFULL));
        state.SetBalance(addr, funded);

        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok,
            "P9a: OP_BALANCE bytecode did not complete (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 balance = state.Peek(0);

        // EXPECTED (post-fix): a funded account reports a non-zero balance.
        // UNFIXED: OP_BALANCE always pushes 0.
        BOOST_CHECK_MESSAGE(balance != arith_uint256(),
            "P9a (1.24): OP_BALANCE pushed 0 for account " + addr.ToString() +
            "; the balance is hardcoded to 0 and never queried from account "
            "state (sample #" + std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 9b — OP_CALL / CallContract are functional.                  (1.25)
//
// Expected (2.25): OP_CALL / CallContract SHALL load and execute the target
// contract with proper gas/state handling, or fail deterministically with a
// DEFINED error when execution is not possible.
//
// The defining defect (1.25) is the placeholder error string "CALL not fully
// implemented". Whatever the fixed behaviour (successful call or a defined
// deterministic failure), the placeholder error MUST no longer appear.
//
// UNFIXED: HandleCall sets error "CALL not fully implemented" and returns false
// -> the property FAILS (counterexample) confirming 1.25.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p9b_call_not_placeholder_property)
{
    for (int i = 0; i < 32; ++i) {
        // Provide several plausible CALL arguments on the stack so the fixed
        // handler (which will consume call parameters) does not underflow.
        std::vector<uint8_t> code;
        EmitPush1(code, static_cast<uint8_t>(InsecureRandRange(256))); // value
        EmitPush1(code, static_cast<uint8_t>(InsecureRandRange(256))); // target addr word
        EmitPush1(code, static_cast<uint8_t>(1 + InsecureRandRange(255))); // gas
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_CALL));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        uint160 self;
        uint256 r = InsecureRand256();
        std::memcpy(self.begin(), r.begin(), 20);
        state.SetContractAddress(self);

        vm.Execute(code, state, /*storage=*/nullptr);
        std::string err = state.GetError();

        // EXPECTED (post-fix): CALL is implemented — either it executes, or it
        // fails with a defined error. The placeholder must be gone.
        // UNFIXED: err == "CALL not fully implemented".
        BOOST_CHECK_MESSAGE(err.find("not fully implemented") == std::string::npos,
            "P9b (1.25): OP_CALL left the placeholder error \"" + err +
            "\"; contract-to-contract calls are non-functional (sample #" +
            std::to_string(i) + ").");
    }
}

// ===========================================================================
// Property 9c — OP_LOG consumes its operands and emits a log entry.     (1.26)
//
// Expected (2.26): OP_LOG SHALL consume the topic count, topics, and data and
// emit a corresponding log entry.
//
// We push a data word, one topic word, and a topic count (1), then run OP_LOG.
// The fixed handler must record a log entry retrievable via state.GetLogs().
//
// UNFIXED: the OP_LOG dispatch is `return true` (a no-op) — it consumes nothing
// and emits nothing, so state.GetLogs() stays empty -> the property FAILS
// (counterexample) confirming 1.26.
// ===========================================================================
BOOST_AUTO_TEST_CASE(p9c_log_emits_entry_property)
{
    for (int i = 0; i < 32; ++i) {
        uint256 topic = InsecureRand256();
        uint256 data = InsecureRand256();

        std::vector<uint8_t> code;
        // data (bottom), then topic, then topic count (top) = 1.
        EmitPush(code, std::vector<uint8_t>(data.begin(), data.end()));
        EmitPush(code, std::vector<uint8_t>(topic.begin(), topic.end()));
        EmitPush1(code, 0x01); // topic count
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_LOG));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        uint160 self;
        uint256 r = InsecureRand256();
        std::memcpy(self.begin(), r.begin(), 20);
        state.SetContractAddress(self);

        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok,
            "P9c: OP_LOG bytecode did not complete (#" + std::to_string(i) + ")");

        // EXPECTED (post-fix): OP_LOG emits a log entry.
        // UNFIXED: OP_LOG is a no-op -> no logs recorded.
        BOOST_CHECK_MESSAGE(!state.GetLogs().empty(),
            "P9c (1.26): OP_LOG emitted no log entry; it consumes neither the "
            "topic count, topics, nor data and records nothing (sample #" +
            std::to_string(i) + ").");
    }
}

BOOST_AUTO_TEST_SUITE_END()
