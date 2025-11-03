// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/contract.h>
#include <cvm/opcodes.h>
#include <hash.h>
#include <utilstrencodings.h>
#include <streams.h>

namespace CVM {

// Contract transaction marker in OP_RETURN
static const std::string CVM_MARKER = "CVM";
static const uint8_t CVM_VERSION = 0x01;

std::vector<uint8_t> ContractDeployTx::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool ContractDeployTx::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> *this;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<uint8_t> ContractCallTx::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool ContractCallTx::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> *this;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

ContractTxType GetContractTxType(const CTransaction& tx) {
    // Check for OP_RETURN output with CVM marker
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey.size() > 0 && output.scriptPubKey[0] == OP_RETURN) {
            // Parse OP_RETURN data
            std::vector<uint8_t> data(output.scriptPubKey.begin() + 1, output.scriptPubKey.end());
            
            if (data.size() < CVM_MARKER.size() + 2) {
                continue;
            }
            
            // Check marker
            std::string marker(data.begin(), data.begin() + CVM_MARKER.size());
            if (marker != CVM_MARKER) {
                continue;
            }
            
            // Check version
            if (data[CVM_MARKER.size()] != CVM_VERSION) {
                continue;
            }
            
            // Get type
            uint8_t type = data[CVM_MARKER.size() + 1];
            return static_cast<ContractTxType>(type);
        }
    }
    
    return ContractTxType::NONE;
}

bool ParseContractDeployTx(const CTransaction& tx, ContractDeployTx& deployTx) {
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey.size() > 0 && output.scriptPubKey[0] == OP_RETURN) {
            std::vector<uint8_t> data(output.scriptPubKey.begin() + 1, output.scriptPubKey.end());
            
            if (data.size() < CVM_MARKER.size() + 2) {
                continue;
            }
            
            std::string marker(data.begin(), data.begin() + CVM_MARKER.size());
            if (marker != CVM_MARKER) {
                continue;
            }
            
            if (data[CVM_MARKER.size()] != CVM_VERSION) {
                continue;
            }
            
            if (data[CVM_MARKER.size() + 1] != static_cast<uint8_t>(ContractTxType::DEPLOY)) {
                continue;
            }
            
            // Extract contract data
            std::vector<uint8_t> contractData(data.begin() + CVM_MARKER.size() + 2, data.end());
            return deployTx.Deserialize(contractData);
        }
    }
    
    return false;
}

bool ParseContractCallTx(const CTransaction& tx, ContractCallTx& callTx) {
    for (const auto& output : tx.vout) {
        if (output.scriptPubKey.size() > 0 && output.scriptPubKey[0] == OP_RETURN) {
            std::vector<uint8_t> data(output.scriptPubKey.begin() + 1, output.scriptPubKey.end());
            
            if (data.size() < CVM_MARKER.size() + 2) {
                continue;
            }
            
            std::string marker(data.begin(), data.begin() + CVM_MARKER.size());
            if (marker != CVM_MARKER) {
                continue;
            }
            
            if (data[CVM_MARKER.size()] != CVM_VERSION) {
                continue;
            }
            
            if (data[CVM_MARKER.size() + 1] != static_cast<uint8_t>(ContractTxType::CALL)) {
                continue;
            }
            
            // Extract call data
            std::vector<uint8_t> contractData(data.begin() + CVM_MARKER.size() + 2, data.end());
            return callTx.Deserialize(contractData);
        }
    }
    
    return false;
}

bool IsContractTransaction(const CTransaction& tx) {
    return GetContractTxType(tx) != ContractTxType::NONE;
}

uint160 GenerateContractAddress(const uint160& deployerAddr, uint64_t nonce) {
    // Similar to Ethereum: hash(deployer_address + nonce)
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << deployerAddr << nonce;
    
    uint256 hash = Hash(ss.begin(), ss.end());
    
    // Take first 160 bits
    uint160 contractAddr;
    memcpy(contractAddr.begin(), hash.begin(), 20);
    
    return contractAddr;
}

bool ValidateContractCode(const std::vector<uint8_t>& code, std::string& error) {
    if (code.empty()) {
        error = "Empty contract code";
        return false;
    }
    
    if (code.size() > MAX_CODE_SIZE) {
        error = "Contract code exceeds maximum size";
        return false;
    }
    
    // Validate opcodes
    size_t pc = 0;
    while (pc < code.size()) {
        uint8_t opcodeByte = code[pc];
        
        if (!IsValidOpCode(opcodeByte)) {
            error = "Invalid opcode at position " + std::to_string(pc);
            return false;
        }
        
        OpCode opcode = static_cast<OpCode>(opcodeByte);
        
        // Handle PUSH specially
        if (opcode == OpCode::OP_PUSH) {
            if (pc + 1 >= code.size()) {
                error = "PUSH without size byte at position " + std::to_string(pc);
                return false;
            }
            
            uint8_t size = code[pc + 1];
            if (size == 0 || size > 32) {
                error = "Invalid PUSH size at position " + std::to_string(pc);
                return false;
            }
            
            if (pc + 2 + size > code.size()) {
                error = "PUSH size exceeds code length at position " + std::to_string(pc);
                return false;
            }
            
            pc += 2 + size;
        } else {
            pc++;
        }
    }
    
    return true;
}

} // namespace CVM

