// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_TRUSTNODEID_H
#define CASCOIN_CVM_TRUSTNODEID_H

#include <uint256.h>
#include <serialize.h>
#include <script/standard.h>

#include <string>

namespace CVM {

/**
 * Type tag describing which standard Cascoin destination a TrustNodeId
 * represents. The tag makes the stored identifier losslessly reversible to
 * the correct CTxDestination for display: P2PKH, P2SH and P2WPKH all collapse
 * to the same 20 bytes otherwise.
 *
 * Values are stable and MUST NOT be renumbered: they are persisted on disk
 * and (via CVMTrustEdgeData v2) on-chain.
 */
enum class TrustNodeType : uint8_t {
    P2PKH   = 1,   // CKeyID              (uint160, low 20 bytes)
    P2SH    = 2,   // CScriptID           (uint160, low 20 bytes)
    P2WPKH  = 3,   // WitnessV0KeyHash    (uint160, low 20 bytes)
    P2WSH   = 4,   // WitnessV0ScriptHash (uint256, full 32 bytes)
    QUANTUM = 5,   // WitnessV2Quantum    (uint256, full 32 bytes)
};

/**
 * Canonical, lossless trust-node identifier.
 *
 * A TrustNodeId is a 1-byte destination-type tag plus a 32-byte value. It can
 * represent every standard Cascoin destination without truncation:
 *   - CKeyID / CScriptID / WitnessV0KeyHash are uint160 (20 bytes) and are
 *     zero-extended into the low 20 bytes of `data`.
 *   - WitnessV0ScriptHash / WitnessV2Quantum are uint256 (32 bytes) and use
 *     the full `data`.
 *
 * The byte order stored in `data` is the internal (little-endian) byte order
 * of the underlying uint160/uint256, exactly as produced by DecodeDestination.
 * No LE/BE reversal is performed here; the bech32/bech32m reversal lives in
 * base58.cpp and only applies to the string address encoding. This keeps the
 * identifier consistent on write and on read for every supported type.
 */
struct TrustNodeId {
    uint8_t type;      // A TrustNodeType value
    uint256 data;      // 32-byte value; uint160 types zero-extended in low 20 bytes

    TrustNodeId() : type(0) {}
    TrustNodeId(TrustNodeType t, const uint256& d) : type(static_cast<uint8_t>(t)), data(d) {}

    /**
     * Build a TrustNodeId from a decoded destination.
     *
     * @param dest A destination previously returned by DecodeDestination.
     * @param out  Populated on success.
     * @return false for CNoDestination and any unsupported destination type
     *         (e.g. WitnessUnknown); true otherwise.
     */
    static bool FromDestination(const CTxDestination& dest, TrustNodeId& out);

    /**
     * Build a P2PKH TrustNodeId from a legacy 20-byte uint160 value.
     *
     * The 20-byte value is zero-extended into the low 20 bytes of `data`
     * (matching FromDestination for uint160 destinations). This is used to
     * migrate legacy v1 records and legacy 40-hex DB key segments to the wide
     * TrustNodeId representation on read.
     *
     * @param id A legacy 20-byte identifier (e.g. a CKeyID value).
     * @return a TrustNodeId with type P2PKH holding the zero-extended value.
     */
    static TrustNodeId FromLegacyUint160(const uint160& id);

    /**
     * Recover the low-20-byte uint160 value embedded in `data`.
     *
     * For uint160-based types (P2PKH/P2SH/P2WPKH) and for legacy P2PKH nodes
     * this returns the original identifier. For the 32-byte types
     * (P2WSH/QUANTUM) it returns only the low 20 bytes, which is lossy; it is
     * intended solely for legacy code paths that still operate purely in
     * uint160 space (e.g. manipulation detectors, cluster keying for legacy
     * edges) and preserves their existing behavior for P2PKH edges.
     *
     * @return the low 20 bytes of `data` as a uint160.
     */
    uint160 ToUint160() const;

    /**
     * Rebuild the CTxDestination for display / EncodeDestination.
     *
     * @return the CTxDestination corresponding to (type, data); CNoDestination
     *         if `type` is not a recognised TrustNodeType.
     */
    CTxDestination ToDestination() const;

    /**
     * Stable, unambiguous key segment for use in LevelDB keys.
     *
     * Format: "<type:02x>-<data:64hex>" (e.g. "01-<64 hex chars>"). The
     * embedded type tag and '-' separator guarantee the segment never collides
     * with a legacy 40-hex uint160 key segment.
     */
    std::string ToKeyString() const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(type);
        READWRITE(data);
    }

    friend bool operator==(const TrustNodeId& a, const TrustNodeId& b) {
        return a.type == b.type && a.data == b.data;
    }

    friend bool operator!=(const TrustNodeId& a, const TrustNodeId& b) {
        return !(a == b);
    }

    // Ordering so TrustNodeId can be used as a key in std::set / std::map
    // (needed for visited sets and traversal). Order by type then data.
    friend bool operator<(const TrustNodeId& a, const TrustNodeId& b) {
        if (a.type != b.type) return a.type < b.type;
        return a.data < b.data;
    }
};

/**
 * Strict canonical-identity validation for a TrustNodeId.
 *
 * This is the single acceptance gate reused by every migrated downstream
 * user-identity call site (HAT, reputation, clustering, bonded-vote/DAO,
 * propagation, OP_RETURN payloads, and RPC). It does NOT change, reinterpret,
 * or truncate the identity; it only accepts or rejects it.
 *
 * A TrustNodeId is canonical iff ALL of the following hold:
 *   - `type` is one of the stable tags 1..5 (P2PKH, P2SH, P2WPKH, P2WSH,
 *     QUANTUM); unknown tags are rejected.
 *   - For the 20-byte types (P2PKH/P2SH/P2WPKH) the high 12 bytes of `data`
 *     (internal byte order, indices 20..31) are zero; nonzero high padding is
 *     rejected.
 *   - For the 32-byte types (P2WSH/QUANTUM) all 32 bytes are significant and no
 *     padding constraint applies.
 *   - `ToDestination()` yields a supported CTxDestination that maps back to the
 *     exact same identity (no width rejection, no type inference).
 *
 * @param node The identity to validate.
 * @param err  Populated with a human-readable reason on failure; cleared on
 *             success.
 * @return true iff `node` is a strictly canonical TrustNodeId.
 */
bool ValidateCanonicalTrustNode(const TrustNodeId& node, std::string& err);

/**
 * Parse a canonical LevelDB key segment into a TrustNodeId.
 *
 * The input MUST match the exact lowercase shape produced by
 * `TrustNodeId::ToKeyString()`: "<type:02x>-<data:64 lowercase hex>" (exactly
 * 67 characters, a single '-' separator at index 2). This helper rejects
 * unknown types, uppercase/noncanonical hex, extra separators, trailing
 * characters, and — via ValidateCanonicalTrustNode — nonzero high padding for
 * the 20-byte types. It never converts through uint160 or infers a type from a
 * bare width.
 *
 * @param key The textual key segment to parse.
 * @param out Populated on success with the canonical identity.
 * @param err Populated with a human-readable reason on failure; cleared on
 *            success.
 * @return true iff `key` is exactly the canonical ToKeyString() form of a
 *         strictly canonical TrustNodeId.
 */
bool ParseKeyStringToTrustNode(const std::string& key, TrustNodeId& out, std::string& err);

} // namespace CVM

#endif // CASCOIN_CVM_TRUSTNODEID_H
