// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TrustNodeId strict validation helpers — unit tests
 *
 * Spec: .kiro/specs/trustnodeid-full-migration  (bugfix)
 * Task 3.1 (Wave 1): "Identity validation helper and strict acceptance rules"
 *
 * PURPOSE
 * -------
 * Wave 1 adds two additive helpers alongside the unchanged CVM::TrustNodeId
 * type:
 *
 *   bool ValidateCanonicalTrustNode(const TrustNodeId&, std::string& err);
 *   bool ParseKeyStringToTrustNode(const std::string&, TrustNodeId&, std::string& err);
 *
 * These lock in the single canonical-acceptance gate reused at every migrated
 * downstream call site:
 *   - type is one of the stable tags 1..5;
 *   - P2PKH/P2SH/P2WPKH have zero high 12 bytes;
 *   - P2WSH/quantum retain all 32 bytes;
 *   - ToDestination() maps to a supported destination and round-trips;
 *   - textual key input exactly matches the lowercase ToKeyString() shape
 *     "<type:02x>-<data:64 lowercase hex>", rejecting unknown types,
 *     noncanonical hex, extra separators, trailing characters, and nonzero
 *     high padding for the 20-byte types.
 *
 * Requirements: 2.11, 2.12, 5.1
 */

#include <cvm/trustnodeid.h>

#include <pubkey.h>
#include <script/standard.h>
#include <uint256.h>

#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace CVM;

namespace {

//! Zero-extend a uint160 into the low 20 bytes of a uint256 (mirrors the
//! internal ExtendUint160 helper used by TrustNodeId::FromDestination).
uint256 Extend160(const uint160& in)
{
    uint256 out;
    std::memcpy(out.begin(), in.begin(), in.size());
    return out;
}

const std::vector<TrustNodeType> kAllTypes = {
    TrustNodeType::P2PKH,
    TrustNodeType::P2SH,
    TrustNodeType::P2WPKH,
    TrustNodeType::P2WSH,
    TrustNodeType::QUANTUM,
};

//! Build a canonical identity of the given type from a fixed 32-byte pattern.
//! For the 20-byte types only the low 20 bytes are used (high bytes zero).
TrustNodeId MakeCanonical(TrustNodeType t, const uint256& v256)
{
    switch (t) {
    case TrustNodeType::P2PKH:
    case TrustNodeType::P2SH:
    case TrustNodeType::P2WPKH: {
        uint160 v160;
        std::memcpy(v160.begin(), v256.begin(), v160.size());
        return TrustNodeId(t, Extend160(v160));
    }
    case TrustNodeType::P2WSH:
    case TrustNodeType::QUANTUM:
        return TrustNodeId(t, v256);
    }
    return TrustNodeId();
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(trustnodeid_validation_tests, BasicTestingSetup)

// ===========================================================================
// ValidateCanonicalTrustNode ACCEPTS all five canonical destination types.
// ===========================================================================
BOOST_AUTO_TEST_CASE(validate_accepts_all_five_canonical_types)
{
    const uint256 v = uint256S(
        "fffefdfcfbfaf9f8f7f6f5f4f3f2f1f0"
        "0f0e0d0c0b0a09080706050403020100");

    for (TrustNodeType t : kAllTypes) {
        TrustNodeId node = MakeCanonical(t, v);
        std::string err = "sentinel";
        BOOST_CHECK_MESSAGE(ValidateCanonicalTrustNode(node, err),
            "canonical identity rejected (type=" +
            std::to_string(static_cast<int>(t)) + "): " + err);
        BOOST_CHECK_MESSAGE(err.empty(),
            "err must be cleared on success (type=" +
            std::to_string(static_cast<int>(t)) + ")");
    }
}

// ===========================================================================
// Unknown / out-of-range type tags are REJECTED.
// ===========================================================================
BOOST_AUTO_TEST_CASE(validate_rejects_unknown_types)
{
    const uint256 v = uint256S(
        "0102030405060708090a0b0c0d0e0f10"
        "1112131415161718191a1b1c1d1e1f20");

    for (uint8_t tag : {uint8_t(0), uint8_t(6), uint8_t(7), uint8_t(255)}) {
        TrustNodeId node;
        node.type = tag;
        node.data = v;
        std::string err;
        BOOST_CHECK_MESSAGE(!ValidateCanonicalTrustNode(node, err),
            "unknown type tag " + std::to_string(int(tag)) + " must be rejected");
        BOOST_CHECK(!err.empty());
    }
}

// ===========================================================================
// Nonzero high padding for the 20-byte types is REJECTED, while the same
// low-20-byte value with zero high padding is ACCEPTED.
// ===========================================================================
BOOST_AUTO_TEST_CASE(validate_rejects_nonzero_high_padding_for_20byte_types)
{
    // High bytes (indices 20..31) are deliberately non-zero.
    const uint256 highSet = uint256S(
        "0000000000000000000000000000000000000000"  // low 20 bytes (display high)
        "0102030405060708090a0b0c");                 // high 12 bytes

    for (TrustNodeType t : {TrustNodeType::P2PKH, TrustNodeType::P2SH, TrustNodeType::P2WPKH}) {
        // Craft data whose internal high 12 bytes are non-zero.
        uint256 data;
        for (int i = 0; i < 32; ++i) {
            // low 20 bytes zero, high 12 bytes non-zero (internal order)
            *(data.begin() + i) = (i >= 20) ? static_cast<unsigned char>(i) : 0;
        }
        TrustNodeId bad(t, data);
        std::string err;
        BOOST_CHECK_MESSAGE(!ValidateCanonicalTrustNode(bad, err),
            "nonzero high padding must be rejected (type=" +
            std::to_string(static_cast<int>(t)) + ")");
        BOOST_CHECK(!err.empty());

        // The canonical form (high bytes zero) with the same tag is accepted.
        uint256 good; // all zero
        TrustNodeId ok(t, good);
        std::string err2 = "x";
        BOOST_CHECK(ValidateCanonicalTrustNode(ok, err2));
        BOOST_CHECK(err2.empty());
    }
    (void)highSet;
}

// ===========================================================================
// P2WSH / QUANTUM retain all 32 bytes: a value with non-zero high bytes is
// accepted (no padding constraint applies to the 32-byte types).
// ===========================================================================
BOOST_AUTO_TEST_CASE(validate_accepts_full_width_for_32byte_types)
{
    uint256 data;
    for (int i = 0; i < 32; ++i) *(data.begin() + i) = static_cast<unsigned char>(0xA0 + i);

    for (TrustNodeType t : {TrustNodeType::P2WSH, TrustNodeType::QUANTUM}) {
        TrustNodeId node(t, data);
        std::string err = "x";
        BOOST_CHECK_MESSAGE(ValidateCanonicalTrustNode(node, err),
            "full-width 32-byte identity must be accepted (type=" +
            std::to_string(static_cast<int>(t)) + "): " + err);
        BOOST_CHECK(err.empty());
    }
}

// ===========================================================================
// ParseKeyStringToTrustNode round-trips the canonical ToKeyString() form for
// all five types and recovers the exact identity.
// ===========================================================================
BOOST_AUTO_TEST_CASE(parse_roundtrips_canonical_keystring)
{
    const uint256 v = uint256S(
        "112233445566778899aabbccddeeff00"
        "0011223344556677889900aabbccddee");

    for (TrustNodeType t : kAllTypes) {
        TrustNodeId node = MakeCanonical(t, v);
        const std::string key = node.ToKeyString();

        TrustNodeId parsed;
        std::string err = "sentinel";
        BOOST_CHECK_MESSAGE(ParseKeyStringToTrustNode(key, parsed, err),
            "canonical key rejected (type=" +
            std::to_string(static_cast<int>(t)) + "): " + err);
        BOOST_CHECK(err.empty());
        BOOST_CHECK_MESSAGE(parsed == node,
            "parsed identity mismatch (type=" +
            std::to_string(static_cast<int>(t)) + ")");
        BOOST_CHECK_EQUAL(parsed.ToKeyString(), key);
    }
}

// ===========================================================================
// ParseKeyStringToTrustNode rejects noncanonical inputs: uppercase hex,
// unknown type tag, extra separators, trailing characters, wrong length, and
// nonzero high padding for a 20-byte type.
// ===========================================================================
BOOST_AUTO_TEST_CASE(parse_rejects_noncanonical_inputs)
{
    // A valid canonical P2PKH key to mutate.
    uint160 v160;
    for (int i = 0; i < 20; ++i) *(v160.begin() + i) = static_cast<unsigned char>(i + 1);
    TrustNodeId base(TrustNodeType::P2PKH, Extend160(v160));
    const std::string good = base.ToKeyString();
    BOOST_REQUIRE_EQUAL(good.size(), 67u);

    auto rejects = [](const std::string& k) {
        TrustNodeId out;
        std::string err;
        bool ok = ParseKeyStringToTrustNode(k, out, err);
        BOOST_CHECK_MESSAGE(!ok, "expected rejection for key: \"" + k + "\"");
        if (!ok) BOOST_CHECK_MESSAGE(!err.empty(), "rejection must set err for: \"" + k + "\"");
        return !ok;
    };

    // Sanity: the unmutated key is accepted.
    {
        TrustNodeId out; std::string err;
        BOOST_CHECK(ParseKeyStringToTrustNode(good, out, err));
    }

    // Uppercase hex in the data segment.
    {
        std::string k = good;
        for (char& c : k) if (c >= 'a' && c <= 'f') { c = static_cast<char>(c - 'a' + 'A'); break; }
        rejects(k);
    }
    // Uppercase hex in the type segment ("01" -> "0A" style is length-safe;
    // here force a capital letter if present, else inject one).
    {
        std::string k = good;
        k[1] = 'A';       // noncanonical uppercase in type nibble
        rejects(k);
    }
    // Unknown type tag "00".
    {
        std::string k = good;
        k[0] = '0'; k[1] = '0';
        rejects(k);
    }
    // Unknown type tag "06".
    {
        std::string k = good;
        k[0] = '0'; k[1] = '6';
        rejects(k);
    }
    // Missing separator (replace '-' with a hex digit).
    {
        std::string k = good;
        k[2] = '0';
        rejects(k);
    }
    // Extra separator inside the data segment.
    {
        std::string k = good;
        k[10] = '-';
        rejects(k);
    }
    // Trailing character (length 68).
    {
        std::string k = good + "0";
        rejects(k);
    }
    // Truncated (length 66).
    {
        std::string k = good.substr(0, good.size() - 1);
        rejects(k);
    }
    // Empty string.
    rejects("");

    // Nonzero high padding for a 20-byte type: build a QUANTUM key (full width),
    // then relabel it as P2PKH so the high 12 bytes are non-zero.
    {
        uint256 data;
        for (int i = 0; i < 32; ++i) *(data.begin() + i) = static_cast<unsigned char>(i + 1);
        TrustNodeId quantum(TrustNodeType::QUANTUM, data);
        std::string k = quantum.ToKeyString();
        k[0] = '0'; k[1] = '1'; // relabel type as P2PKH; high padding now nonzero
        rejects(k);
    }
}

BOOST_AUTO_TEST_SUITE_END()
