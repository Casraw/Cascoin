# Requirements Document

## Introduction

This specification describes building a real fuzzing harness for the Cascoin Virtual Machine (CVM) as well as a repeatable way to run it (including a regtest-based reproduction/validation path).

The existing generic fuzz harness (`src/test/test_bitcoin_fuzzy.cpp`, built as the binary `test_cascoin_fuzzy`, documented in `doc/developer/fuzzing.md`, AFL-based) exclusively tests the deserialization of standard Bitcoin structures (CBlock, CTransaction, CAddrMan, etc.). It does not exercise the CVM in any way. The goal of this specification is to close that gap: a CVM-specific fuzz target that feeds arbitrary and structured inputs to the public CVM entry points and thereby checks core safety and correctness invariants of the CVM.

The public CVM entry points are (from `src/cvm/cvm.h`):
- `CVM::Execute(code, VMState&, ContractStorage*)`
- `CVM::DeployContract(code, contractAddr, storage)`
- `CVM::CallContract(contractAddr, inputData, VMState&, storage)`
- `CVM::VerifyBytecode(code)` (static)
- Free function `ExecuteContract(code, gasLimit, contractAddr, callerAddr, callValue, inputData, blockHeight, blockHash, timestamp, storage)` returning `ExecutionResult{success, gasUsed, returnData, logs, error}`

The relevant limits/invariants (from `src/cvm/vmstate.h` and `src/cvm/cvm.h`) are: `MAX_STACK_SIZE = 1024`, `MAX_CODE_SIZE = MAX_CONTRACT_SIZE = 24576` (24KB), `MAX_GAS_PER_TX = 1,000,000`, `MAX_GAS_PER_BLOCK = 10,000,000`, `MAX_CALL_DEPTH = 256`, as well as the `VMState::Status` enum with the values RUNNING, STOPPED, RETURNED, REVERTED, OUT_OF_GAS, STACK_OVERFLOW, STACK_UNDERFLOW, INVALID_OPCODE, INVALID_JUMP, VM_ERROR.

The primary fuzzing path is in-process (in the same process, using an in-memory mock implementation of `ContractStorage`). Regtest serves to reproduce found inputs and validate them end-to-end, not as the primary fuzzing loop.

Beyond the native CVM, the harness covers two further execution layers of Cascoin. The CVM architecture consists of three layers:

1. **Native CVM** (`CVM::CVM` in `src/cvm/cvm.cpp`, opcodes in `src/cvm/opcodes.h`): the register-based core with roughly 40 proprietary opcodes. This layer is covered by Requirements 1 through 13.
2. **EVM integration layer** (`CVM::EVMEngine` in `src/cvm/evm_engine.cpp`, together with the host binding `CVM::EVMCHost` in `src/cvm/evmc_host.cpp`): executes Ethereum bytecode via the external evmone interpreter (EVMC interface). The Ethereum opcode set (140+) is implemented by the external evmone interpreter, not by Cascoin code. This layer is only compiled WHEN the preprocessor macro `ENABLE_EVMC` is defined (in the current build `ENABLE_EVMC = 1`, see `config/bitcoin-config.h`); evmone/evmc are linked externally via `-levmone -levmc-loader -levmc-instructions`.
3. **Routing layer** (`CVM::EnhancedVM` in `src/cvm/enhanced_vm.cpp`, created via `EnhancedVMFactory::CreateProductionVM`): the production router that classifies the bytecode format via a `BytecodeDetector` (`src/cvm/bytecode_detector.cpp`) and forwards execution to the native CVM or the EVM engine, including cross-format contract calls.

For the EVM/router path the following applies: the external evmone/evmc interpreter is treated as a trusted third-party library. The fuzzing target is NOT the evmone interpreter itself, but the Cascoin integration code (the `EVMCHost` callbacks and the `EnhancedVM` router) as well as engine-independent invariants. Internal evmone error status codes (e.g. `EVMC_FAILURE`, `EVMC_REVERT`, out-of-gas) count as a defined result and NOT as an invariant violation. The harness builds and runs both with and without `ENABLE_EVMC` defined.

## Glossary

- **CVM (Cascoin Virtual Machine)**: Register-based virtual machine with stack operations for executing smart contract bytecode on Cascoin. Implemented in `src/cvm/`.
- **Fuzzing**: Automated testing technique that repeatedly invokes a target program with generated (often mutated or random) inputs to uncover crashes, undefined behavior, or violated invariants.
- **Fuzz_Harness**: The executable test program that takes an input (byte buffer), forwards it to the CVM entry points under test, and checks invariants. In the context of this specification, the CVM-specific fuzz target.
- **Fuzz_Engine**: The external tool that generates inputs and drives the Fuzz_Harness (e.g. AFL or libFuzzer).
- **AFL (American Fuzzy Lop)**: Coverage-guided Fuzz_Engine that works via compiled instrumentation as well as the operating modes "persistent mode" and "deferred forkserver". Already used in `doc/developer/fuzzing.md`.
- **libFuzzer**: In-process Fuzz_Engine of the LLVM/Clang toolchain that expects an entry function `LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)`.
- **Corpus**: Collection of input files that a Fuzz_Engine uses as a starting point (Seed_Corpus) and as storage for interesting discovered inputs.
- **Seed_Corpus**: The initial, curated corpus of valid and partially valid CVM bytecode with which fuzzing is started.
- **Property_Based_Testing**: Testing approach in which general properties (invariants) are checked across a large space of generated inputs, instead of comparing individual example values.
- **Regtest**: Cascoin's local regression-test network mode, in which blocks are generated deterministically and CVM contracts can be executed via RPC (`deploycontract`, `callcontract`, `sendcvmcontract`).
- **ContractStorage_Mock**: In-memory implementation of the abstract interface `ContractStorage` (Load/Store/Exists) for deterministic, side-effect-free use by the Fuzz_Harness.
- **Entry_Point_Selector**: A value derived from the first bytes of the input that selects which CVM entry point is fuzzed (analogous to the `TEST_ID` pattern in `test_bitcoin_fuzzy.cpp`).
- **ExecutionResult**: Result structure of the `ExecuteContract` function with the fields `success`, `gasUsed`, `returnData`, `logs`, `error`.
- **Crash_Input**: A concrete input that triggers a crash or invariant violation in the Fuzz_Harness and is stored for reproduction.
- **ENABLE_EVMC**: Preprocessor macro that controls whether the EVM integration layer (`EVMEngine`, `EVMCHost`) is compiled. Defined in `config/bitcoin-config.h`; in the current build `ENABLE_EVMC = 1`.
- **EVM_Engine (EVMEngine)**: Cascoin integration class `CVM::EVMEngine` in `src/cvm/evm_engine.cpp` that executes Ethereum bytecode via the external evmone interpreter. Entry points: `Execute`, `DeployContract`, `StaticCall`, `DelegateCall`.
- **EVMC / evmone**: EVMC is the C interface between Cascoin and an Ethereum interpreter; evmone is the external interpreter treated as trusted that implements the Ethereum opcode set (140+). Linked externally via `-levmone -levmc-loader -levmc-instructions`. Not a fuzzing target.
- **EVMCHost**: Cascoin class `CVM::EVMCHost` in `src/cvm/evmc_host.cpp` that implements the EVMC host callbacks (including `account_exists`, `get_storage`, `set_storage`, `get_balance`, `get_code_size`, `get_code_hash`, `copy_code`, `selfdestruct`, `call`, `get_tx_context`, `get_block_hash`, `emit_log` as well as the transient storage callbacks `get_transient_storage`/`set_transient_storage`).
- **EnhancedVM (Router)**: Production router `CVM::EnhancedVM` in `src/cvm/enhanced_vm.cpp`, created via `EnhancedVMFactory::CreateProductionVM`. Classifies bytecode via BytecodeDetector and forwards to the native CVM or the EVM_Engine. Entry points: `Execute`, `DeployContract`, `CallContract`. Own limits: `MAX_BYTECODE_SIZE = 24576`, `MAX_CALL_DEPTH = 1024`.
- **BytecodeDetector / BytecodeFormat**: Component `src/cvm/bytecode_detector.cpp` that classifies a byte buffer into exactly one `BytecodeFormat`. The enum values are `UNKNOWN`, `CVM_NATIVE`, `EVM_BYTECODE`, and `HYBRID`.
- **Hybrid bytecode**: Bytecode classified by the BytecodeDetector as `BytecodeFormat::HYBRID` because it is executable on both virtual machines.
- **Transient Storage (TLOAD/TSTORE)**: Transaction-local storage of the EVM layer, addressed via the EVMC callbacks `get_transient_storage` (TLOAD) and `set_transient_storage` (TSTORE). Values persist within a single execution and are reset between transactions.
- **EVMExecutionResult**: Result structure of the EVM_Engine entry points with, among others, the fields `success`, `status_code`, `gas_used`, `gas_left`, `output_data`, `logs`, `error_message`.
- **EnhancedExecutionResult**: Result structure of the EnhancedVM entry points with, among others, the fields `success`, `gas_used`, `return_data`, `logs`, `error`, `executed_format`, `contract_address`.
- **EVMCHost_Mock**: In-memory implementation of the EVMC host binding (analogous to the ContractStorage_Mock) for deterministic, side-effect-free use by the Fuzz_Harness, including persistent storage, account/context bridge, and transient storage.

## Requirements

### Requirement 1: CVM-specific fuzz target with entry point selection

**User Story:** As a Cascoin developer, I want a fuzz harness that forwards arbitrary inputs to the public CVM entry points, so that the CVM core logic is tested and not just Bitcoin deserialization.

#### Acceptance Criteria

1. THE Fuzz_Harness SHALL use the first byte of the input byte buffer as the Entry_Point_Selector and map the remaining buffer to exactly one of the following five CVM entry points: `CVM::Execute`, `CVM::VerifyBytecode`, `CVM::DeployContract`, `CVM::CallContract`, and the free function `ExecuteContract`.
2. IF the input byte buffer contains fewer than 1 byte (is empty), THEN THE Fuzz_Harness SHALL not call any CVM entry point and SHALL return with status code 0.
3. IF the first byte of the input byte buffer cannot be mapped to any of the five defined CVM entry points, THEN THE Fuzz_Harness SHALL not call any CVM entry point and SHALL return with status code 0.
4. THE Fuzz_Harness SHALL use a new, empty ContractStorage_Mock instance as the storage argument for every call to a CVM entry point.
5. WHEN a CVM entry point requires a gas limit, THE Fuzz_Harness SHALL derive this gas limit from the input and clamp it to the range from 0 up to and including `MAX_GAS_PER_TX` (1,000,000).
6. WHEN a called CVM entry point returns, THE Fuzz_Harness SHALL return with status code 0.
7. IF a called CVM entry point raises an expected exception, THEN THE Fuzz_Harness SHALL catch this exception and return with status code 0, without aborting the fuzzing process.

### Requirement 2: In-memory ContractStorage mock

**User Story:** As a Cascoin developer, I want an in-memory implementation of ContractStorage, so that the fuzz harness runs deterministically and without LevelDB side effects.

#### Acceptance Criteria

1. THE ContractStorage_Mock SHALL fully implement the abstract interface `ContractStorage` with the methods `Load`, `Store`, and `Exists`.
2. WHEN `Store(contractAddr, key, value)` has been called and afterwards `Load(contractAddr, key, out)` is called with the same `contractAddr` and `key`, THE ContractStorage_Mock SHALL set `out` to the last stored `value` and return `true`.
3. IF `Load(contractAddr, key, out)` is called for a pair of `contractAddr` and `key` for which no value has been stored, THEN THE ContractStorage_Mock SHALL leave `out` unchanged and return `false`.
4. WHEN `Store(contractAddr, key, value)` is called multiple times with the same pair of `contractAddr` and `key`, THE ContractStorage_Mock SHALL retain exclusively the last stored `value` and discard all previously stored values for this pair.
5. WHEN `Exists(contractAddr, key)` is called for a pair of `contractAddr` and `key`, THE ContractStorage_Mock SHALL return `true` if and only if at least one `Store` call has previously occurred for this pair, and `false` otherwise.
6. WHEN `Store(contractAddr, key, value)` is called for a specific pair of `contractAddr` and `key`, THE ContractStorage_Mock SHALL leave the stored values of all other pairs of `contractAddr` and `key` unchanged.
7. WHILE `Load`, `Store`, and `Exists` are executing, THE ContractStorage_Mock SHALL keep its data exclusively in memory and SHALL not perform any read, write, or create accesses to a persistent storage device.

### Requirement 3: Crash freedom with arbitrary bytecode

**User Story:** As a Cascoin developer, I want the CVM to never crash on arbitrary bytecode, so that malicious or faulty contracts cannot bring down nodes.

#### Acceptance Criteria

1. WHEN an input byte buffer with a length of 0 to 24,576 bytes is forwarded to any CVM entry point, THE CVM SHALL complete execution without memory access errors, without triggered assertions, and without undefined behavior, and SHALL return a defined result or error status.
2. IF a CVM entry point raises a C++ exception, THEN THE Fuzz_Harness SHALL catch this exception, store the triggering input byte buffer as a reproducible test case, and report the exception as an invariant violation.
3. WHILE the Fuzz_Harness runs instrumented under AddressSanitizer or UndefinedBehaviorSanitizer, THE Fuzz_Harness SHALL treat every error reported by the sanitizer as a reproducible crash and store the triggering input byte buffer as a reproduction case.
4. WHEN an input byte buffer is processed by a CVM entry point, THE CVM SHALL terminate execution deterministically within the gas limit of 1,000,000 gas per transaction, so that no infinite loop blocks the node.
5. IF an input byte buffer exceeds the maximum contract size of 24,576 bytes, THEN THE CVM SHALL reject the input and return an error status indicating the size overflow, without crashing the node.

### Requirement 4: Termination through gas metering

**User Story:** As a Cascoin developer, I want every CVM execution to terminate, so that infinite loops cannot enable denial-of-service attacks.

#### Acceptance Criteria

1. WHEN an arbitrary input byte buffer is executed with a finite gas limit in the range from 0 to 1,000,000 gas, THE CVM SHALL terminate execution after at most as many executed opcodes as the gas limit divided by the minimum gas cost per opcode allows, and SHALL yield a `VMState::Status` other than `RUNNING`.
2. THE CVM SHALL deduct at least 1 gas unit from the available gas for every executed opcode.
3. IF the available gas before the next opcode is less than its gas cost, THEN THE CVM SHALL halt execution immediately, set the status `OUT_OF_GAS`, and discard all state changes made during this execution.
4. IF the gas limit at the start of execution is 0, THEN THE CVM SHALL halt execution without executing any opcode and set the status `OUT_OF_GAS`.

### Requirement 5: Gas invariants

**User Story:** As a Cascoin developer, I want gas consumption to be correctly bounded and monotonic, so that the CVM's resource accounting remains reliable.

#### Acceptance Criteria

1. WHEN a CVM execution with the gas limit `gasLimit` has completed in any termination mode (regular completion via STOP/RETURN, abort, or gas exhaustion), THE CVM SHALL ensure that `GetGasUsed()` is greater than or equal to 0 and less than or equal to `gasLimit`.
2. WHILE a CVM execution progresses, THE CVM SHALL keep `GetGasUsed()` monotonically non-decreasing.
3. WHEN `ExecuteContract` returns an `ExecutionResult`, THE CVM SHALL set the field `gasUsed` exactly equal to the value of `GetGasUsed()`.
4. IF the provided `gasLimit` lies outside the range from 0 up to and including 1,000,000 gas, THEN THE CVM SHALL reject execution, make no state changes, and return a response indicating an invalid `gasLimit`.
5. IF the gas is exhausted during a CVM execution (the gas consumption would exceed `gasLimit`), THEN THE CVM SHALL abort execution immediately, clamp `GetGasUsed()` to at most `gasLimit`, roll back all state changes, and return a response indicating an out-of-gas condition.

### Requirement 6: Stack limit

**User Story:** As a Cascoin developer, I want the execution stack to never grow beyond its limit, so that memory overflows are ruled out.

#### Acceptance Criteria

1. WHILE a CVM execution is running, THE CVM SHALL keep the stack size (`VMState::StackSize()`) at every point in time at most `MAX_STACK_SIZE` (1024 elements).
2. IF an instruction would grow the stack beyond `MAX_STACK_SIZE` (1024 elements), THEN THE CVM SHALL not execute the instruction, leave the stack contents unchanged, halt execution, and return the status `STACK_OVERFLOW` as an error indication to the caller.
3. IF an instruction would remove more values from the stack than are currently present, THEN THE CVM SHALL not execute the instruction, leave the stack contents unchanged, halt execution, and return the status `STACK_UNDERFLOW` as an error indication to the caller.
4. WHEN execution has been halted with status `STACK_OVERFLOW` or `STACK_UNDERFLOW`, THE CVM SHALL not execute any further instructions of the contract.

### Requirement 7: Handling of invalid bytecode size, opcodes, and jumps

**User Story:** As a Cascoin developer, I want invalid bytecode to be rejected via defined status codes instead of crashes, so that faulty inputs are handled predictably.

#### Acceptance Criteria

1. WHEN `CVM::VerifyBytecode(code)` is called with a `code` whose length is greater than `MAX_CODE_SIZE` (24576 bytes), THE CVM SHALL return `false` without executing or storing the `code`.
2. WHEN `CVM::VerifyBytecode(code)` is called with a `code` whose length lies in the range from 1 up to and including `MAX_CODE_SIZE` (24576 bytes) and all of whose bytes correspond to defined opcodes, THE CVM SHALL return `true`.
3. IF `CVM::VerifyBytecode(code)` is called with an empty `code` (length 0 bytes), THEN THE CVM SHALL return `false` without storing the `code`.
4. WHEN bytecode whose length exceeds `MAX_CODE_SIZE` (24576 bytes) is executed, THE CVM SHALL reject execution, set the status `VM_ERROR`, and not execute any instruction of this bytecode.
5. IF during execution a byte is reached that does not correspond to any defined opcode, THEN THE CVM SHALL halt execution immediately, set the status `INVALID_OPCODE`, and not commit any state changes (persistent storage writes) of the aborted call.
6. IF a jump instruction (JUMP or JUMPI) references a target whose index is greater than or equal to the bytecode length or that does not point to a valid jump destination, THEN THE CVM SHALL halt execution immediately, set the status `INVALID_JUMP`, and not commit any state changes (persistent storage writes) of the aborted call.

### Requirement 8: Deterministic execution

**User Story:** As a Cascoin developer, I want identical inputs to produce identical results, so that consensus and reproducibility are guaranteed.

#### Acceptance Criteria

1. WHEN `ExecuteContract` is called twice with identical `code`, identical context, and a bit-identical initial state of the ContractStorage_Mock (the same set of key-value pairs with byte-identical values), THE CVM SHALL produce in both calls an `ExecutionResult` with identical `success`, with exactly equal numeric `gasUsed`, and with byte-for-byte identical `returnData`.
2. WHEN `ExecuteContract` is called twice with identical input and identical context, THE CVM SHALL produce the same `VMState::Status` in both calls.
3. WHEN `ExecuteContract` is called twice with identical `code`, identical context, and a bit-identical initial state of the ContractStorage_Mock, THE CVM SHALL leave a bit-identical final state of the ContractStorage_Mock (the same set of key-value pairs with byte-identical values) after both calls.
4. IF `ExecuteContract` with identical input and identical context returns an `ExecutionResult` with `success == false`, THEN THE CVM SHALL produce in both calls an identical `VMState::Status`, an exactly equal numeric `gasUsed`, and a bit-identical final state of the ContractStorage_Mock.
5. WHEN `ExecuteContract` is called at least 100 times in a row with identical `code`, identical context, and a bit-identical initial state of the ContractStorage_Mock, THE CVM SHALL produce in all calls an `ExecutionResult` with identical `success`, exactly equal numeric `gasUsed`, byte-for-byte identical `returnData`, and a bit-identical final state of the ContractStorage_Mock.

### Requirement 9: Integration into the build system

**User Story:** As a Cascoin developer, I want to be able to build the CVM fuzz harness through the existing build system, so that it is created reproducibly and in a CI-capable way.

#### Acceptance Criteria

1. THE Fuzz_Harness SHALL be defined as a GNU Autotools target in `src/Makefile.test.include` and follow the `test_cascoin` naming convention.
2. WHEN a developer builds the fuzz harness target with a single `make` invocation in the `src/` directory, THE Build_System SHALL produce an executable binary that is linked against the CVM library and SHALL terminate with exit code 0.
3. IF the build of the fuzz harness target fails, THEN THE Build_System SHALL terminate with a nonzero exit code, output an error message naming the cause, and neither produce a harness binary nor overwrite an existing one.
4. WHERE the toolchain is configured with AFL instrumentation, THE Fuzz_Harness SHALL be executable in AFL "persistent mode" and "deferred forkserver", process inputs from standard input (stdin), not crash on valid input, and process each individual input in at most 60 seconds.
5. WHERE the toolchain is configured with libFuzzer support, THE Fuzz_Harness SHALL provide an entry function `LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)` that accepts inputs with a length of 0 to at least 1 MiB (1,048,576 bytes) and does not crash on valid input.
6. WHEN the fuzz harness target is built twice with identical configuration and identical source state without manual intermediate steps, THE Build_System SHALL produce a functionally equivalent binary.

### Requirement 10: Initial seed corpus

**User Story:** As a Cascoin developer, I want an initial seed corpus of valid and partially valid CVM bytecode, so that the fuzz engine efficiently reaches deep execution paths.

#### Acceptance Criteria

1. THE Fuzz_Harness SHALL ship with a Seed_Corpus that contains at least one input whose size does not exceed the maximum supported CVM bytecode size (at most 24 KB) and that passes `CVM::VerifyBytecode` with a success status (returning "valid").
2. THE Seed_Corpus SHALL contain at least one input for each CVM entry point supported per Requirement 1 that deterministically selects that entry point on every execution.
3. THE Seed_Corpus SHALL contain at least one input that performs on the ContractStorage_Mock an SSTORE operation followed by an SLOAD operation on the same key and reads back the previously written value unchanged.
4. WHEN a Seed_Corpus input is passed to the Fuzz_Harness, THE Fuzz_Harness SHALL complete its processing without triggering an invariant violation (assertion abort, memory error, or sanitizer report) and within the applicable gas cap.
5. THE Seed_Corpus SHALL contain at least one input that passes `CVM::VerifyBytecode` with an error status (returning "invalid"), in order to cover partially valid execution paths.
6. IF a Seed_Corpus input does not pass `CVM::VerifyBytecode`, THEN THE Fuzz_Harness SHALL discard it without an invariant violation and continue processing the remaining inputs.

### Requirement 11: Repeatable execution workflow including regtest path

**User Story:** As a Cascoin developer, I want documented, repeatable commands to build and run a fuzzing session as well as a regtest validation path, so that I can reliably start fuzzing and confirm findings.

#### Acceptance Criteria

1. THE Documentation SHALL contain complete commands, executable without further adjustment, for building the CVM fuzz harness.
2. THE Documentation SHALL contain complete commands, executable without further adjustment, for starting a fuzzing session that explicitly specify both an input directory (Seed_Corpus) and an output directory as parameters.
3. THE Documentation SHALL describe step-by-step commands with which a Crash_Input found through fuzzing is reproduced in a local regtest network via the RPC commands `deploycontract`, `callcontract`, and `sendcvmcontract`, including the expected, observable result by which a successful reproduction of the Crash_Input is recognized.
4. THE Documentation SHALL state that the primary fuzzing occurs in-process and that regtest serves the reproduction and validation of findings.
5. THE Documentation SHALL specify at least one bounding parameter with a concrete value for the fuzzing session, either a maximum runtime in seconds or a maximum number of executions.
6. THE Documentation SHALL specify for each documented build and execution step the expected, observable success signal by which the successful completion of the step is recognized.

### Requirement 12: Reproducibility of crash inputs

**User Story:** As a Cascoin developer, I want discovered crash inputs to be stored and deterministically replayed, so that I can investigate and fix reported errors.

#### Acceptance Criteria

1. WHEN the Fuzz_Engine discovers a Crash_Input, THE Fuzz_Engine SHALL store the Crash_Input under a unique, collision-free filename in the output directory, without overwriting a previously stored Crash_Input.
2. IF storing a Crash_Input in the output directory fails (e.g. missing write permissions or no available storage space), THEN THE Fuzz_Engine SHALL display an error message naming the cause and keep all previously stored Crash_Inputs unchanged.
3. WHEN the Fuzz_Harness is executed again with a stored Crash_Input as input, THE Fuzz_Harness SHALL trigger on every repetition (100% of executions) the same invariant violation or the same crash with an identical observable result.
4. THE Fuzz_Harness SHALL support reading exactly one input file specified via the command line, so that a stored Crash_Input can be replayed without a running Fuzz_Engine.
5. IF the input file specified via the command line does not exist or cannot be read, THEN THE Fuzz_Harness SHALL abort execution without crashing itself and display an error message naming the cause.

### Requirement 13: Update of the fuzzing documentation

**User Story:** As a Cascoin developer, I want the fuzzing documentation to contain the CVM-specific instructions, so that other developers can use the CVM fuzz harness without further questions.

#### Acceptance Criteria

1. THE Documentation SHALL contain, in the file `doc/developer/fuzzing.md`, a standalone, headed section describing the CVM fuzz harness and specifying at least the name of the harness binary as well as the build command to create the harness.
2. THE Documentation SHALL, in the CVM section, list every CVM entry point selectable through the Entry_Point_Selector in a complete list, specifying per entry point the identifier and a one-sentence description.
3. THE Documentation SHALL, in the CVM section, specify the storage location of the Seed_Corpus for the CVM fuzz harness as a path relative to the repository root directory.
4. THE Documentation SHALL, in the CVM section, specify the command with which the Entry_Point_Selector is set to a particular CVM entry point.
5. THE Documentation SHALL, in the CVM section, list the additional EVM_Engine entry points (`EVMEngine::Execute`, `EVMEngine::DeployContract`, `EVMEngine::StaticCall`, `EVMEngine::DelegateCall`) and EnhancedVM entry points (`EnhancedVM::Execute`, `EnhancedVM::DeployContract`, `EnhancedVM::CallContract`) together with a one-sentence description each.
6. THE Documentation SHALL state that the EVM-specific entry points are only active when `ENABLE_EVMC` is defined and that they return as a no-op with status code 0 when `ENABLE_EVMC` is not defined.
7. THE Documentation SHALL state that the external evmone/evmc interpreter is treated as a trusted third-party library and is not itself a fuzzing target.

### Requirement 14: Extension of the Entry_Point_Selector with EVM and router entry points

**User Story:** As a Cascoin developer, I want the fuzz harness to offer, in addition to the native CVM, also the EVM engine and the EnhancedVM router as selectable targets, so that the entire Cascoin integration code of all three layers is fuzzed.

#### Acceptance Criteria

1. WHERE `ENABLE_EVMC` is defined, THE Fuzz_Harness SHALL be able to map the Entry_Point_Selector, in addition to the entry points from Requirement 1, to exactly one of the following EVM_Engine entry points: `EVMEngine::Execute`, `EVMEngine::DeployContract`, `EVMEngine::StaticCall`, and `EVMEngine::DelegateCall`.
2. WHERE `ENABLE_EVMC` is defined, THE Fuzz_Harness SHALL be able to map the Entry_Point_Selector additionally to exactly one of the following EnhancedVM entry points: `EnhancedVM::Execute`, `EnhancedVM::DeployContract`, and `EnhancedVM::CallContract`.
3. WHERE `ENABLE_EVMC` is NOT defined, IF the Entry_Point_Selector selects an EVM_Engine or EnhancedVM entry point, THEN THE Fuzz_Harness SHALL not call any of these entry points and SHALL return deterministically with status code 0.
4. THE Fuzz_Harness SHALL build successfully and be executable both with and without `ENABLE_EVMC` defined.
5. WHEN the Entry_Point_Selector selects an EVM_Engine or EnhancedVM entry point, THE Fuzz_Harness SHALL pass the remaining input byte buffer as bytecode or input data and the derived context (gas limit, contract address, caller address, call value, input data, block height, block hash, timestamp) to the selected entry point.
6. WHEN the Entry_Point_Selector selects an EVM_Engine or EnhancedVM entry point that requires a gas limit, THE Fuzz_Harness SHALL derive this gas limit from the input and clamp it to the range from 0 up to and including `MAX_GAS_PER_TX` (1,000,000).
7. WHEN a selected EVM_Engine or EnhancedVM entry point returns, THE Fuzz_Harness SHALL return with status code 0.

### Requirement 15: Treatment of evmone as a trusted third-party library

**User Story:** As a Cascoin developer, I want the fuzz harness to treat the external evmone interpreter as trusted and to check only the Cascoin integration code as well as engine-independent invariants, so that errors of our integration and not peculiarities of evmone are reported.

#### Acceptance Criteria

1. WHEN an EVM_Engine or EnhancedVM entry point returns a result with an evmone error status code (`EVMC_FAILURE`, `EVMC_REVERT`, out-of-gas, or another defined EVMC status code), THE Fuzz_Harness SHALL treat this result as a defined execution result and SHALL NOT report it as an invariant violation.
2. THE Fuzz_Harness SHALL report invariant violations exclusively for crashes, undefined behavior, triggered assertions, sanitizer reports, or violated engine-independent invariants (Requirements 16 through 19), not for error status codes reported within the `EVMExecutionResult` or `EnhancedExecutionResult`.
3. WHEN the Fuzz_Harness executes the EVM layer, THE Fuzz_Harness SHALL call exclusively the public entry points of the EVM_Engine, the EVMCHost callbacks, and the EnhancedVM, and SHALL not call any internal symbols of the evmone interpreter directly.
4. IF the external evmone interpreter cannot be loaded at runtime, THEN THE Fuzz_Harness SHALL treat this as a defined error condition, return with status code 0, and not report an invariant violation.

### Requirement 16: Crash and UB freedom as well as termination of the EVM and hybrid path

**User Story:** As a Cascoin developer, I want arbitrary bytecode via the EVM engine and the EnhancedVM router to never crash and to always terminate, so that malicious or faulty contracts endanger a node on no layer.

#### Acceptance Criteria

1. WHEN an input byte buffer with a length of 0 to 24,576 bytes is forwarded to an EVM_Engine or EnhancedVM entry point, THE Fuzz_Harness SHALL complete execution without memory access errors, without triggered assertions, and without undefined behavior, and SHALL return an `EVMExecutionResult` or `EnhancedExecutionResult` with a defined status.
2. WHEN an EVM_Engine or EnhancedVM entry point is executed with a gas limit in the range from 0 up to and including 1,000,000 gas, THE Fuzz_Harness SHALL terminate execution deterministically within this gas limit, so that no infinite loop blocks the node.
3. WHEN an EVM_Engine or EnhancedVM entry point returns a result, THE Fuzz_Harness SHALL ensure that the field `gas_used` is greater than or equal to 0 and less than or equal to the provided gas limit.
4. IF an input byte buffer exceeds the limit `EnhancedVM::MAX_BYTECODE_SIZE` (24,576 bytes), THEN THE EnhancedVM SHALL reject the input and return an `EnhancedExecutionResult` with `success == false` and a set `error` field, without crashing the node.
5. IF an EVM_Engine or EnhancedVM entry point raises a C++ exception, THEN THE Fuzz_Harness SHALL catch this exception, store the triggering input byte buffer as a reproducible test case, and report the exception as an invariant violation.
6. WHILE the Fuzz_Harness executes the EVM/hybrid path instrumented under AddressSanitizer or UndefinedBehaviorSanitizer, THE Fuzz_Harness SHALL treat every error reported by the sanitizer as a reproducible crash and store the triggering input byte buffer as a reproduction case.

### Requirement 17: Fuzzing of the BytecodeDetector and the format routing

**User Story:** As a Cascoin developer, I want the BytecodeDetector to classify every input buffer unambiguously and deterministically and the EnhancedVM to route there consistently, so that the format detection and the routing are robust against arbitrary inputs.

#### Acceptance Criteria

1. WHEN an arbitrary input byte buffer with a length of 0 to 24,576 bytes is passed to the BytecodeDetector, THE BytecodeDetector SHALL return, without crashing, exactly one of the `BytecodeFormat` values `CVM_NATIVE`, `EVM_BYTECODE`, `HYBRID`, or `UNKNOWN`.
2. WHEN the BytecodeDetector is called twice with a bit-identical input byte buffer, THE BytecodeDetector SHALL return the same `BytecodeFormat` value in both calls.
3. WHEN the EnhancedVM executes an input byte buffer that the BytecodeDetector classifies as `CVM_NATIVE`, THE EnhancedVM SHALL forward execution to the native CVM and set the field `executed_format` in the `EnhancedExecutionResult` to `CVM_NATIVE`.
4. WHEN the EnhancedVM executes an input byte buffer that the BytecodeDetector classifies as `EVM_BYTECODE`, THE EnhancedVM SHALL forward execution to the EVM_Engine and set the field `executed_format` in the `EnhancedExecutionResult` to `EVM_BYTECODE`.
5. IF the BytecodeDetector classifies an input byte buffer as `UNKNOWN`, THEN THE EnhancedVM SHALL reject execution and return an `EnhancedExecutionResult` with `success == false` and a set `error` field, without crashing the node.
6. WHEN the EnhancedVM executes an input byte buffer, THE EnhancedVM SHALL report in the `EnhancedExecutionResult` exactly the `BytecodeFormat` value in the field `executed_format` to which execution was actually forwarded.

### Requirement 18: Fuzzing of the EVMCHost storage, account, and context bridge

**User Story:** As a Cascoin developer, I want the EVMCHost callbacks to be fuzzed via an in-memory mock, so that the storage, account, and context bridge between Cascoin and evmone works correctly and without side effects.

#### Acceptance Criteria

1. THE EVMCHost_Mock SHALL fully implement the EVMC host callbacks and keep its data exclusively in memory, without any read, write, or create accesses to a persistent storage device.
2. WHEN a value has been set via `set_storage` for a pair of contract address and storage key and afterwards `get_storage` is called with the same values, THE EVMCHost_Mock SHALL return the last set value byte-identically.
3. IF `get_storage` is called for a pair of contract address and storage key for which no value has been set, THEN THE EVMCHost_Mock SHALL return a zero value (all bytes equal to 0).
4. IF `account_exists`, `get_balance`, `get_code_size`, `get_code_hash`, or `copy_code` is called for a non-existent account, THEN THE EVMCHost_Mock SHALL return a defined default value (non-existent or zero value) without crashing.
5. WHERE `ENABLE_EVMC` is defined, THE EVMCHost SHALL register the transient storage callbacks `get_transient_storage` and `set_transient_storage` with nonzero (non-null) function pointers in the EVMC host interface table.
6. WHEN a value has been set via `set_transient_storage` for a pair of contract address and key within a single execution and afterwards `get_transient_storage` is called with the same values, THE EVMCHost_Mock SHALL return the last set value byte-identically.
7. WHEN a new execution (transaction) begins, THE EVMCHost_Mock SHALL reset the entire transient storage to zero, so that `get_transient_storage` returns a zero value for every pair of contract address and key.

### Requirement 19: Determinism of the router and EVM path

**User Story:** As a Cascoin developer, I want the EVM and router path to deterministically produce the same result for identical input, so that consensus and reproducibility are guaranteed across all three layers.

#### Acceptance Criteria

1. WHEN an EnhancedVM entry point is called twice with identical input, identical context, and a bit-identical initial state of the EVMCHost_Mock, THE EnhancedVM SHALL produce in both calls an `EnhancedExecutionResult` with identical `success`, exactly equal numeric `gas_used`, byte-for-byte identical `return_data`, and identical `executed_format`.
2. WHEN an EnhancedVM entry point is called twice with identical input, identical context, and a bit-identical initial state of the EVMCHost_Mock, THE EnhancedVM SHALL leave a bit-identical final state of the EVMCHost_Mock (the same set of key-value pairs with byte-identical values) after both calls.
3. WHEN an EVM_Engine entry point is called twice with identical input, identical context, and a bit-identical initial state of the EVMCHost_Mock, THE EVM_Engine SHALL produce in both calls an `EVMExecutionResult` with identical `success`, identical `status_code`, exactly equal numeric `gas_used`, and byte-for-byte identical `output_data`.
4. WHEN an EnhancedVM entry point is called at least 100 times in a row with identical input, identical context, and a bit-identical initial state of the EVMCHost_Mock, THE EnhancedVM SHALL produce in all calls an `EnhancedExecutionResult` with identical `success`, exactly equal numeric `gas_used`, byte-for-byte identical `return_data`, identical `executed_format`, and a bit-identical final state of the EVMCHost_Mock.
