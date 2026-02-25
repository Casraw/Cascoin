# Implementation Plan: FALCON-512 Public Key Registry

## Overview

This plan implements the FALCON-512 Public Key Registry for Cascoin, providing storage optimization for post-quantum transactions. The implementation follows the existing patterns from `cvmdb.h/cpp` for LevelDB storage and integrates with the quantum address infrastructure in `address_quantum.h/cpp`.

## Tasks

- [x] 1. Create QuantumPubKeyRegistry core class
  - [x] 1.1 Create `src/quantum_registry.h` header file
    - Define `QUANTUM_WITNESS_MARKER_REGISTRATION` (0x51) and `QUANTUM_WITNESS_MARKER_REFERENCE` (0x52) constants
    - Define `QuantumWitnessData` struct for parsed witness data
    - Define `QuantumRegistryStats` struct for statistics
    - Declare `QuantumPubKeyRegistry` class with LevelDB and LRU cache members
    - Declare global `g_quantumRegistry` pointer
    - Declare `InitQuantumRegistry()` and `ShutdownQuantumRegistry()` functions
    - _Requirements: 1.1, 1.2, 1.3, 4.1, 4.2, 6.1, 6.2, 7.3, 7.4_

  - [x] 1.2 Implement `src/quantum_registry.cpp` database operations
    - Implement constructor with LevelDB initialization at `{datadir}/quantum_pubkeys`
    - Implement destructor with graceful shutdown
    - Implement `RegisterPubKey()` with SHA256 hash computation and synchronous writes
    - Implement `LookupPubKey()` with cache-first lookup and hash verification
    - Implement `IsRegistered()` for existence check
    - Implement `GetStats()` to return key count and database size
    - Implement `GetLastError()` for error reporting
    - _Requirements: 1.4, 1.5, 1.6, 2.1, 2.2, 2.3, 2.4, 2.5, 3.1, 3.2, 3.3, 3.4, 3.5, 10.5_

  - [x] 1.3 Write property test for registration round-trip
    - **Property 1: Registration Round-Trip**
    - **Validates: Requirements 1.2, 1.3, 2.1, 3.1, 7.1**

  - [x] 1.4 Write property test for hash integrity
    - **Property 2: Hash Integrity on Retrieval**
    - **Validates: Requirements 2.1, 3.3**

- [x] 2. Implement LRU cache
  - [x] 2.1 Implement LRU cache operations in `quantum_registry.cpp`
    - Implement `AddToCache()` with capacity check and LRU eviction
    - Implement `LookupCache()` with cache hit/miss tracking
    - Use `std::list` for LRU ordering and `std::unordered_map` for O(1) lookup
    - Implement thread-safe access with `CCriticalSection`
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5_

  - [x] 2.2 Write property test for LRU eviction
    - **Property 9: LRU Cache Eviction**
    - **Validates: Requirements 6.5**

  - [x] 2.3 Write property test for registration count accuracy
    - **Property 10: Registration Count Accuracy**
    - **Validates: Requirements 7.3**

- [x] 3. Checkpoint - Ensure core registry tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement quantum witness parsing
  - [x] 4.1 Implement `ParseQuantumWitness()` function in `quantum_registry.cpp`
    - Parse first byte as marker (0x51 or 0x52)
    - For 0x51: extract 897-byte public key and signature
    - For 0x52: extract 32-byte hash and signature
    - Return error for invalid markers or malformed witnesses
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6_

  - [x] 4.2 Write property test for witness parsing
    - **Property 6: Witness Parsing Correctness**
    - **Validates: Requirements 4.1, 4.2, 4.3, 4.4, 4.5**

  - [x] 4.3 Write property test for invalid marker rejection
    - **Property 7: Invalid Marker Byte Rejection**
    - **Validates: Requirements 4.6**

- [x] 5. Implement transaction verification integration
  - [x] 5.1 Implement `VerifyQuantumTransaction()` function
    - For registration: use included public key directly
    - For reference: lookup public key from registry
    - Verify SHA256(pubkey) matches quantum address program
    - Call FALCON-512 signature verification
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

  - [x] 5.2 Integrate with validation.cpp
    - Add quantum witness detection in `CheckInputs()`
    - Call `ParseQuantumWitness()` for witness version 2 transactions
    - Call `VerifyQuantumTransaction()` for signature verification
    - Register public keys on successful registration transaction validation
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 5.3 Write property test for address derivation
    - **Property 8: Address Derivation Verification**
    - **Validates: Requirements 5.4, 5.5**

  - [x] 5.4 Write property test for invalid size rejection
    - **Property 4: Invalid Public Key Size Rejection**
    - **Validates: Requirements 2.5**

  - [x] 5.5 Write property test for unregistered lookup failure
    - **Property 5: Unregistered Hash Lookup Failure**
    - **Validates: Requirements 3.2**

- [x] 6. Checkpoint - Ensure verification tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement activation height enforcement
  - [x] 7.1 Add activation check in validation
    - Check `chainActive.Height() >= consensus.quantumActivationHeight` before accepting quantum witnesses
    - Reject 0x51/0x52 markers before activation with "Quantum features not yet active" error
    - _Requirements: 9.1, 9.2, 9.3, 9.4_

  - [x] 7.2 Write property test for activation enforcement
    - **Property 11: Activation Height Enforcement**
    - **Validates: Requirements 9.1, 9.2, 9.3**

  - [x] 7.3 Write property test for registration idempotence
    - **Property 3: Registration Idempotence**
    - **Validates: Requirements 2.2, 2.3**

- [x] 8. Implement RPC commands
  - [x] 8.1 Create `src/rpc/quantum.cpp` with RPC implementations
    - Implement `getquantumpubkey` - lookup by hash, return pubkey or error -32001
    - Implement `getquantumregistrystats` - return total keys, db size, cache stats
    - Implement `isquantumpubkeyregistered` - return boolean for hash existence
    - Register commands under "quantum" help category
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_

  - [x] 8.2 Register RPC commands in `rpc/register.h`
    - Add quantum RPC registration function
    - Include in server initialization
    - _Requirements: 7.6_

  - [x] 8.3 Write unit tests for RPC commands
    - Test `getquantumpubkey` with valid and invalid hashes
    - Test `getquantumregistrystats` response format
    - Test `isquantumpubkeyregistered` boolean responses
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5_

- [x] 9. Implement initialization and shutdown
  - [x] 9.1 Add registry initialization in `init.cpp`
    - Call `InitQuantumRegistry()` during node startup
    - Handle initialization failure gracefully (log error, continue without compact mode)
    - _Requirements: 1.4, 1.6_

  - [x] 9.2 Add registry shutdown in `init.cpp`
    - Call `ShutdownQuantumRegistry()` during node shutdown
    - Flush pending writes before closing
    - _Requirements: 1.5_

  - [x] 9.3 Implement `-rebuildquantumregistry` startup option
    - Add command-line argument parsing
    - Implement blockchain scan from activation height
    - Extract and register public keys from 0x51 witnesses
    - _Requirements: 8.5_

- [x] 10. Update build system
  - [x] 10.1 Update `src/Makefile.am`
    - Add `quantum_registry.cpp` and `quantum_registry.h` to source lists
    - Add `rpc/quantum.cpp` to RPC sources
    - Add test files to test sources
    - _Requirements: N/A (build configuration)_

- [ ] 11. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- All tasks including property tests are required for comprehensive testing
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties
- Unit tests validate specific examples and edge cases
- The implementation follows existing patterns from `cvmdb.h/cpp` and `address_quantum.h/cpp`
