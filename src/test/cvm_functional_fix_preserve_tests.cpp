// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Global Preservation Baseline Test Suite
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 2: "Write global preservation baseline suite (BEFORE implementing fixes)"
 * Property 21: Preservation — Standard/WoT/valid-signature behaviour unchanged.
 *
 * PURPOSE
 * -------
 * These tests follow the *observation-first* methodology from design.md
 * (Testing Strategy → Preservation Checking). They capture the legacy behaviour
 * of NON-flagged CVM code paths on the CURRENT (unfixed) code as golden vectors.
 * They are EXPECTED TO PASS on the unfixed code; after the fixes land, the SAME
 * assertions must still hold — the fixed code must reproduce these vectors
 * exactly (Property 21: ¬isBugCondition ⇒ F'(x) = F(x)).
 *
 * A test that FAILS on the unfixed code means the captured case is NOT actually
 * a preservation baseline (it reaches a flagged path) and must be re-classified.
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (3.x):
 *   3.10 Standard (non-CVM/EVM) transaction: recognised as non-contract and
 *        passes context-independent validation (mempool acceptance precheck).
 *   3.3  WoT transactions (reputation vote) remain non-contract — 100% fee to
 *        the miner (no gas subsidy diversion).
 *   3.4  Existing secp256k1 ECDSA sign/verify roundtrip continues to work.
 *   3.11 Genuinely valid signatures still accepted by the OP_VERIFY_SIG family
 *        (push 1).
 *   3.14 Canonical deployer+nonce contract-address scheme (contract.cpp) is
 *        deterministic and unchanged.
 *   3.13 Receipt JSON keeps all existing fields (logsBloom excluded — it is a
 *        flagged path that will change).
 *   3.22 Zero-value deploy/call exposes CALLVALUE = 0.
 *
 * Property-based generators (Property 21): random standard/WoT transactions and
 * random (deployer, nonce) pairs assert the preserved invariants across the
 * whole input space, not just single examples.
 *
 * Requirements: 3.2, 3.3, 3.4, 3.10, 3.11, 3.13, 3.14, 3.22
 */

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <cvm/receipt.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <key.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <hash.h>
#include <uint256.h>
#include <arith_uint256.h>

#include <univalue.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Number of random samples for the property-based cases.
static constexpr int kSamples = 256;

// Build a raw OP_RETURN script exactly as the CVM/reputation parsers expect:
// [OP_RETURN][raw marker+version+type+payload...] (raw bytes, no pushdata).
CScript MakeRawOpReturn(const std::vector<uint8_t>& raw)
{
    CScript script;
    script << OP_RETURN;
    script.insert(script.end(), raw.begin(), raw.end());
    return script;
}

// A standard P2PKH transaction (no CVM/WoT marker at all).
CTransactionRef MakeStandardP2PKHTx(const CKeyID& payee, CAmount value)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0); // non-null (not coinbase)
    mtx.vin[0].scriptSig = CScript() << OP_1;             // dummy scriptSig
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(value, GetScriptForDestination(payee));
    return MakeTransactionRef(std::move(mtx));
}

// A Web-of-Trust reputation-vote transaction ("REP" + 0x01 + serialized vote).
CTransactionRef MakeReputationVoteTx(const uint160& target, int64_t voteValue,
                                     const std::string& reason)
{
    CVM::ReputationVoteTx v;
    v.targetAddress = target;
    v.voteValue = voteValue;
    v.reason = reason;
    std::vector<uint8_t> payload = v.Serialize();

    std::vector<uint8_t> raw = {'R', 'E', 'P', 0x01};
    raw.insert(raw.end(), payload.begin(), payload.end());

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout = COutPoint(InsecureRand256(), 0);
    mtx.vout.resize(1);
    mtx.vout[0] = CTxOut(0, MakeRawOpReturn(raw));
    return MakeTransactionRef(std::move(mtx));
}

// Push a value onto the CVM stack via a raw arith_uint256 (helper mirrors the
// stack-based OP_VERIFY_SIG interface, which reads message / size-indicator /
// pubkey as stack words).
void PushWord(std::vector<uint8_t>& code, const std::vector<uint8_t>& bytes)
{
    // OP_PUSH <size> <bytes...>
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
    code.push_back(static_cast<uint8_t>(bytes.size()));
    code.insert(code.end(), bytes.begin(), bytes.end());
}

// Emit a length-prefixed variable-length byte field for the OP_VERIFY_SIG
// family, matching the encoding decoded by PopVarBytes in cvm.cpp (and mirroring
// EmitPushVarBytes in cvm_workstream2_preserve_tests.cpp):
//   push word_0, word_1, ... word_{W-1}, then the byte length (W=ceil(L/32)).
// Each word_c carries field bytes [c*32, min((c+1)*32, L)) big-endian, exactly
// as OP_PUSH folds them. The length word is pushed last so the handler pops it
// first, then reconstructs the field from the words below.
void PushVarBytes(std::vector<uint8_t>& code, const std::vector<uint8_t>& field)
{
    const size_t L = field.size();
    const size_t W = (L + 31) / 32;
    for (size_t c = 0; c < W; ++c) {
        const size_t start = c * 32;
        const size_t chunkLen = std::min<size_t>(32, L - start);
        std::vector<uint8_t> chunk(field.begin() + start, field.begin() + start + chunkLen);
        PushWord(code, chunk);
    }
    PushWord(code, std::vector<uint8_t>{static_cast<uint8_t>(L)});
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_functional_fix_preserve_tests, BasicTestingSetup)

// ===========================================================================
// 3.10  Standard (non-CVM/EVM) transactions are recognised as non-contract and
//       pass context-independent validation (the precheck used before mempool
//       acceptance). This behaviour must be identical after the fixes.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_10_standard_tx_is_non_contract_and_valid)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID payee = key.GetPubKey().GetID();

    CTransactionRef tx = MakeStandardP2PKHTx(payee, 50 * COIN);

    // A standard transaction reaches none of the CVM/WoT flagged paths.
    BOOST_CHECK_MESSAGE(!CVM::IsContractTransaction(*tx),
        "Preservation 3.10: a standard P2PKH tx must NOT be a contract tx.");
    BOOST_CHECK(CVM::GetContractTxType(*tx) == CVM::ContractTxType::NONE);
    BOOST_CHECK_MESSAGE(!CVM::IsReputationVoteTransaction(*tx),
        "Preservation 3.10: a standard P2PKH tx must NOT be a WoT vote.");

    // Context-independent validity (same check the mempool runs first).
    CValidationState state;
    bool ok = CheckTransaction(*tx, state);
    BOOST_CHECK_MESSAGE(ok && state.IsValid(),
        "Preservation 3.10: standard tx must pass CheckTransaction; reason="
        + state.GetRejectReason());
}

// Property 21 (3.10): random standard transactions are all non-contract and
// valid — no standard-tx shape should ever be mistaken for a CVM/WoT tx.
BOOST_AUTO_TEST_CASE(preserve_3_10_standard_tx_property)
{
    int nonContract = 0, valid = 0;
    for (int i = 0; i < kSamples; ++i) {
        CKey key;
        key.MakeNewKey(InsecureRandBool()); // random compressed/uncompressed
        CKeyID payee = key.GetPubKey().GetID();
        CAmount value = static_cast<CAmount>(InsecureRandRange(21000000ULL * COIN));

        CTransactionRef tx = MakeStandardP2PKHTx(payee, value);

        bool isContract = CVM::IsContractTransaction(*tx);
        bool isVote = CVM::IsReputationVoteTransaction(*tx);
        CValidationState state;
        bool ok = CheckTransaction(*tx, state);

        if (!isContract && !isVote) ++nonContract;
        if (ok) ++valid;

        BOOST_REQUIRE_MESSAGE(!isContract && !isVote,
            "Preservation 3.10 (property): random standard tx #" +
            std::to_string(i) + " was misclassified as CVM/WoT.");
        BOOST_REQUIRE_MESSAGE(ok,
            "Preservation 3.10 (property): random standard tx #" +
            std::to_string(i) + " failed CheckTransaction: " +
            state.GetRejectReason());
    }
    BOOST_CHECK_EQUAL(nonContract, kSamples);
    BOOST_CHECK_EQUAL(valid, kSamples);
}

// ===========================================================================
// 3.3  Web-of-Trust transactions (reputation votes) remain non-contract, so
//      100% of the fee goes to the miner (no gas subsidy is applied). This must
//      be unchanged after the fixes.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_3_wot_tx_is_non_contract)
{
    uint160 target;
    target.SetHex("00112233445566778899aabbccddeeff00112233");

    CTransactionRef tx = MakeReputationVoteTx(target, 50, "preserve-3.3");

    // It IS a WoT vote ...
    BOOST_CHECK_MESSAGE(CVM::IsReputationVoteTransaction(*tx),
        "Preservation 3.3: reputation vote must be recognised as a WoT vote.");
    // ... but it is NOT a CVM contract transaction (=> 100% fee to miner).
    BOOST_CHECK_MESSAGE(!CVM::IsContractTransaction(*tx),
        "Preservation 3.3: a WoT vote must remain non-contract so the full fee "
        "goes to the miner (no gas subsidy).");
    BOOST_CHECK(CVM::GetContractTxType(*tx) == CVM::ContractTxType::NONE);

    // Round-trips through the WoT parser (semantics preserved).
    CVM::ReputationVoteTx parsed;
    BOOST_REQUIRE(CVM::ParseReputationVoteTx(*tx, parsed));
    BOOST_CHECK(parsed.targetAddress == target);
    BOOST_CHECK_EQUAL(parsed.voteValue, 50);
}

// Property 21 (3.3): random WoT votes are always non-contract and round-trip.
BOOST_AUTO_TEST_CASE(preserve_3_3_wot_tx_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // Build a random 20-byte target address directly.
        uint160 target;
        uint256 r = InsecureRand256();
        std::memcpy(target.begin(), r.begin(), 20);

        int64_t voteValue = static_cast<int64_t>(InsecureRandRange(201)) - 100; // [-100,100]

        CTransactionRef tx = MakeReputationVoteTx(target, voteValue, "p" + std::to_string(i));

        BOOST_REQUIRE_MESSAGE(CVM::IsReputationVoteTransaction(*tx),
            "Preservation 3.3 (property): random WoT vote #" + std::to_string(i) +
            " not recognised as a vote.");
        BOOST_REQUIRE_MESSAGE(!CVM::IsContractTransaction(*tx),
            "Preservation 3.3 (property): random WoT vote #" + std::to_string(i) +
            " misclassified as a contract tx (would divert fee).");

        CVM::ReputationVoteTx parsed;
        BOOST_REQUIRE(CVM::ParseReputationVoteTx(*tx, parsed));
        BOOST_REQUIRE(parsed.targetAddress == target);
        BOOST_REQUIRE_EQUAL(parsed.voteValue, voteValue);
    }
}

// ===========================================================================
// 3.4  The existing secp256k1 ECDSA sign/verify path continues to produce and
//      accept valid signatures. This is the primitive the fixed OP_VERIFY_SIG
//      family will reuse, so its behaviour must be preserved.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_4_secp256k1_sign_verify_roundtrip)
{
    for (int i = 0; i < 16; ++i) {
        CKey key;
        key.MakeNewKey(true);
        CPubKey pub = key.GetPubKey();
        BOOST_REQUIRE(pub.IsValid());

        uint256 msgHash = InsecureRand256();
        std::vector<uint8_t> sig;
        BOOST_REQUIRE_MESSAGE(key.Sign(msgHash, sig),
            "Preservation 3.4: signing must succeed.");
        BOOST_CHECK_MESSAGE(pub.Verify(msgHash, sig),
            "Preservation 3.4: a genuine signature must verify.");

        // A tampered message must NOT verify (baseline sanity of the primitive).
        uint256 otherHash = InsecureRand256();
        if (otherHash != msgHash) {
            BOOST_CHECK_MESSAGE(!pub.Verify(otherHash, sig),
                "Preservation 3.4: a signature must not verify a different msg.");
        }
    }
}

// ===========================================================================
// 3.11  Genuinely valid signatures are still accepted by the OP_VERIFY_SIG
//       family (push 1). We drive the opcodes with a real secp256k1 signature
//       encoded in the deterministic length-prefixed multi-word form decoded by
//       PopVarBytes in cvm.cpp (mirroring MakeValidSignatureScenario /
//       EmitPushVarBytes in cvm_workstream2_preserve_tests.cpp): the 32-byte
//       message word, then the genuine DER signature as a length-prefixed field,
//       then the genuine public key as a length-prefixed field (pushed last so
//       the handler pops pubkey, signature, message in that order). Ground truth
//       that the scenario is genuinely valid is established with real secp256k1
//       (CPubKey::Verify == true), independent of the VM encoding. After the fix
//       a genuinely valid signature must still push 1.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_11_valid_sig_accepted_by_op_verify_sig)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();
    BOOST_REQUIRE(pub.IsValid());
    uint256 msgHash = InsecureRand256();
    std::vector<uint8_t> sig;
    BOOST_REQUIRE(key.Sign(msgHash, sig));
    BOOST_REQUIRE(pub.Verify(msgHash, sig)); // genuinely valid (secp256k1 ground truth)

    // ECDSA signatures are <= 72 bytes; that size routes to the ECDSA path in
    // both OP_VERIFY_SIG (auto-detect) and OP_VERIFY_SIG_ECDSA.
    BOOST_REQUIRE_MESSAGE(sig.size() <= 72,
        "sanity: DER ECDSA signature should be <= 72 bytes, got " +
        std::to_string(sig.size()));

    // The full genuine signature and public-key bytes are supplied in the new
    // multi-word encoding so real secp256k1 verification succeeds inside the
    // handler and the opcode pushes 1.
    std::vector<uint8_t> pubBytes(pub.begin(), pub.end());

    for (CVM::OpCode op : {CVM::OpCode::OP_VERIFY_SIG, CVM::OpCode::OP_VERIFY_SIG_ECDSA}) {
        std::vector<uint8_t> code;
        PushWord(code, std::vector<uint8_t>(msgHash.begin(), msgHash.end())); // 32-byte message word
        PushVarBytes(code, sig);       // genuine signature (length-prefixed)
        PushVarBytes(code, pubBytes);  // genuine pubkey (length-prefixed, on top)
        code.push_back(static_cast<uint8_t>(op));
        code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok, "bytecode execution did not complete");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 result = state.Peek(0);
        BOOST_CHECK_MESSAGE(result == arith_uint256(1),
            "Preservation 3.11: a genuinely valid signature must be accepted "
            "(push 1) by the OP_VERIFY_SIG family; opcode result=" +
            result.GetHex());
    }
}

// ===========================================================================
// 3.14  The canonical CVM deployer+nonce contract-address scheme in
//       contract.cpp (Hash(deployer || nonce)[0:20]) is deterministic and must
//       remain unchanged. (Only the separate EVM CREATE scheme changes.)
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_14_canonical_address_scheme)
{
    uint160 deployer;
    deployer.SetHex("0123456789abcdef0123456789abcdef01234567");
    const uint64_t nonce = 7;

    uint160 addr1 = CVM::GenerateContractAddress(deployer, nonce);
    uint160 addr2 = CVM::GenerateContractAddress(deployer, nonce);

    // Deterministic.
    BOOST_CHECK_MESSAGE(addr1 == addr2,
        "Preservation 3.14: address generation must be deterministic.");

    // Matches the documented formula Hash(deployer || nonce)[0:20].
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << deployer << nonce;
    uint256 h = Hash(ss.begin(), ss.end());
    uint160 expected;
    std::memcpy(expected.begin(), h.begin(), 20);
    BOOST_CHECK_MESSAGE(addr1 == expected,
        "Preservation 3.14: address must equal Hash(deployer||nonce)[0:20]; got "
        + addr1.ToString() + " expected " + expected.ToString());

    // Golden vector: pin the numeric address so the scheme cannot silently
    // change (captured on the unfixed code; the fixed code must reproduce it).
    BOOST_TEST_MESSAGE("Golden canonical address (deployer=" + deployer.ToString() +
                       ", nonce=7) = " + addr1.ToString());
    BOOST_CHECK_MESSAGE(
        addr1.ToString() == "a56cb9a042b34a3cd12fdb28af77ae338be1c5bc",
        "Preservation 3.14: canonical address golden vector changed; got " +
        addr1.ToString());

    // Distinct inputs => distinct addresses.
    BOOST_CHECK(CVM::GenerateContractAddress(deployer, nonce + 1) != addr1);
    uint160 deployer2 = deployer;
    *deployer2.begin() ^= 0xFF;
    BOOST_CHECK(CVM::GenerateContractAddress(deployer2, nonce) != addr1);
}

// Property 21 (3.14): for random (deployer, nonce) the scheme is deterministic
// and equals the documented formula.
BOOST_AUTO_TEST_CASE(preserve_3_14_canonical_address_property)
{
    for (int i = 0; i < kSamples; ++i) {
        uint160 deployer;
        uint256 r = InsecureRand256();
        std::memcpy(deployer.begin(), r.begin(), 20);
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 a = CVM::GenerateContractAddress(deployer, nonce);
        uint160 b = CVM::GenerateContractAddress(deployer, nonce);
        BOOST_REQUIRE_MESSAGE(a == b,
            "Preservation 3.14 (property): non-deterministic address at #" +
            std::to_string(i));

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << deployer << nonce;
        uint256 h = Hash(ss.begin(), ss.end());
        uint160 expected;
        std::memcpy(expected.begin(), h.begin(), 20);
        BOOST_REQUIRE_MESSAGE(a == expected,
            "Preservation 3.14 (property): formula mismatch at #" +
            std::to_string(i));
    }
}

// ===========================================================================
// 3.13  Receipt JSON keeps all existing fields. The bloom filter (logsBloom) is
//       a flagged path (1.28) that will change, so we assert its PRESENCE only
//       — never its value — while pinning every other documented field.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_13_receipt_json_fields)
{
    CVM::TransactionReceipt r;
    r.transactionHash = InsecureRand256();
    r.transactionIndex = 3;
    r.blockHash = InsecureRand256();
    r.blockNumber = 12345;
    std::memcpy(r.from.begin(), InsecureRand256().begin(), 20);
    r.contractAddress = CVM::GenerateContractAddress(r.from, 0);
    r.gasUsed = 21000;
    r.cumulativeGasUsed = 42000;
    r.status = 1;
    r.senderReputation = 60;
    r.reputationDiscount = 5;
    r.usedFreeGas = false;

    // Add one log so the logs array shape is captured too.
    CVM::LogEntry log;
    log.address = r.from;
    log.topics.push_back(InsecureRand256());
    log.data = {0xde, 0xad, 0xbe, 0xef};
    r.logs.push_back(log);

    UniValue json = r.ToJSON();

    // All existing top-level fields must be present (3.13).
    const char* fields[] = {
        "transactionHash", "transactionIndex", "blockHash", "blockNumber",
        "from", "to", "contractAddress", "gasUsed", "cumulativeGasUsed",
        "status", "logs", "cascoin"
    };
    for (const char* f : fields) {
        BOOST_CHECK_MESSAGE(json.exists(f),
            std::string("Preservation 3.13: receipt JSON must keep field '") + f + "'.");
    }

    // Field VALUES that must be preserved exactly (independent of logsBloom).
    BOOST_CHECK_EQUAL(json["transactionHash"].get_str(), r.transactionHash.GetHex());
    BOOST_CHECK_EQUAL(json["blockHash"].get_str(), r.blockHash.GetHex());
    BOOST_CHECK_EQUAL(json["from"].get_str(), r.from.GetHex());
    BOOST_CHECK_EQUAL(json["contractAddress"].get_str(), r.contractAddress.GetHex());
    BOOST_CHECK_EQUAL(json["status"].get_str(), std::string("0x1"));
    BOOST_CHECK(json["logs"].isArray());
    BOOST_CHECK_EQUAL(json["logs"].size(), 1);

    // Cascoin-specific sub-object preserved.
    const UniValue& cascoin = json["cascoin"];
    BOOST_CHECK(cascoin.exists("senderReputation"));
    BOOST_CHECK(cascoin.exists("reputationDiscount"));
    BOOST_CHECK(cascoin.exists("usedFreeGas"));

    // logsBloom: presence only (its value is a flagged path that WILL change).
    BOOST_CHECK_MESSAGE(json.exists("logsBloom"),
        "Preservation 3.13: logsBloom key must exist (its value may change).");
}

// ===========================================================================
// 3.22  A zero-value deploy/call still exposes CALLVALUE = 0. The corrected
//       block-processing path will pass the actual (zero) value, so this input
//       is unchanged. Verified directly against the OP_CALLVALUE handler.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_22_zero_value_callvalue_is_zero)
{
    std::vector<uint8_t> code;
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_CALLVALUE));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    // Default-constructed state (and an explicit zero) both mean CALLVALUE == 0.
    for (bool explicitZero : {false, true}) {
        CVM::CVM vm;
        CVM::VMState state;
        state.SetGasLimit(1000000);
        if (explicitZero) state.SetCallValue(0);

        bool ok = vm.Execute(code, state, /*storage=*/nullptr);
        BOOST_REQUIRE_MESSAGE(ok, "bytecode execution did not complete");
        BOOST_REQUIRE_GE(state.StackSize(), size_t(1));

        arith_uint256 callValue = state.Peek(0);
        BOOST_CHECK_MESSAGE(callValue == arith_uint256(),
            "Preservation 3.22: a zero-value call must expose CALLVALUE = 0; got "
            + callValue.GetHex());
    }
}

BOOST_AUTO_TEST_SUITE_END()
