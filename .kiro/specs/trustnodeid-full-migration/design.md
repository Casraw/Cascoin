# TrustNodeId Full Migration — Bugfix Design

## Overview

This design performs a direct pre-launch conversion of every downstream
user-identity path from `uint160` to the existing `CVM::TrustNodeId`. The affected
HAT/SecureHAT, reputation, wallet-clustering, bonded-vote, DAO, and propagation
formats are not live. Consequently, the implementation does not read, migrate, or
fall back to their previous development-only `uint160` records. Development CVM
databases containing those records may be discarded and rebuilt, and wallet
clusters are rebuilt from chain/index data using typed destinations.

The already-completed `TrustEdge` and `CVMTrustEdgeData` v1/v2 behavior remains
exactly as implemented by `web-of-trust-fixes`. The activated non-contract write path
from `trust-system-activation` also remains in place. This design introduces no new
consensus activation height.

The implementation follows these rules:

1. Keep `TrustNodeId`, its stable tags, serialization, conversion methods, ordering,
   and `ToKeyString()` unchanged.
2. Carry `TrustNodeId` end to end for user identities. Do not call `ToUint160()` or
   `TrustNodeToLegacyUint160` in migrated downstream paths.
3. Replace identity-bearing fields, maps, sets, API parameters/results, cache keys,
   and database identity segments directly. Use `ToKeyString()` for textual keys.
4. Store only one current downstream record layout and one canonical layout for each
   newly changed identity-bearing payload.
5. Preserve contract/EVM `uint160` values and every transaction, dispute, bond,
   source-edge, block, and internal `uint256` hash.
6. Reject malformed or noncanonical typed identities; never truncate or infer a
   destination type.

## Glossary

- **Canonical identity:** `CVM::TrustNodeId { type, data }` for one of the five
  supported destination types.
- **Canonical key segment:** the exact string returned by `ToKeyString()`,
  `<type:02x>-<data:64 lowercase hex>`.
- **Downstream records:** HAT metrics, reputation, wallet clusters, bonded votes,
  DAO disputes/votes, propagated edges, and their indexes/caches.
- **Clean development state:** a CVM database created by the converted binary, or a
  development database whose not-live downstream records were discarded before use.

Values outside the migration boundary stay unchanged:

| Classification | Examples | Representation |
|---|---|---|
| User identity | reputation target/voter; HAT target/viewer; wallet cluster/member; bonded voter/target; DAO challenger/member; propagated source/target | `TrustNodeId` |
| Contract/EVM address | `Contract::address`, call/deployment address, EVM account/storage address | `uint160` |
| Protocol hash | transaction ID, `bondTxHash`, `slashTxHash`, `sourceEdgeTx`, `disputeId`, `originalVoteTx`, block ID | `uint256` |
| Internal fixed-width value | code/state hashes and non-identity storage keys | existing type |

## Bug Details

The RPC boundary already recognizes the five supported destinations, but downstream
APIs and models still require `uint160`. This rejects 32-byte identities and merges
typed 20-byte identities that share the same payload. Wallet clustering additionally
extracts only `CKeyID` and renders members as P2PKH.

## Expected Behavior

Every downstream user-identity path carries a strictly canonical `TrustNodeId` from
input through APIs, records, keys, caches, payloads, and output. All five supported
destination types round-trip without collision. Current records and newly changed
OP_RETURN payloads have one typed layout, development state is clean or rebuilt, and
contract/EVM addresses, hash domains, TrustEdge v1/v2 behavior, and the activated
block-processing path remain unchanged.

## Hypothesized Root Cause

The migration stopped after the WoT core: downstream models retained `uint160`
fields and APIs, RPC code bridged back to those narrow interfaces, clustering only
accepted `CKeyID`, and rendering reconstructed every 20-byte value as P2PKH. Because
the downstream formats were still under development, their storage and payload
models were never finalized around the typed identity already available at the RPC
boundary.

The direct fix removes those narrow boundaries instead of preserving them.

```pascal
FUNCTION expectedBehavior(operation)
  result <- execute(operation)
  RETURN noWidthRejection(result)
         AND exactTrustNodeIdRoundTrip(result)
         AND distinctTypedIdentitiesDoNotCollide(result)
         AND typedClusterExtractionAndRendering(result)
END FUNCTION
```

Concrete post-fix examples:

- A quantum target reaches SecureHAT and reputation without conversion to `uint160`.
- P2PKH, P2SH, and P2WPKH nodes with equal 20-byte payloads have distinct fields,
  collection entries, and keys because their type tags differ.
- A common-input transaction containing P2SH, P2WSH, and quantum prevouts links all
  supported resolved inputs under the existing heuristic.
- A P2SH cluster member is rendered through `ToDestination()` as P2SH.
- A stale development record is not interpreted by an alternate reader; operators
  clean/rebuild development state.

## Correctness Properties

### Property 1: Canonical identity round-trip

_For any_ supported P2PKH, P2SH, P2WPKH, P2WSH, or quantum destination,
`FromDestination` followed by `ToDestination`, and serialization followed by
deserialization, preserve the exact type and data.

**Validates: Requirements 2.1, 2.8, 2.11**

### Property 2: Collision safety

_For any_ two canonical `TrustNodeId` values that differ by type or data, their
serialized identities, `ToKeyString()` values, map/set keys, and cache keys are
distinct.

**Validates: Requirements 2.9, 2.11, 2.12**

### Property 3: Native downstream API closure

_For any_ supported target/viewer/member identity, SecureHAT, reputation, wallet
clustering, cluster trust, bonded-vote/DAO, and propagation APIs accept
`TrustNodeId`, retain it through internal calls, and retrieve the same identity.

**Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7**

### Property 4: Current record round-trip

_For any_ current downstream HAT, reputation, cluster, bonded-vote, DAO, or
propagation record with bounded containers, serialization and deserialization
reproduce every field, consume all bytes, and reject malformed or trailing data.

**Validates: Requirements 2.8, 2.11, 5.1**

### Property 5: Canonical payload round-trip

_For any_ supported identities and valid scalar/hash fields, each newly changed
reputation, bonded-vote, DAO-dispute, or DAO-vote payload has one accepted canonical
encoding; parsing reproduces every field and exact reserialization reproduces the
same bytes.

**Validates: Requirements 2.10, 2.11, 4.4, 4.5**

### Property 6: Five-type clustering extraction

_For any_ transaction with supported resolved prevouts, `AnalyzeTransaction`
extracts each exact typed destination and applies the existing common-input and
change heuristics independent of destination type. Unsupported or unresolved
prevouts remain ignored.

**Validates: Requirements 2.4, 2.5, 4.6**

### Property 7: Cluster persistence and rendering

_For any_ typed cluster, save/load and chain/index rebuild produce the same member
sets and deterministic cluster IDs; every rendered member decodes to its stored
`TrustNodeId`.

**Validates: Requirements 2.4, 3.2, 4.6**

### Property 8: Idempotency

_For any_ valid typed write or processing operation, repeating the same operation or
rebuilding from the same active-chain input produces an identical logical state with
no duplicate entries.

**Validates: Requirements 2.13, 3.2, 4.4**

### Property 9: Fixed-width scope preservation

_For any_ contract/EVM address or transaction/dispute/bond/source/internal hash,
serialization, key construction, equality, and derivation remain byte-for-byte
unchanged.

**Validates: Requirements 4.1, 4.2**

### Property 10: Completed-feature preservation

_For any_ input covered by `web-of-trust-fixes` or `trust-system-activation`, the
completed `TrustEdge`/`CVMTrustEdgeData` v1/v2 and block-processing behaviors remain
unchanged, including validation, quantum parsing, and reconnect handling.

**Validates: Requirements 4.3, 4.4, 4.5, 4.6, 4.7**

## Fix Implementation

### 1. Canonical Identity and Validation

Use `TrustNodeId` unchanged. Do not add fields, change tag values, alter
serialization, or modify `ToKeyString()`. All migrated call sites validate:

- type is one of `1..5`;
- P2PKH, P2SH, and P2WPKH have zero in the high 12 bytes;
- P2WSH and quantum retain all 32 bytes;
- `ToDestination()` returns a supported destination;
- textual key input exactly matches the canonical lowercase `ToKeyString()` shape.

A strict parsing helper may be added around the unchanged type if needed, but no
conversion through `uint160` is permitted in downstream identity paths. Invalid
stored identities are errors, not candidates for type inference.

### 2. Direct Data-Model and API Conversion

**HAT/SecureHAT** — `src/cvm/securehat.{h,cpp}`,
`behaviormetrics.{h,cpp}`, `graphanalysis.{h,cpp}`

Convert target/viewer/address fields and signatures, `TradeRecord.partner`, unique
partner sets, graph node/member/entry-point sets, queues, maps, and caches to
`TrustNodeId`. `GraphAnalyzer` enumerates only canonical forward TrustEdges through
the existing TrustGraph key-shape-aware path and never narrows an edge endpoint.

Representative signatures:

```cpp
int16_t CalculateFinalTrust(const TrustNodeId& target,
                            const TrustNodeId& viewer);
BehaviorMetrics GetBehaviorMetrics(const TrustNodeId& address);
GraphMetrics GetGraphMetrics(const TrustNodeId& address);
```

**Reputation** — `src/cvm/reputation.{h,cpp}`

Convert reputation address/target/voter fields and every corresponding API,
secondary index, activity key, low-reputation enumeration, and cache to
`TrustNodeId`. Transaction-pattern data remains keyed by transaction hashes where it
is transaction-domain data.

**Bonded vote and DAO** — `src/cvm/trustgraph.{h,cpp}` and callers

Convert `BondedVote.voter/target`, `DAODispute.challenger`, and
`daoVotes`/`daoStakes` map keys to `TrustNodeId`. Convert DAO member and vote APIs
accordingly. Keep `bondTxHash`, dispute IDs, original vote IDs, slash IDs, and reward
distribution IDs as `uint256`; quorum, stake, slashing, reward, and threshold logic is
unchanged.

**Wallet clustering and propagation** — `src/cvm/walletcluster.{h,cpp}`,
`clustertrustquery.{h,cpp}`, `trustpropagator.{h,cpp}`, and
`clusterupdatehandler.{h,cpp}`

Convert cluster IDs, members, union-find maps, address indexes, query signatures,
cluster summaries, propagated source/target/original-target fields, and LRU cache
keys to `TrustNodeId`. Keep transaction and source-edge identifiers as `uint256`.
Deduplication uses exact typed pairs, not low 20-byte projections.

**RPC and transaction builders** — `src/rpc/cvm.cpp`, `src/cvm/txbuilder.{h,cpp}`

Pass decoded `TrustNodeId` values directly. Remove downstream uses of
`TrustNodeToLegacyUint160`; remove the helper if no valid call sites remain. Render
stored identities only with:

```cpp
EncodeDestination(node.ToDestination())
```

An invalid stored destination produces a data/internal error instead of a fabricated
P2PKH address. Builders and signer out-parameters carry typed user identities while
contract addresses and hash parameters keep their native types.

### 3. Single Current Record Layouts and Keys

Each changed downstream record serializes its fields directly with `TrustNodeId` in
the current field order. Do not add downstream record envelopes solely for this
conversion. Readers accept only the current typed layout and require complete stream
consumption and bounded containers.

Use existing canonical textual prefixes with `ToKeyString()` identity segments:

| Data | Single current key |
|---|---|
| Behavior metrics | `behavior_<TNI>` |
| Stake info | `stake_<TNI>` |
| Temporal metrics | `temporal_<TNI>` |
| Reputation enumeration | `repidx_<TNI>` |
| Reputation activity/history | `txhist_<TNI>` |
| Wallet cluster | `wc_<clusterTNI>` |
| Address-to-cluster | `wca_<memberTNI>` |
| Address transaction index | `txcluster_addr_<TNI>` |
| Transaction input index | `txcluster_tx_<txid64>` with `vector<TrustNodeId>` |
| Bonded vote primary | `vote_<txid64>` |
| Target vote index | `votes_<targetTNI>_<txid64>` |
| DAO dispute primary | `dispute_<id64>` |
| DAO vote transaction index | `dispute_by_vote_<txid64>` |
| Propagated edge | `trust_prop_<fromTNI>_<toTNI>` |
| Propagated source index | `trust_prop_idx_<source64>_<toTNI>` |
| Cluster trust summary | `cluster_trust_<clusterTNI>` |

`<TNI>` is exactly `ToKeyString()`. Hash-only keys keep their current shape, while
the values they reference carry typed identities. Key parsers reject extra
separators, trailing characters, invalid tags, uppercase/noncanonical hex, and
nonzero high padding for 20-byte types.

The reputation primary raw key is the one intentional namespace rename:
`reputation_<TNI>`. This single current name materially avoids accidental overlap
with stale development raw-`R` records. No old reputation namespace is queried.
No other renamed parallel namespace is introduced.

A clean development database contains only these current key/value shapes. If stale
not-live records are present, startup or the affected subsystem reports that a clean
rebuild is required; it does not attempt alternate parsing.

### 4. Canonical Identity-Bearing OP_RETURN Payloads

Keep the outer `CVM_MAGIC`, current operation types, integer byte order, and general
size validation. The operation type selects one payload family, and each changed
family has one exact typed body:

| Payload | Canonical body | Exact body size |
|---|---|---:|
| Reputation | `target:TNI33, vote:int16, timestamp:uint32` | 39 bytes |
| Bonded vote | `voter:TNI33, target:TNI33, vote:int16, bond:int64, timestamp:uint32` | 80 bytes |
| DAO dispute | `originalVoteTx:uint256, challenger:TNI33, bond:int64, timestamp:uint32` | 77 bytes |
| DAO vote | `disputeId:uint256, member:TNI33, support:uint8, stake:int64, timestamp:uint32` | 78 bytes |

No payload version byte is needed because there is one current format. Parsers
require the exact size, canonical identities, valid scalar ranges, and complete
consumption. Builders emit only these forms. The existing feature activation that
already governs the relevant operation remains authoritative; no dedicated migration
activation parameter or equivalent new gate is added.

Any separate reputation marker or wrapper that carries identity is likewise changed
directly to one typed current layout. It is not given an alternate parser.

`CVMTrustEdgeData` is explicitly excluded. Its completed v1/v2 detection, encoding,
validation, and processing remain byte-for-byte and behaviorally unchanged.

### 5. Wallet Clustering and Rebuild

`AnalyzeTransaction` resolves prevouts as today, calls `ExtractDestination`, and then
`TrustNodeId::FromDestination`. It includes exactly the five supported destination
types and ignores unresolved prevouts, invalid indexes, `CNoDestination`, and
`WitnessUnknown`.

The existing common-input heuristic is retained: deduplicate exact typed inputs and,
when at least two remain, union them. The existing two-output lower-value change
heuristic is retained and applied to any supported typed change destination. Equal
data with different type tags remains distinct.

Union-find maps use `TrustNodeId`, with path compression and the existing efficient
union strategy. The externally visible cluster ID is deterministic (the minimum
member under `TrustNodeId::operator<`) regardless of transaction/union order.
Persistence sorts and deduplicates records and indexes before upsert.

`buildwalletclusters` is the supported way to materialize clusters after conversion.
It scans the applicable active-chain/index source, extracts typed destinations, and
writes only current cluster records. Repeating the rebuild with the same input
produces the same cluster IDs, member sets, and indexes. There is no automatic
startup scan.

The heuristic privacy limitations remain: common-input ownership and smaller-output
change inference can produce false positives and expose linkability. RPC and user
documentation continue to describe clusters as heuristic rather than verified
ownership.

### 6. Error Handling, Security, Performance, and Observability

**Error handling and atomicity**

- Reject unknown destination types, malformed keys, noncanonical identities, invalid
  exact payload sizes, oversized containers, trailing bytes, and invalid scalar
  ranges before committing state.
- Unsupported destinations continue to produce the existing address error; only the
  obsolete width-specific rejection is removed.
- A malformed current record is an error. Background enumeration may skip and count
  it, but may not reinterpret it.
- Primary writes precede secondary-index writes. An index failure is reported, and a
  repeated idempotent upsert repairs the index without duplicate logical state.

**Collision and domain safety**

- Stable identity tags 1 through 5 are not renumbered.
- Distinct `(type,data)` values remain distinct in all records, keys, collections,
  caches, and payloads.
- No P2WSH/quantum identity is projected to 20 bytes.
- Contract/EVM and hash domains do not enter identity namespaces.

**Performance**

- Exact primary/index lookups remain O(1).
- Union-find remains near-linear with path compression and the existing union
  strategy; sorting/deduplication occurs at persistence boundaries.
- Graph analysis uses canonical TrustGraph APIs instead of broad raw scans.
- Existing propagation limits, cache limits, and clustering analysis bounds remain.
- No compatibility scan or startup chain scan is introduced.

**Observability**

Use existing CVM logging with rate limiting. Count malformed current keys/records,
failed index writes, rebuild starts/completions, rejected payloads, and idempotent
replay skips. Log bounded natural IDs or `ToKeyString()` only when needed; never log
wallet secrets or complete cluster member lists.

### 7. Rollout

This is one atomic pre-launch implementation change:

1. Convert models and native APIs to `TrustNodeId`.
2. Convert current serializers, database keys/indexes, and caches.
3. Convert clustering extraction, persistence, rebuild, and rendering.
4. Convert the four newly changed OP_RETURN payload families and their builders,
   validators, and block processors to their one canonical layouts.
5. Remove downstream width conversions and run clean-state and preservation tests.
6. Before starting the converted binary, discard/rebuild any development CVM state
   that contains the not-live downstream `uint160` layouts; run
   `buildwalletclusters` when cluster materialization is required.

No chain activation parameter, startup transformation, or mixed-format operation is
part of rollout. Existing `web-of-trust-fixes` and `trust-system-activation`
behavior remains active throughout.

### 8. Concrete File and Symbol Inventory

| Files | Intended changes |
|---|---|
| `src/cvm/securehat.{h,cpp}`, `behaviormetrics.{h,cpp}`, `graphanalysis.{h,cpp}` | typed HAT fields/APIs/collections/current records |
| `src/cvm/reputation.{h,cpp}` | typed reputation model/APIs/current keys and indexes |
| `src/cvm/walletcluster.{h,cpp}`, `clustertrustquery.{h,cpp}` | typed union-find, extraction, persistence, queries, rendering |
| `src/cvm/trustpropagator.{h,cpp}`, `clusterupdatehandler.{h,cpp}` | typed propagated records, summaries, indexes, caches |
| `src/cvm/trustgraph.{h,cpp}` | typed bonded-vote and DAO fields/maps/APIs; TrustEdge untouched |
| `src/cvm/softfork.{h,cpp}` | one current typed layout for four newly changed payloads; TrustEdge untouched |
| `src/cvm/txbuilder.{h,cpp}`, `blockprocessor.{h,cpp}` | typed builder/processor values with existing activation behavior |
| `src/rpc/cvm.cpp` | direct typed calls and typed address rendering |
| `src/cvm/hat_consensus.cpp` and DAO/reward callers | typed identities without hash/threshold changes |
| `src/Makefile.test.include`, `src/test/` | register direct-format and preservation tests |

`src/cvm/trustnodeid.{h,cpp}` is not redesigned. A strict external parsing/validation
helper may use it, but the type itself remains unchanged.

### 9. Requirement Traceability

| Requirements | Design coverage |
|---|---|
| 1.1–1.4 | Native API/model conversion and direct current persistence |
| 1.5–1.6 | Five-type clustering extraction and typed rendering |
| 1.7–1.9 | Typed bonded/DAO/propagation models and canonical payloads |
| 2.1–2.7 | Sections 1–2; direct subsystem conversion |
| 2.8–2.9 | Section 3; one current layout and canonical keys |
| 2.10–2.12 | Sections 1, 3, and 4; strict payload/identity/collision rules |
| 2.13 | Sections 5–7; deterministic upsert/replay/rebuild |
| 3.1–3.3 | Clean development state, explicit discard, and typed rebuild |
| 4.1–4.2 | Fixed-width scope table and preservation property |
| 4.3–4.7 | Completed WoT/activation and existing algorithm preservation |
| 4.8 | Native typed APIs retain existing 20-byte destination behavior |
| 5.1–5.4 | Section 6 error, performance, and observability rules |

## Testing Strategy

### Validation Approach

Run targeted unit/property tests for the unchanged identity type at every subsystem
boundary, current-record and payload tests, clustering tests, then block/RPC
integration tests. Finally rerun the completed WoT and activation suites. Tests start
from a clean development CVM state or explicitly discard stale downstream fixtures.

### Unit Tests

- Cover all five destination types through `FromDestination`, `ToDestination`,
  serialization, strict validation, `ToKeyString()`, and RPC rendering.
- For every changed record, generate a direct current-format fixture, round-trip it,
  require complete input consumption, and verify nested identity collections preserve
  type, bytes, cardinality, and values.
- Verify exact current keys, including the one `reputation_<TNI>` namespace, and
  rejection of malformed/noncanonical key segments.
- Verify canonical payload sizes 39, 80, 77, and 78 bytes; exact field order and
  integer encoding; malformed/truncated/trailing data; invalid types/high padding;
  and scalar validation.
- Keep existing `CVMTrustEdgeData` v1/v2 golden fixtures unchanged.
- Verify compile-time/runtime preservation of contract/EVM `uint160` fields and all
  transaction/dispute/bond/source `uint256` fields.
- Verify no affected downstream API or cache key narrows a `TrustNodeId`.

### Property-Based Tests

Generate canonical identities uniformly across all five tags, including equal data
under different tags; bounded current records; valid/invalid payloads; and
transactions with resolvable, unsupported, and unresolved prevouts. Record the seed
and minimized counterexample on failure.

Executable properties:

1. `decode(encode(node)) == node` for all five types.
2. Distinct canonical nodes have distinct serialization and keys.
3. Each current downstream record round-trips exactly and consumes all bytes.
4. Each canonical payload round-trips exactly; every other length or malformed
   identity is rejected.
5. Native downstream API writes are retrievable only through the same exact typed
   identity.
6. Common-input components contain all and only supported resolved input identities.
7. Existing change-selection behavior is invariant under supported destination-type
   substitution.
8. Cluster IDs/member sets are invariant under transaction and union-order
   permutations.
9. Save/load/rebuild and repeated transaction/block processing are idempotent.
10. RPC output decodes to each stored typed identity.
11. Contract/EVM addresses and hash fields serialize exactly as before.
12. Existing TrustEdge v1/v2 and trust-system activation properties still pass.

### Integration Tests

- Exercise reputation, four HAT RPCs, wallet-cluster/cluster-trust RPCs, bonded votes,
  DAO paths, and propagation with each supported destination type as applicable;
  assert no width rejection and canonical output.
- Mine/process the one current reputation, bonded-vote, DAO-dispute, and DAO-vote
  payload forms, then compare resulting state with equivalent direct API writes.
- Repeat block processing/reconnect and assert no duplicate reputation application,
  votes, dispute-member entries, cluster indexes, or propagation entries.
- Build mixed-type common-input and two-output change transactions; run
  `buildwalletclusters` twice and across restart; verify stable IDs, member sets,
  indexes, and original address encodings.
- Start with a clean CVM state, write all current record families, restart, and verify
  direct new-format round trips.
- As an operational test, seed stale not-live downstream development keys and verify
  the documented clean-rebuild error/path rather than data reinterpretation.
- Rerun `trustnodeid_tests`, relevant quantum and propagation suites,
  `cvm_wot_fix_property_tests`, `cvm_wot_preserve_tests`,
  `cvm_trust_activation_fix_property_tests`, and
  `cvm_trust_activation_preserve_tests`.

Representative validation commands after implementation:

```bash
src/test/test_cascoin --run_test=trustnodeid_full_migration_tests
src/test/test_cascoin --run_test=trustnodeid_tests
src/test/test_cascoin --run_test=trustpropagator_tests
src/test/test_cascoin --run_test=cvm_wot_fix_property_tests
src/test/test_cascoin --run_test=cvm_trust_activation_fix_property_tests
```

Functional/regtest coverage runs through `test/functional/test_runner.py`. No
`tasks.md` is created by this design revision.
