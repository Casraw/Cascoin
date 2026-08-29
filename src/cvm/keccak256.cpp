// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/keccak256.h>

#include <cstring>

namespace CVM {

namespace {

// Keccak-f[1600] round constants.
const uint64_t kRoundConstants[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

// Rotation offsets for the rho step.
const int kRotationOffsets[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
    27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};

// Lane permutation indices for the pi step.
const int kPiLane[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

inline uint64_t RotL64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

inline uint64_t Load64LE(const uint8_t* p) {
    uint64_t r = 0;
    for (int i = 0; i < 8; ++i) {
        r |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return r;
}

inline void Store64LE(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

void KeccakF1600(uint64_t st[25]) {
    for (int round = 0; round < 24; ++round) {
        // Theta
        uint64_t bc[5];
        for (int i = 0; i < 5; ++i) {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (int i = 0; i < 5; ++i) {
            uint64_t t = bc[(i + 4) % 5] ^ RotL64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) {
                st[j + i] ^= t;
            }
        }

        // Rho and Pi
        uint64_t t = st[1];
        for (int i = 0; i < 24; ++i) {
            int j = kPiLane[i];
            uint64_t tmp = st[j];
            st[j] = RotL64(t, kRotationOffsets[i]);
            t = tmp;
        }

        // Chi
        for (int j = 0; j < 25; j += 5) {
            uint64_t row[5];
            for (int i = 0; i < 5; ++i) {
                row[i] = st[j + i];
            }
            for (int i = 0; i < 5; ++i) {
                st[j + i] ^= (~row[(i + 1) % 5]) & row[(i + 2) % 5];
            }
        }

        // Iota
        st[0] ^= kRoundConstants[round];
    }
}

} // namespace

void Keccak256(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint64_t st[25];
    std::memset(st, 0, sizeof(st));

    const size_t rate = 136; // 1088 bits (Keccak-256 rate = 17 lanes)

    // Absorb full blocks.
    while (len >= rate) {
        for (int lane = 0; lane < 17; ++lane) {
            st[lane] ^= Load64LE(data + 8 * lane);
        }
        KeccakF1600(st);
        data += rate;
        len -= rate;
    }

    // Absorb the final (padded) block. Keccak padding: append 0x01, then set
    // the top bit of the last byte of the rate (0x80).
    uint8_t block[136];
    std::memset(block, 0, rate);
    std::memcpy(block, data, len);
    block[len] ^= 0x01;
    block[rate - 1] ^= 0x80;
    for (int lane = 0; lane < 17; ++lane) {
        st[lane] ^= Load64LE(block + 8 * lane);
    }
    KeccakF1600(st);

    // Squeeze 32 bytes.
    for (int lane = 0; lane < 4; ++lane) {
        Store64LE(out + 8 * lane, st[lane]);
    }
}

std::array<uint8_t, 32> Keccak256(const std::vector<uint8_t>& data) {
    std::array<uint8_t, 32> out;
    Keccak256(data.empty() ? reinterpret_cast<const uint8_t*>("") : data.data(),
              data.size(), out.data());
    return out;
}

void EthCreateAddressBytes(const uint8_t senderBE[20], uint64_t nonce, uint8_t outAddrBE[20]) {
    // RLP-encode the list [sender (20-byte string), nonce (integer)].
    std::vector<uint8_t> payload;

    // Encode the 20-byte sender string: prefix 0x80 + 20 = 0x94, then the bytes.
    payload.push_back(0x80 + 20);
    payload.insert(payload.end(), senderBE, senderBE + 20);

    // Encode the nonce as a minimal big-endian integer.
    if (nonce == 0) {
        // The empty string (0x80) represents integer zero in RLP.
        payload.push_back(0x80);
    } else {
        uint8_t be[8];
        for (int i = 0; i < 8; ++i) {
            be[i] = static_cast<uint8_t>((nonce >> (8 * (7 - i))) & 0xFF);
        }
        int start = 0;
        while (start < 8 && be[start] == 0) {
            ++start;
        }
        int nbytes = 8 - start;
        if (nbytes == 1 && be[start] < 0x80) {
            // A single byte in [0x00, 0x7f] is its own RLP encoding.
            payload.push_back(be[start]);
        } else {
            payload.push_back(0x80 + nbytes);
            payload.insert(payload.end(), be + start, be + 8);
        }
    }

    // Wrap the payload in an RLP list header. The payload here is always < 56
    // bytes, so the short-list prefix (0xc0 + length) applies.
    std::vector<uint8_t> rlp;
    rlp.push_back(0xc0 + static_cast<uint8_t>(payload.size()));
    rlp.insert(rlp.end(), payload.begin(), payload.end());

    uint8_t digest[32];
    Keccak256(rlp.data(), rlp.size(), digest);

    // The address is the last 20 bytes of the digest (big-endian).
    std::memcpy(outAddrBE, digest + 12, 20);
}

} // namespace CVM
