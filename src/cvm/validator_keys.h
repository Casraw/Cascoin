// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_VALIDATOR_KEYS_H
#define CASCOIN_CVM_VALIDATOR_KEYS_H

#include <key.h>
#include <pubkey.h>
#include <uint256.h>
#include <sync.h>
#include <fs.h>
#include <map>
#include <string>

namespace CVM {

/**
 * Validator Key Manager
 * 
 * Manages validator keys for HAT v2 consensus participation.
 * Validators need keys to sign validation responses.
 */
class ValidatorKeyManager {
private:
    mutable CCriticalSection cs_keys;
    
    // Active validator key
    CKey validatorKey;
    CPubKey validatorPubKey;
    uint160 validatorAddress;
    bool hasValidatorKey;
    
    // Key storage path
    fs::path keyFilePath;
    
    // Load key from file
    bool LoadKeyFromFile();
    
    // Save key to file
    bool SaveKeyToFile();
    
public:
    ValidatorKeyManager();
    ~ValidatorKeyManager();
    
    /**
     * Initialize validator key manager
     * @param dataDir Data directory for key storage
     * @return true if initialized successfully
     */
    bool Initialize(const fs::path& dataDir);
    
    /**
     * Check if validator key is loaded
     */
    bool HasValidatorKey() const;
    
    /**
     * Get validator address
     */
    uint160 GetValidatorAddress() const;
    
    /**
     * Get validator public key
     */
    CPubKey GetValidatorPubKey() const;
    
    /**
     * Sign data with validator key
     * @param hash Data hash to sign
     * @param signature Output signature
     * @return true if signed successfully
     */
    bool Sign(const uint256& hash, std::vector<uint8_t>& signature) const;
    
    /**
     * Verify signature with validator public key
     * @param hash Data hash
     * @param signature Signature to verify
     * @param pubkey Public key to verify against
     * @return true if signature is valid
     */
    static bool Verify(const uint256& hash, const std::vector<uint8_t>& signature, const CPubKey& pubkey);
    
    /**
     * Generate new validator key
     * @return true if generated successfully
     */
    bool GenerateNewKey();
    
    /**
     * Import validator key from hex string
     * @param keyHex Private key in hex format
     * @return true if imported successfully
     */
    bool ImportKey(const std::string& keyHex);
    
    /**
     * Export validator key to hex string
     * @param keyHex Output private key in hex format
     * @return true if exported successfully
     */
    bool ExportKey(std::string& keyHex) const;
    
    /**
     * Load validator key from wallet
     * @param walletAddress Address to use from wallet
     * @return true if loaded successfully
     */
    bool LoadFromWallet(const uint160& walletAddress);
};

// Global validator key manager
extern std::unique_ptr<ValidatorKeyManager> g_validatorKeys;

} // namespace CVM

#endif // CASCOIN_CVM_VALIDATOR_KEYS_H
