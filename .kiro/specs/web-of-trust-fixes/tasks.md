# Implementation Plan

## Overview

This plan turns the Web-of-Trust Fixes design into an incremental, test-driven set of
coding tasks. It follows the bugfix methodology: first surface counterexamples on the
**unfixed** code (exploration), then apply the two-tier fixes (Tier A: no format change;
Tier B: versioned, backward-compatible quantum/address-width support), then lock behavior
with preservation and fix-check property tests, and finally validate end-to-end.

Conventions:
- Property-based / optional test tasks are marked with `*` per workflow convention.
- Property tasks use the `**Property N: Type**` format to enable hover status.
- New unit tests follow the existing pattern in
  `src/test/cvm_workstream1_fix_property_tests.cpp` and `src/test/trustpropagator_tests.cpp`
  (Boost.Test, binary `src/test/test_cascoin`) and are registered in
  `src/Makefile.test.include` under `BITCOIN_TESTS`.
- Build system is Autotools; unit-test binary is `test_cascoin`, functional tests live in
  `test/functional/`.

## Tasks

- [x] 1.* Write exploratory bug-condition tests that reproduce ALL defects on the UNFIXED code
  - **Property 1: Bug Condition** - WoT Defect Reproduction (round-trip corruption, count mismatch, wrong `from`, zero paths, zero reputation, address-type rejection, hex viewer)
  - **CRITICAL**: These tests MUST FAIL on the current (unfixed) code — the failures confirm the bugs exist.
  - **DO NOT attempt to fix the tests or the code when they fail here.** These tests encode the expected behavior and will validate the fixes when they pass later.
  - **GOAL**: Surface concrete counterexamples for every in-scope bug condition (design "Exploratory Bug Condition Checking").
  - Create a new test file `src/test/cvm_wot_fix_explore_tests.cpp` and register it in `src/Makefile.test.include` (`BITCOIN_TESTS`), following the pattern of `src/test/cvm_workstream1_fix_property_tests.cpp`.
  - **Scoped PBT Approach**: for deterministic reproductions use concrete stored graphs; use property generators where the design calls for random edges/DAGs.
  - Test case 1 — Foreign-record corruption: add one canonical edge via `TrustGraph::AddTrustEdge`, then create `trust_prop_*`/`trust_prop_idx_*` via `TrustPropagator`; call the `listtrustrelations` enumeration path and assert every returned field (`fromAddress`, `toAddress`, `trustWeight`, `timestamp`, `bondAmount`, `bondTxHash`, `slashed`, `reason`) equals what was written and `reason` is valid UTF-8. Document counterexamples (e.g. `trustWeight` read as `11291`, `bondAmount` as `~6.9e11`, far-future timestamp, `slashed=1`, non-UTF-8 `reason`). (Bug 1 — 2.1, 2.2, 2.3)
  - Test case 2 — Count mismatch: for the same state assert `TrustGraph::GetGraphStats().total_trust_edges` equals the forward-edge count enumerated by `listtrustrelations`. Document counterexample (stats over-counts `trust_prop_*`, e.g. `6` vs `1`). (Bug 2 — 2.7)
  - Test case 3 — Placeholder/wrong `from`: exercise `addtrust` write path and assert the stored `from` equals the creating address (not all-zeros); exercise `sendtrustrelation`/`BuildTrustTransaction` and assert the RPC-side propagated `from` equals the on-chain signer identity. Document counterexample (all-zeros placeholder / reserve-key mismatch). (Bug 1 — 2.4)
  - Test case 4 — Zero trust paths: build canonical chain `A→B→C→D` (weights 80) and assert `getweightedreputation("D","A",3).paths_found >= 1`. Document counterexample (`paths_found: 0`). (Bug 2 — 2.5)
  - Test case 5 — Zero reputation despite a path: single canonical edge `B→C`, viewer `B`, assert non-zero weighted reputation at `maxdepth 1`. Document counterexample (`reputation: 0` due to no `BondedVote` records). (Bug 2 — 2.5, 2.6)
  - Test case 6 — Address-type rejection: call each WoT RPC decode path with a bech32 P2WSH (`WitnessV0ScriptHash`) address and a quantum (`WitnessV2Quantum`, `rcasq…`) address; assert no `"Address type not supported"` error. Document counterexample (`error code -5`). (Bug 3 — 2.9, 2.12)
  - Test case 7 — Hex viewer echo: assert `getweightedreputation` echoes the supplied base58 `viewer` string, not `uint160` hex. Document counterexample (hash160 hex). (Bug 2 — 2.8)
  - Run all exploration tests on UNFIXED code with `src/test/test_cascoin --run_test=cvm_wot_fix_explore_tests`.
  - **EXPECTED OUTCOME**: Tests FAIL (this is correct — it proves the bugs exist). Mark this task complete when the tests are written, run, and the failures/counterexamples are documented.
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 1.10, 1.12_

---

## Tier A — Read-path / identity fixes (no on-disk or on-chain format change)

- [x] 2. Change A1 — Filter canonical forward edges in the enumerators
  - [x] 2.1 Add the `IsCanonicalForwardEdgeKey` helper
    - Add `bool IsCanonicalForwardEdgeKey(const std::string& key)` in the `CVM` namespace, declared in `src/cvm/trustgraph.h` and defined in `src/cvm/trustgraph.cpp`.
    - Return true only when the key `starts_with("trust_")` AND does NOT contain `"trust_in_"` AND does NOT `starts_with("trust_prop_")` (this also excludes `trust_prop_idx_`).
    - Prefer prefix checks (`rfind(prefix, 0) == 0`) over `find(...) != npos` to avoid substring false positives.
    - _Bug_Condition: isBugCondition reads_foreign — enumeration matches `trust_prop_*`/`trust_prop_idx_*` under the `trust_` prefix_
    - _Expected_Behavior: enumerators consider only canonical `trust_<from>_<to>` keys (Property 1)_
    - _Requirements: 2.1, 2.7_
  - [x] 2.2 Apply the helper in `TrustGraph::GetGraphStats`
    - In `src/cvm/trustgraph.cpp`, replace the `if (key.find("trust_in_") == npos) trustEdgeCount++;` logic with `if (IsCanonicalForwardEdgeKey(key)) trustEdgeCount++;`.
    - _Bug_Condition: isBugCondition reads_foreign for `gettrustgraphstats`_
    - _Expected_Behavior: `total_trust_edges` counts only canonical forward edges (Property 2)_
    - _Requirements: 2.7_
  - [x] 2.3 Apply the helper in `listtrustrelations` with full-stream-consume defensive check
    - In `src/rpc/cvm.cpp` (`listtrustrelations`), replace `if (key.find("trust_in_") != npos) continue;` with `if (!CVM::IsCanonicalForwardEdgeKey(key)) continue;`.
    - Keep the existing `try/catch` around `ss >> edge` and additionally require the stream to be fully consumed (reject records with trailing bytes) so any future foreign record is skipped rather than silently misread.
    - _Bug_Condition: isBugCondition reads_foreign for `listtrustrelations`_
    - _Expected_Behavior: only canonical edges deserialized; exact fields; valid UTF-8 `reason`; parseable CLI reply (Property 1)_
    - _Requirements: 2.1, 2.2, 2.3_

- [x] 3. Change A2 — Correct the `from` identity on the write paths
  - [x] 3.1 Extend `BuildTrustTransaction` to output the resolved from-identity
    - In `src/cvm/txbuilder.h` / `src/cvm/txbuilder.cpp`, add an out-parameter (e.g. `uint160& outFromAddress` / `TrustNodeId& outFrom`) so the RPC learns the exact signer key (`GetKeyFromPool` key) embedded in the on-chain `CVMTrustEdgeData`.
    - _Bug_Condition: isBugCondition wrong_from — RPC propagation uses a different key than the on-chain edge_
    - _Expected_Behavior: RPC and on-chain record agree on `from` (Property 1)_
    - _Requirements: 2.4_
  - [x] 3.2 Fix `sendtrustrelation` to propagate from the signer identity
    - In `src/rpc/cvm.cpp` (`sendtrustrelation`), set `edge.fromAddress` from the value returned by `BuildTrustTransaction` (the on-chain signer) instead of `pwallet->GetAllReserveKeys().begin()->first`.
    - _Bug_Condition: isBugCondition wrong_from for `sendtrustrelation`_
    - _Expected_Behavior: stored/propagated `from` equals the creating address (Property 1); enables traversal keyed on `from` (Property 2)_
    - _Requirements: 2.4, 2.5, 2.6_
  - [x] 3.3 Fix `addtrust` placeholder from-identity
    - In `src/rpc/cvm.cpp` (`addtrust`), remove `uint160 fromAddress; // Placeholder`; resolve the caller/signer address (add a required/optional `"from"` argument or derive from the wallet default address) and reject when it cannot be resolved rather than storing zeros. Decode it via the unified address path (depends on Task 6, Change B2).
    - _Bug_Condition: isBugCondition wrong_from for `addtrust`_
    - _Expected_Behavior: stored `from` equals the creating address (Property 1); single-wallet edges work (Property 4)_
    - _Requirements: 2.4, 2.11_
  - [x] 3.4 Pass the resolved from-identity through the block processor
    - In `src/cvm/blockprocessor.cpp` (`ProcessTrustEdge`), ensure the signer/from identity decoded from the OP_RETURN is passed through to `TrustGraph::AddTrustEdge` unchanged.
    - _Bug_Condition: isBugCondition wrong_from at block-processing time_
    - _Expected_Behavior: on-chain edge keyed under the correct `from` (Property 1, Property 2)_
    - _Requirements: 2.4, 2.5_

- [x] 4. Change A3 — Derive reputation from trust paths in `GetWeightedReputation`
  - In `src/cvm/trustgraph.cpp` (`TrustGraph::GetWeightedReputation`, `viewer != target` branch): when paths exist, compute a non-zero score from the trust-path weights (path-strength-weighted aggregate of the final-hop weight, normalized to the same scale as the self-view average). Continue incorporating `BondedVote` records at the target when present, but do NOT require them for a non-zero result.
  - Preserve the `viewer == target` self-view branch exactly (average of non-slashed incoming trust).
  - _Bug_Condition: isBugCondition zero_when_path — path exists but reputation is 0_
  - _Expected_Behavior: non-zero weighted reputation derived from path weights (Property 2)_
  - _Preservation: self-view branch unchanged (Property 5)_
  - _Requirements: 2.5, 2.6, 3.5_

- [x] 5. Change A4 — Echo viewer as base58 in `getweightedreputation`
  - In `src/rpc/cvm.cpp` (`getweightedreputation`), replace `result.pushKV("viewer", viewerAddress.ToString())` with the base58 form (echo the supplied `viewer` string or `EncodeDestination` of the resolved viewer). Apply the same fix to any hop/address fields that currently emit `uint160::ToString()` where a displayable address is expected.
  - _Bug_Condition: isBugCondition viewer_is_hex_
  - _Expected_Behavior: `viewer` echoed as base58 (Property 2)_
  - _Requirements: 2.8_

---

## Tier B — Versioned, backward-compatible quantum / address-width support

- [x] 6. Change B1 — Introduce the canonical wide `TrustNodeId` type
  - [x] 6.1 Create `src/cvm/trustnodeid.h` / `src/cvm/trustnodeid.cpp`
    - Define `enum class TrustNodeType : uint8_t { P2PKH=1, P2SH=2, P2WPKH=3, P2WSH=4, QUANTUM=5 }` and `struct TrustNodeId { uint8_t type; uint256 data; ... }` in the `CVM` namespace.
    - Implement `static bool FromDestination(const CTxDestination&, TrustNodeId&)` (false for unsupported/`CNoDestination`), `CTxDestination ToDestination() const`, `std::string ToKeyString() const` (tagged, e.g. `"<type:02x>-<data:64hex>"`, non-colliding with legacy 40-hex segments), and `ADD_SERIALIZE_METHODS` (`READWRITE(type); READWRITE(data);`).
    - uint160 types (P2PKH/P2SH/P2WPKH) zero-extend into the low 20 bytes; P2WSH/QUANTUM use the full 32 bytes. Match `base58.cpp`/quantum LE/BE handling.
    - Add the new files to the appropriate library sources in `src/Makefile.am` (CVM sources).
    - _Bug_Condition: isBugCondition — non-`uint160` destinations cannot be represented_
    - _Expected_Behavior: lossless, reversible wide identifier for all five standard types (Property 3)_
    - _Requirements: 2.12, 3.10_
  - [x] 6.2* Unit test `TrustNodeId` round-trip for all five destination types
    - **Property 3: Bug Condition** - TrustNodeId Lossless Round-Trip
    - Create `src/test/trustnodeid_tests.cpp` and register it in `src/Makefile.test.include`.
    - Property: for each of `CKeyID`, `CScriptID`, `WitnessV0KeyHash`, `WitnessV0ScriptHash`, `WitnessV2Quantum`, `FromDestination` then `ToDestination` returns the original destination, and `ToKeyString` is stable and collision-free across types.
    - Include serialization round-trip (serialize then deserialize `TrustNodeId`) and LE/BE handling for quantum.
    - _Requirements: 2.10, 2.12, 3.10_

- [x] 7. Change B2 — Unified `DecodeTrustNode` helper applied across all WoT RPCs
  - [x] 7.1 Add the `DecodeTrustNode` helper
    - In `src/rpc/cvm.cpp`, add `bool DecodeTrustNode(const std::string& addr, CVM::TrustNodeId& out, std::string& err)` that uses the existing `DecodeDestination`/`IsQuantumAddress`/`EncodeDestination` (unchanged) and accepts `CKeyID`, `CScriptID`, `WitnessV0KeyHash`, `WitnessV0ScriptHash`, and `WitnessV2Quantum`; throw `RPC_INVALID_ADDRESS_OR_KEY` with a precise message otherwise.
    - _Bug_Condition: isBugCondition — WoT RPCs reject valid non-`uint160` destinations_
    - _Expected_Behavior: every standard destination accepted and mapped to `TrustNodeId` (Property 3)_
    - _Requirements: 2.9_
  - [x] 7.2 Apply `DecodeTrustNode` in every WoT RPC
    - Replace the repeated `boost::get<CKeyID>/…` branches in `addtrust`, `getweightedreputation`, `listtrustrelations`, `sendtrustrelation`, `sendbondedvote`, `votereputation`, `getreputation`, and any other WoT RPC with `DecodeTrustNode`.
    - _Bug_Condition: isBugCondition — per-RPC `"Address type not supported"`_
    - _Expected_Behavior: uniform acceptance of all supported types (Property 3)_
    - _Preservation: existing `DecodeDestination`/`IsQuantumAddress`/`EncodeDestination` behavior unchanged (Property 6)_
    - _Requirements: 2.9, 3.9_

- [x] 8. Change B3 — Versioned `TrustEdge` keyed by `TrustNodeId`
  - [x] 8.1 Update `TrustEdge` structure and versioned serialization
    - In `src/cvm/trustgraph.h`, change `TrustEdge.fromAddress`/`toAddress` from `uint160` to `TrustNodeId`, add a leading `uint8_t nVersion`, and implement the versioned `SerializationOp`: detect legacy v1 (no version byte, fixed 54-byte-equivalent layout) and migrate on read to `TrustNodeId{P2PKH, uint160-zero-extended}`; write/read v2 with `nVersion >= 2`.
    - _Bug_Condition: isBugCondition — 20-byte fields cannot hold 32-byte identifiers_
    - _Expected_Behavior: edges store the full 32-byte identifier without truncation (Property 3)_
    - _Preservation: legacy v1 DB records still readable (Property 6)_
    - _Requirements: 2.12, 3.3_
  - [x] 8.2 Update DB key format with v1 back-compat read
    - Update the key builder to `"trust_" + from.ToKeyString() + "_" + to.ToKeyString()` and the reverse/`GetOutgoingTrust` prefixes accordingly. Read paths MUST accept both shapes: a 40-hex segment → legacy `P2PKH`/`uint160` node; a tagged segment → `TrustNodeId`.
    - _Bug_Condition: isBugCondition — `uint160` keys cannot key quantum edges_
    - _Expected_Behavior: lossless keying; retrievable by the same address (Property 3)_
    - _Preservation: legacy keys still resolve (Property 6)_
    - _Requirements: 2.10, 2.12, 3.3, 3.10_
  - [x] 8.3 Update `TrustGraph` methods to `TrustNodeId` with a `uint160` wrapper overload
    - In `src/cvm/trustgraph.cpp`, update `AddTrustEdge`, `GetTrustEdge`, `GetOutgoingTrust`, `GetIncomingTrust`, `FindTrustPaths`, `FindPathsRecursive`, `GetWeightedReputation`, and `TrustPath`/`visited` sets from `uint160` to `TrustNodeId`. Keep a thin `uint160` overload that wraps `TrustNodeId{P2PKH, ...}` for existing callers (e.g. `IsDAOMember`, HAT/consensus helpers) to minimize blast radius.
    - _Bug_Condition: isBugCondition — traversal/storage limited to `uint160`_
    - _Expected_Behavior: store/list/traverse for all supported types (Property 3, Property 4)_
    - _Preservation: existing `uint160` callers behave identically (Property 5, Property 6)_
    - _Requirements: 2.10, 2.11, 2.12, 3.3_

- [x] 9. Change B4 — Versioned on-chain `CVMTrustEdgeData`
  - [x] 9.1 Add versioned payload with v1 detect + v2 emit-only-when-needed
    - In `src/cvm/softfork.h` / `src/cvm/softfork.cpp`, define a v2 `CVMTrustEdgeData` payload beginning with a version byte and encoding `from`/`to` as `TrustNodeId`. `Deserialize` MUST detect legacy v1 (fixed 54 bytes, no version byte, from/to as `uint160` → `P2PKH`) and parse it exactly as today, and parse v2 only when the version byte (`>= 2`) is present. Emit v2 ONLY when a `P2WSH`/quantum node is involved; continue emitting v1 for pure-`uint160` edges.
    - _Bug_Condition: isBugCondition — on-chain payload is `uint160`-only_
    - _Expected_Behavior: quantum/P2WSH edges representable on-chain (Property 3)_
    - _Preservation: historical v1 OP_RETURNs decode identically (Property 6)_
    - _Requirements: 2.9, 2.12, 3.9_
  - [x] 9.2 Pass `TrustNodeId` through `ProcessTrustEdge`
    - In `src/cvm/blockprocessor.cpp` (`ProcessTrustEdge`), decode the versioned payload into `TrustNodeId` and pass it through to `AddTrustEdge`.
    - _Bug_Condition: isBugCondition — block processor truncates to `uint160`_
    - _Expected_Behavior: on-chain quantum/P2WSH edges stored losslessly (Property 3)_
    - _Preservation: v1 payloads processed as before (Property 6)_
    - _Requirements: 2.9, 2.12, 3.9_

---

## Preservation property tests (BEFORE relying on the fixes; capture on unfixed baseline where applicable)

- [x] 10.* Write preservation property tests for shared serialization and legacy round-trips
  - **Property 2: Preservation** - Non-Bug-Condition Behavior Unchanged (serialization, validation, traversal, self-view)
  - **IMPORTANT**: Follow observation-first methodology — observe/record current correct behavior on inputs where `isBugCondition` is false, then assert it is unchanged.
  - Create `src/test/cvm_wot_preserve_tests.cpp` and register it in `src/Makefile.test.include`, following `src/test/cvm_workstream2_preserve_tests.cpp`.
  - Legacy P2PKH round-trip: random legacy edges store/list/traverse identically. (3.3)
  - `PropagatedTrustEdge` round-trip: random propagated edges still serialize/deserialize byte-for-byte (only the canonical enumerators stop reading them). (3.7)
  - `BondedVote` / `DAODispute` / reputation-record round-trip unchanged. (3.7)
  - Validation filters: out-of-range weight, insufficient bond, `maxdepth` outside `1..10` still rejected with the same messages. (3.1, 3.2, 3.6)
  - Traversal filters: edges with `trustWeight < 10`, slashed edges, and visited nodes (cycles) still skipped. (3.4)
  - Self-view: `viewer == target` still returns the average of non-slashed incoming trust. (3.5)
  - Existing quantum handling: `DecodeDestination`/`IsQuantumAddress`/`EncodeDestination` outputs unchanged for the same inputs. (3.9)
  - Cluster fields: `cluster_id`/`cluster_size`/`edges_propagated` unchanged for unaffected inputs. (3.8)
  - Legacy v1 on-chain payload: 54-byte `CVMTrustEdgeData` decodes to the same edge as before. (3.9)
  - Run on the baseline and confirm PASS; re-run after the fixes and confirm still PASS (no regressions).
  - **EXPECTED OUTCOME**: Tests PASS (they confirm behavior to preserve).
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10_

---

## Fix-check property tests (map to Correctness Properties P1–P6)

- [x] 11.* Write fix-check property tests P1–P6
  - **Property 1: Expected Behavior** - Fix Verification (Correctness Properties P1–P6)
  - **IMPORTANT**: These extend/parallel the exploration tests from Task 1; run AFTER the fixes and confirm PASS.
  - Add tests to `src/test/cvm_wot_fix_property_tests.cpp` (new file, registered in `src/Makefile.test.include`), one test per property where practical:
    - P1 — Canonical edge round-trip & enumeration integrity: random canonical edges interleaved with random `trust_prop_*`/`trust_prop_idx_*`; `listtrustrelations` returns exactly the canonical set with exact fields and valid UTF-8 `reason`. (Design Property 1 → 2.1, 2.2, 2.3, 2.4)
    - P2 — Path traversal & reputation: random DAGs of canonical edges and random viewer/target/depth; a path is found iff one exists within depth, reputation is non-zero when a qualifying path exists, `total_trust_edges == listtrustrelations count`, viewer echoed as base58. (Design Property 2 → 2.5, 2.6, 2.7, 2.8)
    - P3 — All standard address types lossless: random destinations across all five types accepted, stored under `TrustNodeId`, retrievable by the same address. (Design Property 3 → 2.9, 2.10, 2.12)
    - P4 — Single-wallet trust graph: edges among one wallet's addresses store/list/traverse. (Design Property 4 → 2.11)
    - P5 — Preservation of validation/traversal filters/self-view/shared serialization (cross-check with Task 10). (Design Property 5 → 3.1, 3.2, 3.4, 3.5, 3.6, 3.7)
    - P6 — Preservation of legacy addresses/existing quantum handling/clustering/keying consistency (cross-check with Task 10). (Design Property 6 → 3.3, 3.8, 3.9, 3.10)
  - Run with `src/test/test_cascoin --run_test=cvm_wot_fix_property_tests`.
  - **EXPECTED OUTCOME**: Tests PASS (confirming the bugs are fixed).
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12_

---

## Verify exploration tests now pass

- [x] 12. Re-run the exploration tests from Task 1 and confirm they now pass
  - **Property 1: Expected Behavior** - WoT Defect Reproduction (now resolved)
  - **IMPORTANT**: Re-run the SAME tests from Task 1 — do NOT write new tests. They encode the expected behavior; passing confirms the bugs are fixed.
  - Run `src/test/test_cascoin --run_test=cvm_wot_fix_explore_tests`.
  - **EXPECTED OUTCOME**: Tests PASS (confirms all in-scope bugs are resolved).
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12_

- [x] 13. Re-run the preservation tests and confirm no regressions
  - **Property 2: Preservation** - Non-Bug-Condition Behavior Unchanged
  - **IMPORTANT**: Re-run the SAME tests from Task 10 — do NOT write new tests.
  - Run `src/test/test_cascoin --run_test=cvm_wot_preserve_tests`.
  - **EXPECTED OUTCOME**: Tests PASS (confirms no regressions).
  - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10_

---

## Integration / functional test

- [x] 14.* Add an end-to-end functional regtest for the single-wallet WoT flow
  - Create `test/functional/feature_wot_fixes.py` (following existing `test/functional/feature_cvm_*.py` patterns) and register it in `test/functional/test_runner.py`.
  - Single wallet: build `A→B→C→D` via `sendtrustrelation`, mine, then:
    - `listtrustrelations` returns parseable JSON with exact expected fields (weight, bond, reason, from/to, slashed). (2.1, 2.2, 2.3, 2.4)
    - `gettrustgraphstats.total_trust_edges` is consistent with the `listtrustrelations` count. (2.7)
    - `getweightedreputation("D","A",3)` returns `paths_found >= 1`, non-zero reputation, and a base58 `viewer`. (2.5, 2.8)
  - Repeat the same end-to-end flow using bech32 P2WSH addresses and quantum (`rcasq…`) addresses. (2.9, 2.10, 2.12)
  - Confirm the single-wallet graph stores, lists, and traverses correctly. (2.11)
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12_

---

## Final build / verification checkpoint

- [x] 15. Checkpoint — Build and run the WoT test suites
  - Build the unit-test binary and daemon/CLI: `make -j$(nproc)` producing `src/test/test_cascoin`, `src/cascoind`, and `src/cascoin-cli` (Autotools; run `./autogen.sh && ./configure` first if needed).
  - Run the WoT unit suites: `src/test/test_cascoin --run_test=cvm_wot_fix_explore_tests`, `cvm_wot_preserve_tests`, `cvm_wot_fix_property_tests`, `trustnodeid_tests`, and the existing `trustpropagator_tests` (regression).
  - Run the functional test: `test/functional/test_runner.py feature_wot_fixes.py`.
  - Ensure all tests pass. Ask the user if questions arise.
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12, 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9, 3.10_

---

## Notes

- Tier A (Tasks 2–5) requires no on-disk or on-chain format change, so no migration/reindex is needed and existing edges stay valid.
- Tier B (Tasks 6–9) is additive and backward-compatible: legacy v1 DB records and v1 OP_RETURN payloads are still read with their exact legacy layout; v2 is emitted only when a P2WSH/quantum node is involved.
- Files touched by multiple tasks are sequenced to avoid conflicts (see the Task Dependency Graph notes).
- Tasks marked `*` are optional/test tasks per workflow convention; exploration (Task 1) must fail on unfixed code before any fix is applied.

## Task Dependency Graph

```json
{
  "waves": [
    {
      "wave": 1,
      "description": "Exploration — reproduce all bugs on unfixed code (must fail)",
      "tasks": ["1"]
    },
    {
      "wave": 2,
      "description": "Tier B foundation — wide identifier type (no conflicts with Tier A files)",
      "tasks": ["6.1", "6.2"]
    },
    {
      "wave": 3,
      "description": "Tier A read-path + Tier B RPC decode (trustgraph.* and rpc/cvm.* sequenced within file)",
      "tasks": ["2.1", "7.1"]
    },
    {
      "wave": 4,
      "description": "Enumerator application + unified decode application (depend on 2.1/7.1)",
      "tasks": ["2.2", "2.3", "7.2"]
    },
    {
      "wave": 5,
      "description": "Tier A identity fixes (txbuilder -> rpc -> blockprocessor)",
      "tasks": ["3.1"]
    },
    {
      "wave": 6,
      "description": "RPC-side identity fixes (depend on 3.1 and 7.2 for addtrust decode)",
      "tasks": ["3.2", "3.3"]
    },
    {
      "wave": 7,
      "description": "Block processor pass-through of from-identity",
      "tasks": ["3.4"]
    },
    {
      "wave": 8,
      "description": "Tier A reputation + viewer echo",
      "tasks": ["4", "5"]
    },
    {
      "wave": 9,
      "description": "Tier B versioned DB edge (trustgraph.* — sequenced after Tier A trustgraph edits)",
      "tasks": ["8.1"]
    },
    {
      "wave": 10,
      "description": "Tier B key format + method signatures (depend on 8.1)",
      "tasks": ["8.2", "8.3"]
    },
    {
      "wave": 11,
      "description": "Tier B on-chain payload + block processor",
      "tasks": ["9.1", "9.2"]
    },
    {
      "wave": 12,
      "description": "Preservation + fix-check property tests",
      "tasks": ["10", "11"]
    },
    {
      "wave": 13,
      "description": "Verify exploration + preservation tests post-fix",
      "tasks": ["12", "13"]
    },
    {
      "wave": 14,
      "description": "End-to-end functional regtest",
      "tasks": ["14"]
    },
    {
      "wave": 15,
      "description": "Final build + verification checkpoint",
      "tasks": ["15"]
    }
  ],
  "notes": [
    "Files touched by multiple tasks are sequenced to avoid conflicts: src/cvm/trustgraph.{h,cpp} in 2.1 -> 2.2 -> 8.1 -> 8.2/8.3 -> 4; src/rpc/cvm.cpp in 2.3 -> 7.1 -> 7.2 -> 3.2/3.3 -> 5; src/cvm/blockprocessor.cpp in 3.4 -> 9.2; src/cvm/softfork.* in 9.1.",
    "The exploratory test task (1) comes first and is expected to FAIL on unfixed code.",
    "Task 3.3 (addtrust) depends on 7.2 (DecodeTrustNode applied) for unified address decoding.",
    "Tasks marked * are optional/test tasks per workflow convention."
  ]
}
```
