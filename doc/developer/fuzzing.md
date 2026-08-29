Fuzz-testing Cascoin Core
==========================

A special test harness `test_cascoin_fuzzy` is provided to provide an easy
entry point for fuzzers and the like. In this document we'll describe how to
use it with AFL.

Building AFL
-------------

It is recommended to always use the latest version of afl:
```
wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
tar -zxvf afl-latest.tgz
cd afl-<version>
make
export AFLPATH=$PWD
```

Instrumentation
----------------

To build Cascoin Core using AFL instrumentation (this assumes that the
`AFLPATH` was set as above):
```
./configure --disable-ccache --disable-shared --enable-tests CC=${AFLPATH}/afl-gcc CXX=${AFLPATH}/afl-g++
export AFL_HARDEN=1
cd src/
make test/test_cascoin_fuzzy
```
We disable ccache because we don't want to pollute the ccache with instrumented
objects, and similarly don't want to use non-instrumented cached objects linked
in.

The fuzzing can be sped up significantly (~200x) by using `afl-clang-fast` and
`afl-clang-fast++` in place of `afl-gcc` and `afl-g++` when compiling. When
compiling using `afl-clang-fast`/`afl-clang-fast++` the resulting
`test_cascoin_fuzzy` binary will be instrumented in such a way that the AFL
features "persistent mode" and "deferred forkserver" can be used. See
https://github.com/mcarpenter/afl/tree/master/llvm_mode for details.

Preparing fuzzing
------------------

AFL needs an input directory with examples, and an output directory where it
will place examples that it found. These can be anywhere in the file system,
we'll define environment variables to make it easy to reference them.

```
mkdir inputs
AFLIN=$PWD/inputs
mkdir outputs
AFLOUT=$PWD/outputs
```

Example inputs are available from:

- https://download.visucore.com/bitcoin/bitcoin_fuzzy_in.tar.xz
- http://strateman.ninja/fuzzing.tar.xz

Extract these (or other starting inputs) into the `inputs` directory before starting fuzzing.

Fuzzing
--------

To start the actual fuzzing use:
```
$AFLPATH/afl-fuzz -i ${AFLIN} -o ${AFLOUT} -m52 -- test/test_cascoin_fuzzy
```

You may have to change a few kernel parameters to test optimally - `afl-fuzz`
will print an error and suggestion if so.

Fuzzing the CVM
===============

In addition to the generic `test_cascoin_fuzzy` harness described above (which
only exercises Bitcoin structure deserialization), Cascoin ships a dedicated
fuzz harness for the Cascoin Virtual Machine (CVM). Its binary is
`test_cascoin_cvm_fuzzy` (built from `src/test/test_cvm_fuzzy.cpp`). It forwards
arbitrary inputs to the public CVM entry points and checks core safety and
correctness invariants (crash freedom, termination, gas/stack bounds,
deterministic execution).

The primary fuzzing loop runs entirely in-process, using an in-memory
`ContractStorage` mock, so runs are deterministic and never touch LevelDB or
any other persistent storage. Regtest (see the bottom of this section) is used
only to reproduce and validate findings end-to-end, not as the primary fuzzing
loop.

Building the CVM harness
------------------------

The harness is a standard Autotools target. From the repository root, configure
the tree (once) and then build the target from `src/`:

```
./autogen.sh
./configure --disable-ccache --disable-shared --enable-tests
cd src/
make test/test_cascoin_cvm_fuzzy
```

Success signal: the build finishes with exit code 0 and produces the executable
`src/test/test_cascoin_cvm_fuzzy`.

To build with AFL instrumentation, configure the tree with the AFL compilers
(assuming `AFLPATH` is set as in the "Building AFL" section above), preferring
`afl-clang-fast`/`afl-clang-fast++` for persistent mode and deferred forkserver
support:

```
./configure --disable-ccache --disable-shared --enable-tests \
    CC=${AFLPATH}/afl-clang-fast CXX=${AFLPATH}/afl-clang-fast++
export AFL_HARDEN=1
cd src/
make test/test_cascoin_cvm_fuzzy
```

The harness builds and runs both with and without `ENABLE_EVMC` defined (the
EVM/router entry points below become no-ops when it is not defined).

Selecting an entry point
------------------------

The **first byte** of every input buffer is the entry-point selector; it is
interpreted directly as an index into the target list below. The remaining
bytes carry the code/input data (read from the front) and the derived execution
context — gas limit, addresses, call value, block height, block hash, timestamp
(read from the tail). The derived gas limit is always clamped to the range
`0..MAX_GAS_PER_TX` (1,000,000).

To exercise a specific entry point, make the first byte of the input file equal
to the selector value. For example, to run `CVM::VerifyBytecode` (selector 1)
over a byte sequence, prepend the byte `0x01`:

```
printf '\x01\x60\x00' > /tmp/verify_input.bin
./test/test_cascoin_cvm_fuzzy /tmp/verify_input.bin
```

An empty buffer, or a first byte greater than or equal to 13 (the range
marker), calls no entry point and returns with status code 0.

Native CVM entry points (always available)
------------------------------------------

These selectors are compiled unconditionally:

- `0` — `CVM::Execute`: runs the register-based interpreter over the front bytes
  as bytecode with the derived context.
- `1` — `CVM::VerifyBytecode` (static): classifies the front bytes as valid or
  invalid CVM bytecode without executing them.
- `2` — `CVM::DeployContract`: verifies and deploys the front bytes as contract
  bytecode at the derived contract address.
- `3` — `CVM::CallContract`: calls the contract at the derived address, passing
  the front bytes as input data.
- `4` — `ExecuteContract` (free function): executes the front bytes as bytecode
  with full context and returns an `ExecutionResult` (`success`, `gasUsed`,
  `returnData`, `logs`, `error`).

EVM and router entry points (only with `ENABLE_EVMC`)
-----------------------------------------------------

The following selectors are active only when the harness is compiled with
`ENABLE_EVMC` defined (the current default build). When `ENABLE_EVMC` is not
defined, each of these selectors is an inert no-op that returns with status
code 0, and the selector numbering stays identical so the same seed corpus
works in both build configurations.

EVM_Engine entry points (`CVM::EVMEngine`, backed by the external evmone
interpreter):

- `5` — `EVMEngine::Execute`: executes the front bytes as Ethereum bytecode with
  the derived context.
- `6` — `EVMEngine::DeployContract`: deploys the front bytes as EVM creation
  bytecode from the derived deployer address and value.
- `7` — `EVMEngine::StaticCall`: performs a read-only EVM call to the derived
  target address with the front bytes as call data.
- `8` — `EVMEngine::DelegateCall`: performs an EVM delegate call to the derived
  target address with the front bytes as call data.

EnhancedVM router entry points (`CVM::EnhancedVM`, format-detecting router):

- `9` — `EnhancedVM::Execute`: detects the bytecode format and routes execution
  to the native CVM or the EVM engine.
- `10` — `EnhancedVM::DeployContract`: detects the format and deploys the front
  bytes accordingly.
- `11` — `EnhancedVM::CallContract`: detects the format of the target contract
  and routes the call with the front bytes as call data.
- `12` — `BytecodeDetector::DetectFormat`: classifies the front bytes into
  exactly one bytecode format (`CVM_NATIVE`, `EVM_BYTECODE`, `HYBRID`, or
  `UNKNOWN`).

The external evmone/evmc interpreter is treated as a **trusted third-party
library** and is **not** itself a fuzzing target. The fuzzing target for the EVM
and router layers is the Cascoin integration code (the `EVMCHost` callbacks and
the `EnhancedVM` router) plus engine-independent invariants. Internal evmone
error status codes (`EVMC_FAILURE`, `EVMC_REVERT`, out-of-gas, and similar)
count as a defined result, not as an invariant violation.

Seed corpus
-----------

An initial seed corpus ships in `src/test/fuzz_seeds/cvm/` (relative to the
repository root). It contains at least one seed per selectable entry point, a
`VerifyBytecode`-valid seed, a `VerifyBytecode`-invalid seed, and an
SSTORE-then-SLOAD round-trip seed. Point the fuzz engine's input directory at
this path when starting a session.

Running a fuzzing session
-------------------------

AFL needs an input directory (the seed corpus) and an output directory (where it
stores discovered inputs and crashes):

```
AFLIN=$PWD/test/fuzz_seeds/cvm
mkdir -p /tmp/cvm_out
AFLOUT=/tmp/cvm_out
```

Start fuzzing with a concrete bounding parameter. The example below bounds the
session to a fixed wall-clock budget of 60 seconds via `AFL_EXIT_ON_TIME`
(alternatively, omit the bound and stop manually with Ctrl-C, or use
`AFL_EXIT_WHEN_DONE=1` to stop once AFL has exhausted its queue):

```
AFL_EXIT_ON_TIME=60 $AFLPATH/afl-fuzz -i ${AFLIN} -o ${AFLOUT} -m52 \
    -- test/test_cascoin_cvm_fuzzy
```

Success signal: AFL prints its interactive status screen showing a non-zero
`exec speed` (executions/sec) and the queue advancing. Any input that triggers
an invariant violation or crash is written under `${AFLOUT}/crashes/`.

If the tree was instead built with a libFuzzer-capable toolchain, the same
target can be driven in-process with an explicit bound, using either a maximum
runtime or a maximum number of executions, and an artifact directory for
crashes:

```
# Bound by runtime (60 seconds):
./test/test_cascoin_cvm_fuzzy -max_total_time=60 \
    -artifact_prefix=/tmp/cvm_out/ test/fuzz_seeds/cvm/

# Or bound by number of executions (100000 runs):
./test/test_cascoin_cvm_fuzzy -runs=100000 \
    -artifact_prefix=/tmp/cvm_out/ test/fuzz_seeds/cvm/
```

Success signal: libFuzzer prints periodic `#<n> ... exec/s:` progress lines and
exits with `Done ...` when the bound is reached; any failing input is written as
a `crash-*` artifact under the prefix directory.

Replaying a single input
------------------------

The harness reads exactly one input file when a path is given as its first
command-line argument, so a stored crash input can be replayed without a running
fuzz engine:

```
./test/test_cascoin_cvm_fuzzy ${AFLOUT}/crashes/id:000000,...
```

Because both native and (under `ENABLE_EVMC`) EVM/router execution are
deterministic, the same buffer reproduces the same observable result on 100% of
runs. If the file does not exist or cannot be read, the harness prints an error
message naming the cause to stderr and exits without crashing itself.

Reproducing a finding in regtest
--------------------------------

The in-process harness is the primary fuzzing loop; regtest is used to
reproduce and validate a finding end-to-end. Extract the bytecode from the
crash input (the front bytes after the selector byte) and drive it through the
CVM RPC commands in a local regtest network:

```bash
# 1) Start the regtest daemon
src/cascoind -regtest -daemon
# Success signal: "Cascoin server starting"; getblockchaininfo returns "chain": "regtest".

# 2) Generate mature coins
src/cascoin-cli -regtest generate 101
# Success signal: 101 returned block hashes; getbalance > 0.

# 3) Prepare and deploy the contract with the reproduced bytecode
src/cascoin-cli -regtest deploycontract <bytecode_hex>
src/cascoin-cli -regtest sendcvmcontract <deploy_hex>
src/cascoin-cli -regtest generate 1
# Success signal: getcontractinfo <addr> returns the contract metadata
# (or, for invalid bytecode, a defined error instead of a daemon crash).

# 4) Call the contract with the reproduced input data
src/cascoin-cli -regtest callcontract <addr> <inputdata_hex>
# Success signal: a defined RPC response with success/error and gasUsed;
# the daemon stays alive (getblockchaininfo still responds).
```

A crash input is considered successfully reproduced when the in-process harness
reports an invariant violation while regtest shows the expected defined behavior
(a defined error status and no daemon crash); the difference localizes the
integration error.
