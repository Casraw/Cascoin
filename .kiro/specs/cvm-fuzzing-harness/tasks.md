# Implementation Plan: CVM Fuzzing Harness

## Overview

This plan implements a CVM-specific fuzzing harness for Cascoin in C++ (C++17, Boost test framework). It builds incrementally: first the build wiring so a skeleton harness compiles, then the input decoder (`FuzzConsumer`) and selector, the in-memory `ContractStorage_Mock`, the native CVM dispatcher/handlers and invariant checkers, the fuzz-engine entry points and CLI replay, the native correctness properties (P1–P9), then the EVM/EnhancedVM wiring and its properties (P10–P15) under `ENABLE_EVMC`, followed by the seed corpus, CLI-replay smoke test and reproducibility property (P16), the documentation update, and a final cross-configuration build/test checkpoint.

New/changed files:
- `src/test/test_cvm_fuzzy.cpp` → binary `test_cascoin_cvm_fuzzy` (harness).
- `src/test/cvm_fuzz_property_tests.cpp` → part of `test_cascoin` (property + smoke tests).
- `src/Makefile.test.include` → new `noinst_PROGRAMS` target + `BITCOIN_TESTS` entry.
- `src/test/fuzz_seeds/cvm/` → seed corpus.
- `doc/developer/fuzzing.md` → CVM section.

All EVM/router code is gated behind `#ifdef ENABLE_EVMC`.

## Tasks

- [x] 1. Set up build integration and source skeletons
  - [x] 1.1 Wire build targets and create skeleton sources
    - Add `noinst_PROGRAMS += test/test_cascoin_cvm_fuzzy` and the `test_test_cascoin_cvm_fuzzy_*` block to `src/Makefile.test.include`, mirroring the existing `test_test_cascoin_fuzzy_*` block (same `LDADD` list including `$(EVMC_LIBS)`, `$(BOOST_LIBS)`, `$(CRYPTO_LIBS)`, `$(LIBOQS_LIBS)`)
    - Add `test/cvm_fuzz_property_tests.cpp` to the `BITCOIN_TESTS` list so it links into `test_cascoin`
    - Create `src/test/test_cvm_fuzzy.cpp` with a minimal `int test_one_input(std::vector<uint8_t>)` returning 0 and a weak `main`, so the target links and produces a runnable binary
    - Create `src/test/cvm_fuzz_property_tests.cpp` with a Boost `BOOST_AUTO_TEST_SUITE(cvm_fuzz_property_tests)` and one trivial placeholder case so the suite compiles under `test_cascoin`
    - _Requirements: 9.1, 9.2, 9.3, 9.6_

- [x] 2. Implement input decoding (selector + FuzzConsumer)
  - [x] 2.1 Define the selector enum and derived context structure
    - Add `enum CvmFuzzTarget : uint8_t { CVM_EXECUTE=0, CVM_VERIFY_BYTECODE, CVM_DEPLOY_CONTRACT, CVM_CALL_CONTRACT, EXECUTE_CONTRACT, EVM_EXECUTE, EVM_DEPLOY_CONTRACT, EVM_STATIC_CALL, EVM_DELEGATE_CALL, ENHANCED_EXECUTE, ENHANCED_DEPLOY_CONTRACT, ENHANCED_CALL_CONTRACT, BYTECODE_DETECT, CVM_FUZZ_TARGET_END }` to `test_cvm_fuzzy.cpp`
    - Add the internal `FuzzContext` struct (gasLimit, callValue, blockHeight, timestamp, contractAddr, callerAddr, blockHash, code) per the design data model
    - Keep `CVM_FUZZ_TARGET_END = 13` stable regardless of `ENABLE_EVMC` so selector values and corpus stay portable
    - _Requirements: 1.1, 14.1, 14.2_
  - [x] 2.2 Implement the FuzzConsumer (tail-consumption context derivation)
    - Implement `FuzzConsumer(const uint8_t* data, size_t size)` with `ConsumeIntegralFromTail<T>()`, `ConsumeUint160FromTail()`, `ConsumeUint256FromTail()`, `ConsumeGasLimit()`, and `ConsumeRemainingFront()`
    - Read context fields from the end of the buffer; return zero-filled values when bytes are missing (no error path)
    - `ConsumeGasLimit()` returns `uint32 % (MAX_GAS_PER_TX + 1)` so the value is always clamped to 0..1,000,000
    - Mask the block-height high bit so `blockHeight >= 0`
    - _Requirements: 1.5, 14.6_
    - _Properties: P8_

- [x] 3. Implement the in-memory ContractStorage mock
  - [x] 3.1 Implement ContractStorage_Mock
    - Implement `ContractStorage_Mock : public CVM::ContractStorage` backed by `std::map<std::pair<uint160,uint256>, uint256>` plus a `std::set<uint160>` for address tracking
    - `Load` returns the last stored value and `true`, or leaves `out` unchanged and returns `false` for an unset pair
    - `Store` overwrites the value for a pair and isolates all other pairs; `Exists(addr)` returns true iff at least one `Store` occurred for that address (real interface signature)
    - Keep all data in memory only (no disk access); add a `Snapshot()` accessor for the determinism checker
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7_

- [x] 4. Implement native dispatch and invariant checkers
  - [x] 4.1 Implement native handlers and the dispatcher
    - Implement `test_one_input`: empty buffer → return 0; read `buffer[0]` as selector; `selector >= CVM_FUZZ_TARGET_END` → return 0
    - Derive context via `FuzzConsumer`; create a fresh `ContractStorage_Mock` per entry-point call
    - Implement `RunCvmExecute`, `RunCvmVerify`, `RunCvmDeploy`, `RunCvmCall`, `RunExecuteContract` and wire them into the `switch(selector)` for selectors 0–4
    - Wrap entry-point calls in a `try`/`catch`; expected exceptions → return 0; on return → status 0
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7_
  - [x] 4.2 Implement native invariant checker functions
    - Implement `CheckGasBounds(gasUsed, gasLimit)` (`gasUsed <= gasLimit`), `CheckStackBound(VMState)` (`StackSize() <= MAX_STACK_SIZE`), `CheckTerminated(VMState)` (status != RUNNING)
    - Implement status-classification checks for oversized code → `VM_ERROR`, invalid opcode → `INVALID_OPCODE`, invalid jump → `INVALID_JUMP`, with no state commit; `VerifyBytecode` empty/oversized → `false`
    - For `ExecuteContract` assert `result.gasUsed == state.GetGasUsed()`
    - Violations trigger `abort()`/assert; defined error results are NOT violations
    - Call the checkers from the native handlers after each entry point
    - _Requirements: 3.1, 3.2, 4.1, 4.3, 4.4, 5.1, 5.2, 5.3, 6.1, 6.4, 7.1, 7.2, 7.3, 7.4, 7.5, 7.6_

- [x] 5. Implement fuzz-engine entry points and CLI replay
  - [x] 5.1 Implement stdin reader, libFuzzer/AFL entry points, weak main, and CLI replay
    - Port `read_stdin` from `test_bitcoin_fuzzy.cpp` (cap `1<<20` bytes)
    - Add `extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t)` and `LLVMFuzzerInitialize` accepting 0..≥1 MiB inputs
    - Add a weak `main` with `#ifdef __AFL_INIT` deferred forkserver and `#ifdef __AFL_LOOP` persistent mode over `read_stdin`
    - Implement CLI replay: if `argv[1]` is present, read exactly that file instead of stdin; an unreadable/missing file → error message to stderr and defined exit without a self-crash
    - _Requirements: 9.4, 9.5, 12.4, 12.5_

- [x] 6. Implement native correctness property tests (P1–P9)
  - [x]* 6.1 Property test P1 — crash freedom and termination of the native CVM
    - Generate buffers of length 0..24,576 routed through native entry points with gas 0..MAX_GAS_PER_TX; assert no crash/UB and final status != RUNNING (≥100 iterations)
    - _Requirements: 3.1, 3.4, 4.1, 1.6, 1.7_
    - _Properties: P1_
  - [x]* 6.2 Property test P2 — gas bounds and result consistency
    - After `ExecuteContract`: assert `0 <= gasUsed <= gasLimit` and `ExecutionResult.gasUsed == VMState::GetGasUsed()` (≥100 iterations)
    - _Requirements: 5.1, 5.2, 5.3_
    - _Properties: P2_
  - [x]* 6.3 Property test P3 — stack bound
    - Assert `VMState::StackSize() <= MAX_STACK_SIZE` at end of execution across generated buffers (≥100 iterations)
    - _Requirements: 6.1, 6.4_
    - _Properties: P3_
  - [x]* 6.4 Property test P4 — status classification of invalid bytecode
    - Assert oversized → `VM_ERROR`, invalid opcode → `INVALID_OPCODE`, invalid jump → `INVALID_JUMP`, with no state commit (≥100 iterations)
    - _Requirements: 7.4, 7.5, 7.6_
    - _Properties: P4_
  - [ ]* 6.5 Property test P5 — VerifyBytecode classification by size and opcodes
    - Assert length 0 or > MAX_CODE_SIZE → `false`; buffers of only valid opcode bytes (length 1..MAX_CODE_SIZE) → `true` (≥100 iterations)
    - _Requirements: 7.1, 7.2, 7.3_
    - _Properties: P5_
  - [ ]* 6.6 Property test P6 — determinism of native execution
    - Run `ExecuteContract` ≥100 times with a fresh mock per run for identical code/context/initial state; assert identical `success`, `gasUsed`, `returnData`, status, and `Snapshot()` final state
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_
    - _Properties: P6_
  - [ ]* 6.7 Property test P7 — ContractStorage_Mock semantics
    - Over random `(uint160, uint256, uint256)` triples: `Store` then `Load` returns last value + true; unset pair → false and `out` unchanged; `Store` isolates other pairs; `Exists(addr)` true iff a `Store` occurred (≥100 iterations)
    - _Requirements: 2.2, 2.3, 2.4, 2.5, 2.6_
    - _Properties: P7_
  - [ ]* 6.8 Property test P8 — gas-limit derivation within the valid range
    - Assert `FuzzConsumer`-derived gas limit is always in 0..MAX_GAS_PER_TX for arbitrary buffers and any selector (≥100 iterations)
    - _Requirements: 1.5, 14.6_
    - _Properties: P8_
  - [ ]* 6.9 Property test P9 — deterministic, valid selector routing
    - Assert first byte maps deterministically to at most one target; value >= `CVM_FUZZ_TARGET_END` (and, without `ENABLE_EVMC`, any EVM/router selector) → no entry-point call and status 0 (≥100 iterations)
    - _Requirements: 1.1, 1.3, 14.1, 14.2, 14.3_
    - _Properties: P9_

- [ ] 7. Checkpoint - native path builds and native property tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 8. Implement EVM and router wiring (under ENABLE_EVMC)
  - [x] 8.1 Implement EvmFixture and EVMEngine handlers
    - Add the `#ifdef ENABLE_EVMC` `EvmFixture` (in-memory `CVMDatabase` with `fMemory=true, fWipe=true` + a test `TrustContext` + real `EVMCHost` wiring; no disk access)
    - Implement `RunEvmExecute`, `RunEvmDeploy`, `RunEvmStaticCall`, `RunEvmDelegateCall` calling only public `EVMEngine` entry points and passing derived context (gas clamped, addresses, value, block context, input data)
    - Treat an unloadable evmone interpreter as a defined error → return 0; never call internal evmone symbols
    - _Requirements: 14.1, 14.5, 14.6, 14.7, 15.3, 15.4, 16.1, 16.2, 18.1_
  - [x] 8.2 Implement EnhancedVM and BytecodeDetector handlers plus no-EVMC no-ops
    - Implement `RunEnhancedExecute`, `RunEnhancedDeploy`, `RunEnhancedCall` via `EnhancedVMFactory::CreateProductionVM`, and `RunBytecodeDetect` via `BytecodeDetector::DetectFormat`
    - Wire selectors 5–12 into the dispatcher inside `#ifdef ENABLE_EVMC`; add the `#else` branch making selectors 5–12 no-ops returning status 0
    - _Requirements: 14.2, 14.3, 14.4, 14.5, 14.7, 17.1, 17.3, 17.4, 17.5, 17.6_
  - [x] 8.3 Implement EVM/router invariant checkers
    - Add gas-bounds check (`0 <= gas_used <= gasLimit`) for `EVMExecutionResult`/`EnhancedExecutionResult`
    - Add routing-consistency check: `executed_format` equals the routed format; `UNKNOWN` → `success == false` with `error` set; oversized (> `EnhancedVM::MAX_BYTECODE_SIZE`) → `success == false`
    - Explicitly do NOT treat EVMC error status codes (`EVMC_FAILURE`, `EVMC_REVERT`, out-of-gas) as violations; only crash/UB/assertion/engine-independent invariant breaks abort
    - _Requirements: 15.1, 15.2, 16.3, 16.4, 17.3, 17.4, 17.6_

- [ ] 9. Implement EVM/router correctness property tests (P10–P15)
  - [ ]* 9.1 Property test P10 — crash freedom and gas bounds of the EVM/router path
    - Under `ENABLE_EVMC`, generate buffers 0..24,576 through `EVMEngine`/`EnhancedVM` entry points; assert no crash/UB, defined result, and `0 <= gas_used <= gasLimit` (≥100 iterations)
    - _Requirements: 16.1, 16.2, 16.3, 14.7_
    - _Properties: P10_
  - [ ]* 9.2 Property test P11 — totality and determinism of the BytecodeDetector
    - Assert `DetectFormat` returns exactly one of {CVM_NATIVE, EVM_BYTECODE, HYBRID, UNKNOWN} without crash and is stable for bit-identical input (≥100 iterations)
    - _Requirements: 17.1, 17.2_
    - _Properties: P11_
  - [ ]* 9.3 Property test P12 — consistency of format detection and routing
    - Assert non-`UNKNOWN` inputs produce `executed_format` equal to the routed format; `UNKNOWN` → `success == false` with `error` set and no crash (≥100 iterations)
    - _Requirements: 17.3, 17.4, 17.5, 17.6_
    - _Properties: P12_
  - [ ]* 9.4 Property test P13 — EVMCHost storage and account semantics
    - Assert `set_storage`→`get_storage` round-trip byte-identical; unset pair → zero value; non-existent account callbacks (`account_exists`, `get_balance`, `get_code_size`, `get_code_hash`, `copy_code`) return defined defaults without crash (≥100 iterations)
    - _Requirements: 18.2, 18.3, 18.4_
    - _Properties: P13_
  - [ ]* 9.5 Property test P14 — transient storage round-trip and reset
    - Assert transient callback pointers are non-null; `set_transient_storage`→`get_transient_storage` round-trip within one execution; after `ClearTransientStorage`/new execution → zero value for every pair (≥100 iterations)
    - _Requirements: 18.5, 18.6, 18.7_
    - _Properties: P14_
  - [ ]* 9.6 Property test P15 — determinism of the EVM/router path
    - Run `EnhancedVM` (and `EVMEngine`) ≥100 times with a fresh `EvmFixture`; assert identical `success`, `gas_used`, `return_data`, `executed_format`, and host final state (Engine: `status_code`, `output_data`)
    - _Requirements: 19.1, 19.2, 19.3, 19.4_
    - _Properties: P15_

- [x] 10. Create the seed corpus
  - [x] 10.1 Create seed corpus files in src/test/fuzz_seeds/cvm/
    - Add raw byte-buffer seeds in harness input format (byte 0 = selector, front = code/inputData, tail = context), each ≤ 24 KB, with tail bytes chosen so a high valid gas limit is derived
    - Cover at least one seed per Req 1 / Req 14 entry point (selectors 0–5, 9, 12), a `VerifyBytecode`-valid seed, a `VerifyBytecode`-invalid seed, and an SSTORE-then-SLOAD round-trip seed
    - _Requirements: 10.1, 10.2, 10.3, 10.5_
  - [ ]* 10.2 Smoke test over the seed corpus
    - Add a Boost test that replays every file under `src/test/fuzz_seeds/cvm/` through the harness (CLI replay of `test_cascoin_cvm_fuzzy`) and asserts each completes without an invariant violation; invalid seeds are discarded without a violation
    - _Requirements: 10.4, 10.6_

- [ ] 11. Implement crash reproducibility test
  - [ ]* 11.1 Property test P16 — reproducibility of stored crash inputs
    - Replay each corpus/known input via CLI replay multiple times and assert the observable result is identical on 100% of runs (deterministic replay)
    - _Requirements: 12.3_
    - _Properties: P16_

- [x] 12. Update the fuzzing documentation
  - [x] 12.1 Add the CVM section to doc/developer/fuzzing.md
    - Add a standalone headed CVM section stating the binary name `test_cascoin_cvm_fuzzy` and the exact build command
    - List every native entry point selectable via the Entry_Point_Selector with a one-sentence description, and how to set the selector to a specific entry point (first input byte)
    - List the EVM_Engine (`Execute`, `DeployContract`, `StaticCall`, `DelegateCall`) and EnhancedVM (`Execute`, `DeployContract`, `CallContract`) entry points, note they are active only with `ENABLE_EVMC` (no-op status 0 otherwise), and that evmone/evmc is a trusted, non-fuzzed third-party library
    - Specify the seed corpus path `src/test/fuzz_seeds/cvm/` (relative to repo root)
    - Add complete build/run commands with a bounding parameter (max runtime or max executions), input+output directory arguments, per-step success signals, the in-process vs regtest note, and the regtest reproduction steps using `deploycontract`, `callcontract`, `sendcvmcontract`
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 13.1, 13.2, 13.3, 13.4, 13.5, 13.6, 13.7_

- [ ] 13. Final checkpoint - cross-configuration build and property suite
  - [ ] 13.1 Build with and without ENABLE_EVMC and run the CVM property suite
    - Build `test_cascoin_cvm_fuzzy` and `test_cascoin` both with `ENABLE_EVMC` defined and with it undefined; confirm both configurations compile and link
    - Run `src/test/test_cascoin --run_test=cvm_fuzz_property_tests` and confirm all property, smoke, and reproducibility tests pass
    - _Requirements: 9.2, 14.4_

## Notes

- Tasks marked with `*` are optional test tasks and can be skipped for a faster MVP, but every correctness property P1–P16 is covered by exactly one such task.
- Each task references specific requirement sub-clauses (and, where relevant, design properties) for traceability.
- Property tests live in `src/test/cvm_fuzz_property_tests.cpp` (suite `cvm_fuzz_property_tests`, binary `test_cascoin`) and run ≥100 iterations each, via rapidcheck if available, otherwise a hand-written generator loop.
- The harness `src/test/test_cvm_fuzzy.cpp` (binary `test_cascoin_cvm_fuzzy`) is the fuzz target; all EVM/router code is gated behind `#ifdef ENABLE_EVMC`.
- No long-running fuzzing campaigns, deployment, or user-acceptance tasks are included; the CLI-replay smoke test over the seed corpus is the CI-safe substitute.

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["2.1"] },
    { "id": 2, "tasks": ["2.2"] },
    { "id": 3, "tasks": ["3.1", "10.1"] },
    { "id": 4, "tasks": ["4.1"] },
    { "id": 5, "tasks": ["4.2"] },
    { "id": 6, "tasks": ["5.1"] },
    { "id": 7, "tasks": ["8.1", "6.1"] },
    { "id": 8, "tasks": ["8.2", "6.2"] },
    { "id": 9, "tasks": ["8.3", "6.3"] },
    { "id": 10, "tasks": ["6.4"] },
    { "id": 11, "tasks": ["6.5"] },
    { "id": 12, "tasks": ["6.6"] },
    { "id": 13, "tasks": ["6.7"] },
    { "id": 14, "tasks": ["6.8"] },
    { "id": 15, "tasks": ["6.9"] },
    { "id": 16, "tasks": ["9.1"] },
    { "id": 17, "tasks": ["9.2"] },
    { "id": 18, "tasks": ["9.3"] },
    { "id": 19, "tasks": ["9.4"] },
    { "id": 20, "tasks": ["9.5"] },
    { "id": 21, "tasks": ["9.6"] },
    { "id": 22, "tasks": ["10.2", "12.1"] },
    { "id": 23, "tasks": ["11.1"] },
    { "id": 24, "tasks": ["13.1"] }
  ]
}
```
