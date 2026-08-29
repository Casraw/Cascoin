# Bugfix Requirements Document

## Introduction

The on-chain Web-of-Trust (WoT) subsystem is producing incorrect results when trust
relationships are created, persisted, and queried. The defects were discovered on a
fresh Cascoin regtest node with four funded wallets. An on-chain trust chain
`A → B → C → D` was built with `sendtrustrelation "<addr>" 80 2 "<reason>"` (weight 80,
bond 2 CAS per edge). All three transactions succeeded (`edges_propagated: 1` each) and
were mined, yet reading the edges back and querying transitive trust returned garbage or
empty results.

Four distinct defects are in scope:

1. **Trust-edge persistence/deserialization corruption (primary):** Edges written to the
   CVM database do not round-trip. Reading them back yields field values that do not
   match what was written (weight, bond, reason, timestamp, from-address, slashed flag all
   wrong), and the corrupted `reason` bytes break the CLI with a JSON parse error.
2. **Path-finding returns zero trust paths:** `getweightedreputation` reports
   `paths_found: 0` and `reputation: 0` even for a single stored direct edge, and the edge
   count reported by `gettrustgraphstats` disagrees with `listtrustrelations`.
3. **Only legacy (P2PKH) addresses accepted (regression):** bech32/segwit addresses are
   rejected with `"Address type not supported"`; previously all standard address types
   worked. The same rejection applies to Cascoin quantum/Falcon addresses
   (`casq`/`tcasq`/`rcasq`, Bech32m witness v2, `WitnessV2Quantum`), so no non-legacy
   standard address type can participate in the trust graph. Note that quantum addresses
   are keyed by a 32-byte identifier (`uint256` = SHA256 of the FALCON-512 public key),
   which does not fit the `uint160` key/field type currently used for trust edges.
4. **Single-wallet trust graph broken (regression):** Building and querying a trust graph
   among multiple addresses of a single wallet no longer works; multiple separate wallets
   appear to be required.

These are coding/behavioral defects. The fix must make each defect's triggering condition
no longer produce the wrong result, while preserving all behavior that is currently
correct.

## Bug Analysis

### Current Behavior (Defect)

Bug 1 — TrustEdge serialization round-trip corruption:

1.1 WHEN a trust edge is written to the CVM database by `TrustGraph::AddTrustEdge` (key `trust_<from>_<to>`) and later read back (via `ReadGeneric` + `CDataStream ss(...); ss >> edge;` in `TrustGraph::GetOutgoingTrust`/`listtrustrelations`) THEN the system returns field values that do not equal the stored values (e.g. `weight = 11291` for a stored `80`, `bond_amount = 692717578398.37` for a stored `2`, `timestamp` in the year ~2089, `slashed = 1` although never set).

1.2 WHEN a persisted trust edge is read back THEN the `reason` field contains invalid/binary bytes instead of the stored human-readable string (e.g. "C trusts D").

1.3 WHEN `listtrustrelations` returns an edge whose `reason` contains invalid (non-UTF-8) bytes THEN `cascoin-cli listtrustrelations` fails with "couldn't parse reply from server".

1.4 WHEN a persisted trust edge is read back THEN the reported `from` address matches none of the addresses that created the edges.

Bug 2 — Path-finding does not traverse stored edges:

1.5 WHEN `getweightedreputation "<D>" "<viewer>" 3` is called for the stored 3-hop chain `A → B → C → D` THEN the system returns `paths_found: 0` and `reputation: 0`.

1.6 WHEN `getweightedreputation` is called for a single stored direct edge at `maxdepth 1` (e.g. `B → C` viewed from `B`, or using the exact stored `from` address of a retrievable edge as viewer) THEN the system returns `paths_found: 0` and `reputation: 0`.

1.7 WHEN trust-edge counts are queried THEN `gettrustgraphstats` reports `total_trust_edges: 6` while `listtrustrelations` reports `count: 1` for the same graph state.

1.8 WHEN `getweightedreputation` returns a result THEN it echoes the `viewer` field as a hash160 hex string (e.g. "62d20cebcd...") rather than the base58 address that was supplied.

Bug 3 — bech32 addresses rejected (regression):

1.9 WHEN `sendtrustrelation`/`addtrust` (and the other WoT RPCs) are called with a bech32/segwit address (e.g. `rcas1q...`) OR with a Cascoin quantum/Falcon address (e.g. `rcasq1...`, Bech32m witness v2, decoded as `WitnessV2Quantum`) THEN the system rejects it with `error code -5: "Address type not supported"`, because the RPCs only accept `CKeyID`, `CScriptID`, and `WitnessV0KeyHash` (all `uint160`) and reject every other destination variant.

1.10 WHEN a WoT RPC is called with any non-legacy standard address type — including bech32/segwit and quantum/Falcon addresses — THEN the operation fails, so only legacy base58 P2PKH addresses (`m...`/`n...`) can participate in the trust graph.

1.12 WHEN a WoT RPC is called with a quantum/Falcon address THEN the system cannot key or store the resulting trust edge, because the address maps to a 32-byte `WitnessV2Quantum` (`uint256`) identifier while `TrustEdge.fromAddress`/`TrustEdge.toAddress` and the `trust_<from>_<to>` keys are `uint160` (20 bytes), so the quantum identifier does not fit the existing edge key/field type.

Bug 4 — single-wallet trust graph broken (regression):

1.11 WHEN a trust graph is built among multiple addresses belonging to a single wallet THEN the edges cannot be created, listed, and traversed consistently, so a working trust graph requires multiple separate wallets.

### Expected Behavior (Correct)

Bug 1 — TrustEdge serialization round-trip corruption:

2.1 WHEN a trust edge is written to the CVM database and later read back THEN the system SHALL return field values identical to those written for every field (`fromAddress`, `toAddress`, `trustWeight`, `timestamp`, `bondAmount`, `bondTxHash`, `slashed`, `reason`).

2.2 WHEN a persisted trust edge is read back THEN the `reason` field SHALL equal the exact string that was stored (e.g. "C trusts D").

2.3 WHEN `listtrustrelations` returns persisted edges THEN the `cascoin-cli` client SHALL receive a valid, parseable JSON response (no "couldn't parse reply from server" error) because all string fields round-trip correctly.

2.4 WHEN a persisted trust edge is read back THEN the reported `from` address SHALL equal the address that created the edge.

Bug 2 — Path-finding traverses stored edges:

2.5 WHEN `getweightedreputation "<D>" "<A>" 3` is called for a stored chain `A → B → C → D` with edge weights at or above the traversal threshold THEN the system SHALL find at least one trust path (`paths_found >= 1`) and SHALL return a non-zero weighted reputation, propagating trust across up to `maxTrustPathDepth` (3) hops.

2.6 WHEN `getweightedreputation` is called for a single stored direct edge at `maxdepth 1` (viewer = the edge's `from` address, target = the edge's `to` address) THEN the system SHALL find that path (`paths_found >= 1`).

2.7 WHEN trust-edge counts are queried THEN `gettrustgraphstats` `total_trust_edges` and the number of forward edges enumerated by `listtrustrelations` SHALL be consistent for the same graph state (both counting the same set of forward `trust_<from>_<to>` edges).

2.8 WHEN `getweightedreputation` returns a result THEN the `viewer` field SHALL be reported as the base58 address corresponding to the supplied viewer, and edges SHALL be keyed and queried using the same address representation on write and on read.

Bug 3 — all standard address types accepted:

2.9 WHEN `sendtrustrelation`/`addtrust` (and `getweightedreputation`, `votereputation`, `getreputation`, and the other WoT RPCs) are called with any standard Cascoin address type — legacy P2PKH, P2SH, bech32/segwit, AND Cascoin quantum/Falcon addresses (`casq`/`tcasq`/`rcasq`, Bech32m witness v2, `WitnessV2Quantum`) — THEN the system SHALL accept the address and create/query the corresponding trust edge without an "Address type not supported" error.

2.10 WHEN a trust edge is created with one standard address type and later queried using the same address THEN the system SHALL find and return that edge regardless of the address type used, including quantum/Falcon addresses: a trust edge created with a quantum address SHALL be storable, listable (via `listtrustrelations`/`gettrustgraphstats`), and queryable (via `getweightedreputation` and the other WoT RPCs) using that same quantum address.

2.12 WHEN a trust edge involves a quantum/Falcon address THEN the system SHALL key and store the edge using an identifier wide enough to represent the 32-byte `WitnessV2Quantum` (`uint256`) value without truncation or collision; the trust-edge key and `fromAddress`/`toAddress` field types (currently `uint160`) SHALL accommodate quantum identifiers so that no quantum address information is lost between the address supplied to the RPC and the stored/queried edge. (The concrete key/field representation is a design decision for design.md.)

Bug 4 — single-wallet trust graph works:

2.11 WHEN a trust graph is built among multiple addresses belonging to a single wallet THEN the system SHALL store, list, and traverse those edges correctly, so a functional trust graph can be constructed and queried using addresses from a single wallet.

### Unchanged Behavior (Regression Prevention)

3.1 WHEN a trust edge is created with an invalid weight (outside the range -100 to +100) THEN the system SHALL CONTINUE TO reject it with a validation error.

3.2 WHEN a trust edge is created with a bond below the required bond THEN the system SHALL CONTINUE TO reject it with an "Insufficient bond" error.

3.3 WHEN a trust edge is created with valid legacy (P2PKH) base58 addresses THEN the system SHALL CONTINUE TO store, list, and traverse it correctly (the legacy path that works today must keep working).

3.4 WHEN a trust path traversal encounters an edge with trust weight below the traversal threshold (< 10), a slashed edge, or a visited node (cycle) THEN the system SHALL CONTINUE TO skip that edge as it does today.

3.5 WHEN `getweightedreputation` is called with `viewer == target` (self-view) THEN the system SHALL CONTINUE TO return the average of non-slashed incoming trust.

3.6 WHEN `getweightedreputation` is called with a `maxdepth` outside the range 1 to 10 THEN the system SHALL CONTINUE TO reject it with a "Max depth must be between 1 and 10" error.

3.7 WHEN other persisted CVM records that share serialization infrastructure (bonded votes, DAO disputes, reputation records) are written and read back THEN the system SHALL CONTINUE TO round-trip them correctly.

3.8 WHEN cluster-aware propagation and cluster reputation aggregation are invoked by the WoT RPCs THEN the system SHALL CONTINUE TO produce the same cluster fields (`cluster_id`, `cluster_size`, `edges_propagated`) for inputs that are unaffected by these fixes.

3.9 WHEN quantum/Falcon address handling elsewhere in the system already works (e.g. `DecodeDestination`/`IsQuantumAddress` recognizing `casq`/`tcasq`/`rcasq` addresses, quantum address encoding, and FALCON-512 routing) THEN the system SHALL CONTINUE TO behave identically after the WoT address-type fix, which only extends acceptance in the WoT RPCs and MUST NOT alter existing quantum address parsing, validation, or encoding.

3.10 WHEN a WoT RPC parses/normalizes an address for keying a trust edge THEN the system SHALL CONTINUE TO use the same address representation on write and on read for every supported address type — including quantum/Falcon addresses, whose full 32-byte identifier SHALL be represented consistently on store and on query — consistent with the address-keying requirement in 2.8, so an edge created with a given address is always retrievable by that same address.
