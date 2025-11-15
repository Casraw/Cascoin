// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/softfork.h>
#include <cvm/cvm.h>
#include <cvm/cvmdb.h>
#include <consensus/params.h>
#include <script/script.h>
#include <script/standard.h>
#include <hash.h>
#include <util.h>
#include <streams.h>

namespace CVM {

CScript BuildCVMOpReturn(CVMOpType opType, const std::vector<uint8_t>& data) {
    CScript script;
    script << OP_RETURN;
    
    // Add CVM magic bytes
    script << CVM_MAGIC;
    
    // Add operation type as single-byte vector (CScript << will add length prefix automatically)
    std::vector<unsigned char> opTypeVec(1, static_cast<unsigned char>(opType));
    script << opTypeVec;
    
    // Add actual data (limited by MAX_OP_RETURN_SIZE)
    if (data.size() > MAX_OP_RETURN_SIZE - CVM_MAGIC.size() - 1) {
        LogPrintf("CVM: Warning: OP_RETURN data truncated\n");
        std::vector<uint8_t> truncated(data.begin(), data.begin() + MAX_OP_RETURN_SIZE - CVM_MAGIC.size() - 1);
        script << truncated;
    } else {
        script << data;
    }
    
    return script;
}

bool IsCVMOpReturn(const CTxOut& txout) {
    if (!txout.scriptPubKey.IsUnspendable()) {
        return false;
    }
    
    // Check for OP_RETURN
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    if (!txout.scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    
    // Check for CVM magic
    std::vector<uint8_t> vch;
    if (!txout.scriptPubKey.GetOp(pc, opcode, vch)) {
        return false;
    }
    
    if (vch.size() != CVM_MAGIC.size()) {
        return false;
    }
    
    return std::equal(vch.begin(), vch.end(), CVM_MAGIC.begin());
}

bool ParseCVMOpReturn(const CTxOut& txout, CVMOpType& opType, std::vector<uint8_t>& data) {
    if (!IsCVMOpReturn(txout)) {
        return false;
    }
    
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<uint8_t> vch;
    
    // Skip OP_RETURN
    txout.scriptPubKey.GetOp(pc, opcode);
    
    // Skip CVM magic
    txout.scriptPubKey.GetOp(pc, opcode, vch);
    
    // Get operation type
    if (!txout.scriptPubKey.GetOp(pc, opcode, vch) || vch.size() != 1) {
        return false;
    }
    opType = static_cast<CVMOpType>(vch[0]);
    
    // Get remaining data
    data.clear();
    while (txout.scriptPubKey.GetOp(pc, opcode, vch)) {
        data.insert(data.end(), vch.begin(), vch.end());
    }
    
    return true;
}

int FindCVMOpReturn(const CTransaction& tx) {
    for (size_t i = 0; i < tx.vout.size(); i++) {
        if (IsCVMOpReturn(tx.vout[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// CVMDeployData serialization
std::vector<uint8_t> CVMDeployData::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << codeHash;
    ss << gasLimit;
    ss << static_cast<uint8_t>(format);
    ss << metadata;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool CVMDeployData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> codeHash;
        ss >> gasLimit;
        
        // Try to read format byte (for backward compatibility)
        if (ss.size() > 0) {
            uint8_t formatByte;
            ss >> formatByte;
            // Map old format values to new enum
            if (formatByte == 0x01) format = BytecodeFormat::CVM_NATIVE;
            else if (formatByte == 0x02) format = BytecodeFormat::EVM_BYTECODE;
            else format = BytecodeFormat::UNKNOWN;
        } else {
            format = BytecodeFormat::UNKNOWN;  // Default for old transactions
        }
        
        ss >> metadata;
        return true;
    } catch (...) {
        return false;
    }
}

// CVMCallData serialization
std::vector<uint8_t> CVMCallData::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << contractAddress;
    ss << gasLimit;
    ss << static_cast<uint8_t>(format);
    ss << callData;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool CVMCallData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> contractAddress;
        ss >> gasLimit;
        
        // Try to read format byte (for backward compatibility)
        if (ss.size() > 0) {
            uint8_t formatByte;
            ss >> formatByte;
            // Map old format values to new enum
            if (formatByte == 0x01) format = BytecodeFormat::CVM_NATIVE;
            else if (formatByte == 0x02) format = BytecodeFormat::EVM_BYTECODE;
            else format = BytecodeFormat::UNKNOWN;
        } else {
            format = BytecodeFormat::UNKNOWN;  // Default for old transactions
        }
        
        ss >> callData;
        return true;
    } catch (...) {
        return false;
    }
}

// CVMReputationData serialization
std::vector<uint8_t> CVMReputationData::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << targetAddress;
    ss << voteValue;
    ss << timestamp;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool CVMReputationData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> targetAddress;
        ss >> voteValue;
        ss >> timestamp;
        return true;
    } catch (...) {
        return false;
    }
}

bool IsCVMSoftForkActive(int height, const Consensus::Params& params) {
    return height >= params.cvmActivationHeight || 
           height >= params.asrsActivationHeight;
}

bool ValidateCVMSoftFork(const CTransaction& tx, int height, 
                         const Consensus::Params& params,
                         std::string& error) {
    // Check if soft fork is active
    if (!IsCVMSoftForkActive(height, params)) {
        return true; // Not active yet, accept all
    }
    
    // Find CVM OP_RETURN
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        return true; // Not a CVM transaction, accept
    }
    
    // Parse CVM data
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        error = "Invalid CVM OP_RETURN format";
        return false;
    }
    
    // Validate based on operation type
    switch (opType) {
        case CVMOpType::CONTRACT_DEPLOY: {
            if (height < params.cvmActivationHeight) {
                error = "CVM not active yet";
                return false;
            }
            
            CVMDeployData deployData;
            if (!deployData.Deserialize(data)) {
                error = "Invalid contract deployment data";
                return false;
            }
            
            // Check gas limit
            if (deployData.gasLimit > params.cvmMaxGasPerTx) {
                error = "Gas limit exceeds maximum";
                return false;
            }
            
            // Note: Actual bytecode validation happens when bytecode is provided
            // (could be in witness data or separate storage)
            break;
        }
        
        case CVMOpType::CONTRACT_CALL: {
            if (height < params.cvmActivationHeight) {
                error = "CVM not active yet";
                return false;
            }
            
            CVMCallData callData;
            if (!callData.Deserialize(data)) {
                error = "Invalid contract call data";
                return false;
            }
            
            // Check gas limit
            if (callData.gasLimit > params.cvmMaxGasPerTx) {
                error = "Gas limit exceeds maximum";
                return false;
            }
            
            break;
        }
        
        case CVMOpType::REPUTATION_VOTE: {
            if (height < params.asrsActivationHeight) {
                error = "ASRS not active yet";
                return false;
            }
            
            CVMReputationData repData;
            if (!repData.Deserialize(data)) {
                error = "Invalid reputation vote data";
                return false;
            }
            
            // Check vote range
            if (repData.voteValue < -100 || repData.voteValue > 100) {
                error = "Vote value out of range";
                return false;
            }
            
            break;
        }
        
        default:
            error = "Unknown CVM operation type";
            return false;
    }
    
    // CRITICAL: For soft fork, we only log errors but don't reject
    // This allows old nodes to accept the block
    if (!error.empty()) {
        LogPrintf("CVM Soft Fork Validation Warning: %s (tx: %s)\n", 
                  error, tx.GetHash().ToString());
        // Don't return false - let it pass for soft fork compatibility
        error.clear();
    }
    
    return true;
}

// Web-of-Trust Serialize/Deserialize implementations

std::vector<uint8_t> CVMTrustEdgeData::Serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize fromAddress (20 bytes)
    result.insert(result.end(), fromAddress.begin(), fromAddress.end());
    
    // Serialize toAddress (20 bytes)
    result.insert(result.end(), toAddress.begin(), toAddress.end());
    
    // Serialize weight (2 bytes, little-endian)
    result.push_back(weight & 0xFF);
    result.push_back((weight >> 8) & 0xFF);
    
    // Serialize bondAmount (8 bytes, little-endian)
    int64_t bondValue = bondAmount;
    for (int i = 0; i < 8; i++) {
        result.push_back((bondValue >> (i * 8)) & 0xFF);
    }
    
    // Serialize timestamp (4 bytes, little-endian)
    for (int i = 0; i < 4; i++) {
        result.push_back((timestamp >> (i * 8)) & 0xFF);
    }
    
    return result;
}

bool CVMTrustEdgeData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        if (data.size() < 54) { // 20+20+2+8+4
            return false;
        }
        
        size_t offset = 0;
        
        // Deserialize fromAddress (20 bytes)
        std::copy(data.begin() + offset, data.begin() + offset + 20, fromAddress.begin());
        offset += 20;
        
        // Deserialize toAddress (20 bytes)
        std::copy(data.begin() + offset, data.begin() + offset + 20, toAddress.begin());
        offset += 20;
        
        // Deserialize weight (2 bytes, little-endian)
        weight = data[offset] | (data[offset+1] << 8);
        offset += 2;
        
        // Deserialize bondAmount (8 bytes, little-endian)
        int64_t bondValue = 0;
        for (int i = 0; i < 8; i++) {
            bondValue |= (int64_t(data[offset+i]) << (i * 8));
        }
        bondAmount = bondValue;
        offset += 8;
        
        // Deserialize timestamp (4 bytes, little-endian)
        timestamp = data[offset] | (data[offset+1] << 8) | 
                   (data[offset+2] << 16) | (data[offset+3] << 24);
        
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CVM: Failed to deserialize CVMTrustEdgeData: %s\n", e.what());
        return false;
    }
}

std::vector<uint8_t> CVMBondedVoteData::Serialize() const {
    std::vector<uint8_t> result;
    
    // Serialize voter (20 bytes)
    result.insert(result.end(), voter.begin(), voter.end());
    
    // Serialize target (20 bytes)
    result.insert(result.end(), target.begin(), target.end());
    
    // Serialize voteValue (2 bytes, little-endian)
    result.push_back(voteValue & 0xFF);
    result.push_back((voteValue >> 8) & 0xFF);
    
    // Serialize bondAmount (8 bytes, little-endian)
    int64_t bondValue = bondAmount;
    for (int i = 0; i < 8; i++) {
        result.push_back((bondValue >> (i * 8)) & 0xFF);
    }
    
    // Serialize timestamp (4 bytes, little-endian)
    for (int i = 0; i < 4; i++) {
        result.push_back((timestamp >> (i * 8)) & 0xFF);
    }
    
    return result;
}

bool CVMBondedVoteData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        if (data.size() < 54) { // 20+20+2+8+4
            return false;
        }
        
        size_t offset = 0;
        
        // Deserialize voter (20 bytes)
        std::copy(data.begin() + offset, data.begin() + offset + 20, voter.begin());
        offset += 20;
        
        // Deserialize target (20 bytes)
        std::copy(data.begin() + offset, data.begin() + offset + 20, target.begin());
        offset += 20;
        
        // Deserialize voteValue (2 bytes, little-endian)
        voteValue = data[offset] | (data[offset+1] << 8);
        offset += 2;
        
        // Deserialize bondAmount (8 bytes, little-endian)
        int64_t bondValue = 0;
        for (int i = 0; i < 8; i++) {
            bondValue |= (int64_t(data[offset+i]) << (i * 8));
        }
        bondAmount = bondValue;
        offset += 8;
        
        // Deserialize timestamp (4 bytes, little-endian)
        timestamp = data[offset] | (data[offset+1] << 8) | 
                   (data[offset+2] << 16) | (data[offset+3] << 24);
        
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CVM: Failed to deserialize CVMBondedVoteData: %s\n", e.what());
        return false;
    }
}

std::vector<uint8_t> CVMDAODisputeData::Serialize() const {
    // Keep OP_RETURN payload <= 80 bytes: omit human-readable reason here.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << originalVoteTxHash;   // 32 bytes
    ss << challenger;           // 20 bytes
    ss << challengeBond;        // 8 bytes
    ss << timestamp;            // 4 bytes
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool CVMDAODisputeData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> originalVoteTxHash;
        ss >> challenger;
        ss >> challengeBond;
        ss >> timestamp;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CVM: Failed to deserialize CVMDAODisputeData: %s\n", e.what());
        return false;
    }
}

std::vector<uint8_t> CVMDAOVoteData::Serialize() const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << disputeId;
    ss << daoMember;
    ss << supportSlash;
    ss << stake;
    ss << timestamp;
    return std::vector<uint8_t>(ss.begin(), ss.end());
}

bool CVMDAOVoteData::Deserialize(const std::vector<uint8_t>& data) {
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> disputeId;
        ss >> daoMember;
        ss >> supportSlash;
        ss >> stake;
        ss >> timestamp;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("CVM: Failed to deserialize CVMDAOVoteData: %s\n", e.what());
        return false;
    }
}

// EVM Transaction Validation

bool ValidateEVMDeployment(const CTransaction& tx, 
                          const CVMDeployData& deployData,
                          int height,
                          std::string& error)
{
    // Check bytecode format
    if (deployData.format == BytecodeFormat::CVM_NATIVE) {
        error = "EVM deployment with CVM format specified";
        return false;
    }
    
    // Check gas limit (EVM uses same limits as CVM)
    if (deployData.gasLimit == 0) {
        error = "Gas limit cannot be zero";
        return false;
    }
    
    if (deployData.gasLimit > MAX_GAS_PER_TX) {
        error = "Gas limit exceeds maximum (" + std::to_string(MAX_GAS_PER_TX) + ")";
        return false;
    }
    
    // Check bytecode hash
    if (deployData.codeHash.IsNull()) {
        error = "Code hash cannot be null";
        return false;
    }
    
    // Validate bytecode if provided
    if (!deployData.bytecode.empty()) {
        // Check bytecode size (24KB max)
        const size_t MAX_CONTRACT_SIZE = 24 * 1024;
        if (deployData.bytecode.size() > MAX_CONTRACT_SIZE) {
            error = "Bytecode exceeds maximum size (" + std::to_string(MAX_CONTRACT_SIZE) + " bytes)";
            return false;
        }
        
        // Verify code hash matches
        uint256 computedHash = Hash(deployData.bytecode.begin(), deployData.bytecode.end());
        if (computedHash != deployData.codeHash) {
            error = "Code hash mismatch";
            return false;
        }
        
        // Auto-detect format if not specified
        if (deployData.format == BytecodeFormat::UNKNOWN) {
            // Will be detected by BytecodeDetector during execution
            LogPrintf("CVM: Auto-detecting bytecode format for deployment\n");
        }
    }
    
    // Check reputation requirement (if database available)
    if (g_cvmdb) {
        // Get deployer address from transaction inputs
        // Note: This is a simplified check - full implementation would extract actual deployer
        uint8_t minReputation = 50;  // Minimum reputation for contract deployment
        
        // TODO: Extract deployer address from tx inputs and check reputation
        // For now, we'll allow deployment (reputation check can be added later)
    }
    
    return true;
}

bool ValidateEVMCall(const CTransaction& tx,
                    const CVMCallData& callData,
                    int height,
                    std::string& error)
{
    // Check contract address
    if (callData.contractAddress.IsNull()) {
        error = "Contract address cannot be null";
        return false;
    }
    
    // Check gas limit
    if (callData.gasLimit == 0) {
        error = "Gas limit cannot be zero";
        return false;
    }
    
    if (callData.gasLimit > MAX_GAS_PER_TX) {
        error = "Gas limit exceeds maximum (" + std::to_string(MAX_GAS_PER_TX) + ")";
        return false;
    }
    
    // Check if contract exists (if database available)
    if (g_cvmdb) {
        Contract contract;
        if (!g_cvmdb->ReadContract(callData.contractAddress, contract)) {
            error = "Contract does not exist";
            return false;
        }
        
        // Verify format matches if specified
        if (callData.format != BytecodeFormat::UNKNOWN) {
            // TODO: Check contract format matches expected format
            // This would require storing format in contract metadata
        }
    }
    
    return true;
}

bool IsEVMTransaction(const CTransaction& tx)
{
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        return false;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        return false;
    }
    
    // Check for explicit EVM operations
    if (opType == CVMOpType::EVM_DEPLOY || opType == CVMOpType::EVM_CALL) {
        return true;
    }
    
    // Check for generic operations with EVM format
    if (opType == CVMOpType::CONTRACT_DEPLOY) {
        CVMDeployData deployData;
        if (deployData.Deserialize(data)) {
            return deployData.format == BytecodeFormat::EVM_BYTECODE;
        }
    } else if (opType == CVMOpType::CONTRACT_CALL) {
        CVMCallData callData;
        if (callData.Deserialize(data)) {
            return callData.format == BytecodeFormat::EVM_BYTECODE;
        }
    }
    
    return false;
}

BytecodeFormat GetTransactionBytecodeFormat(const CTransaction& tx)
{
    int cvmOutputIndex = FindCVMOpReturn(tx);
    if (cvmOutputIndex < 0) {
        return BytecodeFormat::UNKNOWN;
    }
    
    CVMOpType opType;
    std::vector<uint8_t> data;
    if (!ParseCVMOpReturn(tx.vout[cvmOutputIndex], opType, data)) {
        return BytecodeFormat::UNKNOWN;
    }
    
    // Check operation type
    if (opType == CVMOpType::EVM_DEPLOY || opType == CVMOpType::EVM_CALL) {
        return BytecodeFormat::EVM_BYTECODE;
    }
    
    // Parse data to get format
    if (opType == CVMOpType::CONTRACT_DEPLOY) {
        CVMDeployData deployData;
        if (deployData.Deserialize(data)) {
            return deployData.format;
        }
    } else if (opType == CVMOpType::CONTRACT_CALL) {
        CVMCallData callData;
        if (callData.Deserialize(data)) {
            return callData.format;
        }
    }
    
    return BytecodeFormat::UNKNOWN;
}

bool ParseCVMDeployData(const std::vector<uint8_t>& data, CVMDeployData& deployData) {
    return deployData.Deserialize(data);
}

bool ParseCVMCallData(const std::vector<uint8_t>& data, CVMCallData& callData) {
    return callData.Deserialize(data);
}

} // namespace CVM

