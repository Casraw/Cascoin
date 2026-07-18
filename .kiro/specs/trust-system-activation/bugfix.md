# Bugfix Requirements Document

## Introduction

The Web-of-Trust (WoT) subsystem is designed to be driven **on-chain**: a user
broadcasts a trust relationship with `sendtrustrelation` (or a reputation vote
with `sendcvmvote`, a bonded vote with `sendbondedvote`, a DAO dispute/vote,
etc.), the transaction carries a CVM `OP_RETURN` payload, and when the block
that contains that transaction is connected, the node is supposed to parse the
payload and persist the canonical record (for a trust edge, the
`trust_<from>_<to>` entry read by `listtrustrelations`, `gettrustgraphstats`,
and `getweightedreputation`). The prior `web-of-trust-fixes` spec fixed the
read path, identity resolution, path-based reputation, viewer echo, and
quantum/wide-address support, but discovered that the **on-chain write path is
not wired up**: the block-processing step that would persist these records is
disabled.

Concretely, in `src/validation.cpp` `ConnectBlock()`,
`CVMBlockProcessor::ProcessBlock()` is intentionally disabled — only
`CVMBlockProcessor::ProcessClusterUpdates()` runs. The active CVM path,
`CVM::BlockValidator::ValidateBlock()`, only handles contract transactions
(`CONTRACT_DEPLOY`, `CONTRACT_CALL`, `EVM_DEPLOY`, `EVM_CALL`) and explicitly
**skips** every non-contract CVM transaction type (`TRUST_EDGE`, `BONDED_VOTE`,
`DAO_DISPUTE`, `DAO_VOTE`, `REPUTATION_VOTE`). As a result, the handlers that
persist those records — `ProcessTrustEdge`, `ProcessBondedVote`,
`ProcessDAODispute`, `ProcessDAOVote`, and `ProcessVote` — never fire during
block connection. `ProcessBlock()` was disabled because it re-executed contract
deployments/calls (work now done by `BlockValidator`), producing duplicate work
and expensive per-transaction calculations that hung the node. The disable was
too broad: it also removed the only code path that persists on-chain trust
edges, bonded votes, DAO records, and reputation votes.

The observable consequence is that `sendtrustrelation` reports success
(`edges_propagated >= 1`, a `txid` that gets mined) but no canonical
`trust_<from>_<to>` edge exists afterward. The RPC's own post-broadcast call to
`TrustPropagator::PropagateTrustEdge` writes only *propagated-edge* index
records (a different schema, keyed by a truncated `uint160`) at broadcast time —
it never calls `TrustGraph::AddTrustEdge`, so the canonical forward edge that
the WoT read APIs enumerate is never created. Today a working trust graph can
only be built with `addtrust`, which writes the canonical edge directly; the
on-chain flow the system was designed around does not function end-to-end.

**Bug condition (informal).** Let `F` be the current node and `F'` the fixed
node. The bug is triggered when a connected block contains a non-contract CVM
`OP_RETURN` transaction:

```pascal
FUNCTION isBugCondition(block)
  INPUT: block that is being connected on a CVM-active chain
  OUTPUT: boolean

  RETURN EXISTS tx IN block.vtx WHERE
           NOT tx.IsCoinBase()
           AND hasCVMOpReturn(tx)
           AND opType(tx) IN { TRUST_EDGE, BONDED_VOTE,
                               DAO_DISPUTE, DAO_VOTE, REPUTATION_VOTE }
END FUNCTION
```

```pascal
// Property: Fix Checking - on-chain WoT records are persisted on block connect
FOR ALL block WHERE isBugCondition(block) DO
  connectBlock'(block)
  FOR EACH non-contract CVM tx IN block DO
    ASSERT canonicalRecordFor(tx) is persisted and readable
           by the corresponding WoT/reputation read API
  END FOR
  ASSERT connectBlock'(block) does not hang and does not
         re-execute contract deploy/call work
END FOR
```

```pascal
// Property: Preservation Checking - everything else is unchanged
FOR ALL block WHERE NOT isBugCondition(block) DO
  ASSERT connectBlock(block) = connectBlock'(block)
END FOR
```

## Bug Analysis

### Current Behavior (Defect)

On-chain trust edges are never persisted as canonical edges:

1.1 WHEN a block containing a `TRUST_EDGE` CVM `OP_RETURN` transaction (produced
by `sendtrustrelation`) is connected THEN the system does not persist a canonical
`trust_<from>_<to>` edge, because `CVMBlockProcessor::ProcessBlock()` (and thus
`ProcessTrustEdge`) is disabled in `ConnectBlock()` and `BlockValidator::ValidateBlock()`
skips `TRUST_EDGE` transactions.

1.2 WHEN `sendtrustrelation` returns success (`edges_propagated >= 1`, a mined
`txid`) and the block is connected THEN `listtrustrelations`, `gettrustgraphstats`,
and `getweightedreputation` report no corresponding canonical edge, so the
on-chain trust relationship has no effect on the trust graph.

1.3 WHEN `sendtrustrelation` runs THEN the only persisted side effect is a set of
*propagated-edge* index records written off-chain by `TrustPropagator::PropagateTrustEdge`
at broadcast time (keyed by a truncated `uint160`), and no canonical forward edge
is written via `TrustGraph::AddTrustEdge`.

On-chain reputation votes are never applied:

1.4 WHEN a block containing a `REPUTATION_VOTE` CVM `OP_RETURN` transaction
(produced by `sendcvmvote`) is connected THEN the target's reputation is not
updated, because `ProcessVote` never fires during block connection.

On-chain bonded votes are never persisted:

1.5 WHEN a block containing a `BONDED_VOTE` CVM `OP_RETURN` transaction (produced
by `sendbondedvote`) is connected THEN no bonded-vote record is persisted, because
`ProcessBondedVote` never fires during block connection.

On-chain DAO disputes and DAO votes are never persisted:

1.6 WHEN a block containing a `DAO_DISPUTE` CVM `OP_RETURN` transaction is connected
THEN no dispute record is persisted, because `ProcessDAODispute` never fires during
block connection.

1.7 WHEN a block containing a `DAO_VOTE` CVM `OP_RETURN` transaction is connected
THEN no DAO vote is recorded and no dispute is resolved, because `ProcessDAOVote`
never fires during block connection.

End-to-end regtest evidence of the broken flow:

1.8 WHEN a trust chain `A → B → C → D` is built exclusively with `sendtrustrelation`
and mined THEN `getweightedreputation "<D>" "<A>" 3` returns `paths_found: 0` and
`reputation: 0`, because no canonical edges were persisted from the on-chain payloads;
a working graph currently requires `addtrust` (direct write) instead of the on-chain
path, which is why `test/functional/feature_wot_fixes.py` builds the graph with
`addtrust`.

### Expected Behavior (Correct)

On-chain trust edges are persisted as canonical edges on block connect:

2.1 WHEN a block containing a `TRUST_EDGE` CVM `OP_RETURN` transaction (produced by
`sendtrustrelation`) is connected on a CVM-active chain THEN the system SHALL parse
the payload and persist a canonical `trust_<from>_<to>` edge via the TrustGraph
store, using the on-chain signer as `from` and the payload target as `to`, with the
payload's weight, bond, bond-tx hash, and timestamp.

2.2 WHEN a canonical trust edge has been persisted from an on-chain `sendtrustrelation`
transaction THEN `listtrustrelations`, `gettrustgraphstats`, and `getweightedreputation`
SHALL enumerate and traverse that edge exactly as they do for an edge created by
`addtrust`, so the on-chain path and the direct-write path yield an equivalent
canonical trust graph.

2.3 WHEN a trust chain `A → B → C → D` is built exclusively with `sendtrustrelation`,
mined, and each block connected THEN `getweightedreputation "<D>" "<A>" 3` SHALL find
at least one trust path (`paths_found >= 1`) and return a non-zero weighted reputation,
matching the result that the equivalent `addtrust`-built graph produces.

On-chain reputation votes are applied on block connect:

2.4 WHEN a block containing a `REPUTATION_VOTE` CVM `OP_RETURN` transaction (produced
by `sendcvmvote`) is connected THEN the system SHALL update the target's reputation
score consistently with the vote payload, so `getreputation` reflects the on-chain vote.

On-chain bonded votes are persisted on block connect:

2.5 WHEN a block containing a valid `BONDED_VOTE` CVM `OP_RETURN` transaction (produced
by `sendbondedvote`) is connected THEN the system SHALL persist the corresponding
bonded-vote record so it is available to the reputation and DAO subsystems.

On-chain DAO disputes and DAO votes are persisted on block connect:

2.6 WHEN a block containing a valid `DAO_DISPUTE` CVM `OP_RETURN` transaction is
connected THEN the system SHALL persist the dispute record so it is listable and
queryable via the DAO RPCs.

2.7 WHEN a block containing a valid `DAO_VOTE` CVM `OP_RETURN` transaction is connected
THEN the system SHALL record the DAO vote against the referenced dispute and SHALL
resolve the dispute when the existing resolution threshold is met.

Persistence is confirmation-driven, idempotent, and hang-free:

2.8 WHEN the same on-chain non-contract CVM transaction is processed during block
connection THEN the system SHALL persist its canonical record exactly once (idempotently),
so that block connection, and any reorg/reconnection of the same block, does not create
duplicate or conflicting records, and the on-chain result agrees with the off-chain
propagation side effect already produced by the broadcasting RPC rather than double-counting it.

2.9 WHEN a block containing on-chain non-contract CVM transactions is connected THEN
the system SHALL persist the corresponding records without re-executing contract
deployments/calls and without the expensive per-transaction calculations that previously
caused the node to hang, so block connection completes in bounded time.

### Unchanged Behavior (Regression Prevention)

3.1 WHEN a block contains contract CVM/EVM transactions (`CONTRACT_DEPLOY`,
`CONTRACT_CALL`, `EVM_DEPLOY`, `EVM_CALL`) THEN the system SHALL CONTINUE TO validate
and execute them exactly once via `BlockValidator::ValidateBlock()`, with the same gas
accounting, subsidy/rebate handling, contract-state persistence, and success/failure
semantics as today (the re-enabled non-contract processing MUST NOT duplicate or alter
this path).

3.2 WHEN a block is connected on a CVM-active chain THEN the system SHALL CONTINUE TO
run `ProcessClusterUpdates()` for wallet trust propagation with unchanged results.

3.3 WHEN a block is connected THEN the node SHALL CONTINUE TO complete block connection
without hanging, and overall block-connection performance SHALL remain comparable to the
current behavior (the fix MUST NOT reintroduce the duplicate-work / expensive-calculation
hang that caused `ProcessBlock()` to be disabled).

3.4 WHEN a trust edge is created via `addtrust` (direct write) with valid parameters
THEN the system SHALL CONTINUE TO store, list, and traverse it correctly, identically to
today.

3.5 WHEN an on-chain trust edge is created with an invalid weight (outside -100..+100),
an insufficient bond, or an invalid/unparseable CVM payload THEN the system SHALL CONTINUE
TO reject or ignore it as it does today, without persisting a canonical record and without
failing block validation for the rest of the block.

3.6 WHEN the CVM soft fork is not active at a given height, or a block contains no CVM
`OP_RETURN` transactions THEN block connection SHALL CONTINUE TO behave exactly as today
(no WoT/reputation records written).

3.7 WHEN a block containing a non-contract CVM transaction is validated with
`fJustCheck` (validation-only, e.g. mempool/`TestBlockValidity`) THEN the system SHALL
CONTINUE TO avoid persisting durable state, consistent with the existing `fJustCheck`
contract for contract transactions.

3.8 WHEN the WoT read APIs (`listtrustrelations`, `gettrustgraphstats`,
`getweightedreputation`), quantum/wide-address support, identity resolution, and viewer
echo are exercised for inputs unaffected by this fix THEN the system SHALL CONTINUE TO
produce the same results delivered by the completed `web-of-trust-fixes` work.

3.9 WHEN other persisted CVM records that share serialization/storage infrastructure
(contracts, contract state, nonces, propagated-edge index records) are written and read
back THEN the system SHALL CONTINUE TO round-trip them correctly and remain unchanged by
this fix.
