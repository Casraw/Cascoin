// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/validator_keys.h>
#include <util.h>
#include <utilstrencodings.h>
#include <hash.h>
#include <random.h>
#include <wallet/wallet.h>
#include <fstream>

namespace CVM {

std::unique_ptr<ValidatorKeyManager> g_validatorKeys;

ValidatorKeyManager::ValidatorKeyManager()
    : hasValidatorKey(false)
{
}

ValidatorKeyManager::~ValidatorKeyManager()
{
}

bool ValidatorKeyManager::Initialize(const fs::path& dataDir)
{
    LOCK(cs_keys);
    
    keyFilePath = dataDir / "validator.key";
    
    // Try to load existing key
    if (fs::exists(keyFilePath)) {
        if (LoadKeyFromFile()) {
            LogPrintf("ValidatorKeys: Loaded validator key from %s (address: %s)\n",
                     keyFilePath.string(), validatorAddress.ToString());
            return true;
        } else {
            LogPrintf("ValidatorKeys: Failed to load key from %s\n", keyFilePath.string());
        }
    }
    
    // Check if validator address is configured
    std::string configAddress = gArgs.GetArg("-validatoraddress", "");
    if (!configAddress.empty()) {
        // Try to load from wallet
        try {
            uint160 addr;
            if (configAddress.length() == 40) {
                // Hex address
                std::vector<uint8_t> data = ParseHex(configAddress);
                if (data.size() == 20) {
                    memcpy(addr.begin(), data.data(), 20);
                    if (LoadFromWallet(addr)) {
                        LogPrintf("ValidatorKeys: Loaded validator key from wallet (address: %s)\n",
                                 addr.ToString());
                        return true;
                    }
                }
            }
        } catch (...) {
            LogPrintf("ValidatorKeys: Failed to parse -validatoraddress\n");
        }
    }
    
    // No key available - validator mode disabled
    LogPrintf("ValidatorKeys: No validator key configured. Use -validatoraddress or generate key with RPC.\n");
    return false;
}

bool ValidatorKeyManager::HasValidatorKey() const
{
    LOCK(cs_keys);
    return hasValidatorKey;
}

uint160 ValidatorKeyManager::GetValidatorAddress() const
{
    LOCK(cs_keys);
    return validatorAddress;
}

CPubKey ValidatorKeyManager::GetValidatorPubKey() const
{
    LOCK(cs_keys);
    return validatorPubKey;
}

bool ValidatorKeyManager::Sign(const uint256& hash, std::vector<uint8_t>& signature) const
{
    LOCK(cs_keys);
    
    if (!hasValidatorKey) {
        return false;
    }
    
    return validatorKey.Sign(hash, signature);
}

bool ValidatorKeyManager::Verify(const uint256& hash, const std::vector<uint8_t>& signature, const CPubKey& pubkey)
{
    return pubkey.Verify(hash, signature);
}

bool ValidatorKeyManager::GenerateNewKey()
{
    LOCK(cs_keys);
    
    // Generate new key
    validatorKey.MakeNewKey(true); // compressed
    validatorPubKey = validatorKey.GetPubKey();
    validatorAddress = validatorPubKey.GetID();
    hasValidatorKey = true;
    
    // Save to file
    if (!SaveKeyToFile()) {
        LogPrintf("ValidatorKeys: Failed to save generated key\n");
        return false;
    }
    
    LogPrintf("ValidatorKeys: Generated new validator key (address: %s)\n",
             validatorAddress.ToString());
    
    return true;
}

bool ValidatorKeyManager::ImportKey(const std::string& keyHex)
{
    LOCK(cs_keys);
    
    // Parse hex key
    std::vector<uint8_t> keyData = ParseHex(keyHex);
    if (keyData.size() != 32) {
        LogPrintf("ValidatorKeys: Invalid key size: %d bytes\n", keyData.size());
        return false;
    }
    
    // Set key
    validatorKey.Set(keyData.begin(), keyData.end(), true); // compressed
    if (!validatorKey.IsValid()) {
        LogPrintf("ValidatorKeys: Invalid private key\n");
        return false;
    }
    
    validatorPubKey = validatorKey.GetPubKey();
    validatorAddress = validatorPubKey.GetID();
    hasValidatorKey = true;
    
    // Save to file
    if (!SaveKeyToFile()) {
        LogPrintf("ValidatorKeys: Failed to save imported key\n");
        return false;
    }
    
    LogPrintf("ValidatorKeys: Imported validator key (address: %s)\n",
             validatorAddress.ToString());
    
    return true;
}

bool ValidatorKeyManager::ExportKey(std::string& keyHex) const
{
    LOCK(cs_keys);
    
    if (!hasValidatorKey) {
        return false;
    }
    
    // Get private key bytes
    CPrivKey privkey = validatorKey.GetPrivKey();
    if (privkey.size() < 32) {
        return false;
    }
    
    // Export as hex (first 32 bytes)
    keyHex = HexStr(privkey.begin(), privkey.begin() + 32);
    
    return true;
}

bool ValidatorKeyManager::LoadFromWallet(const uint160& walletAddress)
{
    LOCK(cs_keys);
    
    // Get wallet
    if (::vpwallets.empty()) {
        LogPrintf("ValidatorKeys: No wallet available\n");
        return false;
    }
    CWallet* pwallet = ::vpwallets[0];
    
    // Get key from wallet
    CKeyID keyID(walletAddress);
    CKey key;
    if (!pwallet->GetKey(keyID, key)) {
        LogPrintf("ValidatorKeys: Key not found in wallet for address %s\n",
                 walletAddress.ToString());
        return false;
    }
    
    validatorKey = key;
    validatorPubKey = key.GetPubKey();
    validatorAddress = walletAddress;
    hasValidatorKey = true;
    
    // Save to file for future use
    SaveKeyToFile();
    
    return true;
}

bool ValidatorKeyManager::LoadKeyFromFile()
{
    try {
        std::ifstream file(keyFilePath.string(), std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read key data (32 bytes)
        std::vector<uint8_t> keyData(32);
        file.read(reinterpret_cast<char*>(keyData.data()), 32);
        if (!file.good()) {
            return false;
        }
        
        // Set key
        validatorKey.Set(keyData.begin(), keyData.end(), true); // compressed
        if (!validatorKey.IsValid()) {
            return false;
        }
        
        validatorPubKey = validatorKey.GetPubKey();
        validatorAddress = validatorPubKey.GetID();
        hasValidatorKey = true;
        
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ValidatorKeys: Exception loading key: %s\n", e.what());
        return false;
    }
}

bool ValidatorKeyManager::SaveKeyToFile()
{
    try {
        // Create directory if needed
        fs::create_directories(keyFilePath.parent_path());
        
        // Write key data
        std::ofstream file(keyFilePath.string(), std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        
        // Get private key bytes
        CPrivKey privkey = validatorKey.GetPrivKey();
        if (privkey.size() < 32) {
            return false;
        }
        
        // Write first 32 bytes
        file.write(reinterpret_cast<const char*>(privkey.data()), 32);
        if (!file.good()) {
            return false;
        }
        
        file.close();
        
        // Set restrictive permissions (owner read/write only)
        fs::permissions(keyFilePath, fs::perms::owner_read | fs::perms::owner_write);
        
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ValidatorKeys: Exception saving key: %s\n", e.what());
        return false;
    }
}

} // namespace CVM
