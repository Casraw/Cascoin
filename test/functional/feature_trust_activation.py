#!/usr/bin/env python3
# Copyright (c) 2025 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end functional regtest for the on-chain Web-of-Trust WRITE path.

Spec: .kiro/specs/trust-system-activation  (bugfix)
Task 1: "Write exploratory bug-condition tests that reproduce ALL defects on
         the UNFIXED code".

PURPOSE
-------
This test drives the on-chain WoT write path end-to-end through the REAL block
connection code (`ConnectBlock`). A user broadcasts a trust relationship
(`sendtrustrelation`) or a reputation vote (`sendcvmvote`), the transaction is
mined, and the block is connected. Each assertion encodes the EXPECTED
(post-fix, per Expected-Behavior clause 2.x) behaviour: the canonical
`trust_<from>_<to>` edge / reputation update SHALL be persisted on block connect
and readable through the WoT/reputation read APIs.

On the CURRENT (unfixed) code these assertions are EXPECTED TO FAIL — every
failure is a concrete counterexample confirming the defect:
`CVMBlockProcessor::ProcessBlock()` is disabled in `ConnectBlock()` and
`BlockValidator::ValidateBlock()` skips every non-contract CVM type, so no
canonical record is ever persisted for a mined non-contract CVM transaction.
After the fix (Alternative B: a slim `ProcessNonContractBlock` dispatch wired
into `ConnectBlock`), the SAME test must pass unchanged (spec task 3.5).

DO NOT fix the production code or this test when it fails here. Surfacing the
counterexamples is the whole point of this task.

Bug-condition coverage (bugfix.md / design.md):
  Case 1  Trust edge not persisted as a canonical edge      (1.1, 1.2, 1.3 -> 2.1, 2.2)
  Case 2  Trust edge does not enable a weighted-rep path     (1.8       -> 2.3)
  Case 3  Reputation vote not applied on connect            (1.4       -> 2.4)

Bonded-vote / DAO defects (1.5, 1.6, 1.7 -> 2.5, 2.6, 2.7) are additionally
surfaced by the unit suite `cvm_trust_activation_explore_tests` (which observes
the canonical LevelDB records directly). The DAO RPCs (`createdispute`,
`votedispute`) persist a best-effort record at BROADCAST time, so reading them
back via the DAO RPCs does not cleanly distinguish the connect-time persistence
defect; that distinction is exercised by the fix-check suite (task 4).

FIX-CHECK EXTENSIONS (task 4, run AFTER the fix — EXPECTED TO PASS)
------------------------------------------------------------------
The methods below map to the design's Correctness Properties P1–P4 and drive the
persistence flow end-to-end through the real `ConnectBlock`:
  P1  On-chain A→B→C→D chain built only with sendtrustrelation is traversable
      and yields a non-zero weighted reputation                        (2.3)
  P2  A mined BONDED_VOTE persists a bonded-vote record on connect      (2.5)
  P3  Disconnect → reconnect (invalidateblock/reconsiderblock) of a block
      persists each canonical record exactly once — no duplicate edge,
      no double-counted reputation                                     (2.8)
The remaining P2 DAO-record cases (2.6, 2.7) and P4 (2.9) are exercised by the
unit/property suite `cvm_trust_activation_fix_property_tests`.

_Requirements: 1.1, 1.2, 1.3, 1.4, 1.8, 2.3, 2.5, 2.8_
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)

# The regtest sha256d target needs more than the default 1,000,000 hashing
# attempts per block, so mine with a generous maxtries.
MINE_MAXTRIES = 100000000


class TrustActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Regtest activates CVM at height 0 and quantum at height 1. Mine with
        # sha256d so the CPU-based generate RPC can find regtest blocks quickly.
        self.extra_args = [['-powalgo=sha256d']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Maturing coinbase outputs for the single test wallet")
        # Quantum activates at regtest height 1, after which getnewaddress()
        # returns quantum addresses. Mine the coinbase to an explicit legacy
        # address so the on-chain RPCs can fund bonds/fees from spendable legacy
        # (ECDSA) UTXOs.
        self.funding_address = node.getnewaddress("", "legacy")
        self.mine_until_funded(node, min_balance=10)

        self.check_trust_edge_persisted_on_connect()
        self.check_reputation_vote_applied_on_connect()

        self.log.info("Trust-activation on-chain write-path exploration complete")

        # --- Fix-check extensions (task 4, Correctness Properties P1–P4) ---
        # P1 (single on-chain edge persisted & traversable) is covered above by
        # check_trust_edge_persisted_on_connect; the A→B→C→D chain-equivalence
        # case (2.3) is exercised at the unit level in
        # cvm_trust_activation_fix_property_tests (sendtrustrelation always signs
        # `from` with the wallet's own key, so a multi-hop chain cannot be built
        # from a single wallet here).
        self.check_bonded_vote_persisted_on_connect()     # P2 -> 2.5
        self.check_reorg_idempotent()                     # P3 -> 2.8

        self.log.info("Trust-activation fix-check (P2, P3) complete")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def mine_until_funded(self, node, min_balance):
        """Mine (in batches) to the legacy funding address until the wallet has
        at least ``min_balance`` mature, spendable coins."""
        attempts = 0
        while node.getbalance() < min_balance:
            node.generatetoaddress(25, self.funding_address, MINE_MAXTRIES)
            attempts += 1
            if attempts >= 60:
                raise AssertionError(
                    "could not mine a spendable balance on regtest (height=%d, balance=%s)"
                    % (node.getblockcount(), node.getbalance()))
        self.log.info("  funded wallet: balance=%s at height=%d"
                      % (node.getbalance(), node.getblockcount()))

    def canonical_listing(self):
        """Return listtrustrelations and assert count matches the edge list."""
        result = self.nodes[0].listtrustrelations()
        if "edges" not in result or "count" not in result:
            raise AssertionError("listtrustrelations reply missing edges/count: %s" % result)
        assert_equal(result["count"], len(result["edges"]))
        return result

    # ------------------------------------------------------------------
    # Case 1 + Case 2 — on-chain trust edge persisted & traversable
    # ------------------------------------------------------------------
    def check_trust_edge_persisted_on_connect(self):
        node = self.nodes[0]
        self.log.info("=== On-chain sendtrustrelation must persist a canonical edge ===")

        before = self.canonical_listing()["count"]

        # Broadcast an on-chain trust relationship and mine it into a block.
        target = node.getnewaddress("", "legacy")
        res = node.sendtrustrelation(target, 80, 2, "on-chain trust activation")
        if "txid" not in res:
            raise AssertionError("sendtrustrelation reply missing txid: %s" % res)
        assert_greater_than_or_equal(res["edges_propagated"], 1)
        node.generatetoaddress(1, self.funding_address, MINE_MAXTRIES)
        self.log.info("  broadcast + mined sendtrustrelation (txid=%s, to=%s)"
                      % (res["txid"], target))

        # --- 2.1 / 2.2: a canonical trust_<from>_<to> edge now exists ---------
        after = self.canonical_listing()
        # EXPECTED (post-fix): exactly one new canonical forward edge is enumerated.
        # UNFIXED: ConnectBlock never persists the canonical edge -> count unchanged.
        assert_equal(after["count"], before + 1)

        edge = None
        for e in after["edges"]:
            if e.get("to") == target:
                edge = e
                break
        if edge is None:
            raise AssertionError(
                "Case 1 (2.1/2.2): no canonical edge to the on-chain target %s was "
                "persisted after sendtrustrelation was mined and connected; "
                "ConnectBlock does not persist on-chain trust edges." % target)

        assert_equal(edge["to"], target)
        assert_equal(edge["weight"], 80)
        assert_equal(edge["slashed"], False)
        signer = edge["from"]
        self.log.info("  canonical edge persisted: %s -> %s (weight=%d)"
                      % (signer, target, edge["weight"]))

        # gettrustgraphstats must agree with the enumeration count (2.2).
        stats = node.gettrustgraphstats()
        if "total_trust_edges" not in stats:
            raise AssertionError("gettrustgraphstats reply missing total_trust_edges: %s" % stats)
        assert_equal(stats["total_trust_edges"], after["count"])

        # --- 2.3: the persisted on-chain edge is traversable ------------------
        rep = node.getweightedreputation(target, signer, 1)
        # EXPECTED (post-fix): the single persisted edge signer -> target is found.
        # UNFIXED: no canonical edge exists, so no path is found.
        assert_greater_than_or_equal(rep["paths_found"], 1)
        assert_greater_than(abs(float(rep["individual_reputation"])), 0.0)
        self.log.info("  on-chain edge traversable: getweightedreputation paths_found=%d"
                      % rep["paths_found"])

    # ------------------------------------------------------------------
    # Case 3 — on-chain reputation vote applied on connect
    # ------------------------------------------------------------------
    def check_reputation_vote_applied_on_connect(self):
        node = self.nodes[0]
        self.log.info("=== On-chain sendcvmvote must update reputation on connect ===")

        target = node.getnewaddress("", "legacy")
        base = node.getreputation(target)
        base_score = int(base["score"])
        base_votes = int(base["votecount"])
        self.log.info("  baseline reputation for %s: score=%d votecount=%d"
                      % (target, base_score, base_votes))

        vote_value = 100
        res = node.sendcvmvote(target, vote_value, "on-chain reputation activation")
        if "txid" not in res:
            raise AssertionError("sendcvmvote reply missing txid: %s" % res)
        node.generatetoaddress(1, self.funding_address, MINE_MAXTRIES)
        self.log.info("  broadcast + mined sendcvmvote (txid=%s, vote=%d)"
                      % (res["txid"], vote_value))

        after = node.getreputation(target)
        after_score = int(after["score"])
        after_votes = int(after["votecount"])

        # EXPECTED (post-fix): the mined vote is applied on connect, so the score
        # moves by the vote value and the vote count increments.
        # UNFIXED: ProcessVote never fires during ConnectBlock -> unchanged.
        assert_equal(after_score, base_score + vote_value)
        assert_equal(after_votes, base_votes + 1)
        self.log.info("  reputation updated on connect: score=%d votecount=%d"
                      % (after_score, after_votes))

    # ------------------------------------------------------------------
    # P2 — on-chain bonded vote persisted on connect (2.5)
    # ------------------------------------------------------------------
    def check_bonded_vote_persisted_on_connect(self):
        node = self.nodes[0]
        self.log.info("=== P2: on-chain sendbondedvote must persist a bonded-vote record ===")

        before = int(node.gettrustgraphstats().get("total_votes", 0))

        target = node.getnewaddress("", "legacy")
        res = node.sendbondedvote(target, 100, 2, "bonded vote activation")
        if "txid" not in res:
            raise AssertionError("sendbondedvote reply missing txid: %s" % res)
        node.generatetoaddress(1, self.funding_address, MINE_MAXTRIES)
        self.log.info("  broadcast + mined sendbondedvote (txid=%s, to=%s)"
                      % (res["txid"], target))

        after = int(node.gettrustgraphstats().get("total_votes", 0))
        # EXPECTED (post-fix): ProcessBondedVote persists the vote_<txid> record
        # on connect, so the bonded-vote count (total_votes) increases by one.
        assert_equal(after, before + 1)
        self.log.info("  bonded-vote record persisted on connect: total_votes %d -> %d"
                      % (before, after))

    # ------------------------------------------------------------------
    # P3 — disconnect → reconnect is idempotent (2.8)
    # ------------------------------------------------------------------
    def check_reorg_idempotent(self):
        node = self.nodes[0]
        self.log.info("=== P3: disconnect → reconnect must persist records exactly once ===")

        # Persist a fresh on-chain trust edge and a reputation vote in one block.
        target = node.getnewaddress("", "legacy")
        rep_target = node.getnewaddress("", "legacy")

        edge_before = self.canonical_listing()["count"]
        node.sendtrustrelation(target, 80, 2, "reorg edge")
        base_rep = node.getreputation(rep_target)
        node.sendcvmvote(rep_target, 50, "reorg vote")
        node.generatetoaddress(1, self.funding_address, MINE_MAXTRIES)

        block_height = node.getblockcount()
        block_hash = node.getblockhash(block_height)

        # State after the initial connect.
        edge_after = self.canonical_listing()["count"]
        rep_after = node.getreputation(rep_target)
        assert_equal(edge_after, edge_before + 1)
        assert_equal(int(rep_after["score"]), int(base_rep["score"]) + 50)
        assert_equal(int(rep_after["votecount"]), int(base_rep["votecount"]) + 1)

        # Disconnect the block, then reconnect it (reorg round-trip).
        node.invalidateblock(block_hash)
        node.reconsiderblock(block_hash)
        # Re-mine to the same tip height if reconsider left the txs in mempool.
        if node.getblockcount() < block_height:
            node.generatetoaddress(block_height - node.getblockcount(),
                                   self.funding_address, MINE_MAXTRIES)

        # EXPECTED (post-fix): idempotent persistence — exactly one canonical
        # edge and no double-counted reputation after the disconnect/reconnect.
        edge_final = self.canonical_listing()["count"]
        rep_final = node.getreputation(rep_target)
        assert_equal(edge_final, edge_after)
        assert_equal(int(rep_final["score"]), int(rep_after["score"]))
        assert_equal(int(rep_final["votecount"]), int(rep_after["votecount"]))
        self.log.info("  reorg idempotent: edges=%d score=%d votecount=%d (unchanged)"
                      % (edge_final, int(rep_final["score"]), int(rep_final["votecount"])))


if __name__ == '__main__':
    TrustActivationTest().main()
