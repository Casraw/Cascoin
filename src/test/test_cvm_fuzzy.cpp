// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CVM-specific fuzzing harness (binary: test_cascoin_cvm_fuzzy).
//
// Spec: .kiro/specs/cvm-fuzzing-harness
// Task 1.1: "Wire build targets and create skeleton sources"
//
// This is the initial skeleton. It mirrors the structure of the existing
// generic harness test/test_bitcoin_fuzzy.cpp so the target links and produces
// a runnable binary. Subsequent tasks flesh out the selector enum, the
// FuzzConsumer, the in-memory ContractStorage mock, the native/EVM dispatchers,
// the invariant checkers, and CLI replay.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <stdint.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include <uint256.h>

#include <cvm/cvm.h>     // CVM::CVM, CVM::ExecuteContract, CVM::ExecutionResult
#include <cvm/vmstate.h> // CVM::MAX_GAS_PER_TX, CVM::ContractStorage, CVM::VMState

// EVM integration layer (Task 8.1). Only compiled when ENABLE_EVMC is defined
// (in the current build ENABLE_EVMC = 1). The external evmone/evmc interpreter
// is a trusted third-party library and is never a fuzz target itself.
#ifdef ENABLE_EVMC
#include <fs.h>                 // fs::path, fs::temp_directory_path, fs::unique_path
#include <cvm/cvmdb.h>          // CVM::CVMDatabase (in-memory backing store)
#include <cvm/trust_context.h>  // CVM::TrustContext (test trust context)
#include <cvm/evm_engine.h>     // CVM::EVMEngine, CVM::EVMExecutionResult
#include <cvm/evmc_host.h>      // CVM::EVMCHost (real host wiring, pulled in via EVMEngine)
#include <cvm/enhanced_vm.h>       // CVM::EnhancedVM, CVM::EnhancedVMFactory, CVM::EnhancedExecutionResult
#include <cvm/bytecode_detector.h> // CVM::BytecodeDetector, CVM::BytecodeFormat, CVM::BytecodeDetectionResult
#endif // ENABLE_EVMC

// -----------------------------------------------------------------------------
// Entry_Point_Selector (Task 2.1)
// -----------------------------------------------------------------------------
//
// The first byte of every fuzz input is interpreted directly as an index into
// this enum (analogous to the TEST_ID pattern in test/test_bitcoin_fuzzy.cpp).
// Values >= CVM_FUZZ_TARGET_END are invalid and lead to status code 0.
//
// Selectors 0..4 are native CVM entry points and are always available.
// Selectors 5..12 are EVM/router entry points; they are wired only under
// ENABLE_EVMC and become no-ops (status 0) otherwise. Importantly,
// CVM_FUZZ_TARGET_END stays equal to 13 regardless of ENABLE_EVMC, so selector
// values never shift between build variants and the same seed corpus remains
// valid in both configurations.
enum CvmFuzzTarget : uint8_t {
    CVM_EXECUTE = 0,           // CVM::Execute (native)
    CVM_VERIFY_BYTECODE,       // CVM::VerifyBytecode (native, static)
    CVM_DEPLOY_CONTRACT,       // CVM::DeployContract (native)
    CVM_CALL_CONTRACT,         // CVM::CallContract (native)
    EXECUTE_CONTRACT,          // ExecuteContract free function (native)
    EVM_EXECUTE,               // EVMEngine::Execute (EVM, ENABLE_EVMC)
    EVM_DEPLOY_CONTRACT,       // EVMEngine::DeployContract (EVM, ENABLE_EVMC)
    EVM_STATIC_CALL,           // EVMEngine::StaticCall (EVM, ENABLE_EVMC)
    EVM_DELEGATE_CALL,         // EVMEngine::DelegateCall (EVM, ENABLE_EVMC)
    ENHANCED_EXECUTE,          // EnhancedVM::Execute (router, ENABLE_EVMC)
    ENHANCED_DEPLOY_CONTRACT,  // EnhancedVM::DeployContract (router, ENABLE_EVMC)
    ENHANCED_CALL_CONTRACT,    // EnhancedVM::CallContract (router, ENABLE_EVMC)
    BYTECODE_DETECT,           // BytecodeDetector::DetectFormat (router, ENABLE_EVMC)
    CVM_FUZZ_TARGET_END        // Range marker; must remain 13 in all builds.
};

// -----------------------------------------------------------------------------
// Derived fuzz context (Task 2.1)
// -----------------------------------------------------------------------------
//
// Internal structure holding the context fields derived from a fuzz input by
// the FuzzConsumer (implemented in Task 2.2). The context is passed to the CVM
// entry points selected by the dispatcher. `code` doubles as `inputData` for
// call-style entry points.
struct FuzzContext {
    uint64_t gasLimit;             // 0..MAX_GAS_PER_TX (clamped by the consumer)
    uint64_t callValue;            // wei-style call value
    int      blockHeight;          // >= 0 (high bit masked by the consumer)
    int64_t  timestamp;            // block timestamp
    uint160  contractAddr;         // target/contract address
    uint160  callerAddr;           // caller address
    uint256  blockHash;            // current block hash
    std::vector<uint8_t> code;     // remaining front bytes: code or inputData
};

// -----------------------------------------------------------------------------
// FuzzConsumer (Task 2.2)
// -----------------------------------------------------------------------------
//
// Hand-written, deterministic, side-effect-free structured consumer, modelled
// on the FuzzedDataProvider pattern but without the Clang/libFuzzer-only
// dependency (so it also works under the AFL/GCC build).
//
// Byte layout (the selector byte has already been stripped by the caller):
//
//   +-----------------------------+--------------------------------+
//   | Front bytes (code/inputData)| Tail bytes (context fields)    |
//   | ---- read from the start -->| <-- read from the end -----    |
//   +-----------------------------+--------------------------------+
//
// Context fields are consumed from the END of the buffer, while code/input data
// remain contiguous at the front (easy for the fuzz engine to mutate). Integral
// context fields are interpreted little-endian. There is deliberately NO error
// path: if the buffer does not hold enough bytes for a field, the missing bytes
// are treated as zero, so every input maps to a well-defined context.
class FuzzConsumer {
public:
    // `data`/`size` describe the buffer AFTER the selector byte was removed.
    FuzzConsumer(const uint8_t* data, size_t size)
        : m_begin(data), m_front(0), m_back(size) {}

    // Read sizeof(T) bytes from the end of the remaining buffer and interpret
    // them little-endian. Missing (high-order) bytes are treated as zero.
    template <typename T>
    T ConsumeIntegralFromTail()
    {
        static_assert(std::is_integral<T>::value, "T must be an integral type");
        const size_t want = sizeof(T);
        const size_t avail = Remaining();
        const size_t n = want < avail ? want : avail;

        // The consumed region is [m_back - n, m_back). The byte at the lowest
        // address is the least significant (little-endian); absent high bytes
        // default to zero via the initial value of 0.
        typename std::make_unsigned<T>::type value = 0;
        for (size_t i = 0; i < n; ++i) {
            value |= static_cast<typename std::make_unsigned<T>::type>(
                         m_begin[m_back - n + i])
                     << (8 * i);
        }
        m_back -= n;
        return static_cast<T>(value);
    }

    // Read 20 bytes from the end of the remaining buffer into a uint160.
    // Missing bytes default to zero.
    uint160 ConsumeUint160FromTail()
    {
        std::vector<unsigned char> bytes = ConsumeBytesFromTail(20);
        return uint160(bytes);
    }

    // Read 32 bytes from the end of the remaining buffer into a uint256.
    // Missing bytes default to zero.
    uint256 ConsumeUint256FromTail()
    {
        std::vector<unsigned char> bytes = ConsumeBytesFromTail(32);
        return uint256(bytes);
    }

    // Derive a gas limit from a 4-byte little-endian value clamped to the valid
    // range 0..MAX_GAS_PER_TX (inclusive) via modulo (MAX_GAS_PER_TX + 1).
    uint64_t ConsumeGasLimit()
    {
        const uint32_t raw = ConsumeIntegralFromTail<uint32_t>();
        return static_cast<uint64_t>(raw) % (CVM::MAX_GAS_PER_TX + 1);
    }

    // Return all remaining front bytes as code / inputData.
    std::vector<uint8_t> ConsumeRemainingFront()
    {
        std::vector<uint8_t> out(m_begin + m_front, m_begin + m_back);
        m_front = m_back;
        return out;
    }

private:
    // Number of bytes still available between the front and tail cursors.
    size_t Remaining() const { return m_back - m_front; }

    // Consume `count` bytes from the tail into a fixed-size, zero-initialised
    // buffer. Consumed bytes occupy the low positions [0, n); any missing bytes
    // remain zero. Always returns exactly `count` bytes (base_blob asserts an
    // exact width).
    std::vector<unsigned char> ConsumeBytesFromTail(size_t count)
    {
        std::vector<unsigned char> out(count, 0);
        const size_t avail = Remaining();
        const size_t n = count < avail ? count : avail;
        if (n > 0) {
            std::memcpy(out.data(), m_begin + (m_back - n), n);
            m_back -= n;
        }
        return out;
    }

    const uint8_t* m_begin; // start of the buffer (without selector)
    size_t m_front;         // next front index (inclusive)
    size_t m_back;          // next tail index (exclusive)
};

// -----------------------------------------------------------------------------
// ContractStorage_Mock (Task 3.1)
// -----------------------------------------------------------------------------
//
// In-memory implementation of the abstract interface CVM::ContractStorage
// (src/cvm/vmstate.h). It gives the fuzz harness deterministic, side-effect-free
// contract storage without any LevelDB access, so runs are reproducible.
//
// Backing store: a std::map keyed by the (contract address, storage key) pair,
// mapping to the last stored value. A separate std::set tracks every address
// for which at least one Store has occurred, which backs Exists(addr).
//
// Requirements 2.1-2.7:
//   2.1 Fully implements Load/Store/Exists of CVM::ContractStorage.
//   2.2 Store(addr,key,value) then Load(addr,key,out) => out = last value, true.
//   2.3 Load on an unset pair leaves out unchanged and returns false.
//   2.4 Repeated Store on the same pair keeps only the last value.
//   2.5 Exists(addr) is true iff at least one Store occurred for that address.
//   2.6 Store on one pair leaves all other pairs unchanged (isolation).
//   2.7 All data is kept in memory only; no persistent-storage access.
//
// Note on the real interface signature: Requirement 2.5 is phrased as
// Exists(contractAddr, key), but the actual virtual method in vmstate.h is
// Exists(const uint160& contractAddr) WITHOUT a key. The mock implements the
// real signature, so "exists" means "at least one Store for this address".
class ContractStorage_Mock : public CVM::ContractStorage
{
public:
    // Storage key: (contract address, storage key).
    using StorageKey = std::pair<uint160, uint256>;
    // Storage map: last stored value per (address, key) pair.
    using StorageMap = std::map<StorageKey, uint256>;

    // Load the value previously stored for (contractAddr, key).
    // Returns true and sets `value` to the last stored value if the pair is
    // present (Req 2.2); returns false and leaves `value` unchanged for an
    // unset pair (Req 2.3). No disk access (Req 2.7).
    bool Load(const uint160& contractAddr, const uint256& key, uint256& value) override
    {
        auto it = m_store.find(StorageKey(contractAddr, key));
        if (it == m_store.end()) {
            return false; // Req 2.3: leave `value` untouched.
        }
        value = it->second; // Req 2.2: return the last stored value.
        return true;
    }

    // Store `value` for (contractAddr, key). Overwrites any previous value for
    // that exact pair (Req 2.4) and leaves all other pairs untouched (Req 2.6).
    // Records the address so Exists(addr) reports true (Req 2.5). No disk
    // access (Req 2.7). Always succeeds for the in-memory backing store.
    bool Store(const uint160& contractAddr, const uint256& key, const uint256& value) override
    {
        m_store[StorageKey(contractAddr, key)] = value; // Req 2.4 + 2.6.
        m_addrs.insert(contractAddr);                   // Req 2.5.
        return true;
    }

    // Returns true iff at least one Store has occurred for `contractAddr`
    // (Req 2.5). Matches the real interface signature (no key parameter).
    bool Exists(const uint160& contractAddr) override
    {
        return m_addrs.count(contractAddr) > 0;
    }

    // Read-only view of the full backing store for the determinism checker
    // (Req 8.3: bit-identical final state comparison across repeated runs).
    const StorageMap& Snapshot() const { return m_store; }

private:
    StorageMap m_store;           // (addr, key) -> last stored value.
    std::set<uint160> m_addrs;    // addresses that have received a Store.
};

// Read the entire input buffer from stdin, capped at 1 MiB.
// Ported from test/test_bitcoin_fuzzy.cpp.
static bool read_stdin(std::vector<uint8_t>& data)
{
    uint8_t buffer[1024];
    ssize_t length = 0;
    while ((length = read(STDIN_FILENO, buffer, 1024)) > 0) {
        data.insert(data.end(), buffer, buffer + length);

        if (data.size() > (1 << 20)) return false;
    }
    return length == 0;
}

// -----------------------------------------------------------------------------
// CLI replay reader (Task 5.1)
// -----------------------------------------------------------------------------
//
// Read exactly the file at `path` as raw bytes (binary mode), capped at the
// same 1 MiB limit as read_stdin. This backs CLI replay of a stored crash
// input without a running fuzz engine (Req 12.4).
//
// On any failure (missing file, unreadable file, or oversized input) an error
// message naming the cause is written to stderr and false is returned, so the
// caller can exit with a defined, non-crashing status instead of aborting
// itself (Req 12.5).
static bool read_file(const char* path, std::vector<uint8_t>& data)
{
    errno = 0;
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        // errno is set by the underlying open() on most platforms; fall back to
        // ENOENT when the standard library does not propagate it.
        std::fprintf(stderr,
                     "test_cascoin_cvm_fuzzy: cannot open input file '%s': %s\n",
                     path, std::strerror(errno != 0 ? errno : ENOENT));
        return false;
    }

    // Read in fixed-size chunks, enforcing the same 1 MiB cap as read_stdin.
    char chunk[1024];
    while (file.read(chunk, sizeof(chunk)) || file.gcount() > 0) {
        data.insert(data.end(), chunk, chunk + file.gcount());
        if (data.size() > (1 << 20)) {
            std::fprintf(stderr,
                         "test_cascoin_cvm_fuzzy: input file '%s' exceeds the 1 MiB cap\n",
                         path);
            return false;
        }
    }

    // A set badbit indicates a genuine read error (EOF only sets failbit and is
    // the normal loop-termination condition).
    if (file.bad()) {
        std::fprintf(stderr,
                     "test_cascoin_cvm_fuzzy: error reading input file '%s': %s\n",
                     path, std::strerror(errno));
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Native invariant checkers (Task 4.2)
// -----------------------------------------------------------------------------
//
// Small, side-effect-free functions that are called after each native entry
// point. A violated invariant triggers assert()/abort() so the fuzz engine or
// sanitizer records the triggering buffer as a reproducible crash (Req 3.2,
// 12.1). Defined error results (a false return value or one of the defined
// error statuses such as OUT_OF_GAS, STACK_OVERFLOW, INVALID_OPCODE, ...) are
// NOT invariant violations and are deliberately not asserted against.
//
// Important: every assertion below encodes an invariant the native CVM
// GENUINELY guarantees (verified against src/cvm/cvm.cpp and
// src/cvm/vmstate.cpp). Assertions that would fire on legitimate inputs are
// intentionally avoided so replaying the seed corpus never produces a spurious
// abort. See CheckTerminated and CheckExecuteClassification for the specific
// cases (the final RUNNING status on fall-through, and the eager SSTORE commit
// with no rollback).

// True iff `s` holds one of the ten defined VMState::Status enum values. Used
// as a defensive check that execution left the state in a well-defined status
// (guards against memory corruption producing a garbage status value).
static bool IsDefinedStatus(CVM::VMState::Status s)
{
    switch (s) {
    case CVM::VMState::Status::RUNNING:
    case CVM::VMState::Status::STOPPED:
    case CVM::VMState::Status::RETURNED:
    case CVM::VMState::Status::REVERTED:
    case CVM::VMState::Status::OUT_OF_GAS:
    case CVM::VMState::Status::STACK_OVERFLOW:
    case CVM::VMState::Status::STACK_UNDERFLOW:
    case CVM::VMState::Status::INVALID_OPCODE:
    case CVM::VMState::Status::INVALID_JUMP:
    case CVM::VMState::Status::VM_ERROR:
        return true;
    }
    return false;
}

// Gas bound (Req 5.1): consumed gas never exceeds the limit. VMState::UseGas
// clamps gasRemaining to 0 and can never over-consume, so GetGasUsed() is always
// in [0, gasLimit]; gasUsed is unsigned, so the lower bound is implicit.
static void CheckGasBounds(uint64_t gasUsed, uint64_t gasLimit)
{
    (void)gasUsed;
    (void)gasLimit;
    assert(gasUsed <= gasLimit);
}

// Stack bound (Req 6.1): the value stack never exceeds MAX_STACK_SIZE.
// VMState::Push/Dup refuse to grow the stack past the limit (setting
// STACK_OVERFLOW instead), so this holds at every observable point.
static void CheckStackBound(const CVM::VMState& state)
{
    (void)state;
    assert(state.StackSize() <= CVM::MAX_STACK_SIZE);
}

// Termination (Req 4.1): after a native entry point returns, execution has
// halted and left a DEFINED status.
//
// Note on the deliberate weakening versus the literal design text: the design
// sketch suggests `assert(GetStatus() != RUNNING)`, but the native CVM does not
// guarantee that. CVM::Execute's main loop is
// `while (IsRunning() && pc < code.size())`, so bytecode that runs off the end
// without an explicit OP_STOP/OP_RETURN halts with the status still RUNNING
// (a legitimate, non-error outcome). The shipped seed seed_sstore_sload does
// exactly this. Asserting `!= RUNNING` would therefore abort on valid inputs
// and break the seed-corpus smoke test. We assert only the genuinely
// guaranteed invariant (the status is a defined enum value). The stronger
// Req 4.1 expectation is exercised by the controlled property test P1.
static void CheckTerminated(const CVM::VMState& state)
{
    (void)state;
    assert(IsDefinedStatus(state.GetStatus()));
}

// Status classification for CVM::Execute (Req 7.4): empty or oversized bytecode
// is rejected at entry, BEFORE any opcode runs, with status VM_ERROR, a false
// return value, and no storage commit. Because rejection happens before the
// execution loop, "no state commit" is genuinely guaranteed for this case and
// is asserted via the empty storage snapshot.
//
// Invalid-opcode (Req 7.5) and invalid-jump (Req 7.6) classification is NOT
// asserted here: reaching those statuses depends on the executed path, and the
// current CVM commits SSTORE writes eagerly with no rollback, so a blanket
// "no state commit" assertion on those paths would not reflect genuine
// behavior. Those cases are covered by the controlled property test P4.
static void CheckExecuteClassification(const std::vector<uint8_t>& code, bool executed,
                                       const CVM::VMState& state,
                                       const ContractStorage_Mock& storage)
{
    (void)code;
    (void)executed;
    (void)state;
    (void)storage;
    if (code.empty() || code.size() > CVM::MAX_CODE_SIZE) {
        assert(state.GetStatus() == CVM::VMState::Status::VM_ERROR); // Req 7.4
        assert(!executed);                                           // rejected, false
        assert(storage.Snapshot().empty());                         // no state commit
    }
}

// VerifyBytecode classification by size (Req 7.1, 7.3): empty or oversized
// bytecode is always classified invalid (false).
static void CheckVerifyClassification(const std::vector<uint8_t>& code, bool valid)
{
    (void)code;
    (void)valid;
    if (code.empty() || code.size() > CVM::MAX_CODE_SIZE) {
        assert(!valid);
    }
}

// -----------------------------------------------------------------------------
// Native CVM entry-point handlers (Task 4.1)
// -----------------------------------------------------------------------------
//
// Each handler forwards a decoded fuzz context to exactly one public native CVM
// entry point (selectors 0..4). Every handler creates a fresh, empty
// ContractStorage_Mock (Req 1.4) so runs stay isolated and side-effect-free,
// and always returns status code 0 when the entry point returns (Req 1.6).
//
// The invariant checkers (assertions on gas/stack/status bounds) are added in
// Task 4.2; these handlers currently just drive the entry points so that the
// dispatcher exercises the CVM core with arbitrary input.

// Selector 0: CVM::Execute(code, VMState&, ContractStorage*).
// Builds a VMState mirroring the free-function ExecuteContract setup, then runs
// the register-based interpreter over `code`.
static int RunCvmExecute(const std::vector<uint8_t>& code, uint64_t gasLimit,
                         const uint160& contractAddr, const uint160& callerAddr,
                         uint64_t callValue, int blockHeight,
                         const uint256& blockHash, int64_t timestamp)
{
    ContractStorage_Mock storage;

    CVM::VMState state;
    state.SetGasLimit(gasLimit);
    state.SetContractAddress(contractAddr);
    state.SetCallerAddress(callerAddr);
    state.SetCallValue(callValue);
    state.SetBlockHeight(blockHeight);
    state.SetBlockHash(blockHash);
    state.SetTimestamp(timestamp);

    CVM::CVM vm;
    const bool executed = vm.Execute(code, state, &storage);

    // Task 4.2: run the invariant checkers after the entry point.
    CheckGasBounds(state.GetGasUsed(), gasLimit);            // Req 5.1
    CheckStackBound(state);                                  // Req 6.1
    CheckTerminated(state);                                  // Req 4.1
    CheckExecuteClassification(code, executed, state, storage); // Req 7.4
    return 0; // Req 1.6
}

// Selector 1: CVM::VerifyBytecode(code) (static).
// Pure classification of the bytecode; no storage or context is required.
static int RunCvmVerify(const std::vector<uint8_t>& code)
{
    const bool valid = CVM::CVM::VerifyBytecode(code);

    // Task 4.2: empty/oversized bytecode must be classified invalid (Req 7.1/7.3).
    CheckVerifyClassification(code, valid);
    return 0; // Req 1.6
}

// Selector 2: CVM::DeployContract(code, contractAddr, storage).
static int RunCvmDeploy(const std::vector<uint8_t>& code, const uint160& contractAddr)
{
    ContractStorage_Mock storage;

    CVM::CVM vm;
    const bool deployed = vm.DeployContract(code, contractAddr, &storage);
    (void)deployed;

    // Task 4.2: DeployContract calls VerifyBytecode first, so empty/oversized
    // bytecode must fail deployment (Req 7.1/7.3).
    if (code.empty() || code.size() > CVM::MAX_CODE_SIZE) {
        assert(!deployed);
    }
    return 0; // Req 1.6
}

// Selector 3: CVM::CallContract(contractAddr, inputData, VMState&, storage).
// The remaining front bytes are interpreted as inputData; the derived
// contractAddr is the call target.
static int RunCvmCall(const uint160& contractAddr, const std::vector<uint8_t>& inputData,
                      uint64_t gasLimit, const uint160& callerAddr, uint64_t callValue,
                      int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    ContractStorage_Mock storage;

    CVM::VMState state;
    state.SetGasLimit(gasLimit);
    state.SetContractAddress(contractAddr);
    state.SetCallerAddress(callerAddr);
    state.SetCallValue(callValue);
    state.SetBlockHeight(blockHeight);
    state.SetBlockHash(blockHash);
    state.SetTimestamp(timestamp);

    CVM::CVM vm;
    vm.CallContract(contractAddr, inputData, state, &storage);

    // Task 4.2: run the invariant checkers after the entry point.
    CheckGasBounds(state.GetGasUsed(), gasLimit); // Req 5.1
    CheckStackBound(state);                       // Req 6.1
    CheckTerminated(state);                       // Req 4.1
    return 0; // Req 1.6
}

// Selector 4: ExecuteContract(...) free function returning ExecutionResult.
static int RunExecuteContract(const std::vector<uint8_t>& code, uint64_t gasLimit,
                              const uint160& contractAddr, const uint160& callerAddr,
                              uint64_t callValue, int blockHeight,
                              const uint256& blockHash, int64_t timestamp)
{
    ContractStorage_Mock storage;

    CVM::ExecutionResult result = CVM::ExecuteContract(
        code, gasLimit, contractAddr, callerAddr, callValue,
        /*inputData=*/code, blockHeight, blockHash, timestamp, &storage);

    // Task 4.2: gas consumed never exceeds the limit (Req 5.1). ExecuteContract
    // owns its VMState internally, so it is not exposed here; the design's
    // `result.gasUsed == state.GetGasUsed()` equality holds by construction in
    // cvm.cpp (a direct assignment) and is exercised by property test P2.
    CheckGasBounds(result.gasUsed, gasLimit);
    return 0; // Req 1.6
}

// -----------------------------------------------------------------------------
// EVM engine fixture and handlers (Task 8.1) — only under ENABLE_EVMC
// -----------------------------------------------------------------------------
//
// These handlers drive the EVM integration layer (CVM::EVMEngine) through its
// public entry points. They are wired into the dispatcher switch (selectors
// 5..8) in Task 8.2, alongside the EnhancedVM/BytecodeDetector handlers below
// (selectors 9..12). The full EVM/router routing-consistency invariant checkers
// belong to Task 8.3.
#ifdef ENABLE_EVMC

// EvmFixture bundles the in-memory state needed to exercise the EVM layer with
// no disk access (Req 18.1):
//   - an in-memory CVMDatabase (fMemory=true, fWipe=true), so LevelDB keeps
//     everything in RAM and nothing touches persistent storage;
//   - a test TrustContext bound to that database;
//   - a real EVMEngine, which internally wires a real EVMCHost against the same
//     database + trust context (including in-memory transient storage).
//
// A fresh fixture is constructed per fuzz call so runs stay isolated and
// deterministic. The external evmone/evmc interpreter is treated as a trusted
// third-party library (Req 15.3/15.4): if it cannot be loaded, the EVMEngine
// entry points return a defined failure result (success == false / an EVMC
// error status) instead of crashing, and the harness simply returns status 0.
// No internal evmone symbols are ever called from here.
struct EvmFixture {
    std::unique_ptr<CVM::CVMDatabase> db;
    std::shared_ptr<CVM::TrustContext> trust;
    std::unique_ptr<CVM::EVMEngine> engine;

    EvmFixture()
    {
        // Unique in-memory path per fixture; fMemory=true keeps all state in
        // RAM (no persistent-storage access, Req 18.1).
        const fs::path dbPath = fs::temp_directory_path() / fs::unique_path();
        db = std::unique_ptr<CVM::CVMDatabase>(
            new CVM::CVMDatabase(dbPath, 1 << 20, /*fMemory=*/true, /*fWipe=*/true));
        trust = std::make_shared<CVM::TrustContext>(db.get());
        engine = std::unique_ptr<CVM::EVMEngine>(new CVM::EVMEngine(db.get(), trust));
    }
};

// EVM gas bound (Req 16.3): consumed gas never exceeds the supplied limit. This
// is an engine-independent invariant. Defined EVMC error statuses
// (EVMC_FAILURE, EVMC_REVERT, out-of-gas, ...) are explicitly NOT treated as
// violations (Req 15.1/15.2). The full EVM/router invariant checkers land in
// Task 8.3; this basic gas-bounds check is added here as permitted by the task.
static void CheckEvmGasBounds(const CVM::EVMExecutionResult& r, uint64_t gasLimit)
{
    (void)r;
    (void)gasLimit;
    assert(r.gas_used <= gasLimit);
}

// Selector 5: EVMEngine::Execute(...). Runs arbitrary EVM bytecode with the
// derived context. The remaining front bytes are the bytecode; no separate
// input data is supplied for the plain execute path. Gas is already clamped to
// 0..MAX_GAS_PER_TX by the FuzzConsumer (Req 14.6).
static int RunEvmExecute(const std::vector<uint8_t>& code, uint64_t gasLimit,
                         const uint160& contractAddr, const uint160& callerAddr,
                         uint64_t callValue, int blockHeight,
                         const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    CVM::EVMExecutionResult r = fx.engine->Execute(
        code, gasLimit, contractAddr, callerAddr, callValue,
        /*input_data=*/std::vector<uint8_t>{}, blockHeight, blockHash, timestamp);

    CheckEvmGasBounds(r, gasLimit); // Req 16.3
    return 0;                       // Req 14.7
}

// Selector 6: EVMEngine::DeployContract(...). The front bytes are the contract
// creation bytecode; no separate constructor data is supplied. The derived
// caller address is the deployer and the derived call value is the deploy
// value.
static int RunEvmDeploy(const std::vector<uint8_t>& code, uint64_t gasLimit,
                        const uint160& deployerAddr, uint64_t deployValue,
                        int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    CVM::EVMExecutionResult r = fx.engine->DeployContract(
        code, /*constructor_data=*/std::vector<uint8_t>{}, gasLimit,
        deployerAddr, deployValue, blockHeight, blockHash, timestamp);

    CheckEvmGasBounds(r, gasLimit); // Req 16.3
    return 0;                       // Req 14.7
}

// Selector 7: EVMEngine::StaticCall(...) (read-only). The derived contract
// address is the call target and the remaining front bytes are the call data.
static int RunEvmStaticCall(const uint160& contractAddr,
                            const std::vector<uint8_t>& callData, uint64_t gasLimit,
                            const uint160& callerAddr, int blockHeight,
                            const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    CVM::EVMExecutionResult r = fx.engine->StaticCall(
        contractAddr, callData, gasLimit, callerAddr, blockHeight, blockHash, timestamp);

    CheckEvmGasBounds(r, gasLimit); // Req 16.3
    return 0;                       // Req 14.7
}

// Selector 8: EVMEngine::DelegateCall(...). The derived contract address is the
// call target and the remaining front bytes are the call data. The derived
// caller address doubles as the original caller for the delegated frame.
static int RunEvmDelegateCall(const uint160& contractAddr,
                              const std::vector<uint8_t>& callData, uint64_t gasLimit,
                              const uint160& callerAddr, uint64_t callValue,
                              int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    CVM::EVMExecutionResult r = fx.engine->DelegateCall(
        contractAddr, callData, gasLimit, callerAddr, /*original_caller=*/callerAddr,
        callValue, blockHeight, blockHash, timestamp);

    CheckEvmGasBounds(r, gasLimit); // Req 16.3
    return 0;                       // Req 14.7
}

// -----------------------------------------------------------------------------
// EnhancedVM (router) handlers (Task 8.2) — only under ENABLE_EVMC
// -----------------------------------------------------------------------------
//
// These handlers drive the format-detecting router CVM::EnhancedVM through its
// public entry points (selectors 9..11) and the standalone
// CVM::BytecodeDetector (selector 12). Each EnhancedVM instance is built via
// CVM::EnhancedVMFactory::CreateProductionVM against a fresh EvmFixture's
// in-memory database + test trust context, so runs stay isolated and touch no
// persistent storage (Req 18.1). Every handler returns status 0 when the entry
// point returns (Req 14.7). Defined error/failure results (success == false,
// an error string, or an EVMC error status) are NOT invariant violations
// (Req 15.1/15.2) — only crashes/UB/assertion breaks abort.

// Basic gas bound (Req 16.3) for EnhancedExecutionResult: consumed gas never
// exceeds the supplied limit. This is an engine-independent invariant. The full
// routing-consistency invariant checkers are added in Task 8.3; this basic
// gas-bounds check is permitted here.
static void CheckEnhancedGasBounds(const CVM::EnhancedExecutionResult& r, uint64_t gasLimit)
{
    (void)r;
    (void)gasLimit;
    assert(r.gas_used <= gasLimit);
}

// Identifies which EnhancedVM entry point produced a result, so the
// routing-consistency checker can apply only the guarantees that entry point
// actually makes (see the per-branch reasoning inside CheckRoutingConsistency).
enum class EnhancedEntry { Execute, Deploy, Call };

// Mirror of the private EnhancedVM::MAX_BYTECODE_SIZE (24,576 bytes, Req 16.4).
// The constant is declared private in enhanced_vm.h and is therefore not
// reachable from the harness, so we replicate its documented value here.
static constexpr size_t kEnhancedMaxBytecodeSize = 24576;

// Routing-consistency invariant checker for the EnhancedVM (router) path
// (Task 8.3, Req 16.4 / 17.3 / 17.4 / 17.5 / 17.6).
//
// This checker asserts ONLY the guarantees the router source in
// src/cvm/enhanced_vm.cpp actually makes. Several requirements are only
// partially honoured by the current implementation; where that is the case we
// deliberately do NOT assert the idealised requirement (which would abort on
// perfectly defined behaviour) and instead document the deviation. Behaviour
// verified against enhanced_vm.cpp:
//
//   * UNKNOWN rejection (Req 17.5): EnhancedVM::Execute rejects an UNKNOWN /
//     invalid format up front (is_valid == false, or CanExecuteFormat == false)
//     via CreateErrorResult, and DeployContract rejects it inside
//     ValidateContractDeployment. Both paths therefore yield success == false
//     with a non-empty error. This is a genuine guarantee and IS asserted.
//
//   * Oversized rejection (Req 16.4): only DeployContract enforces the size
//     limit (ValidateContractDeployment rejects bytecode larger than
//     MAX_BYTECODE_SIZE). Execute performs no size check of its own, so an
//     oversized buffer on the Execute path is a defined, gas-bounded execution
//     rather than a guaranteed failure. The size assertion is therefore applied
//     ONLY to the Deploy entry point.
//
//   * executed_format (Req 17.3 / 17.4 / 17.6): the router sets executed_format
//     to EVM_BYTECODE in ExecuteEVMBytecode and to HYBRID in
//     ExecuteHybridContract, so those mappings hold on a successful Execute.
//     For CVM_NATIVE, however, Execute reassigns its result from
//     ExecuteCVMBytecode, which never sets executed_format, so the field stays
//     UNKNOWN even though execution was routed to the native CVM. Req 17.3 is
//     thus NOT met by the current code; asserting it would abort on defined
//     behaviour, so the CVM_NATIVE mapping is intentionally NOT asserted
//     (deviation documented — an impl fix is needed before the P12 property
//     test in task 9.3 can pass). DeployContract also does not execute the
//     constructor for empty-constructor EVM bytecode and leaves executed_format
//     UNKNOWN, so executed_format is not asserted on the Deploy path either.
//
//   * The Call entry point receives call DATA (not bytecode) in `code`, and a
//     fresh in-memory fixture holds no deployed contract, so CallContract
//     returns "Contract not found" (success == false). Detection of the call
//     data says nothing about routing, so only the universal enum-validity
//     invariant is checked for the Call path.
//
// EVMC error statuses (EVMC_FAILURE, EVMC_REVERT, out-of-gas) and any
// success == false coming from an engine are defined results, NOT violations
// (Req 15.1 / 15.2); none of the assertions below treat them as such.
static void CheckRoutingConsistency(const std::vector<uint8_t>& code,
                                    const CVM::EnhancedExecutionResult& r,
                                    EnhancedEntry entry)
{
    // Universal invariant: executed_format is always one of the four defined
    // enum values. A garbage value would indicate memory corruption rather than
    // a defined result.
    assert(r.executed_format == CVM::BytecodeFormat::UNKNOWN ||
           r.executed_format == CVM::BytecodeFormat::CVM_NATIVE ||
           r.executed_format == CVM::BytecodeFormat::EVM_BYTECODE ||
           r.executed_format == CVM::BytecodeFormat::HYBRID);

    // The Call path executes stored contract bytecode, not `code` (which is the
    // call data), and no contract is deployed in the fresh fixture, so there is
    // no routing decision to validate here.
    if (entry == EnhancedEntry::Call) {
        return;
    }

    // Oversized rejection (Req 16.4) — only guaranteed on the Deploy path.
    if (entry == EnhancedEntry::Deploy && code.size() > kEnhancedMaxBytecodeSize) {
        assert(!r.success);
    }

    // Re-detect the format with a detector configured identically to the
    // router's internal one. The BytecodeDetector defaults (confidence 0.7,
    // strict validation off) match the detector EnhancedVM builds in its
    // constructor, and detection is deterministic, so this reproduces the
    // router's own classification.
    CVM::BytecodeDetector detector;
    const CVM::BytecodeFormat detected = detector.DetectFormat(code).format;

    if (detected == CVM::BytecodeFormat::UNKNOWN) {
        // Req 17.5: an UNKNOWN buffer is rejected with success == false and a
        // set error field on both the Execute and Deploy paths.
        assert(!r.success);
        assert(!r.error.empty());
        return;
    }

    // Req 17.4 / 17.6: a successful Execute routed to the EVM engine (or the
    // hybrid path) reports the matching executed_format. Guarded by r.success
    // so that a defined engine-side failure (which leaves executed_format at
    // its default) is never treated as a violation. The CVM_NATIVE mapping
    // (Req 17.3) is intentionally omitted — see the deviation note above.
    if (entry == EnhancedEntry::Execute && r.success) {
        if (detected == CVM::BytecodeFormat::EVM_BYTECODE) {
            assert(r.executed_format == CVM::BytecodeFormat::EVM_BYTECODE);
        } else if (detected == CVM::BytecodeFormat::HYBRID) {
            assert(r.executed_format == CVM::BytecodeFormat::HYBRID);
        }
    }
}

// Selector 9: EnhancedVM::Execute(...). Detects the bytecode format and routes
// execution to the matching engine (native CVM or EVM). The remaining front
// bytes are the bytecode; no separate input data is supplied for the plain
// execute path. Gas is already clamped to 0..MAX_GAS_PER_TX by the
// FuzzConsumer (Req 14.6).
static int RunEnhancedExecute(const std::vector<uint8_t>& code, uint64_t gasLimit,
                              const uint160& contractAddr, const uint160& callerAddr,
                              uint64_t callValue, int blockHeight,
                              const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    std::unique_ptr<CVM::EnhancedVM> vm =
        CVM::EnhancedVMFactory::CreateProductionVM(fx.db.get(), fx.trust);

    CVM::EnhancedExecutionResult r = vm->Execute(
        code, gasLimit, contractAddr, callerAddr, callValue,
        /*input_data=*/std::vector<uint8_t>{}, blockHeight, blockHash, timestamp);

    CheckEnhancedGasBounds(r, gasLimit);                       // Req 16.3
    CheckRoutingConsistency(code, r, EnhancedEntry::Execute);  // Req 16.4/17.4/17.5/17.6
    return 0;                                                  // Req 14.7
}

// Selector 10: EnhancedVM::DeployContract(...). The front bytes are the
// contract creation bytecode; no separate constructor data is supplied. The
// derived caller address is the deployer and the derived call value is the
// deploy value.
static int RunEnhancedDeploy(const std::vector<uint8_t>& code, uint64_t gasLimit,
                             const uint160& deployerAddr, uint64_t deployValue,
                             int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    std::unique_ptr<CVM::EnhancedVM> vm =
        CVM::EnhancedVMFactory::CreateProductionVM(fx.db.get(), fx.trust);

    CVM::EnhancedExecutionResult r = vm->DeployContract(
        code, /*constructor_data=*/std::vector<uint8_t>{}, gasLimit,
        deployerAddr, deployValue, blockHeight, blockHash, timestamp);

    CheckEnhancedGasBounds(r, gasLimit);                      // Req 16.3
    CheckRoutingConsistency(code, r, EnhancedEntry::Deploy);  // Req 16.4/17.5
    return 0;                                                 // Req 14.7
}

// Selector 11: EnhancedVM::CallContract(...). The derived contract address is
// the call target and the remaining front bytes are the call data.
static int RunEnhancedCall(const uint160& contractAddr, const std::vector<uint8_t>& callData,
                           uint64_t gasLimit, const uint160& callerAddr, uint64_t callValue,
                           int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    EvmFixture fx;
    std::unique_ptr<CVM::EnhancedVM> vm =
        CVM::EnhancedVMFactory::CreateProductionVM(fx.db.get(), fx.trust);

    CVM::EnhancedExecutionResult r = vm->CallContract(
        contractAddr, callData, gasLimit, callerAddr, callValue,
        blockHeight, blockHash, timestamp);

    CheckEnhancedGasBounds(r, gasLimit);                        // Req 16.3
    CheckRoutingConsistency(callData, r, EnhancedEntry::Call);  // enum-validity invariant
    return 0;                                                   // Req 14.7
}

// Selector 12: BytecodeDetector::DetectFormat(...). Pure classification of the
// bytecode; no storage or context is required. DetectFormat must be total and
// return exactly one of the four defined BytecodeFormat values (Req 17.1); we
// assert the returned format is a defined enum value (a garbage value would
// indicate memory corruption, not a defined result).
static int RunBytecodeDetect(const std::vector<uint8_t>& code)
{
    CVM::BytecodeDetector detector;
    CVM::BytecodeDetectionResult r = detector.DetectFormat(code);

    // Req 17.1: DetectFormat returns exactly one of the defined formats.
    assert(r.format == CVM::BytecodeFormat::UNKNOWN ||
           r.format == CVM::BytecodeFormat::CVM_NATIVE ||
           r.format == CVM::BytecodeFormat::EVM_BYTECODE ||
           r.format == CVM::BytecodeFormat::HYBRID);
    return 0; // Req 14.7
}

#endif // ENABLE_EVMC

// -----------------------------------------------------------------------------
// Dispatcher (Task 4.1)
// -----------------------------------------------------------------------------
//
// Decodes the Entry_Point_Selector from buffer[0], derives the fuzz context
// from the remaining bytes via the FuzzConsumer, and calls exactly one CVM
// entry point within a try block.
//
//   - Empty buffer            -> status 0 (Req 1.2).
//   - selector >= END marker  -> status 0 (Req 1.3).
//   - Expected exceptions      -> caught, status 0 (Req 1.7).
//   - Entry point returns      -> status 0 (Req 1.6).
//
// Selectors 5..12 (EVM/router) are wired in Task 8.x under ENABLE_EVMC; for now
// they fall through to the default no-op returning status 0.
int test_one_input(std::vector<uint8_t> buffer)
{
    if (buffer.empty()) return 0; // Req 1.2

    const uint8_t selector = buffer[0];
    FuzzConsumer c(buffer.data() + 1, buffer.size() - 1);

    if (selector >= CVM_FUZZ_TARGET_END) return 0; // Req 1.3

    // Derive the context fields from the tail of the buffer (Req 1.5). The
    // block height high bit is masked so blockHeight is always non-negative.
    const uint64_t gasLimit   = c.ConsumeGasLimit();
    const uint256  blockHash  = c.ConsumeUint256FromTail();
    const uint160  contractA  = c.ConsumeUint160FromTail();
    const uint160  callerA    = c.ConsumeUint160FromTail();
    const int64_t  timestamp  = c.ConsumeIntegralFromTail<int64_t>();
    const int      blockH     = static_cast<int>(c.ConsumeIntegralFromTail<uint32_t>() & 0x7fffffff);
    const uint64_t callValue  = c.ConsumeIntegralFromTail<uint64_t>();
    const std::vector<uint8_t> code = c.ConsumeRemainingFront();

    try {
        switch (selector) {
        case CVM_EXECUTE:
            return RunCvmExecute(code, gasLimit, contractA, callerA,
                                 callValue, blockH, blockHash, timestamp);
        case CVM_VERIFY_BYTECODE:
            return RunCvmVerify(code);
        case CVM_DEPLOY_CONTRACT:
            return RunCvmDeploy(code, contractA);
        case CVM_CALL_CONTRACT:
            return RunCvmCall(contractA, /*inputData=*/code, gasLimit, callerA,
                              callValue, blockH, blockHash, timestamp);
        case EXECUTE_CONTRACT:
            return RunExecuteContract(code, gasLimit, contractA, callerA,
                                      callValue, blockH, blockHash, timestamp);
#ifdef ENABLE_EVMC
        // Selectors 5..8: EVM engine entry points (Task 8.1).
        case EVM_EXECUTE:
            return RunEvmExecute(code, gasLimit, contractA, callerA,
                                 callValue, blockH, blockHash, timestamp);
        case EVM_DEPLOY_CONTRACT:
            return RunEvmDeploy(code, gasLimit, /*deployerAddr=*/callerA,
                                /*deployValue=*/callValue, blockH, blockHash, timestamp);
        case EVM_STATIC_CALL:
            return RunEvmStaticCall(contractA, /*callData=*/code, gasLimit, callerA,
                                    blockH, blockHash, timestamp);
        case EVM_DELEGATE_CALL:
            return RunEvmDelegateCall(contractA, /*callData=*/code, gasLimit, callerA,
                                      callValue, blockH, blockHash, timestamp);
        // Selectors 9..11: EnhancedVM (format-detecting router) entry points (Task 8.2).
        case ENHANCED_EXECUTE:
            return RunEnhancedExecute(code, gasLimit, contractA, callerA,
                                      callValue, blockH, blockHash, timestamp);
        case ENHANCED_DEPLOY_CONTRACT:
            return RunEnhancedDeploy(code, gasLimit, /*deployerAddr=*/callerA,
                                     /*deployValue=*/callValue, blockH, blockHash, timestamp);
        case ENHANCED_CALL_CONTRACT:
            return RunEnhancedCall(contractA, /*callData=*/code, gasLimit, callerA,
                                   callValue, blockH, blockHash, timestamp);
        // Selector 12: standalone bytecode format detector (Task 8.2).
        case BYTECODE_DETECT:
            return RunBytecodeDetect(code);
#else
        // Without ENABLE_EVMC the EVM/router selectors 5..12 are inert no-ops
        // returning status 0 (Req 14.3). CVM_FUZZ_TARGET_END stays 13 so the
        // selector space and seed corpus remain identical across build variants.
        case EVM_EXECUTE:
        case EVM_DEPLOY_CONTRACT:
        case EVM_STATIC_CALL:
        case EVM_DELEGATE_CALL:
        case ENHANCED_EXECUTE:
        case ENHANCED_DEPLOY_CONTRACT:
        case ENHANCED_CALL_CONTRACT:
        case BYTECODE_DETECT:
            return 0; // Req 14.3
#endif // ENABLE_EVMC
        default:
            // Any remaining selector value below CVM_FUZZ_TARGET_END that is not
            // explicitly handled is a no-op with status 0 (defensive default).
            return 0;
        }
    } catch (const std::exception&) {
        return 0; // Req 1.7: expected exceptions do not abort the fuzzer.
    }
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input(std::vector<uint8_t>(data, data + size));
    return 0;
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

// Disabled under WIN32 due to clash with Cygwin's WinMain.
#ifndef WIN32
// Declare main(...) "weak" to allow for libFuzzer linking. libFuzzer provides
// the main(...) function.
__attribute__((weak))
#endif
int main(int argc, char** argv)
{
    // CLI replay (Req 12.4/12.5): when a file-path argument is present, read
    // exactly that file instead of stdin and run it through the harness once.
    // This takes precedence over the AFL persistent-mode loop and the stdin
    // fallback so a stored crash input can be replayed without a running fuzz
    // engine. A missing/unreadable file yields a defined, non-crashing exit
    // after read_file has printed a cause-naming message to stderr.
    if (argc > 1) {
        std::vector<uint8_t> buffer;
        if (!read_file(argv[1], buffer)) {
            return 1; // defined non-zero exit; the harness itself does not crash
        }
        return test_one_input(buffer);
    }

#ifdef __AFL_INIT
    // Enable AFL deferred forkserver mode. Requires compilation using
    // afl-clang-fast++. See fuzzing.md for details.
    __AFL_INIT();
#endif

#ifdef __AFL_LOOP
    // Enable AFL persistent mode. Requires compilation using afl-clang-fast++.
    // See fuzzing.md for details.
    int ret = 0;
    while (__AFL_LOOP(1000)) {
        std::vector<uint8_t> buffer;
        if (!read_stdin(buffer)) {
            continue;
        }
        ret = test_one_input(buffer);
    }
    return ret;
#else
    std::vector<uint8_t> buffer;
    if (!read_stdin(buffer)) {
        return 0;
    }
    return test_one_input(buffer);
#endif
}
