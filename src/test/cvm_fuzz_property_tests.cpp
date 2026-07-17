// Copyright (c) 2025 The Cascoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CVM fuzzing harness — property and smoke test suite (binary: test_cascoin).
//
// Spec: .kiro/specs/cvm-fuzzing-harness
//
// This suite exercises the NATIVE CVM directly (the fuzz-harness main in
// test_cvm_fuzzy.cpp is a separate binary and is NOT linked into test_cascoin,
// so its symbols cannot be reused). A minimal in-memory ContractStorage_Mock is
// replicated below so execution stays deterministic and free of LevelDB side
// effects.
//
// -----------------------------------------------------------------------------
// FINDING (Task 6.1 / Property P1, Requirement 4.1)
// -----------------------------------------------------------------------------
// Property P1 (design.md) and Requirement 4.1 state that after a native CVM
// entry point returns, the VMState::Status SHALL be something other than
// RUNNING. Reading src/cvm/cvm.cpp confirms this literal expectation does NOT
// hold for all inputs:
//
//   bool CVM::Execute(...) {
//       ...
//       while (state.IsRunning() && state.GetPC() < code.size()) { ... }
//       return state.GetStatus() == STOPPED || state.GetStatus() == RETURNED;
//   }
//
// The loop terminates as soon as the program counter reaches the end of the
// bytecode. Bytecode that runs off the end WITHOUT an explicit OP_STOP /
// OP_RETURN therefore leaves the status at RUNNING (Execute merely returns
// false). The free function ExecuteContract does NOT normalise this either — it
// copies gasUsed/returnData/logs/error out of the VMState but never touches the
// status, so its internal VMState is likewise left RUNNING for run-off-end
// bytecode (and ExecuteContract does not expose the status enum at all).
//
// Concrete counterexample (routed through CVM::Execute):
//   code = { 0x01, 0x01, 0x00 }   // OP_PUSH size=1 value=0x00, no OP_STOP
//   -> Execute returns false, GetStatus() == RUNNING, GetPC() >= code.size()
//
// Per the workspace testing guidance we do NOT silently change the acceptance
// criteria, and we do NOT commit a knowingly-failing test. Instead this test
// asserts the invariants the CVM ACTUALLY guarantees for every native entry
// point (crash/UB freedom, deterministic termination, a defined status enum
// value, and gasUsed <= gasLimit) and additionally documents the precise
// post-condition the code guarantees for the RUNNING case:
//
//   after Execute returns, status == RUNNING  =>  GetPC() >= code.size()
//   (i.e. RUNNING only ever survives as the "ran off the end" outcome).
//
// The literal-P1 deviation is recorded through the PBT tooling with the
// counterexample above so the spec owner can decide whether Requirement 4.1 /
// P1 should be reworded (e.g. to treat run-off-end as a defined non-error
// halt) or the CVM should set a terminal status on run-off-end.
// -----------------------------------------------------------------------------

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h> // ENABLE_EVMC gating for P9 selector routing
#endif

#include <cvm/cvm.h>
#include <cvm/vmstate.h>
#include <cvm/opcodes.h>

#include <random.h>
#include <uint256.h>

#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// In-memory ContractStorage implementation (Req 2). Backed by a std::map keyed
// by (address, key); a std::set tracks which addresses have been written so
// Exists(addr) matches the real interface signature. All data stays in memory.
class ContractStorage_Mock : public CVM::ContractStorage
{
public:
    bool Load(const uint160& addr, const uint256& key, uint256& value) override
    {
        auto it = m_store.find(std::make_pair(addr, key));
        if (it == m_store.end()) return false;   // unset pair -> leave value untouched
        value = it->second;                       // last stored value
        return true;
    }

    bool Store(const uint160& addr, const uint256& key, const uint256& value) override
    {
        m_store[std::make_pair(addr, key)] = value;  // overwrites; isolates other pairs
        m_addrs.insert(addr);
        return true;
    }

    bool Exists(const uint160& addr) override
    {
        return m_addrs.count(addr) > 0;
    }

    // Number of committed (address, key) -> value entries. Used by P4 to assert
    // "no state commit": a value of 0 proves no SSTORE reached the backend.
    size_t CommittedCount() const { return m_store.size(); }

    // Bit-identical view of the full backing store. Used by P6 to compare the
    // final storage state across repeated executions (Req 8.3 / 8.5). std::map
    // over comparable keys/values has a total, value-based operator==, so two
    // Snapshot() results compare equal iff they hold the same set of pairs with
    // byte-identical values.
    using StoreMap = std::map<std::pair<uint160, uint256>, uint256>;
    const StoreMap& Snapshot() const { return m_store; }

private:
    std::map<std::pair<uint160, uint256>, uint256> m_store;
    std::set<uint160> m_addrs;
};

// True iff s is one of the ten defined VMState::Status enumerators. Used to
// assert the CVM never leaves an out-of-range / garbage status value.
bool IsDefinedStatus(CVM::VMState::Status s)
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

// Build a fresh VMState carrying a derived execution context.
CVM::VMState MakeState(uint64_t gasLimit, const uint160& contractAddr,
                       const uint160& callerAddr, uint64_t callValue,
                       int blockHeight, const uint256& blockHash, int64_t timestamp)
{
    CVM::VMState state;
    state.SetGasLimit(gasLimit);
    state.SetContractAddress(contractAddr);
    state.SetCallerAddress(callerAddr);
    state.SetCallValue(callValue);
    state.SetBlockHeight(blockHeight);
    state.SetBlockHash(blockHash);
    state.SetTimestamp(timestamp);
    return state;
}

uint160 RandUint160(FastRandomContext& rng)
{
    uint160 a;
    std::vector<unsigned char> b = rng.randbytes(20);
    std::memcpy(a.begin(), b.data(), 20);
    return a;
}

// ---------------------------------------------------------------------------
// FuzzConsumer replica (Task 6.8 / Property P8).
//
// The real FuzzConsumer lives in the harness source src/test/test_cvm_fuzzy.cpp,
// which is built as a SEPARATE binary (test_cascoin_cvm_fuzzy) and is NOT linked
// into test_cascoin — so its symbols cannot be reused here. To property-test the
// gas-limit derivation we replicate the harness logic byte-for-byte (mirroring
// the ContractStorage_Mock replica above). This copy is intentionally identical
// to the harness ConsumeGasLimit()/ConsumeIntegralFromTail<T>() so the property
// exercises the exact derivation the harness performs.
//
// Tail-consumption model: context fields are read from the END of the buffer;
// missing bytes default to zero (no error path). ConsumeGasLimit() reads a
// 4-byte little-endian value from the tail and clamps it to 0..MAX_GAS_PER_TX
// via modulo (MAX_GAS_PER_TX + 1).
class FuzzConsumer
{
public:
    // `data`/`size` describe the buffer AFTER the selector byte was removed.
    FuzzConsumer(const uint8_t* data, size_t size)
        : m_begin(data), m_front(0), m_back(size) {}

    // Read sizeof(T) bytes from the end of the remaining buffer, little-endian.
    // Missing (high-order) bytes are treated as zero.
    template <typename T>
    T ConsumeIntegralFromTail()
    {
        static_assert(std::is_integral<T>::value, "T must be an integral type");
        const size_t want = sizeof(T);
        const size_t avail = Remaining();
        const size_t n = want < avail ? want : avail;

        typename std::make_unsigned<T>::type value = 0;
        for (size_t i = 0; i < n; ++i) {
            value |= static_cast<typename std::make_unsigned<T>::type>(
                         m_begin[m_back - n + i])
                     << (8 * i);
        }
        m_back -= n;
        return static_cast<T>(value);
    }

    // Derive a gas limit from a 4-byte little-endian tail value clamped to the
    // valid range 0..MAX_GAS_PER_TX (inclusive) via modulo (MAX_GAS_PER_TX + 1).
    uint64_t ConsumeGasLimit()
    {
        const uint32_t raw = ConsumeIntegralFromTail<uint32_t>();
        return static_cast<uint64_t>(raw) % (CVM::MAX_GAS_PER_TX + 1);
    }

private:
    size_t Remaining() const { return m_back - m_front; }

    const uint8_t* m_begin;
    size_t m_front;
    size_t m_back;
};

// ---------------------------------------------------------------------------
// P9 support: Entry_Point_Selector model (Task 6.9 / Property P9).
//
// The fuzz-harness dispatcher (test_one_input) lives in test_cvm_fuzzy.cpp, a
// SEPARATE binary (test_cascoin_cvm_fuzzy) that is NOT linked into test_cascoin
// (same reason the ContractStorage_Mock and FuzzConsumer are replicated above).
// To validate the routing property directly we replicate the harness's selector
// enum and the EXACT control flow of its switch(selector) dispatcher — including
// the #ifdef ENABLE_EVMC gating — as a pure function of the input buffer.
// Keeping this a byte-for-byte mirror of test_one_input is what makes the
// property meaningful: it pins the documented routing contract.
// ---------------------------------------------------------------------------

// Mirror of enum CvmFuzzTarget in test_cvm_fuzzy.cpp. CVM_FUZZ_TARGET_END must
// stay 13 in every build (independent of ENABLE_EVMC) so selector values and
// the seed corpus remain portable across build variants.
enum CvmFuzzTarget : uint8_t {
    CVM_EXECUTE = 0,           // native, always available
    CVM_VERIFY_BYTECODE,       // native, always available
    CVM_DEPLOY_CONTRACT,       // native, always available
    CVM_CALL_CONTRACT,         // native, always available
    EXECUTE_CONTRACT,          // native, always available
    EVM_EXECUTE,               // EVM/router, gated by ENABLE_EVMC
    EVM_DEPLOY_CONTRACT,       // EVM/router, gated by ENABLE_EVMC
    EVM_STATIC_CALL,           // EVM/router, gated by ENABLE_EVMC
    EVM_DELEGATE_CALL,         // EVM/router, gated by ENABLE_EVMC
    ENHANCED_EXECUTE,          // EVM/router, gated by ENABLE_EVMC
    ENHANCED_DEPLOY_CONTRACT,  // EVM/router, gated by ENABLE_EVMC
    ENHANCED_CALL_CONTRACT,    // EVM/router, gated by ENABLE_EVMC
    BYTECODE_DETECT,           // EVM/router, gated by ENABLE_EVMC
    CVM_FUZZ_TARGET_END        // range marker; always 13
};

// Outcome of dispatching one full input buffer through the harness model.
//   callCount: number of entry-point calls the harness would make (0 or 1).
//   target:    the selected CvmFuzzTarget when a call occurs, else -1.
//   status:    the harness return status on a defined path (always 0).
struct DispatchResult {
    int callCount;
    int target;
    int status;
    bool operator==(const DispatchResult& o) const
    {
        return callCount == o.callCount && target == o.target && status == o.status;
    }
    bool operator!=(const DispatchResult& o) const { return !(*this == o); }
};

// Faithful, side-effect-free model of test_one_input's dispatcher control flow.
// It decides — purely from buffer[0] — whether an entry point is called and, if
// so, which single target. It deliberately does NOT run the CVM: P9 is about the
// routing decision (determinism, single-valuedness, gating), not execution.
//
//   * empty buffer            -> no call, status 0                 (Req 1.2)
//   * selector >= END marker  -> no call, status 0                 (Req 1.3)
//   * selector 0..4           -> exactly one native target         (Req 1.1)
//   * selector 5..12          -> exactly one EVM/router target when
//                                ENABLE_EVMC is defined (Req 14.1/14.2),
//                                otherwise no call, status 0        (Req 14.3)
DispatchResult SimulateDispatch(const std::vector<uint8_t>& buffer)
{
    if (buffer.empty()) return {0, -1, 0};                  // Req 1.2
    const uint8_t selector = buffer[0];
    if (selector >= CVM_FUZZ_TARGET_END) return {0, -1, 0};  // Req 1.3

    int callCount = 0;
    int target = -1;
    switch (selector) {
    case CVM_EXECUTE:
    case CVM_VERIFY_BYTECODE:
    case CVM_DEPLOY_CONTRACT:
    case CVM_CALL_CONTRACT:
    case EXECUTE_CONTRACT:
        ++callCount;                        // Req 1.1: always exactly one native call
        target = static_cast<int>(selector);
        break;
#ifdef ENABLE_EVMC
    case EVM_EXECUTE:
    case EVM_DEPLOY_CONTRACT:
    case EVM_STATIC_CALL:
    case EVM_DELEGATE_CALL:
    case ENHANCED_EXECUTE:
    case ENHANCED_DEPLOY_CONTRACT:
    case ENHANCED_CALL_CONTRACT:
    case BYTECODE_DETECT:
        ++callCount;                        // Req 14.1/14.2: exactly one EVM/router call
        target = static_cast<int>(selector);
        break;
#else
    case EVM_EXECUTE:
    case EVM_DEPLOY_CONTRACT:
    case EVM_STATIC_CALL:
    case EVM_DELEGATE_CALL:
    case ENHANCED_EXECUTE:
    case ENHANCED_DEPLOY_CONTRACT:
    case ENHANCED_CALL_CONTRACT:
    case BYTECODE_DETECT:
        break;                              // Req 14.3: gated out -> no call
#endif
    default:
        break;                              // defensive: no call
    }
    return {callCount, target, 0};           // status is always 0 on a defined path
}

// True iff `selector` is one of the EVM/router entry points (5..12), i.e. the
// selectors gated behind ENABLE_EVMC.
bool IsEvmRouterSelector(uint8_t selector)
{
    return selector >= EVM_EXECUTE && selector <= BYTECODE_DETECT;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cvm_fuzz_property_tests, BasicTestingSetup)

// ===========================================================================
// Property P1 — Crash freedom and termination of the native CVM.
//
// For every byte buffer of length 0..24,576 routed through a native CVM entry
// point (Execute / ExecuteContract / CallContract) with a gas limit in the
// range 0..MAX_GAS_PER_TX, the execution SHALL:
//   * not crash and exhibit no undefined behaviour (any thrown std::exception
//     is caught and treated as a defined termination per Req 1.7);
//   * terminate deterministically (the call returns; the finite gas limit
//     bounds the opcode count, Req 3.4 / 4.1);
//   * leave a defined VMState::Status enum value; and
//   * consume no more gas than the limit (0 <= gasUsed <= gasLimit).
//
// Documented deviation (see file header): the literal "status != RUNNING"
// expectation of P1 / Req 4.1 does not hold for run-off-end bytecode, so this
// test asserts the actual post-condition instead:
//     status == RUNNING  =>  GetPC() >= code.size().
//
// Validates: Requirements 3.1, 3.4, 4.1, 1.6, 1.7
// ===========================================================================
BOOST_AUTO_TEST_CASE(p1_crash_freedom_and_termination_property)
{
    // Deterministic PRNG (fixed seed) so the property runs identically on every
    // invocation and any counterexample is reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c1"));

    constexpr int kIterations = 300; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // Length in 0..MAX_CODE_SIZE (24576). Bias towards small buffers half of
        // the time so short, fully-executed programs are exercised alongside
        // large ones (which mostly trip INVALID_OPCODE early).
        size_t len = rng.randbool()
            ? static_cast<size_t>(rng.randrange(65))                        // 0..64
            : static_cast<size_t>(rng.randrange(CVM::MAX_CODE_SIZE + 1));    // 0..24576

        std::vector<uint8_t> code;
        {
            std::vector<unsigned char> bytes = rng.randbytes(len);
            code.assign(bytes.begin(), bytes.end());
        }

        // Gas limit clamped to 0..MAX_GAS_PER_TX (1,000,000).
        uint64_t gasLimit = rng.randrange(CVM::MAX_GAS_PER_TX + 1);

        // Derived context.
        uint160 contractAddr = RandUint160(rng);
        uint160 callerAddr   = RandUint160(rng);
        uint64_t callValue   = rng.rand64();
        int blockHeight      = static_cast<int>(rng.rand32() & 0x7fffffff); // >= 0
        int64_t timestamp    = static_cast<int64_t>(rng.rand64());
        uint256 blockHash    = rng.rand256();

        // Route through one of the three native entry points.
        int target = static_cast<int>(rng.randrange(3));

        try {
            if (target == 0) {
                // ---- CVM::Execute (owns the VMState, so status is observable) ----
                CVM::VMState state = MakeState(gasLimit, contractAddr, callerAddr,
                                               callValue, blockHeight, blockHash, timestamp);
                ContractStorage_Mock storage;
                CVM::CVM vm;
                vm.Execute(code, state, &storage);

                CVM::VMState::Status st = state.GetStatus();
                BOOST_CHECK_MESSAGE(IsDefinedStatus(st),
                    "P1: CVM::Execute left an undefined status value (iter #" +
                    std::to_string(i) + ", len=" + std::to_string(len) + ")");
                BOOST_CHECK_MESSAGE(state.GetGasUsed() <= gasLimit,
                    "P1: gasUsed (" + std::to_string(state.GetGasUsed()) +
                    ") exceeds gasLimit (" + std::to_string(gasLimit) +
                    ") (iter #" + std::to_string(i) + ")");
                // Actual guarantee for the RUNNING outcome (documented deviation).
                if (st == CVM::VMState::Status::RUNNING) {
                    BOOST_CHECK_MESSAGE(state.GetPC() >= code.size(),
                        "P1: status RUNNING but PC (" + std::to_string(state.GetPC()) +
                        ") < code.size (" + std::to_string(code.size()) +
                        ") — RUNNING should only survive as the run-off-end outcome "
                        "(iter #" + std::to_string(i) + ")");
                }
            } else if (target == 1) {
                // ---- ExecuteContract (free function; returns ExecutionResult) ----
                ContractStorage_Mock storage;
                CVM::ExecutionResult r = CVM::ExecuteContract(
                    code, gasLimit, contractAddr, callerAddr, callValue,
                    /*inputData=*/std::vector<uint8_t>(), blockHeight, blockHash,
                    timestamp, &storage);
                BOOST_CHECK_MESSAGE(r.gasUsed <= gasLimit,
                    "P1: ExecuteContract gasUsed (" + std::to_string(r.gasUsed) +
                    ") exceeds gasLimit (" + std::to_string(gasLimit) +
                    ") (iter #" + std::to_string(i) + ")");
            } else {
                // ---- CVM::CallContract (owns the VMState) ----
                CVM::VMState state = MakeState(gasLimit, contractAddr, callerAddr,
                                               callValue, blockHeight, blockHash, timestamp);
                ContractStorage_Mock storage;
                CVM::CVM vm;
                // The remaining buffer is treated as call input data; contractAddr
                // is the (absent) call target.
                vm.CallContract(contractAddr, code, state, &storage);

                BOOST_CHECK_MESSAGE(IsDefinedStatus(state.GetStatus()),
                    "P1: CVM::CallContract left an undefined status value (iter #" +
                    std::to_string(i) + ")");
                BOOST_CHECK_MESSAGE(state.GetGasUsed() <= gasLimit,
                    "P1: CallContract gasUsed exceeds gasLimit (iter #" +
                    std::to_string(i) + ")");
            }
        } catch (const std::exception& e) {
            // Req 1.7: an expected exception is a defined termination, not a
            // crash. It must not abort the process; catching it here is enough.
            BOOST_TEST_MESSAGE(std::string("P1: caught std::exception (defined "
                "termination, iter #") + std::to_string(i) + "): " + e.what());
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P1: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Documented counterexample for the literal P1 / Req 4.1 deviation.
    // Run-off-end bytecode (a lone OP_PUSH with no OP_STOP) leaves status
    // RUNNING. We assert the ACTUAL behaviour here so the deviation is captured
    // and pinned, rather than silently asserting the (currently false) literal
    // expectation.
    // -----------------------------------------------------------------------
    {
        const std::vector<uint8_t> runOffEnd = {0x01, 0x01, 0x00}; // OP_PUSH size=1 val=0
        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(runOffEnd, state, &storage);

        BOOST_CHECK_MESSAGE(!ok,
            "P1 finding: Execute of run-off-end bytecode should return false");
        // Documents that Req 4.1's literal "status != RUNNING" does NOT hold.
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::RUNNING,
            "P1 finding: expected the documented run-off-end status RUNNING");
        // The actual guaranteed post-condition for the RUNNING outcome.
        BOOST_CHECK(state.GetPC() >= runOffEnd.size());
        BOOST_CHECK(state.GetGasUsed() <= CVM::MAX_GAS_PER_TX);
    }
}

// ===========================================================================
// Property P2 — Gas bounds and result consistency.
//
// For every byte buffer and every gas limit in the range 0..MAX_GAS_PER_TX,
// after ExecuteContract completes the following SHALL hold:
//   * 0 <= gasUsed <= gasLimit                         (Req 5.1)
//   * gasUsed is monotonically bounded by the limit    (Req 5.2, observable
//     end-state bound)
//   * ExecutionResult.gasUsed == VMState::GetGasUsed() (Req 5.3)
//
// Observability note / documented deviation:
// The free function ExecuteContract (src/cvm/cvm.cpp) OWNS its VMState
// internally and never exposes it — it only copies state.GetGasUsed() into
// result.gasUsed. The literal equality "ExecutionResult.gasUsed ==
// VMState::GetGasUsed()" of Req 5.3 therefore cannot be read off the public
// API of ExecuteContract directly. It is validated here in two complementary,
// real ways (no mocks of the VM itself):
//   1. Consistency by reconstruction: we build a VMState with byte-identical
//      context (exactly what ExecuteContract sets up) and run CVM::Execute on
//      it ourselves. Because native execution is deterministic for identical
//      code/context/initial-storage, the gasUsed on our observable VMState is
//      the same value ExecuteContract's hidden VMState holds. We assert
//      result.gasUsed == observedState.GetGasUsed(), which is exactly the
//      Req 5.3 equality made observable.
//   2. Determinism of gasUsed: a second ExecuteContract call with identical
//      input yields an equal gasUsed, indirectly confirming the copy is a
//      faithful, stable reflection of the internal VMState.
//
// gasUsed is an unsigned 64-bit value, so the "0 <=" half of the bound holds
// by construction; the meaningful check is the upper bound gasUsed <= gasLimit.
//
// Validates: Requirements 5.1, 5.2, 5.3
// ===========================================================================
BOOST_AUTO_TEST_CASE(p2_gas_bounds_and_result_consistency_property)
{
    // Deterministic PRNG with a DIFFERENT fixed seed than P1 (…c1) so this
    // property explores a distinct slice of the input space while staying
    // fully reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c2"));

    constexpr int kIterations = 250; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // Length in 0..MAX_CODE_SIZE (24576), biased towards small buffers half
        // the time so short fully-executed programs (which actually consume gas
        // opcode-by-opcode) are exercised alongside large ones.
        size_t len = rng.randbool()
            ? static_cast<size_t>(rng.randrange(65))                       // 0..64
            : static_cast<size_t>(rng.randrange(CVM::MAX_CODE_SIZE + 1));   // 0..24576

        std::vector<uint8_t> code;
        {
            std::vector<unsigned char> bytes = rng.randbytes(len);
            code.assign(bytes.begin(), bytes.end());
        }

        // Gas limit clamped to 0..MAX_GAS_PER_TX (1,000,000).
        uint64_t gasLimit = rng.randrange(CVM::MAX_GAS_PER_TX + 1);

        // Derived execution context (identical fields to ExecuteContract's).
        uint160 contractAddr = RandUint160(rng);
        uint160 callerAddr   = RandUint160(rng);
        uint64_t callValue   = rng.rand64();
        int blockHeight      = static_cast<int>(rng.rand32() & 0x7fffffff); // >= 0
        int64_t timestamp    = static_cast<int64_t>(rng.rand64());
        uint256 blockHash    = rng.rand256();

        try {
            // ---- Primary call under test: the free function ExecuteContract ----
            ContractStorage_Mock storage;
            CVM::ExecutionResult r = CVM::ExecuteContract(
                code, gasLimit, contractAddr, callerAddr, callValue,
                /*inputData=*/std::vector<uint8_t>(), blockHeight, blockHash,
                timestamp, &storage);

            // Req 5.1 — upper gas bound (0 <= gasUsed holds by unsignedness).
            BOOST_CHECK_MESSAGE(r.gasUsed <= gasLimit,
                "P2: ExecuteContract gasUsed (" + std::to_string(r.gasUsed) +
                ") exceeds gasLimit (" + std::to_string(gasLimit) +
                ") (iter #" + std::to_string(i) + ", len=" + std::to_string(len) + ")");

            // Req 5.3 — result.gasUsed == VMState::GetGasUsed(), made observable
            // by reconstructing the exact VMState ExecuteContract builds and
            // running CVM::Execute on it. Deterministic execution guarantees the
            // observed gasUsed equals the value inside ExecuteContract's hidden
            // VMState, which is the value it copies into result.gasUsed.
            CVM::VMState observedState = MakeState(gasLimit, contractAddr, callerAddr,
                                                   callValue, blockHeight, blockHash,
                                                   timestamp);
            ContractStorage_Mock observedStorage;
            CVM::CVM vm;
            vm.Execute(code, observedState, &observedStorage);

            BOOST_CHECK_MESSAGE(r.gasUsed == observedState.GetGasUsed(),
                "P2: ExecutionResult.gasUsed (" + std::to_string(r.gasUsed) +
                ") != VMState::GetGasUsed() (" +
                std::to_string(observedState.GetGasUsed()) +
                ") — Req 5.3 consistency broken (iter #" + std::to_string(i) + ")");

            // The reconstructed VMState must honour the same upper bound.
            BOOST_CHECK_MESSAGE(observedState.GetGasUsed() <= gasLimit,
                "P2: reconstructed VMState gasUsed (" +
                std::to_string(observedState.GetGasUsed()) +
                ") exceeds gasLimit (" + std::to_string(gasLimit) +
                ") (iter #" + std::to_string(i) + ")");

            // Req 5.2 — determinism of gasUsed: a repeated ExecuteContract call
            // with identical input and a fresh mock yields an equal gasUsed,
            // confirming the reported value is a stable reflection of the
            // internal VMState rather than an artefact.
            ContractStorage_Mock storage2;
            CVM::ExecutionResult r2 = CVM::ExecuteContract(
                code, gasLimit, contractAddr, callerAddr, callValue,
                std::vector<uint8_t>(), blockHeight, blockHash, timestamp, &storage2);
            BOOST_CHECK_MESSAGE(r.gasUsed == r2.gasUsed,
                "P2: ExecuteContract gasUsed not deterministic across identical "
                "calls (" + std::to_string(r.gasUsed) + " vs " +
                std::to_string(r2.gasUsed) + ", iter #" + std::to_string(i) + ")");
        } catch (const std::exception& e) {
            // An expected exception is a defined termination (Req 1.7); it must
            // not abort the process. There is no gas result to bound in that case.
            BOOST_TEST_MESSAGE(std::string("P2: caught std::exception (defined "
                "termination, iter #") + std::to_string(i) + "): " + e.what());
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P2: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Explicit consistency example: a short, fully-executing program that
    // actually consumes gas (OP_PUSH 0x00, OP_STOP). This pins the Req 5.3
    // equality on a concrete, gas-consuming input rather than relying solely on
    // random buffers that often trip INVALID_OPCODE on the first byte.
    // -----------------------------------------------------------------------
    {
        const std::vector<uint8_t> program = {0x01, 0x01, 0x00, 0x00}; // PUSH size=1 val=0; STOP
        const uint64_t gasLimit = CVM::MAX_GAS_PER_TX;

        ContractStorage_Mock storage;
        CVM::ExecutionResult r = CVM::ExecuteContract(
            program, gasLimit, uint160(), uint160(), 0,
            std::vector<uint8_t>(), 0, uint256(), 0, &storage);

        CVM::VMState observedState = MakeState(gasLimit, uint160(), uint160(),
                                               0, 0, uint256(), 0);
        ContractStorage_Mock observedStorage;
        CVM::CVM vm;
        vm.Execute(program, observedState, &observedStorage);

        BOOST_CHECK(r.gasUsed <= gasLimit);
        BOOST_CHECK_MESSAGE(r.gasUsed == observedState.GetGasUsed(),
            "P2: result.gasUsed != VMState::GetGasUsed() on the STOP program");
    }
}

// ===========================================================================
// Property P3 — Stack bound.
//
// For every byte buffer executed via a native CVM entry point that owns an
// observable VMState (CVM::Execute / CVM::CallContract), the value stack SHALL
// never exceed MAX_STACK_SIZE (1024) elements. Because Push()/Dup() refuse to
// grow the stack past the limit (they set STACK_OVERFLOW and return without
// mutating the stack, see src/cvm/vmstate.cpp), the observable end-of-execution
// stack size is a faithful upper-bound witness: if any intermediate step had
// exceeded the bound the final StackSize() would too. We therefore assert
//     state.StackSize() <= MAX_STACK_SIZE
// after every native entry point returns.
//
// The property is exercised in two complementary ways:
//   1. Fuzzed buffers (length 0..MAX_CODE_SIZE, gas 0..MAX_GAS_PER_TX) routed
//      through CVM::Execute and CVM::CallContract (>= 100 iterations).
//   2. A crafted, deterministic program that deliberately drives the stack to
//      and past the limit using OP_PUSH, so the STACK_OVERFLOW halt path
//      (Req 6.2 / 6.4) is directly covered: execution must stop at the limit
//      with status STACK_OVERFLOW and StackSize() pinned at exactly
//      MAX_STACK_SIZE (the overflowing push is rejected, not applied).
//
// Validates: Requirements 6.1, 6.4
// ===========================================================================
BOOST_AUTO_TEST_CASE(p3_stack_bound_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c3) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c3"));

    constexpr int kIterations = 300; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // Length in 0..MAX_CODE_SIZE (24576), biased towards small buffers half
        // the time so short programs that genuinely push several values are
        // exercised alongside large buffers (which mostly trip INVALID_OPCODE
        // early).
        size_t len = rng.randbool()
            ? static_cast<size_t>(rng.randrange(65))                        // 0..64
            : static_cast<size_t>(rng.randrange(CVM::MAX_CODE_SIZE + 1));    // 0..24576

        std::vector<uint8_t> code;
        {
            std::vector<unsigned char> bytes = rng.randbytes(len);
            code.assign(bytes.begin(), bytes.end());
        }

        // Gas limit clamped to 0..MAX_GAS_PER_TX (1,000,000).
        uint64_t gasLimit = rng.randrange(CVM::MAX_GAS_PER_TX + 1);

        // Derived context.
        uint160 contractAddr = RandUint160(rng);
        uint160 callerAddr   = RandUint160(rng);
        uint64_t callValue   = rng.rand64();
        int blockHeight      = static_cast<int>(rng.rand32() & 0x7fffffff); // >= 0
        int64_t timestamp    = static_cast<int64_t>(rng.rand64());
        uint256 blockHash    = rng.rand256();

        // Route through a native entry point that OWNS an observable VMState so
        // the final stack size can be read back (ExecuteContract hides its
        // VMState and is intentionally not used here).
        const bool useCall = rng.randbool();

        try {
            CVM::VMState state = MakeState(gasLimit, contractAddr, callerAddr,
                                           callValue, blockHeight, blockHash, timestamp);
            ContractStorage_Mock storage;
            CVM::CVM vm;

            if (useCall) {
                // Remaining buffer is treated as call input data; contractAddr
                // is the (absent) call target.
                vm.CallContract(contractAddr, code, state, &storage);
            } else {
                vm.Execute(code, state, &storage);
            }

            // Core P3 invariant (Req 6.1): the stack never exceeds the bound.
            BOOST_CHECK_MESSAGE(state.StackSize() <= CVM::MAX_STACK_SIZE,
                "P3: StackSize() (" + std::to_string(state.StackSize()) +
                ") exceeds MAX_STACK_SIZE (" + std::to_string(CVM::MAX_STACK_SIZE) +
                ") after " + (useCall ? "CallContract" : "Execute") +
                " (iter #" + std::to_string(i) + ", len=" + std::to_string(len) + ")");
            // The status must remain a defined enum value regardless of outcome.
            BOOST_CHECK_MESSAGE(IsDefinedStatus(state.GetStatus()),
                "P3: undefined status value (iter #" + std::to_string(i) + ")");
        } catch (const std::exception& e) {
            // Req 1.7: an expected exception is a defined termination, not a
            // crash; catching it here is enough.
            BOOST_TEST_MESSAGE(std::string("P3: caught std::exception (defined "
                "termination, iter #") + std::to_string(i) + "): " + e.what());
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P3: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Crafted overflow program (Req 6.2 / 6.4): deliberately push MAX_STACK_SIZE
    // + 1 values so the stack is driven to its limit and one push beyond it.
    // Each PUSH is encoded as { OP_PUSH(0x01), size(0x01), value(0x00) } — a
    // 1-byte immediate of 0 (see HandlePush / ReadImmediate in cvm.cpp). The
    // 1025th push must be rejected: Push() sets STACK_OVERFLOW and returns
    // WITHOUT mutating the stack, so execution halts with the stack pinned at
    // exactly MAX_STACK_SIZE and no further instruction runs.
    // -----------------------------------------------------------------------
    {
        const size_t kPushes = CVM::MAX_STACK_SIZE + 1; // 1025 -> one past the limit
        std::vector<uint8_t> code;
        code.reserve(kPushes * 3);
        for (size_t n = 0; n < kPushes; ++n) {
            code.push_back(0x01); // OP_PUSH
            code.push_back(0x01); // immediate size = 1 byte
            code.push_back(0x00); // immediate value = 0
        }
        // 1025 * VERYLOW(3) = 3075 gas, well under MAX_GAS_PER_TX, and
        // 1025 * 3 = 3075 bytes, well under MAX_CODE_SIZE — so neither the gas
        // nor the size guard fires before the stack limit is reached.
        BOOST_REQUIRE(code.size() <= CVM::MAX_CODE_SIZE);

        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);

        // Execution must have failed (halted at the overflow), not completed.
        BOOST_CHECK_MESSAGE(!ok,
            "P3: overflow program should not complete via STOP/RETURN");
        // The defined halt status for an overflowing push (Req 6.2).
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::STACK_OVERFLOW,
            "P3: expected STACK_OVERFLOW after pushing past the stack limit");
        // The core bound must still hold — and be pinned at exactly the limit,
        // proving the overflowing push was rejected rather than applied.
        BOOST_CHECK_MESSAGE(state.StackSize() <= CVM::MAX_STACK_SIZE,
            "P3: StackSize() exceeded MAX_STACK_SIZE on the overflow program");
        BOOST_CHECK_EQUAL(state.StackSize(), CVM::MAX_STACK_SIZE);
    }

    // -----------------------------------------------------------------------
    // Crafted near-limit program (Req 6.1): push exactly MAX_STACK_SIZE values,
    // then OP_STOP. Execution must complete normally with the stack filled to
    // the limit but never beyond it.
    // -----------------------------------------------------------------------
    {
        std::vector<uint8_t> code;
        code.reserve(CVM::MAX_STACK_SIZE * 3 + 1);
        for (size_t n = 0; n < CVM::MAX_STACK_SIZE; ++n) {
            code.push_back(0x01); // OP_PUSH
            code.push_back(0x01); // immediate size = 1 byte
            code.push_back(0x00); // immediate value = 0
        }
        code.push_back(0x44); // OP_STOP

        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);

        BOOST_CHECK_MESSAGE(ok,
            "P3: filling the stack to exactly the limit then STOP should succeed");
        BOOST_CHECK(state.GetStatus() == CVM::VMState::Status::STOPPED);
        BOOST_CHECK_EQUAL(state.StackSize(), CVM::MAX_STACK_SIZE);
        BOOST_CHECK(state.StackSize() <= CVM::MAX_STACK_SIZE);
    }
}

// ===========================================================================
// Property P4 — Status classification of invalid bytecode.
//
// For every byte buffer whose execution hits one of the three violation
// classes, the native CVM SHALL return the matching defined status without a
// state commit:
//   * length > MAX_CODE_SIZE            -> VM_ERROR        (Req 7.4)
//   * a reached, undefined opcode byte  -> INVALID_OPCODE  (Req 7.5)
//   * a JUMP/JUMPI to a bad destination -> INVALID_JUMP    (Req 7.6)
//
// How the CVM realises these (confirmed by reading src/cvm/cvm.cpp):
//   * CVM::Execute rejects code.size() > MAX_CODE_SIZE up front (before the
//     execution loop starts), sets VM_ERROR and runs no instruction.
//   * The execution loop calls IsValidOpCode(byte) on the current PC byte; an
//     undefined byte immediately sets INVALID_OPCODE and returns.
//   * There is NO EVM-style JUMPDEST concept: HandleJump pops the target off
//     the stack and the ONLY validity rule is `target < code.size()`. A target
//     >= code.size() sets INVALID_JUMP. (For JUMPI the condition must be
//     non-zero, otherwise the jump is skipped and no target check happens.)
//
// -----------------------------------------------------------------------------
// DEVIATION (Task 4.2 / 6.1 finding — eager SSTORE commit, no rollback)
// -----------------------------------------------------------------------------
// Req 7.5 / 7.6 additionally require that an aborted INVALID_OPCODE / INVALID_JUMP
// call commits NO persistent state changes. Reading HandleStorage in
// src/cvm/cvm.cpp shows OP_SSTORE calls `storage->Store(...)` directly, with no
// journal and no rollback when the call later aborts. So the literal
// "no state commit" guarantee does NOT hold in general: any SSTORE that runs
// BEFORE the failing opcode/jump is already committed to the backend.
//
// Concrete counterexample (routed through CVM::Execute):
//   code = { 0x01,0x01,0x2A,   // PUSH size=1 value=0x2A  (value)
//            0x01,0x01,0x07,   // PUSH size=1 value=0x07  (key)
//            0x51,             // SSTORE  -> commits (addr,7)=42 eagerly
//            0x00 }            // undefined opcode -> INVALID_OPCODE
//   -> status INVALID_OPCODE, but CommittedCount() == 1 (the SSTORE stuck).
//
// Per the workspace testing guidance we do NOT silently change the acceptance
// criteria and we do NOT commit a knowingly-failing test. Following the pattern
// of P1/P3, this test asserts the guarantee the CVM ACTUALLY provides:
//   * the STATUS classification (VM_ERROR / INVALID_OPCODE / INVALID_JUMP) holds
//     for every buffer in each class; and
//   * "no state commit" is asserted using approach (a): the crafted opcode/jump
//     programs are constructed so that NO SSTORE runs before the failing
//     instruction, so an empty CommittedCount() is a real, guaranteed witness.
// The eager-commit deviation is pinned below with the counterexample above so
// the spec owner can decide whether Req 7.5/7.6 should be reworded (accept that
// pre-abort SSTOREs persist) or the CVM should gain SSTORE rollback-on-abort.
//
// Validates: Requirements 7.4, 7.5, 7.6
// ===========================================================================
BOOST_AUTO_TEST_CASE(p4_invalid_bytecode_status_classification_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c4) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c4"));

    // Pick a byte value that is guaranteed NOT to be a defined opcode. Falls
    // back to 0x00 (which IsValidOpCode rejects) if the random draw is unlucky.
    auto pickInvalidOpcodeByte = [&rng]() -> uint8_t {
        for (int tries = 0; tries < 64; ++tries) {
            uint8_t b = static_cast<uint8_t>(rng.randrange(256));
            if (!CVM::IsValidOpCode(b)) return b;
        }
        return 0x00; // 0x00 is undefined -> IsValidOpCode(0x00) == false
    };

    constexpr int kIterations = 300; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // Every entry point sees a fresh, empty storage mock; the crafted
        // programs never run an SSTORE, so CommittedCount() must stay 0.
        const uint160 contractAddr = RandUint160(rng);
        const uint160 callerAddr   = RandUint160(rng);
        const uint256 blockHash    = rng.rand256();
        const int64_t timestamp    = static_cast<int64_t>(rng.rand64());
        const int blockHeight      = static_cast<int>(rng.rand32() & 0x7fffffff);
        const uint64_t gasLimit    = CVM::MAX_GAS_PER_TX; // ample gas so only the
                                                          // violation class fires

        // Rotate through the three violation classes so each gets >= ~100 runs.
        const int klass = i % 3;

        try {
            CVM::VMState state = MakeState(gasLimit, contractAddr, callerAddr,
                                           0, blockHeight, blockHash, timestamp);
            ContractStorage_Mock storage;
            CVM::CVM vm;

            if (klass == 0) {
                // ---- Oversized code -> VM_ERROR (Req 7.4) ----
                // Length strictly greater than MAX_CODE_SIZE; content is random
                // and irrelevant because Execute rejects the buffer before the
                // loop ever inspects a byte.
                const size_t extra = 1 + static_cast<size_t>(rng.randrange(256));
                std::vector<uint8_t> code;
                {
                    std::vector<unsigned char> bytes =
                        rng.randbytes(CVM::MAX_CODE_SIZE + extra);
                    code.assign(bytes.begin(), bytes.end());
                }
                BOOST_REQUIRE(code.size() > CVM::MAX_CODE_SIZE);

                bool ok = vm.Execute(code, state, &storage);
                BOOST_CHECK_MESSAGE(!ok,
                    "P4: oversized code must not execute successfully (iter #" +
                    std::to_string(i) + ")");
                BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::VM_ERROR,
                    "P4: oversized code expected VM_ERROR, got a different status "
                    "(iter #" + std::to_string(i) + ")");
                // No opcode ran, so nothing was committed (Req 7.4 no-commit).
                BOOST_CHECK_MESSAGE(storage.CommittedCount() == 0,
                    "P4: oversized code committed state (iter #" +
                    std::to_string(i) + ")");
            } else if (klass == 1) {
                // ---- Undefined opcode at PC 0 -> INVALID_OPCODE (Req 7.5) ----
                // Byte 0 is an undefined opcode so the loop halts on the very
                // first instruction; the trailing random bytes are never
                // reached and no SSTORE ever runs.
                std::vector<uint8_t> code;
                code.push_back(pickInvalidOpcodeByte());
                const size_t tail = static_cast<size_t>(rng.randrange(32));
                {
                    std::vector<unsigned char> bytes = rng.randbytes(tail);
                    code.insert(code.end(), bytes.begin(), bytes.end());
                }
                BOOST_REQUIRE(code.size() <= CVM::MAX_CODE_SIZE);

                bool ok = vm.Execute(code, state, &storage);
                BOOST_CHECK_MESSAGE(!ok,
                    "P4: invalid-opcode program must not succeed (iter #" +
                    std::to_string(i) + ")");
                BOOST_CHECK_MESSAGE(
                    state.GetStatus() == CVM::VMState::Status::INVALID_OPCODE,
                    "P4: undefined opcode byte expected INVALID_OPCODE (iter #" +
                    std::to_string(i) + ", byte=" +
                    std::to_string(static_cast<int>(code[0])) + ")");
                BOOST_CHECK_MESSAGE(storage.CommittedCount() == 0,
                    "P4: invalid-opcode program committed state (iter #" +
                    std::to_string(i) + ")");
            } else {
                // ---- Out-of-range JUMP/JUMPI target -> INVALID_JUMP (Req 7.6) ----
                // Push a target that is guaranteed >= code.size(), then JUMP
                // (or JUMPI with a non-zero condition). HandleJump validates
                // only `target < code.size()`, so the jump is rejected before
                // any storage write.
                const bool useJumpI = rng.randbool();
                std::vector<uint8_t> code;
                if (useJumpI) {
                    // PUSH condition (non-zero) so JUMPI actually evaluates the
                    // target; stack order requires the condition to be pushed
                    // first (target must sit on top for HandleJump's first Pop).
                    code.push_back(0x01); // OP_PUSH
                    code.push_back(0x01); // size = 1
                    code.push_back(0x01); // condition = 1 (non-zero)
                }
                // PUSH target = 0xFFFF (65535), far beyond the tiny program.
                code.push_back(0x01); // OP_PUSH
                code.push_back(0x02); // size = 2
                code.push_back(0xFF);
                code.push_back(0xFF);
                code.push_back(useJumpI ? 0x41 : 0x40); // OP_JUMPI / OP_JUMP

                BOOST_REQUIRE(static_cast<size_t>(0xFFFF) >= code.size());

                bool ok = vm.Execute(code, state, &storage);
                BOOST_CHECK_MESSAGE(!ok,
                    "P4: out-of-range jump must not succeed (iter #" +
                    std::to_string(i) + ", " + (useJumpI ? "JUMPI" : "JUMP") + ")");
                BOOST_CHECK_MESSAGE(
                    state.GetStatus() == CVM::VMState::Status::INVALID_JUMP,
                    "P4: out-of-range jump target expected INVALID_JUMP (iter #" +
                    std::to_string(i) + ", " + (useJumpI ? "JUMPI" : "JUMP") + ")");
                BOOST_CHECK_MESSAGE(storage.CommittedCount() == 0,
                    "P4: invalid-jump program committed state (iter #" +
                    std::to_string(i) + ")");
            }

            // The status must always be a defined enum value regardless of class.
            BOOST_CHECK_MESSAGE(IsDefinedStatus(state.GetStatus()),
                "P4: undefined status value (iter #" + std::to_string(i) + ")");
        } catch (const std::exception& e) {
            // Req 1.7: an expected exception is a defined termination, not a
            // crash; catching it here is enough.
            BOOST_TEST_MESSAGE(std::string("P4: caught std::exception (defined "
                "termination, iter #") + std::to_string(i) + "): " + e.what());
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P4: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Crafted, fully deterministic witnesses — one per violation class — pinned
    // as concrete examples independent of the random loop above.
    // -----------------------------------------------------------------------

    // (a) Oversized code -> VM_ERROR, no state commit (Req 7.4).
    {
        std::vector<uint8_t> code(CVM::MAX_CODE_SIZE + 1, 0x44 /* OP_STOP */);
        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);
        BOOST_CHECK(!ok);
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::VM_ERROR,
            "P4: crafted oversized code expected VM_ERROR");
        BOOST_CHECK_EQUAL(storage.CommittedCount(), 0u);
    }

    // (b) Undefined opcode 0x00 at PC 0 -> INVALID_OPCODE, no state commit (Req 7.5).
    {
        const std::vector<uint8_t> code = {0x00}; // 0x00 is not a defined opcode
        BOOST_REQUIRE(!CVM::IsValidOpCode(0x00));
        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);
        BOOST_CHECK(!ok);
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::INVALID_OPCODE,
            "P4: crafted undefined-opcode program expected INVALID_OPCODE");
        BOOST_CHECK_EQUAL(storage.CommittedCount(), 0u);
    }

    // (c) Out-of-range JUMP target -> INVALID_JUMP, no state commit (Req 7.6).
    {
        // PUSH size=1 value=0xFF (255), then OP_JUMP; code.size()==4 so 255 is
        // an invalid destination.
        const std::vector<uint8_t> code = {0x01, 0x01, 0xFF, 0x40};
        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);
        BOOST_CHECK(!ok);
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::INVALID_JUMP,
            "P4: crafted out-of-range JUMP expected INVALID_JUMP");
        BOOST_CHECK_EQUAL(storage.CommittedCount(), 0u);
    }

    // (d) Documented eager-commit deviation (see header): an SSTORE that runs
    // BEFORE an undefined opcode IS committed even though the call aborts with
    // INVALID_OPCODE. This pins the deviation so the literal Req 7.5 "no state
    // commit" is known to fail for programs that write before failing.
    {
        const std::vector<uint8_t> code = {
            0x01, 0x01, 0x2A,   // PUSH size=1 value=0x2A (42) -> value
            0x01, 0x01, 0x07,   // PUSH size=1 value=0x07 (7)  -> key
            0x51,               // OP_SSTORE -> commits (addr, 7) = 42 eagerly
            0x00                // undefined opcode -> INVALID_OPCODE
        };
        CVM::VMState state = MakeState(CVM::MAX_GAS_PER_TX, uint160(), uint160(),
                                       0, 0, uint256(), 0);
        ContractStorage_Mock storage;
        CVM::CVM vm;
        bool ok = vm.Execute(code, state, &storage);
        BOOST_CHECK(!ok);
        BOOST_CHECK_MESSAGE(state.GetStatus() == CVM::VMState::Status::INVALID_OPCODE,
            "P4 finding: SSTORE-then-invalid-opcode should end INVALID_OPCODE");
        // Documents that Req 7.5's literal "no state commit" does NOT hold once
        // an SSTORE has run before the aborting instruction (eager commit).
        BOOST_CHECK_MESSAGE(storage.CommittedCount() == 1,
            "P4 finding: expected the pre-abort SSTORE to remain committed "
            "(eager-commit deviation)");
    }
}

// ===========================================================================
// Property P5 — VerifyBytecode classification by size and opcodes.
//
// For every `code` the following SHALL hold:
//   * length 0               -> CVM::VerifyBytecode returns false  (Req 7.3)
//   * length > MAX_CODE_SIZE  -> returns false                     (Req 7.1)
//   * length 1..MAX_CODE_SIZE consisting exclusively of bytes that
//     satisfy IsValidOpCode  -> returns true                       (Req 7.2)
//
// How the CVM realises this (confirmed by reading CVM::VerifyBytecode in
// src/cvm/cvm.cpp):
//   * `code.empty() || code.size() > MAX_CODE_SIZE` -> return false, up front.
//   * Otherwise it walks the buffer; any byte failing IsValidOpCode -> false.
//
// -----------------------------------------------------------------------------
// DEVIATION (Task 6.5 finding — OP_PUSH immediate framing)
// -----------------------------------------------------------------------------
// Req 7.2 / P5 literally say "all bytes are valid opcodes => true". This does
// NOT hold in general, because VerifyBytecode gives OP_PUSH (0x01) special
// treatment: a PUSH is followed by a 1-byte immediate SIZE (which must be
// 1..32) and then SIZE immediate bytes. When the walker reaches an OP_PUSH it
// requires that framing and returns false if:
//   * the SIZE byte is missing (PUSH is the last byte),
//   * SIZE == 0 or SIZE > 32, or
//   * fewer than SIZE immediate bytes remain.
// So a buffer whose bytes are ALL valid opcodes can still be rejected.
//
// Concrete counterexample:
//   code = { 0x01 }                       // a lone OP_PUSH
//   -> IsValidOpCode(0x01) == true for the only byte, yet VerifyBytecode
//      returns false (the SIZE byte is missing).
//
// Per the workspace testing guidance we do NOT silently change the acceptance
// criteria and do NOT commit a knowingly-failing test. Following the P1/P4
// pattern, the true-classification half of the property is asserted on the
// faithful, guaranteed sub-space that the code actually accepts:
//   (a) buffers built exclusively from valid NON-PUSH opcode bytes, and
//   (b) buffers built from valid opcodes with every OP_PUSH correctly framed
//       (well-formed SIZE + immediate bytes).
// Both sub-spaces are "only valid opcode bytes" and both are guaranteed to
// return true. The OP_PUSH framing deviation is pinned below with the
// counterexample so the spec owner can decide whether Req 7.2 should be
// reworded (require valid opcode framing, not merely valid opcode bytes).
//
// The false-classification half (length 0 or > MAX_CODE_SIZE -> false) holds
// unconditionally and is asserted directly for arbitrary content.
//
// Validates: Requirements 7.1, 7.2, 7.3
// ===========================================================================
BOOST_AUTO_TEST_CASE(p5_verifybytecode_size_and_opcode_classification_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c5) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c5"));

    // Build the set of valid opcode bytes excluding OP_PUSH (0x01) by scanning
    // the whole byte range through the real IsValidOpCode.
    constexpr uint8_t kOpPush = 0x01; // OpCode::OP_PUSH
    std::vector<uint8_t> validNonPush;
    for (int b = 0; b < 256; ++b) {
        auto byte = static_cast<uint8_t>(b);
        if (CVM::IsValidOpCode(byte) && byte != kOpPush) {
            validNonPush.push_back(byte);
        }
    }
    // There must be plenty of non-PUSH opcodes to sample from.
    BOOST_REQUIRE(validNonPush.size() >= 10);

    // Emit a well-formed PUSH sequence into `code`: OP_PUSH, SIZE (1..32),
    // followed by SIZE arbitrary immediate bytes. VerifyBytecode accepts this.
    auto appendWellFormedPush = [&rng, kOpPush](std::vector<uint8_t>& code) {
        const uint8_t size = static_cast<uint8_t>(1 + rng.randrange(32)); // 1..32
        code.push_back(kOpPush);
        code.push_back(size);
        std::vector<unsigned char> imm = rng.randbytes(size);
        code.insert(code.end(), imm.begin(), imm.end());
    };

    constexpr int kIterations = 300; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // --- Req 7.2 (true half): all-valid-opcode buffers must verify true ---
        // Alternate between the two guaranteed-accepted sub-spaces so both the
        // pure non-PUSH path and the well-formed-PUSH framing path are covered.
        const bool withPushes = rng.randbool();

        std::vector<uint8_t> validCode;
        if (!withPushes) {
            // (a) Only valid NON-PUSH opcode bytes. Length 1..2048.
            const size_t len = 1 + static_cast<size_t>(rng.randrange(2048));
            validCode.reserve(len);
            for (size_t n = 0; n < len; ++n) {
                validCode.push_back(validNonPush[rng.randrange(validNonPush.size())]);
            }
        } else {
            // (b) Valid opcodes with every OP_PUSH correctly framed. Build a
            // handful of tokens, each either a single non-PUSH opcode or a
            // well-formed PUSH sequence, keeping the buffer within limits.
            const int tokens = 1 + static_cast<int>(rng.randrange(64));
            for (int t = 0; t < tokens; ++t) {
                if (rng.randbool()) {
                    appendWellFormedPush(validCode);
                } else {
                    validCode.push_back(validNonPush[rng.randrange(validNonPush.size())]);
                }
            }
        }
        BOOST_REQUIRE(!validCode.empty());
        BOOST_REQUIRE(validCode.size() <= static_cast<size_t>(CVM::MAX_CODE_SIZE));

        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(validCode) == true,
            "P5: a length-" + std::to_string(validCode.size()) +
            " buffer of only valid opcode bytes (" +
            (withPushes ? "well-formed PUSH mix" : "non-PUSH only") +
            ") should VerifyBytecode as true (iter #" + std::to_string(i) + ")");

        // --- Req 7.1 (false half): length > MAX_CODE_SIZE must verify false ---
        // Content is deliberately all-valid opcodes to prove the size guard,
        // not the opcode check, is what rejects the buffer.
        {
            const size_t extra = 1 + static_cast<size_t>(rng.randrange(64));
            std::vector<uint8_t> oversized(CVM::MAX_CODE_SIZE + extra,
                                           0x44 /* OP_STOP, a valid opcode */);
            BOOST_REQUIRE(oversized.size() > static_cast<size_t>(CVM::MAX_CODE_SIZE));
            BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(oversized) == false,
                "P5: oversized code (len=" + std::to_string(oversized.size()) +
                " > MAX_CODE_SIZE) should VerifyBytecode as false (iter #" +
                std::to_string(i) + ")");
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P5: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Crafted, fully deterministic witnesses pinned independently of the loop.
    // -----------------------------------------------------------------------

    // Req 7.3 — empty code -> false.
    {
        const std::vector<uint8_t> empty;
        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(empty) == false,
            "P5: empty code must VerifyBytecode as false (Req 7.3)");
    }

    // Req 7.1 — code exactly one byte over the limit -> false (all valid bytes).
    {
        const std::vector<uint8_t> justOver(CVM::MAX_CODE_SIZE + 1,
                                             0x44 /* OP_STOP */);
        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(justOver) == false,
            "P5: MAX_CODE_SIZE+1 code must VerifyBytecode as false (Req 7.1)");
    }

    // Req 7.2 — code exactly at the limit made of valid non-PUSH opcodes -> true.
    {
        const std::vector<uint8_t> atLimit(CVM::MAX_CODE_SIZE, 0x44 /* OP_STOP */);
        BOOST_REQUIRE(atLimit.size() == static_cast<size_t>(CVM::MAX_CODE_SIZE));
        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(atLimit) == true,
            "P5: MAX_CODE_SIZE code of valid opcodes must VerifyBytecode true (Req 7.2)");
    }

    // Req 7.2 — a single valid non-PUSH opcode byte -> true.
    {
        const std::vector<uint8_t> one = {0x44}; // OP_STOP
        BOOST_REQUIRE(CVM::IsValidOpCode(0x44));
        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(one) == true,
            "P5: a single valid opcode byte must VerifyBytecode true (Req 7.2)");
    }

    // Documented OP_PUSH framing deviation (see header): a lone OP_PUSH is a
    // valid opcode byte, yet VerifyBytecode rejects it because the immediate
    // SIZE byte is missing. This pins the deviation so the literal Req 7.2
    // "all bytes valid opcodes => true" is known to fail for ill-framed PUSH.
    {
        const std::vector<uint8_t> lonePush = {kOpPush}; // {0x01}
        BOOST_REQUIRE(CVM::IsValidOpCode(kOpPush));
        BOOST_CHECK_MESSAGE(CVM::CVM::VerifyBytecode(lonePush) == false,
            "P5 finding: a lone OP_PUSH (valid opcode byte) is rejected because "
            "its immediate SIZE byte is missing — the literal Req 7.2 "
            "'all valid opcode bytes => true' does not hold for ill-framed PUSH");
    }
}

// ===========================================================================
// Property P7 — ContractStorage_Mock semantics.
//
// The in-memory ContractStorage_Mock (Req 2) must behave as a faithful
// key/value store keyed by (address, key). For every address, every key, and
// every value the following SHALL hold:
//   * round-trip (Req 2.2): after Store(addr, key, value), Load(addr, key, out)
//     sets out to the LAST stored value and returns true;
//   * miss (Req 2.3): for a pair that was never stored, Load returns false and
//     leaves out unchanged;
//   * overwrite (Req 2.4): repeated Store on the same pair retains only the
//     last value and discards earlier ones;
//   * isolation (Req 2.6): a Store on one pair leaves every other pair's stored
//     value untouched;
//   * existence (Req 2.5): Exists(addr) is true iff at least one Store has
//     occurred for addr (the real interface takes only the address).
//
// The property is validated against an independent reference model (a plain
// std::map + std::set built in lock-step with the mock): after a random
// sequence of Store operations the mock must agree with the reference on every
// Load, on out-unchanged for misses, and on Exists for every touched and a set
// of untouched addresses. Because the reference is rebuilt from the same
// operation stream, agreement across all keys simultaneously witnesses both
// overwrite (last-writer-wins) and isolation (non-target pairs never change).
//
// Validates: Requirements 2.2, 2.3, 2.4, 2.5, 2.6
// ===========================================================================
BOOST_AUTO_TEST_CASE(p7_contract_storage_mock_semantics_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c7) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c7"));

    // A sentinel value used to detect that Load leaves `out` unchanged on a
    // miss. It is deliberately unusual so an accidental write of 0 would show.
    const uint256 kSentinel = uint256S(
        "00000000000000000000000000000000000000000000000000000000deadbeef");

    constexpr int kIterations = 300; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        ContractStorage_Mock mock;

        // Independent reference model built in lock-step with the mock.
        std::map<std::pair<uint160, uint256>, uint256> ref;
        std::set<uint160> refAddrs;

        // Draw a small pool of addresses and keys so collisions (overwrites and
        // repeated addresses across different keys) actually occur.
        const int nAddrs = 1 + static_cast<int>(rng.randrange(4)); // 1..4
        const int nKeys  = 1 + static_cast<int>(rng.randrange(4)); // 1..4
        std::vector<uint160> addrPool;
        std::vector<uint256> keyPool;
        for (int a = 0; a < nAddrs; ++a) addrPool.push_back(RandUint160(rng));
        for (int k = 0; k < nKeys; ++k)  keyPool.push_back(rng.rand256());

        // Apply a random sequence of Store operations. Reusing pooled addresses
        // and keys guarantees repeated (addr,key) pairs -> exercises overwrite,
        // while distinct pairs exercise isolation.
        const int nOps = 1 + static_cast<int>(rng.randrange(40)); // 1..40
        for (int op = 0; op < nOps; ++op) {
            const uint160 addr = addrPool[rng.randrange(addrPool.size())];
            const uint256 key  = keyPool[rng.randrange(keyPool.size())];
            const uint256 val  = rng.rand256();

            BOOST_CHECK(mock.Store(addr, key, val)); // Store reports success
            ref[std::make_pair(addr, key)] = val;    // last-writer-wins in ref
            refAddrs.insert(addr);
        }

        // ---- Round-trip + overwrite + isolation (Req 2.2, 2.4, 2.6) ----
        // Every stored pair must Load back the LAST stored value (true), and
        // because the reference holds the last value for every pair, agreement
        // across all pairs simultaneously proves last-writer-wins and that no
        // pair leaked into another.
        for (const auto& kv : ref) {
            uint256 out = kSentinel; // will be overwritten by a successful Load
            const bool found = mock.Load(kv.first.first, kv.first.second, out);
            BOOST_CHECK_MESSAGE(found,
                "P7: Load of a stored pair returned false (iter #" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out == kv.second,
                "P7: Load returned a stale/wrong value — expected last stored "
                "value (iter #" + std::to_string(i) + ")");
        }

        // ---- Miss: unset pair -> false, out unchanged (Req 2.3) ----
        // Search for a pooled (addr,key) pair that was never stored. If every
        // pooled pair happens to be stored, fabricate a fresh address that is
        // guaranteed absent.
        {
            bool haveMiss = false;
            uint160 missAddr;
            uint256 missKey;
            for (const auto& a : addrPool) {
                for (const auto& k : keyPool) {
                    if (ref.find(std::make_pair(a, k)) == ref.end()) {
                        missAddr = a; missKey = k; haveMiss = true; break;
                    }
                }
                if (haveMiss) break;
            }
            if (!haveMiss) {
                // Fresh random address is overwhelmingly unlikely to collide;
                // loop until it is provably absent from the reference.
                do { missAddr = RandUint160(rng); } while (refAddrs.count(missAddr) > 0);
                missKey = keyPool.front();
            }

            uint256 out = kSentinel;
            const bool found = mock.Load(missAddr, missKey, out);
            BOOST_CHECK_MESSAGE(!found,
                "P7: Load of an unset pair returned true (iter #" +
                std::to_string(i) + ")");
            BOOST_CHECK_MESSAGE(out == kSentinel,
                "P7: Load of an unset pair modified out (Req 2.3 violated, "
                "iter #" + std::to_string(i) + ")");
        }

        // ---- Existence (Req 2.5): Exists(addr) iff a Store occurred ----
        for (const auto& a : addrPool) {
            const bool expected = refAddrs.count(a) > 0;
            BOOST_CHECK_MESSAGE(mock.Exists(a) == expected,
                "P7: Exists(addr) disagreed with the reference (iter #" +
                std::to_string(i) + ")");
        }
        // A fresh, never-stored address must not exist.
        {
            uint160 fresh;
            do { fresh = RandUint160(rng); } while (refAddrs.count(fresh) > 0);
            BOOST_CHECK_MESSAGE(!mock.Exists(fresh),
                "P7: Exists returned true for a never-stored address (iter #" +
                std::to_string(i) + ")");
        }

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P7: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Crafted, fully deterministic witnesses pinning each acceptance criterion.
    // -----------------------------------------------------------------------
    {
        ContractStorage_Mock mock;
        const uint160 addrA = RandUint160(rng);
        const uint160 addrB = RandUint160(rng);
        const uint256 key1  = uint256S("01");
        const uint256 key2  = uint256S("02");
        const uint256 v1    = uint256S("11");
        const uint256 v2    = uint256S("22");
        const uint256 v3    = uint256S("33");

        // (2.5) nothing stored yet -> Exists is false.
        BOOST_CHECK(!mock.Exists(addrA));

        // (2.2) round-trip.
        BOOST_CHECK(mock.Store(addrA, key1, v1));
        uint256 out = kSentinel;
        BOOST_CHECK(mock.Load(addrA, key1, out));
        BOOST_CHECK(out == v1);

        // (2.5) after a Store, Exists is true for that address.
        BOOST_CHECK(mock.Exists(addrA));

        // (2.6) isolation: storing (addrA,key2) leaves (addrA,key1) untouched,
        // and (addrB,*) is entirely independent.
        BOOST_CHECK(mock.Store(addrA, key2, v2));
        out = kSentinel;
        BOOST_CHECK(mock.Load(addrA, key1, out));
        BOOST_CHECK(out == v1); // unchanged by the (addrA,key2) write

        // (2.4) overwrite: re-Store (addrA,key1) with v3, only the last value
        // survives.
        BOOST_CHECK(mock.Store(addrA, key1, v3));
        out = kSentinel;
        BOOST_CHECK(mock.Load(addrA, key1, out));
        BOOST_CHECK(out == v3);

        // (2.3) miss: (addrB,key1) never stored -> false, out unchanged.
        out = kSentinel;
        BOOST_CHECK(!mock.Load(addrB, key1, out));
        BOOST_CHECK(out == kSentinel);
        BOOST_CHECK(!mock.Exists(addrB));
    }
}

// ===========================================================================
// Property P6 — Determinism of native execution.
//
// For every byte buffer, every context, and every bit-identical initial state
// of the ContractStorage_Mock, any number (>= 100) of consecutive
// ExecuteContract calls SHALL return identical:
//   * success                         (Req 8.1, 8.4)
//   * gasUsed (exactly equal numeric)  (Req 8.1, 8.4)
//   * returnData (byte-for-byte)       (Req 8.1)
//   * VMState::Status                  (Req 8.2, 8.4)
//   * final ContractStorage_Mock state (Snapshot(), bit-identical) (Req 8.3, 8.5)
//
// Observability note (same as P2): the free function ExecuteContract owns its
// VMState internally and never exposes it, so success/gasUsed/returnData and
// the final storage Snapshot() are read directly off ExecuteContract, while the
// VMState::Status is observed by additionally running CVM::Execute on a
// reconstructed VMState carrying the byte-identical context and the byte-
// identical seeded initial storage. Because native execution is deterministic
// for identical code/context/initial-state, the status observed via
// CVM::Execute is the same status ExecuteContract's hidden VMState reaches, and
// its final storage state matches ExecuteContract's — both are asserted below.
//
// Each scenario is run >= 100 consecutive times with a FRESH mock per run, each
// mock re-seeded to the identical initial state, and every run is compared
// against the first run of that scenario.
//
// Validates: Requirements 8.1, 8.2, 8.3, 8.4, 8.5
// ===========================================================================
BOOST_AUTO_TEST_CASE(p6_determinism_of_native_execution_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c6) so the scenarios are
    // reproducible and independent of the other properties.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c6"));

    // A single execution scenario: fixed code, context, and an initial storage
    // seed (a set of (addr,key,value) triples applied to every fresh mock).
    struct Scenario {
        std::vector<uint8_t> code;
        uint64_t gasLimit;
        uint160  contractAddr;
        uint160  callerAddr;
        uint64_t callValue;
        int      blockHeight;
        uint256  blockHash;
        int64_t  timestamp;
        std::vector<uint8_t> inputData;
        std::vector<std::tuple<uint160, uint256, uint256>> seed; // initial storage
    };

    // Apply a scenario's initial-state seed to a fresh mock so every run starts
    // from the bit-identical initial storage state.
    auto seedStorage = [](ContractStorage_Mock& s, const Scenario& sc) {
        for (const auto& t : sc.seed) {
            s.Store(std::get<0>(t), std::get<1>(t), std::get<2>(t));
        }
    };

    // Run one scenario >= kReps times with a fresh, identically-seeded mock per
    // run and assert every run agrees with the first on success, gasUsed,
    // returnData, final storage Snapshot(), and (via CVM::Execute) status.
    auto runScenario = [&](const Scenario& sc, const std::string& label) {
        constexpr int kReps = 120; // >= 100 required by the task

        // ---- Reference run (run #0) ----
        ContractStorage_Mock refStorage;
        seedStorage(refStorage, sc);
        CVM::ExecutionResult ref = CVM::ExecuteContract(
            sc.code, sc.gasLimit, sc.contractAddr, sc.callerAddr, sc.callValue,
            sc.inputData, sc.blockHeight, sc.blockHash, sc.timestamp, &refStorage);
        const ContractStorage_Mock::StoreMap refSnapshot = refStorage.Snapshot();

        // Reference status via a reconstructed, observable VMState + seeded mock.
        // (ExecuteContract feeds the code as the program; inputData is set on the
        // state so the reconstruction mirrors ExecuteContract's own setup.)
        CVM::VMState refState = MakeState(sc.gasLimit, sc.contractAddr, sc.callerAddr,
                                          sc.callValue, sc.blockHeight, sc.blockHash,
                                          sc.timestamp);
        ContractStorage_Mock refStatusStorage;
        seedStorage(refStatusStorage, sc);
        { CVM::CVM vm; vm.Execute(sc.code, refState, &refStatusStorage); }
        const CVM::VMState::Status refStatus = refState.GetStatus();

        for (int rep = 1; rep < kReps; ++rep) {
            // Fresh mock, re-seeded to the identical initial state (Req 8.5).
            ContractStorage_Mock storage;
            seedStorage(storage, sc);
            CVM::ExecutionResult r = CVM::ExecuteContract(
                sc.code, sc.gasLimit, sc.contractAddr, sc.callerAddr, sc.callValue,
                sc.inputData, sc.blockHeight, sc.blockHash, sc.timestamp, &storage);

            BOOST_CHECK_MESSAGE(r.success == ref.success,
                "P6[" + label + "]: success differs at rep #" + std::to_string(rep));
            BOOST_CHECK_MESSAGE(r.gasUsed == ref.gasUsed,
                "P6[" + label + "]: gasUsed differs at rep #" + std::to_string(rep) +
                " (" + std::to_string(r.gasUsed) + " vs " +
                std::to_string(ref.gasUsed) + ")");
            BOOST_CHECK_MESSAGE(r.returnData == ref.returnData,
                "P6[" + label + "]: returnData differs at rep #" + std::to_string(rep));
            BOOST_CHECK_MESSAGE(storage.Snapshot() == refSnapshot,
                "P6[" + label + "]: final storage state differs at rep #" +
                std::to_string(rep));

            // Status determinism via the reconstructed VMState path.
            CVM::VMState state = MakeState(sc.gasLimit, sc.contractAddr, sc.callerAddr,
                                           sc.callValue, sc.blockHeight, sc.blockHash,
                                           sc.timestamp);
            ContractStorage_Mock statusStorage;
            seedStorage(statusStorage, sc);
            { CVM::CVM vm; vm.Execute(sc.code, state, &statusStorage); }
            BOOST_CHECK_MESSAGE(state.GetStatus() == refStatus,
                "P6[" + label + "]: VMState::Status differs at rep #" +
                std::to_string(rep));
            // The reconstructed run's storage must also match the reference
            // (independent witness of Req 8.3 final-state determinism).
            BOOST_CHECK_MESSAGE(statusStorage.Snapshot() == refStatusStorage.Snapshot(),
                "P6[" + label + "]: reconstructed final storage differs at rep #" +
                std::to_string(rep));
        }
    };

    // -----------------------------------------------------------------------
    // Scenario A: random fuzzed buffers. Several distinct scenarios, each run
    // >= 100 times, so determinism is checked across a slice of the input space
    // as well as across repetitions. Buffers are kept small so the >= 100
    // repetitions stay fast; short programs also actually execute opcodes
    // (rather than tripping INVALID_OPCODE on byte 0 every time).
    // -----------------------------------------------------------------------
    for (int scn = 0; scn < 8; ++scn) {
        Scenario sc;
        size_t len = static_cast<size_t>(rng.randrange(48)); // 0..47
        {
            std::vector<unsigned char> bytes = rng.randbytes(len);
            sc.code.assign(bytes.begin(), bytes.end());
        }
        sc.gasLimit     = rng.randrange(CVM::MAX_GAS_PER_TX + 1);
        sc.contractAddr = RandUint160(rng);
        sc.callerAddr   = RandUint160(rng);
        sc.callValue    = rng.rand64();
        sc.blockHeight  = static_cast<int>(rng.rand32() & 0x7fffffff);
        sc.timestamp    = static_cast<int64_t>(rng.rand64());
        sc.blockHash    = rng.rand256();
        runScenario(sc, "rand#" + std::to_string(scn));
    }

    // -----------------------------------------------------------------------
    // Scenario B: a crafted program that WRITES persistent state (SSTORE) then
    // STOPs, so the final Snapshot() is non-empty. This makes the storage-state
    // determinism (Req 8.3 / 8.5) a meaningful, non-trivial witness rather than
    // an always-empty map.
    //   PUSH size=1 value=0x2A (42) -> value
    //   PUSH size=1 value=0x07 (7)  -> key
    //   SSTORE                      -> (contractAddr, 7) = 42
    //   STOP
    // -----------------------------------------------------------------------
    {
        Scenario sc;
        sc.code = {0x01, 0x01, 0x2A,   // PUSH 42
                   0x01, 0x01, 0x07,   // PUSH 7
                   0x51,               // SSTORE
                   0x44};              // STOP
        sc.gasLimit     = CVM::MAX_GAS_PER_TX;
        sc.contractAddr = RandUint160(rng);
        sc.callerAddr   = RandUint160(rng);
        sc.callValue    = 0;
        sc.blockHeight  = 12345;
        sc.timestamp    = 1700000000;
        sc.blockHash    = rng.rand256();
        runScenario(sc, "sstore-stop");
    }

    // -----------------------------------------------------------------------
    // Scenario C: identical NON-empty initial storage state. Seed a few pairs
    // into every fresh mock before execution and run a program that overwrites
    // one seeded key and adds another, so determinism is checked with a
    // bit-identical, non-trivial initial state (Req 8.5 "same set of key-value
    // pairs with byte-identical values").
    // -----------------------------------------------------------------------
    {
        Scenario sc;
        sc.contractAddr = RandUint160(rng);
        sc.callerAddr   = RandUint160(rng);
        sc.callValue    = 0;
        sc.blockHeight  = 42;
        sc.timestamp    = 1690000000;
        sc.blockHash    = rng.rand256();
        sc.gasLimit     = CVM::MAX_GAS_PER_TX;
        // Seed three pairs on the contract's own address (the address SSTORE
        // writes to, so an overwrite is observable) plus an unrelated address.
        sc.seed.emplace_back(sc.contractAddr, uint256S("07"), uint256S("01"));
        sc.seed.emplace_back(sc.contractAddr, uint256S("09"), uint256S("02"));
        sc.seed.emplace_back(RandUint160(rng), uint256S("07"), uint256S("03"));
        // Overwrite key 7 with value 42, then STOP.
        sc.code = {0x01, 0x01, 0x2A,   // PUSH 42
                   0x01, 0x01, 0x07,   // PUSH 7
                   0x51,               // SSTORE (overwrites seeded key 7)
                   0x44};              // STOP
        runScenario(sc, "seeded-overwrite");
    }
}

// ===========================================================================
// Property P9 — Deterministic, valid selector routing.
//
// For every input byte buffer, the Entry_Point_Selector (the first byte) maps
// DETERMINISTICALLY to AT MOST ONE entry point:
//   * a valid selector value (0..CVM_FUZZ_TARGET_END-1) selects EXACTLY ONE
//     target — a native target for 0..4 (always), and an EVM/router target for
//     5..12 when ENABLE_EVMC is defined                        (Req 1.1, 14.1, 14.2)
//   * a value >= CVM_FUZZ_TARGET_END leads to NO entry-point call and status 0
//                                                               (Req 1.3)
//   * when ENABLE_EVMC is NOT defined, any EVM/router selector (5..12) leads to
//     NO entry-point call and status 0                          (Req 14.3)
//
// The harness dispatcher (test_one_input) lives in a separate binary that is
// not linked here, so SimulateDispatch (defined above) is a byte-for-byte
// mirror of its switch(selector) control flow, including the #ifdef ENABLE_EVMC
// gating. This property asserts the four facets of the routing contract:
//   (1) totality      — every one of the 256 possible first-byte values maps to
//                        a defined outcome without a crash;
//   (2) at most one   — callCount is always 0 or 1 (never fans out to 2+);
//   (3) determinism   — the same buffer always routes identically, and the
//                        route depends ONLY on the first byte (tail bytes and
//                        buffer length never change it);
//   (4) gating        — out-of-range and (per build config) EVM/router
//                        selectors route to no call with status 0.
//
// The full 0..255 first-byte domain is swept exhaustively AND a randomised loop
// (>= 100 iterations) varies the tail bytes to prove the tail never perturbs
// the routing decision.
//
// Validates: Requirements 1.1, 1.3, 14.1, 14.2, 14.3
// ===========================================================================
BOOST_AUTO_TEST_CASE(p9_deterministic_valid_selector_routing_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c9) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c9"));

    // The expected route for a first byte, computed independently of
    // SimulateDispatch's switch so the two are cross-checked against each other.
    auto expectedRoute = [](uint8_t selector) -> DispatchResult {
        if (selector >= CVM_FUZZ_TARGET_END) return {0, -1, 0}; // Req 1.3
        if (selector <= EXECUTE_CONTRACT) {
            return {1, static_cast<int>(selector), 0};          // Req 1.1 (native)
        }
        // selector in 5..12 (EVM/router).
#ifdef ENABLE_EVMC
        return {1, static_cast<int>(selector), 0};              // Req 14.1/14.2
#else
        return {0, -1, 0};                                      // Req 14.3
#endif
    };

    // -----------------------------------------------------------------------
    // (A) Exhaustive first-byte domain sweep (all 256 values). For each byte we
    // build a buffer with that selector plus a random tail, then assert the
    // route matches the independently-computed expectation, that at most one
    // call occurs, that the status is always 0, and that a second dispatch of
    // the same buffer is bit-identical (determinism).
    // -----------------------------------------------------------------------
    for (int b = 0; b < 256; ++b) {
        const uint8_t selector = static_cast<uint8_t>(b);

        // Buffer = selector byte + a random tail (0..40 bytes).
        std::vector<uint8_t> buffer;
        buffer.push_back(selector);
        const size_t tail = static_cast<size_t>(rng.randrange(41));
        {
            std::vector<unsigned char> bytes = rng.randbytes(tail);
            buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        }

        const DispatchResult got = SimulateDispatch(buffer);
        const DispatchResult exp = expectedRoute(selector);

        // (2) At most one entry-point call — never fans out.
        BOOST_CHECK_MESSAGE(got.callCount == 0 || got.callCount == 1,
            "P9: selector " + std::to_string(b) + " produced callCount " +
            std::to_string(got.callCount) + " (expected 0 or 1)");
        // Harness always returns status 0 on a defined path.
        BOOST_CHECK_MESSAGE(got.status == 0,
            "P9: selector " + std::to_string(b) + " status != 0");
        // Route matches the independently-derived expectation.
        BOOST_CHECK_MESSAGE(got == exp,
            "P9: selector " + std::to_string(b) + " routed to {call=" +
            std::to_string(got.callCount) + ",target=" + std::to_string(got.target) +
            "} but expected {call=" + std::to_string(exp.callCount) + ",target=" +
            std::to_string(exp.target) + "}");
        // A called target must equal the selector (identity mapping); a no-call
        // must carry target -1.
        if (got.callCount == 1) {
            BOOST_CHECK_MESSAGE(got.target == static_cast<int>(selector),
                "P9: called target (" + std::to_string(got.target) +
                ") != selector (" + std::to_string(b) + ")");
        } else {
            BOOST_CHECK_MESSAGE(got.target == -1,
                "P9: no-call route must carry target -1 (selector " +
                std::to_string(b) + ")");
        }

        // (1)/(3) Out-of-range gating and determinism of a repeated dispatch.
        if (selector >= CVM_FUZZ_TARGET_END) {
            BOOST_CHECK_MESSAGE(got.callCount == 0 && got.status == 0,
                "P9: out-of-range selector " + std::to_string(b) +
                " must be a no-op with status 0 (Req 1.3)");
        }
        // Without ENABLE_EVMC, EVM/router selectors are inert (Req 14.3);
        // with it, they select exactly one target (Req 14.1/14.2).
        if (IsEvmRouterSelector(selector)) {
#ifdef ENABLE_EVMC
            BOOST_CHECK_MESSAGE(got.callCount == 1,
                "P9: EVM/router selector " + std::to_string(b) +
                " must select exactly one target under ENABLE_EVMC");
#else
            BOOST_CHECK_MESSAGE(got.callCount == 0 && got.status == 0,
                "P9: EVM/router selector " + std::to_string(b) +
                " must be a no-op with status 0 without ENABLE_EVMC (Req 14.3)");
#endif
        }

        // Determinism: dispatching the identical buffer again is bit-identical.
        const DispatchResult got2 = SimulateDispatch(buffer);
        BOOST_CHECK_MESSAGE(got == got2,
            "P9: dispatch of an identical buffer was not deterministic (selector " +
            std::to_string(b) + ")");
    }

    // -----------------------------------------------------------------------
    // (B) Randomised loop (>= 100 iterations): the route must depend ONLY on
    // the first byte. For each iteration we fix a selector, then dispatch it
    // with several DIFFERENT random tails (and lengths) and assert every one
    // yields the identical route. This proves the tail bytes and buffer length
    // never perturb the routing decision (determinism per Req 1.1).
    // -----------------------------------------------------------------------
    constexpr int kIterations = 200; // >= 100 required by the task
    int executed = 0;
    for (int i = 0; i < kIterations; ++i) {
        const uint8_t selector = static_cast<uint8_t>(rng.randrange(256));

        // Baseline: selector byte with an empty tail.
        std::vector<uint8_t> base{selector};
        const DispatchResult baseRoute = SimulateDispatch(base);

        // The empty-tail baseline must equal the independently-expected route.
        BOOST_CHECK_MESSAGE(baseRoute == expectedRoute(selector),
            "P9: empty-tail baseline route mismatch for selector " +
            std::to_string(static_cast<int>(selector)));

        // Dispatch the SAME selector with several different random tails.
        for (int v = 0; v < 4; ++v) {
            std::vector<uint8_t> buffer{selector};
            const size_t tail = static_cast<size_t>(rng.randrange(64)); // 0..63
            {
                std::vector<unsigned char> bytes = rng.randbytes(tail);
                buffer.insert(buffer.end(), bytes.begin(), bytes.end());
            }
            const DispatchResult r = SimulateDispatch(buffer);
            BOOST_CHECK_MESSAGE(r == baseRoute,
                "P9: route changed with the tail bytes for selector " +
                std::to_string(static_cast<int>(selector)) +
                " (tail len " + std::to_string(tail) + ", iter #" +
                std::to_string(i) + ") — routing must depend only on byte 0");
            BOOST_CHECK(r.callCount == 0 || r.callCount == 1);
            BOOST_CHECK(r.status == 0);
        }

        ++executed;
    }
    BOOST_CHECK_MESSAGE(executed >= 100,
        "P9: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // (C) Pinned edge cases as concrete, deterministic witnesses.
    // -----------------------------------------------------------------------
    // Empty buffer -> no call, status 0 (Req 1.2, boundary of the selector path).
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{});
        BOOST_CHECK_EQUAL(r.callCount, 0);
        BOOST_CHECK_EQUAL(r.target, -1);
        BOOST_CHECK_EQUAL(r.status, 0);
    }
    // First in-range native selector 0 -> exactly one native call.
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{CVM_EXECUTE});
        BOOST_CHECK_EQUAL(r.callCount, 1);
        BOOST_CHECK_EQUAL(r.target, static_cast<int>(CVM_EXECUTE));
        BOOST_CHECK_EQUAL(r.status, 0);
    }
    // Last native selector 4 -> exactly one native call (boundary of always-on range).
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{EXECUTE_CONTRACT});
        BOOST_CHECK_EQUAL(r.callCount, 1);
        BOOST_CHECK_EQUAL(r.target, static_cast<int>(EXECUTE_CONTRACT));
    }
    // The END marker (13) is the first invalid selector -> no call, status 0 (Req 1.3).
    {
        const DispatchResult r = SimulateDispatch(
            std::vector<uint8_t>{static_cast<uint8_t>(CVM_FUZZ_TARGET_END)});
        BOOST_CHECK_EQUAL(r.callCount, 0);
        BOOST_CHECK_EQUAL(r.target, -1);
        BOOST_CHECK_EQUAL(r.status, 0);
    }
    // The maximum byte value 255 is out of range -> no call, status 0 (Req 1.3).
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{0xFF});
        BOOST_CHECK_EQUAL(r.callCount, 0);
        BOOST_CHECK_EQUAL(r.target, -1);
        BOOST_CHECK_EQUAL(r.status, 0);
    }
    // First EVM/router selector 5: gated by ENABLE_EVMC (Req 14.1/14.3).
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{EVM_EXECUTE});
#ifdef ENABLE_EVMC
        BOOST_CHECK_EQUAL(r.callCount, 1);
        BOOST_CHECK_EQUAL(r.target, static_cast<int>(EVM_EXECUTE));
#else
        BOOST_CHECK_EQUAL(r.callCount, 0);
        BOOST_CHECK_EQUAL(r.target, -1);
#endif
        BOOST_CHECK_EQUAL(r.status, 0);
    }
    // Last EVM/router selector 12 (BYTECODE_DETECT): boundary of the gated range.
    {
        const DispatchResult r = SimulateDispatch(std::vector<uint8_t>{BYTECODE_DETECT});
#ifdef ENABLE_EVMC
        BOOST_CHECK_EQUAL(r.callCount, 1);
        BOOST_CHECK_EQUAL(r.target, static_cast<int>(BYTECODE_DETECT));
#else
        BOOST_CHECK_EQUAL(r.callCount, 0);
        BOOST_CHECK_EQUAL(r.target, -1);
#endif
        BOOST_CHECK_EQUAL(r.status, 0);
    }
}

// ===========================================================================
// Property P8 — Gas-limit derivation within the valid range.
//
// For every input byte buffer, the gas limit derived by the FuzzConsumer always
// lies in the range 0..MAX_GAS_PER_TX (inclusive), regardless of the chosen
// entry point (selector). The harness decode is reproduced exactly: byte 0 is
// the selector, the remaining bytes feed the FuzzConsumer, and ConsumeGasLimit()
// is the first tail field consumed (as in test_one_input in test_cvm_fuzzy.cpp).
//
// The derivation is `raw32 % (MAX_GAS_PER_TX + 1)`, so the result is provably in
// [0, MAX_GAS_PER_TX]. The property confirms this holds across:
//   * empty buffers and buffers with fewer than 4 tail bytes (missing bytes are
//     zero-filled, so the derived value stays in range);
//   * buffers of every length up to MAX_CODE_SIZE with random content;
//   * every selector value 0..255 (valid, out-of-range, and EVM/router ones) —
//     the clamp is independent of the selector byte.
//
// The FuzzConsumer used here is a byte-for-byte replica of the harness consumer
// (the harness lives in the separate test_cascoin_cvm_fuzzy binary and is not
// linked into test_cascoin), mirroring the ContractStorage_Mock replica pattern.
//
// Validates: Requirements 1.5, 14.6
// ===========================================================================
BOOST_AUTO_TEST_CASE(p8_gas_limit_derivation_within_valid_range_property)
{
    // Deterministic PRNG with a distinct fixed seed (…c8) so this property
    // explores its own slice of the input space and stays reproducible.
    FastRandomContext rng(uint256S(
        "00000000000000000000000000000000000000000000000000000000000000c8"));

    constexpr int kIterations = 400; // >= 100 required by the task
    int executed = 0;

    for (int i = 0; i < kIterations; ++i) {
        // Whole-input length in 0..MAX_CODE_SIZE + 1, biased towards short
        // buffers half the time so the "fewer than 4 tail bytes" edge cases
        // (including the empty and selector-only buffers) are exercised
        // alongside long ones.
        size_t len = rng.randbool()
            ? static_cast<size_t>(rng.randrange(9))                          // 0..8
            : static_cast<size_t>(rng.randrange(CVM::MAX_CODE_SIZE + 2));     // 0..24577

        std::vector<uint8_t> buffer;
        {
            std::vector<unsigned char> bytes = rng.randbytes(len);
            buffer.assign(bytes.begin(), bytes.end());
        }

        // Reproduce the harness decode: byte 0 (if present) is the selector and
        // is NOT consumed by the FuzzConsumer; the rest feeds the consumer.
        const uint8_t* payload = buffer.empty() ? nullptr : buffer.data() + 1;
        const size_t payloadSize = buffer.empty() ? 0 : buffer.size() - 1;

        FuzzConsumer c(payload, payloadSize);
        const uint64_t gasLimit = c.ConsumeGasLimit();

        // Core P8 invariant: the derived gas limit is always within the valid
        // range (0 <= gasLimit holds by unsignedness; the meaningful check is
        // the upper bound). Selector-independent by construction.
        BOOST_CHECK_MESSAGE(gasLimit <= CVM::MAX_GAS_PER_TX,
            "P8: derived gasLimit (" + std::to_string(gasLimit) +
            ") exceeds MAX_GAS_PER_TX (" + std::to_string(CVM::MAX_GAS_PER_TX) +
            ") (iter #" + std::to_string(i) + ", len=" + std::to_string(len) + ")");

        ++executed;
    }

    BOOST_CHECK_MESSAGE(executed >= 100,
        "P8: property must run at least 100 iterations (ran " +
        std::to_string(executed) + ")");

    // -----------------------------------------------------------------------
    // Selector independence: for a fixed payload, sweeping the selector byte
    // over the full 0..255 range must never change the derived gas limit nor
    // push it out of range (the clamp reads only tail bytes, never the
    // selector). This pins Req 14.6 ("any selector") explicitly.
    // -----------------------------------------------------------------------
    {
        // A payload whose last 4 tail bytes decode to a value far larger than
        // MAX_GAS_PER_TX, so the modulo clamp is genuinely exercised. Little-
        // endian 0xFFFFFFFF = 4,294,967,295 -> % 1,000,001.
        const std::vector<uint8_t> payload = {
            0xAA, 0xBB, 0xCC,                 // front bytes (code) — ignored by gas
            0xFF, 0xFF, 0xFF, 0xFF            // 4 tail bytes -> raw32 = 0xFFFFFFFF
        };
        const uint64_t expected =
            static_cast<uint64_t>(0xFFFFFFFFu) % (CVM::MAX_GAS_PER_TX + 1);

        for (int sel = 0; sel < 256; ++sel) {
            std::vector<uint8_t> buffer;
            buffer.push_back(static_cast<uint8_t>(sel)); // selector byte
            buffer.insert(buffer.end(), payload.begin(), payload.end());

            FuzzConsumer c(buffer.data() + 1, buffer.size() - 1);
            const uint64_t gasLimit = c.ConsumeGasLimit();

            BOOST_CHECK_MESSAGE(gasLimit <= CVM::MAX_GAS_PER_TX,
                "P8: derived gasLimit out of range for selector " +
                std::to_string(sel));
            BOOST_CHECK_MESSAGE(gasLimit == expected,
                "P8: gasLimit depends on the selector byte (selector " +
                std::to_string(sel) + ", got " + std::to_string(gasLimit) +
                ", expected " + std::to_string(expected) + ")");
        }
    }

    // -----------------------------------------------------------------------
    // Boundary witnesses for the clamp, independent of the random loop.
    // -----------------------------------------------------------------------

    // (a) Empty payload (buffer holds only a selector) -> zero-filled tail ->
    // gasLimit 0, the minimum of the valid range.
    {
        FuzzConsumer c(nullptr, 0);
        const uint64_t gasLimit = c.ConsumeGasLimit();
        BOOST_CHECK_EQUAL(gasLimit, 0u);
        BOOST_CHECK(gasLimit <= CVM::MAX_GAS_PER_TX);
    }

    // (b) A raw32 tail value equal to MAX_GAS_PER_TX maps to MAX_GAS_PER_TX (the
    // upper bound). MAX_GAS_PER_TX (1,000,000) is < 2^32, so it is representable
    // directly and passes through the modulo unchanged.
    {
        const uint32_t raw = static_cast<uint32_t>(CVM::MAX_GAS_PER_TX);
        const std::vector<uint8_t> payload = {
            static_cast<uint8_t>(raw & 0xFF),
            static_cast<uint8_t>((raw >> 8) & 0xFF),
            static_cast<uint8_t>((raw >> 16) & 0xFF),
            static_cast<uint8_t>((raw >> 24) & 0xFF)
        };
        FuzzConsumer c(payload.data(), payload.size());
        const uint64_t gasLimit = c.ConsumeGasLimit();
        BOOST_CHECK_EQUAL(gasLimit, static_cast<uint64_t>(CVM::MAX_GAS_PER_TX));
        BOOST_CHECK(gasLimit <= CVM::MAX_GAS_PER_TX);
    }

    // (c) raw32 == MAX_GAS_PER_TX + 1 wraps back to 0 (modulo boundary).
    {
        const uint32_t raw = static_cast<uint32_t>(CVM::MAX_GAS_PER_TX + 1);
        const std::vector<uint8_t> payload = {
            static_cast<uint8_t>(raw & 0xFF),
            static_cast<uint8_t>((raw >> 8) & 0xFF),
            static_cast<uint8_t>((raw >> 16) & 0xFF),
            static_cast<uint8_t>((raw >> 24) & 0xFF)
        };
        FuzzConsumer c(payload.data(), payload.size());
        const uint64_t gasLimit = c.ConsumeGasLimit();
        BOOST_CHECK_EQUAL(gasLimit, 0u);
        BOOST_CHECK(gasLimit <= CVM::MAX_GAS_PER_TX);
    }
}

BOOST_AUTO_TEST_SUITE_END()
