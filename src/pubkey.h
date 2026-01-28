// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PUBKEY_H
#define BITCOIN_PUBKEY_H

#include <hash.h>
#include <serialize.h>
#include <uint256.h>

#include <stdexcept>
#include <vector>

const unsigned int BIP32_EXTKEY_SIZE = 74;

/**
 * Public key type enumeration for dual-stack cryptographic key management.
 * Supports both classical ECDSA (secp256k1) and post-quantum FALCON-512 public keys.
 *
 * Requirements: 1.2 (support storage of ECDSA and FALCON-512 public keys)
 */
enum class CPubKeyType : uint8_t {
    PUBKEY_TYPE_INVALID = 0x00,  //!< Invalid or uninitialized public key
    PUBKEY_TYPE_ECDSA = 0x01,    //!< Classical ECDSA secp256k1 public key (33/65 bytes)
    PUBKEY_TYPE_QUANTUM = 0x05   //!< Post-quantum FALCON-512 public key (897 bytes)
};

/** A reference to a CKey: the Hash160 of its serialized public key */
class CKeyID : public uint160
{
public:
    CKeyID() : uint160() {}
    explicit CKeyID(const uint160& in) : uint160(in) {}
};

typedef uint256 ChainCode;

/** An encapsulated public key. */
class CPubKey
{
public:
    /**
     * secp256k1 (ECDSA) key sizes:
     */
    static const unsigned int PUBLIC_KEY_SIZE             = 65;
    static const unsigned int COMPRESSED_PUBLIC_KEY_SIZE  = 33;
    static const unsigned int SIGNATURE_SIZE              = 72;
    static const unsigned int COMPACT_SIGNATURE_SIZE      = 65;
    
    /**
     * FALCON-512 (quantum) key and signature sizes:
     * Requirements: 1.2 (support storage of FALCON-512 public keys - 897 bytes)
     */
    static const unsigned int QUANTUM_PUBLIC_KEY_SIZE     = 897;
    static const unsigned int QUANTUM_SIGNATURE_SIZE      = 666;      // Typical size
    static const unsigned int MAX_QUANTUM_SIGNATURE_SIZE  = 700;      // Maximum size
    
    /**
     * see www.keylength.com
     * script supports up to 75 for single byte push
     */
    static_assert(
        PUBLIC_KEY_SIZE >= COMPRESSED_PUBLIC_KEY_SIZE,
        "COMPRESSED_PUBLIC_KEY_SIZE is larger than PUBLIC_KEY_SIZE");

private:

    /**
     * The key type for this public key.
     * Requirements: 1.2 (key type to distinguish between ECDSA and quantum)
     */
    CPubKeyType keyType;

    /**
     * Storage for public key data.
     * For ECDSA: up to 65 bytes (stored in fixed array for backward compatibility)
     * For Quantum: 897 bytes (stored in vector due to larger size)
     */
    unsigned char vch[PUBLIC_KEY_SIZE];
    std::vector<unsigned char> vchQuantum;

    //! Compute the length of an ECDSA pubkey with a given first byte.
    unsigned int static GetLen(unsigned char chHeader)
    {
        if (chHeader == 2 || chHeader == 3)
            return COMPRESSED_PUBLIC_KEY_SIZE;
        if (chHeader == 4 || chHeader == 6 || chHeader == 7)
            return PUBLIC_KEY_SIZE;
        return 0;
    }

    //! Set this key data to be invalid
    void Invalidate()
    {
        vch[0] = 0xFF;
        // Note: We don't change keyType here - it should be set by the caller
        // or remain as the default ECDSA type for backward compatibility
        vchQuantum.clear();
    }

public:
    //! Construct an invalid public key.
    CPubKey() : keyType(CPubKeyType::PUBKEY_TYPE_ECDSA)
    {
        Invalidate();
    }

    //! Initialize a public key using begin/end iterators to byte data.
    //! Updated to handle quantum public keys (897 bytes).
    //! Requirements: 1.2 (handle quantum public keys)
    template <typename T>
    void Set(const T pbegin, const T pend)
    {
        size_t dataLen = pend - pbegin;
        
        // Check if this is a quantum public key (897 bytes)
        if (dataLen == QUANTUM_PUBLIC_KEY_SIZE) {
            keyType = CPubKeyType::PUBKEY_TYPE_QUANTUM;
            vchQuantum.assign(pbegin, pend);
            vch[0] = 0xFF; // Mark ECDSA storage as invalid
            return;
        }
        
        // Handle ECDSA public keys
        vchQuantum.clear();
        int len = dataLen == 0 ? 0 : GetLen(pbegin[0]);
        if (len && len == (int)dataLen) {
            keyType = CPubKeyType::PUBKEY_TYPE_ECDSA;
            memcpy(vch, (unsigned char*)&pbegin[0], len);
        } else {
            keyType = CPubKeyType::PUBKEY_TYPE_INVALID;
            Invalidate();
        }
    }
    
    //! Set a quantum public key explicitly.
    //! Requirements: 1.2 (handle quantum public keys)
    void SetQuantum(const std::vector<unsigned char>& pubkey)
    {
        if (pubkey.size() == QUANTUM_PUBLIC_KEY_SIZE) {
            keyType = CPubKeyType::PUBKEY_TYPE_QUANTUM;
            vchQuantum = pubkey;
            vch[0] = 0xFF; // Mark ECDSA storage as invalid
        } else {
            Invalidate();
        }
    }

    //! Construct a public key using begin/end iterators to byte data.
    template <typename T>
    CPubKey(const T pbegin, const T pend) : keyType(CPubKeyType::PUBKEY_TYPE_ECDSA)
    {
        Set(pbegin, pend);
    }

    //! Construct a public key from a byte vector.
    explicit CPubKey(const std::vector<unsigned char>& _vch) : keyType(CPubKeyType::PUBKEY_TYPE_ECDSA)
    {
        Set(_vch.begin(), _vch.end());
    }

    //! Simple read-only vector-like interface to the pubkey data.
    //! Updated to return correct size based on key type.
    //! Requirements: 1.2 (return correct size based on type)
    unsigned int size() const {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return vchQuantum.size();
        }
        return GetLen(vch[0]);
    }
    
    const unsigned char* begin() const {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return vchQuantum.data();
        }
        return vch;
    }
    
    const unsigned char* end() const {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return vchQuantum.data() + vchQuantum.size();
        }
        return vch + GetLen(vch[0]);
    }
    
    const unsigned char& operator[](unsigned int pos) const {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return vchQuantum[pos];
        }
        return vch[pos];
    }
    
    /**
     * Get the public key type.
     * Requirements: 1.2 (type detection methods)
     * @return The key type (PUBKEY_TYPE_ECDSA, PUBKEY_TYPE_QUANTUM, or PUBKEY_TYPE_INVALID)
     */
    CPubKeyType GetKeyType() const { return keyType; }
    
    /**
     * Check if this is a quantum (FALCON-512) public key.
     * Requirements: 1.2 (type detection methods)
     * @return true if this is a FALCON-512 public key, false otherwise
     */
    bool IsQuantum() const { return keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM; }
    
    /**
     * Check if this is an ECDSA (secp256k1) public key.
     * Requirements: 1.2 (type detection methods)
     * @return true if this is an ECDSA public key, false otherwise
     */
    bool IsECDSA() const { return keyType == CPubKeyType::PUBKEY_TYPE_ECDSA; }

    //! Comparator implementation.
    //! Updated to handle quantum public keys.
    friend bool operator==(const CPubKey& a, const CPubKey& b)
    {
        if (a.keyType != b.keyType)
            return false;
        if (a.keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return a.vchQuantum == b.vchQuantum;
        }
        return a.vch[0] == b.vch[0] &&
               memcmp(a.vch, b.vch, a.size()) == 0;
    }
    friend bool operator!=(const CPubKey& a, const CPubKey& b)
    {
        return !(a == b);
    }
    friend bool operator<(const CPubKey& a, const CPubKey& b)
    {
        // Different types: ECDSA < QUANTUM
        if (a.keyType != b.keyType)
            return static_cast<uint8_t>(a.keyType) < static_cast<uint8_t>(b.keyType);
        if (a.keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return a.vchQuantum < b.vchQuantum;
        }
        return a.vch[0] < b.vch[0] ||
               (a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) < 0);
    }

    //! Implement serialization, as if this was a byte vector.
    //! Updated to handle quantum public keys with type prefix.
    //! Requirements: 10.2 (serialization with type prefix 0x05 for quantum)
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            // Quantum key serialization: type prefix (0x05) + 897 bytes
            unsigned int len = 1 + QUANTUM_PUBLIC_KEY_SIZE;
            ::WriteCompactSize(s, len);
            uint8_t typeByte = static_cast<uint8_t>(CPubKeyType::PUBKEY_TYPE_QUANTUM);
            s.write((char*)&typeByte, 1);
            s.write((char*)vchQuantum.data(), vchQuantum.size());
        } else {
            // ECDSA key serialization: original format for backward compatibility
            unsigned int len = size();
            ::WriteCompactSize(s, len);
            s.write((char*)vch, len);
        }
    }
    
    template <typename Stream>
    void Unserialize(Stream& s)
    {
        unsigned int len = ::ReadCompactSize(s);
        
        // Check for quantum key format: type prefix (0x05) + 897 bytes = 898 bytes
        if (len == 1 + QUANTUM_PUBLIC_KEY_SIZE) {
            uint8_t typeByte;
            s.read((char*)&typeByte, 1);
            if (typeByte == static_cast<uint8_t>(CPubKeyType::PUBKEY_TYPE_QUANTUM)) {
                keyType = CPubKeyType::PUBKEY_TYPE_QUANTUM;
                vchQuantum.resize(QUANTUM_PUBLIC_KEY_SIZE);
                s.read((char*)vchQuantum.data(), QUANTUM_PUBLIC_KEY_SIZE);
                vch[0] = 0xFF; // Mark ECDSA storage as invalid
                return;
            }
            // Invalid type byte, skip remaining data
            char dummy;
            for (unsigned int i = 0; i < QUANTUM_PUBLIC_KEY_SIZE; i++)
                s.read(&dummy, 1);
            Invalidate();
            return;
        }
        
        // ECDSA key deserialization: original format
        vchQuantum.clear();
        if (len <= PUBLIC_KEY_SIZE) {
            s.read((char*)vch, len);
            // Validate the key - check if it's a valid ECDSA pubkey format
            if (len > 0 && GetLen(vch[0]) == len) {
                keyType = CPubKeyType::PUBKEY_TYPE_ECDSA;
            } else {
                keyType = CPubKeyType::PUBKEY_TYPE_INVALID;
                Invalidate();
            }
        } else {
            // invalid pubkey, skip available data
            char dummy;
            while (len--)
                s.read(&dummy, 1);
            keyType = CPubKeyType::PUBKEY_TYPE_INVALID;
            Invalidate();
        }
    }

    //! Get the KeyID of this public key (hash of its serialization)
    //! For ECDSA: Hash160 of the public key
    //! For Quantum: Hash160 of the public key (for compatibility)
    CKeyID GetID() const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return CKeyID(Hash160(vchQuantum.data(), vchQuantum.data() + vchQuantum.size()));
        }
        return CKeyID(Hash160(vch, vch + size()));
    }
    
    /**
     * Get the full SHA256 hash of this public key.
     * For quantum keys, this is used for witness program derivation.
     * Requirements: 2.2 (SHA256-based key ID for quantum)
     */
    uint256 GetQuantumID() const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return Hash(vchQuantum.data(), vchQuantum.data() + vchQuantum.size());
        }
        return Hash(vch, vch + size());
    }

    //! Get the 256-bit hash of this public key.
    uint256 GetHash() const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return Hash(vchQuantum.data(), vchQuantum.data() + vchQuantum.size());
        }
        return Hash(vch, vch + size());
    }

    /*
     * Check syntactic correctness.
     *
     * Note that this is consensus critical as CheckSig() calls it!
     * Updated to handle quantum public keys.
     */
    bool IsValid() const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return vchQuantum.size() == QUANTUM_PUBLIC_KEY_SIZE;
        }
        return size() > 0;
    }

    //! fully validate whether this is a valid public key (more expensive than IsValid())
    //! For quantum keys, checks size is exactly 897 bytes.
    bool IsFullyValid() const;

    //! Check whether this is a compressed public key.
    //! Quantum keys are not compressed (always returns false for quantum).
    bool IsCompressed() const
    {
        if (keyType == CPubKeyType::PUBKEY_TYPE_QUANTUM) {
            return false; // Quantum keys don't have compression
        }
        return size() == COMPRESSED_PUBLIC_KEY_SIZE;
    }

    /**
     * Verify a DER signature (~72 bytes for ECDSA, ~666 bytes for FALCON-512).
     * If this public key is not fully valid, the return value will be false.
     * Dispatches to appropriate verification based on key type.
     * Requirements: 2.2 (dispatch based on key type)
     */
    bool Verify(const uint256& hash, const std::vector<unsigned char>& vchSig) const;
    
    /**
     * Verify a FALCON-512 quantum signature.
     * This method is called by Verify() when the key type is PUBKEY_TYPE_QUANTUM.
     * Requirements: 2.2 (FALCON-512 verification via quantum module)
     *
     * @param[in] hash The 256-bit message hash that was signed
     * @param[in] vchSig The FALCON-512 signature to verify (up to 700 bytes)
     * @return true if signature is valid, false otherwise
     */
    bool VerifyQuantum(const uint256& hash, const std::vector<unsigned char>& vchSig) const;

    /**
     * Check whether a signature is normalized (lower-S).
     */
    static bool CheckLowS(const std::vector<unsigned char>& vchSig);

    //! Recover a public key from a compact signature.
    bool RecoverCompact(const uint256& hash, const std::vector<unsigned char>& vchSig);

    //! Turn this public key into an uncompressed public key.
    bool Decompress();

    //! Derive BIP32 child pubkey.
    bool Derive(CPubKey& pubkeyChild, ChainCode &ccChild, unsigned int nChild, const ChainCode& cc) const;
};

struct CExtPubKey {
    unsigned char nDepth;
    unsigned char vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CPubKey pubkey;

    friend bool operator==(const CExtPubKey &a, const CExtPubKey &b)
    {
        return a.nDepth == b.nDepth &&
            memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], sizeof(vchFingerprint)) == 0 &&
            a.nChild == b.nChild &&
            a.chaincode == b.chaincode &&
            a.pubkey == b.pubkey;
    }

    void Encode(unsigned char code[BIP32_EXTKEY_SIZE]) const;
    void Decode(const unsigned char code[BIP32_EXTKEY_SIZE]);
    bool Derive(CExtPubKey& out, unsigned int nChild) const;

    void Serialize(CSizeComputer& s) const
    {
        // Optimized implementation for ::GetSerializeSize that avoids copying.
        s.seek(BIP32_EXTKEY_SIZE + 1); // add one byte for the size (compact int)
    }
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        unsigned int len = BIP32_EXTKEY_SIZE;
        ::WriteCompactSize(s, len);
        unsigned char code[BIP32_EXTKEY_SIZE];
        Encode(code);
        s.write((const char *)&code[0], len);
    }
    template <typename Stream>
    void Unserialize(Stream& s)
    {
        unsigned int len = ::ReadCompactSize(s);
        unsigned char code[BIP32_EXTKEY_SIZE];
        if (len != BIP32_EXTKEY_SIZE)
            throw std::runtime_error("Invalid extended key size\n");
        s.read((char *)&code[0], len);
        Decode(code);
    }
};

/** Users of this module must hold an ECCVerifyHandle. The constructor and
 *  destructor of these are not allowed to run in parallel, though. */
class ECCVerifyHandle
{
    static int refcount;

public:
    ECCVerifyHandle();
    ~ECCVerifyHandle();
};

#endif // BITCOIN_PUBKEY_H
