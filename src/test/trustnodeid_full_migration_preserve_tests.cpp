// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TrustNodeId Full Migration — Preservation Test Suite (baseline behaviour)
 *
 * Spec: .kiro/specs/trustnodeid-full-migration  (bugfix)
 * Task 2: "Write preservation property tests for all non-bug-condition
 *          behavior".
 *
 * PURPOSE
 * -------
 * Property 2 (Preservation): for inputs where the bug condition does NOT hold,
 * the migrated code MUST produce the SAME result as the current code. Following
 * the observation-first methodology (design.md → Testing Strategy), these tests
 * capture the CURRENT (unfixed) behaviour of the paths that are OUT OF SCOPE for
 * the user-identity migration and MUST remain byte-for-byte / behaviourally
 * unchanged after the migration lands.
 *
 * These tests are EXPECTED TO PASS on the current (unfixed) baseline and MUST
 * still pass unchanged after the migration (tasks 3.x). A test that FAILS here
 * would mean the captured case actually reaches a migrated path and must be
 * re-classified.
 *
 * Captured behaviours, mapped to Unchanged-Behavior clauses (bugfix.md 4.x):
 *   4.1  Contract/EVM addresses stay 20-byte uint160 with identical derivation,
 *        serialization, and call-data format (Contract, ContractCallTx,
 *        CVMCallData, GenerateContractAddress).
 *   4.2  Protocol/internal hashes stay uint256 with identical semantics
 *        (deployment tx, code hash, and raw uint256 round-trips).
 *   4.3  TrustEdge v1/v2 (CVMTrustEdgeData) golden fixtures and round-trips are
 *        byte-for-byte unchanged (v1 = 54 bytes, v2 = 81 bytes).
 *   4.5  Vote-range validation (ReputationVoteTx::IsValid) is unchanged.
 *   4.6  The common-input-ownership clustering heuristic is unchanged.
 *   4.7  Quantum/Falcon parsing/encoding and TrustNodeId::FromDestination /
 *        ToDestination / serialization / ToKeyString are unchanged for all five
 *        supported destination types.
 *   4.1/4.2  Non-identity fixed-width uint160/uint256 values keep their type,
 *        serialization, equality, and ordering.
 *
 * Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8
 *
 * NOTE on 4.4 (activation-path preservation): the ProcessNonContractBlock
 * ordering / validation-boundary / durable-write behaviour delivered by
 * `trust-system-activation` is already pinned by
 * `cvm_trust_activation_preserve_tests.cpp` and is re-run unchanged; this suite
 * does not duplicate that heavy chain-processing setup.
 */

#include <cvm/contract.h>
#include <cvm/reputation.h>
#include <cvm/softfork.h>
#include <cvm/trustnodeid.h>
#include <cvm/walletcluster.h>
#include <cvm/cvmdb.h>

#include <address_quantum.h>
#include <base58.h>
#include <script/standard.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <streams.h>
#include <uint256.h>
#include <amount.h>
#include <fs.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace CVM;

namespace {

// Serialization/derivation round-trips are cheap; DB-backed clustering runs
// fewer samples to keep the suite fast.
static constexpr int kSamples = 128;
static constexpr int kSamplesDb = 32;

// Exact on-chain payload sizes for CVMTrustEdgeData (see softfork.cpp).
static constexpr size_t kTrustEdgeV1Size = 54; // 20+20+2+8+4
static constexpr size_t kTrustEdgeV2Size = 81; // 1 + (1+32) + (1+32) + 2+8+4

uint160 RandU160()
{
    uint160 a;
    uint256 r = InsecureRand256();
    std::memcpy(a.begin(), r.begin(), 20);
    return a;
}

// Zero-extend a uint160 into the low 20 bytes of a uint256 (mirrors the
// internal helper used by TrustNodeId::FromDestination for uint160 types).
uint256 Extend160(const uint160& in)
{
    uint256 out;
    std::memcpy(out.begin(), in.begin(), in.size());
    return out;
}

// Serialize any object via the disk serializer, returning raw bytes.
template <typename T>
std::vector<uint8_t> DiskBytes(const T& obj)
{
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << obj;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

// Fixture: regtest params (so P2WSH is "rcas1..." and quantum is "rcasq1...")
// plus a fresh in-memory CVM database per test.
struct TniMigrationPreserveSetup : public BasicTestingSetup {
    TniMigrationPreserveSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));
    }
    ~TniMigrationPreserveSetup()
    {
        CVM::g_cvmdb.reset();
    }
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(trustnodeid_full_migration_preserve_tests, TniMigrationPreserveSetup)

// ===========================================================================
// 4.1 — Contract/EVM addresses remain 20-byte uint160 with identical
//       derivation, serialization, and call-data format.
// ===========================================================================

// Contract serializes/deserializes every field byte-for-byte, and its address
// and deployer stay 20-byte uint160. **Validates: Requirements 4.1, 4.2**
BOOST_AUTO_TEST_CASE(preserve_contract_serialization_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        Contract in;
        in.address = RandU160();
        in.deployer = RandU160();
        in.code.resize(1 + InsecureRandRange(64));
        for (auto& b : in.code) b = static_cast<uint8_t>(InsecureRandRange(256));
        in.deploymentHeight = static_cast<int>(InsecureRandRange(0x7FFFFFFFULL));
        in.deploymentTx = InsecureRand256();
        in.isCleanedUp = (InsecureRandRange(2) == 0);

        // Fixed-width scope: contract addresses are 20-byte uint160.
        BOOST_CHECK_MESSAGE(in.address.size() == 20,
            "4.1: Contract::address must be a 20-byte uint160");
        BOOST_CHECK_MESSAGE(in.deploymentTx.size() == 32,
            "4.2: Contract::deploymentTx must be a 32-byte uint256");

        const std::vector<uint8_t> bytes = DiskBytes(in);
        CDataStream ss(bytes, SER_DISK, CLIENT_VERSION);
        Contract out;
        ss >> out;

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.address == in.address, "4.1: Contract.address mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deployer == in.deployer, "4.1: Contract.deployer mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.code == in.code, "4.1: Contract.code mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deploymentHeight == in.deploymentHeight, "4.1: Contract.deploymentHeight mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.deploymentTx == in.deploymentTx, "4.2: Contract.deploymentTx mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.isCleanedUp == in.isCleanedUp, "4.1: Contract.isCleanedUp mismatch" + ctx);

        // Reserializing the decoded object reproduces the same bytes.
        BOOST_CHECK_MESSAGE(DiskBytes(out) == bytes,
            "4.1: Contract reserialization is not byte-for-byte stable" + ctx);
    }
}

// ContractCallTx keeps its 20-byte uint160 contractAddress and round-trips.
// **Validates: Requirements 4.1**
BOOST_AUTO_TEST_CASE(preserve_contract_call_tx_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        ContractCallTx in;
        in.contractAddress = RandU160();
        in.gasLimit = InsecureRandRange(0xFFFFFFFFULL);
        in.value = InsecureRandRange(0xFFFFFFFFULL);
        in.data.resize(InsecureRandRange(48));
        for (auto& b : in.data) b = static_cast<uint8_t>(InsecureRandRange(256));

        const std::vector<uint8_t> bytes = DiskBytes(in);
        CDataStream ss(bytes, SER_DISK, CLIENT_VERSION);
        ContractCallTx out;
        ss >> out;

        const std::string ctx = " (#" + std::to_string(i) + ")";
        BOOST_CHECK_MESSAGE(out.contractAddress == in.contractAddress,
            "4.1: ContractCallTx.contractAddress mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.gasLimit == in.gasLimit, "4.1: ContractCallTx.gasLimit mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.value == in.value, "4.1: ContractCallTx.value mismatch" + ctx);
        BOOST_CHECK_MESSAGE(out.data == in.data, "4.1: ContractCallTx.data mismatch" + ctx);
    }
}

// CVMCallData (the OP_RETURN contract-call payload) keeps its uint160 contract
// address across Serialize/Deserialize. This is a contract-domain payload and
// is explicitly out of the user-identity migration scope.
// **Validates: Requirements 4.1**
BOOST_AUTO_TEST_CASE(preserve_cvm_call_data_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVMCallData in;
        in.contractAddress = RandU160();
        in.gasLimit = InsecureRandRange(0xFFFFFFFFULL);
        in.format = BytecodeFormat::UNKNOWN;
        in.callData.resize(InsecureRandRange(32));
        for (auto& b : in.callData) b = static_cast<uint8_t>(InsecureRandRange(256));

        const std::vector<uint8_t> bytes = in.Serialize();
        CVMCallData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "4.1: CVMCallData must deserialize (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.contractAddress == in.contractAddress,
            "4.1: CVMCallData.contractAddress mismatch (#" + std::to_string(i) + ")");
    }
}

// GenerateContractAddress is a pure, deterministic 20-byte uint160 derivation
// from (deployer, nonce). Same input → same address; that derivation must not
// change. **Validates: Requirements 4.1**
BOOST_AUTO_TEST_CASE(preserve_generate_contract_address_determinism_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint160 deployer = RandU160();
        const uint64_t nonce = InsecureRandRange(0xFFFFFFFFULL);

        const uint160 a = GenerateContractAddress(deployer, nonce);
        const uint160 b = GenerateContractAddress(deployer, nonce);

        BOOST_CHECK_MESSAGE(a == b,
            "4.1: GenerateContractAddress must be deterministic for the same "
            "(deployer, nonce) (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(a.size() == 20,
            "4.1: GenerateContractAddress must return a 20-byte uint160 (#" +
            std::to_string(i) + ")");

        // A different nonce yields a different address (derivation is nonce-sensitive).
        const uint160 c = GenerateContractAddress(deployer, nonce + 1);
        BOOST_CHECK_MESSAGE(c != a,
            "4.1: GenerateContractAddress must depend on the nonce (#" +
            std::to_string(i) + ")");
    }
}

// ===========================================================================
// 4.2 — Protocol/internal hashes remain uint256 with identical semantics.
// ===========================================================================

// CVMDeployData carries a uint256 code hash and no user identity; its payload
// round-trips unchanged. **Validates: Requirements 4.2**
BOOST_AUTO_TEST_CASE(preserve_cvm_deploy_data_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVMDeployData in;
        in.codeHash = InsecureRand256();
        in.gasLimit = InsecureRandRange(0xFFFFFFFFULL);
        in.format = BytecodeFormat::UNKNOWN;
        in.metadata.resize(InsecureRandRange(32));
        for (auto& b : in.metadata) b = static_cast<uint8_t>(InsecureRandRange(256));

        const std::vector<uint8_t> bytes = in.Serialize();
        CVMDeployData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "4.2: CVMDeployData must deserialize (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.codeHash == in.codeHash,
            "4.2: CVMDeployData.codeHash (uint256) mismatch (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(in.codeHash.size() == 32,
            "4.2: code hash must remain a 32-byte uint256 (#" + std::to_string(i) + ")");
    }
}

// Raw uint256 hash domain: serialize/deserialize is exact, equality and
// ordering are preserved. Represents transaction IDs, bond/slash/source/dispute
// hashes, and block IDs which all remain uint256.
// **Validates: Requirements 4.2**
BOOST_AUTO_TEST_CASE(preserve_uint256_hash_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint256 h = InsecureRand256();

        const std::vector<uint8_t> bytes = DiskBytes(h);
        BOOST_CHECK_MESSAGE(bytes.size() == 32,
            "4.2: a uint256 must serialize as exactly 32 bytes (#" + std::to_string(i) + ")");

        CDataStream ss(bytes, SER_DISK, CLIENT_VERSION);
        uint256 out;
        ss >> out;
        BOOST_CHECK_MESSAGE(out == h,
            "4.2: uint256 did not round-trip (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.GetHex() == h.GetHex(),
            "4.2: uint256 hex representation changed (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 4.1 / 4.2 — Non-identity fixed-width uint160 values keep their type,
//             serialization, equality, and ordering.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_non_identity_uint160_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint160 a = RandU160();

        const std::vector<uint8_t> bytes = DiskBytes(a);
        BOOST_CHECK_MESSAGE(bytes.size() == 20,
            "4.1: a uint160 must serialize as exactly 20 bytes (#" + std::to_string(i) + ")");

        CDataStream ss(bytes, SER_DISK, CLIENT_VERSION);
        uint160 out;
        ss >> out;
        BOOST_CHECK_MESSAGE(out == a,
            "4.1: uint160 did not round-trip (#" + std::to_string(i) + ")");

        // Equality/ordering invariants are internally consistent and stable:
        // exactly one of (a < b), (b < a), (a == b) holds.
        const uint160 b = RandU160();
        const bool lt = (a < b);
        const bool gt = (b < a);
        const bool eq = (a == b);
        BOOST_CHECK_MESSAGE((int)lt + (int)gt + (int)eq == 1,
            "4.1: uint160 ordering/equality must be a strict total order (#" +
            std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(eq == !(lt || gt),
            "4.1: uint160 equality must agree with ordering (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 4.3 — TrustEdge v1/v2 (CVMTrustEdgeData) golden fixtures and round-trips are
//       byte-for-byte unchanged.  This is the completed web-of-trust-fixes
//       behaviour that the migration MUST NOT touch.
// ===========================================================================

// Golden vector: a fixed pure-uint160 (P2PKH) edge serializes to the exact
// 54-byte v1 layout `from(20) to(20) weight(2) bond(8) ts(4)` (little-endian
// scalars). **Validates: Requirements 4.3**
BOOST_AUTO_TEST_CASE(preserve_trustedge_v1_golden)
{
    CVMTrustEdgeData in;
    // Deterministic, easily recognizable endpoints.
    for (int i = 0; i < 20; ++i) in.fromAddress.begin()[i] = static_cast<uint8_t>(0x10 + i);
    for (int i = 0; i < 20; ++i) in.toAddress.begin()[i] = static_cast<uint8_t>(0xA0 + i);
    in.weight = 0x1234;                 // 4660
    in.bondAmount = 0x0102030405060708; // recognizable LE pattern
    in.timestamp = 0x0A0B0C0D;

    const std::vector<uint8_t> bytes = in.Serialize();
    BOOST_REQUIRE_MESSAGE(bytes.size() == kTrustEdgeV1Size,
        "4.3: pure-uint160 edge must serialize as the 54-byte v1 layout; got " +
        std::to_string(bytes.size()));

    // Build the expected byte string explicitly.
    std::vector<uint8_t> expected;
    for (int i = 0; i < 20; ++i) expected.push_back(static_cast<uint8_t>(0x10 + i));
    for (int i = 0; i < 20; ++i) expected.push_back(static_cast<uint8_t>(0xA0 + i));
    // weight LE (int16)
    expected.push_back(0x34); expected.push_back(0x12);
    // bond LE (int64)
    for (int i = 0; i < 8; ++i) expected.push_back(static_cast<uint8_t>((in.bondAmount >> (i * 8)) & 0xFF));
    // timestamp LE (uint32)
    for (int i = 0; i < 4; ++i) expected.push_back(static_cast<uint8_t>((in.timestamp >> (i * 8)) & 0xFF));

    BOOST_CHECK_MESSAGE(bytes == expected,
        "4.3: v1 golden byte layout changed");

    CVMTrustEdgeData out;
    BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes), "4.3: v1 golden payload must deserialize");
    BOOST_CHECK(out.fromAddress == in.fromAddress);
    BOOST_CHECK(out.toAddress == in.toAddress);
    BOOST_CHECK(out.weight == in.weight);
    BOOST_CHECK(out.bondAmount == in.bondAmount);
    BOOST_CHECK(out.timestamp == in.timestamp);
}

// Property: any pure-uint160 (P2PKH) edge serializes as 54-byte v1 and
// round-trips every field. **Validates: Requirements 4.3**
BOOST_AUTO_TEST_CASE(preserve_trustedge_v1_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        CVMTrustEdgeData in;
        in.fromAddress = RandU160();
        in.toAddress = RandU160();
        in.weight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        in.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        in.timestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));

        const std::vector<uint8_t> bytes = in.Serialize();
        BOOST_CHECK_MESSAGE(bytes.size() == kTrustEdgeV1Size,
            "4.3: v1 payload must be 54 bytes; got " + std::to_string(bytes.size()) +
            " (#" + std::to_string(i) + ")");

        CVMTrustEdgeData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "4.3: v1 payload must deserialize (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.fromAddress == in.fromAddress && out.toAddress == in.toAddress &&
                            out.weight == in.weight && out.bondAmount == in.bondAmount &&
                            out.timestamp == in.timestamp,
            "4.3: v1 payload decoded to different fields (#" + std::to_string(i) + ")");
    }
}

// Property: an edge involving a P2WSH or quantum node is emitted as the 81-byte
// v2 layout and round-trips the full wide TrustNodeId endpoints, weight, bond,
// and timestamp. **Validates: Requirements 4.3**
BOOST_AUTO_TEST_CASE(preserve_trustedge_v2_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint256 wide = InsecureRand256();
        const uint160 narrow = RandU160();

        CVMTrustEdgeData in;
        // from = wide P2WSH/quantum node, to = a P2PKH node (mix each iteration).
        if (i % 2 == 0) {
            in.from = TrustNodeId(TrustNodeType::P2WSH, wide);
        } else {
            in.from = TrustNodeId(TrustNodeType::QUANTUM, wide);
        }
        in.to = TrustNodeId(TrustNodeType::P2PKH, Extend160(narrow));
        in.weight = static_cast<int16_t>(static_cast<int>(InsecureRandRange(201)) - 100);
        in.bondAmount = static_cast<CAmount>(InsecureRandRange(0x7FFFFFFFFFFFULL));
        in.timestamp = static_cast<uint32_t>(InsecureRandRange(0xFFFFFFFFULL));

        const std::vector<uint8_t> bytes = in.Serialize();
        BOOST_CHECK_MESSAGE(bytes.size() == kTrustEdgeV2Size,
            "4.3: a wide (P2WSH/quantum) edge must serialize as the 81-byte v2 "
            "layout; got " + std::to_string(bytes.size()) + " (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(bytes[0] == CVMTrustEdgeData::VERSION_V2,
            "4.3: v2 payload must begin with the version byte (#" + std::to_string(i) + ")");

        CVMTrustEdgeData out;
        BOOST_REQUIRE_MESSAGE(out.Deserialize(bytes),
            "4.3: v2 payload must deserialize (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.from == in.from,
            "4.3: v2 `from` TrustNodeId did not round-trip (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.to == in.to,
            "4.3: v2 `to` TrustNodeId did not round-trip (#" + std::to_string(i) + ")");
        BOOST_CHECK_MESSAGE(out.weight == in.weight && out.bondAmount == in.bondAmount &&
                            out.timestamp == in.timestamp,
            "4.3: v2 scalar fields did not round-trip (#" + std::to_string(i) + ")");
    }
}

// ===========================================================================
// 4.7 — Quantum/Falcon parsing/encoding and TrustNodeId::FromDestination /
//       ToDestination / serialization / ToKeyString are unchanged for every
//       supported destination type. The unchanged TrustNodeId type is exactly
//       the identity representation the migration standardises on.
// ===========================================================================

// EncodeDestination/DecodeDestination round-trip every standard type and
// IsQuantumAddress recognises only quantum addresses.
// **Validates: Requirements 4.7**
BOOST_AUTO_TEST_CASE(preserve_address_encode_decode_roundtrip_property)
{
    const CChainParams& params = Params();

    for (int i = 0; i < kSamples; ++i) {
        const uint160 h160 = RandU160();
        const uint256 h256 = InsecureRand256();

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
            BOOST_REQUIRE_MESSAGE(!encoded.empty(),
                "4.7: EncodeDestination produced empty string for " + d.first +
                " (#" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(DecodeDestination(encoded) == d.second,
                "4.7: DecodeDestination(EncodeDestination(x)) != x for " + d.first +
                " (#" + std::to_string(i) + ")");

            const bool isQuantum = address::IsQuantumAddress(encoded, params);
            BOOST_CHECK_MESSAGE(isQuantum == (d.first == "QUANTUM"),
                "4.7: IsQuantumAddress mismatch for " + d.first + " (#" +
                std::to_string(i) + ")");
        }
    }
}

// TrustNodeId::FromDestination → ToDestination and serialize/deserialize
// preserve the exact type and data for all five supported types; ToKeyString
// has the stable `<type:02x>-<64 hex>` shape. **Validates: Requirements 4.7**
BOOST_AUTO_TEST_CASE(preserve_trustnodeid_destination_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        const uint160 h160 = RandU160();
        const uint256 h256 = InsecureRand256();

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
            TrustNodeId node;
            BOOST_REQUIRE_MESSAGE(TrustNodeId::FromDestination(d.second, node),
                "4.7: FromDestination must accept " + d.first + " (#" + std::to_string(i) + ")");

            // Destination round-trip.
            BOOST_CHECK_MESSAGE(node.ToDestination() == d.second,
                "4.7: ToDestination(FromDestination(x)) != x for " + d.first +
                " (#" + std::to_string(i) + ")");

            // Serialization round-trip preserves type and data exactly.
            const std::vector<uint8_t> bytes = DiskBytes(node);
            BOOST_CHECK_MESSAGE(bytes.size() == 33,
                "4.7: a serialized TrustNodeId must be 33 bytes (type + 32) for " +
                d.first + " (#" + std::to_string(i) + ")");
            CDataStream ss(bytes, SER_DISK, CLIENT_VERSION);
            TrustNodeId out;
            ss >> out;
            BOOST_CHECK_MESSAGE(out == node,
                "4.7: TrustNodeId serialization did not round-trip for " + d.first +
                " (#" + std::to_string(i) + ")");

            // ToKeyString shape: "<type:02x>-<64 lowercase hex>".
            const std::string key = node.ToKeyString();
            BOOST_CHECK_MESSAGE(key.size() == 2 + 1 + 64,
                "4.7: ToKeyString length changed for " + d.first + " (got \"" + key +
                "\", #" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(key[2] == '-',
                "4.7: ToKeyString separator changed for " + d.first + " (#" +
                std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// 4.5 — Vote-range validation (ReputationVoteTx::IsValid) is unchanged.
//       Only the identity representation changes in the migration; the scalar
//       validation results must stay the same.
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_vote_range_validation_property)
{
    for (int i = 0; i < kSamples; ++i) {
        // In-range, non-zero vote with a reason is valid.
        {
            ReputationVoteTx v;
            v.targetAddress = TrustNodeId::FromLegacyUint160(RandU160());
            int val = static_cast<int>(InsecureRandRange(200)) - 100; // [-100, 99]
            if (val == 0) val = 1;
            v.voteValue = val;
            v.reason = "ok";
            std::string err;
            BOOST_CHECK_MESSAGE(v.IsValid(err),
                "4.5: an in-range, non-zero vote with a reason must be valid "
                "(vote " + std::to_string(v.voteValue) + ", #" + std::to_string(i) +
                ", err=\"" + err + "\")");
        }
        // Out-of-range vote is rejected with the preserved message.
        {
            ReputationVoteTx v;
            v.targetAddress = TrustNodeId::FromLegacyUint160(RandU160());
            v.voteValue = (i % 2 == 0) ? (101 + static_cast<int>(InsecureRandRange(100)))
                                       : -(101 + static_cast<int>(InsecureRandRange(100)));
            v.reason = "bad";
            std::string err;
            BOOST_CHECK_MESSAGE(!v.IsValid(err),
                "4.5: an out-of-range vote must be rejected (vote " +
                std::to_string(v.voteValue) + ", #" + std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(err.find("Vote value must be between -100 and 100") != std::string::npos,
                "4.5: out-of-range rejection message changed; got \"" + err + "\"");
        }
        // Zero vote is rejected (unchanged boundary).
        {
            ReputationVoteTx v;
            v.targetAddress = TrustNodeId::FromLegacyUint160(RandU160());
            v.voteValue = 0;
            v.reason = "zero";
            std::string err;
            BOOST_CHECK_MESSAGE(!v.IsValid(err),
                "4.5: a zero vote must be rejected (#" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// 4.6 — Common-input-ownership clustering heuristic is unchanged.
//
// Addresses used together as inputs to the same transaction are grouped under a
// single cluster root; addresses from unrelated transactions are not linked. We
// drive the REAL WalletClusterer via its persisted transaction/address index
// (RecordTransactionInputs → BuildClusters) so the genuine union-find heuristic
// runs. We observe the grouping through the cluster root
// (GetClusterForAddress), which is the stable externally-visible outcome of the
// heuristic. The migration changes only identity extraction/storage/rendering,
// not this grouping semantics.
// **Validates: Requirements 4.6**
// ===========================================================================
BOOST_AUTO_TEST_CASE(preserve_clustering_common_input_ownership_property)
{
    for (int i = 0; i < kSamplesDb; ++i) {
        // Fresh DB per sample keeps the union-find state isolated.
        CVM::g_cvmdb.reset(new CVM::CVMDatabase(
            fs::temp_directory_path() / fs::unique_path(),
            1 << 20, /*fMemory=*/true, /*fWipe=*/true));

        WalletClusterer clusterer(*CVM::g_cvmdb);

        // Two independent transactions, each with a set of common-input
        // addresses. Members of the same transaction must resolve to the same
        // cluster root; members of different transactions must NOT.
        std::vector<uint160> txA;
        std::vector<uint160> txB;
        const int nA = 2 + static_cast<int>(InsecureRandRange(3)); // [2,4]
        const int nB = 2 + static_cast<int>(InsecureRandRange(3)); // [2,4]
        for (int k = 0; k < nA; ++k) txA.push_back(RandU160());
        for (int k = 0; k < nB; ++k) txB.push_back(RandU160());

        const uint256 txidA = InsecureRand256();
        const uint256 txidB = InsecureRand256();
        clusterer.RecordTransactionInputs(txidA, txA);
        clusterer.RecordTransactionInputs(txidB, txB);

        clusterer.BuildClusters();

        // Common-input heuristic: every input of txA resolves to the same
        // cluster root as txA[0].
        const uint160 rootA = clusterer.GetClusterForAddress(txA[0]);
        for (const uint160& addr : txA) {
            BOOST_CHECK_MESSAGE(clusterer.GetClusterForAddress(addr) == rootA,
                "4.6: common-input address " + addr.ToString() +
                " must resolve to the same cluster root as txA[0] (#" +
                std::to_string(i) + ")");
        }

        // Independent transaction: txB inputs resolve to a different root and so
        // are not linked to txA (no false cross-transaction merge).
        const uint160 rootB = clusterer.GetClusterForAddress(txB[0]);
        BOOST_CHECK_MESSAGE(rootB != rootA,
            "4.6: an unrelated transaction's inputs must NOT be merged with txA "
            "(#" + std::to_string(i) + ")");
        for (const uint160& addr : txB) {
            BOOST_CHECK_MESSAGE(clusterer.GetClusterForAddress(addr) == rootB,
                "4.6: common-input address " + addr.ToString() +
                " must resolve to the same cluster root as txB[0] (#" +
                std::to_string(i) + ")");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
