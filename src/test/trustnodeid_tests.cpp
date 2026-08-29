// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TrustNodeId — Lossless Round-Trip Test Suite
 *
 * Spec: .kiro/specs/web-of-trust-fixes  (bugfix)
 * Task 6.2: "Unit test TrustNodeId round-trip for all five destination types"
 *
 * PURPOSE
 * -------
 * Change B1 introduces the canonical wide `CVM::TrustNodeId` type so that every
 * standard Cascoin destination (P2PKH, P2SH, P2WPKH, P2WSH, quantum) can be
 * represented without truncation. These tests lock in that behaviour:
 *
 *   Property 3 (Bug Condition — TrustNodeId Lossless Round-Trip):
 *     For each of CKeyID, CScriptID, WitnessV0KeyHash, WitnessV0ScriptHash and
 *     WitnessV2Quantum, `TrustNodeId::FromDestination` followed by
 *     `ToDestination` returns the ORIGINAL destination, `ToKeyString` is stable
 *     and collision-free across types, and a serialize/deserialize cycle
 *     reproduces the identical TrustNodeId (including full-width / quantum
 *     values, i.e. no LE/BE reversal or high-byte truncation).
 *
 * Since TrustNodeId is already implemented (Task 6.1), these tests are expected
 * to PASS. Both concrete example cases and property-based (randomised) cases are
 * included.
 *
 * Requirements: 2.10, 2.12, 3.10
 */

#include <cvm/trustnodeid.h>

#include <pubkey.h>
#include <script/standard.h>
#include <streams.h>
#include <uint256.h>
#include <version.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace CVM;

namespace {

// Number of random samples for the property-based cases.
static constexpr int kSamples = 512;

//! Build a uint160 from the low 20 bytes of a random uint256.
uint160 RandUint160()
{
    uint256 r = InsecureRand256();
    uint160 out;
    std::memcpy(out.begin(), r.begin(), out.size());
    return out;
}

//! Zero-extend a uint160 into the low 20 bytes of a uint256 (mirrors the
//! internal ExtendUint160 helper used by TrustNodeId::FromDestination).
uint256 Extend160(const uint160& in)
{
    uint256 out;
    std::memcpy(out.begin(), in.begin(), in.size());
    return out;
}

//! Build a destination of the given TrustNodeType from a random value. uint160
//! types get a random 20-byte value; uint256 types get a full random 32-byte
//! value (so the high 12 bytes exercise the wide/no-truncation path).
CTxDestination MakeRandomDestination(TrustNodeType t)
{
    switch (t) {
    case TrustNodeType::P2PKH:  return CKeyID(RandUint160());
    case TrustNodeType::P2SH:   return CScriptID(RandUint160());
    case TrustNodeType::P2WPKH: return WitnessV0KeyHash(RandUint160());
    case TrustNodeType::P2WSH:  return WitnessV0ScriptHash(InsecureRand256());
    case TrustNodeType::QUANTUM:return WitnessV2Quantum(InsecureRand256());
    }
    return CNoDestination();
}

// All five supported types, in a stable list for iteration.
const std::vector<TrustNodeType> kAllTypes = {
    TrustNodeType::P2PKH,
    TrustNodeType::P2SH,
    TrustNodeType::P2WPKH,
    TrustNodeType::P2WSH,
    TrustNodeType::QUANTUM,
};

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(trustnodeid_tests, BasicTestingSetup)

// ===========================================================================
// FromDestination -> ToDestination is the identity for every supported type
// (property-based, random values). Also asserts the resolved type tag matches
// the source destination kind.                                  (2.10, 2.12)
// ===========================================================================
BOOST_AUTO_TEST_CASE(from_to_destination_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        for (TrustNodeType t : kAllTypes) {
            CTxDestination dest = MakeRandomDestination(t);

            TrustNodeId id;
            BOOST_REQUIRE_MESSAGE(TrustNodeId::FromDestination(dest, id),
                "FromDestination must accept a valid standard destination "
                "(type=" + std::to_string(static_cast<int>(t)) +
                ", sample #" + std::to_string(i) + ")");

            BOOST_CHECK_MESSAGE(id.type == static_cast<uint8_t>(t),
                "Resolved type tag mismatch (expected " +
                std::to_string(static_cast<int>(t)) + ", got " +
                std::to_string(id.type) + ", sample #" + std::to_string(i) + ")");

            CTxDestination back = id.ToDestination();
            BOOST_CHECK_MESSAGE(back == dest,
                "FromDestination->ToDestination is not the identity "
                "(type=" + std::to_string(static_cast<int>(t)) +
                ", sample #" + std::to_string(i) + ")");
        }
    }
}

// ===========================================================================
// Concrete, human-readable example for each type using a fixed value, so a
// regression is easy to eyeball. Also covers uint160 zero-extension: the low 20
// bytes carry the value and the identity holds.                 (2.12, 3.10)
// ===========================================================================
BOOST_AUTO_TEST_CASE(from_to_destination_roundtrip_examples)
{
    // A fixed 32-byte pattern; uint160 destinations use its low 20 bytes.
    uint256 v256 = uint256S(
        "0102030405060708090a0b0c0d0e0f101112131415161718"
        "191a1b1c1d1e1f20");
    uint160 v160;
    std::memcpy(v160.begin(), v256.begin(), v160.size());

    // P2PKH
    {
        CTxDestination dest = CKeyID(v160);
        TrustNodeId id;
        BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
        BOOST_CHECK_EQUAL(id.type, static_cast<uint8_t>(TrustNodeType::P2PKH));
        BOOST_CHECK(id.ToDestination() == dest);
    }
    // P2SH
    {
        CTxDestination dest = CScriptID(v160);
        TrustNodeId id;
        BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
        BOOST_CHECK_EQUAL(id.type, static_cast<uint8_t>(TrustNodeType::P2SH));
        BOOST_CHECK(id.ToDestination() == dest);
    }
    // P2WPKH
    {
        CTxDestination dest = WitnessV0KeyHash(v160);
        TrustNodeId id;
        BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
        BOOST_CHECK_EQUAL(id.type, static_cast<uint8_t>(TrustNodeType::P2WPKH));
        BOOST_CHECK(id.ToDestination() == dest);
    }
    // P2WSH (full 32 bytes)
    {
        CTxDestination dest = WitnessV0ScriptHash(v256);
        TrustNodeId id;
        BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
        BOOST_CHECK_EQUAL(id.type, static_cast<uint8_t>(TrustNodeType::P2WSH));
        BOOST_CHECK(id.data == v256);
        BOOST_CHECK(id.ToDestination() == dest);
    }
    // QUANTUM (full 32 bytes)
    {
        CTxDestination dest = WitnessV2Quantum(v256);
        TrustNodeId id;
        BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
        BOOST_CHECK_EQUAL(id.type, static_cast<uint8_t>(TrustNodeType::QUANTUM));
        BOOST_CHECK(id.data == v256);
        BOOST_CHECK(id.ToDestination() == dest);
    }
}

// ===========================================================================
// Unsupported destinations are rejected (CNoDestination, WitnessUnknown).
// ===========================================================================
BOOST_AUTO_TEST_CASE(from_destination_rejects_unsupported)
{
    TrustNodeId id;

    CTxDestination none = CNoDestination();
    BOOST_CHECK(!TrustNodeId::FromDestination(none, id));

    WitnessUnknown wu;
    wu.version = 3;
    wu.length = 2;
    wu.program[0] = 0xab;
    wu.program[1] = 0xcd;
    CTxDestination unknown = wu;
    BOOST_CHECK(!TrustNodeId::FromDestination(unknown, id));
}

// ===========================================================================
// ToKeyString is stable (deterministic) for a given id, carries the tagged
// "<type:02x>-<64hex>" shape, and never collides with a legacy 40-hex uint160
// segment.                                                            (3.10)
// ===========================================================================
BOOST_AUTO_TEST_CASE(tokeystring_stable_and_tagged)
{
    for (int i = 0; i < kSamples; ++i) {
        for (TrustNodeType t : kAllTypes) {
            CTxDestination dest = MakeRandomDestination(t);
            TrustNodeId id;
            BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));

            std::string k1 = id.ToKeyString();
            std::string k2 = id.ToKeyString();
            BOOST_CHECK_MESSAGE(k1 == k2,
                "ToKeyString must be deterministic (sample #" +
                std::to_string(i) + ")");

            // Shape: 2 hex type digits + '-' + 64 hex data digits = 67 chars.
            BOOST_CHECK_MESSAGE(k1.size() == 67,
                "ToKeyString has unexpected length " + std::to_string(k1.size()) +
                " (\"" + k1 + "\")");
            BOOST_CHECK_MESSAGE(k1[2] == '-',
                "ToKeyString must embed the '-' separator at index 2 (\"" +
                k1 + "\")");

            // A legacy uint160 key segment is 40 hex chars with no separator;
            // the tagged key must never look like one.
            BOOST_CHECK_MESSAGE(k1.size() != 40 &&
                                k1.find('-') != std::string::npos,
                "ToKeyString must not collide with a legacy 40-hex segment (\"" +
                k1 + "\")");
        }
    }
}

// ===========================================================================
// ToKeyString is collision-free across types: the SAME underlying 32-byte
// value assigned to different destination types produces distinct key strings
// (the type tag disambiguates P2PKH/P2SH/P2WPKH, which share the 20-byte hash).
// ===========================================================================
BOOST_AUTO_TEST_CASE(tokeystring_collision_free_across_types)
{
    for (int i = 0; i < kSamples; ++i) {
        uint256 v256 = InsecureRand256();
        uint160 v160;
        std::memcpy(v160.begin(), v256.begin(), v160.size());

        uint256 ext160 = Extend160(v160);
        std::set<std::string> keys;
        std::vector<TrustNodeId> ids = {
            TrustNodeId(TrustNodeType::P2PKH,   ext160),
            TrustNodeId(TrustNodeType::P2SH,    ext160),
            TrustNodeId(TrustNodeType::P2WPKH,  ext160),
            TrustNodeId(TrustNodeType::P2WSH,   v256),
            TrustNodeId(TrustNodeType::QUANTUM, v256),
        };

        for (const TrustNodeId& id : ids) {
            bool inserted = keys.insert(id.ToKeyString()).second;
            BOOST_CHECK_MESSAGE(inserted,
                "ToKeyString collision across types for value " + v256.GetHex() +
                " (sample #" + std::to_string(i) + ")");
        }
        BOOST_CHECK_EQUAL(keys.size(), ids.size());
    }
}

// ===========================================================================
// Serialization round-trip: serialize a TrustNodeId then deserialize it and
// assert equality. Exercises all five types with full-width values so the
// quantum/P2WSH high bytes (LE/BE handling) survive intact.      (2.12, 3.10)
// ===========================================================================
BOOST_AUTO_TEST_CASE(serialization_roundtrip_property)
{
    for (int i = 0; i < kSamples; ++i) {
        for (TrustNodeType t : kAllTypes) {
            CTxDestination dest = MakeRandomDestination(t);
            TrustNodeId id;
            BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));

            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << id;

            // Wire layout is exactly 1 type byte + 32 data bytes.
            BOOST_CHECK_MESSAGE(ss.size() == 33,
                "Unexpected serialized size " + std::to_string(ss.size()) +
                " (type=" + std::to_string(static_cast<int>(t)) + ")");

            TrustNodeId decoded;
            ss >> decoded;

            BOOST_CHECK_MESSAGE(decoded == id,
                "Serialization round-trip mismatch (type=" +
                std::to_string(static_cast<int>(t)) + ", sample #" +
                std::to_string(i) + ")");
            BOOST_CHECK(decoded.type == id.type);
            BOOST_CHECK(decoded.data == id.data);

            // The decoded id must still rebuild the original destination.
            BOOST_CHECK(decoded.ToDestination() == dest);
        }
    }
}

// ===========================================================================
// Explicit quantum wide-value / byte-order preservation: a quantum value whose
// high bytes are non-zero must survive FromDestination, serialization and
// ToDestination without truncation or reversal.                  (2.12, 3.10)
// ===========================================================================
BOOST_AUTO_TEST_CASE(quantum_wide_value_preserved)
{
    // High bytes deliberately non-zero to catch any 20-byte truncation and any
    // accidental endianness flip.
    uint256 v = uint256S(
        "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0"
        "0f0e0d0c0b0a09080706050403020100");

    CTxDestination dest = WitnessV2Quantum(v);
    TrustNodeId id;
    BOOST_REQUIRE(TrustNodeId::FromDestination(dest, id));
    BOOST_CHECK(id.data == v); // stored in internal byte order, no reversal

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << id;
    TrustNodeId decoded;
    ss >> decoded;

    BOOST_CHECK(decoded.data == v);
    CTxDestination back = decoded.ToDestination();
    BOOST_CHECK(back == dest);

    const WitnessV2Quantum* q = boost::get<WitnessV2Quantum>(&back);
    BOOST_REQUIRE(q != nullptr);
    BOOST_CHECK(static_cast<const uint256&>(*q) == v);
}

BOOST_AUTO_TEST_SUITE_END()
