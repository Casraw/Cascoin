# Implementation Plan

## Overview

This plan turns the TrustNodeId Full Migration design into an incremental, test-driven
set of coding tasks following the bugfix methodology: first surface counterexamples on
the **unfixed** code (exploration), then capture the behavior to preserve, then perform
the **direct pre-launch conversion** of every downstream user-identity path from
`uint160` to `CVM::TrustNodeId`, wave by wave, and finally lock behavior with fix-check
and preservation tests and validate end-to-end.

This is a **direct conversion only**. Per the approved bugfix.md/design.md and the
explicit user decision (the affected downstream formats are NOT live):

- Do **NOT** add legacy downstream deserializers, data migrations, dual-read
  namespaces, compatibility fallbacks, ambiguity provenance, migration activation
  heights, or mixed old/new database support.
- Existing development CVM state **MAY be discarded/rebuilt**. The converted binary
  reads and writes only the one current typed layout/namespace per record.
- The already-completed `TrustEdge`/`CVMTrustEdgeData` v1/v2 implementation from
  `web-of-trust-fixes` is preserved **unchanged**.
- No new consensus activation height is introduced for the four newly changed
  OP_RETURN payloads.

Scope boundary (strict): only user identities become `TrustNodeId`. Contract/EVM
addresses stay `uint160`; transaction/dispute/bond/source-edge/block/internal hashes
stay `uint256`.

Conventions:
- Property-based / optional test tasks are marked with `*` per workflow convention.
- Property tasks use the `**Property N: Type**` format to enable hover status.
  Property 1 = Bug Condition (exploration), Property 2 = Preservation. Fix-check tasks
  reuse the `**Property N: ...**` convention and map to the design's Correctness
  Properties (design P1–P10).
- New unit tests follow the existing pattern in `src/test/cvm_wot_fix_property_tests.cpp`,
  `src/test/cvm_wot_preserve_tests.cpp`, `src/test/trustnodeid_tests.cpp`, and
  `src/test/cvm_workstream2_preserve_tests.cpp` (Boost.Test, binary
  `src/test/test_cascoin`) and are registered in `src/Makefile.test.include` under
  `BITCOIN_TESTS`.
- Functional tests live in `test/functional/` and are registered in
  `test/functional/test_runner.py`, following `feature_wot_fixes.py` and
  `feature_trust_activation.py` (regtest mining: `-powalgo=sha256d`, high `maxtries`;
  CVM active at regtest height 0, quantum at height 1).
- Build system is Autotools; the unit-test binary is `test_cascoin` (NOT `test_bitcoin`).
- Do not mark implementation tasks complete; only the workflow owner marks completion
  after tests pass.

## Tasks

- [x] 1.* Write bug-condition exploration tests that reproduce ALL downstream width/type defects on the UNFIXED code
  - **Property 1: Bug Condition** - Downstream user-identity `uint160` rejection, truncation, and collision
  - **CRITICAL**: These tests MUST FAIL on the current (unfixed) code — the failures confirm the bug exists (wide identities rejected/truncated; typed 20-byte identities collide).
  - **DO NOT attempt to fix the tests or the code when they fail here.** These tests encode the expected behavior and will validate the fix when they pass later.
  - **GOAL**: Surface concrete counterexamples for every in-scope defect in bugfix.md §1.1–1.9.
  - **Scoped PBT Approach**: for the deterministic width-rejection/collision defects use concrete stored fixtures (specific P2WSH/quantum destinations and equal-data/different-tag P2PKH/P2SH/P2WPKH triples); use property generators across all five destination tags where the design calls for it.
  - Create `src/test/trustnodeid_full_migration_explore_tests.cpp` and register it in `src/Makefile.test.include` under `BITCOIN_TESTS`.
  - Test case 1 — RPC width rejection: drive a WoT RPC decode path (`src/rpc/cvm.cpp`) with a bech32 P2WSH (`WitnessV0ScriptHash`) and a quantum (`WitnessV2Quantum`, `rcasq…`) address and confirm `TrustNodeToLegacyUint160(node, addr)` throws `RPC_INVALID_ADDRESS_OR_KEY`. Document the counterexample. (Bug 1.1 — validates 2.1)
  - Test case 2 — HAT keys cannot carry/distinguish identities: key `StakeInfo`/`TemporalMetrics`/`BehaviorMetrics`/`GraphMetrics` (`src/cvm/securehat.{h,cpp}`, `behaviormetrics.{h,cpp}`, `graphanalysis.{h,cpp}`) by `uint160` and show (a) a P2WSH/quantum identity cannot be represented, and (b) P2PKH/P2SH/P2WPKH identities with equal 20-byte payloads map to the same key. Document counterexamples. (Bug 1.2 — validates 2.2)
  - Test case 3 — Reputation truncation/collision: store a reputation target/voter via the `uint160`-keyed reputation store (`src/cvm/reputation.{h,cpp}`); show wide identities are rejected/truncated and equal-data/different-tag identities collide. Document counterexamples. (Bug 1.3 — validates 2.3)
  - Test case 4 — Clustering loses destinations: show `uint160` cluster/member identities in `src/cvm/walletcluster.{h,cpp}` / `clustertrustquery.{h,cpp}` cannot preserve P2WSH/quantum, and that `WalletClusterer::AnalyzeTransaction` extracts only `CKeyID` prevouts so P2SH/P2WPKH/P2WSH/quantum inputs are excluded from the common-input heuristic. Document counterexamples. (Bugs 1.4, 1.5 — validates 2.4, 2.5)
  - Test case 5 — Wrong member rendering: show a cluster RPC in `src/rpc/cvm.cpp` renders a stored non-P2PKH member as `CKeyID` with the wrong address type. Document the counterexample. (Bug 1.6 — validates 2.4)
  - Test case 6 — Bonded-vote/DAO cannot carry all types: show `BondedVote.voter/target`, `DAODispute.challenger`, and `daoVotes`/`daoStakes` keys in `src/cvm/trustgraph.{h,cpp}` are `uint160` and cannot represent all five user-identity types. Document the counterexample. (Bug 1.7 — validates 2.6)
  - Test case 7 — Propagation loses width/type: show `PropagatedTrustEdge.fromAddress/toAddress/originalTarget`, cluster summaries, propagation indexes, and caches in `src/cvm/trustpropagator.{h,cpp}` / `clusterupdatehandler.{h,cpp}` are `uint160` and lose identity width/type. Document the counterexample. (Bug 1.8 — validates 2.7)
  - Test case 8 — OP_RETURN payloads cannot carry all types: show the reputation, bonded-vote, DAO-dispute, and DAO-vote payloads in `src/cvm/softfork.{h,cpp}` encode identity as 20 bytes and cannot carry every supported identity. Document the counterexample. (Bug 1.9 — validates 2.10)
  - Run with `src/test/test_cascoin --run_test=trustnodeid_full_migration_explore_tests`.
  - **EXPECTED OUTCOME**: Tests FAIL (this is correct — it proves the bug exists). Mark this task complete when the tests are written, run, and the failures/counterexamples are documented.
  - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9_

---

## Preservation property tests (capture the unfixed baseline BEFORE any code change)

- [x] 2.* Write preservation property tests for all non-bug-condition behavior
  - **Property 2: Preservation** - Contract/EVM `uint160`, `uint256` hash domains, TrustEdge v1/v2, activation path, and existing algorithm semantics unchanged
  - **IMPORTANT**: Follow observation-first methodology — observe/record current correct behavior on inputs where `isBugCondition` is false on the UNFIXED node, then assert it is unchanged after the migration.
  - Property-based testing is recommended: generate contract/EVM addresses, transaction/dispute/bond/source hashes, TrustEdge v1/v2 fixtures, and non-identity fixed-width values across the input domain.
  - Create `src/test/trustnodeid_full_migration_preserve_tests.cpp` and register it in `src/Makefile.test.include`, following `src/test/cvm_wot_preserve_tests.cpp` and `src/test/cvm_workstream2_preserve_tests.cpp`.
  - Contract/EVM preservation: `Contract::address`, call/deployment address, and EVM account/storage addresses remain 20-byte `uint160` with byte-for-byte identical derivation, serialization, and DB key format. (4.1)
  - Hash-domain preservation: transaction IDs, `bondTxHash`, `slashTxHash`, `sourceEdgeTx`, `disputeId`, `originalVoteTx`, block IDs, and code/state hashes remain `uint256` with identical semantics. (4.2)
  - TrustEdge v1/v2 preservation: keep existing `CVMTrustEdgeData` v1/v2 golden fixtures from `web-of-trust-fixes` unchanged; `TrustEdge` key-shape detection, traversal, weighted reputation, and RPC behavior are byte-for-byte and behaviorally unchanged. (4.3)
  - Activation-path preservation: `ProcessNonContractBlock` ordering, validation boundary, durable-write behavior, and on-chain/direct-write equivalence from `trust-system-activation` are unchanged. (4.4)
  - Validation preservation: vote-range, bond, DAO-threshold, unsupported `CNoDestination`/`WitnessUnknown`, malformed-data, soft-fork gating, and `fJustCheck` results are unchanged except for now accepting all five canonical identity types in newly typed fields. (4.5)
  - Clustering heuristic preservation: common-input ownership, two-output change selection, minimum cluster reputation/HAT, propagation limits, edge conflict resolution, and direct-over-propagated precedence semantics are unchanged. (4.6)
  - Quantum/Falcon preservation: quantum parsing, encoding, signature routing, and existing `TrustNodeId::FromDestination`/`ToDestination` behavior are unchanged. (4.7)
  - Non-identity uint160/uint256 preservation: any non-identity fixed-width values keep their current types. (4.1, 4.2)
  - Run on the UNFIXED baseline and confirm PASS; these will be re-run after the migration to confirm no regressions.
  - **EXPECTED OUTCOME**: Tests PASS (they confirm behavior to preserve).
  - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8_

---

## The migration — direct `uint160` → `TrustNodeId` conversion (dependency-ordered waves)

- [x] 3. Migration — convert every downstream user-identity path to `TrustNodeId`

  - [x] 3.1 Wave 1 — Identity validation helper and strict acceptance rules
    - Add a strict external parsing/validation helper (e.g. `bool ValidateCanonicalTrustNode(const CVM::TrustNodeId&, std::string& err)` and `bool ParseKeyStringToTrustNode(const std::string&, CVM::TrustNodeId&, std::string& err)`) alongside the unchanged type in `src/cvm/trustnodeid.{h,cpp}`. Do NOT redesign `TrustNodeId`, change tag values, alter serialization, or modify `ToKeyString()`.
    - Acceptance rules enforced by the helper and reused at every migrated call site: type is one of `1..5`; P2PKH/P2SH/P2WPKH have zero in the high 12 bytes; P2WSH/quantum retain all 32 bytes; `ToDestination()` returns a supported destination; textual key input exactly matches the canonical lowercase `ToKeyString()` shape `<type:02x>-<data:64 lowercase hex>`. Reject unknown types, noncanonical hex, extra separators, trailing characters, and nonzero high padding for 20-byte types.
    - Never convert through `uint160` in downstream identity paths; invalid stored identities are errors, not candidates for type inference.
    - _Bug_Condition: isBugCondition(op) where the path requires uint160 or erases destination type_
    - _Expected_Behavior: expectedBehavior — only strictly canonical TrustNodeId accepted; no width rejection; exact round-trip (design Properties 1, 2)_
    - _Preservation: `TrustNodeId` type, tags, serialization, and `ToKeyString()` unchanged (design Property 10)_
    - _Requirements: 2.11, 2.12, 5.1_

  - [x] 3.2 Wave 2 — HAT / SecureHAT / graph analysis typed conversion
    - In `src/cvm/securehat.{h,cpp}`, `behaviormetrics.{h,cpp}`, `graphanalysis.{h,cpp}`: convert target/viewer/address fields and API signatures, `TradeRecord.partner`, unique-partner sets, graph node/member/entry-point sets, queues, maps, and caches from `uint160` to `TrustNodeId`.
    - Representative signatures: `int16_t CalculateFinalTrust(const TrustNodeId& target, const TrustNodeId& viewer);`, `BehaviorMetrics GetBehaviorMetrics(const TrustNodeId& address);`, `GraphMetrics GetGraphMetrics(const TrustNodeId& address);`.
    - `GraphAnalyzer` enumerates only canonical forward TrustEdges through the existing TrustGraph key-shape-aware path and never narrows an edge endpoint.
    - Convert current DB keys to `behavior_<TNI>`, `stake_<TNI>`, `temporal_<TNI>` where `<TNI>` is exactly `ToKeyString()`. One current layout per record; reader requires complete stream consumption and bounded containers.
    - _Bug_Condition: isBugCondition(op) — HAT metric keys cannot represent/distinguish typed identities (1.2)_
    - _Expected_Behavior: expectedBehavior — HAT fields/APIs/keys/caches use TrustNodeId; write-then-read recovers exact type and bytes (design Property 3)_
    - _Preservation: HAT scoring/thresholds unchanged; only identity representation changes (design Property 10)_
    - _Requirements: 2.2, 2.8, 2.9, 2.11, 2.12_

  - [x] 3.3 Wave 3 — Reputation model, indexes, and single current namespace
    - In `src/cvm/reputation.{h,cpp}`: convert reputation address/target/voter fields and every corresponding API, secondary index, activity key, low-reputation enumeration, and cache from `uint160` to `TrustNodeId`. Transaction-pattern data remains keyed by transaction hashes.
    - Use current keys `repidx_<TNI>` and `txhist_<TNI>`, and the one intentional namespace rename for the reputation primary raw key: `reputation_<TNI>` (materially avoids overlap with stale development raw-`R` records). Do NOT query any old reputation namespace; introduce no other renamed parallel namespace.
    - Reader accepts only the current typed layout, requires complete consumption and bounded containers, and rejects malformed/noncanonical key segments.
    - _Bug_Condition: isBugCondition(op) — reputation uint160 rejects/truncates and collides (1.3)_
    - _Expected_Behavior: expectedBehavior — reputation uses TrustNodeId end to end; write-then-read recovers exact type and all bytes (design Properties 3, 4)_
    - _Preservation: scoring math and transaction-pattern hashing unchanged (design Property 10)_
    - _Requirements: 2.3, 2.8, 2.9, 2.11, 2.12_

  - [x] 3.4 Wave 4 — Wallet clustering, cluster-trust query, extraction, persistence, rendering
    - In `src/cvm/walletcluster.{h,cpp}` and `clustertrustquery.{h,cpp}`: convert cluster IDs, members, union-find maps, address indexes, query signatures, and cluster summaries from `uint160` to `TrustNodeId`. Deduplication uses exact typed pairs, not low-20-byte projections.
    - `WalletClusterer::AnalyzeTransaction`: resolve prevouts as today, call `ExtractDestination`, then `TrustNodeId::FromDestination`; include exactly the five supported destination types (P2PKH, P2SH, P2WPKH, P2WSH, quantum); ignore unresolved prevouts, invalid indexes, `CNoDestination`, and `WitnessUnknown`. Retain the existing common-input heuristic (dedup exact typed inputs, union when ≥2 remain) and the existing two-output lower-value change heuristic for any supported typed change destination. Equal data with different type tags stays distinct.
    - Union-find maps use `TrustNodeId` with path compression and the existing union strategy. Externally visible cluster ID is deterministic (minimum member under `TrustNodeId::operator<`). Persistence sorts and deduplicates records/indexes before upsert.
    - Render members only via `EncodeDestination(node.ToDestination())`; an invalid stored destination produces a data/internal error, not a fabricated P2PKH address.
    - Current keys: `wc_<clusterTNI>`, `wca_<memberTNI>`, `txcluster_addr_<TNI>`, `txcluster_tx_<txid64>` with value `vector<TrustNodeId>`, and `cluster_trust_<clusterTNI>`. Transaction/source-edge identifiers remain `uint256`.
    - `buildwalletclusters` is the supported materialization path: it scans the applicable active-chain/index source, extracts typed destinations, writes only current cluster records, and is deterministic/idempotent for the same active-chain input. No automatic startup scan is introduced.
    - _Bug_Condition: isBugCondition(op) — clustering uint160 loses destinations; AnalyzeTransaction extracts only CKeyID; members rendered as CKeyID (1.4, 1.5, 1.6)_
    - _Expected_Behavior: expectedBehavior — five-type typed extraction, typed persistence/queries, typed rendering (design Properties 3, 6, 7)_
    - _Preservation: common-input/change heuristics and cluster asymptotics unchanged (design Property 10; 4.6)_
    - _Requirements: 2.4, 2.5, 2.8, 2.9, 2.11, 2.12, 3.2, 5.3_

  - [x] 3.5 Wave 5 — Trust propagation and cluster-update handler typed conversion
    - In `src/cvm/trustpropagator.{h,cpp}` and `clusterupdatehandler.{h,cpp}`: convert `PropagatedTrustEdge.fromAddress/toAddress/originalTarget`, cluster summaries, propagation indexes, conflict handling, and LRU cache keys from `uint160` to `TrustNodeId`, preserving the exact identity in records, indexes, and RPC output. Deduplication uses exact typed pairs.
    - Current keys: `trust_prop_<fromTNI>_<toTNI>` and `trust_prop_idx_<source64>_<toTNI>`. Source-edge hashes remain `uint256`.
    - Retain existing propagation limits, cache limits, edge conflict resolution, and direct-over-propagated precedence.
    - _Bug_Condition: isBugCondition(op) — propagation uint160 loses identity width/type (1.8)_
    - _Expected_Behavior: expectedBehavior — propagation carries TrustNodeId end to end; exact round-trip and no collision (design Properties 3, 4)_
    - _Preservation: propagation limits/conflict resolution/precedence unchanged (design Property 10; 4.6)_
    - _Requirements: 2.7, 2.8, 2.9, 2.11, 2.12_

  - [x] 3.6 Wave 6 — Bonded votes and DAO data models typed conversion
    - In `src/cvm/trustgraph.{h,cpp}` and callers: convert `BondedVote.voter/target`, `DAODispute.challenger`, and `daoVotes`/`daoStakes` map keys from `uint160` to `TrustNodeId`; convert DAO member and vote APIs accordingly. Update `src/cvm/hat_consensus.cpp` and DAO/reward callers to pass typed identities without changing hash/threshold logic.
    - Keep `bondTxHash`, dispute IDs, original vote IDs, slash IDs, and reward-distribution IDs as `uint256`; quorum, stake, slashing, reward, and threshold logic unchanged.
    - Current keys: `vote_<txid64>`, target vote index `votes_<targetTNI>_<txid64>`, `dispute_<id64>`, and `dispute_by_vote_<txid64>`.
    - **Do NOT touch `TrustEdge`/`CVMTrustEdgeData`** in `trustgraph.{h,cpp}`; the completed v1/v2 implementation stays unchanged.
    - _Bug_Condition: isBugCondition(op) — bonded/DAO uint160 cannot represent all five identity types (1.7)_
    - _Expected_Behavior: expectedBehavior — bonded/DAO identity fields and map keys use TrustNodeId; hashes stay uint256 (design Properties 3, 4, 9)_
    - _Preservation: quorum/stake/slashing/reward/threshold logic and TrustEdge v1/v2 unchanged (design Property 10; 4.3)_
    - _Requirements: 2.6, 2.8, 2.9, 2.11, 2.12_

  - [x] 3.7 Wave 7 — Canonical one-layout OP_RETURN payloads, builders, and processors
    - In `src/cvm/softfork.{h,cpp}`: convert the four newly changed identity-bearing payloads to exactly one canonical `TrustNodeId` (`TNI33`) layout each. Keep the outer `CVM_MAGIC`, current operation types, and integer byte order. Exact bodies and sizes:
      - Reputation — `target:TNI33, vote:int16, timestamp:uint32` = **39 bytes**.
      - Bonded vote — `voter:TNI33, target:TNI33, vote:int16, bond:int64, timestamp:uint32` = **80 bytes**.
      - DAO dispute — `originalVoteTx:uint256, challenger:TNI33, bond:int64, timestamp:uint32` = **77 bytes**.
      - DAO vote — `disputeId:uint256, member:TNI33, support:uint8, stake:int64, timestamp:uint32` = **78 bytes**.
    - No payload version byte and **no new activation height**; the existing feature activation already governing each operation remains authoritative. Parsers require exact size, canonical identities, valid scalar ranges, and complete consumption; builders emit only these forms. Any separate reputation marker/wrapper carrying identity is changed directly to one typed current layout with no alternate parser.
    - In `src/cvm/txbuilder.{h,cpp}` and `blockprocessor.{h,cpp}`: pass typed user identities through builders/signer out-parameters and processors while contract addresses and hash parameters keep native types.
    - **`CVMTrustEdgeData` is explicitly excluded** — its v1/v2 detection, encoding, validation, and processing remain byte-for-byte and behaviorally unchanged.
    - _Bug_Condition: isBugCondition(op) — payloads encode identity as 20 bytes and cannot carry every identity (1.9)_
    - _Expected_Behavior: expectedBehavior — one canonical typed layout per payload with exact length, strict identity validation, complete consumption; no new gate (design Property 5)_
    - _Preservation: CVMTrustEdgeData v1/v2 and existing activation behavior unchanged (design Properties 5, 10; 4.4)_
    - _Requirements: 2.10, 2.11, 2.12, 5.1_

  - [x] 3.8 Wave 8 — RPC and call-site cleanup; remove downstream `TrustNodeToLegacyUint160`
    - In `src/rpc/cvm.cpp`: pass decoded `TrustNodeId` values directly into the now-typed downstream APIs (reputation, HAT, wallet-cluster/cluster-trust, bonded-vote, DAO, propagation). Render stored identities only with `EncodeDestination(node.ToDestination())`. Builders/signer out-parameters carry typed user identities; contract/hash parameters keep native types.
    - Remove **all** migrated user-identity uses of `TrustNodeToLegacyUint160` (the bridging call sites at `src/rpc/cvm.cpp` lines around `getreputation`, `sendcvmvote`/vote build, `sendtrustrelation`, `sendbondedvote`, `votereputation`, `getbehaviormetrics`, `getgraphmetrics`, `getsecuretrust`, `gettrustbreakdown`, `getwalletcluster`, `geteffectivetrust`, and cluster-trust RPCs). Remove the `TrustNodeToLegacyUint160` helper entirely once no valid user-identity call sites remain.
    - _Bug_Condition: isBugCondition(op) — RPC bridges typed identities back through uint160 (1.1)_
    - _Expected_Behavior: expectedBehavior — RPCs carry TrustNodeId end to end with no width rejection; typed rendering (design Property 3)_
    - _Preservation: `DecodeDestination`/`IsQuantumAddress`/`EncodeDestination` behavior unchanged (design Property 10)_
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.6, 2.7_

  - [x] 3.9 Wave 9 — Clean development-state / rebuild behavior (no migration logic)
    - Ensure the converted binary reads and writes only the one current typed layout and namespace defined for each downstream record. Do **NOT** add alternate readers, migration routines, or dual-read namespaces.
    - When stale not-live downstream `uint160` records are present in a development CVM database, the affected subsystem (or startup) reports that a clean rebuild is required rather than reinterpreting stale bytes as typed identities. Wallet clusters are rebuilt from chain/index data via `buildwalletclusters` (deterministic and idempotent for the same active-chain input).
    - Use existing CVM logging with rate limiting: count malformed current keys/records, failed index writes, rebuild starts/completions, rejected payloads, and idempotent replay skips; log bounded natural IDs or `ToKeyString()` only when needed; never log wallet secrets or full member lists. Primary writes precede secondary-index writes; an index failure is reported and a repeated idempotent upsert repairs the index without duplicate logical state.
    - _Bug_Condition: isBugCondition(op) — stale not-live uint160 records would be reinterpreted (3.1, 3.3)_
    - _Expected_Behavior: expectedBehavior — one current layout/namespace; clean rebuild rather than reinterpretation; deterministic idempotent cluster rebuild (design Properties 7, 8)_
    - _Preservation: no automatic startup chain scan; existing logging/rate-limiting conventions (5.3, 5.4)_
    - _Requirements: 3.1, 3.2, 3.3, 5.2, 5.3, 5.4_

  - [x] 3.10 Verify the bug-condition exploration tests now pass
    - **Property 1: Expected Behavior** - Downstream user identities carry TrustNodeId without width rejection, truncation, or collision
    - **IMPORTANT**: Re-run the SAME tests from Task 1 — do NOT write new tests. They encode the expected behavior; passing confirms the bug is fixed.
    - Run `src/test/test_cascoin --run_test=trustnodeid_full_migration_explore_tests`.
    - **EXPECTED OUTCOME**: Tests PASS (confirms all five destination types work end to end across HAT, reputation, clustering, bonded-vote/DAO, propagation, payloads, and RPC).
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.10_

  - [x] 3.11 Verify the preservation tests still pass
    - **Property 2: Preservation** - Non-bug-condition behavior unchanged
    - **IMPORTANT**: Re-run the SAME tests from Task 2 — do NOT write new tests.
    - Run `src/test/test_cascoin --run_test=trustnodeid_full_migration_preserve_tests`.
    - **EXPECTED OUTCOME**: Tests PASS (confirms no regressions in contract/EVM `uint160`, `uint256` hash domains, TrustEdge v1/v2, activation path, and existing algorithm semantics).
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8_

---

## Fix-check property tests (map to design Correctness Properties P1–P10)

- [x] 4.* Write fix-check property tests P1–P10
  - **Property 3: Expected Behavior** - Fix Verification (design Correctness Properties P1–P10)
  - **IMPORTANT**: These extend/parallel the exploration tests from Task 1; run AFTER the migration and confirm PASS. Use property-based generation across all five destination tags, including equal data under different tags; bounded current records; valid/invalid payloads; and transactions with resolvable, unsupported, and unresolved prevouts. Record the seed and minimized counterexample on failure.
  - Add tests to `src/test/trustnodeid_full_migration_fix_property_tests.cpp` (new file, registered in `src/Makefile.test.include`), one test per property where practical:
    - P1 — Canonical identity round-trip: for all five destination types, `FromDestination`→`ToDestination` and serialize→deserialize preserve exact type and data. (design P1 → 2.1, 2.8, 2.11)
    - P2 — Collision safety: two `TrustNodeId` values differing by type or data have distinct serialized identities, `ToKeyString()` values, map/set keys, and cache keys (include equal-data/different-tag P2PKH/P2SH/P2WPKH). (design P2 → 2.9, 2.11, 2.12)
    - P3 — Native downstream API closure: SecureHAT, reputation, wallet clustering, cluster trust, bonded-vote/DAO, and propagation APIs accept `TrustNodeId`, retain it through internal calls, and retrieve the same identity. (design P3 → 2.1–2.7)
    - P4 — Current record round-trip: each current HAT/reputation/cluster/bonded-vote/DAO/propagation record with bounded containers round-trips exactly, consumes all bytes, and rejects malformed/trailing data (malformed canonical identity/record tests). (design P4 → 2.8, 2.11, 5.1)
    - P5 — Canonical payload round-trip: each newly changed payload has exactly one accepted canonical encoding; verify exact body sizes **39 / 80 / 77 / 78**, exact field order and integer encoding, and reject every other length, malformed identity, invalid type/high padding, and out-of-range scalar (malformed canonical payload tests). (design P5 → 2.10, 2.11, 4.4, 4.5)
    - P6 — Five-type clustering extraction: for transactions with supported resolved prevouts, `AnalyzeTransaction` extracts each exact typed destination and applies common-input/change heuristics independent of type; unsupported/unresolved prevouts are ignored (common-input, change, rendering tests). (design P6 → 2.4, 2.5, 4.6)
    - P7 — Cluster persistence and rendering: save/load and chain/index rebuild produce the same member sets and deterministic cluster IDs; every rendered member decodes to its stored `TrustNodeId`. (design P7 → 2.4, 3.2, 4.6)
    - P8 — Idempotency: repeating a valid typed write/processing operation or rebuilding from the same active-chain input produces identical logical state with no duplicate entries. (design P8 → 2.13, 3.2, 4.4)
    - P9 — Fixed-width scope preservation: contract/EVM addresses and transaction/dispute/bond/source/internal hashes serialize, key, compare, and derive byte-for-byte unchanged (non-identity uint160/uint256 preservation tests). (design P9 → 4.1, 4.2)
    - P10 — Completed-feature preservation: inputs covered by `web-of-trust-fixes`/`trust-system-activation` (TrustEdge v1/v2, block processing, quantum parsing, reconnect) remain unchanged (WoT/activation regressions). (design P10 → 4.3–4.7)
  - Run with `src/test/test_cascoin --run_test=trustnodeid_full_migration_fix_property_tests`.
  - **EXPECTED OUTCOME**: Tests PASS (confirming the migration is correct).
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12, 2.13, 3.2, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

---

## Integration / functional test

- [x] 5.* Add an end-to-end functional regtest for the typed downstream flow
  - Create `test/functional/feature_trustnodeid_migration.py` (following `test/functional/feature_wot_fixes.py` and `feature_trust_activation.py`: single wallet, `-powalgo=sha256d`, high `maxtries`) and register it in `test/functional/test_runner.py`.
  - Exercise reputation (`sendcvmvote`/`getreputation`), the four HAT RPCs (`getbehaviormetrics`, `getgraphmetrics`, `getsecuretrust`, `gettrustbreakdown`), wallet-cluster/cluster-trust RPCs (`buildwalletclusters`, `getwalletcluster`, `geteffectivetrust`), bonded votes (`sendbondedvote`), DAO paths (`createdispute`, `votedispute`, `listdisputes`, `getdispute`), and propagation with each supported destination type (P2PKH, P2SH, P2WPKH, P2WSH, quantum `rcasq…`) as applicable; assert no width rejection and canonical output.
  - Mine/process the one current reputation, bonded-vote, DAO-dispute, and DAO-vote payload forms, then compare resulting state with equivalent direct API writes. Repeat block processing/reconnect and assert no duplicate reputation application, votes, dispute-member entries, cluster indexes, or propagation entries (idempotency).
  - Build mixed-type common-input and two-output change transactions; run `buildwalletclusters` twice and across restart; verify stable cluster IDs, member sets, indexes, and original address encodings (rendering).
  - Start with a clean CVM state, write all current record families, restart, and verify direct new-format round trips. As an operational test, seed stale not-live downstream development keys and verify the documented clean-rebuild error/path rather than data reinterpretation.
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.13, 3.1, 3.2, 3.3_

---

## Audit / final build / verification checkpoint

- [x] 6. Checkpoint — Legacy-call audit, clean-state build, and full suite run
  - **Legacy-call audit**: `grep_search` the codebase for `TrustNodeToLegacyUint160` and confirm **zero** remaining migrated user-identity call sites, and that the helper is removed. Then audit `TrustNodeId::ToUint160()`: **reject** any remaining lossy user-identity uses (fail the checkpoint until removed) while **retaining** genuine contract/EVM fixed-width uses (contract address derivation, EVM account/storage addresses) and `uint256` hash-domain uses. Document each retained use with a one-line justification comment referencing the fixed-width scope table.
  - **Clean development DB / rebuild**: verify no migration logic exists; confirm the converted binary opens only clean current-layout state, reports a clean-rebuild requirement on stale not-live records, and that `buildwalletclusters` rematerializes clusters deterministically. Discard/rebuild any development CVM state containing the not-live downstream `uint160` layouts before running.
  - **Build**: `make -j$(nproc)` producing `src/test/test_cascoin`, `src/cascoind`, and `src/cascoin-cli` (Autotools; run `./autogen.sh && ./configure` first if needed).
  - **Run unit suites**: `src/test/test_cascoin --run_test=trustnodeid_full_migration_explore_tests`, `trustnodeid_full_migration_preserve_tests`, `trustnodeid_full_migration_fix_property_tests`, `trustnodeid_tests`, `trustpropagator_tests`, `cvm_wot_fix_property_tests`, `cvm_wot_preserve_tests`, `cvm_trust_activation_fix_property_tests`, and `cvm_trust_activation_preserve_tests` (WoT + activation regressions).
  - **Run functional tests**: `test/functional/test_runner.py feature_trustnodeid_migration.py feature_wot_fixes.py feature_trust_activation.py`.
  - Ensure all tests pass and no downstream path narrows a `TrustNodeId`. Ask the user if questions arise.
  - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12, 2.13, 3.1, 3.2, 3.3, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7, 4.8, 5.1, 5.2, 5.3, 5.4_

---

## Notes

- This is a direct pre-launch conversion. No legacy downstream deserializers, data
  migrations, dual-read namespaces, compatibility fallbacks, ambiguity provenance,
  migration activation heights, or mixed old/new database support are added.
- Development CVM state may be discarded/rebuilt; the converted binary reads and writes
  only the one current typed layout/namespace per record.
- The `TrustEdge`/`CVMTrustEdgeData` v1/v2 implementation is preserved unchanged; the
  four newly changed payloads gain one canonical typed layout with no new activation
  height (sizes 39/80/77/78).
- Files touched by multiple waves are sequenced to avoid conflicts (see the Task
  Dependency Graph): `src/cvm/trustgraph.{h,cpp}` in Wave 6 (TrustEdge untouched);
  `src/rpc/cvm.cpp` in Wave 8; `src/cvm/softfork.{h,cpp}` and
  `txbuilder/blockprocessor` in Wave 7.
- The exploration task (1) comes first and is expected to FAIL on unfixed code; the
  preservation task (2) is expected to PASS on unfixed code.
- Tasks marked `*` are optional/test tasks per workflow convention.
- Do not mark implementation tasks complete; completion is recorded only after the
  associated tests pass.

## Task Dependency Graph

```json
{
  "waves": [
    {
      "wave": 1,
      "description": "Exploration — reproduce all downstream width/type defects on unfixed code (must FAIL)",
      "tasks": ["1"]
    },
    {
      "wave": 2,
      "description": "Preservation baseline on unfixed code (must PASS)",
      "tasks": ["2"]
    },
    {
      "wave": 3,
      "description": "Identity validation/audit helper (src/cvm/trustnodeid.{h,cpp}, type unchanged)",
      "tasks": ["3.1"]
    },
    {
      "wave": 4,
      "description": "HAT/graph typed conversion (securehat/behaviormetrics/graphanalysis)",
      "tasks": ["3.2"]
    },
    {
      "wave": 5,
      "description": "Reputation typed conversion + reputation_<TNI> namespace (reputation.{h,cpp})",
      "tasks": ["3.3"]
    },
    {
      "wave": 6,
      "description": "Wallet clustering + cluster-trust query, extraction, persistence, rendering",
      "tasks": ["3.4"]
    },
    {
      "wave": 7,
      "description": "Propagation + cluster-update handler typed conversion",
      "tasks": ["3.5"]
    },
    {
      "wave": 8,
      "description": "Bonded votes + DAO typed conversion (trustgraph.{h,cpp}; TrustEdge untouched)",
      "tasks": ["3.6"]
    },
    {
      "wave": 9,
      "description": "Canonical OP_RETURN payloads/builders/processors (softfork/txbuilder/blockprocessor; sizes 39/80/77/78)",
      "tasks": ["3.7"]
    },
    {
      "wave": 10,
      "description": "RPC + call-site cleanup; remove downstream TrustNodeToLegacyUint160 (rpc/cvm.cpp)",
      "tasks": ["3.8"]
    },
    {
      "wave": 11,
      "description": "Clean development-state / rebuild behavior (no migration logic)",
      "tasks": ["3.9"]
    },
    {
      "wave": 12,
      "description": "Verify exploration + preservation tests post-migration",
      "tasks": ["3.10", "3.11"]
    },
    {
      "wave": 13,
      "description": "Fix-check property tests P1–P10",
      "tasks": ["4"]
    },
    {
      "wave": 14,
      "description": "End-to-end functional regtest",
      "tasks": ["5"]
    },
    {
      "wave": 15,
      "description": "Audit (ToUint160/legacy calls) + clean-state build + full verification checkpoint",
      "tasks": ["6"]
    }
  ],
  "notes": [
    "Direct conversion only: no legacy deserializers, migrations, dual-read namespaces, fallbacks, provenance, migration activation heights, or mixed old/new DB support.",
    "Development CVM state may be discarded/rebuilt; one current typed layout/namespace per record.",
    "TrustEdge/CVMTrustEdgeData v1/v2 preserved unchanged; four payloads get one canonical typed layout (39/80/77/78) with no new activation height.",
    "File sequencing to avoid conflicts: src/cvm/trustgraph.{h,cpp} in Wave 8 (TrustEdge untouched); src/rpc/cvm.cpp in Wave 10; src/cvm/softfork.{h,cpp} + txbuilder/blockprocessor in Wave 9.",
    "The exploration task (1) must FAIL on unfixed code; the preservation task (2) must PASS on unfixed code.",
    "Wave 15 audit removes migrated TrustNodeToLegacyUint160 calls and rejects remaining lossy TrustNodeId::ToUint160() user-identity uses while retaining genuine contract/EVM fixed-width uses.",
    "Tasks marked * are optional/test tasks per workflow convention."
  ]
}
```
