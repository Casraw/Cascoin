// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 2 Preservation Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 7: "Write Workstream-2 preservation tests (BEFORE fix)"
 *
 * Workstream 2 = Core-VM opcode handlers
 * (bugfix.md clauses 1.23, 1.24, 1.25, 1.26).
 *
 * PURPOSE
 * -------
 * Property 21 (Preservation): ¬isBugCondition ⇒ F'(x) = F(x). Following the
 * *observation-first* methodology from design.md (Testing Strategy →
 * Preservation Checking), these tests capture the legacy behaviour of the
 * NON-flagged core-VM paths on the CURRENT (unfixed) code as golden vectors.
 * They are EXPECTED TO PASS on the unfixed code (baseline behaviour to preserve)
 * and MUST still pass unchanged after the Workstream-2 fixes land (task 8.4).
 *
 * A test that FAILS on the unfixed code would mean the captured case actually
 * reaches a flagged path and must be re-classified.
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (3.x):
 *   3.7  Persistent contract storage SLOAD/SSTORE round-trip semantics: a value
 *        stored under a key is loaded back identically. The storage opcode
 *        handlers are NOT flagged for Workstream 2 and must remain byte-for-byte
 *        identical after the OP_BALANCE/OP_CALL/OP_LOG fixes.
 *   3.9  Already-supported opcodes/context execute unchanged: arithmetic
 *        (ADD/SUB/MUL), comparison (EQ/NE/LT/GT/LE/GE), and the context opcodes
 *        that are already correct (CALLVALUE / TIMESTAMP / BLOCKHEIGHT).
 *   3.11 Genuinely valid signatures still push 1 from the OP_VERIFY_SIG family.
 *        (See the detailed encoding-dependency note on the 3.11 tests below.)
 *
 * Property-based (Property 21): random non-flagged inputs (random keys/values,
 * random operands, random context values, random genuine key material) produce
 * identical results before and after the fix.
 *
 * Requirements: 3.7, 3.9, 3.11
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
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// Cheap opcode-level properties can afford many samples.
static constexpr int kSamples = 256;

// ---------------------------------------------------------------------------
// In-memory ContractStorage backend.
//
// This is NOT a mock of production behaviour; it is a concrete, faithful
// implementation of the CVM::ContractStorage interface (defined in
// cvm/vmstate.h) that lets us exercise the real SLOAD/SSTORE opcode handlers in
// cvm.cpp without pulling in LevelDB. The VM's storage semantics (round-trip
// fidelity) are what we are preserving (3.7); the backing store just needs to
// honour the interface contract: a value stored under (addr, key) is returned
// unchanged by a subsequent load.
// ---------------------------------------------------------------------------
class MemoryStorage : public CVM::ContractStorage {
public:
    bool Load(const uint160& contractAddr, const uint256& key, uint256& value) override
    {
        auto it = data.find(Compose(contractAddr, key));
        if (it == data.end()) return false;
        value = it->second;
        return true;
    }

    bool Store(const uint160& contractAddr, const uint256& key, const uint256& value) override
    {
        data[Compose(contractAddr, key)] = value;
        touched.insert(contractAddr);
        return true;
    }

    bool Exists(const uint160& contractAddr) override
    {
        return touched.count(contractAddr) > 0;
    }

private:
    static std::string Compose(const uint160& addr, const uint256& key)
    {
        return addr.ToString() + ":" + key.ToString();
    }
    std::map<std::string, uint256> data;
    std::set<uint160> touched;
};

// Append a PUSH<size> immediate carrying the given bytes (size must be 1..32).
// The VM reads the bytes big-endian into an arith_uint256 stack word.
void EmitPush(std::vector<uint8_t>& code, const std::vector<uint8_t>& bytes)
{
    BOOST_REQUIRE(!bytes.empty() && bytes.size() <= 32);
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
    code.push_back(static_cast<uint8_t>(bytes.size()));
    code.insert(code.end(), bytes.begin(), bytes.end());
}

// Push a single-byte value.
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
// as OP_PUSH folds them. The length word is on top so the handler pops it first.
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

// Mirror of CVM::ReadImmediate's big-endian interpretation: the immediate bytes
// are folded most-significant-first into an arith_uint256. Used to predict the
// stack word a PUSH of the given bytes produces.
arith_uint256 BytesToArithBE(const std::vector<uint8_t>& bytes)
{
    arith_uint256 v;
    for (uint8_t b : bytes) {
        v = (v << 8) | arith_uint256(b);
    }
    return v;
}

// Run a program to completion and return success; on success the caller reads
// the result via state.Peek(0).
bool RunProgram(const std::vector<uint8_t>& code, CVM::VMState& state,
                CVM::ContractStorage* storage)
{
    CVM::CVM vm;
    state.SetGasLimit(1000000);
    return vm.Execute(code, state, storage);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream2_preserve_tests, BasicTestingSetup)

// ===========================================================================
// 3.7 — SLOAD / SSTORE persistent-storage round-trip semantics unchanged.
//
// SSTORE pops (key, value) from the stack and writes storage[addr][key]=value;
// SLOAD pops (key) and pushes storage[addr][key]. Storing a value under a key
// then loading it back MUST yield the stored value. The storage handlers are
// out of scope for Workstream 2 and must stay identical after the fix.
// ===========================================================================

// Golden vector: store a known value, load it back, expect equality.
BOOST_AUTO_TEST_CASE(p21_sload_sstore_roundtrip_golden)
{
    MemoryStorage storage;

    uint160 self;
    *self.begin() = 0xAB; // arbitrary non-null contract address

    const uint8_t keyByte = 0x2A;
    const std::vector<uint8_t> valueBytes = {0x12, 0x34, 0x56, 0x78};
    const arith_uint256 expected = BytesToArithBE(valueBytes);

    std::vector<uint8_t> code;
    // SSTORE expects (top=key, below=value): push value first, then key.
    EmitPush(code, valueBytes);
    EmitPush1(code, keyByte);
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SSTORE));
    // SLOAD expects (top=key): push key, load pushes the stored value.
    EmitPush1(code, keyByte);
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SLOAD));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::VMState state;
    state.SetContractAddress(self);
    BOOST_REQUIRE(RunProgram(code, state, &storage));
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

    BOOST_CHECK_MESSAGE(state.Peek(0) == expected,
        "3.7: SLOAD after SSTORE returned " + state.Peek(0).GetHex() +
        " but the stored value was " + expected.GetHex());
}

// Property: for random (key, value) 32-byte words, SLOAD after SSTORE returns
// exactly the stored value. **Validates: Requirements 3.7**
BOOST_AUTO_TEST_CASE(p21_sload_sstore_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        MemoryStorage storage;

        uint160 self;
        uint256 r = InsecureRand256();
        std::memcpy(self.begin(), r.begin(), 20);
        if (self.IsNull()) *self.begin() = 0x01;

        uint256 key = InsecureRand256();
        uint256 value = InsecureRand256();
        const std::vector<uint8_t> valueBytes(value.begin(), value.end());
        const arith_uint256 expected = BytesToArithBE(valueBytes);

        std::vector<uint8_t> code;
        EmitPush32(code, value);           // value (below)
        EmitPush32(code, key);             // key (top)
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SSTORE));
        EmitPush32(code, key);             // key (top)
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SLOAD));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        CVM::VMState state;
        state.SetContractAddress(self);
        BOOST_REQUIRE_MESSAGE(RunProgram(code, state, &storage),
            "3.7: SLOAD/SSTORE program did not complete (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        BOOST_CHECK_MESSAGE(state.Peek(0) == expected,
            "3.7: round-trip mismatch — loaded " + state.Peek(0).GetHex() +
            " expected " + expected.GetHex() + " (sample #" + std::to_string(i) + ")");
    }
}

// Loading an unset key yields 0 (unchanged default behaviour).
BOOST_AUTO_TEST_CASE(p21_sload_missing_key_is_zero)
{
    MemoryStorage storage;
    uint160 self;
    *self.begin() = 0xCD;

    std::vector<uint8_t> code;
    EmitPush1(code, 0x07); // an unset key
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_SLOAD));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::VMState state;
    state.SetContractAddress(self);
    BOOST_REQUIRE(RunProgram(code, state, &storage));
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
    BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256(),
        "3.7: SLOAD of an unset key must return 0, got " + state.Peek(0).GetHex());
}

// ===========================================================================
// 3.9 — Already-supported opcodes/context execute unchanged.
//
// Arithmetic (ADD/SUB/MUL), comparison, and the correct context opcodes
// (CALLVALUE/TIMESTAMP/BLOCKHEIGHT) are out of scope for Workstream 2 and must
// stay identical after the OP_BALANCE/OP_CALL/OP_LOG fixes.
// ===========================================================================

// Emit a PUSH of the minimal big-endian encoding of a 64-bit value. CVM's
// ReadImmediate folds the immediate bytes big-endian, so the reconstructed stack
// word equals `v` exactly.
void EmitPushBE(std::vector<uint8_t>& code, uint64_t v)
{
    std::vector<uint8_t> bytes;
    if (v == 0) {
        bytes.push_back(0x00);
    } else {
        while (v != 0) {
            bytes.insert(bytes.begin(), static_cast<uint8_t>(v & 0xFF));
            v >>= 8;
        }
    }
    EmitPush(code, bytes);
}

// Helper: run a binary op over (a, b). Pop order in the handler is b (top) then
// a, so we push a first, then b.
arith_uint256 RunBinaryOp(CVM::OpCode op, uint64_t a, uint64_t b)
{
    std::vector<uint8_t> code;
    EmitPushBE(code, a);
    EmitPushBE(code, b);
    code.push_back(static_cast<uint8_t>(op));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::VMState state;
    BOOST_REQUIRE(RunProgram(code, state, nullptr));
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
    return state.Peek(0);
}

// Golden vectors for arithmetic and comparison.
BOOST_AUTO_TEST_CASE(p21_arithmetic_comparison_golden)
{
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_ADD, 7, 5) == arith_uint256(12));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_SUB, 20, 8) == arith_uint256(12));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_MUL, 6, 7) == arith_uint256(42));

    // Comparison opcodes push 1 (true) / 0 (false).
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_EQ, 4, 4) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_NE, 4, 5) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_LT, 3, 9) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_GT, 3, 9) == arith_uint256());
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_LE, 9, 9) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_GE, 8, 9) == arith_uint256());
}

// Property: ADD/SUB/MUL and comparison match the arith_uint256 reference.
// Operands are kept small enough to avoid ambiguity while still random.
// **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(p21_arithmetic_comparison_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // 32-bit operands: ADD/MUL cannot overflow a 256-bit word, and SUB is
        // ordered so a >= b (no wraparound ambiguity).
        uint64_t x = InsecureRandRange(0x100000000ULL);
        uint64_t y = InsecureRandRange(0x100000000ULL);
        uint64_t a = std::max(x, y);
        uint64_t b = std::min(x, y);
        arith_uint256 A(a), B(b);

        BOOST_CHECK_MESSAGE(RunBinaryOp(CVM::OpCode::OP_ADD, a, b) == A + B,
            "3.9: ADD mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(RunBinaryOp(CVM::OpCode::OP_SUB, a, b) == A - B,
            "3.9: SUB mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(RunBinaryOp(CVM::OpCode::OP_MUL, a, b) == A * B,
            "3.9: MUL mismatch (#" + std::to_string(i) + ")");

        BOOST_CHECK_MESSAGE(
            RunBinaryOp(CVM::OpCode::OP_EQ, a, b) == (A == B ? arith_uint256(1) : arith_uint256()),
            "3.9: EQ mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(
            RunBinaryOp(CVM::OpCode::OP_LT, a, b) == (A < B ? arith_uint256(1) : arith_uint256()),
            "3.9: LT mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(
            RunBinaryOp(CVM::OpCode::OP_GE, a, b) == (A >= B ? arith_uint256(1) : arith_uint256()),
            "3.9: GE mismatch (#" + std::to_string(i) + ")");
    }
}

// Context opcodes that are already correct must expose the configured value.
// **Validates: Requirements 3.9**
BOOST_AUTO_TEST_CASE(p21_context_opcodes_property)
{
    for (int i = 0; i < 64; ++i) {
        uint64_t callValue = InsecureRandRange(0x100000000ULL);
        int64_t timestamp = static_cast<int64_t>(InsecureRandRange(0x7FFFFFFFULL));
        int blockHeight = static_cast<int>(InsecureRandRange(0x7FFFFFFFULL));

        // OP_CALLVALUE
        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_CALLVALUE),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::VMState state;
            state.SetCallValue(callValue);
            BOOST_REQUIRE(RunProgram(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256(callValue),
                "3.9: CALLVALUE mismatch (#" + std::to_string(i) + ")");
        }
        // OP_TIMESTAMP
        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_TIMESTAMP),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::VMState state;
            state.SetTimestamp(timestamp);
            BOOST_REQUIRE(RunProgram(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256((uint64_t)timestamp),
                "3.9: TIMESTAMP mismatch (#" + std::to_string(i) + ")");
        }
        // OP_BLOCKHEIGHT
        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_BLOCKHEIGHT),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::VMState state;
            state.SetBlockHeight(blockHeight);
            BOOST_REQUIRE(RunProgram(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256((uint64_t)blockHeight),
                "3.9: BLOCKHEIGHT mismatch (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// 3.11 — Genuinely valid signatures still push 1 from the OP_VERIFY_SIG family.
//
// ENCODING-DEPENDENCY NOTE (IMPORTANT — must be honoured by task 8.1)
// -------------------------------------------------------------------
// The core VM stack carries 256-bit arith_uint256 words, so a full 64–72 byte
// ECDSA signature and 33/65 byte public key cannot fit into a single stack slot.
// The UNFIXED OP_VERIFY_SIG family exploits this: it treats the "signature" slot
// as a mere *size indicator* and hardcodes verifyResult=true, so ANY input —
// including a genuinely valid signature — pushes 1.
//
// This preservation test captures only the ACCEPTANCE DIRECTION required by
// clause 3.11: "a scenario representing a genuinely valid signature pushes 1".
// It deliberately does NOT pin the assertion to the unfixed size-indicator hack;
// the *ground truth* that the scenario is genuinely valid is established with the
// project's real secp256k1 CKey/CPubKey (CPubKey::Verify == true), independent of
// the VM encoding.
//
// The valid-signature scenario is centralised in the single helper
// MakeValidSignatureScenario() below. The Workstream-2 fix (task 8.1) MUST
// preserve a way to present a genuinely valid signature to OP_VERIFY_SIG such
// that it still pushes 1. Because a full signature does not fit one 256-bit word,
// the fix will likely supply the signature/pubkey via a memory/data region or a
// multi-word encoding. WHEN that encoding is defined, update ONLY the body of
// MakeValidSignatureScenario() to emit the genuine bytes in that encoding — the
// assertion (a valid signature => push 1) stays the same and this test keeps
// passing on both unfixed and fixed code. The complementary rejection direction
// (forged/garbage signatures push 0) is asserted by the Workstream-2 exploratory
// suite (task 6, cvm_workstream2_fix_property_tests.cpp).
// ===========================================================================

namespace {

// Result of building a genuine-valid-signature scenario: the bytecode to run and
// the ground-truth (from real secp256k1) that the signature genuinely verifies.
struct ValidSigScenario {
    std::vector<uint8_t> code;
    bool genuinelyValid;      // established via CPubKey::Verify — encoding-independent
    CVM::OpCode verifyOp;
};

// Build a program that presents a GENUINELY VALID signature scenario to the given
// verify opcode. See the ENCODING-DEPENDENCY NOTE above: the encoding here is the
// current single-slot size-indicator form; task 8.1 must keep a valid-signature
// scenario yielding 1 (updating this helper's encoding if/when it changes).
ValidSigScenario MakeValidSignatureScenario(CVM::OpCode verifyOp)
{
    ValidSigScenario s;
    s.verifyOp = verifyOp;

    CKey signingKey;
    signingKey.MakeNewKey(/*fCompressed=*/true);
    CPubKey signingPub = signingKey.GetPubKey();
    BOOST_REQUIRE(signingPub.IsValid());

    uint256 messageHash = InsecureRand256();

    std::vector<uint8_t> genuineSig;
    BOOST_REQUIRE(signingKey.Sign(messageHash, genuineSig));
    BOOST_REQUIRE(!genuineSig.empty() && genuineSig.size() <= 72);

    // Ground truth (encoding-independent): the signature genuinely verifies
    // against its own public key.
    s.genuinelyValid = signingPub.Verify(messageHash, genuineSig);

    // New multi-word encoding (defined by task 8.1, decoded by PopVarBytes in
    // cvm.cpp): push the 32-byte message word, then the genuine signature as a
    // length-prefixed field, then the genuine public key as a length-prefixed
    // field. The handler pops pubkey, signature, message in that order, so the
    // pubkey field is pushed last (ends up on top). The full genuine signature
    // and public-key bytes are supplied so real secp256k1 verification succeeds
    // and the opcode pushes 1.
    std::vector<uint8_t> pubBytes(signingPub.begin(), signingPub.end());
    std::vector<uint8_t>& code = s.code;
    EmitPush32(code, messageHash);
    EmitPushVarBytes(code, genuineSig);
    EmitPushVarBytes(code, pubBytes);
    code.push_back(static_cast<uint8_t>(verifyOp));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));
    return s;
}

} // anonymous namespace

// Golden: a genuinely valid ECDSA signature scenario pushes 1.
BOOST_AUTO_TEST_CASE(p21_valid_signature_pushes_one_golden)
{
    for (CVM::OpCode op : {CVM::OpCode::OP_VERIFY_SIG, CVM::OpCode::OP_VERIFY_SIG_ECDSA}) {
        ValidSigScenario s = MakeValidSignatureScenario(op);
        // Precondition: the scenario really is a valid signature (real secp256k1).
        BOOST_REQUIRE_MESSAGE(s.genuinelyValid,
            "3.11 precondition: scenario signature must genuinely verify for " +
            std::string(CVM::GetOpCodeName(op)));

        CVM::VMState state;
        BOOST_REQUIRE(RunProgram(s.code, state, nullptr));
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256(1),
            "3.11: " + std::string(CVM::GetOpCodeName(op)) +
            " must push 1 for a genuinely valid signature, got " +
            state.Peek(0).GetHex());
    }
}

// Property: across many random genuine key/signature pairs, a valid signature
// scenario pushes 1. **Validates: Requirements 3.11**
BOOST_AUTO_TEST_CASE(p21_valid_signature_pushes_one_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVM::OpCode op = (i % 2 == 0) ? CVM::OpCode::OP_VERIFY_SIG
                                      : CVM::OpCode::OP_VERIFY_SIG_ECDSA;
        ValidSigScenario s = MakeValidSignatureScenario(op);
        BOOST_REQUIRE_MESSAGE(s.genuinelyValid,
            "3.11 precondition: scenario signature must genuinely verify (#" +
            std::to_string(i) + ")");

        CVM::VMState state;
        BOOST_REQUIRE_MESSAGE(RunProgram(s.code, state, nullptr),
            "3.11: verify-sig program did not complete (#" + std::to_string(i) + ")");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256(1),
            "3.11: " + std::string(CVM::GetOpCodeName(op)) +
            " must push 1 for a genuinely valid signature (sample #" +
            std::to_string(i) + "), got " + state.Peek(0).GetHex());
    }
}

BOOST_AUTO_TEST_SUITE_END()
