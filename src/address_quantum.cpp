// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <address_quantum.h>

#include <bech32.h>
#include <chainparams.h>
#include <hash.h>
#include <pubkey.h>
#include <utilstrencodings.h>

namespace address {

std::string GetQuantumHRP(const CChainParams& params)
{
    // Determine quantum HRP based on network ID
    const std::string& networkID = params.NetworkIDString();
    
    if (networkID == "main") {
        return bech32::QUANTUM_HRP_MAINNET;  // "casq"
    } else if (networkID == "test") {
        return bech32::QUANTUM_HRP_TESTNET;  // "tcasq"
    } else if (networkID == "regtest") {
        return bech32::QUANTUM_HRP_REGTEST;  // "rcasq"
    }
    
    // Default to mainnet HRP for unknown networks
    return bech32::QUANTUM_HRP_MAINNET;
}

uint256 GetQuantumWitnessProgram(const CPubKey& pubkey)
{
    // Derive witness program from SHA256 of the public key
    // Requirements: 3.6 (derive address program from SHA256(pubkey))
    return pubkey.GetQuantumID();
}

uint256 GetQuantumWitnessProgram(const std::vector<unsigned char>& pubkeyData)
{
    // Derive witness program from SHA256 of the raw public key data
    return Hash(pubkeyData.begin(), pubkeyData.end());
}

std::string EncodeQuantumAddress(const CPubKey& pubkey, const CChainParams& params)
{
    // Validate that this is a quantum public key
    if (!pubkey.IsQuantum() || !pubkey.IsValid()) {
        return {};
    }
    
    // Get the quantum HRP for this network
    std::string hrp = GetQuantumHRP(params);
    
    // Derive the witness program (SHA256 of public key)
    uint256 program = GetQuantumWitnessProgram(pubkey);
    
    // Build the Bech32m data: [witness_version] + [program in 5-bit groups]
    std::vector<uint8_t> data;
    data.push_back(QUANTUM_WITNESS_VERSION);  // Witness version 2
    
    // Convert 32-byte program to 5-bit groups
    std::vector<uint8_t> programBytes(program.begin(), program.end());
    ConvertBits<8, 5, true>(data, programBytes.begin(), programBytes.end());
    
    // Encode using Bech32m (BIP-350)
    return bech32::EncodeBech32m(hrp, data);
}

std::string EncodeQuantumAddress(const std::vector<unsigned char>& pubkeyData, const CChainParams& params)
{
    // Validate public key size (897 bytes for FALCON-512)
    if (pubkeyData.size() != CPubKey::QUANTUM_PUBLIC_KEY_SIZE) {
        return {};
    }
    
    // Get the quantum HRP for this network
    std::string hrp = GetQuantumHRP(params);
    
    // Derive the witness program (SHA256 of public key)
    uint256 program = GetQuantumWitnessProgram(pubkeyData);
    
    // Build the Bech32m data: [witness_version] + [program in 5-bit groups]
    std::vector<uint8_t> data;
    data.push_back(QUANTUM_WITNESS_VERSION);  // Witness version 2
    
    // Convert 32-byte program to 5-bit groups
    std::vector<uint8_t> programBytes(program.begin(), program.end());
    ConvertBits<8, 5, true>(data, programBytes.begin(), programBytes.end());
    
    // Encode using Bech32m (BIP-350)
    return bech32::EncodeBech32m(hrp, data);
}

DecodedAddress DecodeAddress(const std::string& address, const CChainParams& params)
{
    DecodedAddress result;
    
    // Try Bech32/Bech32m decoding first
    bech32::DecodeResult bechResult = bech32::DecodeWithType(address);
    
    if (bechResult.encoding != bech32::Encoding::INVALID && !bechResult.data.empty()) {
        // Successfully decoded as Bech32 or Bech32m
        result.hrp = bechResult.hrp;
        
        // First byte is the witness version
        result.witnessVersion = bechResult.data[0];
        
        // Convert remaining 5-bit groups back to 8-bit bytes
        std::vector<uint8_t> programData;
        if (ConvertBits<5, 8, false>(programData, bechResult.data.begin() + 1, bechResult.data.end())) {
            result.program = programData;
        }
        
        // Check if this is a quantum address
        // Requirements: 3.5, 3.9 (quantum address recognition and HRP validation)
        if (bech32::IsQuantumHRP(bechResult.hrp)) {
            // Quantum address: must be Bech32m with witness version 2
            if (bechResult.encoding == bech32::Encoding::BECH32M && 
                result.witnessVersion == QUANTUM_WITNESS_VERSION &&
                result.program.size() == QUANTUM_PROGRAM_SIZE) {
                
                // Validate HRP matches the network
                std::string expectedHRP = GetQuantumHRP(params);
                if (bechResult.hrp == expectedHRP) {
                    result.isValid = true;
                    result.isQuantum = true;
                } else {
                    // HRP doesn't match network - invalid
                    // Requirements: 3.9 (reject addresses with witness version 2 but incorrect HRP)
                    result.isValid = false;
                    result.isQuantum = false;
                }
            } else {
                // Invalid quantum address format
                result.isValid = false;
                result.isQuantum = false;
            }
        } else if (bechResult.hrp == params.Bech32HRP()) {
            // Standard Bech32/Bech32m address (cas/tcas/rcas)
            // Requirements: 3.7 (legacy address routing to ECDSA verification)
            result.isValid = true;
            result.isQuantum = false;
            
            // Validate encoding matches witness version per BIP-350
            // Witness v0 should use Bech32, v1+ should use Bech32m
            if (result.witnessVersion == 0) {
                result.isValid = (bechResult.encoding == bech32::Encoding::BECH32);
            } else if (result.witnessVersion >= 1 && result.witnessVersion <= 16) {
                result.isValid = (bechResult.encoding == bech32::Encoding::BECH32M);
            }
        } else {
            // Unknown HRP
            result.isValid = false;
            result.isQuantum = false;
        }
        
        return result;
    }
    
    // Not a Bech32/Bech32m address - could be Base58
    // For Base58 addresses, we return isQuantum = false to route to ECDSA
    // Requirements: 3.7 (legacy address routing to ECDSA verification)
    result.isValid = false;  // Let the caller handle Base58 decoding
    result.isQuantum = false;
    result.witnessVersion = -1;
    
    return result;
}

bool IsQuantumAddress(const std::string& address, const CChainParams& params)
{
    DecodedAddress decoded = DecodeAddress(address, params);
    return decoded.isValid && decoded.isQuantum;
}

} // namespace address
