# Implementation Plan

## Overview

This plan turns the Trust System Activation design into an incremental, test-driven set of
coding tasks following the bugfix methodology: first surface counterexamples on the
**unfixed** code (exploration), then capture the behavior to preserve, then apply the
chosen fix — **Alternative B**: a slim, non-contract-only dispatch
(`CVMBlockProcessor::ProcessNonContractBlock`) called from `ConnectBlock` in the
durable-write phase next to `ProcessClusterUpdates` — and finally lock behavior with
fix-check and preservation tests and validate end-to-end.

The bug: the on-chain WoT write path is dead code. `CVMBlockProcessor::ProcessBlock()` is
disabled in `ConnectBlock()` (it re-executed contract work and hung the node), and
`BlockValidator::ValidateBlock()` skips every non-contract CVM type. As a result no
canonical record is persisted for `TRUST_EDGE`, `BONDED_VOTE`, `DAO_DISPUTE`, `DAO_VOTE`,
or `REPUTATION_VOTE` transactions.

Conventions:
- Property-based / optional test tasks are marked with `*` per workflow convention.
- Property tasks use the `**Property N: Type**` format to enable hover status. Property
  numbering follows the design's Correctness Properties (P1–P4 = Bug Condition / fix
  checks; P5 = Preservation).
- New unit tests follow the existing WoT patterns
  (`src/test/cvm_wot_fix_explore_tests.cpp`, `src/test/cvm_wot_preserve_tests.cpp`,
  `src/test/cvm_wot_fix_property_tests.cpp`; Boost.Test, binary `src/test/test_cascoin`)
  and are registered in `src/Makefile.test.include` under `BITCOIN_TESTS`.
- Functional tests live in `test/functional/` and are registered in
  `test/functional/test_runner.py`, following `test/functional/feature_wot_fixes.py`
  (regtest mining needs `-powalgo=sha256d` and a high `maxtries`; CVM activates at regtest
  height 0 and quantum at height 1).
- Build system is Autotools; the unit-test binary is `test_cascoin` (NOT `test_bitcoin`).

## Tasks

- [x] 1.* Write exploratory bug-condition tests that reproduce ALL defects on the UNFIXED code
  - **Property 1: Bug Condition** - On-chain WoT/reputation records not persisted on block connect
  - **CRITICAL**: These tests MUST FAIL on the current (unfixed) code — the failures confirm the bug exists (on-chain records are not persisted).
  - **DO NOT attempt to fix the tests or the code when they fail here.** These tests encode the expected behavior and will validate the fix when they pass later.
  - **GOAL**: Surface concrete counterexamples for every in-scope defect (design "Exploratory Bug Condition Checking"), confirming the root cause (non-contract dispatch disabled in `ConnectBlock`; `BlockValidator::ValidateBlock()` `continue`s past non-contract types).
  - **Scoped PBT Approach**: these are deterministic block-connection defects, so scope the property to the concrete failing cases (one mined block per non-contract op type) for reproducibility.
  - Because the write path is a consensus-adjacent block-connection step, add an **end-to-end functional regtest** `test/functional/feature_trust_activation.py` (following `test/functional/feature_wot_fixes.py`: single wallet, `-powalgo=sha256d`, high `maxtries`, legacy mining address for spendable bonds/fees) and register it in `test/functional/test_runner.py`. Add unit tests where a smaller seam exists (see below).
  - Functional test case 1 — Trust edge not persisted: `sendtrustrelation "<B>" 80`, mine and connect → `listtrustrelations` `count == 0` and no canonical `trust_<A>_<B>` edge exists. Document the counterexample. (Bug 1.1, 1.2, 1.3 — validates 2.1, 2.2)
  - Functional test case 2 — Trust chain has no paths: build `A → B → C → D` exclusively with `sendtrustrelation`, mine → `getweightedreputation "<D>" "<A>" 3` returns `paths_found: 0`, `reputation: 0`. Contrast with the equivalent `addtrust`-built graph which yields `paths_found >= 1`. Document the counterexample. (Bug 1.8 — validates 2.3)
  - Functional test case 3 — Reputation vote not applied: read `getreputation "<T>"`, `sendcvmvote "<T>" 100 "reason"`, mine → `getreputation "<T>"` is unchanged. Document the counterexample. (Bug 1.4 — validates 2.4)
  - Functional test case 4 — Bonded vote / DAO dispute / DAO vote not persisted: mine each (`sendbondedvote`, DAO dispute, DAO vote) and assert the corresponding record (`vote_<txid>` / `dispute_<txid>` / DAO vote) is absent via the read/DAO RPCs. Document the counterexamples. (Bugs 1.5, 1.6, 1.7 — validates 2.5, 2.6, 2.7)
  - Unit test (smaller seam) — Non-contract dispatch absent: create `src/test/cvm_trust_activation_explore_tests.cpp` (registered in `src/Makefile.test.include`), build a block containing one `TRUST_EDGE` (and one `REPUTATION_VOTE`) CVM `OP_RETURN` tx, run the current `ConnectBlock` non-contract path, and assert no canonical `trust_<from>_<to>` edge / no reputation change is persisted (mirrors the disabled `ProcessBlock`). Document the counterexample.
  - Run the functional test with `test/functional/test_runner.py feature_trust_activation.py` and the unit test with `src/test/test_cascoin --run_test=cvm_trust_activation_explore_tests`.
  - **EXPECTED OUTCOME**: Tests FAIL (this is correct — it proves the bug exists). Mark this task complete when the tests are written, run, and the failures/counterexamples are documented.
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8_

---

## Preservation property tests (capture the unfixed baseline BEFORE applying the fix)

- [x] 2.* Write preservation property tests for all non-bug-condition behavior
  - **Property 5: Preservation** - Contract path, cluster updates, direct write, rejection, gating, fJustCheck, reorg idempotency, propagation coexistence, and completed WoT behavior unchanged
  - **IMPORTANT**: Follow observation-first methodology — observe/record current correct behavior on inputs where `isBugCondition` is false on the UNFIXED node, then assert it is unchanged.
  - Property-based testing is recommended: generate many block/tx shapes across the input domain (contract-only blocks, CVM-free blocks, `fJustCheck` blocks, pre-activation blocks, invalid-payload blocks).
  - Create `src/test/cvm_trust_activation_preserve_tests.cpp` (registered in `src/Makefile.test.include`), following `src/test/cvm_wot_preserve_tests.cpp` and `src/test/cvm_workstream2_preserve_tests.cpp`.
  - Contract path unchanged: a block with `CONTRACT_DEPLOY`/`CONTRACT_CALL`/`EVM_DEPLOY`/`EVM_CALL` executes exactly once via `BlockValidator::ValidateBlock()` with identical gas/subsidy/state/success-failure semantics; the non-contract dispatch does not touch it. (3.1)
  - `ProcessClusterUpdates()` results are identical for the same block state. (3.2)
  - No hang / bounded time: connection time for WoT-heavy blocks is comparable to the unfixed contract-only path. (3.3)
  - `addtrust` (direct write) stores/lists/traverses edges identically. (3.4)
  - Invalid input rejected/ignored: on-chain edge with weight outside −100..+100, insufficient bond, or an unparseable payload persists no record and does not fail validation for the rest of the block. (3.5)
  - Soft-fork gating / CVM-free block: pre-activation blocks and blocks with no CVM `OP_RETURN` transactions connect exactly as today. (3.6)
  - `fJustCheck`: validating a block with non-contract CVM txs under `fJustCheck` writes no durable WoT/reputation state. (3.7)
  - Completed `web-of-trust-fixes` behavior: WoT read APIs, quantum/wide-address support, identity resolution, and viewer echo are unchanged for inputs unaffected by this fix. (3.8)
  - Shared serialization round-trips: contracts, contract state, nonces, and propagated-edge index records (`trust_prop_*` / `trust_prop_idx_*`) still round-trip correctly. (3.9)
  - Run on the UNFIXED baseline and confirm PASS; these will be re-run after the fix to confirm no regressions.
  - **EXPECTED OUTCOME**: Tests PASS (they confirm behavior to preserve).
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

---

## The fix — slim non-contract-only dispatch (Alternative B)

- [x] 3. Fix — activate on-chain non-contract CVM record persistence on block connect

  - [x] 3.1 Change 1 — Add a non-contract-only dispatch to `CVMBlockProcessor`
    - In `src/cvm/blockprocessor.h` / `src/cvm/blockprocessor.cpp`, add `static void ProcessNonContractBlock(const CBlock& block, int height, CVMDatabase& db);`.
    - Implementation: iterate `block.vtx`, skip coinbase, `FindCVMOpReturn` + `ParseCVMOpReturn`, and dispatch **only** the five non-contract op types (`TRUST_EDGE`, `BONDED_VOTE`, `DAO_DISPUTE`, `DAO_VOTE`, `REPUTATION_VOTE`) to the existing handlers (`ProcessTrustEdge`, `ProcessBondedVote`, `ProcessDAODispute`, `ProcessDAOVote`, `ProcessVote`).
    - Explicitly SKIP the four contract types (`CONTRACT_DEPLOY`, `CONTRACT_CALL`, `EVM_DEPLOY`, `EVM_CALL`) and unknown types — they are `BlockValidator`'s responsibility and MUST NOT be re-executed here (this avoids the `TrustContext`/`SecureHAT`/Enhanced-VM hang that caused the original `ProcessBlock()` disable).
    - Add a thin `ProcessNonContractTransaction` helper (or refactor `ProcessTransaction` to skip contract types) to avoid duplicating the parse/switch.
    - Wrap per-tx processing in a defensive `try/catch` so no exception escapes `ConnectBlock`; a handler that rejects an invalid record (bad weight/bond/payload) logs and returns without persisting and without aborting the rest of the block.
    - _Bug_Condition: isBugCondition(block) — a connected block (fJustCheck == false) on a CVM-active chain contains a non-contract CVM OP_RETURN tx (`TRUST_EDGE`/`BONDED_VOTE`/`DAO_DISPUTE`/`DAO_VOTE`/`REPUTATION_VOTE`)_
    - _Expected_Behavior: expectedBehavior — each non-contract CVM tx's persistence handler is invoked so its canonical record is persisted (Properties 1, 2); no contract deploy/call re-executed (Property 4)_
    - _Preservation: contract path and invalid-input rejection unchanged (Property 5)_
    - _Requirements: 2.1, 2.4, 2.5, 2.6, 2.7, 2.9, 3.1, 3.5_

  - [x] 3.2 Change 2 — Call the new dispatch from `ConnectBlock()` in the durable-write phase
    - In `src/validation.cpp`, in the existing soft-fork block that currently runs only `ProcessClusterUpdates()` (after `if (fJustCheck) return true;`), call `CVM::CVMBlockProcessor::ProcessNonContractBlock(block, pindex->nHeight, *CVM::g_cvmdb)` **before** `ProcessClusterUpdates(...)`, so records exist before cluster propagation runs.
    - Keep the `CVM::IsCVMSoftForkActive(pindex->nHeight, chainparams.GetConsensus())` gate and the `CVM::g_cvmdb` null check; leave the existing error-log `else` branch unchanged.
    - Because this section is reached only when `fJustCheck == false`, no durable WoT/reputation write can occur during validation-only (satisfies 3.7 structurally, exactly like `ProcessClusterUpdates`).
    - Leave the contract path (`BlockValidator::ValidateBlock()`, earlier in `ConnectBlock`) and the coinbase/subsidy/validator-payment logic untouched.
    - _Bug_Condition: isBugCondition(block) — dispatch was disabled in ConnectBlock_
    - _Expected_Behavior: expectedBehavior — non-contract records persisted on connect, gated by soft fork, skipped under fJustCheck (Properties 1, 2, 4)_
    - _Preservation: ProcessClusterUpdates, soft-fork gating, fJustCheck, and contract path unchanged (Property 5)_
    - _Requirements: 2.1, 2.9, 3.1, 3.2, 3.3, 3.6, 3.7_

  - [x] 3.3 Change 3 — Make `REPUTATION_VOTE` application idempotent / reorg-safe
    - In `src/cvm/blockprocessor.cpp` (`ProcessVote`), add a processed-transaction marker so a given `REPUTATION_VOTE` tx is applied at most once: key `repvote_applied_<txid>`; if `db.ExistsGeneric(appliedKey)` return early, else apply the score delta and `db.WriteGeneric(appliedKey, {1})` (use the existing generic read/exists/write helpers).
    - Verify and note that the other four handlers are already idempotent upserts keyed by a unique identifier: `AddTrustEdge` → `trust_<from>_<to>` (overwrite), `RecordBondedVote` → `vote_<bondTxHash>` (overwrite), `CreateDispute` → `dispute_<txid>` (overwrite), `VoteOnDispute` → member-keyed `dispute.daoVotes[member]` with a `resolved` guard. No change needed for those four.
    - _Bug_Condition: isBugCondition(block) with a `REPUTATION_VOTE` tx reprocessed on reorg/reconnect_
    - _Expected_Behavior: expectedBehavior — the same non-contract tx persists its canonical record exactly once (idempotent), no double-counting (Property 3)_
    - _Preservation: single-application reputation semantics unchanged for first-time processing (Property 5)_
    - _Requirements: 2.8_

  - [x] 3.4 Change 4 — Verify/document the reconciliation invariant with `trust_prop_*`
    - No code change. Verify (and add a code comment / design note) that the canonical edge namespace (`trust_<from>_<to>`, written by `ProcessTrustEdge` → `AddTrustEdge`) and the off-chain propagation namespace (`trust_prop_*` / `trust_prop_idx_*`, written by `TrustPropagator::PropagateTrustEdge` at broadcast time) are disjoint, and that the canonical enumerators (`IsCanonicalForwardEdgeKey` in `listtrustrelations` / `GetGraphStats`) already exclude the propagated records.
    - Confirm the RPC and the on-chain edge agree on `from` (the RPC passes `resolvedFromAddress` from `BuildTrustTransaction` into propagation), so the on-chain canonical record agrees with — and does not double-count against — the off-chain side effect.
    - _Bug_Condition: isBugCondition(block) with a `TRUST_EDGE` tx whose RPC already wrote `trust_prop_*`_
    - _Expected_Behavior: expectedBehavior — canonical record agrees with off-chain propagation, no double-count (Property 3)_
    - _Preservation: propagated-edge index round-trip and enumeration counts unchanged (Property 5)_
    - _Requirements: 2.8, 3.9_

  - [x] 3.5 Verify the bug condition exploration tests now pass
    - **Property 1: Expected Behavior** - On-chain WoT/reputation records persisted on block connect
    - **IMPORTANT**: Re-run the SAME tests from Task 1 — do NOT write new tests. They encode the expected behavior; passing confirms the bug is fixed.
    - Run `test/functional/test_runner.py feature_trust_activation.py` and `src/test/test_cascoin --run_test=cvm_trust_activation_explore_tests`.
    - **EXPECTED OUTCOME**: Tests PASS (confirms the on-chain records are now persisted and readable).
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7_

  - [x] 3.6 Verify the preservation tests still pass
    - **Property 5: Preservation** - Non-bug-condition behavior unchanged
    - **IMPORTANT**: Re-run the SAME tests from Task 2 — do NOT write new tests.
    - Run `src/test/test_cascoin --run_test=cvm_trust_activation_preserve_tests`.
    - **EXPECTED OUTCOME**: Tests PASS (confirms no regressions in the contract path, cluster updates, direct write, rejection, gating, fJustCheck, or completed WoT behavior).
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

---

## Fix-check property / functional tests (map to Correctness Properties P1–P4)

- [x] 4.* Write fix-check property / functional tests P1–P4
  - **Property 2: Expected Behavior** - Fix Verification (Correctness Properties P1–P4)
  - **IMPORTANT**: These extend/parallel the exploration tests from Task 1; run AFTER the fix and confirm PASS. Prefer functional/regtest end-to-end for the persistence flow, plus unit tests for the dispatch/idempotency logic and property-based tests for generated block/tx shapes.
  - Add unit/property tests to `src/test/cvm_trust_activation_fix_property_tests.cpp` (new file, registered in `src/Makefile.test.include`) and extend `test/functional/feature_trust_activation.py`:
    - P1 — Trust edge persisted & traversable: after `sendtrustrelation` mined+connected, a canonical `trust_<from>_<to>` edge exists (signer as `from`, target as `to`, with payload weight/bond/bond-tx/timestamp); `listtrustrelations`/`gettrustgraphstats`/`getweightedreputation` enumerate/traverse it exactly as an `addtrust` edge; `A → B → C → D` built only with `sendtrustrelation` yields `paths_found >= 1` and non-zero reputation equal to the `addtrust`-built result. (Design P1 → 2.1, 2.2, 2.3)
    - P2 — Reputation/bonded/DAO records persisted: `sendcvmvote` updates `getreputation`; a valid `BONDED_VOTE` persists a bonded-vote record; a valid `DAO_DISPUTE` persists a listable/queryable dispute; a valid `DAO_VOTE` records the vote and resolves the dispute at the existing threshold. (Design P2 → 2.4, 2.5, 2.6, 2.7)
    - P3 — Idempotent, reorg-safe, reconciled with propagation: connect → disconnect → reconnect a block with each non-contract type; assert exactly one canonical record and no double-counted reputation; assert canonical edge count is not inflated by coexisting `trust_prop_*` records. (Design P3 → 2.8)
    - P4 — Bounded-time persistence without contract re-execution: a block mixing contract + non-contract CVM txs connects without hanging, and the non-contract dispatch does not re-execute contract deploy/call work. (Design P4 → 2.9)
  - Run with `src/test/test_cascoin --run_test=cvm_trust_activation_fix_property_tests` and `test/functional/test_runner.py feature_trust_activation.py`.
  - **EXPECTED OUTCOME**: Tests PASS (confirming the bug is fixed).
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9_

---

## Final build / verification checkpoint

- [x] 5. Checkpoint — Build and run all trust-activation test suites
  - Build the unit-test binary and daemon/CLI: `make -j$(nproc)` producing `src/test/test_cascoin`, `src/cascoind`, and `src/cascoin-cli` (Autotools; run `./autogen.sh && ./configure` first if needed).
  - Run the unit suites: `src/test/test_cascoin --run_test=cvm_trust_activation_explore_tests`, `cvm_trust_activation_preserve_tests`, `cvm_trust_activation_fix_property_tests`, and the existing `cvm_wot_*` suites (regression for the completed `web-of-trust-fixes` work).
  - Run the functional tests: `test/functional/test_runner.py feature_trust_activation.py` and `feature_wot_fixes.py` (regression).
  - Ensure all tests pass, and that block connection completes without hanging. Ask the user if questions arise.
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9_

---

## Notes

- The chosen fix is Alternative B (slim non-contract-only dispatch). Alternative A (extending `BlockValidator`) and re-enabling `ProcessBlock` verbatim are explicitly NOT planned — see the design's "Approach and alternatives considered".
- The fix is minimal: reuse the already-fixed handlers (which use `TrustNodeId` per `web-of-trust-fixes`), keep persistence in `blockprocessor.cpp`, satisfy `fJustCheck` structurally, and never touch the contract execution/gas/rollback path.
- Files touched by multiple tasks are sequenced to avoid conflicts (see the Task Dependency Graph): `src/cvm/blockprocessor.{h,cpp}` in 3.1 → 3.3 → 3.4; `src/validation.cpp` in 3.2.
- The exploration task (1) comes first and is expected to FAIL on unfixed code; the preservation task (2) is expected to PASS on unfixed code.
- Tasks marked `*` are optional/test tasks per workflow convention.

## Task Dependency Graph

```json
{
  "waves": [
    {
      "wave": 1,
      "description": "Exploration — reproduce all defects on unfixed code (must FAIL)",
      "tasks": ["1"]
    },
    {
      "wave": 2,
      "description": "Preservation baseline on unfixed code (must PASS)",
      "tasks": ["2"]
    },
    {
      "wave": 3,
      "description": "Change 1 — add ProcessNonContractBlock dispatch (src/cvm/blockprocessor.{h,cpp})",
      "tasks": ["3.1"]
    },
    {
      "wave": 4,
      "description": "Change 2 — wire dispatch into ConnectBlock (src/validation.cpp), depends on 3.1",
      "tasks": ["3.2"]
    },
    {
      "wave": 5,
      "description": "Change 3 — ProcessVote idempotency marker (src/cvm/blockprocessor.cpp, sequenced after 3.1)",
      "tasks": ["3.3"]
    },
    {
      "wave": 6,
      "description": "Change 4 — verify/document reconciliation invariant (no code change, sequenced after 3.1)",
      "tasks": ["3.4"]
    },
    {
      "wave": 7,
      "description": "Verify exploration + preservation tests post-fix",
      "tasks": ["3.5", "3.6"]
    },
    {
      "wave": 8,
      "description": "Fix-check property / functional tests P1–P4",
      "tasks": ["4"]
    },
    {
      "wave": 9,
      "description": "Final build + verification checkpoint",
      "tasks": ["5"]
    }
  ],
  "notes": [
    "Files touched by multiple tasks are sequenced to avoid conflicts: src/cvm/blockprocessor.{h,cpp} in 3.1 -> 3.3 -> 3.4; src/validation.cpp only in 3.2.",
    "The exploratory test task (1) comes first and is expected to FAIL on unfixed code; the preservation task (2) is expected to PASS on unfixed code.",
    "Tasks marked * are optional/test tasks per workflow convention.",
    "Only Alternative B is planned; extending BlockValidator (Alternative A) and re-enabling ProcessBlock verbatim are out of scope."
  ]
}
```
