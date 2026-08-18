// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/trustnodeid.h>

#include <pubkey.h>
#include <tinyformat.h>

#include <cstring>

namespace CVM {

namespace {

//! Zero-extend a 20-byte uint160 into the low 20 bytes of a 32-byte uint256.
//! The remaining high bytes are left zero. Internal byte order is preserved.
uint256 ExtendUint160(const uint160& in)
{
    uint256 out; // default-constructed to all zeros
    std::memcpy(out.begin(), in.begin(), in.size());
    return out;
}

//! Recover the low-20-byte uint160 previously embedded by ExtendUint160.
uint160 TruncateToUint160(const uint256& in)
{
    uint160 out;
    std::memcpy(out.begin(), in.begin(), out.size());
    return out;
}

} // anonymous namespace

bool TrustNodeId::FromDestination(const CTxDestination& dest, TrustNodeId& out)
{
    // uint160-based destinations: zero-extend into the low 20 bytes.
    if (const CKeyID* id = boost::get<CKeyID>(&dest)) {
        out.type = static_cast<uint8_t>(TrustNodeType::P2PKH);
        out.data = ExtendUint160(*id);
        return true;
    }
    if (const CScriptID* id = boost::get<CScriptID>(&dest)) {
        out.type = static_cast<uint8_t>(TrustNodeType::P2SH);
        out.data = ExtendUint160(*id);
        return true;
    }
    if (const WitnessV0KeyHash* id = boost::get<WitnessV0KeyHash>(&dest)) {
        out.type = static_cast<uint8_t>(TrustNodeType::P2WPKH);
        out.data = ExtendUint160(*id);
        return true;
    }

    // uint256-based destinations: use the full 32 bytes.
    if (const WitnessV0ScriptHash* id = boost::get<WitnessV0ScriptHash>(&dest)) {
        out.type = static_cast<uint8_t>(TrustNodeType::P2WSH);
        out.data = *id;
        return true;
    }
    if (const WitnessV2Quantum* id = boost::get<WitnessV2Quantum>(&dest)) {
        out.type = static_cast<uint8_t>(TrustNodeType::QUANTUM);
        out.data = *id;
        return true;
    }

    // CNoDestination, WitnessUnknown, and anything else are unsupported.
    return false;
}

TrustNodeId TrustNodeId::FromLegacyUint160(const uint160& id)
{
    // Legacy trust edges keyed all destinations as bare uint160 P2PKH values.
    // Zero-extend into the low 20 bytes, exactly as FromDestination does for
    // uint160 destinations, so migrated records key/round-trip consistently.
    return TrustNodeId(TrustNodeType::P2PKH, ExtendUint160(id));
}

uint160 TrustNodeId::ToUint160() const
{
    // Recover the low-20-byte value. For uint160-based types this is the exact
    // original identifier; for 32-byte types it is a lossy truncation used only
    // by legacy uint160-only consumers.
    return TruncateToUint160(data);
}

CTxDestination TrustNodeId::ToDestination() const
{
    switch (static_cast<TrustNodeType>(type)) {
    case TrustNodeType::P2PKH:
        return CKeyID(TruncateToUint160(data));
    case TrustNodeType::P2SH:
        return CScriptID(TruncateToUint160(data));
    case TrustNodeType::P2WPKH:
        return WitnessV0KeyHash(TruncateToUint160(data));
    case TrustNodeType::P2WSH:
        return WitnessV0ScriptHash(data);
    case TrustNodeType::QUANTUM:
        return WitnessV2Quantum(data);
    }
    // Unknown type tag: not representable as a destination.
    return CNoDestination();
}

std::string TrustNodeId::ToKeyString() const
{
    // "<type:02x>-<data:64hex>" — the type tag and '-' separator guarantee the
    // segment never collides with a legacy 40-hex uint160 key segment.
    return strprintf("%02x-%s", type, data.GetHex());
}

bool ValidateCanonicalTrustNode(const TrustNodeId& node, std::string& err)
{
    // 1) Type tag must be one of the stable values 1..5.
    if (node.type < static_cast<uint8_t>(TrustNodeType::P2PKH) ||
        node.type > static_cast<uint8_t>(TrustNodeType::QUANTUM)) {
        err = strprintf("unsupported trust-node type tag %d", static_cast<int>(node.type));
        return false;
    }

    const TrustNodeType t = static_cast<TrustNodeType>(node.type);
    const bool is20ByteType = (t == TrustNodeType::P2PKH ||
                               t == TrustNodeType::P2SH ||
                               t == TrustNodeType::P2WPKH);

    // 2) For 20-byte destination types the high 12 bytes (internal byte order,
    //    indices 20..31) MUST be zero. Nonzero high padding is noncanonical and
    //    is never silently truncated.
    if (is20ByteType) {
        for (size_t i = 20; i < 32; ++i) {
            if (*(node.data.begin() + i) != 0) {
                err = "noncanonical 20-byte identity: nonzero high padding";
                return false;
            }
        }
    }
    // For P2WSH/QUANTUM all 32 bytes are significant; no padding constraint.

    // 3) ToDestination() must yield a supported destination that maps back to
    //    the exact same identity. This guards against any width rejection or
    //    type inference and confirms the value is representable end to end.
    CTxDestination dest = node.ToDestination();
    TrustNodeId roundtrip;
    if (!TrustNodeId::FromDestination(dest, roundtrip)) {
        err = "trust-node does not map to a supported destination";
        return false;
    }
    if (roundtrip != node) {
        err = "trust-node destination round-trip mismatch";
        return false;
    }

    err.clear();
    return true;
}

bool ParseKeyStringToTrustNode(const std::string& key, TrustNodeId& out, std::string& err)
{
    // Canonical shape is exactly "<type:02x>-<data:64 lowercase hex>", i.e.
    // 2 + 1 + 64 = 67 characters with the single '-' separator at index 2.
    static const size_t kCanonicalLen = 67;
    if (key.size() != kCanonicalLen) {
        err = strprintf("invalid key length %d (expected %d)",
                        static_cast<int>(key.size()), static_cast<int>(kCanonicalLen));
        return false;
    }
    if (key[2] != '-') {
        err = "missing '-' separator at position 2";
        return false;
    }

    // Every character other than the single separator MUST be a lowercase hex
    // digit. This rejects uppercase hex, extra separators, and any trailing or
    // embedded noncanonical character.
    auto is_lower_hex = [](char c) -> bool {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    };
    for (size_t i = 0; i < key.size(); ++i) {
        if (i == 2) continue; // the one canonical separator
        if (!is_lower_hex(key[i])) {
            err = strprintf("noncanonical character at position %d", static_cast<int>(i));
            return false;
        }
    }

    // Decode the 1-byte type tag from the two leading lowercase hex digits.
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        return 10 + (c - 'a');
    };
    const int type = hex_val(key[0]) * 16 + hex_val(key[1]);

    // Decode the 32-byte data segment. SetHex consumes the 64 hex characters we
    // already validated as lowercase hex.
    uint256 data;
    data.SetHex(key.substr(3));

    out.type = static_cast<uint8_t>(type);
    out.data = data;

    // Re-emit the canonical key and require an exact match. Because ToKeyString
    // always produces the lowercase "<type:02x>-<64hex>" form, this rejects any
    // residual noncanonical encoding that survived the character scan.
    if (out.ToKeyString() != key) {
        err = "noncanonical trust-node key string";
        return false;
    }

    // Finally enforce the full canonical identity acceptance rules (type range
    // and high-byte padding for the 20-byte types).
    if (!ValidateCanonicalTrustNode(out, err)) {
        return false;
    }

    err.clear();
    return true;
}

} // namespace CVM
