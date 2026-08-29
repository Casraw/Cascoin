# CVM Functional Fixes Bugfix Design

## Overview

This design formalizes the fixes for the 62 verified defects in `src/cvm/`
documented in the approved requirements (`bugfix.md`, clauses 1.1–1.62). Each
defect is a functional gap: code that compiles and runs but does not perform the
work it claims. The defects range from consensus-critical (subsidy accounting,
gas cost, contract-address derivation, signature enforcement, state
persistence/rollback) to service-quality gaps (Sybil/fraud detection,
cross-chain bridging, RPC completeness, graceful degradation).

The fix approach applies the bug-condition methodology per defect and groups the
defects into twelve coherent **workstreams** by category. For every workstream
the design records: the concrete code location (file/function) where the defect
lives, the root cause, the fix approach, how the fix satisfies the corresponding
Expected Behavior clause (**fix checking**), and how it leaves the "Unchanged
Behavior" regression clauses intact (**preservation checking**).

A subset of these defects is **consensus-critical**: changing them alters which
blocks a node considers valid (subsidy limits, gas cost, contract address
derivation, `OP_VERIFY_SIG` enforcement, state persistence/rollback, coinbase
validator split). Because the CVM is not yet live on any production network,
there is no historical chain to preserve below any activation point, so the
corrected behavior is applied **unconditionally** (directly). Consensus-critical
fixes are still called out as such — they change which blocks are valid — but
they are simply implemented directly, replacing the buggy behavior. Non-consensus
fixes (RPC output, local metrics, Sybil scoring) are likewise applied directly.

The overarching strategy is: (1) surface counterexamples on the unfixed code to
confirm each root cause, (2) implement the minimal targeted fix at the concrete
location, and (3) verify with unit, property-based, and functional tests that the
bug is fixed for buggy inputs and that non-buggy inputs behave identically.

## Glossary

- **Bug_Condition (C)**: An input that reaches any flagged CVM code path
  (clauses 1.1–1.62) whose current behavior is a placeholder, stub, or hardcoded
  value instead of the real computation.
- **Property (P)**: The correct behavior a flagged path SHALL exhibit, as stated
  in the corresponding Expected Behavior clause (2.1–2.62).
- **Preservation**: Behavior of non-flagged inputs (standard transactions, WoT
  transactions, genuinely valid signatures, already-correct paths) that MUST
  remain byte-for-byte identical, as stated in the Unchanged Behavior clauses
  (3.1–3.24).
- **F / F'**: The original (unfixed) function and the fixed function.
- **Consensus-critical fix**: A fix that changes which blocks/transactions are
  accepted. Because the CVM is not yet live on a production network, these fixes
  are applied unconditionally with no historical chain to preserve.
- **Deployer+nonce address scheme**: The canonical CVM contract-address
  derivation `GenerateContractAddress(deployerAddr, nonce)` in `contract.cpp`
  (`Hash(deployerAddr || nonce)[0:20]`).
- **TrustContext**: The per-execution structure carrying caller reputation and
  trust-tagged memory policy passed to the Enhanced VM.
- **HAT v2 / SecureHAT**: The Hybrid Adaptive Trust distributed-consensus and
  scoring subsystem used for validator selection and validation.
- **ASRS**: Anti-Scam Reputation System — the on-chain reputation vote/score
  system in `reputation.cpp`.

## Bug Details

### Bug Condition

The bug manifests whenever an input exercises one of the flagged CVM code paths.
Each flagged path is either a TODO/placeholder body, a hardcoded return, or a
"simplified" check standing in for real logic. The generalized bug condition:

**Formal Specification:**
```
FUNCTION isBugCondition(input)
  INPUT: input reaching a CVM code path P in {1.1 .. 1.62}
  OUTPUT: boolean

  RETURN reachesFlaggedPath(input, P)
         AND observedBehavior(P, input) == placeholderOrHardcodedBehavior(P)
         AND observedBehavior(P, input) != specifiedBehavior(P)   // per clause 2.x
END FUNCTION
```

Per-workstream specializations of `isBugCondition` are given in the Fix
Implementation section.

### Examples

- **Subsidy accounting (1.1)**: A block carrying two CVM transactions each with a
  1,000,000-gas subsidy is accepted even though the accumulated subsidy exceeds
  the per-block maximum, because the accumulation loop body is a TODO and total
  is always 0. Expected: the block is rejected once the total exceeds the max.
- **Gas cost (1.2)**: A deploy transaction with `gasLimit = 500,000` and a gas
  price of 3 sat/gas is charged 500,000 satoshi (1:1) instead of
  `gasUsed × 3`. Expected: cost derived from actual gas used × gas price.
- **Contract address (1.16)**: `ProcessCVMBlock` in `cvmtx.cpp` derives the
  address as `txHash[0:20]`, producing an address that differs from the canonical
  `GenerateContractAddress(deployer, nonce)` used everywhere else — the deployed
  contract cannot be found by later calls. Expected: same deployer+nonce scheme.
- **Signature verification (1.23)**: A CVM contract executing `OP_VERIFY_SIG`
  with a garbage signature pushes 1 (valid) because `verifyResult = true` is
  hardcoded (`cvm.cpp` lines ~446/450/482/514). Expected: real secp256k1/FALCON
  verification; push 1 only for a genuinely valid signature.
- **State rollback (1.21)**: A block whose contract execution fails is not
  reverted because `RollbackContractState()` is a log-only no-op — partial state
  writes leak into the committed DB. Expected: atomic persist / full revert.
- **Edge case — zero-value call (3.22)**: A deploy/call carrying zero value must
  still expose `CALLVALUE = 0`; the corrected `blockprocessor.cpp` path passes
  the actual (zero) value, so behavior is unchanged for this input.

## Expected Behavior

### Preservation Requirements

**Unchanged Behaviors (must stay identical after the fix):**

- Standard (non-CVM, non-EVM) transaction validation and mempool acceptance (3.10).
- Blocks with no CVM/EVM transactions and no subsidies validate exactly as before (3.2, 3.17).
- Web-of-Trust transactions (reputation vote, trust edge, bonded vote, DAO
  dispute/vote) remain non-contract with 100% fees to the miner (3.3).
- Existing secp256k1 ECDSA signing/verification of validator responses continues
  to produce and accept valid signatures (3.4, 3.18).
- Deterministic Fisher-Yates validator selection for a given seed/height (3.5).
- Merkle verification math for genuinely committed leaves (3.6).
- Persistent contract storage SLOAD/SSTORE semantics (3.7).
- Free-gas eligibility with sufficient allowance yields a zero effective fee (3.8).
- Already-supported EVM opcodes/context values execute unchanged (3.9).
- Genuinely valid signatures still accepted by the `OP_VERIFY_SIG` family (3.11).
- Canonical deployer+nonce address generation unchanged (3.14).
- Receipt JSON keeps all existing fields; only `logsBloom` is corrected (3.13).
- Cross-chain proofs with valid signatures + committed source state still accepted (3.15).
- Transactions subsidy-eligible under both old and new checks still get the subsidy (3.16).
- Already-loaded persisted state (blacklist/allowance/reputation/trust-graph)
  exposed unchanged after load routines complete (3.19).
- Bytecode-format detection, trust-graph manipulation detection, behavior-metric
  scoring (verified functional, out of scope) unchanged (3.20).
- Other security-monitoring RPCs unchanged (3.21).
- Zero-value deploy/call still exposes `CALLVALUE = 0` (3.22).
- Already-durable execution results exposed unchanged after `CommitExecutionState` (3.23).
- Genuinely healthy subsystems still report success under graceful degradation (3.24).

**Scope:**
All inputs that do NOT reach a flagged path (clauses 1.1–1.62) must be completely
unaffected. Because the CVM is not yet live on a production network, there is no
historical chain and no activation point below which behavior must be preserved;
the corrected behavior applies unconditionally to every flagged path.

## Hypothesized Root Cause

The defects share a small number of root-cause patterns, each verified by reading
the source:

1. **Unfinished TODO / placeholder bodies**: Loop bodies and functions left as
   TODOs that return a neutral value (1.1 subsidy accumulation, 1.46 mempool
   priority init, 1.51 PruneReceipts, 1.52/1.45 load routines, 1.54 metrics).

2. **Hardcoded / "simplified" stand-ins**: Constants substituted for real
   computation (1.2 1:1 gas, 1.3 `isValid=true`/80%, 1.4 trust=50, 1.19
   `isBeneficial=true`, 1.22 skipped 70/30 check, 1.23 `verifyResult=true`, 1.24
   `OP_BALANCE`→0, 1.28 zero bloom, 1.40/1.43 reputation-only benefit checks,
   1.42 load=50).

3. **Divergent / incorrect derivation**: Address derived from the wrong source
   (1.16 `txHash[0:20]`, 1.27/1.56 pseudo-address from prevout hash) instead of
   the canonical scheme; state root from a fixed string + time (1.5).

4. **Missing wiring to a real subsystem**: A function returns success/empty
   without invoking the subsystem it names (1.8 no P2P send, 1.15/1.62 no real
   health check, 1.34 no cross-chain send, 1.38 empty delta, 1.47 responses not
   accumulated, 1.13/1.50 empty transaction index, 1.61 no DB flush).

5. **Enforcement bypass**: A check that accepts on a weak proxy (1.6 length-only
   signature check, 1.37 length-only attestation check, 1.36 accepts any pubkey,
   1.33 non-empty-only proof check).

6. **No-op execution during block processing**: Contract code/constructor never
   run, votes attributed to the zero address, context values hardcoded (1.17,
   1.18, 1.60).

## Correctness Properties

This section is the single source of truth for correctness properties.
Properties 1–20 are fix properties that assert `isBugCondition ⇒ specified
behavior` (each mapping to one or more Expected Behavior clauses 2.x). The final
property (number 21) is a preservation property asserting
`¬isBugCondition ⇒ F'(x) = F(x)` (mapping to Unchanged Behavior clauses 3.x).

Property 1: Bug Condition — Per-block subsidy limit enforced

_For any_ block whose CVM/EVM transactions carry gas subsidies, the fixed
accounting SHALL accumulate the actual per-transaction subsidies and reject the
block when the total exceeds the per-block subsidy maximum.

**Validates: Requirements 2.1**

Property 2: Bug Condition — Gas cost from actual gas and gas price

_For any_ contract deploy or call transaction, the fixed extraction SHALL compute
gas cost as `gasUsed × gasPrice` from the transaction rather than a fixed 1:1
rate.

**Validates: Requirements 2.2**

Property 3: Bug Condition — Contract address consistency

_For any_ contract deploy processed by `cvmtx.cpp ProcessCVMBlock`, the fixed code
SHALL derive the contract address via the canonical `GenerateContractAddress(
deployer, nonce)` scheme so it equals the address produced by every other CVM
path for the same deployer and nonce.

**Validates: Requirements 2.16**

Property 4: Bug Condition — Contract execution during block processing

_For any_ contract deploy or call processed in block processing, the fixed code
SHALL execute the constructor/contract code and account for actual gas used
(not merely accumulate `gasLimit`).

**Validates: Requirements 2.17**

Property 5: Bug Condition — Reputation votes attributed to real voter

_For any_ reputation vote applied in `UpdateReputationScores`, the fixed code
SHALL resolve the real voter address from the transaction inputs and update
per-participant behavior scores, never attributing to the zero address.

**Validates: Requirements 2.18**

Property 6: Bug Condition — Block state persisted atomically / rolled back

_For any_ block whose contract execution fails, the fixed validator SHALL revert
all contract-state changes; for any accepted block it SHALL persist changes
atomically.

**Validates: Requirements 2.19, 2.20, 2.21**

Property 7: Bug Condition — Coinbase validator split enforced

_For any_ coinbase evaluated when validator participation data is available,
`CheckCoinbaseValidatorPayments` SHALL enforce the 70/30 validator payment split.

**Validates: Requirements 2.22**

Property 8: Bug Condition — Signature verification enforced

_For any_ execution of `OP_VERIFY_SIG`, `OP_VERIFY_SIG_ECDSA`, or
`OP_VERIFY_SIG_QUANTUM`, the fixed VM SHALL verify the signature against the
message and public key with the appropriate algorithm and push 1 only for a
genuinely valid signature.

**Validates: Requirements 2.23**

Property 9: Bug Condition — OP_BALANCE and OP_CALL and OP_LOG functional

_For any_ execution of `OP_BALANCE` the VM SHALL push the account's actual
balance; for `OP_CALL`/`CallContract` it SHALL load and execute the target or
fail deterministically with a defined error; for `OP_LOG` it SHALL consume the
topic count, topics, and data and emit a log entry.

**Validates: Requirements 2.24, 2.25, 2.26**

Property 10: Bug Condition — Reputation proofs are real

_For any_ reputation state proof or merkle proof, the fixed code SHALL produce a
real validator signature and derive the root/proof from the committed reputation
state tree; verification SHALL perform ECDSA verification and succeed only for
genuinely committed entries.

**Validates: Requirements 2.5, 2.6, 2.7**

Property 11: Bug Condition — HAT distributed validation is real

_For any_ HAT consensus challenge the fixed code SHALL transmit a P2P message and
report success only when dispatched; validators SHALL perform actual task
validation reporting a derived `isValid`/confidence and a trust score computed
from the trust graph; disputes SHALL use the actual self-reported score; and
`ProcessValidatorResponse` SHALL accumulate responses toward consensus.

**Validates: Requirements 2.3, 2.4, 2.8, 2.9, 2.47**

Property 12: Bug Condition — Fee/gas/subsidy accounting uses real inputs

_For any_ fee/subsidy computation the fixed code SHALL apply applicable subsidies
before the effective fee, assess actual network benefit (not reputation alone),
resolve the real sender, derive network load/conversion from live sources,
transfer/credit rebates, restore persisted allowance state, and initialize with
the CVM database.

**Validates: Requirements 2.10, 2.40, 2.41, 2.42, 2.43, 2.44, 2.45, 2.46**

Property 13: Bug Condition — EVM compatibility features functional

_For any_ EVM execution the fixed code SHALL support TLOAD/TSTORE transient
storage, populate BASEFEE, derive CREATE addresses as
`keccak256(rlp([sender, nonce]))[12:]`, compute `logsBloom` from logs, resolve
the real sender and estimate gas from execution, inject caller reputation and
enforce trust-tagged memory policy, capture real nested-frame state, and produce
verifiable storage proofs.

**Validates: Requirements 2.11, 2.12, 2.27, 2.28, 2.29, 2.30, 2.31, 2.32**

Property 14: Bug Condition — Cross-chain proofs and sends are real

_For any_ inbound cross-chain proof the fixed code SHALL verify it against the
source chain's committed state; sends SHALL transmit via the corresponding
endpoint and report success only when dispatched; proofs/reads SHALL derive from
the actual state trie and return all committed attestations; oracle keys SHALL be
checked against a trusted-oracle registry.

**Validates: Requirements 2.33, 2.34, 2.35, 2.36**

Property 15: Bug Condition — Distributed-consensus signatures/state sync real

_For any_ cross-chain attestation signature the fixed code SHALL verify against
the attestor's public key; delta computation/requests SHALL query the DB and the
peer; trust-graph verify/apply SHALL operate against real state rather than
failing when no validator is configured.

**Validates: Requirements 2.37, 2.38, 2.39**

Property 16: Bug Condition — Sybil/fraud detection functional

_For any_ cluster detection, rapid-fire detection, exchange-pattern detection,
reputation-index query, or address transaction lookup, the fixed code SHALL
return real results derived from transaction/reputation data rather than
empty/false placeholders.

**Validates: Requirements 2.13, 2.14, 2.48, 2.49, 2.50**

Property 17: Bug Condition — Storage/state sync and misc paths functional

_For any_ PruneReceipts, blacklist/allowance load, state-proof/metadata build,
opcode metric record, backward-compat validation, sender/deployer extraction,
commit-phase-start determination, or cluster-merge linking, the fixed code SHALL
perform the real operation (delete, iterate, size, count, verify, extract) rather
than a placeholder.

**Validates: Requirements 2.51, 2.52, 2.53, 2.54, 2.55, 2.56, 2.57, 2.58**

Property 18: Bug Condition — Security RPC returns real validator stats

_For any_ `getvalidatorstats_security` invocation the fixed RPC SHALL return the
per-validator statistics documented in its help text from the HAT consensus
system.

**Validates: Requirements 2.59**

Property 19: Bug Condition — Primary path value/block-hash and commit correct

_For any_ deploy/call through the primary `blockprocessor.cpp` path the fixed code
SHALL pass the actual transaction value and real block hash to the Enhanced VM,
and `CommitExecutionState` SHALL flush pending contract-state writes durably.

**Validates: Requirements 2.60, 2.61**

Property 20: Bug Condition — Graceful degradation uses real subsystems

_For any_ resource-usage check or TRUST_CONTEXT/HAT_VALIDATION fallback the fixed
code SHALL measure actual memory/CPU/storage and invoke the real subsystems,
reporting success/failure from the actual result.

**Validates: Requirements 2.15, 2.62**

Property 21: Preservation — Standard and WoT transactions unchanged

_For any_ input that does NOT reach a flagged CVM path — standard transactions,
WoT transactions, blocks without CVM/EVM content, already-valid signatures,
already-correct persisted-state loads, and out-of-scope detectors — the fixed
code SHALL produce exactly the same result as the original code.

**Validates: Requirements 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10, 3.11, 3.12, 3.13, 3.14, 3.15, 3.16, 3.17, 3.18, 3.19, 3.20, 3.21, 3.22, 3.23, 3.24**

## Fix Implementation

The 62 defects are grouped into twelve workstreams. Each workstream lists its
defect clauses, concrete code locations, the fix approach, the fix-checking
target (Expected Behavior 2.x), and the preservation guarantees (Unchanged 3.x).

### Consensus-critical fixes (applied directly)

Several workstreams change which blocks are valid (subsidy limits, gas cost,
address derivation, signature enforcement, state persistence/rollback, coinbase
split). Because the CVM is not yet live on a production network, there is no
historical chain to preserve and no coordinated activation point is needed — the
corrected behavior replaces the buggy behavior directly. These fixes are still
flagged as consensus-critical so reviewers understand they alter block validity,
but they are implemented unconditionally.

**Coordination note:** The two `ProcessCVMBlock` (`cvmtx.cpp`) and primary
(`blockprocessor.cpp`) execution paths must be reconciled so that contract
address derivation, execution, value/block-hash context, and state commit are
consistent between them (3.12). Divergence between the two paths would itself be
a consensus fault, so this reconciliation is required for correctness.

### Workstream 1 — Consensus-critical accounting & block processing

**Clauses:** 1.1, 1.2, 1.16, 1.17, 1.18, 1.19, 1.20, 1.21, 1.22, 1.60, 1.61
**Fix-checking targets:** 2.1, 2.2, 2.16–2.22, 2.60, 2.61
**Preservation:** 3.1, 3.2, 3.12, 3.14, 3.17, 3.22, 3.23

Concrete changes (applied directly):
- **`block_validator.cpp` `ValidateBlock`** (subsidy loop, ~line 720): accumulate
  actual per-tx subsidy into a running total; reject via `state.DoS`/`return
  false` when total exceeds `chainparams.cvmMaxGasPerBlock`-derived subsidy max
  (2.1). Replace `bool isBeneficial = true` (~line 725) with the real benefit
  determination from `GasSubsidyTracker::IsBeneficialOperation(trust)` and record
  actual `gasUsed` rather than `gasLimit` (2.19).
- **Gas cost extraction** (`block_validator.cpp`/`fee_calculator.cpp`): compute
  `gasCost = gasUsed × gasPrice` from the transaction instead of `cost =
  gasLimit` (2.2). Verify fee against the transaction's input values in
  `VerifyReputationGasCosts` (2.20).
- **`block_validator.cpp` `SaveContractState`/`RollbackContractState`**: make
  writes go through a batch that is committed atomically on success and discarded
  (real revert) on failure, instead of the log-only no-op (2.21).
- **`cvmtx.cpp ProcessCVMBlock`** (line 146 `memcpy(contractAddr.begin(),
  txHash.begin(), 20)`): replace with `contractAddr =
  GenerateContractAddress(deployer, nonce)` using the deployer extracted via
  `ExtractDeployerAddress` and the DB nonce (2.16). Execute the constructor
  (deploy) / contract code (call) via the Enhanced VM and accumulate actual gas
  used instead of only `gasLimit` (2.17).
- **`cvmtx.cpp UpdateReputationScores`** (line 200 `uint160 voterAddr;`): resolve
  the real voter from the tx inputs (UTXO/`ExtractSenderAddress`) before
  `ApplyVote`, and update per-participant behavior scores (2.18).
- **`validator_compensation.cpp CheckCoinbaseValidatorPayments`** (line ~253
  TODO): enforce the 70/30 split against validator participation data (2.22).
- **`blockprocessor.cpp`** primary path (~lines 292/404): pass the actual tx
  value and the real block hash to the Enhanced VM instead of hardcoded `0` /
  `uint256()` (2.60). **`enhanced_vm.cpp CommitExecutionState`**: flush pending
  contract-state writes to the DB instead of logging only (2.61).

### Workstream 2 — Core-VM opcode handlers

**Clauses:** 1.23, 1.24, 1.25, 1.26  **Targets:** 2.23–2.26
**Preservation:** 3.7, 3.9, 3.11
**Consensus-critical:** 1.23, and 1.24–1.26 (they change execution results and
therefore block-processing outcomes). Applied directly.

- **`cvm.cpp` `OP_VERIFY_SIG` family** (lines ~446, 450, 482, 514
  `verifyResult = true`): extract the message, signature, and pubkey from the
  stack and perform real secp256k1 verification (ECDSA) or FALCON-512
  verification (quantum, when `DEPLOYMENT_QUANTUM` active); push 1 only on a valid
  signature (2.23). Genuinely valid signatures still push 1 (3.11).
- **`cvm.cpp` `OP_BALANCE`**: query the account balance (UTXO/state) and push it
  instead of 0 (2.24).
- **`cvm.cpp` `HandleCall` / `CallContract`**: load the target contract and
  execute with proper gas/state handling; where execution is not possible, fail
  deterministically with a defined error (2.25).
- **`cvm.cpp` `OP_LOG`**: pop topic count, topics, and data; emit a log entry
  (2.26). SLOAD/SSTORE and other opcodes untouched (3.7, 3.9).

### Workstream 3 — Reputation signatures & merkle proofs

**Clauses:** 1.5, 1.6, 1.7  **Targets:** 2.5, 2.6, 2.7  **Preservation:** 3.6
- **`reputation_signature.cpp`**: sign the proof data with the validator key
  (reuse existing secp256k1 path from 3.4/3.18) instead of copying 32 bytes of
  the proof hash; derive the state root from the committed reputation state tree
  rather than `fixedString + time` (2.5).
- **Signature verification**: replace the length-only check with real ECDSA
  verification against the signer's public key (2.6).
- **Merkle proof build**: query the real reputation state tree so verification
  against the committed root succeeds only for committed leaves (2.7). Existing
  merkle verification math for committed leaves is unchanged (3.6).

### Workstream 4 — HAT v2 distributed consensus

**Clauses:** 1.3, 1.4, 1.8, 1.9, 1.47  **Targets:** 2.3, 2.4, 2.8, 2.9, 2.47
**Preservation:** 3.4, 3.5, 3.18
- **`hat_consensus.cpp` / `consensus_validator.cpp`**: perform actual task
  validation and derive `isValid`/confidence from it (replace `isValid=true`/80%)
  (2.3); compute the reported trust score from the trust graph (replace 50)
  (2.4).
- **Challenge send**: transmit a real P2P message to the target validator and
  report success only when dispatched (replace the bare success) (2.8).
- **Dispute creation**: use the validator's actual self-reported score so a
  discrepancy can be detected (2.9).
- **`mempool_manager.cpp ProcessValidatorResponse`**: accumulate responses in the
  validation session and evaluate consensus instead of logging only (2.47).
- Deterministic Fisher-Yates selection and the ECDSA sign/verify path for
  responses are preserved (3.5, 3.4, 3.18).

### Workstream 5 — Fee / gas / subsidy accounting

**Clauses:** 1.10, 1.40, 1.41, 1.42, 1.43, 1.44, 1.45, 1.46
**Targets:** 2.10, 2.40–2.46  **Preservation:** 3.1, 3.8, 3.16
- **`mempool_priority.cpp`**: apply applicable gas subsidy before computing the
  effective fee (replace the TODO) (2.10); initialize with the CVM database
  (2.46).
- **`fee_calculator.cpp`**: replace `reputation >= 80` (line 228) with a real
  network-benefit assessment (2.40); resolve the real sender in
  `ExtractSenderAddress` via the validation UTXO set (2.41); derive network load
  from live mempool state and the gas→satoshi rate from the configured pricing
  source (replace hardcoded 50 / fixed rate) (2.42).
- **`sustainable_gas.cpp IsBeneficialOperation`**: replace `callerReputation >=
  70` with a real benefit assessment (2.43).
- **`gas_subsidy.cpp`**: actually transfer/credit rebate amounts and serialize all
  subsidy record fields (2.44).
- **`gas_allowance.cpp LoadAllowanceStates`**: iterate the DB and restore
  persisted allowance state (2.45).
- Note: fee-model changes that affect the accepted fee/subsidy split for CVM
  transactions interact with consensus; like the other consensus-critical fixes
  they are applied directly. Eligibility that matched under both old and new
  checks is preserved (3.16); free-gas zero-fee path and the previous fixed-rate
  split are preserved for matching inputs (3.8, 3.1).

### Workstream 6 — EVM compatibility

**Clauses:** 1.11, 1.12, 1.27, 1.28, 1.29, 1.30, 1.31, 1.32
**Targets:** 2.11, 2.12, 2.27–2.32  **Preservation:** 3.9, 3.13, 3.14
- **`enhanced_vm.cpp` / EVM host**: register TLOAD/TSTORE transient-storage
  handlers with per-transaction lifetime and post-tx clearing (2.11); populate
  BASEFEE for the current block (2.12).
- **`nonce_manager.cpp GenerateContractAddress`** (line ~97, currently
  `Hash160(sender||nonce)`): compute `keccak256(rlp([sender, nonce]))[12:]` for
  Ethereum compatibility (2.27). Note the CVM-native `contract.cpp`
  `GenerateContractAddress` (deployer+nonce) is a **separate** scheme and remains
  unchanged (3.14) — only the EVM CREATE path changes. `evmc_host.cpp` CREATE must
  be reconciled to the same Ethereum scheme.
- **`receipt.cpp ToJSON`**: compute `logsBloom` from the receipt's log addresses
  and topics (replace 512 zero hex chars); all other fields unchanged (2.28, 3.13).
- **`evm_rpc.cpp`**: resolve the actual sender and estimate gas from real
  execution (replace empty `uint160` + fixed constants) (2.29).
- **`evm_engine.cpp`**: inject caller reputation into the message and enforce
  trust-tagged memory access policy (2.30).
- **`enhanced_vm.cpp`** nested-frame save: capture the real `ExecutionFrame`
  context (2.31).
- **`enhanced_storage.cpp`**: build/verify a Merkle Patricia Trie storage proof
  that attests to committed storage state (2.32).
- Already-supported opcodes execute unchanged (3.9).

### Workstream 7 — Cross-chain bridging & oracle trust

**Clauses:** 1.33, 1.34, 1.35, 1.36  **Targets:** 2.33–2.36  **Preservation:** 3.15
- **`cross_chain_bridge.cpp ReputationProof::Verify`**: verify the proof against
  the source chain's committed state (replace non-empty-only check) (2.33).
- **LayerZero/CCIP send**: transmit via the endpoint and report success only when
  dispatched (2.34).
- **Merkle proof / attestation read**: derive from the actual state trie and
  return all committed attestations, not only cached ones (2.35).
- **`trust_context.cpp IsKnownLayerZeroOracle`**: check the key against a
  trusted-oracle registry for the chain (replace accept-any) (2.36).
- Valid signature + committed source state still accepted (3.15).

### Workstream 8 — Distributed-consensus signatures & state sync

**Clauses:** 1.37, 1.38, 1.39  **Targets:** 2.37, 2.38, 2.39  **Preservation:** 3.15
- **`consensus_safety.cpp`**: verify attestation signatures against the attestor's
  public key (replace 64–128-byte length check) (2.37); compute trust-graph deltas
  by querying the DB and request deltas from the peer (replace empty delta) (2.38).
- **`trust_graph_sync.cpp`**: verify/apply against real state instead of returning
  false when no validator is configured (2.39).

### Workstream 9 — Sybil-resistance & fraud detection

**Clauses:** 1.13, 1.14, 1.48, 1.49, 1.50  **Targets:** 2.13, 2.14, 2.48, 2.49, 2.50
**Preservation:** 3.20  (non-consensus — ships unconditionally)
- **`walletcluster.cpp GetTransactionsForAddress`**: return the address's
  transactions from a transaction/address index (replace empty list) (2.50), and
  apply the common-input-ownership heuristic to cluster addresses (2.13).
- **`reputation.cpp`**: `DetectExchangePattern` returns true when volume matches
  the pattern (2.48); `GetAddressesWithReputation` returns addresses backed by a
  maintained reputation index (2.49).
- **Rapid-fire detection**: return true when history matches the rapid-fire
  pattern (2.14).
- Trust-graph manipulation detection and behavior-metric scoring untouched (3.20).

### Workstream 10 — Storage / state sync & miscellaneous

**Clauses:** 1.51, 1.52, 1.53, 1.54, 1.55, 1.56, 1.57, 1.58
**Targets:** 2.51–2.58  **Preservation:** 3.19, 3.20
- **`cvmdb.cpp PruneReceipts`**: delete receipts for blocks below the given height
  (2.51).
- **`access_control_audit.cpp LoadBlacklist`**: iterate the DB and restore all
  persisted entries (2.52); already-loaded state unchanged (3.19).
- **`contract_state_sync.cpp`**: use proper key encoding and report actual storage
  size and chunk count (2.53).
- **`metrics.cpp RecordOpcodeExecution`**: track per-opcode counts with
  thread-safe access (2.54).
- **`backward_compat.cpp`**: query the trust-graph DB and compare scores against
  the defined tolerance (2.55).
- **`tx_priority.cpp` / `blockprocessor.cpp` address extraction** (pseudo-address
  from prevout hash): extract the real address from the tx inputs via the UTXO set
  (2.56). This is consensus-adjacent where it feeds block processing; like the
  other consensus-critical fixes it is applied directly.
- **`commit_reveal.cpp`**: use an explicit commit-phase-start field (2.57).
- **`clusterupdatehandler.cpp`**: use the actual linking address for cluster
  merges (2.58).

### Workstream 11 — Security monitoring RPC

**Clauses:** 1.59  **Target:** 2.59  **Preservation:** 3.21 (non-consensus)
- **`security_rpc.cpp getvalidatorstats_security`**: return real per-validator
  statistics (total/accurate/inaccurate validations, abstentions, accuracy rate,
  reputation, last activity) from the HAT consensus system, populating the fields
  its help text documents (replace the static `message` placeholder). Other
  security RPCs unchanged (3.21).

### Workstream 12 — Graceful degradation

**Clauses:** 1.15, 1.62  **Targets:** 2.15, 2.62  **Preservation:** 3.24 (non-consensus)
- **`graceful_degradation.cpp`**: implement real `CheckMemoryUsage`,
  `CheckCPUUsage`, `CheckStorageUsage`; invoke the real reputation subsystem for
  reputation queries/health checks and the real trust-context / HAT-validation
  subsystems for those fallback paths; report success/failure from the actual
  result (2.15, 2.62). Genuinely healthy subsystems still report success (3.24).

## Testing Strategy

### Validation Approach

Two phases. **Phase A (exploratory):** on the UNFIXED code, write tests that
demonstrate each defect (fail on unfixed code) to confirm the root cause; if a
test unexpectedly passes, the root-cause hypothesis for that clause is refuted
and must be re-analyzed. **Phase B (fix + preservation):** after each fix, verify
the bug is fixed for buggy inputs (fix checking) and that non-buggy inputs behave
identically (preservation checking).

Existing suites apply: C++ unit tests in `src/test/` run via `make check` or
`src/test/test_cascoin --run_test=<suite>`; Python functional tests in
`test/functional/` via `test/functional/test_runner.py` (e.g. `feature_cvm.py`).
New CVM unit suites should follow the `test_cascoin` boost-test convention.

### Exploratory Bug Condition Checking

**Goal:** Surface counterexamples on unfixed code to confirm/refute root causes.

**Representative test cases (fail on unfixed code):**
1. Block with subsidies exceeding the per-block max is accepted (1.1).
2. Deploy with gasPrice≠1 charged 1:1 (1.2).
3. `cvmtx.cpp` deploy produces `txHash[0:20]` address ≠ canonical address (1.16).
4. Deploy/call in block processing leaves contract un-executed / gas = gasLimit (1.17).
5. Reputation vote attributed to zero address (1.18).
6. Failed-block state not rolled back — leaked writes remain (1.21).
7. `OP_VERIFY_SIG` with a forged signature pushes 1 (1.23).
8. `OP_BALANCE` pushes 0 for a funded account (1.24).
9. Reputation signature of ≥64 bytes but invalid passes verification (1.6).
10. `getvalidatorstats_security` returns the static message, not real stats (1.59).

**Expected counterexamples:** placeholder/hardcoded outputs (0, true, empty,
`txHash[0:20]`, static message) diverging from the Expected Behavior clause.

### Fix Checking

**Goal:** For all inputs where the bug condition holds, the fixed function
produces the specified behavior.

**Pseudocode:**
```
FOR ALL input WHERE isBugCondition(input) DO
  result := fixedFunction(input)
  ASSERT specifiedBehavior(result)          // per clause 2.x
END FOR
```

### Preservation Checking

**Goal:** For all inputs where the bug condition does NOT hold, the fixed function
produces the same result as the original.

**Pseudocode:**
```
FOR ALL input WHERE NOT isBugCondition(input) DO
  ASSERT originalFunction(input) = fixedFunction(input)
END FOR
```

**Testing approach:** Property-based testing is recommended for preservation
because it generates many inputs across the domain and catches edge cases manual
tests miss. Capture legacy behavior on the UNFIXED code first (golden vectors for
standard/WoT transactions, valid signatures, already-correct loads), then assert
the fixed code reproduces it.

**Preservation test cases:**
1. Standard transaction validation/mempool acceptance identical (3.10).
2. WoT transactions remain non-contract, 100% fee to miner (3.3).
3. Valid secp256k1 signatures still accepted by `OP_VERIFY_SIG` family (3.11, 3.4).
4. Canonical deployer+nonce addresses unchanged (3.14).
5. Receipt JSON identical except added `logsBloom` (3.13).
6. Blocks with no CVM/EVM content and the primary blockprocessor path (except the
   corrected value/block-hash/commit) validate/save/finalize as before (3.12, 3.17, 3.2).
7. Zero-value deploy/call still exposes `CALLVALUE = 0` (3.22).

### Unit Tests

- Per-opcode handler tests: `OP_VERIFY_SIG`/ECDSA/QUANTUM, `OP_BALANCE`,
  `OP_CALL`, `OP_LOG` (Workstream 2).
- Address-derivation equality: `cvmtx.cpp` path == canonical `contract.cpp`
  scheme; EVM CREATE == `keccak256(rlp([sender,nonce]))[12:]` (1.16, 1.27).
- Subsidy accumulation / per-block-max rejection boundary (1.1).
- Gas-cost = gasUsed × gasPrice, including the 1:1 boundary case (1.2, 3.1).
- State save/rollback atomicity: failed block leaves no leaked writes (1.21).
- Signature/proof verification accept-valid / reject-forged (1.5, 1.6, 1.7).
- RPC field population for `getvalidatorstats_security` (1.59).

### Property-Based Tests

- **Fix property (Property 8):** for random (msg, key) pairs, `OP_VERIFY_SIG`
  pushes 1 iff the signature genuinely verifies.
- **Fix property (Property 1):** for random blocks of subsidized txs, the block
  is rejected iff the accumulated subsidy exceeds the per-block max.
- **Fix property (Property 3/Property 13):** for random (deployer, nonce),
  `cvmtx` and canonical addresses agree; EVM CREATE matches the Ethereum vector.
- **Preservation property (Property 21):** for random standard/WoT transactions
  and non-flagged inputs, fixed and original validation results are identical.
- **Fix property (Property 6):** for random blocks with a forced execution
  failure, the committed state after validation equals the pre-block state.

Each numbered property in the Correctness Properties section maps to at least one
property-based test; the PBT task list will reference these property numbers.

### Integration Tests

- Functional (`test/functional/feature_cvm.py` and new cases): deploy → call a
  contract across the `cvmtx.cpp` and `blockprocessor.cpp` paths and assert the
  same address, execution, value/block-hash context, and durable state (1.16,
  1.17, 1.60, 1.61, 3.12).
- Multi-node HAT consensus: challenge dispatch, response accumulation, and
  consensus decision across nodes (1.8, 1.47).
- Coinbase 70/30 split enforcement once participation data is present (1.22).
- Cross-chain proof verification round-trip against committed source state (1.33,
  1.34, 1.35).
