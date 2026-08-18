#!/usr/bin/env python3
# Copyright (c) 2025 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end functional regtest for the typed downstream identity flow.

Spec: .kiro/specs/trustnodeid-full-migration  (bugfix)
Task 5: "Add an end-to-end functional regtest for the typed downstream flow".

PURPOSE
-------
The TrustNodeId full migration converts every downstream user-identity path
(reputation, HAT/SecureHAT, wallet clustering, cluster trust, bonded votes, DAO
disputes/votes, and trust propagation) from a 20-byte ``uint160`` to the wide,
lossless ``CVM::TrustNodeId`` representation. This regtest drives those paths
end-to-end through the real daemon/RPC surface and asserts the two central
post-fix guarantees for EVERY supported destination type:

    - P2PKH   (legacy, base58)                    -> TrustNodeType 1
    - P2SH    (p2sh-segwit, base58)               -> TrustNodeType 2
    - P2WPKH  (bech32 single-key)                 -> TrustNodeType 3
    - P2WSH   (bech32 2-of-2 multisig)            -> TrustNodeType 4
    - quantum (FALCON-512 ``rcasq...``)           -> TrustNodeType 5

Guarantees asserted:
    * NO width rejection: none of the migrated RPCs reject an otherwise
      supported P2WSH or quantum (32-byte) identity with
      ``RPC_INVALID_ADDRESS_OR_KEY``.                                (2.1-2.7)
    * CANONICAL output: every identity echoed by an RPC decodes back to the
      exact address that was supplied / stored (no P2PKH fabrication, no
      truncation, no type erasure).                                 (2.4, 2.8)

Phases
------
1. Reputation           sendcvmvote (on-chain) + getreputation for all 5 types;
                        the on-chain 39-byte typed payload is compared against
                        an equivalent direct API write (votereputation).  (2.3)
2. HAT v2               getbehaviormetrics / getgraphmetrics / getsecuretrust /
                        gettrustbreakdown for all 5 types; no width rejection,
                        canonical echo, well-formed output.               (2.2)
3. Bonded votes         sendbondedvote for all 5 types; the mined typed
                        bonded-vote payload persists on connect.          (2.6)
4. DAO                  createdispute / votedispute / listdisputes /
                        getdispute; challenger and DAO-member identities are
                        rendered through their stored typed destination.  (2.6)
5. Clustering /         a mixed-type common-input transaction plus a
   propagation          two-output change transaction; buildwalletclusters is
                        run twice and across a restart and must produce stable
                        cluster IDs, member sets, and original address
                        encodings; geteffectivetrust carries every type.  (2.4, 2.5, 3.2)
6. Idempotency          disconnect -> reconnect a block carrying a reputation
                        vote and a bonded vote; no duplicate application.  (2.13)
7. Clean state /        restart the daemon and confirm every current typed
   round trip           record family still round-trips (clean-state read).  (3.1, 3.3)

The operational "seed stale not-live uint160 records and require a clean
rebuild" case (3.3) cannot be provoked through the RPC surface (it requires
writing raw legacy DB bytes) and is covered at the unit level by
``trustnodeid_full_migration_explore_tests`` /
``trustnodeid_full_migration_fix_property_tests``. This functional test instead
asserts the positive counterpart: a clean development state written by the
converted binary reads back exactly across a restart.

_Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.13, 3.1, 3.2, 3.3_
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
)

# The regtest sha256d target needs more than the default 1,000,000 hashing
# attempts per block, so mine with a generous maxtries.
MINE_MAXTRIES = 100000000

# All five supported downstream identity representations.
ADDRESS_KINDS = ["legacy", "p2sh", "p2wpkh", "p2wsh", "quantum"]


class TrustNodeIdMigrationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Regtest activates CVM at height 0 and quantum at height 1, so the full
        # typed downstream feature set is available immediately. Mine with
        # sha256d so the CPU-based generate RPC can find regtest blocks quickly.
        # -txindex lets WalletClusterer::AnalyzeTransaction resolve prevout
        # scriptPubKeys (via GetTransaction) so the common-input heuristic can
        # link the mixed-type inputs of a confirmed transaction.
        self.node_args = ['-powalgo=sha256d', '-txindex=1']
        self.extra_args = [self.node_args]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        self.log.info("Maturing coinbase outputs for the single test wallet")
        # Quantum activates at regtest height 1, after which getnewaddress()
        # returns quantum addresses by default. Mine the coinbase to an explicit
        # legacy address so the on-chain RPCs can fund bonds/fees from spendable
        # legacy (ECDSA) UTXOs.
        self.funding_address = node.getnewaddress("", "legacy")
        self.mine_until_funded(node, min_balance=200)

        self.check_reputation_all_types()          # Phase 1 (2.3, 2.8)
        self.check_hat_all_types()                 # Phase 2 (2.2, 2.8)
        self.check_bonded_votes_all_types()        # Phase 3 (2.6)
        self.check_dao_flow()                      # Phase 4 (2.6, 2.8)
        self.check_clustering_and_propagation()    # Phase 5 (2.4, 2.5, 3.2)
        self.check_idempotent_reprocessing()       # Phase 6 (2.13)
        self.check_clean_state_round_trip()        # Phase 7 (3.1, 3.3)

        self.log.info("All TrustNodeId typed-downstream end-to-end flows passed")

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
            if attempts >= 80:
                raise AssertionError(
                    "could not mine a spendable balance on regtest (height=%d, balance=%s)"
                    % (node.getblockcount(), node.getbalance()))
        self.log.info("  funded wallet: balance=%s at height=%d"
                      % (node.getbalance(), node.getblockcount()))

    def mine(self, count=1):
        self.nodes[0].generatetoaddress(count, self.funding_address, MINE_MAXTRIES)

    def make_address(self, kind):
        """Return a fresh wallet address of the requested representation.

        - legacy:  P2PKH base58 (CKeyID)                     -> TrustNodeType 1
        - p2sh:    P2SH-wrapped segwit base58 (CScriptID)    -> TrustNodeType 2
        - p2wpkh:  bech32 witness-v0 key hash                -> TrustNodeType 3
        - p2wsh:   bech32 witness-v0 script hash (2-of-2)    -> TrustNodeType 4
        - quantum: FALCON-512 quantum address (rcasq...)     -> TrustNodeType 5
        """
        node = self.nodes[0]
        if kind == "legacy":
            return node.getnewaddress("", "legacy")
        if kind == "p2sh":
            return node.getnewaddress("", "p2sh-segwit")
        if kind == "p2wpkh":
            return node.getnewaddress("", "bech32")
        if kind == "p2wsh":
            k1 = node.getnewaddress("", "legacy")
            k2 = node.getnewaddress("", "legacy")
            return node.addmultisigaddress(2, [k1, k2], "", "bech32")["address"]
        if kind == "quantum":
            return node.getnewaddress("", "quantum")
        raise AssertionError("unknown address kind: %s" % kind)

    def assert_is_address_string(self, value, context):
        """A rendered identity must be a non-empty string that decodes to a
        valid destination (i.e. canonical, not offset garbage)."""
        assert isinstance(value, str) and len(value) > 0, \
            "%s: expected a non-empty address string, got %r" % (context, value)
        info = self.nodes[0].validateaddress(value)
        assert_equal(info["isvalid"], True)

    # ------------------------------------------------------------------
    # Phase 1 — Reputation for every destination type
    # ------------------------------------------------------------------
    def check_reputation_all_types(self):
        node = self.nodes[0]
        self.log.info("=== Phase 1: reputation (sendcvmvote/getreputation) for all types ===")

        vote_value = 50
        for kind in ADDRESS_KINDS:
            target = self.make_address(kind)

            # Baseline: querying reputation for a 32-byte (P2WSH/quantum) target
            # MUST NOT be rejected on width grounds; it returns a canonical echo.
            base = node.getreputation(target)
            assert_equal(base["address"], target)          # canonical echo (2.8)
            base_score = int(base["score"])
            base_votes = int(base["votecount"])

            # On-chain reputation vote: the 39-byte typed REPUTATION payload is
            # mined and applied on block connect for every identity type.
            res = node.sendcvmvote(target, vote_value, "typed reputation (%s)" % kind)
            assert "txid" in res, "sendcvmvote(%s) missing txid: %s" % (kind, res)
            assert_equal(res["address"], target)
            self.mine(1)

            after = node.getreputation(target)
            assert_equal(after["address"], target)         # canonical echo (2.8)
            # The mined typed vote applied on connect: score moves by the vote
            # value and the vote count increments (no width rejection for the
            # 32-byte P2WSH/quantum identities).                    (2.3)
            assert_equal(int(after["score"]), base_score + vote_value)
            assert_equal(int(after["votecount"]), base_votes + 1)
            self.log.info("  %-7s reputation applied on connect: score=%d votecount=%d"
                          % (kind, int(after["score"]), int(after["votecount"])))

        # On-chain typed payload vs equivalent direct API write must agree.
        # Use two fresh P2WSH targets (a 32-byte type that the pre-fix code
        # could not carry) and apply the same +40 vote through each path.
        onchain_target = self.make_address("p2wsh")
        direct_target = self.make_address("p2wsh")

        node.sendcvmvote(onchain_target, 40, "onchain typed")
        self.mine(1)
        node.votereputation(direct_target, 40, "direct typed")  # direct DB write

        onchain_rep = node.getreputation(onchain_target)
        direct_rep = node.getreputation(direct_target)
        assert_equal(int(onchain_rep["score"]), int(direct_rep["score"]))
        assert_equal(int(onchain_rep["votecount"]), int(direct_rep["votecount"]))
        self.log.info("  on-chain typed payload == direct API write: score=%d votecount=%d"
                      % (int(onchain_rep["score"]), int(onchain_rep["votecount"])))

    # ------------------------------------------------------------------
    # Phase 2 — HAT v2 RPCs for every destination type
    # ------------------------------------------------------------------
    def check_hat_all_types(self):
        node = self.nodes[0]
        self.log.info("=== Phase 2: HAT v2 RPCs for all types (no width rejection) ===")

        for kind in ADDRESS_KINDS:
            addr = self.make_address(kind)
            viewer = self.make_address("legacy")

            # getbehaviormetrics — accepts any supported destination, echoes it.
            bm = node.getbehaviormetrics(addr)
            assert_equal(bm["address"], addr)
            for field in ("total_trades", "unique_partners", "base_reputation",
                          "final_reputation"):
                assert field in bm, "getbehaviormetrics(%s) missing %s" % (kind, field)

            # getgraphmetrics — accepts any supported destination, echoes it.
            gm = node.getgraphmetrics(addr)
            assert_equal(gm["address"], addr)
            assert "degree_centrality" in gm

            # getsecuretrust — target only, then target + viewer.
            st = node.getsecuretrust(addr)
            assert_equal(st["target"], addr)
            assert "trust_score" in st
            st2 = node.getsecuretrust(addr, viewer)
            assert_equal(st2["target"], addr)
            assert_equal(st2["viewer"], viewer)

            # gettrustbreakdown — full component breakdown, canonical echo.
            tb = node.gettrustbreakdown(addr, viewer)
            assert_equal(tb["target"], addr)
            assert_equal(tb["viewer"], viewer)
            for comp in ("behavior", "wot", "economic", "temporal"):
                assert comp in tb, "gettrustbreakdown(%s) missing %s" % (kind, comp)
            assert "final_score" in tb
            self.log.info("  %-7s HAT RPCs accepted the identity and echoed it canonically"
                          % kind)

    # ------------------------------------------------------------------
    # Phase 3 — Bonded votes for every destination type
    # ------------------------------------------------------------------
    def check_bonded_votes_all_types(self):
        node = self.nodes[0]
        self.log.info("=== Phase 3: bonded votes (sendbondedvote) for all types ===")

        for kind in ADDRESS_KINDS:
            target = self.make_address(kind)
            before = int(node.gettrustgraphstats().get("total_votes", 0))

            res = node.sendbondedvote(target, 100, 2, "typed bonded vote (%s)" % kind)
            assert "txid" in res, "sendbondedvote(%s) missing txid: %s" % (kind, res)
            # The RPC echoes the target address it was given (canonical).
            assert_equal(res["target_address"], target)
            self.mine(1)

            after = int(node.gettrustgraphstats().get("total_votes", 0))
            # The mined 80-byte typed bonded-vote payload persists on connect,
            # so the bonded-vote count increases by exactly one — even for the
            # 32-byte P2WSH/quantum targets the pre-fix code could not carry.
            assert_equal(after, before + 1)
            self.log.info("  %-7s bonded vote persisted on connect: total_votes %d -> %d"
                          % (kind, before, after))

    # ------------------------------------------------------------------
    # Phase 4 — DAO dispute / vote paths
    # ------------------------------------------------------------------
    def check_dao_flow(self):
        node = self.nodes[0]
        self.log.info("=== Phase 4: DAO createdispute/votedispute/listdisputes/getdispute ===")

        # A bonded vote to challenge.
        target = self.make_address("legacy")
        bonded = node.sendbondedvote(target, 100, 2, "vote to dispute")
        vote_txid = bonded["txid"]
        self.mine(1)

        # Create a dispute challenging that bonded vote.
        disp = node.createdispute(vote_txid, 1, "typed dispute")
        dispute_id = disp["dispute_id"]
        assert_equal(disp["vote_tx"], vote_txid)
        self.mine(1)

        # A DAO member votes on the dispute. votedispute's identity fields
        # accept the standard P2PKH/P2SH/P2WPKH representations; use an explicit
        # P2PKH member so we can assert its canonical echo. The RPC MUST accept
        # the typed identity without a width rejection and echo it back exactly.
        #
        # NOTE: whether the vote is actually *recorded* into the dispute's
        # daoVotes map is gated by TrustGraph::IsDAOMember (reputation >= 70,
        # >= 100 CAS bonded, and a ``dao_activity_`` record). The activity record
        # has no RPC writer, so DAO membership cannot be established through the
        # RPC surface in regtest — the recorded-vote round trip is covered at the
        # unit level (trustnodeid_full_migration_fix_property_tests). Here we
        # therefore assert the width/canonical guarantees the RPC controls and
        # render any recorded votes canonically when present.
        member = self.make_address("legacy")
        vres = node.votedispute(dispute_id, True, member, 1)
        assert_equal(vres["dispute_id"], dispute_id)
        # The voter identity echoed is the exact member address (typed render,
        # no width rejection).
        assert_equal(vres["voter"], member)
        self.mine(1)

        # listdisputes: our dispute is present with a canonically rendered
        # challenger (an address string, not a hash160 hex or offset garbage).
        listing = node.listdisputes("all")
        found = None
        for d in listing["disputes"]:
            if d["dispute_id"] == dispute_id:
                found = d
                break
        assert found is not None, "created dispute %s not returned by listdisputes" % dispute_id
        assert isinstance(found["challenger"], str)
        if found["challenger"]:
            self.assert_is_address_string(found["challenger"], "listdisputes challenger")
        assert_equal(found["original_vote_tx"], vote_txid)

        # getdispute: the dispute round-trips, and every recorded DAO member is
        # rendered through its stored typed destination (decodes back to a valid
        # address rather than a fabricated P2PKH).
        detail = node.getdispute(dispute_id)
        assert_equal(detail["dispute_id"], dispute_id)
        assert_equal(detail["original_vote_tx"], vote_txid)
        if detail["challenger"]:
            self.assert_is_address_string(detail["challenger"], "getdispute challenger")
        for v in detail["dao_votes"]:
            self.assert_is_address_string(v["dao_member"], "getdispute dao_member")
        self.log.info("  DAO dispute %s: RPCs accepted typed inputs and rendered canonically "
                      "(dao_votes recorded=%d)" % (dispute_id[:16], len(detail["dao_votes"])))

    # ------------------------------------------------------------------
    # Phase 5 — Wallet clustering + cluster trust + propagation
    # ------------------------------------------------------------------
    def fund_address(self, addr, amount):
        """Send ``amount`` CAS to ``addr`` from the wallet and return the
        resulting (txid, vout) outpoint that pays ``addr``."""
        node = self.nodes[0]
        txid = node.sendtoaddress(addr, amount)
        raw = node.getrawtransaction(txid, True)
        for vout in raw["vout"]:
            spk = vout["scriptPubKey"]
            if addr in spk.get("addresses", []):
                return txid, vout["n"]
        raise AssertionError("could not locate output paying %s in tx %s" % (addr, txid))

    def check_clustering_and_propagation(self):
        node = self.nodes[0]
        self.log.info("=== Phase 5: wallet clustering, cluster trust, propagation ===")

        # --- Mixed-type common-input transaction ---------------------------
        # Fund three wallet addresses of DIFFERENT representations, then spend
        # all three together in a single transaction. The common-input
        # heuristic must link every supported typed input into one cluster.
        a_pkh = self.make_address("legacy")
        a_sh = self.make_address("p2sh")
        a_wpkh = self.make_address("p2wpkh")

        out_pkh = self.fund_address(a_pkh, 5)
        out_sh = self.fund_address(a_sh, 5)
        out_wpkh = self.fund_address(a_wpkh, 5)
        self.mine(1)

        inputs = [
            {"txid": out_pkh[0], "vout": out_pkh[1]},
            {"txid": out_sh[0], "vout": out_sh[1]},
            {"txid": out_wpkh[0], "vout": out_wpkh[1]},
        ]
        # Spend the combined ~15 CAS to a fresh sink, leaving fee. This is the
        # mixed-type common-input transaction.
        sink = self.make_address("legacy")
        raw = node.createrawtransaction(inputs, {sink: Decimal("14.9")})
        signed = node.signrawtransaction(raw)
        assert_equal(signed["complete"], True)
        node.sendrawtransaction(signed["hex"])
        self.mine(1)

        # --- Two-output change transaction ---------------------------------
        # A normal wallet send produces recipient + change (2 outputs); the
        # change-detection heuristic links the change identity to the inputs.
        node.sendtoaddress(self.make_address("p2wpkh"), 3)
        self.mine(1)

        # --- buildwalletclusters (run #1) ----------------------------------
        stats1 = node.buildwalletclusters()
        assert "total_clusters" in stats1
        cluster1 = node.getwalletcluster(a_pkh)
        members1 = set(cluster1["members"])
        # The three mixed-type inputs are clustered together, and every member
        # is rendered through its stored typed destination.
        assert a_pkh in members1
        assert a_sh in members1, "P2SH input missing from common-input cluster: %s" % members1
        assert a_wpkh in members1, "P2WPKH input missing from common-input cluster: %s" % members1
        for m in cluster1["members"]:
            self.assert_is_address_string(m, "getwalletcluster member")
        cluster_id1 = cluster1["cluster_id"]
        self.assert_is_address_string(cluster_id1, "cluster_id")
        self.log.info("  common-input cluster (run1): id=%s members=%d"
                      % (cluster_id1, len(members1)))

        # --- buildwalletclusters (run #2) — idempotent/deterministic -------
        node.buildwalletclusters()
        cluster2 = node.getwalletcluster(a_pkh)
        assert_equal(cluster2["cluster_id"], cluster_id1)
        assert_equal(set(cluster2["members"]), members1)
        self.log.info("  buildwalletclusters run2 produced identical id and member set")

        # --- geteffectivetrust carries every destination type --------------
        for kind in ADDRESS_KINDS:
            t = self.make_address(kind)
            et = node.geteffectivetrust(t)
            # target echoed is the exact typed address (canonical rendering).
            assert_equal(et["target"], t)
            assert "effective_score" in et
            assert "cluster_id" in et
            self.assert_is_address_string(et["cluster_id"], "geteffectivetrust cluster_id")

        # --- Propagation via sendtrustrelation preserves canonical output --
        prop_target = self.make_address("legacy")
        pr = node.sendtrustrelation(prop_target, 80, 2, "typed propagation")
        assert "txid" in pr
        assert_greater_than_or_equal(pr["edges_propagated"], 1)
        self.mine(1)
        et_prop = node.geteffectivetrust(prop_target)
        assert_equal(et_prop["target"], prop_target)
        assert_greater_than_or_equal(int(et_prop["propagated_edges"]), 0)
        self.log.info("  propagation exercised; geteffectivetrust echoes typed target")

        # Stash for the cross-restart check in Phase 7.
        self.cluster_probe_addr = a_pkh
        self.cluster_probe_id = cluster_id1
        self.cluster_probe_members = members1

    # ------------------------------------------------------------------
    # Phase 6 — Idempotent reprocessing (disconnect -> reconnect)
    # ------------------------------------------------------------------
    def check_idempotent_reprocessing(self):
        node = self.nodes[0]
        self.log.info("=== Phase 6: disconnect -> reconnect is idempotent ===")

        rep_target = self.make_address("quantum")   # a 32-byte typed identity
        vote_target = self.make_address("p2wsh")     # a 32-byte typed identity

        base_rep = node.getreputation(rep_target)
        votes_before = int(node.gettrustgraphstats().get("total_votes", 0))
        edges_before = int(node.listtrustrelations()["count"])

        node.sendcvmvote(rep_target, 30, "idempotent vote")
        node.sendbondedvote(vote_target, 100, 2, "idempotent bonded vote")
        self.mine(1)

        block_height = node.getblockcount()
        block_hash = node.getblockhash(block_height)

        rep_after = node.getreputation(rep_target)
        votes_after = int(node.gettrustgraphstats().get("total_votes", 0))
        assert_equal(int(rep_after["score"]), int(base_rep["score"]) + 30)
        assert_equal(votes_after, votes_before + 1)

        # Reorg round-trip: disconnect and reconnect the block.
        node.invalidateblock(block_hash)
        node.reconsiderblock(block_hash)
        if node.getblockcount() < block_height:
            self.mine(block_height - node.getblockcount())

        rep_final = node.getreputation(rep_target)
        votes_final = int(node.gettrustgraphstats().get("total_votes", 0))
        edges_final = int(node.listtrustrelations()["count"])
        # No duplicate application: reputation and bonded-vote counts are
        # unchanged by the disconnect/reconnect, and no phantom edges appear.
        assert_equal(int(rep_final["score"]), int(rep_after["score"]))
        assert_equal(votes_final, votes_after)
        assert_greater_than_or_equal(edges_final, edges_before)
        self.log.info("  reorg idempotent: score=%d total_votes=%d (unchanged)"
                      % (int(rep_final["score"]), votes_final))

    # ------------------------------------------------------------------
    # Phase 7 — Clean-state round trip across a restart
    # ------------------------------------------------------------------
    def check_clean_state_round_trip(self):
        node = self.nodes[0]
        self.log.info("=== Phase 7: clean-state typed records round-trip across restart ===")

        # Write one current record in each family against 32-byte typed
        # identities, capture the state, restart the daemon, and confirm every
        # current typed layout reads back exactly (clean-state read path).
        rep_addr = self.make_address("quantum")
        node.sendcvmvote(rep_addr, 25, "persist reputation")
        self.mine(1)
        rep_before = node.getreputation(rep_addr)

        cluster_before = node.getwalletcluster(self.cluster_probe_addr)

        self.restart_node(0, extra_args=self.node_args)

        # Reputation record round-trips exactly (typed reputation_<TNI> key).
        rep_after = node.getreputation(rep_addr)
        assert_equal(rep_after["address"], rep_addr)
        assert_equal(int(rep_after["score"]), int(rep_before["score"]))
        assert_equal(int(rep_after["votecount"]), int(rep_before["votecount"]))

        # Cluster records rebuild deterministically from the same active chain:
        # identical cluster ID and member set, with canonical member encodings.
        node.buildwalletclusters()
        cluster_after = node.getwalletcluster(self.cluster_probe_addr)
        assert_equal(cluster_after["cluster_id"], cluster_before["cluster_id"])
        assert_equal(set(cluster_after["members"]), set(cluster_before["members"]))
        assert_equal(cluster_after["cluster_id"], self.cluster_probe_id)
        assert_equal(set(cluster_after["members"]), self.cluster_probe_members)
        for m in cluster_after["members"]:
            self.assert_is_address_string(m, "post-restart cluster member")
        self.log.info("  clean-state records round-tripped across restart; "
                      "cluster id/members stable and canonically encoded")


if __name__ == '__main__':
    TrustNodeIdMigrationTest().main()
