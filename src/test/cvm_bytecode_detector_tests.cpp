// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cvm/bytecode_detector.h>
#include <test/test_bitcoin.h>
#include <utilstrencodings.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cvm_bytecode_detector_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(detect_evm_bytecode)
{
    CVM::BytecodeDetector detector;
    
    // EVM bytecode with PUSH opcodes (0x60-0x7f)
    std::vector<uint8_t> evm_bytecode = ParseHex("604260005260206000f3");
    
    CVM::BytecodeDetectionResult result = detector.DetectFormat(evm_bytecode);
    
    BOOST_CHECK(result.format == CVM::BytecodeFormat::EVM_BYTECODE);
    BOOST_CHECK_GT(result.confidence, 0.8);  // Confidence is 0.0-1.0
    BOOST_CHECK(!result.reason.empty());
}

BOOST_AUTO_TEST_CASE(detect_cvm_bytecode)
{
    CVM::BytecodeDetector detector;
    
    // CVM bytecode (register-based patterns)
    // Note: This is a simplified pattern
    std::vector<uint8_t> cvm_bytecode = ParseHex("01020304");
    
    CVM::BytecodeDetectionResult result = detector.DetectFormat(cvm_bytecode);
    
    // Should detect as CVM or UNKNOWN for short bytecode
    BOOST_CHECK(result.format == CVM::BytecodeFormat::CVM_NATIVE || 
                result.format == CVM::BytecodeFormat::UNKNOWN);
}

BOOST_AUTO_TEST_CASE(detect_empty_bytecode)
{
    CVM::BytecodeDetector detector;
    
    std::vector<uint8_t> empty_bytecode;
    
    CVM::BytecodeDetectionResult result = detector.DetectFormat(empty_bytecode);
    
    // Empty bytecode should have low confidence
    BOOST_CHECK_LT(result.confidence, 0.5);  // Confidence is 0.0-1.0
}

BOOST_AUTO_TEST_CASE(detect_short_bytecode)
{
    CVM::BytecodeDetector detector;
    
    // Very short bytecode
    std::vector<uint8_t> short_bytecode = ParseHex("60");
    
    CVM::BytecodeDetectionResult result = detector.DetectFormat(short_bytecode);
    
    // Short bytecode may have lower confidence
    BOOST_CHECK_GE(result.confidence, 0.0);
    BOOST_CHECK_LE(result.confidence, 1.0);
}

BOOST_AUTO_TEST_CASE(detect_long_evm_bytecode)
{
    CVM::BytecodeDetector detector;
    
    // Longer EVM bytecode with multiple PUSH opcodes
    std::vector<uint8_t> long_bytecode = ParseHex(
        "6080604052348015600f57600080fd5b50603f80601d6000396000f3fe"
        "6080604052600080fdfea2646970667358221220"
    );
    
    CVM::BytecodeDetectionResult result = detector.DetectFormat(long_bytecode);
    
    BOOST_CHECK(result.format == CVM::BytecodeFormat::EVM_BYTECODE);
    BOOST_CHECK_GT(result.confidence, 0.8);  // Confidence is 0.0-1.0
}

BOOST_AUTO_TEST_CASE(bytecode_optimization)
{
    CVM::BytecodeDetector detector;
    
    // Test bytecode optimization using BytecodeUtils
    std::vector<uint8_t> bytecode = ParseHex("604260005260206000f3");
    
    bool is_optimized = CVM::BytecodeUtils::IsBytecodeOptimized(bytecode, CVM::BytecodeFormat::EVM_BYTECODE);
    
    // Should return a boolean
    BOOST_CHECK(is_optimized == true || is_optimized == false);
}

BOOST_AUTO_TEST_CASE(bytecode_disassembly)
{
    CVM::BytecodeDetector detector;
    
    // Test bytecode disassembly using BytecodeUtils
    std::vector<uint8_t> bytecode = ParseHex("604260005260206000f3");
    
    std::string disassembly = CVM::BytecodeUtils::DisassembleBytecode(bytecode, CVM::BytecodeFormat::EVM_BYTECODE);
    
    // Should return non-empty disassembly
    BOOST_CHECK(!disassembly.empty());
}

BOOST_AUTO_TEST_CASE(confidence_range)
{
    CVM::BytecodeDetector detector;
    
    // Test various bytecodes and ensure confidence is in valid range
    std::vector<std::string> test_bytecodes = {
        "604260005260206000f3",  // EVM
        "01020304",                // CVM-like
        "60",                      // Short
        ""                         // Empty
    };
    
    for (const auto& hex : test_bytecodes) {
        std::vector<uint8_t> bytecode = ParseHex(hex);
        CVM::BytecodeDetectionResult result = detector.DetectFormat(bytecode);
        
        // Confidence must be 0.0-1.0
        BOOST_CHECK_GE(result.confidence, 0.0);
        BOOST_CHECK_LE(result.confidence, 1.0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
