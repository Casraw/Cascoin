// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/bytecode_detector.h>
#include <cvm/opcodes.h>
#include <hash.h>
#include <util.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace CVM {

// Static pattern definitions
const std::vector<uint8_t> BytecodeDetector::EVM_CONSTRUCTOR_PATTERN = {0x60, 0x80, 0x60, 0x40}; // PUSH1 0x80 PUSH1 0x40
const std::vector<uint8_t> BytecodeDetector::EVM_FUNCTION_SELECTOR_PATTERN = {0x63}; // PUSH4 (function selector)
const std::vector<uint8_t> BytecodeDetector::CVM_HEADER_PATTERN = {0x43, 0x56, 0x4D}; // "CVM" magic bytes
const std::vector<uint8_t> BytecodeDetector::CVM_TRUST_PATTERN = {0x70, 0x71, 0x72}; // Trust-related opcodes
const std::vector<uint8_t> BytecodeDetector::HYBRID_SEPARATOR_PATTERN = {0xFF, 0xEE, 0xDD, 0xCC}; // Hybrid separator

// EVM opcodes (subset for detection)
const std::set<uint8_t> BytecodeDetector::EVM_OPCODES = {
    0x00, // STOP
    0x01, // ADD
    0x02, // MUL
    0x03, // SUB
    0x04, // DIV
    0x05, // SDIV
    0x06, // MOD
    0x07, // SMOD
    0x08, // ADDMOD
    0x09, // MULMOD
    0x0a, // EXP
    0x0b, // SIGNEXTEND
    0x10, // LT
    0x11, // GT
    0x12, // SLT
    0x13, // SGT
    0x14, // EQ
    0x15, // ISZERO
    0x16, // AND
    0x17, // OR
    0x18, // XOR
    0x19, // NOT
    0x1a, // BYTE
    0x1b, // SHL
    0x1c, // SHR
    0x1d, // SAR
    0x20, // SHA3
    0x30, // ADDRESS
    0x31, // BALANCE
    0x32, // ORIGIN
    0x33, // CALLER
    0x34, // CALLVALUE
    0x35, // CALLDATALOAD
    0x36, // CALLDATASIZE
    0x37, // CALLDATACOPY
    0x38, // CODESIZE
    0x39, // CODECOPY
    0x3a, // GASPRICE
    0x3b, // EXTCODESIZE
    0x3c, // EXTCODECOPY
    0x3d, // RETURNDATASIZE
    0x3e, // RETURNDATACOPY
    0x3f, // EXTCODEHASH
    0x40, // BLOCKHASH
    0x41, // COINBASE
    0x42, // TIMESTAMP
    0x43, // NUMBER
    0x44, // DIFFICULTY
    0x45, // GASLIMIT
    0x46, // CHAINID
    0x47, // SELFBALANCE
    0x48, // BASEFEE
    0x50, // POP
    0x51, // MLOAD
    0x52, // MSTORE
    0x53, // MSTORE8
    0x54, // SLOAD
    0x55, // SSTORE
    0x56, // JUMP
    0x57, // JUMPI
    0x58, // PC
    0x59, // MSIZE
    0x5a, // GAS
    0x5b, // JUMPDEST
    0x80, // DUP1
    0x81, // DUP2
    0x82, // DUP3
    0x83, // DUP4
    0x84, // DUP5
    0x85, // DUP6
    0x86, // DUP7
    0x87, // DUP8
    0x88, // DUP9
    0x89, // DUP10
    0x8a, // DUP11
    0x8b, // DUP12
    0x8c, // DUP13
    0x8d, // DUP14
    0x8e, // DUP15
    0x8f, // DUP16
    0x90, // SWAP1
    0x91, // SWAP2
    0x92, // SWAP3
    0x93, // SWAP4
    0x94, // SWAP5
    0x95, // SWAP6
    0x96, // SWAP7
    0x97, // SWAP8
    0x98, // SWAP9
    0x99, // SWAP10
    0x9a, // SWAP11
    0x9b, // SWAP12
    0x9c, // SWAP13
    0x9d, // SWAP14
    0x9e, // SWAP15
    0x9f, // SWAP16
    0xa0, // LOG0
    0xa1, // LOG1
    0xa2, // LOG2
    0xa3, // LOG3
    0xa4, // LOG4
    0xf0, // CREATE
    0xf1, // CALL
    0xf2, // CALLCODE
    0xf3, // RETURN
    0xf4, // DELEGATECALL
    0xf5, // CREATE2
    0xfa, // STATICCALL
    0xfd, // REVERT
    0xfe, // INVALID
    0xff  // SELFDESTRUCT
};

// EVM PUSH opcodes (0x60-0x7f)
const std::set<uint8_t> BytecodeDetector::EVM_PUSH_OPCODES = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f
};

// EVM jump opcodes
const std::set<uint8_t> BytecodeDetector::EVM_JUMP_OPCODES = {
    0x56, // JUMP
    0x57, // JUMPI
    0x5b  // JUMPDEST
};

// CVM opcodes (from opcodes.h)
const std::set<uint8_t> BytecodeDetector::CVM_OPCODES = {
    0x01, // OP_PUSH
    0x02, // OP_POP
    0x03, // OP_DUP
    0x04, // OP_SWAP
    0x10, // OP_ADD
    0x11, // OP_SUB
    0x12, // OP_MUL
    0x13, // OP_DIV
    0x14, // OP_MOD
    0x20, // OP_AND
    0x21, // OP_OR
    0x22, // OP_XOR
    0x23, // OP_NOT
    0x30, // OP_EQ
    0x31, // OP_NE
    0x32, // OP_LT
    0x33, // OP_GT
    0x34, // OP_LE
    0x35, // OP_GE
    0x40, // OP_JUMP
    0x41, // OP_JUMPI
    0x42, // OP_CALL
    0x43, // OP_RETURN
    0x44, // OP_STOP
    0x50, // OP_SLOAD
    0x51, // OP_SSTORE
    0x60, // OP_SHA256
    0x61, // OP_VERIFY_SIG
    0x62, // OP_PUBKEY
    0x70, // OP_ADDRESS
    0x71, // OP_BALANCE
    0x72, // OP_CALLER
    0x73, // OP_CALLVALUE
    0x74, // OP_TIMESTAMP
    0x75, // OP_BLOCKHASH
    0x76, // OP_BLOCKHEIGHT
    0x80, // OP_GAS
    0x90, // OP_LOG
    0x91  // OP_REVERT
};

// CVM register opcodes (subset)
const std::set<uint8_t> BytecodeDetector::CVM_REGISTER_OPCODES = {
    0x01, // OP_PUSH (register-based)
    0x02, // OP_POP
    0x03, // OP_DUP
    0x04  // OP_SWAP
};

// CVM trust-specific opcodes
const std::set<uint8_t> BytecodeDetector::CVM_TRUST_OPCODES = {
    0x70, // OP_ADDRESS (trust context)
    0x71, // OP_BALANCE (trust-weighted)
    0x72  // OP_CALLER (trust verification)
};

BytecodeDetector::BytecodeDetector() 
    : confidence_threshold(0.7), strict_validation(false) {
    ResetStats();
}

BytecodeDetector::~BytecodeDetector() {
}

BytecodeDetectionResult BytecodeDetector::DetectFormat(const std::vector<uint8_t>& bytecode) {
    return DetectFormat(bytecode.data(), bytecode.size());
}

BytecodeDetectionResult BytecodeDetector::DetectFormat(const uint8_t* data, size_t size) {
    BytecodeDetectionResult result;
    
    if (!data || size == 0) {
        result.reason = "Empty bytecode";
        UpdateStats(result);
        return result;
    }
    
    std::vector<uint8_t> bytecode(data, data + size);
    result.estimated_size = size;
    
    // Check for hybrid contract first
    if (IsHybridContract(bytecode)) {
        result.format = BytecodeFormat::HYBRID;
        result.confidence = 0.95; // High confidence for hybrid detection
        result.reason = "Hybrid contract with format separators detected";
        result.is_valid = ValidateEVMBytecode(ExtractEVMPortion(bytecode)) && 
                         ValidateCVMBytecode(ExtractCVMPortion(bytecode));
    }
    // Check for EVM bytecode
    else if (IsEVMBytecode(bytecode)) {
        result.format = BytecodeFormat::EVM_BYTECODE;
        result.confidence = CalculateEVMConfidence(bytecode);
        result.reason = "EVM opcodes and patterns detected";
        result.is_valid = strict_validation ? ValidateEVMBytecode(bytecode) : true;
    }
    // Check for CVM bytecode
    else if (IsCVMBytecode(bytecode)) {
        result.format = BytecodeFormat::CVM_NATIVE;
        result.confidence = CalculateCVMConfidence(bytecode);
        result.reason = "CVM register-based opcodes detected";
        result.is_valid = strict_validation ? ValidateCVMBytecode(bytecode) : true;
    }
    else {
        result.format = BytecodeFormat::UNKNOWN;
        result.confidence = 0.0;
        result.reason = "No recognizable bytecode patterns found";
        result.is_valid = false;
    }
    
    UpdateStats(result);
    return result;
}

bool BytecodeDetector::IsEVMBytecode(const std::vector<uint8_t>& bytecode) {
    if (bytecode.empty()) return false;
    
    // Check for EVM-specific patterns
    bool has_evm_opcodes = HasEVMOpcodes(bytecode);
    bool has_push_pattern = HasEVMPushPattern(bytecode);
    bool has_jump_dest = HasEVMJumpDestinations(bytecode);
    
    // EVM bytecode typically has PUSH opcodes and valid opcodes
    return has_evm_opcodes && (has_push_pattern || has_jump_dest);
}

bool BytecodeDetector::IsCVMBytecode(const std::vector<uint8_t>& bytecode) {
    if (bytecode.empty()) return false;
    
    // Check for CVM-specific patterns
    bool has_cvm_opcodes = HasCVMOpcodes(bytecode);
    bool has_register_pattern = HasCVMRegisterPattern(bytecode);
    bool has_trust_opcodes = HasCVMTrustOpcodes(bytecode);
    
    // CVM bytecode has register-based patterns or trust opcodes
    return has_cvm_opcodes && (has_register_pattern || has_trust_opcodes);
}

bool BytecodeDetector::IsHybridContract(const std::vector<uint8_t>& bytecode) {
    if (bytecode.size() < 100) return false; // Too small for hybrid
    
    return HasHybridMarkers(bytecode) || HasFormatSeparators(bytecode);
}

bool BytecodeDetector::ValidateEVMBytecode(const std::vector<uint8_t>& bytecode) {
    if (bytecode.empty()) return false;
    
    // Basic EVM bytecode validation
    for (size_t i = 0; i < bytecode.size(); ) {
        uint8_t opcode = bytecode[i];
        
        // Check if it's a valid EVM opcode
        if (EVM_OPCODES.find(opcode) == EVM_OPCODES.end() && 
            EVM_PUSH_OPCODES.find(opcode) == EVM_PUSH_OPCODES.end()) {
            return false;
        }
        
        // Handle PUSH opcodes (they have immediate data)
        if (EVM_PUSH_OPCODES.find(opcode) != EVM_PUSH_OPCODES.end()) {
            uint8_t push_size = opcode - 0x5f; // PUSH1 = 0x60, so size = 1
            if (i + push_size >= bytecode.size()) {
                return false; // Not enough bytes for PUSH data
            }
            i += 1 + push_size;
        } else {
            i++;
        }
    }
    
    return true;
}

bool BytecodeDetector::ValidateCVMBytecode(const std::vector<uint8_t>& bytecode) {
    if (bytecode.empty()) return false;
    
    // Basic CVM bytecode validation
    for (size_t i = 0; i < bytecode.size(); ) {
        uint8_t opcode = bytecode[i];
        
        // Check if it's a valid CVM opcode
        if (CVM_OPCODES.find(opcode) == CVM_OPCODES.end()) {
            return false;
        }
        
        // Handle CVM PUSH opcode (has size byte + data)
        if (opcode == 0x01) { // OP_PUSH
            if (i + 1 >= bytecode.size()) {
                return false;
            }
            uint8_t size = bytecode[i + 1];
            if (size == 0 || size > 32 || i + 2 + size > bytecode.size()) {
                return false;
            }
            i += 2 + size;
        } else {
            i++;
        }
    }
    
    return true;
}

std::vector<uint8_t> BytecodeDetector::ExtractEVMPortion(const std::vector<uint8_t>& hybrid_bytecode) {
    size_t evm_start = FindEVMSection(hybrid_bytecode);
    if (evm_start == std::string::npos) {
        return {};
    }
    
    // Find end of EVM section (look for separator or CVM section)
    size_t evm_end = hybrid_bytecode.size();
    for (size_t i = evm_start; i < hybrid_bytecode.size() - HYBRID_SEPARATOR_PATTERN.size(); ++i) {
        if (MatchesPattern(hybrid_bytecode, HYBRID_SEPARATOR_PATTERN, i)) {
            evm_end = i;
            break;
        }
    }
    
    return std::vector<uint8_t>(hybrid_bytecode.begin() + evm_start, hybrid_bytecode.begin() + evm_end);
}

std::vector<uint8_t> BytecodeDetector::ExtractCVMPortion(const std::vector<uint8_t>& hybrid_bytecode) {
    size_t cvm_start = FindCVMSection(hybrid_bytecode);
    if (cvm_start == std::string::npos) {
        return {};
    }
    
    return std::vector<uint8_t>(hybrid_bytecode.begin() + cvm_start, hybrid_bytecode.end());
}

void BytecodeDetector::ResetStats() {
    stats = DetectionStats{};
}

// Private implementation methods

bool BytecodeDetector::HasEVMOpcodes(const std::vector<uint8_t>& bytecode) {
    size_t evm_opcode_count = 0;
    for (uint8_t byte : bytecode) {
        if (EVM_OPCODES.find(byte) != EVM_OPCODES.end() || 
            EVM_PUSH_OPCODES.find(byte) != EVM_PUSH_OPCODES.end()) {
            evm_opcode_count++;
        }
    }
    
    // At least 30% of bytes should be valid EVM opcodes
    return (double)evm_opcode_count / bytecode.size() >= 0.3;
}

bool BytecodeDetector::HasEVMPushPattern(const std::vector<uint8_t>& bytecode) {
    // Look for common EVM patterns like PUSH1 0x80 PUSH1 0x40 (memory setup)
    return MatchesPattern(bytecode, EVM_CONSTRUCTOR_PATTERN) ||
           std::any_of(bytecode.begin(), bytecode.end(), 
                      [](uint8_t b) { return EVM_PUSH_OPCODES.find(b) != EVM_PUSH_OPCODES.end(); });
}

bool BytecodeDetector::HasEVMJumpDestinations(const std::vector<uint8_t>& bytecode) {
    return std::any_of(bytecode.begin(), bytecode.end(),
                      [](uint8_t b) { return b == 0x5b; }); // JUMPDEST
}

double BytecodeDetector::CalculateEVMConfidence(const std::vector<uint8_t>& bytecode) {
    double confidence = 0.0;
    
    if (HasEVMOpcodes(bytecode)) confidence += 0.4;
    if (HasEVMPushPattern(bytecode)) confidence += 0.3;
    if (HasEVMJumpDestinations(bytecode)) confidence += 0.2;
    if (MatchesPattern(bytecode, EVM_CONSTRUCTOR_PATTERN)) confidence += 0.1;
    
    return std::min(1.0, confidence);
}

bool BytecodeDetector::HasCVMOpcodes(const std::vector<uint8_t>& bytecode) {
    size_t cvm_opcode_count = 0;
    for (uint8_t byte : bytecode) {
        if (CVM_OPCODES.find(byte) != CVM_OPCODES.end()) {
            cvm_opcode_count++;
        }
    }
    
    // At least 30% of bytes should be valid CVM opcodes
    return (double)cvm_opcode_count / bytecode.size() >= 0.3;
}

bool BytecodeDetector::HasCVMRegisterPattern(const std::vector<uint8_t>& bytecode) {
    // Look for CVM PUSH pattern (opcode + size + data)
    for (size_t i = 0; i < bytecode.size() - 2; ++i) {
        if (bytecode[i] == 0x01) { // OP_PUSH
            uint8_t size = bytecode[i + 1];
            if (size > 0 && size <= 32 && i + 2 + size <= bytecode.size()) {
                return true;
            }
        }
    }
    return false;
}

bool BytecodeDetector::HasCVMTrustOpcodes(const std::vector<uint8_t>& bytecode) {
    return std::any_of(bytecode.begin(), bytecode.end(),
                      [](uint8_t b) { return CVM_TRUST_OPCODES.find(b) != CVM_TRUST_OPCODES.end(); });
}

double BytecodeDetector::CalculateCVMConfidence(const std::vector<uint8_t>& bytecode) {
    double confidence = 0.0;
    
    if (HasCVMOpcodes(bytecode)) confidence += 0.4;
    if (HasCVMRegisterPattern(bytecode)) confidence += 0.3;
    if (HasCVMTrustOpcodes(bytecode)) confidence += 0.2;
    if (MatchesPattern(bytecode, CVM_HEADER_PATTERN)) confidence += 0.1;
    
    return std::min(1.0, confidence);
}

bool BytecodeDetector::HasHybridMarkers(const std::vector<uint8_t>& bytecode) {
    return MatchesPattern(bytecode, CVM_HEADER_PATTERN) && 
           (HasEVMOpcodes(bytecode) || HasCVMOpcodes(bytecode));
}

bool BytecodeDetector::HasFormatSeparators(const std::vector<uint8_t>& bytecode) {
    return MatchesPattern(bytecode, HYBRID_SEPARATOR_PATTERN);
}

size_t BytecodeDetector::FindEVMSection(const std::vector<uint8_t>& bytecode) {
    // Look for EVM section start (after hybrid header)
    for (size_t i = 0; i < bytecode.size() - 10; ++i) {
        if (HasEVMOpcodes(std::vector<uint8_t>(bytecode.begin() + i, bytecode.begin() + i + 10))) {
            return i;
        }
    }
    return std::string::npos;
}

size_t BytecodeDetector::FindCVMSection(const std::vector<uint8_t>& bytecode) {
    // Look for CVM section start (usually after separator)
    auto separator_pos = FindPatternOccurrences(bytecode, HYBRID_SEPARATOR_PATTERN);
    if (!separator_pos.empty()) {
        return separator_pos[0] + HYBRID_SEPARATOR_PATTERN.size();
    }
    
    // Fallback: look for CVM opcodes
    for (size_t i = 0; i < bytecode.size() - 10; ++i) {
        if (HasCVMOpcodes(std::vector<uint8_t>(bytecode.begin() + i, bytecode.begin() + i + 10))) {
            return i;
        }
    }
    return std::string::npos;
}

bool BytecodeDetector::MatchesPattern(const std::vector<uint8_t>& bytecode, const std::vector<uint8_t>& pattern, size_t offset) {
    if (offset + pattern.size() > bytecode.size()) {
        return false;
    }
    
    return std::equal(pattern.begin(), pattern.end(), bytecode.begin() + offset);
}

std::vector<size_t> BytecodeDetector::FindPatternOccurrences(const std::vector<uint8_t>& bytecode, const std::vector<uint8_t>& pattern) {
    std::vector<size_t> occurrences;
    
    for (size_t i = 0; i <= bytecode.size() - pattern.size(); ++i) {
        if (MatchesPattern(bytecode, pattern, i)) {
            occurrences.push_back(i);
        }
    }
    
    return occurrences;
}

void BytecodeDetector::UpdateStats(const BytecodeDetectionResult& result) {
    stats.total_detections++;
    
    switch (result.format) {
        case BytecodeFormat::EVM_BYTECODE:
            stats.evm_detected++;
            break;
        case BytecodeFormat::CVM_NATIVE:
            stats.cvm_detected++;
            break;
        case BytecodeFormat::HYBRID:
            stats.hybrid_detected++;
            break;
        default:
            stats.unknown_detected++;
            break;
    }
    
    // Update average confidence
    stats.average_confidence = ((stats.average_confidence * (stats.total_detections - 1)) + result.confidence) / stats.total_detections;
}

// Utility Functions

namespace BytecodeUtils {

std::string FormatToString(BytecodeFormat format) {
    switch (format) {
        case BytecodeFormat::CVM_NATIVE: return "CVM_NATIVE";
        case BytecodeFormat::EVM_BYTECODE: return "EVM_BYTECODE";
        case BytecodeFormat::HYBRID: return "HYBRID";
        default: return "UNKNOWN";
    }
}

BytecodeFormat StringToFormat(const std::string& format_str) {
    if (format_str == "CVM_NATIVE") return BytecodeFormat::CVM_NATIVE;
    if (format_str == "EVM_BYTECODE") return BytecodeFormat::EVM_BYTECODE;
    if (format_str == "HYBRID") return BytecodeFormat::HYBRID;
    return BytecodeFormat::UNKNOWN;
}

bool IsValidBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat expected_format) {
    BytecodeDetector detector;
    auto result = detector.DetectFormat(bytecode);
    return result.format == expected_format && result.is_valid;
}

size_t EstimateBytecodeComplexity(const std::vector<uint8_t>& bytecode) {
    // Simple complexity estimation based on unique opcodes and jumps
    std::set<uint8_t> unique_opcodes(bytecode.begin(), bytecode.end());
    size_t jump_count = std::count_if(bytecode.begin(), bytecode.end(),
                                     [](uint8_t b) { return b == 0x56 || b == 0x57 || b == 0x40 || b == 0x41; });
    
    return unique_opcodes.size() + jump_count * 2;
}

HybridContractLayout AnalyzeHybridLayout(const std::vector<uint8_t>& bytecode) {
    HybridContractLayout layout = {};
    
    BytecodeDetector detector;
    if (!detector.IsHybridContract(bytecode)) {
        return layout;
    }
    
    // Find sections
    layout.evm_offset = detector.FindEVMSection(bytecode);
    layout.cvm_offset = detector.FindCVMSection(bytecode);
    
    if (layout.evm_offset != std::string::npos && layout.cvm_offset != std::string::npos) {
        if (layout.evm_offset < layout.cvm_offset) {
            layout.evm_size = layout.cvm_offset - layout.evm_offset;
            layout.cvm_size = bytecode.size() - layout.cvm_offset;
        } else {
            layout.cvm_size = layout.evm_offset - layout.cvm_offset;
            layout.evm_size = bytecode.size() - layout.evm_offset;
        }
    }
    
    layout.header_size = std::min(layout.evm_offset, layout.cvm_offset);
    layout.has_metadata = layout.header_size > 0;
    
    return layout;
}

std::vector<uint8_t> CreateHybridContract(const std::vector<uint8_t>& evm_code, const std::vector<uint8_t>& cvm_code) {
    std::vector<uint8_t> hybrid;
    
    // Add hybrid header
    hybrid.insert(hybrid.end(), BytecodeDetector::CVM_HEADER_PATTERN.begin(), BytecodeDetector::CVM_HEADER_PATTERN.end());
    
    // Add EVM section
    hybrid.insert(hybrid.end(), evm_code.begin(), evm_code.end());
    
    // Add separator
    hybrid.insert(hybrid.end(), BytecodeDetector::HYBRID_SEPARATOR_PATTERN.begin(), BytecodeDetector::HYBRID_SEPARATOR_PATTERN.end());
    
    // Add CVM section
    hybrid.insert(hybrid.end(), cvm_code.begin(), cvm_code.end());
    
    return hybrid;
}

std::string DisassembleBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat format) {
    if (bytecode.empty()) {
        return "Empty bytecode";
    }
    
    std::string disassembly;
    disassembly += "Bytecode Disassembly (" + FormatToString(format) + ")\n";
    disassembly += "Size: " + std::to_string(bytecode.size()) + " bytes\n";
    disassembly += "----------------------------------------\n";
    
    if (format == BytecodeFormat::EVM_BYTECODE || format == BytecodeFormat::HYBRID) {
        // EVM disassembly
        for (size_t i = 0; i < bytecode.size(); ) {
            uint8_t opcode = bytecode[i];
            disassembly += strprintf("%04x: %02x ", i, opcode);
            
            // Decode EVM opcodes
            if (opcode >= 0x60 && opcode <= 0x7f) {
                // PUSH opcodes
                uint8_t push_size = opcode - 0x5f;
                disassembly += strprintf("PUSH%d ", push_size);
                
                if (i + push_size < bytecode.size()) {
                    disassembly += "0x";
                    for (uint8_t j = 0; j < push_size && i + 1 + j < bytecode.size(); ++j) {
                        disassembly += strprintf("%02x", bytecode[i + 1 + j]);
                    }
                    i += 1 + push_size;
                } else {
                    disassembly += "[INCOMPLETE]";
                    i++;
                }
            } else {
                // Other opcodes
                std::string opcode_name = "UNKNOWN";
                switch (opcode) {
                    case 0x00: opcode_name = "STOP"; break;
                    case 0x01: opcode_name = "ADD"; break;
                    case 0x02: opcode_name = "MUL"; break;
                    case 0x03: opcode_name = "SUB"; break;
                    case 0x04: opcode_name = "DIV"; break;
                    case 0x10: opcode_name = "LT"; break;
                    case 0x11: opcode_name = "GT"; break;
                    case 0x14: opcode_name = "EQ"; break;
                    case 0x15: opcode_name = "ISZERO"; break;
                    case 0x16: opcode_name = "AND"; break;
                    case 0x17: opcode_name = "OR"; break;
                    case 0x18: opcode_name = "XOR"; break;
                    case 0x19: opcode_name = "NOT"; break;
                    case 0x20: opcode_name = "SHA3"; break;
                    case 0x30: opcode_name = "ADDRESS"; break;
                    case 0x31: opcode_name = "BALANCE"; break;
                    case 0x33: opcode_name = "CALLER"; break;
                    case 0x34: opcode_name = "CALLVALUE"; break;
                    case 0x35: opcode_name = "CALLDATALOAD"; break;
                    case 0x36: opcode_name = "CALLDATASIZE"; break;
                    case 0x50: opcode_name = "POP"; break;
                    case 0x51: opcode_name = "MLOAD"; break;
                    case 0x52: opcode_name = "MSTORE"; break;
                    case 0x54: opcode_name = "SLOAD"; break;
                    case 0x55: opcode_name = "SSTORE"; break;
                    case 0x56: opcode_name = "JUMP"; break;
                    case 0x57: opcode_name = "JUMPI"; break;
                    case 0x5b: opcode_name = "JUMPDEST"; break;
                    case 0x80: case 0x81: case 0x82: case 0x83:
                    case 0x84: case 0x85: case 0x86: case 0x87:
                    case 0x88: case 0x89: case 0x8a: case 0x8b:
                    case 0x8c: case 0x8d: case 0x8e: case 0x8f:
                        opcode_name = strprintf("DUP%d", opcode - 0x7f);
                        break;
                    case 0x90: case 0x91: case 0x92: case 0x93:
                    case 0x94: case 0x95: case 0x96: case 0x97:
                    case 0x98: case 0x99: case 0x9a: case 0x9b:
                    case 0x9c: case 0x9d: case 0x9e: case 0x9f:
                        opcode_name = strprintf("SWAP%d", opcode - 0x8f);
                        break;
                    case 0xa0: opcode_name = "LOG0"; break;
                    case 0xa1: opcode_name = "LOG1"; break;
                    case 0xa2: opcode_name = "LOG2"; break;
                    case 0xa3: opcode_name = "LOG3"; break;
                    case 0xa4: opcode_name = "LOG4"; break;
                    case 0xf0: opcode_name = "CREATE"; break;
                    case 0xf1: opcode_name = "CALL"; break;
                    case 0xf2: opcode_name = "CALLCODE"; break;
                    case 0xf3: opcode_name = "RETURN"; break;
                    case 0xf4: opcode_name = "DELEGATECALL"; break;
                    case 0xf5: opcode_name = "CREATE2"; break;
                    case 0xfa: opcode_name = "STATICCALL"; break;
                    case 0xfd: opcode_name = "REVERT"; break;
                    case 0xfe: opcode_name = "INVALID"; break;
                    case 0xff: opcode_name = "SELFDESTRUCT"; break;
                }
                disassembly += opcode_name;
                i++;
            }
            disassembly += "\n";
        }
    } else if (format == BytecodeFormat::CVM_NATIVE) {
        // CVM disassembly
        for (size_t i = 0; i < bytecode.size(); ) {
            uint8_t opcode = bytecode[i];
            disassembly += strprintf("%04x: %02x ", i, opcode);
            
            std::string opcode_name = "UNKNOWN";
            switch (opcode) {
                case 0x01: opcode_name = "PUSH"; break;
                case 0x02: opcode_name = "POP"; break;
                case 0x03: opcode_name = "DUP"; break;
                case 0x04: opcode_name = "SWAP"; break;
                case 0x10: opcode_name = "ADD"; break;
                case 0x11: opcode_name = "SUB"; break;
                case 0x12: opcode_name = "MUL"; break;
                case 0x13: opcode_name = "DIV"; break;
                case 0x14: opcode_name = "MOD"; break;
                case 0x20: opcode_name = "AND"; break;
                case 0x21: opcode_name = "OR"; break;
                case 0x22: opcode_name = "XOR"; break;
                case 0x23: opcode_name = "NOT"; break;
                case 0x30: opcode_name = "EQ"; break;
                case 0x31: opcode_name = "NE"; break;
                case 0x32: opcode_name = "LT"; break;
                case 0x33: opcode_name = "GT"; break;
                case 0x40: opcode_name = "JUMP"; break;
                case 0x41: opcode_name = "JUMPI"; break;
                case 0x42: opcode_name = "CALL"; break;
                case 0x43: opcode_name = "RETURN"; break;
                case 0x44: opcode_name = "STOP"; break;
                case 0x50: opcode_name = "SLOAD"; break;
                case 0x51: opcode_name = "SSTORE"; break;
                case 0x60: opcode_name = "SHA256"; break;
                case 0x61: opcode_name = "VERIFY_SIG"; break;
                case 0x70: opcode_name = "ADDRESS"; break;
                case 0x71: opcode_name = "BALANCE"; break;
                case 0x72: opcode_name = "CALLER"; break;
                case 0x73: opcode_name = "CALLVALUE"; break;
                case 0x74: opcode_name = "TIMESTAMP"; break;
                case 0x75: opcode_name = "BLOCKHASH"; break;
                case 0x76: opcode_name = "BLOCKHEIGHT"; break;
                case 0x80: opcode_name = "GAS"; break;
                case 0x90: opcode_name = "LOG"; break;
                case 0x91: opcode_name = "REVERT"; break;
            }
            
            disassembly += opcode_name;
            
            // Handle PUSH opcode with data
            if (opcode == 0x01 && i + 1 < bytecode.size()) {
                uint8_t size = bytecode[i + 1];
                disassembly += strprintf(" size=%d data=0x", size);
                for (uint8_t j = 0; j < size && i + 2 + j < bytecode.size(); ++j) {
                    disassembly += strprintf("%02x", bytecode[i + 2 + j]);
                }
                i += 2 + size;
            } else {
                i++;
            }
            disassembly += "\n";
        }
    } else {
        disassembly += "Unknown format - raw hex dump:\n";
        disassembly += BytecodeToHex(bytecode);
    }
    
    return disassembly;
}

std::string BytecodeToHex(const std::vector<uint8_t>& bytecode) {
    std::string hex;
    for (uint8_t byte : bytecode) {
        hex += strprintf("%02x", byte);
    }
    return hex;
}

std::vector<uint8_t> HexToBytecode(const std::string& hex) {
    std::vector<uint8_t> bytecode;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        bytecode.push_back(byte);
    }
    return bytecode;
}

bool IsBytecodeOptimized(const std::vector<uint8_t>& bytecode, BytecodeFormat format) {
    if (bytecode.empty()) {
        return false;
    }
    
    // Check for common optimization patterns
    bool has_optimizations = false;
    
    if (format == BytecodeFormat::EVM_BYTECODE || format == BytecodeFormat::HYBRID) {
        // EVM optimization checks
        
        // 1. Check for redundant PUSH/POP sequences
        bool has_redundant_pushpop = false;
        for (size_t i = 0; i < bytecode.size() - 1; ++i) {
            if ((bytecode[i] >= 0x60 && bytecode[i] <= 0x7f) && bytecode[i + 1] == 0x50) {
                has_redundant_pushpop = true;
                break;
            }
        }
        
        // 2. Check for unnecessary DUP operations
        bool has_unnecessary_dup = false;
        for (size_t i = 0; i < bytecode.size() - 1; ++i) {
            if ((bytecode[i] >= 0x80 && bytecode[i] <= 0x8f) && bytecode[i + 1] == 0x50) {
                has_unnecessary_dup = true;
                break;
            }
        }
        
        // 3. Check for dead code after RETURN/STOP/REVERT
        bool has_dead_code = false;
        for (size_t i = 0; i < bytecode.size() - 1; ++i) {
            if (bytecode[i] == 0xf3 || bytecode[i] == 0x00 || bytecode[i] == 0xfd) {
                // Check if there's code after (excluding JUMPDEST)
                if (i + 1 < bytecode.size() && bytecode[i + 1] != 0x5b) {
                    has_dead_code = true;
                    break;
                }
            }
        }
        
        // Bytecode is considered optimized if it doesn't have these issues
        has_optimizations = !has_redundant_pushpop && !has_unnecessary_dup && !has_dead_code;
        
        // 4. Check for efficient PUSH usage (using smallest PUSH size)
        bool uses_efficient_push = true;
        for (size_t i = 0; i < bytecode.size(); ) {
            uint8_t opcode = bytecode[i];
            if (opcode >= 0x60 && opcode <= 0x7f) {
                uint8_t push_size = opcode - 0x5f;
                if (i + push_size < bytecode.size()) {
                    // Check if a smaller PUSH could be used
                    bool all_zero = true;
                    for (uint8_t j = 0; j < push_size - 1; ++j) {
                        if (bytecode[i + 1 + j] != 0) {
                            all_zero = false;
                            break;
                        }
                    }
                    if (all_zero && push_size > 1) {
                        uses_efficient_push = false;
                        break;
                    }
                    i += 1 + push_size;
                } else {
                    i++;
                }
            } else {
                i++;
            }
        }
        
        has_optimizations = has_optimizations && uses_efficient_push;
        
    } else if (format == BytecodeFormat::CVM_NATIVE) {
        // CVM optimization checks
        
        // 1. Check for redundant register operations
        bool has_redundant_ops = false;
        for (size_t i = 0; i < bytecode.size() - 1; ++i) {
            if (bytecode[i] == 0x01 && bytecode[i + 1] == 0x02) { // PUSH followed by POP
                has_redundant_ops = true;
                break;
            }
        }
        
        // 2. Check for efficient data packing
        bool uses_efficient_packing = true;
        for (size_t i = 0; i < bytecode.size(); ) {
            if (bytecode[i] == 0x01 && i + 1 < bytecode.size()) { // PUSH
                uint8_t size = bytecode[i + 1];
                if (size > 32) { // Invalid size
                    uses_efficient_packing = false;
                    break;
                }
                i += 2 + size;
            } else {
                i++;
            }
        }
        
        has_optimizations = !has_redundant_ops && uses_efficient_packing;
    }
    
    return has_optimizations;
}

std::vector<uint8_t> OptimizeBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat format) {
    if (bytecode.empty()) {
        return bytecode;
    }
    
    std::vector<uint8_t> optimized;
    
    if (format == BytecodeFormat::EVM_BYTECODE || format == BytecodeFormat::HYBRID) {
        // EVM bytecode optimization
        
        for (size_t i = 0; i < bytecode.size(); ) {
            uint8_t opcode = bytecode[i];
            
            // Optimization 1: Remove redundant PUSH/POP sequences
            if ((opcode >= 0x60 && opcode <= 0x7f) && i + 1 < bytecode.size()) {
                uint8_t push_size = opcode - 0x5f;
                if (i + 1 + push_size < bytecode.size() && bytecode[i + 1 + push_size] == 0x50) {
                    // Skip PUSH and following POP
                    i += 2 + push_size;
                    continue;
                }
            }
            
            // Optimization 2: Remove unnecessary DUP followed by POP
            if ((opcode >= 0x80 && opcode <= 0x8f) && i + 1 < bytecode.size() && bytecode[i + 1] == 0x50) {
                // Skip DUP and POP
                i += 2;
                continue;
            }
            
            // Optimization 3: Remove dead code after RETURN/STOP/REVERT
            if (opcode == 0xf3 || opcode == 0x00 || opcode == 0xfd) {
                optimized.push_back(opcode);
                i++;
                // Skip until next JUMPDEST
                while (i < bytecode.size() && bytecode[i] != 0x5b) {
                    i++;
                }
                continue;
            }
            
            // Optimization 4: Use smallest PUSH size
            if (opcode >= 0x60 && opcode <= 0x7f) {
                uint8_t push_size = opcode - 0x5f;
                if (i + push_size < bytecode.size()) {
                    // Find actual size needed
                    uint8_t actual_size = push_size;
                    for (uint8_t j = 0; j < push_size - 1; ++j) {
                        if (bytecode[i + 1 + j] != 0) {
                            break;
                        }
                        actual_size--;
                    }
                    
                    if (actual_size < push_size && actual_size > 0) {
                        // Use smaller PUSH
                        optimized.push_back(0x5f + actual_size);
                        for (uint8_t j = push_size - actual_size; j < push_size; ++j) {
                            optimized.push_back(bytecode[i + 1 + j]);
                        }
                        i += 1 + push_size;
                        continue;
                    }
                }
            }
            
            // No optimization applied, copy as-is
            optimized.push_back(opcode);
            
            // Handle PUSH data
            if (opcode >= 0x60 && opcode <= 0x7f) {
                uint8_t push_size = opcode - 0x5f;
                for (uint8_t j = 0; j < push_size && i + 1 + j < bytecode.size(); ++j) {
                    optimized.push_back(bytecode[i + 1 + j]);
                }
                i += 1 + push_size;
            } else {
                i++;
            }
        }
        
    } else if (format == BytecodeFormat::CVM_NATIVE) {
        // CVM bytecode optimization
        
        for (size_t i = 0; i < bytecode.size(); ) {
            uint8_t opcode = bytecode[i];
            
            // Optimization 1: Remove redundant PUSH/POP sequences
            if (opcode == 0x01 && i + 1 < bytecode.size()) { // PUSH
                uint8_t size = bytecode[i + 1];
                if (i + 2 + size < bytecode.size() && bytecode[i + 2 + size] == 0x02) { // Followed by POP
                    // Skip PUSH and POP
                    i += 3 + size;
                    continue;
                }
            }
            
            // Optimization 2: Optimize PUSH data size
            if (opcode == 0x01 && i + 1 < bytecode.size()) {
                uint8_t size = bytecode[i + 1];
                if (size > 0 && size <= 32 && i + 2 + size <= bytecode.size()) {
                    // Find actual size needed (remove leading zeros)
                    uint8_t actual_size = size;
                    for (uint8_t j = 0; j < size - 1; ++j) {
                        if (bytecode[i + 2 + j] != 0) {
                            break;
                        }
                        actual_size--;
                    }
                    
                    if (actual_size < size && actual_size > 0) {
                        // Use smaller size
                        optimized.push_back(0x01); // PUSH
                        optimized.push_back(actual_size);
                        for (uint8_t j = size - actual_size; j < size; ++j) {
                            optimized.push_back(bytecode[i + 2 + j]);
                        }
                        i += 2 + size;
                        continue;
                    }
                }
            }
            
            // No optimization applied, copy as-is
            optimized.push_back(opcode);
            
            // Handle PUSH data
            if (opcode == 0x01 && i + 1 < bytecode.size()) {
                uint8_t size = bytecode[i + 1];
                optimized.push_back(size);
                for (uint8_t j = 0; j < size && i + 2 + j < bytecode.size(); ++j) {
                    optimized.push_back(bytecode[i + 2 + j]);
                }
                i += 2 + size;
            } else {
                i++;
            }
        }
        
    } else {
        // Unknown format, return as-is
        return bytecode;
    }
    
    LogPrint(BCLog::CVM, "BytecodeUtils: Optimized bytecode from %d to %d bytes\n",
             bytecode.size(), optimized.size());
    
    return optimized;
}

} // namespace BytecodeUtils

// BytecodeDetectionCache Implementation

BytecodeDetectionCache::BytecodeDetectionCache(size_t max_entries) 
    : max_entries(max_entries), total_requests(0), cache_hits(0) {
}

BytecodeDetectionCache::~BytecodeDetectionCache() {
}

bool BytecodeDetectionCache::HasResult(const std::vector<uint8_t>& bytecode) {
    total_requests++;
    uint256 hash = HashBytecode(bytecode);
    return cache.find(hash) != cache.end();
}

BytecodeDetectionResult BytecodeDetectionCache::GetResult(const std::vector<uint8_t>& bytecode) {
    uint256 hash = HashBytecode(bytecode);
    auto it = cache.find(hash);
    if (it != cache.end()) {
        cache_hits++;
        it->second.access_count++;
        return it->second.result;
    }
    
    return BytecodeDetectionResult{};
}

void BytecodeDetectionCache::StoreResult(const std::vector<uint8_t>& bytecode, const BytecodeDetectionResult& result) {
    if (cache.size() >= max_entries) {
        EvictOldEntries();
    }
    
    uint256 hash = HashBytecode(bytecode);
    CacheEntry entry;
    entry.bytecode_hash = hash;
    entry.result = result;
    entry.timestamp = GetTime();
    entry.access_count = 1;
    
    cache[hash] = entry;
}

void BytecodeDetectionCache::Clear() {
    cache.clear();
    total_requests = 0;
    cache_hits = 0;
}

size_t BytecodeDetectionCache::Size() const {
    return cache.size();
}

double BytecodeDetectionCache::HitRate() const {
    if (total_requests == 0) return 0.0;
    return static_cast<double>(cache_hits) / total_requests;
}

uint256 BytecodeDetectionCache::HashBytecode(const std::vector<uint8_t>& bytecode) {
    return Hash(bytecode.begin(), bytecode.end());
}

void BytecodeDetectionCache::EvictOldEntries() {
    // Remove 25% of least recently used entries
    size_t to_remove = max_entries / 4;
    
    std::vector<std::pair<uint256, int64_t>> entries_by_time;
    for (const auto& pair : cache) {
        entries_by_time.emplace_back(pair.first, pair.second.timestamp);
    }
    
    std::sort(entries_by_time.begin(), entries_by_time.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    for (size_t i = 0; i < to_remove && i < entries_by_time.size(); ++i) {
        cache.erase(entries_by_time[i].first);
    }
}

} // namespace CVM