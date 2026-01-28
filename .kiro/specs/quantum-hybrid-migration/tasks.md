# Implementation Plan: Quantum Hybrid Migration

## Overview

This implementation plan breaks down the Post-Quantum Cryptography (PQC) feature into discrete coding tasks. The implementation follows a bottom-up approach: cryptographic primitives first, then consensus rules, then wallet/network integration. Each task builds on previous work and includes property-based tests to validate correctness.

## Tasks

- [x] 1. Set up liboqs dependency and build system integration
  - Add liboqs to `depends/` build system for cross-compilation support
  - Update `configure.ac` to detect and link liboqs
  - Update `Makefile.am` to include quantum crypto source files
  - Add `--enable-quantum` configure flag (default: enabled)
  - Create `src/crypto/quantum/` directory structure
  - _Requirements: 9.1_

- [x] 2. Implement FALCON-512 cryptographic module
  - [x] 2.1 Create quantum namespace and core interfaces
    - Create `src/crypto/quantum/falcon.h` with function declarations
    - Create `src/crypto/quantum/falcon.cpp` with liboqs wrapper implementation
    - Implement `PQC_Start()` and `PQC_Stop()` initialization functions
    - Implement `PQC_InitSanityCheck()` for startup validation
    - _Requirements: 9.1, 9.4_
  
  - [x] 2.2 Implement key generation
    - Implement `GenerateKeyPair()` using liboqs FALCON-512
    - Ensure 256-bit entropy from system CSPRNG
    - Add key size validation (privkey: 1281 bytes, pubkey: 897 bytes)
    - _Requirements: 1.1, 1.2, 9.4_
  
  - [x] 2.3 Implement signing and verification
    - Implement `Sign()` function with constant-time operations
    - Implement `Verify()` function
    - Implement `IsCanonicalSignature()` for malleability prevention
    - Add signature size validation (max 700 bytes)
    - _Requirements: 1.5, 9.5, 9.8_
  
  - [x] 2.4 Write property tests for FALCON-512 module
    - **Property 3: Signature size by key type**
    - **Validates: Requirements 1.5, 1.6**

- [x] 3. Checkpoint - Verify cryptographic module
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Extend CKey class for dual-stack key management
  - [x] 4.1 Add CKeyType enumeration and key type support
    - Add `CKeyType` enum to `src/key.h` (KEY_TYPE_INVALID, KEY_TYPE_ECDSA, KEY_TYPE_QUANTUM)
    - Add `keyType` member variable to CKey class
    - Add `GetKeyType()`, `IsQuantum()`, `IsECDSA()` methods
    - Resize `keydata` vector based on key type (32 vs 1281 bytes)
    - _Requirements: 1.1, 1.3, 1.4_
  
  - [x] 4.2 Implement quantum key generation
    - Add `MakeNewQuantumKey()` method to CKey
    - Integrate with FALCON-512 module for key generation
    - Update `MakeNewKey()` to set KEY_TYPE_ECDSA explicitly
    - _Requirements: 1.1_
  
  - [x] 4.3 Implement quantum signing
    - Add `SignQuantum()` method to CKey
    - Update `Sign()` to dispatch based on key type
    - Ensure secure memory handling for quantum keys
    - _Requirements: 1.5, 1.7_
  
  - [x] 4.4 Implement key serialization with type prefix
    - Update `Serialize()` to include key type byte (0x02 for quantum)
    - Update `Unserialize()` to read key type and validate
    - Maintain backward compatibility for existing ECDSA keys
    - _Requirements: 1.8, 1.9, 10.1_
  
  - [x] 4.5 Write property tests for CKey
    - **Property 1: Key storage round-trip**
    - **Property 2: Key serialization round-trip**
    - **Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.8, 1.9**

- [-] 5. Extend CPubKey class for quantum public keys
  - [x] 5.1 Add quantum public key support
    - Add constants for quantum key sizes (897 bytes pubkey, 700 bytes max sig)
    - Add `keyType` member and type detection methods
    - Update `Set()` to handle quantum public keys
    - Update `size()` to return correct size based on type
    - _Requirements: 1.2_
  
  - [x] 5.2 Implement quantum verification
    - Add `VerifyQuantum()` method using FALCON-512 module
    - Update `Verify()` to dispatch based on key type
    - Add `GetQuantumID()` for SHA256-based key ID
    - _Requirements: 2.2_
  
  - [x] 5.3 Implement public key serialization
    - Update serialization to include type prefix (0x05 for quantum)
    - Maintain backward compatibility for ECDSA keys
    - _Requirements: 10.2_

- [x] 6. Checkpoint - Verify key management
  - Ensure all tests pass, ask the user if questions arise.

- [-] 7. Implement quantum address encoding
  - [x] 7.1 Extend Bech32 module for Bech32m
    - Update `src/bech32.cpp` to support Bech32m checksum (if not already present)
    - Add constants for quantum HRPs (casq, tcasq, rcasq)
    - _Requirements: 3.1, 3.2, 3.3_
  
  - [x] 7.2 Implement quantum address encoding/decoding
    - Create `src/address_quantum.h` and `src/address_quantum.cpp`
    - Implement `EncodeQuantumAddress()` with witness version 2
    - Implement `DecodeAddress()` returning `DecodedAddress` struct
    - Derive address program from SHA256(pubkey)
    - _Requirements: 3.4, 3.5, 3.6_
  
  - [x] 7.3 Integrate with existing address infrastructure
    - Update `EncodeDestination()` to handle quantum destinations
    - Update `DecodeDestination()` to recognize quantum addresses
    - Add `CTxDestination` variant for quantum addresses
    - _Requirements: 3.7, 3.8, 3.9_
  
  - [x] 7.4 Write property tests for address encoding ✓
    - **Property 8: Quantum address HRP by network** ✓
    - **Property 9: Quantum address encoding round-trip** ✓
    - **Property 10: Address type recognition** ✓
    - **Property 11: Quantum address HRP validation** ✓
    - **Validates: Requirements 3.1-3.9**

- [x] 8. Implement Witness Version 2 consensus rules
  - [x] 8.1 Add consensus parameters for quantum activation
    - Update `src/consensus/params.h` with quantum activation heights
    - Add `quantumActivationHeight` for mainnet (350000, current: ~276606)
    - Add `quantumActivationHeightTestnet` (50000, current: ~5377)
    - Add `quantumActivationHeightRegtest` (1)
    - Add `DEPLOYMENT_QUANTUM` to `DeploymentPos` enum
    - _Requirements: 6.1, 6.2, 6.3, 6.6_
  
  - [x] 8.2 Update chainparams with activation heights
    - Update `src/chainparams.cpp` for mainnet, testnet, regtest
    - Set quantum activation heights in consensus params
    - _Requirements: 6.1, 6.2, 6.3_
  
  - [x] 8.3 Implement VerifyWitnessV2Quantum
    - Add `SIGVERSION_WITNESS_V2_QUANTUM` to `SigVersion` enum
    - Implement `VerifyWitnessV2Quantum()` in `src/script/interpreter.cpp`
    - Validate pubkey size (897 bytes) and signature size (max 700 bytes)
    - Call FALCON-512 verification via quantum module
    - _Requirements: 2.2, 2.3, 2.6_
  
  - [x] 8.4 Update VerifyWitnessProgram for version 2
    - Add witness version 2 case to `VerifyWitnessProgram()`
    - Check activation height before enforcing quantum rules
    - Return `SCRIPT_ERR_SIG_QUANTUM_VERIFY` on verification failure
    - _Requirements: 2.1, 2.2, 2.4, 2.5_
  
  - [x] 8.5 Add script error codes
    - Add `SCRIPT_ERR_SIG_QUANTUM_VERIFY` to `script_error.h`
    - Add `SCRIPT_ERR_QUANTUM_NOT_ACTIVE` for pre-activation rejection
    - Update `ScriptErrorString()` with descriptions
    - _Requirements: 2.4_
  
  - [x] 8.6 Write property tests for consensus rules
    - **Property 4: Witness version determines verification algorithm**
    - **Property 5: Quantum signature acceptance**
    - **Property 6: Quantum pubkey size validation**
    - **Property 7: Sighash consistency**
    - **Property 16: Activation height enforcement**
    - **Property 17: Backward compatibility**
    - **Validates: Requirements 2.1-2.8, 6.4, 6.5, 6.8**

- [x] 9. Checkpoint - Verify consensus rules
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Implement Hive mining quantum support
  - [x] 10.1 Update Hive agent creation for dual signatures
    - Modify agent creation validation in `src/validation.cpp`
    - Accept both ECDSA and FALCON-512 signatures for BCT
    - Store agent key type in agent record
    - _Requirements: 4.1_
  
  - [x] 10.2 Update Hive block validation
    - Modify Hive coinbase validation to check signature type
    - Verify signature matches agent's registered key type
    - Support quantum signatures in Hive blocks
    - _Requirements: 4.4, 4.5, 4.6_
  
  - [x] 10.3 Default to quantum keys post-activation
    - Update agent creation to use quantum keys after activation
    - Maintain legacy agent validation for existing agents
    - _Requirements: 4.2, 4.3_
  
  - [x] 10.4 Write property tests for Hive quantum support
    - **Property 12: Hive dual signature support**
    - **Property 13: Hive signature algorithm matching**
    - **Validates: Requirements 4.1-4.6**

- [x] 11. Implement wallet quantum support
  - [x] 11.1 Update key pool management
    - Add separate key pools for ECDSA and quantum keys
    - Update `CKeyPool` to include key type
    - Implement `TopUpQuantumKeyPool()`
    - _Requirements: 10.6_
  
  - [x] 11.2 Update address generation
    - Modify `GetNewAddress()` to check activation height
    - Return quantum address by default post-activation
    - Add `GetLegacyAddress()` for backward compatibility
    - _Requirements: 5.1, 5.2_
  
  - [x] 11.3 Implement migrate_to_quantum RPC
    - Create `migrate_to_quantum` RPC command in `src/wallet/rpcwallet.cpp`
    - Sweep specified or all legacy UTXOs to quantum address
    - Calculate and display fee estimate
    - Handle insufficient funds error
    - _Requirements: 5.3, 5.4, 5.6, 5.7_
  
  - [x] 11.4 Update wallet backup/restore
    - Ensure quantum keys included in wallet dump
    - Update `dumpwallet` and `importwallet` for quantum keys
    - _Requirements: 10.7_
  
  - [x] 11.5 Implement quantum key import
    - Add WIF-like encoding for quantum keys with distinct version
    - Implement `importquantumkey` RPC command
    - _Requirements: 10.8_
  
  - [x] 11.6 Write property tests for wallet
    - **Property 14: Wallet default address type**
    - **Property 15: Migration transaction structure**
    - **Property 29: Separate key pools**
    - **Property 30: Wallet backup completeness**
    - **Validates: Requirements 5.1-5.8, 10.6-10.8**

- [x] 12. Checkpoint - Verify wallet integration
  - Ensure all tests pass, ask the user if questions arise.

- [x] 13. Implement CVM quantum signature support
  - [x] 13.1 Extend VERIFY_SIG opcode
    - Update `HandleCrypto()` in `src/cvm/cvm.cpp`
    - Detect signature type by size (>100 bytes = quantum)
    - Dispatch to appropriate verification function
    - _Requirements: 7.1, 7.2, 7.3_
  
  - [x] 13.2 Add explicit quantum opcodes
    - Add `OP_VERIFY_SIG_QUANTUM` (0x60) to opcodes.h
    - Add `OP_VERIFY_SIG_ECDSA` (0x61) to opcodes.h
    - Implement handlers that enforce specific signature type
    - _Requirements: 7.5, 7.6, 7.8_
  
  - [x] 13.3 Update gas costs
    - Set `GAS_VERIFY_SIG_QUANTUM = 3000`
    - Maintain `GAS_VERIFY_SIG_ECDSA = 60`
    - Update gas calculation in signature verification
    - _Requirements: 7.4_
  
  - [x] 13.4 Verify contract address derivation
    - Ensure contract address derivation is key-type agnostic
    - Test deployment with both ECDSA and quantum deployers
    - _Requirements: 7.7_
  
  - [x] 13.5 Write property tests for CVM
    - **Property 18: CVM signature type detection**
    - **Property 19: CVM quantum gas cost**
    - **Property 20: CVM contract address derivation invariance**
    - **Property 21: CVM explicit opcode type checking**
    - **Validates: Requirements 7.1-7.8**

- [x] 14. Implement network protocol extensions
  - [x] 14.1 Add NODE_QUANTUM service flag
    - Add `NODE_QUANTUM = (1 << 8)` to `src/protocol.h`
    - Update version message to include quantum capability
    - _Requirements: 8.2, 8.3_
  
  - [x] 14.2 Update transaction serialization
    - Ensure transaction serialization handles signatures up to 700 bytes
    - Add size validation in deserialization
    - _Requirements: 8.1_
  
  - [x] 14.3 Implement quantum transaction relay filtering
    - Check peer NODE_QUANTUM flag before relaying quantum tx
    - Skip relay to non-quantum peers for quantum transactions
    - Always relay blocks containing quantum transactions
    - _Requirements: 8.4, 8.5, 8.8_
  
  - [x] 14.4 Add MSG_QUANTUM_TX inventory type
    - Add inventory type for quantum transactions
    - Update inventory handling in `src/net_processing.cpp`
    - _Requirements: 8.6_
  
  - [x] 14.5 Write property tests for network
    - **Property 22: Network large signature support**
    - **Property 23: Network quantum relay filtering**
    - **Property 24: Network block relay universality**
    - **Validates: Requirements 8.1-8.8**

- [x] 15. Implement transaction size and validation limits
  - [x] 15.1 Update virtual size calculation
    - Include quantum signature size in vsize calculation
    - Update fee estimation for quantum transactions
    - _Requirements: 9.6_
  
  - [x] 15.2 Add signature size limits
    - Enforce maximum 1024 bytes per signature
    - Reject transactions exceeding limit
    - _Requirements: 9.7_
  
  - [x] 15.3 Write property tests for limits
    - **Property 25: Transaction virtual size calculation**
    - **Property 26: Signature size limit**
    - **Property 27: Signature canonicality**
    - **Validates: Requirements 9.6, 9.7, 9.8**

- [x] 16. Final checkpoint - Full integration testing
  - Ensure all tests pass, ask the user if questions arise.
  - Run full test suite including functional tests
  - Verify backward compatibility with existing transactions

- [x] 17. Update documentation and RPC help
  - [x] 17.1 Update RPC documentation
    - Add help text for `migrate_to_quantum` command
    - Add help text for `importquantumkey` command
    - Update `getnewaddress` help for quantum default
    - _Requirements: 5.2, 5.3_
  
  - [x] 17.2 Create quantum migration guide
    - Document migration process for users
    - Document activation timeline
    - Add FAQ for common questions
    - _Requirements: All_

## Notes

- All tasks are required for comprehensive implementation with full test coverage
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties
- Unit tests validate specific examples and edge cases
- The implementation follows the existing Cascoin soft-fork pattern used for CVM activation
- liboqs must be compiled with FALCON-512 support enabled
- All cryptographic operations must use secure memory allocation
