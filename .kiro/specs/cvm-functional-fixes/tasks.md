# Implementation Plan

## Overview

This plan implements the 62 CVM functional fixes (`bugfix.md` clauses 1.1–1.62 /
2.1–2.62, preservation 3.1–3.24) organized by the design's 12 workstreams and 21
correctness properties (`design.md`). It follows the bugfix TDD flow per
workstream: **exploratory bug-condition test** (fails on unfixed code) →
**implement fix** → **fix-checking test** (passes after fix) → **preservation-checking
test** (passes on both unfixed and fixed code).

Consensus-critical fixes are applied directly and unconditionally: the CVM is not
yet live on any production network, so the corrected behavior simply replaces the
buggy behavior directly. Consensus-critical work is still called out in prose so
reviewers know where extra care is warranted.

Test binary is `src/test/test_cascoin` (boost-test). Functional tests live in
`test/functional/` and run via `test/functional/test_runner.py`.

## Tasks

### Phase 1 — Global exploratory bug-condition & preservation baselines

These two standalone suites are written and run **on the UNFIXED code** before any
fix, per the design Testing Strategy (Exploratory Bug Condition Checking and
Preservation Checking).

- [x] 1. Write global bug-condition exploration test suite
  - **Property 1: Bug Condition** — Representative CVM defect counterexamples
  - **IMPORTANT**: Write this before implementing any fix.
  - **GOAL**: Surface counterexamples that confirm each root-cause hypothesis. If any
    case unexpectedly passes, the root cause for that clause is refuted and must be
    re-analyzed (per design Phase A).
  - Add a new boost-test suite (e.g. `src/test/cvm_functional_fix_explore_tests.cpp`)
    covering the design's 10 representative cases:
    - Block with subsidies exceeding the per-block max is accepted (1.1).
    - Deploy with `gasPrice != 1` charged 1:1 (1.2).
    - `cvmtx.cpp` deploy produces `txHash[0:20]` address != canonical address (1.16).
    - Deploy/call in block processing leaves contract un-executed / `gas == gasLimit` (1.17).
    - Reputation vote attributed to the zero address (1.18).
    - Failed-block state not rolled back — leaked writes remain (1.21).
    - `OP_VERIFY_SIG` with a forged signature pushes 1 (1.23).
    - `OP_BALANCE` pushes 0 for a funded account (1.24).
    - Reputation signature of >=64 bytes but invalid passes verification (1.6).
    - `getvalidatorstats_security` returns the static message, not real stats (1.59).
  - Run on UNFIXED code with `src/test/test_cascoin --run_test=cvm_functional_fix_explore_tests`.
  - **EXPECTED OUTCOME**: Tests FAIL (placeholder/hardcoded outputs) — this confirms the bugs exist.
  - Document each counterexample.
  - _Requirements: 1.1, 1.2, 1.6, 1.16, 1.17, 1.18, 1.21, 1.23, 1.24, 1.59_

- [x] 2. Write global preservation baseline suite (BEFORE implementing fixes)
  - **Property 21: Preservation** — Standard/WoT/valid-signature behavior unchanged
  - **IMPORTANT**: Follow observation-first methodology — capture legacy behavior on
    the UNFIXED code as golden vectors, then assert the fixed code reproduces it.
  - Add a boost-test suite (e.g. `src/test/cvm_functional_fix_preserve_tests.cpp`) capturing:
    - Standard (non-CVM/EVM) transaction validation & mempool acceptance (3.10).
    - WoT transactions remain non-contract, 100% fee to miner (3.3).
    - Genuinely valid secp256k1 signatures accepted by `OP_VERIFY_SIG` family (3.11, 3.4).
    - Canonical deployer+nonce addresses (`contract.cpp`) (3.14).
    - Receipt JSON fields (3.13, excluding `logsBloom` which will change).
    - Zero-value deploy/call exposes `CALLVALUE = 0` (3.22).
  - Property-based generators for random standard/WoT transactions (Property 21).
  - Run on UNFIXED code.
  - **EXPECTED OUTCOME**: Tests PASS (baseline behavior to preserve).
  - _Requirements: 3.2, 3.3, 3.4, 3.10, 3.11, 3.13, 3.14, 3.22_

---

### Phase 2 — Workstream 1: Consensus-critical accounting & block processing

- [x] 3. Write Workstream-1 exploratory fix-property tests (BEFORE fix)
  - **Property 1: Bug Condition** — Per-block subsidy limit enforced
  - **Property 2: Bug Condition** — Gas cost = `gasUsed * gasPrice`
  - **Property 3: Bug Condition** — `cvmtx` contract address == canonical `GenerateContractAddress`
  - **Property 4: Bug Condition** — Constructor/contract code executed during block processing
  - **Property 5: Bug Condition** — Reputation vote attributed to real voter
  - **Property 6: Bug Condition** — Failed block state rolled back / accepted block persisted atomically
  - **Property 7: Bug Condition** — Coinbase 70/30 validator split enforced
  - **Property 19: Bug Condition** — Primary-path value/block-hash + `CommitExecutionState` flush
  - **IMPORTANT**: Write these property-based tests before implementing the fix; they
    encode Expected Behavior and validate the fix once passing.
  - **Scoped PBT Approach**: for random `(deployer, nonce)` assert `cvmtx` address ==
    canonical address (Property 3); for random subsidized blocks assert reject-iff-over-max
    (Property 1); for random `(gasUsed, gasPrice)` assert cost equality (Property 2);
    for random forced-fail blocks assert committed state == pre-block state (Property 6).
  - Run on UNFIXED code — **EXPECTED OUTCOME**: FAIL (confirms defects 1.1, 1.2, 1.16–1.22, 1.60, 1.61).
  - _Requirements: 1.1, 1.2, 1.16, 1.17, 1.18, 1.19, 1.20, 1.21, 1.22, 1.60, 1.61_

- [x] 4. Write Workstream-1 preservation tests (BEFORE fix)
  - **Property 21: Preservation** — 1:1-rate-equivalent fee/subsidy split unchanged (3.1)
  - Observe on UNFIXED code: blocks with no subsidies / no failed execution validate,
    save, and finalize identically (3.2, 3.17); canonical addresses unchanged (3.14);
    zero-value call exposes `CALLVALUE = 0` (3.22).
  - Property-based: random non-flagged inputs produce identical results before and after the fix.
  - Run on UNFIXED code — **EXPECTED OUTCOME**: PASS.
  - _Requirements: 3.1, 3.2, 3.14, 3.17, 3.22_

- [x] 5. Implement Workstream-1 fixes

  - [x] 5.1 Enforce per-block subsidy accumulation & max in `block_validator.cpp ValidateBlock`
    - Accumulate actual per-tx subsidy into a running total; reject (via `state.DoS`/
      `return false`) when total exceeds the `cvmMaxGasPerBlock`-derived subsidy max.
    - Replace `bool isBeneficial = true` with `GasSubsidyTracker::IsBeneficialOperation(trust)`
      and record actual `gasUsed` (not `gasLimit`).
    - _Bug_Condition: isBugCondition for 1.1, 1.19_
    - _Expected_Behavior: 2.1, 2.19 (accumulate real subsidy, reject over max, real benefit/gasUsed)_
    - _Preservation: 3.2, 3.17_
    - _Requirements: 2.1, 2.19_

  - [x] 5.2 Compute gas cost from actual gas & gas price
    - In `block_validator.cpp` / `fee_calculator.cpp`, compute `gasCost = gasUsed * gasPrice`
      instead of `cost = gasLimit`.
    - Verify fee against the transaction's input values in `VerifyReputationGasCosts`.
    - _Bug_Condition: isBugCondition for 1.2, 1.20_
    - _Expected_Behavior: 2.2, 2.20_
    - _Preservation: 3.1 (1:1-equivalent split unchanged)_
    - _Requirements: 2.2, 2.20_

  - [x] 5.3 Atomic state save / real rollback in `block_validator.cpp`
    - Route writes through a batch committed atomically on success and discarded
      (real revert) on failure, replacing the log-only `RollbackContractState` no-op.
    - _Bug_Condition: isBugCondition for 1.21_
    - _Expected_Behavior: 2.21_
    - _Preservation: 3.17, 3.23_
    - _Requirements: 2.21_

  - [x] 5.4 Fix contract address & execution in `cvmtx.cpp ProcessCVMBlock`
    - Replace `memcpy(contractAddr.begin(), txHash.begin(), 20)` with
      `GenerateContractAddress(deployer, nonce)` (deployer via `ExtractDeployerAddress`,
      nonce from DB).
    - Execute the constructor (deploy) / contract code (call) via the Enhanced VM and
      accumulate actual gas used instead of only `gasLimit`.
    - _Bug_Condition: isBugCondition for 1.16, 1.17_
    - _Expected_Behavior: 2.16, 2.17_
    - _Preservation: 3.14 (canonical scheme)_
    - _Requirements: 2.16, 2.17_

  - [x] 5.5 Resolve real voter in `cvmtx.cpp UpdateReputationScores`
    - Resolve the real voter from tx inputs (UTXO / `ExtractSenderAddress`) before
      `ApplyVote`; update per-participant behavior scores. Never attribute to zero address.
    - _Bug_Condition: isBugCondition for 1.18_
    - _Expected_Behavior: 2.18_
    - _Preservation: 3.3 (WoT tx semantics)_
    - _Requirements: 2.18_

  - [x] 5.6 Enforce coinbase 70/30 split in `validator_compensation.cpp CheckCoinbaseValidatorPayments`
    - Enforce the 70/30 validator payment split against validator participation data
      (replace the TODO success path).
    - _Bug_Condition: isBugCondition for 1.22_
    - _Expected_Behavior: 2.22_
    - _Requirements: 2.22_

  - [x] 5.7 Correct primary-path value/block-hash & durable commit
    - In `blockprocessor.cpp` primary path (~lines 292/404): pass the actual tx value
      and the real block hash to the Enhanced VM instead of `0` / `uint256()`.
    - In `enhanced_vm.cpp CommitExecutionState`: flush pending contract-state writes to
      the DB instead of logging only.
    - Reconcile the `cvmtx.cpp` and `blockprocessor.cpp` paths so address derivation,
      execution, value/block-hash context, and state commit agree.
    - _Bug_Condition: isBugCondition for 1.60, 1.61_
    - _Expected_Behavior: 2.60, 2.61_
    - _Preservation: 3.12, 3.22 (zero-value CALLVALUE == 0), 3.23_
    - _Requirements: 2.60, 2.61_

  - [x] 5.8 Verify Workstream-1 fix-property tests now pass
    - **Property 1/2/3/4/5/6/7/19: Expected Behavior**
    - Re-run the SAME tests from task 3.
    - **EXPECTED OUTCOME**: PASS (bugs fixed).
    - _Requirements: 2.1, 2.2, 2.16, 2.17, 2.18, 2.19, 2.20, 2.21, 2.22, 2.60, 2.61_

  - [x] 5.9 Verify Workstream-1 preservation tests still pass
    - **Property 21: Preservation**
    - Re-run the SAME tests from task 4. Confirm non-flagged inputs are identical.
    - **EXPECTED OUTCOME**: PASS (no regressions).
    - _Requirements: 3.1, 3.2, 3.12, 3.14, 3.17, 3.22, 3.23_

---

### Phase 3 — Workstream 2: Core-VM opcode handlers

- [x] 6. Write Workstream-2 exploratory fix-property tests (BEFORE fix)
  - **Property 8: Bug Condition** — `OP_VERIFY_SIG` family enforces real verification
  - **Property 9: Bug Condition** — `OP_BALANCE`, `OP_CALL`, `OP_LOG` functional
  - **Scoped PBT Approach**: for random `(msg, key)` pairs, `OP_VERIFY_SIG` pushes 1
    iff the signature genuinely verifies (Property 8).
  - Cover: forged signature pushes 1 (1.23); `OP_BALANCE` pushes 0 for funded account
    (1.24); `OP_CALL`/`CallContract` returns "not fully implemented" (1.25); `OP_LOG`
    consumes nothing / emits nothing (1.26).
  - Run on UNFIXED code — **EXPECTED OUTCOME**: FAIL.
  - _Requirements: 1.23, 1.24, 1.25, 1.26_

- [x] 7. Write Workstream-2 preservation tests (BEFORE fix)
  - **Property 21: Preservation** — Valid signatures still accepted; SLOAD/SSTORE & supported opcodes unchanged
  - Observe on UNFIXED code: genuinely valid signatures push 1 (3.11); persistent
    storage SLOAD/SSTORE semantics (3.7); already-supported opcodes/context (3.9).
  - Run on UNFIXED code — **EXPECTED OUTCOME**: PASS.
  - _Requirements: 3.7, 3.9, 3.11_

- [x] 8. Implement Workstream-2 fixes

  - [x] 8.1 Enforce `OP_VERIFY_SIG` / `OP_VERIFY_SIG_ECDSA` / `OP_VERIFY_SIG_QUANTUM`
    - In `cvm.cpp` (~lines 446, 450, 482, 514): extract message, signature, pubkey from
      the stack; perform real secp256k1 (ECDSA) or FALCON-512 verification (quantum, when
      `DEPLOYMENT_QUANTUM` active); push 1 only on a genuinely valid signature.
    - _Bug_Condition: isBugCondition for 1.23_
    - _Expected_Behavior: 2.23_
    - _Preservation: 3.11 (valid sigs still push 1)_
    - _Requirements: 2.23_

  - [x] 8.2 Implement `OP_BALANCE`, `OP_CALL`/`CallContract`, `OP_LOG`
    - `OP_BALANCE`: query account balance (UTXO/state) and push it (not 0).
    - `HandleCall`/`CallContract`: load and execute the target with proper gas/state, or
      fail deterministically with a defined error.
    - `OP_LOG`: pop topic count, topics, and data; emit a log entry.
    - _Bug_Condition: isBugCondition for 1.24, 1.25, 1.26_
    - _Expected_Behavior: 2.24, 2.25, 2.26_
    - _Preservation: 3.7, 3.9_
    - _Requirements: 2.24, 2.25, 2.26_

  - [x] 8.3 Verify Workstream-2 fix-property tests now pass
    - **Property 8/9: Expected Behavior** — re-run the SAME tests from task 6.
    - **EXPECTED OUTCOME**: PASS.
    - _Requirements: 2.23, 2.24, 2.25, 2.26_

  - [x] 8.4 Verify Workstream-2 preservation tests still pass
    - **Property 21: Preservation** — re-run the SAME tests from task 7.
    - **EXPECTED OUTCOME**: PASS.
    - _Requirements: 3.7, 3.9, 3.11_

---

### Phase 4 — Non-consensus & remaining workstreams

### Workstream 3 — Reputation signatures & merkle proofs

- [-] 9. Write Workstream-3 exploratory + preservation tests (BEFORE fix)
  - **Property 10: Bug Condition** — real validator signature + committed state root/proof
  - Explore (fail on unfixed): placeholder signature = first 32 bytes of proof hash and
    state root from `fixedString + time` (1.5); length-only signature check passes forged
    >=64-byte sig (1.6); fabricated sibling hash in merkle proof (1.7).
  - **Property 21: Preservation** — merkle verification math for genuinely committed leaves (3.6).
  - _Requirements: 1.5, 1.6, 1.7, 3.6_

- [ ] 10. Implement Workstream-3 fixes
  - [ ] 10.1 Real signing, state root, verification, and merkle proof
    - `reputation_signature.cpp`: sign proof data with the validator key (reuse existing
      secp256k1 path from 3.4/3.18); derive state root from the committed reputation state
      tree; replace length-only check with ECDSA verification against the signer's pubkey;
      build merkle proofs from the real reputation state tree.
    - _Bug_Condition: isBugCondition for 1.5, 1.6, 1.7_
    - _Expected_Behavior: 2.5, 2.6, 2.7_
    - _Preservation: 3.6_
    - _Requirements: 2.5, 2.6, 2.7_
  - [ ] 10.2 Verify Workstream-3 fix-property test passes and preservation holds
    - **Property 10: Expected Behavior** / **Property 21: Preservation** — re-run task 9 tests.
    - _Requirements: 2.5, 2.6, 2.7, 3.6_

### Workstream 4 — HAT v2 distributed consensus

- [ ] 11. Write Workstream-4 exploratory + preservation tests (BEFORE fix)
  - **Property 11: Bug Condition** — real task validation, P2P challenge, response accumulation
  - Explore (fail on unfixed): `isValid=true`/80% without validation (1.3); trust score
    hardcoded 50 (1.4); challenge returns success with no P2P send (1.8); dispute sets
    self-reported == calculated (1.9); `ProcessValidatorResponse` only logs (1.47).
  - **Property 21: Preservation** — deterministic Fisher-Yates selection (3.5) and existing
    ECDSA sign/verify of responses (3.4, 3.18) unchanged.
  - _Requirements: 1.3, 1.4, 1.8, 1.9, 1.47, 3.4, 3.5, 3.18_

- [ ] 12. Implement Workstream-4 fixes
  - [ ] 12.1 Real validation, trust score, P2P challenge, dispute, response accumulation
    - `hat_consensus.cpp` / `consensus_validator.cpp`: perform actual task validation and
      derive `isValid`/confidence; compute trust score from the trust graph; transmit a
      real P2P challenge and report success only when dispatched; use the validator's
      actual self-reported score in disputes.
    - `mempool_manager.cpp ProcessValidatorResponse`: accumulate responses in the
      validation session and evaluate consensus.
    - _Bug_Condition: isBugCondition for 1.3, 1.4, 1.8, 1.9, 1.47_
    - _Expected_Behavior: 2.3, 2.4, 2.8, 2.9, 2.47_
    - _Preservation: 3.4, 3.5, 3.18_
    - _Requirements: 2.3, 2.4, 2.8, 2.9, 2.47_
  - [ ] 12.2 Verify Workstream-4 fix-property test passes and preservation holds
    - **Property 11: Expected Behavior** / **Property 21: Preservation** — re-run task 11 tests.
    - _Requirements: 2.3, 2.4, 2.8, 2.9, 2.47, 3.4, 3.5, 3.18_

### Workstream 5 — Fee / gas / subsidy accounting (consensus-adjacent)

- [ ] 13. Write Workstream-5 exploratory + preservation tests (BEFORE fix)
  - **Property 12: Bug Condition** — subsidy-before-fee, real benefit, real sender, live load/rate, rebate transfer, allowance restore, DB init
  - Explore (fail on unfixed): subsidy skipped for non-free-gas tx (1.10); `reputation>=80`
    benefit check (1.40); empty sender (1.41); hardcoded load 50 / fixed rate (1.42);
    `callerReputation>=70` (1.43); rebate counters without transfer (1.44); allowance not
    restored at startup (1.45); mempool priority not DB-initialized (1.46).
  - **Property 21: Preservation** — free-gas zero-fee path (3.8), 1:1-equivalent split (3.1),
    dual-eligible subsidy still granted (3.16).
  - _Requirements: 1.10, 1.40, 1.41, 1.42, 1.43, 1.44, 1.45, 1.46, 3.1, 3.8, 3.16_

- [ ] 14. Implement Workstream-5 fixes
  - [ ] 14.1 Fee/gas/subsidy real inputs
    - `mempool_priority.cpp`: apply applicable subsidy before effective fee; initialize with
      the CVM database. `fee_calculator.cpp`: real network-benefit assessment; resolve real
      sender via validation UTXO set; derive load from live mempool and rate from configured
      pricing source. `sustainable_gas.cpp IsBeneficialOperation`: real benefit assessment.
      `gas_subsidy.cpp`: transfer/credit rebates and serialize all record fields.
      `gas_allowance.cpp LoadAllowanceStates`: iterate DB and restore state.
    - _Bug_Condition: isBugCondition for 1.10, 1.40–1.46_
    - _Expected_Behavior: 2.10, 2.40, 2.41, 2.42, 2.43, 2.44, 2.45, 2.46_
    - _Preservation: 3.1, 3.8, 3.16_
    - _Requirements: 2.10, 2.40, 2.41, 2.42, 2.43, 2.44, 2.45, 2.46_
  - [ ] 14.2 Verify Workstream-5 fix-property test passes and preservation holds
    - **Property 12: Expected Behavior** / **Property 21: Preservation** — re-run task 13 tests.
    - _Requirements: 2.10, 2.40, 2.41, 2.42, 2.43, 2.44, 2.45, 2.46, 3.1, 3.8, 3.16_

### Workstream 6 — EVM compatibility

- [ ] 15. Write Workstream-6 exploratory + preservation tests (BEFORE fix)
  - **Property 13: Bug Condition** — TLOAD/TSTORE, BASEFEE, EVM CREATE, logsBloom, sender/gas, reputation/memory, nested frame, storage proof
  - Explore (fail on unfixed): no TLOAD/TSTORE handlers (1.11); BASEFEE returns 0 (1.12);
    CREATE uses `Hash160(sender||nonce)` (1.27); `logsBloom` 512 zero chars (1.28); empty
    sender + fixed-constant gas estimate (1.29); no caller reputation injection (1.30);
    unpopulated nested `ExecutionFrame` (1.31); hash-based (non-MPT) storage proof (1.32).
  - **Property 21: Preservation** — supported opcodes/context unchanged (3.9); receipt JSON
    fields unchanged except `logsBloom` (3.13); CVM-native `contract.cpp` deployer+nonce
    scheme unchanged — only EVM CREATE changes (3.14).
  - Include an Ethereum CREATE golden vector: `keccak256(rlp([sender, nonce]))[12:]`.
  - _Requirements: 1.11, 1.12, 1.27, 1.28, 1.29, 1.30, 1.31, 1.32, 3.9, 3.13, 3.14_

- [ ] 16. Implement Workstream-6 fixes
  - [ ] 16.1 EVM compatibility features
    - `enhanced_vm.cpp`/EVM host: register TLOAD/TSTORE with per-tx lifetime + post-tx clear;
      populate BASEFEE. `nonce_manager.cpp GenerateContractAddress` (~line 97): compute
      `keccak256(rlp([sender, nonce]))[12:]`; reconcile `evmc_host.cpp` CREATE to the same
      scheme. `receipt.cpp ToJSON`: compute `logsBloom` from log addresses/topics.
      `evm_rpc.cpp`: resolve real sender and estimate gas from real execution.
      `evm_engine.cpp`: inject caller reputation and enforce trust-tagged memory policy.
      `enhanced_vm.cpp` nested-frame save: capture real `ExecutionFrame`.
      `enhanced_storage.cpp`: build/verify a Merkle Patricia Trie storage proof.
    - _Bug_Condition: isBugCondition for 1.11, 1.12, 1.27, 1.28, 1.29, 1.30, 1.31, 1.32_
    - _Expected_Behavior: 2.11, 2.12, 2.27, 2.28, 2.29, 2.30, 2.31, 2.32_
    - _Preservation: 3.9, 3.13, 3.14_
    - _Requirements: 2.11, 2.12, 2.27, 2.28, 2.29, 2.30, 2.31, 2.32_
  - [ ] 16.2 Verify Workstream-6 fix-property test passes and preservation holds
    - **Property 13: Expected Behavior** / **Property 21: Preservation** — re-run task 15 tests.
    - _Requirements: 2.11, 2.12, 2.27, 2.28, 2.29, 2.30, 2.31, 2.32, 3.9, 3.13, 3.14_

### Workstream 7 — Cross-chain bridging & oracle trust

- [ ] 17. Write Workstream-7 exploratory + preservation tests (BEFORE fix)
  - **Property 14: Bug Condition** — proof verified vs. source state; real sends; trie-derived proofs; oracle registry
  - Explore (fail on unfixed): non-empty-only proof check (1.33); send only logs/stores
    locally (1.34); simplified hash proof + cached-only attestations (1.35); accept-any
    oracle pubkey (1.36).
  - **Property 21: Preservation** — valid signature + committed source state still accepted (3.15).
  - _Requirements: 1.33, 1.34, 1.35, 1.36, 3.15_

- [ ] 18. Implement Workstream-7 fixes
  - [ ] 18.1 Real cross-chain verification, sends, proofs, oracle registry
    - `cross_chain_bridge.cpp ReputationProof::Verify`: verify against source chain committed
      state. LayerZero/CCIP send: transmit via endpoint, report success only when dispatched.
      Merkle proof/attestation read: derive from actual state trie, return all committed
      attestations. `trust_context.cpp IsKnownLayerZeroOracle`: check against a trusted-oracle
      registry for the chain.
    - _Bug_Condition: isBugCondition for 1.33, 1.34, 1.35, 1.36_
    - _Expected_Behavior: 2.33, 2.34, 2.35, 2.36_
    - _Preservation: 3.15_
    - _Requirements: 2.33, 2.34, 2.35, 2.36_
  - [ ] 18.2 Verify Workstream-7 fix-property test passes and preservation holds
    - **Property 14: Expected Behavior** / **Property 21: Preservation** — re-run task 17 tests.
    - _Requirements: 2.33, 2.34, 2.35, 2.36, 3.15_

### Workstream 8 — Distributed-consensus signatures & state sync

- [ ] 19. Write Workstream-8 exploratory + preservation tests (BEFORE fix)
  - **Property 15: Bug Condition** — attestation sig verified vs. attestor pubkey; real deltas; verify/apply vs. real state
  - Explore (fail on unfixed): 64–128-byte length-only attestation check (1.37); empty delta,
    no DB query / no peer request (1.38); verify/apply returns false when no validator
    configured (1.39).
  - **Property 21: Preservation** — valid attestations with committed state still accepted (3.15).
  - _Requirements: 1.37, 1.38, 1.39, 3.15_

- [ ] 20. Implement Workstream-8 fixes
  - [ ] 20.1 Real attestation verification, delta computation, and state sync
    - `consensus_safety.cpp`: verify attestation signatures against the attestor's pubkey;
      compute trust-graph deltas by querying the DB and request deltas from the peer.
      `trust_graph_sync.cpp`: verify/apply against real state instead of failing when no
      validator is configured.
    - _Bug_Condition: isBugCondition for 1.37, 1.38, 1.39_
    - _Expected_Behavior: 2.37, 2.38, 2.39_
    - _Preservation: 3.15_
    - _Requirements: 2.37, 2.38, 2.39_
  - [ ] 20.2 Verify Workstream-8 fix-property test passes and preservation holds
    - **Property 15: Expected Behavior** / **Property 21: Preservation** — re-run task 19 tests.
    - _Requirements: 2.37, 2.38, 2.39, 3.15_

### Workstream 9 — Sybil-resistance & fraud detection (non-consensus, unconditional)

- [ ] 21. Write Workstream-9 exploratory + preservation tests (BEFORE fix)
  - **Property 16: Bug Condition** — cluster/rapid-fire/exchange detection, reputation index, address tx lookup real
  - Explore (fail on unfixed): empty cluster results/false (1.13); rapid-fire always false
    (1.14); `DetectExchangePattern` always false (1.48); `GetAddressesWithReputation` empty
    (1.49); `GetTransactionsForAddress` empty (1.50).
  - **Property 21: Preservation** — trust-graph manipulation detection & behavior-metric
    scoring unchanged (3.20).
  - _Requirements: 1.13, 1.14, 1.48, 1.49, 1.50, 3.20_

- [ ] 22. Implement Workstream-9 fixes
  - [ ] 22.1 Real Sybil/fraud detection
    - `walletcluster.cpp GetTransactionsForAddress`: return the address's transactions from a
      transaction/address index; apply common-input-ownership heuristic to cluster addresses.
      `reputation.cpp`: `DetectExchangePattern` true when volume matches; `GetAddressesWithReputation`
      backed by a maintained reputation index. Rapid-fire detection: true when history matches.
    - _Bug_Condition: isBugCondition for 1.13, 1.14, 1.48, 1.49, 1.50_
    - _Expected_Behavior: 2.13, 2.14, 2.48, 2.49, 2.50_
    - _Preservation: 3.20_
    - _Requirements: 2.13, 2.14, 2.48, 2.49, 2.50_
  - [ ] 22.2 Verify Workstream-9 fix-property test passes and preservation holds
    - **Property 16: Expected Behavior** / **Property 21: Preservation** — re-run task 21 tests.
    - _Requirements: 2.13, 2.14, 2.48, 2.49, 2.50, 3.20_

### Workstream 10 — Storage / state sync & miscellaneous

- [ ] 23. Write Workstream-10 exploratory + preservation tests (BEFORE fix)
  - **Property 17: Bug Condition** — prune, load, size/count, metrics, backward-compat, address extraction, commit-phase, cluster-merge real
  - Explore (fail on unfixed): `PruneReceipts` only logs (1.51); `LoadBlacklist` no DB iterate
    (1.52); `storageSize=0`/`chunkCount=1` (1.53); opcode metrics skipped (1.54); backward-compat
    returns true without querying/comparing (1.55); pseudo-address from prevout hash (1.56);
    `createdTime` stand-in for commit-phase-start (1.57); first-cluster canonical address as
    link (1.58).
  - **Property 21: Preservation** — already-loaded persisted state unchanged (3.19);
    out-of-scope detectors unchanged (3.20).
  - _Requirements: 1.51, 1.52, 1.53, 1.54, 1.55, 1.56, 1.57, 1.58, 3.19, 3.20_

- [ ] 24. Implement Workstream-10 fixes
  - [ ] 24.1 Storage/state sync & misc real operations
    - `cvmdb.cpp PruneReceipts`: delete receipts below the given height.
      `access_control_audit.cpp LoadBlacklist`: iterate DB, restore all entries.
      `contract_state_sync.cpp`: proper key encoding, actual storage size and chunk count.
      `metrics.cpp RecordOpcodeExecution`: thread-safe per-opcode counts.
      `backward_compat.cpp`: query trust-graph DB, compare scores against tolerance.
      `tx_priority.cpp`/`blockprocessor.cpp`: extract real address from tx inputs via UTXO set.
      `commit_reveal.cpp`: explicit commit-phase-start field.
      `clusterupdatehandler.cpp`: actual linking address for merges.
    - _Bug_Condition: isBugCondition for 1.51–1.58_
    - _Expected_Behavior: 2.51, 2.52, 2.53, 2.54, 2.55, 2.56, 2.57, 2.58_
    - _Preservation: 3.19, 3.20_
    - _Requirements: 2.51, 2.52, 2.53, 2.54, 2.55, 2.56, 2.57, 2.58_
  - [ ] 24.2 Verify Workstream-10 fix-property test passes and preservation holds
    - **Property 17: Expected Behavior** / **Property 21: Preservation** — re-run task 23 tests.
    - _Requirements: 2.51, 2.52, 2.53, 2.54, 2.55, 2.56, 2.57, 2.58, 3.19, 3.20_

### Workstream 11 — Security monitoring RPC (non-consensus, unconditional)

- [ ] 25. Write Workstream-11 exploratory + preservation tests (BEFORE fix)
  - **Property 18: Bug Condition** — `getvalidatorstats_security` returns real per-validator stats
  - Explore (fail on unfixed): RPC returns the static `message` placeholder, not documented
    fields (1.59).
  - **Property 21: Preservation** — other security-monitoring RPCs unchanged (3.21).
  - _Requirements: 1.59, 3.21_

- [ ] 26. Implement Workstream-11 fix
  - [ ] 26.1 Populate real validator statistics
    - `security_rpc.cpp getvalidatorstats_security`: return real per-validator stats
      (total/accurate/inaccurate validations, abstentions, accuracy rate, reputation, last
      activity) from the HAT consensus system, populating the help-text fields.
    - _Bug_Condition: isBugCondition for 1.59_
    - _Expected_Behavior: 2.59_
    - _Preservation: 3.21_
    - _Requirements: 2.59_
  - [ ] 26.2 Verify Workstream-11 fix-property test passes and preservation holds
    - **Property 18: Expected Behavior** / **Property 21: Preservation** — re-run task 25 tests.
    - _Requirements: 2.59, 3.21_

### Workstream 12 — Graceful degradation (non-consensus, unconditional)

- [ ] 27. Write Workstream-12 exploratory + preservation tests (BEFORE fix)
  - **Property 20: Bug Condition** — real resource checks + real TRUST_CONTEXT/HAT_VALIDATION subsystems
  - Explore (fail on unfixed): reputation query/health check returns simulated success (1.15);
    empty no-op resource checks + fallback paths report success without invoking real
    subsystems (1.62).
  - **Property 21: Preservation** — genuinely healthy subsystems still report success (3.24).
  - _Requirements: 1.15, 1.62, 3.24_

- [ ] 28. Implement Workstream-12 fix
  - [ ] 28.1 Real health checks and subsystem invocation
    - `graceful_degradation.cpp`: implement real `CheckMemoryUsage`/`CheckCPUUsage`/
      `CheckStorageUsage`; invoke the real reputation subsystem for reputation queries/health
      checks and the real trust-context / HAT-validation subsystems for fallback paths; report
      success/failure from the actual result.
    - _Bug_Condition: isBugCondition for 1.15, 1.62_
    - _Expected_Behavior: 2.15, 2.62_
    - _Preservation: 3.24_
    - _Requirements: 2.15, 2.62_
  - [ ] 28.2 Verify Workstream-12 fix-property test passes and preservation holds
    - **Property 20: Expected Behavior** / **Property 21: Preservation** — re-run task 27 tests.
    - _Requirements: 2.15, 2.62, 3.24_

---

### Phase 5 — Integration validation

- [ ] 29. Functional dual-path deploy → call integration test
  - Extend `test/functional/feature_cvm.py` (and/or add a new case): deploy then call a
    contract across the `cvmtx.cpp` and `blockprocessor.cpp` paths; assert identical address,
    execution, value/block-hash context, and durable state.
  - _Requirements: 2.16, 2.17, 2.60, 2.61, 3.12_

- [ ] 30. Multi-node HAT consensus + coinbase-split + cross-chain integration tests
  - Multi-node: challenge dispatch, response accumulation, consensus decision (1.8, 1.47).
  - Coinbase 70/30 split enforcement once participation data is present (1.22).
  - Cross-chain proof verification round-trip against committed source state (1.33, 1.34, 1.35).
  - _Requirements: 2.8, 2.22, 2.33, 2.34, 2.35, 2.47_

- [ ] 31. Checkpoint — Ensure all tests pass
  - Build: `make -j$(nproc)`. Unit: `make check` (or targeted `src/test/test_cascoin --run_test=<suite>`).
    Functional: `test/functional/test_runner.py`.
  - Confirm all fix-property tests (Properties 1–20) pass, the preservation
    tests (Property 21) pass, and the exploratory suites now pass after their fixes.
    Ask the user if questions arise.
  - _Requirements: all_

---

## Task Dependency Graph

```json
{
  "waves": [
    { "wave": 1, "tasks": ["1", "2"], "rationale": "Global bug-condition and preservation baselines captured on UNFIXED code." },
    { "wave": 2, "tasks": ["3", "4", "6", "7"], "rationale": "Consensus-critical exploratory + preservation tests (WS1, WS2) before fixes." },
    { "wave": 3, "tasks": ["5", "8"], "rationale": "Consensus-critical implementations WS1, WS2." },
    { "wave": 4, "tasks": ["9", "11", "13", "15", "17", "19", "21", "23", "25", "27"], "rationale": "Independent workstream (WS3-12) exploratory + preservation tests; may run in parallel." },
    { "wave": 5, "tasks": ["10", "12", "14", "16", "18", "20", "22", "24", "26", "28"], "rationale": "Independent workstream (WS3-12) implementations; may run in parallel." },
    { "wave": 6, "tasks": ["29", "30"], "rationale": "Integration validation across implemented workstreams." },
    { "wave": 7, "tasks": ["31"], "rationale": "Final checkpoint: full build + unit + functional test suites pass." }
  ]
}
```

```
1 (global bug-condition explore) ── run on UNFIXED code
2 (global preservation baseline) ── run on UNFIXED code
   │
   ▼
Phase 2  WS1 consensus accounting/block proc:  3 → 4 → 5
Phase 3  WS2 consensus opcode handlers:        6 → 7 → 8
   │
   ▼  (Phase 4 workstreams are largely independent; may run in parallel)
WS3  reputation sig/proofs:      9 → 10
WS4  HAT v2 consensus:           11 → 12
WS5  fee/gas/subsidy:            13 → 14
WS6  EVM compatibility:          15 → 16
WS7  cross-chain bridging:       17 → 18
WS8  distributed consensus sync: 19 → 20
WS9  Sybil/fraud (uncond.):      21 → 22
WS10 storage/misc:               23 → 24
WS11 security RPC (uncond.):     25 → 26
WS12 graceful degradation:       27 → 28
   │
   ▼  (Phase 5 integration — depends on the workstreams it exercises)
29 dual-path integration        (dep: 5, 8)
30 multi-node/coinbase/xchain   (dep: 5, 12, 18)
31 checkpoint                   (dep: all)
```

**Critical path:** `(1,2) → (3,4) → 5 → 8 → 29/30 → 31`. Workstreams 3, 4, 6, 7, 8,
9, 11, 12 can proceed in parallel once the global baselines (1, 2) exist. Each
`write tests` task must run (and produce the expected fail/pass outcome) on UNFIXED
code before its paired implementation task begins.

## Notes

- **Consensus safety:** Consensus-critical fixes are applied directly and
  unconditionally — the CVM is not yet live on any production network, so the
  corrected behavior simply replaces the buggy behavior (there is no legacy path to
  preserve). Divergence between the `cvmtx.cpp` and `blockprocessor.cpp` execution
  paths is itself a consensus fault and must still be reconciled for consistency
  (see task 5.7).
- **TDD discipline:** Each `write tests` task runs on the UNFIXED code first.
  Bug-condition/exploration tests MUST fail (confirming the defect); preservation
  tests MUST pass (capturing baseline). If an exploration test unexpectedly passes,
  the root-cause hypothesis for that clause is refuted and must be re-analyzed before
  implementing.
- **Property mapping:** Fix properties are design Properties 1–20 (`isBugCondition ⇒
  specified behavior`); preservation is Property 21 (non-flagged inputs unchanged).
  `**Property N: Type**` labels drive hover status.
- **Property-based tests** are used for Properties 1, 3, 6, 8, 13, 21 per the
  design Testing Strategy; other properties use targeted unit/functional tests.
- **Build & test commands:** `make -j$(nproc)`; unit `src/test/test_cascoin
  --run_test=<suite>` or `make check`; functional `test/functional/test_runner.py`.
  The test binary is `test_cascoin` (not `test_bitcoin`).
- **Scope:** Only coding/testing tasks are listed.
