// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEY_H
#define BITCOIN_KEY_H

#include <pubkey.h>
#include <serialize.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <stdexcept>
#include <vector>

/**
 * Key type enumeration for dual-stack cryptographic key management.
 * Supports both classical ECDSA (secp256k1) and post-quantum FALCON-512 keys.
 *
 * Requirements: 1.3, 1.4 (key type flag to distinguish between key types)
 */
enum class CKeyType : uint8_t {
    KEY_TYPE_INVALID = 0x00,  //!< Invalid or uninitialized key
    KEY_TYPE_ECDSA = 0x01,    //!< Classical ECDSA secp256k1 key (32-byte private key)
    KEY_TYPE_QUANTUM = 0x02   //!< Post-quantum FALCON-512 key (1281-byte private key)
};

/**
 * secure_allocator is defined in allocators.h
 * CPrivKey is a serialized private key, with all parameters included
 * (PRIVATE_KEY_SIZE bytes)
 */
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;

/** An encapsulated private key. */
class CKey
{
public:
    /**
     * secp256k1 (ECDSA) key sizes:
     */
    static const unsigned int PRIVATE_KEY_SIZE            = 279;
    static const unsigned int COMPRESSED_PRIVATE_KEY_SIZE = 214;
    
    /**
     * ECDSA raw private key size (32 bytes for secp256k1)
     * Requirements: 1.1 (support storage of ECDSA private keys - 32 bytes)
     */
    static const unsigned int ECDSA_PRIVATE_KEY_SIZE = 32;
    
    /**
     * FALCON-512 (quantum) key sizes:
     * Requirements: 1.1 (support storage of FALCON-512 private keys - 1281 bytes)
     */
    static const unsigned int QUANTUM_PRIVATE_KEY_SIZE = 1281;
    
    /**
     * see www.keylength.com
     * script supports up to 75 for single byte push
     */
    static_assert(
        PRIVATE_KEY_SIZE >= COMPRESSED_PRIVATE_KEY_SIZE,
        "COMPRESSED_PRIVATE_KEY_SIZE is larger than PRIVATE_KEY_SIZE");

private:
    //! Whether this private key is valid. We check for correctness when modifying the key
    //! data, so fValid should always correspond to the actual state.
    bool fValid;

    //! Whether the public key corresponding to this private key is (to be) compressed.
    //! Only applicable for ECDSA keys.
    bool fCompressed;
    
    //! The cryptographic algorithm type for this key.
    //! Requirements: 1.3 (store key type flag to distinguish between key types)
    CKeyType keyType;

    //! The actual byte data (32 bytes for ECDSA, 1281 bytes for FALCON-512)
    std::vector<unsigned char, secure_allocator<unsigned char> > keydata;
    
    //! Cached quantum public key (897 bytes for FALCON-512, empty for ECDSA)
    //! This is stored during key generation because deriving it from the private key
    //! requires knowledge of the liboqs internal format.
    std::vector<unsigned char> quantumPubkey;

    //! Check whether the 32-byte array pointed to by vch is valid ECDSA keydata.
    bool static Check(const unsigned char* vch);

public:
    //! Construct an invalid private key (defaults to ECDSA type for backward compatibility).
    CKey() : fValid(false), fCompressed(false), keyType(CKeyType::KEY_TYPE_ECDSA)
    {
        // Important: vch must be 32 bytes in length to not break serialization
        keydata.resize(ECDSA_PRIVATE_KEY_SIZE);
    }
    
    //! Construct a private key with specified type.
    //! Requirements: 1.1 (unified interface for both key types)
    explicit CKey(CKeyType type) : fValid(false), fCompressed(false), keyType(type)
    {
        // Resize keydata based on key type
        if (type == CKeyType::KEY_TYPE_QUANTUM) {
            keydata.resize(QUANTUM_PRIVATE_KEY_SIZE);
        } else {
            keydata.resize(ECDSA_PRIVATE_KEY_SIZE);
        }
    }

    friend bool operator==(const CKey& a, const CKey& b)
    {
        return a.fCompressed == b.fCompressed &&
            a.keyType == b.keyType &&
            a.size() == b.size() &&
            memcmp(a.keydata.data(), b.keydata.data(), a.size()) == 0;
    }

    //! Initialize using begin and end iterators to byte data.
    template <typename T>
    void Set(const T pbegin, const T pend, bool fCompressedIn)
    {
        if (size_t(pend - pbegin) != keydata.size()) {
            fValid = false;
        } else if (Check(&pbegin[0])) {
            memcpy(keydata.data(), (unsigned char*)&pbegin[0], keydata.size());
            fValid = true;
            fCompressed = fCompressedIn;
        } else {
            fValid = false;
        }
    }

    //! Simple read-only vector-like interface.
    unsigned int size() const { return (fValid ? keydata.size() : 0); }
    const unsigned char* begin() const { return keydata.data(); }
    const unsigned char* end() const { return keydata.data() + size(); }

    //! Check whether this private key is valid.
    bool IsValid() const { return fValid; }

    //! Check whether the public key corresponding to this private key is (to be) compressed.
    //! Only meaningful for ECDSA keys.
    bool IsCompressed() const { return fCompressed; }
    
    /**
     * Get the cryptographic algorithm type for this key.
     * Requirements: 1.4 (provide GetKeyType() method)
     * @return The key type (KEY_TYPE_ECDSA, KEY_TYPE_QUANTUM, or KEY_TYPE_INVALID)
     */
    CKeyType GetKeyType() const { return keyType; }
    
    /**
     * Check if this is a quantum (FALCON-512) key.
     * Requirements: 1.4 (provide IsQuantum() method)
     * @return true if this is a FALCON-512 key, false otherwise
     */
    bool IsQuantum() const { return keyType == CKeyType::KEY_TYPE_QUANTUM; }
    
    /**
     * Check if this is an ECDSA (secp256k1) key.
     * Requirements: 1.4 (provide IsECDSA() method)
     * @return true if this is an ECDSA key, false otherwise
     */
    bool IsECDSA() const { return keyType == CKeyType::KEY_TYPE_ECDSA; }

    //! Generate a new ECDSA private key using a cryptographic PRNG.
    //! Sets keyType to KEY_TYPE_ECDSA explicitly.
    //! Requirements: 1.1 (ECDSA key generation)
    void MakeNewKey(bool fCompressed);
    
    /**
     * Generate a new FALCON-512 quantum-resistant private key.
     * Uses the quantum::GenerateKeyPair() function from the FALCON-512 module.
     * Sets keyType to KEY_TYPE_QUANTUM and resizes keydata to 1281 bytes.
     *
     * Requirements: 1.1 (support storage of FALCON-512 private keys - 1281 bytes)
     *
     * @return void (sets fValid to true on success, false on failure)
     */
    void MakeNewQuantumKey();
    
    /**
     * Set quantum key data directly from raw bytes.
     * Used for importing quantum keys from wallet dumps.
     * Requirements: 10.7 (quantum keys included in wallet dump)
     *
     * @param[in] privKeyData Pointer to private key data (1281 bytes)
     * @param[in] privKeySize Size of private key data (must be 1281)
     * @param[in] pubKeyData Public key data (897 bytes)
     * @return true on success, false on failure
     */
    bool SetQuantumKeyData(const unsigned char* privKeyData, size_t privKeySize, 
                           const std::vector<unsigned char>& pubKeyData);

    /**
     * Convert the private key to a CPrivKey (serialized OpenSSL private key data).
     * This is expensive.
     */
    CPrivKey GetPrivKey() const;

    /**
     * Compute the public key from a private key.
     * This is expensive.
     */
    CPubKey GetPubKey() const;

    /**
     * Create a DER-serialized signature.
     * For ECDSA keys, creates a standard secp256k1 signature.
     * For quantum keys, dispatches to SignQuantum() for FALCON-512 signing.
     * The test_case parameter tweaks the deterministic nonce (ECDSA only).
     *
     * Requirements: 1.5, 1.6 (dispatch based on key type)
     */
    bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig, uint32_t test_case = 0) const;
    
    /**
     * Create a FALCON-512 quantum-resistant signature.
     * This method is called by Sign() when the key type is KEY_TYPE_QUANTUM.
     *
     * The signature is approximately 666 bytes (max 700 bytes) and provides
     * NIST Level 1 security (128-bit quantum security).
     *
     * Requirements: 1.5 (FALCON-512 signature generation)
     * Requirements: 1.7 (secure memory handling for quantum keys)
     *
     * @param[in] hash The 256-bit message hash to sign
     * @param[out] vchSig Output buffer for the signature (will be resized)
     * @return true on success, false on failure
     */
    bool SignQuantum(const uint256& hash, std::vector<unsigned char>& vchSig) const;

    /**
     * Create a compact signature (65 bytes), which allows reconstructing the used public key.
     * The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
     * The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
     *                  0x1D = second key with even y, 0x1E = second key with odd y,
     *                  add 0x04 for compressed keys.
     */
    bool SignCompact(const uint256& hash, std::vector<unsigned char>& vchSig) const;

    //! Derive BIP32 child key.
    bool Derive(CKey& keyChild, ChainCode &ccChild, unsigned int nChild, const ChainCode& cc) const;

    /**
     * Verify thoroughly whether a private key and a public key match.
     * This is done using a different mechanism than just regenerating it.
     */
    bool VerifyPubKey(const CPubKey& vchPubKey) const;

    //! Load private key and check that public key matches.
    bool Load(const CPrivKey& privkey, const CPubKey& vchPubKey, bool fSkipCheck);
    
    /**
     * Serialization support for CKey.
     * 
     * Serialization format:
     * - Version byte prefix: 0x01 for ECDSA, 0x02 for quantum
     * - Key data: 32 bytes for ECDSA, 1281 bytes for quantum
     * - Compressed flag: 1 byte (only for ECDSA)
     * 
     * For backward compatibility during deserialization:
     * - If the first byte is 0x01 (ECDSA) or 0x02 (quantum), use new format
     * - Legacy 32-byte keys without prefix are treated as ECDSA
     * 
     * Requirements: 1.8, 1.9, 10.1 (key serialization with type prefix)
     */
    ADD_SERIALIZE_METHODS;
    
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            // Deserialization
            DeserializeKey(s);
        } else {
            // Serialization
            SerializeKey(s);
        }
    }
    
private:
    /**
     * Serialize the key to a stream.
     * Format for ECDSA: [type_byte][key_data][compressed_flag]
     * Format for Quantum: [type_byte][key_data][compressed_flag][pubkey_size][pubkey_data]
     */
    template <typename Stream>
    void SerializeKey(Stream& s) const {
        // Write key type byte (0x01 for ECDSA, 0x02 for quantum)
        uint8_t typeByte = static_cast<uint8_t>(keyType);
        ::Serialize(s, typeByte);
        
        // Write key data
        if (fValid) {
            // Write the actual key data
            s.write((const char*)keydata.data(), keydata.size());
        } else {
            // Write zeros for invalid key
            std::vector<unsigned char> zeros(keydata.size(), 0);
            s.write((const char*)zeros.data(), zeros.size());
        }
        
        // Write compressed flag (only meaningful for ECDSA, but always write for consistency)
        uint8_t compressedByte = fCompressed ? 1 : 0;
        ::Serialize(s, compressedByte);
        
        // For quantum keys, also serialize the cached public key
        if (keyType == CKeyType::KEY_TYPE_QUANTUM) {
            ::WriteCompactSize(s, quantumPubkey.size());
            if (!quantumPubkey.empty()) {
                s.write((const char*)quantumPubkey.data(), quantumPubkey.size());
            }
        }
    }
    
    /**
     * Deserialize the key from a stream.
     * Handles both new format (with type prefix) and legacy format (raw 32-byte ECDSA).
     */
    template <typename Stream>
    void DeserializeKey(Stream& s) {
        // Read first byte to determine format
        uint8_t firstByte;
        ::Unserialize(s, firstByte);
        
        if (firstByte == static_cast<uint8_t>(CKeyType::KEY_TYPE_ECDSA)) {
            // New format: ECDSA key with type prefix
            keyType = CKeyType::KEY_TYPE_ECDSA;
            keydata.resize(ECDSA_PRIVATE_KEY_SIZE);
            s.read((char*)keydata.data(), ECDSA_PRIVATE_KEY_SIZE);
            
            // Read compressed flag
            uint8_t compressedByte;
            ::Unserialize(s, compressedByte);
            fCompressed = (compressedByte != 0);
            
            // Validate key data
            fValid = Check(keydata.data());
            
            // Clear quantum pubkey for ECDSA keys
            quantumPubkey.clear();
        }
        else if (firstByte == static_cast<uint8_t>(CKeyType::KEY_TYPE_QUANTUM)) {
            // New format: Quantum key with type prefix
            keyType = CKeyType::KEY_TYPE_QUANTUM;
            keydata.resize(QUANTUM_PRIVATE_KEY_SIZE);
            s.read((char*)keydata.data(), QUANTUM_PRIVATE_KEY_SIZE);
            
            // Read compressed flag (not used for quantum, but read for format consistency)
            uint8_t compressedByte;
            ::Unserialize(s, compressedByte);
            fCompressed = false; // Compression not applicable for quantum keys
            
            // Read the cached quantum public key
            size_t pubkeySize = ::ReadCompactSize(s);
            if (pubkeySize > 0 && pubkeySize <= 1024) { // Sanity check
                quantumPubkey.resize(pubkeySize);
                s.read((char*)quantumPubkey.data(), pubkeySize);
            } else {
                quantumPubkey.clear();
            }
            
            // Quantum keys are valid if they have the correct size
            // Full validation would require the quantum module
            fValid = (keydata.size() == QUANTUM_PRIVATE_KEY_SIZE);
        }
        else {
            // Legacy format: raw 32-byte ECDSA key (first byte is part of key data)
            // This provides backward compatibility for existing serialized keys
            keyType = CKeyType::KEY_TYPE_ECDSA;
            keydata.resize(ECDSA_PRIVATE_KEY_SIZE);
            
            // First byte is already read, it's part of the key data
            keydata[0] = firstByte;
            
            // Read remaining 31 bytes
            s.read((char*)keydata.data() + 1, ECDSA_PRIVATE_KEY_SIZE - 1);
            
            // Legacy keys are assumed compressed (most common case)
            fCompressed = true;
            
            // Validate key data
            fValid = Check(keydata.data());
            
            // Clear quantum pubkey for ECDSA keys
            quantumPubkey.clear();
        }
    }
};

struct CExtKey {
    unsigned char nDepth;
    unsigned char vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CKey key;

    friend bool operator==(const CExtKey& a, const CExtKey& b)
    {
        return a.nDepth == b.nDepth &&
            memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], sizeof(vchFingerprint)) == 0 &&
            a.nChild == b.nChild &&
            a.chaincode == b.chaincode &&
            a.key == b.key;
    }

    void Encode(unsigned char code[BIP32_EXTKEY_SIZE]) const;
    void Decode(const unsigned char code[BIP32_EXTKEY_SIZE]);
    bool Derive(CExtKey& out, unsigned int nChild) const;
    CExtPubKey Neuter() const;
    void SetMaster(const unsigned char* seed, unsigned int nSeedLen);
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

/** Initialize the elliptic curve support. May not be called twice without calling ECC_Stop first. */
void ECC_Start(void);

/** Deinitialize the elliptic curve support. No-op if ECC_Start wasn't called first. */
void ECC_Stop(void);

/** Check that required EC support is available at runtime. */
bool ECC_InitSanityCheck(void);

#endif // BITCOIN_KEY_H
