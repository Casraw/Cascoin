// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CVM Functional Fixes — Workstream 6 Test Suite (BEFORE fix)
 *
 * Spec: .kiro/specs/cvm-functional-fixes  (bugfix)
 * Task 15: "Write Workstream-6 exploratory + preservation tests (BEFORE fix)"
 *
 * Workstream 6 = EVM compatibility
 * (bugfix.md clauses 1.11, 1.12, 1.27, 1.28, 1.29, 1.30, 1.31, 1.32;
 *  preservation 3.9, 3.13, 3.14).
 *
 * This single suite contains BOTH:
 *
 *   (a) FIX-PROPERTY tests (Property 13 — Bug Condition). Each encodes the
 *       EXPECTED post-fix behaviour (Expected-Behavior clauses 2.11, 2.12,
 *       2.27, 2.28, 2.29, 2.30, 2.31, 2.32). Written BEFORE the fix, they are
 *       EXPECTED TO FAIL on the current (unfixed) code — every failure is a
 *       counterexample confirming a defect. After the Workstream-6 fix lands
 *       (task 16) the SAME tests must pass. Prefixed `p13_`.
 *
 *   (b) PRESERVATION tests (Property 21 — Preservation, clauses 3.9, 3.13,
 *       3.14). These capture behaviour that Workstream 6 must NOT change: the
 *       already-supported CVM opcodes/context (3.9), the receipt JSON field set
 *       (every field except the corrected `logsBloom`, 3.13), and the
 *       CVM-native `contract.cpp` deployer+nonce address scheme (3.14 — only the
 *       EVM CREATE derivation in `nonce_manager.cpp` changes). They are EXPECTED
 *       TO PASS on the unfixed code (baseline behaviour to preserve). Prefixed
 *       `preserve_`.
 *
 * ---------------------------------------------------------------------------
 * BUILD CONFIGURATION (checked at task time): `ENABLE_EVMC` IS defined in this
 * build (config/bitcoin-config.h => `#define ENABLE_EVMC 1`). EVM-specific
 * clauses are therefore compiled and exercised where a unit-level seam exists.
 * ---------------------------------------------------------------------------
 *
 * Defects under test and their testability in THIS build:
 *
 *   1.27  nonce_manager.cpp GenerateContractAddress uses `Hash160(sender||nonce)`
 *         instead of the Ethereum `keccak256(rlp([sender, nonce]))[12:]`.
 *         COVERED (no EVMC needed): Ethereum CREATE golden vector + a property
 *         that the CREATE address no longer equals the legacy Hash160 scheme.
 *
 *   1.28  receipt.cpp TransactionReceipt::ToJSON emits `logsBloom` as 512 zero
 *         hex chars regardless of the logs.
 *         COVERED (no EVMC needed): a receipt carrying logs must produce a
 *         non-zero bloom.
 *
 *   1.32  enhanced_storage.cpp GenerateStorageProof/VerifyStorageProof build a
 *         basic hash structure (not an MPT) and VerifyStorageProof IGNORES the
 *         `root` argument entirely, so a proof verifies against ANY root.
 *         COVERED (EnhancedStorage is compiled unconditionally): a proof must
 *         NOT verify against a root unrelated to the committed state.
 *
 *   1.11  evmc_host.cpp registers `nullptr` for get_transient_storage /
 *         set_transient_storage (TLOAD/TSTORE non-functional).
 *         COVERED under ENABLE_EVMC: the host interface's transient-storage
 *         callbacks must be registered (non-null).
 *
 *   1.12  evmc_host.cpp get_tx_context_fn returns `block_base_fee = {}` (BASEFEE
 *         always 0). DEFERRED: base fee is neither stored on EVMCHost nor
 *         exposed through any public accessor; observing it requires executing
 *         EVM bytecode with the BASEFEE opcode through evmone with a populated
 *         block context (no unit seam). Fixed alongside the host tx-context.
 *
 *   1.29  evm_rpc.cpp sender extraction returns empty uint160 and gas estimation
 *         adds fixed constants. DEFERRED: the extraction/estimation helpers are
 *         file-static inside evm_rpc.cpp and only reachable through the JSON-RPC
 *         dispatch (needs a live chain/mempool), so there is no unit seam.
 *
 *   1.30  evm_engine.cpp does not inject caller reputation and does only basic
 *         trust-tagged memory validation. DEFERRED: the injection/validation
 *         methods (InjectCallerReputation, ValidateTrustTaggedMemoryAccess, …)
 *         are private and only run inside a full EVM execution; no unit seam.
 *
 *   1.31  enhanced_vm.cpp SaveExecutionState pushes an unpopulated placeholder
 *         ExecutionFrame. DEFERRED: SaveExecutionState is private, the
 *         ExecutionFrame struct is private, and the execution_stack is only
 *         mutated during a nested call inside Execute(); no unit seam.
 *
 * The four DEFERRED clauses (1.12, 1.29, 1.30, 1.31) have no unit-level
 * observable in the current class APIs. They are covered by the Workstream-6
 * fix (task 16) plus the dual-path deploy→call functional integration test
 * (task 29) and multi-node integration (task 30). Adding public accessors purely
 * to observe them would change the surface under test; that is intentionally
 * avoided here so the suite compiles and runs against the code as-is.
 *
 * Expected-Behavior targets: 2.11, 2.12, 2.27, 2.28, 2.29, 2.30, 2.31, 2.32
 * Preservation: 3.9, 3.13, 3.14
 * Requirements: 1.11, 1.12, 1.27, 1.28, 1.29, 1.30, 1.31, 1.32, 3.9, 3.13, 3.14
 */

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <cvm/nonce_manager.h>
#include <cvm/receipt.h>
#include <cvm/enhanced_storage.h>
#include <cvm/cvmdb.h>
#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>
#include <cvm/contract.h>

#ifdef ENABLE_EVMC
#include <cvm/evmc_host.h>
#endif

#include <arith_uint256.h>
#include <fs.h>
#include <hash.h>
#include <uint256.h>
#include <univalue.h>
#include <utilstrencodings.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

static constexpr int kSamples = 256;

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

// The legacy (buggy) EVM CREATE derivation the fix must abandon:
// Hash160(sender ++ nonce_big_endian). Mirrors nonce_manager.cpp exactly so the
// property test can assert the corrected address no longer matches it.
uint160 LegacyHash160CreateAddress(const uint160& sender, uint64_t nonce)
{
    std::vector<uint8_t> data;
    data.insert(data.end(), sender.begin(), sender.end());
    for (int i = 7; i >= 0; i--) {
        data.push_back((nonce >> (i * 8)) & 0xFF);
    }
    return Hash160(data.begin(), data.end());
}

// Append a PUSH<size> immediate carrying the given bytes (size must be 1..32).
// The VM reads the bytes big-endian into an arith_uint256 stack word.
void EmitPush(std::vector<uint8_t>& code, const std::vector<uint8_t>& bytes)
{
    BOOST_REQUIRE(!bytes.empty() && bytes.size() <= 32);
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_PUSH));
    code.push_back(static_cast<uint8_t>(bytes.size()));
    code.insert(code.end(), bytes.begin(), bytes.end());
}

// Push a uint64 big-endian; ReadImmediate folds the bytes big-endian, so the
// reconstructed stack word equals `v` exactly.
void EmitPushBE(std::vector<uint8_t>& code, uint64_t v)
{
    std::vector<uint8_t> bytes;
    for (int i = 7; i >= 0; --i) {
        uint8_t b = static_cast<uint8_t>((v >> (i * 8)) & 0xFF);
        if (!bytes.empty() || b != 0 || i == 0) bytes.push_back(b);
    }
    if (bytes.empty()) bytes.push_back(0);
    EmitPush(code, bytes);
}

// Run a binary op over (a, b). Pop order is b (top) then a, so push a first.
arith_uint256 RunBinaryOp(CVM::OpCode op, uint64_t a, uint64_t b)
{
    std::vector<uint8_t> code;
    EmitPushBE(code, a);
    EmitPushBE(code, b);
    code.push_back(static_cast<uint8_t>(op));
    code.push_back(static_cast<uint8_t>(CVM::OpCode::OP_STOP));

    CVM::CVM vm;
    CVM::VMState state;
    state.SetGasLimit(1000000);
    BOOST_REQUIRE(vm.Execute(code, state, nullptr));
    BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
    return state.Peek(0);
}

// A receipt carrying one log entry, fully populated with deterministic fields.
CVM::TransactionReceipt MakePopulatedReceipt(bool withLog)
{
    CVM::TransactionReceipt r;
    r.transactionHash.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    r.transactionIndex = 2;
    r.blockHash.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    r.blockNumber = 0x1234;
    r.from.SetHex("00112233445566778899aabbccddeeff00112233");
    r.to.SetHex("445566778899aabbccddeeff0011223344556677");
    r.contractAddress.SetHex("8899aabbccddeeff00112233445566778899aabb");
    r.gasUsed = 21000;
    r.cumulativeGasUsed = 42000;
    r.status = 1;
    r.senderReputation = 77;
    r.reputationDiscount = 1234;
    r.usedFreeGas = true;

    if (withLog) {
        CVM::LogEntry log;
        log.address.SetHex("cccccccccccccccccccccccccccccccccccccccc");
        uint256 t0; t0.SetHex("00000000000000000000000000000000000000000000000000000000000000aa");
        uint256 t1; t1.SetHex("00000000000000000000000000000000000000000000000000000000000000bb");
        log.topics = {t0, t1};
        log.data = {0xde, 0xad, 0xbe, 0xef};
        r.logs.push_back(log);
    }
    return r;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_workstream6_tests, BasicTestingSetup)

// ###########################################################################
// #  FIX-PROPERTY TESTS (Property 13) — EXPECTED TO FAIL on unfixed code     #
// ###########################################################################

// ===========================================================================
// Property 13 (1.27) — EVM CREATE address == keccak256(rlp([sender, nonce]))[12:]
//
// Expected (2.27): NonceManager::GenerateContractAddress SHALL compute the
// Ethereum-compatible CREATE address so a Cascoin EVM deployment lands at the
// same address an Ethereum client would compute.
//
// This is the Ethereum CREATE golden vector required by the task. The canonical
// ethereumjs-util test vector: for sender
//   0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0
// the CREATE addresses are
//   nonce 0 -> 0xcd234a471b72ba2f1ccf0a70fcaba648a5eecd8d
//   nonce 1 -> 0x343c43a37d37dff08ae8c4a11544c718abb4fcf8
// Addresses are compared in Ethereum big-endian display order (matching how
// receipt.cpp renders `from`/`contractAddress` via GetHex()).
//
// UNFIXED: GenerateContractAddress returns Hash160(sender||nonce), which does
// not match the Ethereum vector -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p13_1_27_create_address_golden_vector)
{
    CVM::NonceManager mgr(nullptr); // GenerateContractAddress is pure (no DB use)

    uint160 sender;
    sender.SetHex("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");

    uint160 expected0; expected0.SetHex("cd234a471b72ba2f1ccf0a70fcaba648a5eecd8d");
    uint160 expected1; expected1.SetHex("343c43a37d37dff08ae8c4a11544c718abb4fcf8");

    uint160 got0 = mgr.GenerateContractAddress(sender, 0);
    uint160 got1 = mgr.GenerateContractAddress(sender, 1);

    BOOST_CHECK_MESSAGE(got0 == expected0,
        "P13 (1.27): EVM CREATE golden vector mismatch for nonce 0; got " +
        got0.GetHex() + " expected " + expected0.GetHex() +
        " (keccak256(rlp([sender,nonce]))[12:]). The unfixed code returns "
        "Hash160(sender||nonce)=" + LegacyHash160CreateAddress(sender, 0).GetHex() + ".");

    BOOST_CHECK_MESSAGE(got1 == expected1,
        "P13 (1.27): EVM CREATE golden vector mismatch for nonce 1; got " +
        got1.GetHex() + " expected " + expected1.GetHex() + ".");
}

// Property 13 (1.27): for random (sender, nonce) the corrected CREATE address
// must NOT equal the legacy Hash160(sender||nonce) scheme. This encodes the
// behavioural change directly: on unfixed code the two are identical for every
// input (they share the same code path) -> the property FAILS everywhere.
BOOST_AUTO_TEST_CASE(p13_1_27_create_address_not_hash160)
{
    CVM::NonceManager mgr(nullptr);

    for (int i = 0; i < kSamples; ++i) {
        uint160 sender = RandAddress();
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 got = mgr.GenerateContractAddress(sender, nonce);
        uint160 legacy = LegacyHash160CreateAddress(sender, nonce);

        BOOST_CHECK_MESSAGE(got != legacy,
            "P13 (1.27): EVM CREATE address still equals the legacy "
            "Hash160(sender||nonce) scheme (sample #" + std::to_string(i) +
            ", sender " + sender.GetHex() + ", nonce " + std::to_string(nonce) +
            "); it must instead be keccak256(rlp([sender,nonce]))[12:].");
    }
}

// ===========================================================================
// Property 13 (1.28) — receipt logsBloom computed from the logs.
//
// Expected (2.28): TransactionReceipt::ToJSON SHALL compute `logsBloom` as a
// bloom filter over the receipt's log addresses and topics, so a receipt that
// carries logs has a NON-zero bloom.
//
// UNFIXED: ToJSON always emits "0x" + 512 '0' chars regardless of the logs ->
// the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p13_1_28_logsbloom_computed_from_logs)
{
    const std::string kAllZeroBloom = "0x" + std::string(512, '0');

    CVM::TransactionReceipt withLogs = MakePopulatedReceipt(/*withLog=*/true);
    UniValue j = withLogs.ToJSON();

    BOOST_REQUIRE_MESSAGE(j.exists("logsBloom"),
        "P13 (1.28): receipt JSON is missing the logsBloom field.");
    std::string bloom = j["logsBloom"].get_str();

    // A real 2048-bit bloom is still 512 hex chars (256 bytes) wide.
    BOOST_CHECK_MESSAGE(bloom.size() == kAllZeroBloom.size(),
        "P13 (1.28): logsBloom width changed (got " + std::to_string(bloom.size()) +
        " chars, expected " + std::to_string(kAllZeroBloom.size()) + ").");

    BOOST_CHECK_MESSAGE(bloom != kAllZeroBloom,
        "P13 (1.28): logsBloom is all zeros for a receipt that carries logs; "
        "ToJSON emits a fixed 512-zero string instead of a bloom computed from "
        "the log addresses/topics.");
}

// ===========================================================================
// Property 13 (1.32) — storage proof attests to committed state (bound to root).
//
// Expected (2.32): EnhancedStorage::GenerateStorageProof/VerifyStorageProof
// SHALL produce and verify a proof that attests to the committed storage state.
// A necessary condition for a real (MPT) proof: it must NOT verify against a
// state root that is unrelated to the committed storage.
//
// UNFIXED: VerifyStorageProof only re-hashes (contractAddr, key, value) and
// IGNORES the `root` argument entirely, so the proof verifies against ANY root
// (including a random/bogus one) -> the property FAILS (counterexample).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p13_1_32_storage_proof_bound_to_root)
{
    auto db = MakeTempDb();
    CVM::EnhancedStorage storage(db.get());

    for (int i = 0; i < 16; ++i) {
        uint160 addr = RandAddress();
        uint256 key = InsecureRand256();
        uint256 value = InsecureRand256();

        BOOST_REQUIRE_MESSAGE(storage.Store(addr, key, value),
            "P13 (1.32): precondition — storing a value should succeed.");

        std::vector<uint256> proof = storage.GenerateStorageProof(addr, key);
        BOOST_REQUIRE_MESSAGE(!proof.empty(),
            "P13 (1.32): precondition — a proof should be produced for a "
            "committed key (sample #" + std::to_string(i) + ").");

        // A root that is NOT the committed storage root. A genuine MPT proof
        // must fail to verify against it; the placeholder verifier ignores it.
        uint256 bogusRoot = InsecureRand256();
        bool verified = storage.VerifyStorageProof(proof, bogusRoot, addr, key, value);

        BOOST_CHECK_MESSAGE(!verified,
            "P13 (1.32): storage proof verified against a bogus/unrelated state "
            "root (sample #" + std::to_string(i) + "); VerifyStorageProof ignores "
            "the root and only re-hashes (addr,key,value), so the proof does not "
            "attest to any committed state.");
    }
}

#ifdef ENABLE_EVMC
// ===========================================================================
// Property 13 (1.11) — TLOAD/TSTORE transient-storage handlers registered.
//
// Expected (2.11): the EVM host SHALL support the transient storage opcodes.
// The evmone backend dispatches TLOAD/TSTORE through the host interface's
// get_transient_storage / set_transient_storage callbacks; if those are null,
// transient storage is non-functional.
//
// UNFIXED: evmc_host.cpp initialises the host interface with `nullptr` for both
// transient-storage callbacks -> the property FAILS (counterexample).
//
// Compiled only when ENABLE_EVMC is defined (it is, in this build).
// ===========================================================================
BOOST_AUTO_TEST_CASE(p13_1_11_transient_storage_handlers_registered)
{
    const evmc_host_interface* iface = CVM::EVMCHost::GetInterface();
    BOOST_REQUIRE_MESSAGE(iface != nullptr,
        "P13 (1.11): EVMCHost::GetInterface() returned null.");

    BOOST_CHECK_MESSAGE(iface->get_transient_storage != nullptr,
        "P13 (1.11): get_transient_storage host callback is null; TLOAD is "
        "non-functional (transient storage handlers are not registered).");
    BOOST_CHECK_MESSAGE(iface->set_transient_storage != nullptr,
        "P13 (1.11): set_transient_storage host callback is null; TSTORE is "
        "non-functional (transient storage handlers are not registered).");
}
#endif // ENABLE_EVMC

// NOTE — DEFERRED fix-property clauses in this build (documented above):
//   1.12 (BASEFEE), 1.29 (evm_rpc sender/gas), 1.30 (evm_engine reputation
//   injection / trust-tagged memory), 1.31 (enhanced_vm nested ExecutionFrame).
//   These have no unit-level observable in the current class APIs (values are
//   private / only reachable through a full EVM execution or JSON-RPC dispatch).
//   They are covered by the Workstream-6 fix (task 16) and the integration
//   tests (tasks 29, 30). No un-compilable code is emitted for them.

// ###########################################################################
// #  PRESERVATION TESTS (Property 21) — EXPECTED TO PASS on unfixed code     #
// ###########################################################################

// ===========================================================================
// Preservation 3.14 — CVM-native contract.cpp deployer+nonce scheme UNCHANGED.
//
// Only the EVM CREATE derivation (nonce_manager.cpp, clause 1.27) changes. The
// CVM-native address scheme `CVM::GenerateContractAddress(deployer, nonce)` in
// contract.cpp — Hash(deployer||nonce)[0:20] — must reproduce identical results
// after the fix. We pin it as a golden vector + property.
// EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_14_cvm_native_address_scheme_golden)
{
    uint160 deployer;
    deployer.SetHex("0123456789abcdef0123456789abcdef01234567");
    const uint64_t nonce = 7;

    uint160 addr1 = CVM::GenerateContractAddress(deployer, nonce);
    uint160 addr2 = CVM::GenerateContractAddress(deployer, nonce);
    BOOST_CHECK_MESSAGE(addr1 == addr2,
        "Preservation 3.14: CVM-native address generation must be deterministic.");

    // Documented formula: Hash(deployer || nonce)[0:20].
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << deployer << nonce;
    uint256 h = Hash(ss.begin(), ss.end());
    uint160 expected;
    std::memcpy(expected.begin(), h.begin(), 20);
    BOOST_CHECK_MESSAGE(addr1 == expected,
        "Preservation 3.14: CVM-native address must equal Hash(deployer||nonce)"
        "[0:20]; got " + addr1.ToString() + " expected " + expected.ToString());

    // Golden vector: pin the numeric address so the CVM-native scheme cannot
    // silently change (matches the Workstream-1 preservation golden vector).
    BOOST_CHECK_MESSAGE(
        addr1.ToString() == "a56cb9a042b34a3cd12fdb28af77ae338be1c5bc",
        "Preservation 3.14: CVM-native address golden vector changed; got " +
        addr1.ToString());
}

// Property 21 (3.14): for random (deployer, nonce) the CVM-native scheme is
// deterministic and equals the documented formula, unchanged by Workstream 6.
BOOST_AUTO_TEST_CASE(preserve_3_14_cvm_native_address_scheme_property)
{
    for (int i = 0; i < kSamples; ++i) {
        uint160 deployer = RandAddress();
        uint64_t nonce = InsecureRand256().GetUint64(0);

        uint160 a = CVM::GenerateContractAddress(deployer, nonce);
        uint160 b = CVM::GenerateContractAddress(deployer, nonce);
        BOOST_REQUIRE_MESSAGE(a == b,
            "Preservation 3.14 (property): non-deterministic CVM-native address "
            "at #" + std::to_string(i));

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << deployer << nonce;
        uint256 h = Hash(ss.begin(), ss.end());
        uint160 expected;
        std::memcpy(expected.begin(), h.begin(), 20);
        BOOST_REQUIRE_MESSAGE(a == expected,
            "Preservation 3.14 (property): CVM-native formula mismatch at #" +
            std::to_string(i));
    }
}

// ===========================================================================
// Preservation 3.13 — Receipt JSON fields UNCHANGED except logsBloom.
//
// The receipt-to-JSON mapping must keep every existing field and value; only
// `logsBloom` is corrected by the fix. We capture the current field set and the
// deterministic field values as golden behaviour, and assert `logsBloom` is
// present (its VALUE is intentionally not pinned — it changes with the fix).
// EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_13_receipt_json_fields_golden)
{
    CVM::TransactionReceipt r = MakePopulatedReceipt(/*withLog=*/true);
    UniValue j = r.ToJSON();

    // Every documented top-level field must be present.
    const std::vector<std::string> topKeys = {
        "transactionHash", "transactionIndex", "blockHash", "blockNumber",
        "from", "to", "contractAddress", "gasUsed", "cumulativeGasUsed",
        "status", "logs", "logsBloom", "cascoin"};
    for (const std::string& k : topKeys) {
        BOOST_CHECK_MESSAGE(j.exists(k),
            "Preservation 3.13: receipt JSON is missing top-level field '" + k + "'.");
    }

    // Deterministic field values (Ethereum-compatible formatting) unchanged.
    BOOST_CHECK_EQUAL(j["transactionHash"].get_str(), r.transactionHash.GetHex());
    BOOST_CHECK_EQUAL(j["transactionIndex"].get_str(), std::string("0x2"));
    BOOST_CHECK_EQUAL(j["blockHash"].get_str(), r.blockHash.GetHex());
    BOOST_CHECK_EQUAL(j["blockNumber"].get_str(), std::string("0x1234"));
    BOOST_CHECK_EQUAL(j["from"].get_str(), r.from.GetHex());
    BOOST_CHECK_EQUAL(j["to"].get_str(), r.to.GetHex());
    BOOST_CHECK_EQUAL(j["contractAddress"].get_str(), r.contractAddress.GetHex());
    BOOST_CHECK_EQUAL(j["gasUsed"].get_str(), std::string("0x5208"));         // 21000
    BOOST_CHECK_EQUAL(j["cumulativeGasUsed"].get_str(), std::string("0xa410")); // 42000
    BOOST_CHECK_EQUAL(j["status"].get_str(), std::string("0x1"));

    // Logs array preserved with per-log fields.
    BOOST_REQUIRE(j["logs"].isArray());
    BOOST_REQUIRE_EQUAL(j["logs"].size(), 1u);
    UniValue log0 = j["logs"][0];
    BOOST_CHECK_EQUAL(log0["address"].get_str(), r.logs[0].address.GetHex());
    BOOST_CHECK_EQUAL(log0["logIndex"].get_str(), std::string("0x0"));
    BOOST_CHECK_EQUAL(log0["data"].get_str(), std::string("0x") + HexStr(r.logs[0].data));
    BOOST_REQUIRE(log0["topics"].isArray());
    BOOST_REQUIRE_EQUAL(log0["topics"].size(), 2u);
    BOOST_CHECK_EQUAL(log0["topics"][0].get_str(), r.logs[0].topics[0].GetHex());
    BOOST_CHECK_EQUAL(log0["topics"][1].get_str(), r.logs[0].topics[1].GetHex());

    // Cascoin-specific sub-object preserved.
    UniValue cascoin = j["cascoin"];
    BOOST_REQUIRE(cascoin.isObject());
    BOOST_CHECK_EQUAL(cascoin["senderReputation"].get_int(), (int)r.senderReputation);
    BOOST_CHECK_EQUAL(cascoin["reputationDiscount"].get_int64(), (int64_t)r.reputationDiscount);
    // usedFreeGas is emitted as a JSON number (univalue has no bool pushKV
    // overload, so the bool promotes to int) — capture that as-is for preservation.
    BOOST_CHECK_EQUAL(cascoin["usedFreeGas"].get_int(), r.usedFreeGas ? 1 : 0);

    // logsBloom must exist and keep its 514-char ("0x" + 512 hex) width — its
    // VALUE is corrected by the fix, so it is NOT pinned here.
    BOOST_REQUIRE(j.exists("logsBloom"));
    BOOST_CHECK_EQUAL(j["logsBloom"].get_str().size(), size_t(2 + 512));
}

// Preservation 3.13: a receipt with NO logs keeps an all-zero bloom (this holds
// both before and after the fix — an empty log set produces an empty bloom).
BOOST_AUTO_TEST_CASE(preserve_3_13_empty_logs_zero_bloom)
{
    CVM::TransactionReceipt r = MakePopulatedReceipt(/*withLog=*/false);
    UniValue j = r.ToJSON();

    BOOST_REQUIRE(j["logs"].isArray());
    BOOST_CHECK_EQUAL(j["logs"].size(), 0u);
    BOOST_CHECK_EQUAL(j["logsBloom"].get_str(), std::string("0x") + std::string(512, '0'));
}

// ===========================================================================
// Preservation 3.9 — Already-supported CVM opcodes/context UNCHANGED.
//
// Workstream 6 adds EVM features but must not alter the existing CVM opcode
// semantics or execution context. We pin arithmetic/comparison golden vectors
// and context-opcode behaviour as baseline. EXPECTED: PASS on unfixed code.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_3_9_arithmetic_comparison_golden)
{
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_ADD, 7, 5) == arith_uint256(12));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_SUB, 20, 8) == arith_uint256(12));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_MUL, 6, 7) == arith_uint256(42));

    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_EQ, 4, 4) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_LT, 3, 9) == arith_uint256(1));
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_GT, 3, 9) == arith_uint256());
    BOOST_CHECK(RunBinaryOp(CVM::OpCode::OP_GE, 8, 9) == arith_uint256());
}

// Property 21 (3.9): ADD/SUB/MUL and comparison match the arith_uint256
// reference across random operands (kept small to avoid overflow ambiguity).
BOOST_AUTO_TEST_CASE(preserve_3_9_arithmetic_comparison_property)
{
    for (int i = 0; i < kSamples; ++i) {
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
    }
}

// Property 21 (3.9): context opcodes expose their configured values unchanged.
BOOST_AUTO_TEST_CASE(preserve_3_9_context_opcodes_property)
{
    for (int i = 0; i < 64; ++i) {
        uint64_t callValue = InsecureRandRange(0x100000000ULL);
        int64_t timestamp = static_cast<int64_t>(InsecureRandRange(0x7FFFFFFFULL));
        int blockHeight = static_cast<int>(InsecureRandRange(0x7FFFFFFFULL));

        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_CALLVALUE),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::CVM vm; CVM::VMState state;
            state.SetGasLimit(1000000);
            state.SetCallValue(callValue);
            BOOST_REQUIRE(vm.Execute(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256(callValue),
                "3.9: CALLVALUE mismatch (#" + std::to_string(i) + ")");
        }
        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_TIMESTAMP),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::CVM vm; CVM::VMState state;
            state.SetGasLimit(1000000);
            state.SetTimestamp(timestamp);
            BOOST_REQUIRE(vm.Execute(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256((uint64_t)timestamp),
                "3.9: TIMESTAMP mismatch (#" + std::to_string(i) + ")");
        }
        {
            std::vector<uint8_t> code = {
                static_cast<uint8_t>(CVM::OpCode::OP_BLOCKHEIGHT),
                static_cast<uint8_t>(CVM::OpCode::OP_STOP)};
            CVM::CVM vm; CVM::VMState state;
            state.SetGasLimit(1000000);
            state.SetBlockHeight(blockHeight);
            BOOST_REQUIRE(vm.Execute(code, state, nullptr));
            BOOST_REQUIRE_GE(state.StackSize(), size_t(1));
            BOOST_CHECK_MESSAGE(state.Peek(0) == arith_uint256((uint64_t)blockHeight),
                "3.9: BLOCKHEIGHT mismatch (#" + std::to_string(i) + ")");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
