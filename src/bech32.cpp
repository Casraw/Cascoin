// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bech32.h>

namespace
{

typedef std::vector<uint8_t> data;

/** The Bech32 character set for encoding. */
const char* CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

/** The Bech32 character set for decoding. */
const int8_t CHARSET_REV[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    15, -1, 10, 17, 21, 20, 26, 30,  7,  5, -1, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1,
    -1, 29, -1, 24, 13, 25,  9,  8, 23, -1, 18, 22, 31, 27, 19, -1,
     1,  0,  3, 16, 11, 28, 12, 14,  6,  4,  2, -1, -1, -1, -1, -1
};

/**
 * Checksum constants for Bech32 and Bech32m.
 * Bech32 uses constant 1, Bech32m uses constant 0x2bc830a3 (BIP-350).
 */
static constexpr uint32_t BECH32_CONST = 1;
static constexpr uint32_t BECH32M_CONST = 0x2bc830a3;

/** Concatenate two byte arrays. */
data Cat(data x, const data& y)
{
    x.insert(x.end(), y.begin(), y.end());
    return x;
}

/** This function will compute what 6 5-bit values to XOR into the last 6 input values, in order to
 *  make the checksum 0. These 6 values are packed together in a single 30-bit integer. The higher
 *  bits correspond to earlier values. */
uint32_t PolyMod(const data& v)
{
    uint32_t c = 1;
    for (auto v_i : v) {
        uint8_t c0 = c >> 25;
        c = ((c & 0x1ffffff) << 5) ^ v_i;
        if (c0 & 1)  c ^= 0x3b6a57b2;
        if (c0 & 2)  c ^= 0x26508e6d;
        if (c0 & 4)  c ^= 0x1ea119fa;
        if (c0 & 8)  c ^= 0x3d4233dd;
        if (c0 & 16) c ^= 0x2a1462b3;
    }
    return c;
}

/** Convert to lower case. */
inline unsigned char LowerCase(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A') + 'a' : c;
}

/** Expand a HRP for use in checksum computation. */
data ExpandHRP(const std::string& hrp)
{
    data ret;
    ret.reserve(hrp.size() + 90);
    ret.resize(hrp.size() * 2 + 1);
    for (size_t i = 0; i < hrp.size(); ++i) {
        unsigned char c = hrp[i];
        ret[i] = c >> 5;
        ret[i + hrp.size() + 1] = c & 0x1f;
    }
    ret[hrp.size()] = 0;
    return ret;
}

/** Verify a checksum and return the encoding type. */
bech32::Encoding VerifyChecksumWithType(const std::string& hrp, const data& values)
{
    uint32_t check = PolyMod(Cat(ExpandHRP(hrp), values));
    if (check == BECH32_CONST) return bech32::Encoding::BECH32;
    if (check == BECH32M_CONST) return bech32::Encoding::BECH32M;
    return bech32::Encoding::INVALID;
}

/** Verify a Bech32 checksum (original BIP-173). */
bool VerifyChecksum(const std::string& hrp, const data& values)
{
    return PolyMod(Cat(ExpandHRP(hrp), values)) == BECH32_CONST;
}

/** Create a checksum with specified constant. */
data CreateChecksumWithConst(const std::string& hrp, const data& values, uint32_t checkConst)
{
    data enc = Cat(ExpandHRP(hrp), values);
    enc.resize(enc.size() + 6);
    uint32_t mod = PolyMod(enc) ^ checkConst;
    data ret(6);
    for (size_t i = 0; i < 6; ++i) {
        ret[i] = (mod >> (5 * (5 - i))) & 31;
    }
    return ret;
}

/** Create a Bech32 checksum (original BIP-173). */
data CreateChecksum(const std::string& hrp, const data& values)
{
    return CreateChecksumWithConst(hrp, values, BECH32_CONST);
}

/** Create a Bech32m checksum (BIP-350). */
data CreateChecksumBech32m(const std::string& hrp, const data& values)
{
    return CreateChecksumWithConst(hrp, values, BECH32M_CONST);
}

} // namespace

namespace bech32
{

/** Encode a Bech32 string (original BIP-173). */
std::string Encode(const std::string& hrp, const data& values) {
    data checksum = CreateChecksum(hrp, values);
    data combined = Cat(values, checksum);
    std::string ret = hrp + '1';
    ret.reserve(ret.size() + combined.size());
    for (auto c : combined) {
        ret += CHARSET[c];
    }
    return ret;
}

/** Encode a Bech32m string (BIP-350). */
std::string EncodeBech32m(const std::string& hrp, const data& values) {
    data checksum = CreateChecksumBech32m(hrp, values);
    data combined = Cat(values, checksum);
    std::string ret = hrp + '1';
    ret.reserve(ret.size() + combined.size());
    for (auto c : combined) {
        ret += CHARSET[c];
    }
    return ret;
}

/** Encode using specified encoding type. */
std::string Encode(Encoding encoding, const std::string& hrp, const data& values) {
    if (encoding == Encoding::BECH32) {
        return Encode(hrp, values);
    } else if (encoding == Encoding::BECH32M) {
        return EncodeBech32m(hrp, values);
    }
    return {};
}

/** Decode a Bech32 string (original behavior for backward compatibility). */
std::pair<std::string, data> Decode(const std::string& str) {
    bool lower = false, upper = false;
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c < 33 || c > 126) return {};
        if (c >= 'a' && c <= 'z') lower = true;
        if (c >= 'A' && c <= 'Z') upper = true;
    }
    if (lower && upper) return {};
    size_t pos = str.rfind('1');
    if (str.size() > 90 || pos == str.npos || pos == 0 || pos + 7 > str.size()) {
        return {};
    }
    data values(str.size() - 1 - pos);
    for (size_t i = 0; i < str.size() - 1 - pos; ++i) {
        unsigned char c = str[i + pos + 1];
        int8_t rev = (c < 33 || c > 126) ? -1 : CHARSET_REV[c];
        if (rev == -1) {
            return {};
        }
        values[i] = rev;
    }
    std::string hrp;
    for (size_t i = 0; i < pos; ++i) {
        hrp += LowerCase(str[i]);
    }
    if (!VerifyChecksum(hrp, values)) {
        return {};
    }
    return {hrp, data(values.begin(), values.end() - 6)};
}

/** Decode a Bech32 or Bech32m string with encoding detection. */
DecodeResult DecodeWithType(const std::string& str) {
    DecodeResult result;
    result.encoding = Encoding::INVALID;
    
    bool lower = false, upper = false;
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = str[i];
        if (c < 33 || c > 126) return result;
        if (c >= 'a' && c <= 'z') lower = true;
        if (c >= 'A' && c <= 'Z') upper = true;
    }
    if (lower && upper) return result;
    
    size_t pos = str.rfind('1');
    if (str.size() > 90 || pos == str.npos || pos == 0 || pos + 7 > str.size()) {
        return result;
    }
    
    data values(str.size() - 1 - pos);
    for (size_t i = 0; i < str.size() - 1 - pos; ++i) {
        unsigned char c = str[i + pos + 1];
        int8_t rev = (c < 33 || c > 126) ? -1 : CHARSET_REV[c];
        if (rev == -1) {
            return result;
        }
        values[i] = rev;
    }
    
    std::string hrp;
    for (size_t i = 0; i < pos; ++i) {
        hrp += LowerCase(str[i]);
    }
    
    Encoding encoding = VerifyChecksumWithType(hrp, values);
    if (encoding == Encoding::INVALID) {
        return result;
    }
    
    result.encoding = encoding;
    result.hrp = hrp;
    result.data = data(values.begin(), values.end() - 6);
    return result;
}

/** Check if an HRP is a quantum address HRP. */
bool IsQuantumHRP(const std::string& hrp) {
    return hrp == QUANTUM_HRP_MAINNET || 
           hrp == QUANTUM_HRP_TESTNET || 
           hrp == QUANTUM_HRP_REGTEST;
}

} // namespace bech32
