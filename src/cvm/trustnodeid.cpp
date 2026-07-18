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

} // namespace CVM
