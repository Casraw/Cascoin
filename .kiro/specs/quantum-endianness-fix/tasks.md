# Implementation Plan: Quantum Endianness Fix

## Overview

Fix the endianness inconsistency in the quantum witness program data flow. The fix adds a BE→LE reversal in `DecodeDestination()` and a LE→BE reversal in the `WitnessV2Quantum` encoder, then removes all compensating reversals from downstream code paths (verification, signing, ownership). Also fixes the `Hash()` → `CSHA256` mismatch in `VerifyQuantumTransaction()`. All changes are scoped to `TX_WITNESS_V2_QUANTUM` / `WitnessV2Quantum` code paths only.

## Tasks

- [x] 1. Fix DecodeDestination and WitnessV2Quantum encoder in base58.cpp
  - [x] 1.1 Add BE→LE byte reversal in DecodeDestination() quantum branch
    - In `src/base58.cpp`, locate the quantum Bech32m decode branch where `WitnessV2Quantum` is constructed
    - Replace direct `std::copy(data.begin(), data.end(), quantum.begin())` with a reverse loop: `quantum.begin()[i] = data[31 - i]`
    - _Requirements: 1.1, 1.3, 2.3_
  - [x] 1.2 Add LE→BE byte reversal in EncodeDestination visitor for WitnessV2Quantum
    - In `src/base58.cpp`, locate the `EncodeDestination` visitor for `WitnessV2Quantum`
    - Before calling `ConvertBits`, reverse the 32 bytes from LE to BE into a temporary `programBE` vector
    - Pass `programBE` to `ConvertBits` instead of `id.begin()`/`id.end()`
    - _Requirements: 1.2, 2.2_

- [x] 2. Remove compensating reversals from verification path in interpreter.cpp
  - [x] 2.1 Remove first programLE reversal loop in VerifyWitnessV2Quantum (registry format path)
    - In `src/script/interpreter.cpp`, locate the first `programLE` reversal loop in `VerifyWitnessV2Quantum()`
    - Replace `memcmp(pubkeyHash.begin(), programLE.data(), 32)` with `memcmp(pubkeyHash.begin(), program.data(), 32)`
    - Remove the `programLE` vector and reversal loop
    - _Requirements: 3.1, 3.2_
  - [x] 2.2 Remove second programLE reversal loop in VerifyWitnessV2Quantum (standard format path)
    - Locate the second `programLE` reversal loop in the same function
    - Apply the same fix: direct comparison against `program.data()`
    - _Requirements: 3.1, 3.2_

- [x] 3. Remove compensating reversals from signing path in sign.cpp
  - [x] 3.1 Remove byte reversal in ProduceSignature() quantum branch
    - In `src/script/sign.cpp`, locate the quantum signing branch in `ProduceSignature()`
    - Replace the reverse-copy loop with `memcpy(pubkeyHash.begin(), result[0].data(), 32)`
    - _Requirements: 4.1_
  - [x] 3.2 Remove byte reversal in IsSolvable() quantum branch
    - In `src/script/sign.cpp`, locate the quantum branch in `IsSolvable()`
    - Replace the reverse-copy loop with `memcpy(pubkeyHash.begin(), vSolutions[0].data(), 32)`
    - _Requirements: 4.2_

- [x] 4. Remove compensating reversals from ownership detection in ismine.cpp
  - [x] 4.1 Remove byte reversal in IsMine() TX_WITNESS_UNKNOWN fallback path
    - In `src/script/ismine.cpp`, locate the `TX_WITNESS_UNKNOWN` fallback path
    - Replace the reverse-copy loop with `memcpy(witnessProgram.begin(), vSolutions[X].data(), 32)`
    - _Requirements: 4.3_
  - [x] 4.2 Remove byte reversal in IsMine() TX_WITNESS_V2_QUANTUM path
    - In `src/script/ismine.cpp`, locate the `TX_WITNESS_V2_QUANTUM` path
    - Replace the reverse-copy loop with `memcpy(witnessProgram.begin(), vSolutions[X].data(), 32)`
    - _Requirements: 4.3_

- [x] 5. Fix hash function in VerifyQuantumTransaction
  - [x] 5.1 Replace Hash() with CSHA256 in VerifyQuantumTransaction()
    - In `src/cvm/quantum_registry.cpp`, locate the `Hash(pubkey.begin(), pubkey.end())` call
    - Replace with `CSHA256().Write(pubkey.data(), pubkey.size()).Finalize(computedProgram.begin())`
    - Ensure `#include <crypto/sha256.h>` is present
    - _Requirements: 5.4_

- [x] 6. Checkpoint — Compile and verify no regressions
  - Build with `make -j$(nproc)` and ensure clean compilation
  - Run existing test suite: `src/test/test_cascoin --run_test=quantum_tests`
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Write unit tests for the endianness fix
  - [x] 7.1 Write address encoding round-trip unit test
    - In `src/test/quantum_tests.cpp`, add a test that generates a FALCON-512 keypair, calls `EncodeQuantumAddress()` then `DecodeDestination()`, and asserts the resulting `WitnessV2Quantum` bytes match `GetQuantumID()` output
    - _Requirements: 9.1_
  - [x] 7.2 Write decode-encode round-trip unit test
    - Add a test that takes a known quantum address string, decodes via `DecodeDestination()`, re-encodes via `EncodeDestination()`, and asserts the strings match
    - _Requirements: 9.1_
  - [x] 7.3 Write script extraction unit test
    - Add a test that creates a `WitnessV2Quantum` from `GetQuantumID()`, calls `GetScriptForDestination()`, extracts the 32-byte program, and asserts it matches `GetQuantumID()` output
    - _Requirements: 9.2_
  - [x] 7.4 Write hash consistency unit test
    - Add a test that computes the pubkey hash via `GetQuantumID()`, `GetQuantumWitnessProgram()`, `ParseQuantumWitness()`, and `VerifyQuantumTransaction()` internal logic, asserting all four produce identical results
    - _Requirements: 9.4_
  - [x] 7.5 Write signing unit test
    - Add a test that creates a quantum output, adds the keypair to a keystore, and verifies `ProduceSignature()` succeeds
    - _Requirements: 9.3_
  - [x] 7.6 Write ECDSA non-interference unit test
    - Add a test that signs and verifies a standard P2WPKH transaction, confirming behavior is unchanged
    - _Requirements: 9.5_
  - [x] 7.7 Write edge case tests for invalid quantum addresses
    - Add tests for empty string, wrong HRP, wrong program length — all should return `CNoDestination`
    - _Requirements: 9.1_

- [x] 8. Checkpoint — Run unit tests
  - Run: `src/test/test_cascoin --run_test=quantum_tests`
  - Ensure all tests pass, ask the user if questions arise.

- [x] 9. Write property-based tests for correctness properties
  - [x] 9.1 Write property test for address encoding round-trip
    - Create `src/test/quantum_endianness_property_tests.cpp`
    - Use `BOOST_DATA_TEST_CASE` with 100+ randomly generated FALCON-512 keypairs
    - For each keypair: encode → decode → assert bytes match `GetQuantumID()`
    - Tag: `Feature: quantum-endianness-fix, Property 1: Address encoding round-trip`
    - **Property 1: Address encoding round-trip**
    - **Validates: Requirements 1.1, 1.3, 2.1, 2.3, 9.1**
  - [x] 9.2 Write property test for address decoding round-trip
    - For each generated keypair: encode to address string, decode, re-encode, assert strings match
    - Tag: `Feature: quantum-endianness-fix, Property 2: Address decoding round-trip`
    - **Property 2: Address decoding round-trip**
    - **Validates: Requirements 2.2**
  - [x] 9.3 Write property test for script canonical bytes
    - For each generated keypair: create `WitnessV2Quantum` from `GetQuantumID()`, build script, extract program, assert matches `GetQuantumID()`
    - Tag: `Feature: quantum-endianness-fix, Property 3: Script stores canonical bytes`
    - **Property 3: Script stores canonical bytes**
    - **Validates: Requirements 1.2, 3.1, 9.2**
  - [x] 9.4 Write property test for hash consistency
    - For each generated keypair: compute hash via all four functions, assert all equal
    - Tag: `Feature: quantum-endianness-fix, Property 4: Hash consistency across all quantum functions`
    - **Property 4: Hash consistency across all quantum functions**
    - **Validates: Requirements 3.3, 5.1, 5.2, 5.3, 5.4, 5.5, 9.4**
  - [x] 9.5 Write property test for signing path
    - For each generated keypair: add to keystore, create quantum output, verify `ProduceSignature()`, `IsSolvable()`, `IsMine()` all succeed
    - Tag: `Feature: quantum-endianness-fix, Property 5: Signing path works with canonical bytes`
    - **Property 5: Signing path works with canonical bytes**
    - **Validates: Requirements 4.1, 4.2, 4.3, 9.3**
  - [x] 9.6 Write property test for ECDSA non-interference
    - For each generated ECDSA keypair: verify `GetScriptForDestination()`, `ProduceSignature()`, `VerifyScript()`, `IsSolvable()`, `IsMine()` all work identically
    - Tag: `Feature: quantum-endianness-fix, Property 6: ECDSA non-interference`
    - **Property 6: ECDSA non-interference**
    - **Validates: Requirements 8.1, 8.2, 9.5**

- [x] 9.7 Register property test file in build system
    - Add `quantum_endianness_property_tests.cpp` to `src/Makefile.test.include`
    - Ensure it compiles and links with `make check`

- [x] 10. Final checkpoint — Full test suite
  - Run: `src/test/test_cascoin --run_test=quantum_tests`
  - Run: `src/test/test_cascoin --run_test=quantum_endianness_property_tests`
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- The fix is a single-point correction at the decode boundary (task 1), with all other implementation tasks (2–5) being removal of now-unnecessary compensating code
- Property tests use `BOOST_DATA_TEST_CASE` with randomly generated FALCON-512 keypairs (minimum 100 iterations each)
- Test binary is `src/test/test_cascoin` (not test_bitcoin)
- Testnet reset is acceptable per Requirement 7.3, so no dual-recognition migration code is needed
