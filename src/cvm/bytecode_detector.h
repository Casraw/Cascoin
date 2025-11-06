// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CASCOIN_CVM_BYTECODE_DETECTOR_H
#define CASCOIN_CVM_BYTECODE_DETECTOR_H

#include <vector>
#include <cstdint>
#include <string>
#include <map>
#include <set>
#include "uint256.h"

namespace CVM {

/**
 * Bytecode Format Types
 */
enum class BytecodeFormat {
    UNKNOWN,        // Unable to determine format
    CVM_NATIVE,     // Cascoin VM native bytecode (register-based)
    EVM_BYTECODE,   // Ethereum Virtual Machine bytecode (stack-based)
    HYBRID          // Contract that can run on both VMs
};

/**
 * Bytecode Detection Result
 */
struct BytecodeDetectionResult {
    BytecodeFormat format;
    double confidence;          // Confidence score (0.0 to 1.0)
    std::string reason;         // Human-readable reason for detection
    bool is_valid;             // Whether bytecode appears to be valid
    size_t estimated_size;     // Estimated runtime size
    
    BytecodeDetectionResult() 
        : format(BytecodeFormat::UNKNOWN), confidence(0.0), is_valid(false), estimated_size(0) {}
};

/**
 * Bytecode Format Detector
 * 
 * Analyzes bytecode to determine whether it's CVM native format,
 * EVM bytecode, or a hybrid contract that can run on both VMs.
 */
class BytecodeDetector {
public:
    BytecodeDetector();
    ~BytecodeDetector();

    // Main detection interface
    BytecodeDetectionResult DetectFormat(const std::vector<uint8_t>& bytecode);
    BytecodeDetectionResult DetectFormat(const uint8_t* data, size_t size);
    
    // Format-specific detection
    bool IsEVMBytecode(const std::vector<uint8_t>& bytecode);
    bool IsCVMBytecode(const std::vector<uint8_t>& bytecode);
    bool IsHybridContract(const std::vector<uint8_t>& bytecode);
    
    // Validation
    bool ValidateEVMBytecode(const std::vector<uint8_t>& bytecode);
    bool ValidateCVMBytecode(const std::vector<uint8_t>& bytecode);
    
    // Analysis utilities
    std::vector<uint8_t> ExtractEVMPortion(const std::vector<uint8_t>& hybrid_bytecode);
    std::vector<uint8_t> ExtractCVMPortion(const std::vector<uint8_t>& hybrid_bytecode);
    
    // Configuration
    void SetConfidenceThreshold(double threshold) { confidence_threshold = threshold; }
    void EnableStrictValidation(bool enable) { strict_validation = enable; }
    
    // Statistics and debugging
    struct DetectionStats {
        size_t total_detections;
        size_t evm_detected;
        size_t cvm_detected;
        size_t hybrid_detected;
        size_t unknown_detected;
        double average_confidence;
    };
    
    DetectionStats GetStats() const { return stats; }
    void ResetStats();
    
    // Section finding (needed by BytecodeUtils)
    size_t FindEVMSection(const std::vector<uint8_t>& bytecode);
    size_t FindCVMSection(const std::vector<uint8_t>& bytecode);
    
    // Known patterns and signatures (needed by BytecodeUtils)
    static const std::vector<uint8_t> CVM_HEADER_PATTERN;
    static const std::vector<uint8_t> HYBRID_SEPARATOR_PATTERN;

private:
    // EVM bytecode detection
    bool HasEVMOpcodes(const std::vector<uint8_t>& bytecode);
    bool HasEVMPushPattern(const std::vector<uint8_t>& bytecode);
    bool HasEVMJumpDestinations(const std::vector<uint8_t>& bytecode);
    double CalculateEVMConfidence(const std::vector<uint8_t>& bytecode);
    
    // CVM bytecode detection  
    bool HasCVMOpcodes(const std::vector<uint8_t>& bytecode);
    bool HasCVMRegisterPattern(const std::vector<uint8_t>& bytecode);
    bool HasCVMTrustOpcodes(const std::vector<uint8_t>& bytecode);
    double CalculateCVMConfidence(const std::vector<uint8_t>& bytecode);
    
    // Hybrid contract detection
    bool HasHybridMarkers(const std::vector<uint8_t>& bytecode);
    bool HasFormatSeparators(const std::vector<uint8_t>& bytecode);
    
    // Validation helpers
    bool ValidateOpcodeSequence(const std::vector<uint8_t>& bytecode, size_t start, size_t end);
    bool CheckBytecodeIntegrity(const std::vector<uint8_t>& bytecode);
    bool VerifyJumpTargets(const std::vector<uint8_t>& bytecode);
    
    // Pattern matching
    bool MatchesPattern(const std::vector<uint8_t>& bytecode, const std::vector<uint8_t>& pattern, size_t offset = 0);
    std::vector<size_t> FindPatternOccurrences(const std::vector<uint8_t>& bytecode, const std::vector<uint8_t>& pattern);
    
    // Statistical analysis
    void UpdateStats(const BytecodeDetectionResult& result);
    double CalculateEntropyScore(const std::vector<uint8_t>& bytecode);
    std::map<uint8_t, size_t> GetOpcodeFrequency(const std::vector<uint8_t>& bytecode);
    
    // Configuration
    double confidence_threshold;
    bool strict_validation;
    
    // Statistics
    DetectionStats stats;
    
    // Known patterns and signatures
    static const std::vector<uint8_t> EVM_CONSTRUCTOR_PATTERN;
    static const std::vector<uint8_t> EVM_FUNCTION_SELECTOR_PATTERN;
    static const std::vector<uint8_t> CVM_TRUST_PATTERN;
    
    // EVM opcode definitions for detection
    static const std::set<uint8_t> EVM_OPCODES;
    static const std::set<uint8_t> EVM_PUSH_OPCODES;
    static const std::set<uint8_t> EVM_JUMP_OPCODES;
    
    // CVM opcode definitions for detection
    static const std::set<uint8_t> CVM_OPCODES;
    static const std::set<uint8_t> CVM_REGISTER_OPCODES;
    static const std::set<uint8_t> CVM_TRUST_OPCODES;
};

/**
 * Bytecode Format Utilities
 */
namespace BytecodeUtils {
    // Format conversion utilities
    std::string FormatToString(BytecodeFormat format);
    BytecodeFormat StringToFormat(const std::string& format_str);
    
    // Bytecode analysis
    bool IsValidBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat expected_format);
    size_t EstimateBytecodeComplexity(const std::vector<uint8_t>& bytecode);
    
    // Hybrid contract utilities
    struct HybridContractLayout {
        size_t header_size;
        size_t evm_offset;
        size_t evm_size;
        size_t cvm_offset;
        size_t cvm_size;
        bool has_metadata;
    };
    
    HybridContractLayout AnalyzeHybridLayout(const std::vector<uint8_t>& bytecode);
    std::vector<uint8_t> CreateHybridContract(const std::vector<uint8_t>& evm_code, const std::vector<uint8_t>& cvm_code);
    
    // Debugging and visualization
    std::string DisassembleBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat format);
    std::string BytecodeToHex(const std::vector<uint8_t>& bytecode);
    std::vector<uint8_t> HexToBytecode(const std::string& hex);
    
    // Performance optimization
    bool IsBytecodeOptimized(const std::vector<uint8_t>& bytecode, BytecodeFormat format);
    std::vector<uint8_t> OptimizeBytecode(const std::vector<uint8_t>& bytecode, BytecodeFormat format);
}

/**
 * Bytecode Detection Cache
 * 
 * Caches detection results to avoid repeated analysis of the same bytecode
 */
class BytecodeDetectionCache {
public:
    BytecodeDetectionCache(size_t max_entries = 1000);
    ~BytecodeDetectionCache();
    
    // Cache operations
    bool HasResult(const std::vector<uint8_t>& bytecode);
    BytecodeDetectionResult GetResult(const std::vector<uint8_t>& bytecode);
    void StoreResult(const std::vector<uint8_t>& bytecode, const BytecodeDetectionResult& result);
    
    // Cache management
    void Clear();
    size_t Size() const;
    double HitRate() const;
    
private:
    struct CacheEntry {
        uint256 bytecode_hash;
        BytecodeDetectionResult result;
        int64_t timestamp;
        size_t access_count;
    };
    
    std::map<uint256, CacheEntry> cache;
    size_t max_entries;
    size_t total_requests;
    size_t cache_hits;
    
    uint256 HashBytecode(const std::vector<uint8_t>& bytecode);
    void EvictOldEntries();
};

} // namespace CVM

#endif // CASCOIN_CVM_BYTECODE_DETECTOR_H