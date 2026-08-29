# Bugfix Requirements Document

## Introduction

The `web-of-trust-fixes` work migrated the Web-of-Trust core to the existing
`CVM::TrustNodeId` type, which represents P2PKH, P2SH, P2WPKH, P2WSH, and
quantum/Falcon (`WitnessV2Quantum`) destinations without truncation. The
`trust-system-activation` work enabled the non-contract CVM block-processing path.
Those completed behaviors, including the `TrustEdge`/`CVMTrustEdgeData` v1/v2
rules, are outside the simplification made by this bugfix and must remain unchanged.

Downstream HAT, reputation, wallet-clustering, DAO, bonded-vote, and trust-
propagation formats have not gone live. This spec therefore defines a direct
pre-launch source and data-model conversion from user-identity `uint160` values to
`TrustNodeId`. It intentionally provides no reader, migration, fallback, or
compatibility guarantee for development CVM databases containing the old downstream
`uint160` layouts. Such databases may be discarded and rebuilt. Wallet clusters are
rebuilt from chain/index data using the new typed representation.

The defect is that downstream code still converts valid typed identities to
`uint160`, which rejects P2WSH and quantum identities and erases the destination type
of 20-byte identities. The fix replaces user-identity fields, collection keys, API
signatures, cache keys, serialized fields, and database key segments directly with
`TrustNodeId`; textual database identity segments use `TrustNodeId::ToKeyString()`.
Newly changed reputation, bonded-vote, DAO-dispute, and DAO-vote OP_RETURN payloads
have one canonical typed layout and require no separate activation height.

The scope boundary is strict: only user identities change. Contract and EVM
20-byte addresses remain `uint160`; transaction, dispute, bond, source-edge, block,
and internal protocol hashes remain `uint256`; other internal fixed-width values
retain their current types.

## Bug Condition

```pascal
FUNCTION isBugCondition(op)
  node <- DecodeTrustNode(op.identity)
  RETURN isDownstreamUserIdentityOperation(op)
         AND (pathRequiresUint160(op) OR pathErasesDestinationType(op))
END FUNCTION
```

The fixed implementation satisfies:

```pascal
FOR ALL op WHERE isBugCondition(op) DO
  result <- fixedNode(op)
  ASSERT result has no width rejection
  ASSERT every identity round-trips as the same TrustNodeId
  ASSERT distinct typed identities never share a key or collection entry
END FOR
```

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN an affected RPC decodes a P2WSH or quantum/Falcon address and then calls
`TrustNodeToLegacyUint160` THEN it rejects the otherwise supported address with
`RPC_INVALID_ADDRESS_OR_KEY` because the downstream API cannot carry 32 bytes.

1.2 WHEN SecureHAT or HAT metric code keys `StakeInfo`, `TemporalMetrics`,
`BehaviorMetrics`, or `GraphMetrics` by `uint160` THEN P2WSH and quantum identities
cannot be represented, while P2PKH, P2SH, and P2WPKH identities with equal 20-byte
payloads cannot be distinguished.

1.3 WHEN the reputation model, API, cache, index, or serialized record uses a
`uint160` target or voter identity THEN wide identities are rejected or truncated and
typed 20-byte identities can collide.

1.4 WHEN wallet clustering and cluster-trust queries use `uint160` cluster/member
identities THEN they cannot preserve all supported destinations.

1.5 WHEN `WalletClusterer::AnalyzeTransaction` extracts only `CKeyID` prevouts THEN
P2SH, P2WPKH, P2WSH, and quantum inputs do not participate in the existing common-
input heuristic.

1.6 WHEN a cluster RPC renders a stored member as `CKeyID` THEN a non-P2PKH member is
reported with the wrong address type.

1.7 WHEN `BondedVote.voter/target`, `DAODispute.challenger`, or the keys of
`DAODispute.daoVotes/daoStakes` use `uint160` THEN the records cannot represent all
five user-identity types safely.

1.8 WHEN `PropagatedTrustEdge.fromAddress/toAddress/originalTarget`, cluster summaries,
propagation indexes, or propagation caches use `uint160` THEN propagation can lose
identity width or destination type.

1.9 WHEN newly changed reputation, bonded-vote, DAO-dispute, or DAO-vote OP_RETURN
payloads encode identity as 20 bytes THEN they cannot carry every supported identity.

### Expected Behavior (Correct)

#### Direct Typed Conversion

2.1 WHEN an affected RPC or native downstream API receives any supported destination
THEN it SHALL carry the result as `TrustNodeId` without a width conversion or width-
specific rejection.

2.2 WHEN SecureHAT or HAT metrics accept, return, store, index, or cache a user
identity THEN the relevant field, API signature, map/set key, cache key, and database
identity segment SHALL use `TrustNodeId`; textual key segments SHALL use
`ToKeyString()`.

2.3 WHEN reputation code accepts, returns, stores, indexes, or caches a target or
voter identity THEN it SHALL use `TrustNodeId` end to end, and a write followed by a
read SHALL recover the exact type and all identity bytes.

2.4 WHEN wallet clustering, cluster trust, cluster summaries, or transaction/address
indexes carry a user identity THEN they SHALL use `TrustNodeId`; a cluster member
SHALL be rendered through its stored typed destination rather than through `CKeyID`.

2.5 WHEN `WalletClusterer::AnalyzeTransaction` resolves a prevout or change output
THEN it SHALL convert every supported P2PKH, P2SH, P2WPKH, P2WSH, or quantum
destination with `TrustNodeId::FromDestination` and apply the existing clustering
heuristics uniformly to those identities.

2.6 WHEN bonded-vote and DAO data models carry a voter, target, challenger, or DAO
member identity THEN the field or map key SHALL be `TrustNodeId`; bond transaction
hashes and dispute identifiers SHALL remain `uint256`.

2.7 WHEN trust propagation carries a source, target, original target, cluster member,
or cache identity THEN it SHALL use `TrustNodeId`, preserving the exact identity in
records, indexes, conflict handling, and RPC output; source-edge hashes SHALL remain
`uint256`.

2.8 WHEN any changed downstream record is serialized and read in a clean development
state THEN its one current typed layout SHALL round-trip every field exactly and
consume the complete input. No alternate old downstream layout is required.

2.9 WHEN a changed database key contains a user-identity segment THEN that segment
SHALL use `ToKeyString()` in the existing canonical namespace where safe. If one
namespace is renamed to avoid stale development records, the implementation SHALL
have only that current namespace and SHALL NOT consult the old name.

2.10 WHEN a reputation, bonded-vote, DAO-dispute, or DAO-vote OP_RETURN payload is
built or parsed THEN it SHALL use exactly one canonical `TrustNodeId` layout, exact
length checking, complete consumption, and strict identity validation. No additional
consensus activation height SHALL be introduced for these not-live formats.

2.11 WHEN a `TrustNodeId` is accepted from a destination, record, payload, or key THEN
the implementation SHALL accept only stable tags 1 through 5, require zero high 12
bytes for P2PKH/P2SH/P2WPKH, preserve all 32 bytes for P2WSH/quantum, and reject
unknown types or noncanonical values.

2.12 WHEN two user identities differ by type or data THEN their serialized forms,
map/set entries, cache entries, and database keys SHALL remain distinct.

2.13 WHEN the same record, transaction, block-processing operation, cluster build, or
index update is repeated THEN the resulting logical state SHALL be unchanged and
SHALL contain no duplicate members, votes, transactions, disputes, or propagated
edges.

### Development-State and Rebuild Assumption

3.1 WHEN an operator has an existing development CVM database containing the not-live
downstream `uint160` records THEN the operator MAY discard those records or the CVM
database and rebuild it. The converted implementation provides no compatibility
guarantee for those old layouts.

3.2 WHEN wallet-clustering state is needed after conversion THEN wallet clusters SHALL
be rebuilt from chain/index data using typed destination extraction. The rebuild SHALL
be deterministic and idempotent for the same active-chain input.

3.3 WHEN the converted binary opens a clean development state THEN it SHALL read and
write only the one current layout and namespace defined for each downstream record.
It SHALL fail or require a clean rebuild rather than reinterpret stale bytes as typed
identities.

### Unchanged Behavior (Regression Prevention)

4.1 WHEN a value belongs to the contract or EVM address domain THEN it SHALL remain a
20-byte `uint160` with its current derivation, serialization, and database key format.

4.2 WHEN a value is a transaction hash, dispute ID, bond hash, source-edge hash, block
hash, code hash, or other protocol/internal hash THEN it SHALL remain `uint256` with
its current semantics.

4.3 WHEN the already-completed WoT core is exercised THEN `TrustEdge`,
`CVMTrustEdgeData`, key-shape detection, traversal, weighted reputation, RPC behavior,
and existing v1/v2 semantics SHALL remain exactly as delivered by
`web-of-trust-fixes`.

4.4 WHEN non-contract CVM data is connected through `ProcessNonContractBlock` THEN the
ordering, validation boundary, durable-write behavior, and on-chain/direct-write
equivalence established by `trust-system-activation` SHALL remain unchanged.

4.5 WHEN validation evaluates vote ranges, bonds, DAO thresholds, unsupported
`CNoDestination`/`WitnessUnknown` values, malformed data, soft-fork gating, or
`fJustCheck` behavior THEN it SHALL preserve the current result except for accepting
all five canonical identity types in the newly typed fields.

4.6 WHEN clustering applies common-input ownership, two-output change selection,
minimum cluster reputation/HAT, propagation limits, edge conflict resolution, or
direct-over-propagated precedence THEN those semantics SHALL remain unchanged; only
destination extraction, identity storage, and rendering SHALL change.

4.7 WHEN quantum/Falcon parsing, encoding, signature routing, or the existing
`TrustNodeId::FromDestination`/`ToDestination` behavior is exercised THEN it SHALL
remain unchanged.

4.8 WHEN P2PKH, P2SH, or P2WPKH identities use previously working downstream
operations in a clean state THEN their functional results SHALL remain the same while
their exact destination types are now retained.

## Quality Requirements

5.1 WHEN malformed current records, keys, payload lengths, type tags, high-byte
padding, oversized containers, trailing bytes, or unsupported destinations are
encountered THEN the operation SHALL reject them deterministically and SHALL NOT
truncate, guess, or partially apply state.

5.2 WHEN an index write fails after a primary write THEN the operation SHALL report
failure, and repeating the same idempotent update SHALL be able to repair the index
without duplicating logical state.

5.3 WHEN an exact identity lookup is performed THEN its normal database operation
SHALL remain O(1), graph and clustering algorithms SHALL retain their existing
asymptotic bounds, and no automatic startup chain scan SHALL be introduced.

5.4 WHEN errors or rebuilds are logged THEN logs SHALL identify the affected current
namespace and a bounded record identifier, SHALL avoid wallet secrets/full member
lists, and SHALL use existing CVM logging and rate-limiting conventions.

## Acceptance Summary

The bugfix is complete when all five destination types work end to end in each
downstream identity subsystem, strict typed records and payloads round-trip without
collision, wallet clustering extracts and renders typed identities, repeated writes
are idempotent, clean-state/rebuild behavior is documented and tested, and the WoT,
activation, contract/EVM, and hash-domain boundaries remain unchanged.
