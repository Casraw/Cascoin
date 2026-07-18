#!/usr/bin/env python3
# Copyright (c) 2025 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end functional regtest for the single-wallet Web-of-Trust (WoT) flow.

This test validates the Web-of-Trust fixes (spec: web-of-trust-fixes) end-to-end
against a single wallet on regtest. It exercises the trust flow through the WoT
RPCs and confirms that the persisted trust graph can be stored, listed, and
traversed correctly for every supported address representation:

    - legacy P2PKH addresses
    - bech32 P2WSH (witness v0 script hash) addresses
    - quantum / FALCON-512 (``rcasq...``) addresses

For each representation it builds a connected trust chain A -> B -> C -> D using
a single wallet, and then verifies:

    * listtrustrelations returns parseable JSON with the exact expected edge
      fields (weight, bond_amount, reason, from, to, slashed).      (2.1-2.4)
    * gettrustgraphstats.total_trust_edges is consistent with the
      listtrustrelations count for the same graph state.            (2.7)
    * getweightedreputation("D","A",3) finds at least one path (paths_found >= 1)
      across the 3-hop chain, returns a non-zero reputation, and echoes the
      viewer as a base58/bech32 address (not a hash160 hex string). (2.5, 2.8)
    * bech32 P2WSH and quantum addresses participate in the graph and are
      queryable by the same address that created the edge.          (2.9, 2.10, 2.12)
    * the single-wallet graph stores, lists, and traverses correctly. (2.11)

It also exercises the on-chain ``sendtrustrelation`` path (broadcast + mine +
cluster propagation) and confirms that the foreign propagation records it writes
(``trust_prop_*`` / ``trust_prop_idx_*``) are NOT misread as canonical trust
edges: ``listtrustrelations`` stays parseable and ``gettrustgraphstats`` stays
consistent with the ``listtrustrelations`` count. This is the end-to-end form of
the primary Bug 1 / Bug 2 fix.                                       (2.1, 2.3, 2.7)

Implementation notes
--------------------
* The canonical trust graph is populated through ``addtrust``, which takes an
  explicit ``from`` argument and writes the edge directly. This lets a single
  wallet build a genuinely connected A -> B -> C -> D chain with controlled
  identities, so the 3-hop ``getweightedreputation`` traversal is meaningful.
* ``sendtrustrelation`` broadcasts an on-chain trust transaction and propagates
  trust off-chain to the target's wallet cluster. It is exercised here to prove
  those propagation records do not corrupt the canonical enumeration/counting.

_Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.7, 2.8, 2.9, 2.10, 2.11, 2.12_
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


class WoTFixesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # Regtest activates CVM at height 0 and quantum at height 1, so the full
        # WoT/quantum feature set is available immediately. Mine with sha256d so
        # the CPU-based generate RPC can find regtest blocks quickly.
        self.extra_args = [['-powalgo=sha256d']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        self.log.info("Maturing coinbase outputs for the single test wallet")
        # Quantum activates at regtest height 1, after which getnewaddress()
        # (and the default mining address) return quantum addresses. Mine the
        # coinbase to an explicit legacy address so sendtrustrelation can fund
        # bonds/fees from spendable legacy (ECDSA) UTXOs.
        self.funding_address = node.getnewaddress("", "legacy")
        self.mine_until_funded(node, min_balance=10)

        # Build and verify a connected single-wallet trust graph for every
        # supported address representation.
        self.run_flow("legacy")
        self.run_flow("p2wsh")
        self.run_flow("quantum")

        # Exercise the on-chain broadcast + propagation path and confirm it does
        # not corrupt the canonical listing/counting.
        self.check_onchain_propagation_does_not_corrupt()

        self.log.info("All Web-of-Trust single-wallet end-to-end flows passed")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def mine_until_funded(self, node, min_balance):
        """Mine (in batches) to the legacy funding address until the wallet has
        at least ``min_balance`` mature, spendable coins.

        Regtest sha256d mining has per-block variance and generatetoaddress may
        return fewer blocks than requested, so mine in a loop rather than a
        single call. Coinbase maturity is 100 blocks, so this also mines well
        past maturity before any spendable balance appears.
        """
        attempts = 0
        while node.getbalance() < min_balance:
            node.generatetoaddress(25, self.funding_address, MINE_MAXTRIES)
            attempts += 1
            assert attempts < 60, (
                "could not mine a spendable balance on regtest (height=%d, balance=%s)"
                % (node.getblockcount(), node.getbalance()))
        self.log.info("  funded wallet: balance=%s at height=%d"
                      % (node.getbalance(), node.getblockcount()))

    def make_address(self, kind):
        """Return a fresh address of the requested representation.

        - legacy:  P2PKH base58 (CKeyID)
        - p2wsh:   bech32 witness-v0 script hash (2-of-2 multisig)
        - quantum: FALCON-512 quantum address (rcasq...)
        """
        node = self.nodes[0]
        if kind == "legacy":
            return node.getnewaddress("", "legacy")
        if kind == "p2wsh":
            k1 = node.getnewaddress("", "legacy")
            k2 = node.getnewaddress("", "legacy")
            return node.addmultisigaddress(2, [k1, k2], "", "bech32")["address"]
        if kind == "quantum":
            return node.getnewaddress("", "quantum")
        raise AssertionError("unknown address kind: %s" % kind)

    def canonical_listing(self):
        """Return listtrustrelations and assert count matches returned edges."""
        result = self.nodes[0].listtrustrelations()
        assert "edges" in result
        assert "count" in result
        # A parseable reply whose count matches the returned edge list (2.3).
        assert_equal(result["count"], len(result["edges"]))
        return result

    def assert_count_consistent(self):
        """gettrustgraphstats.total_trust_edges == listtrustrelations count (2.7)."""
        listing = self.canonical_listing()
        stats = self.nodes[0].gettrustgraphstats()
        assert "total_trust_edges" in stats
        assert_equal(stats["total_trust_edges"], listing["count"])
        return listing

    # ------------------------------------------------------------------
    # Flow
    # ------------------------------------------------------------------
    def run_flow(self, kind):
        node = self.nodes[0]
        self.log.info("=== Web-of-Trust end-to-end flow for %s addresses ===" % kind)

        baseline = self.canonical_listing()["count"]

        # A connected chain A -> B -> C -> D within a single wallet.
        addrs = {name: self.make_address(kind) for name in ("A", "B", "C", "D")}

        # (from, to, weight, bond, reason) for each hop. Weights are >= the
        # traversal threshold so the chain is fully traversable.
        chain = [
            ("A", "B", 80, 2, "A trusts B (%s)" % kind),
            ("B", "C", 75, 2, "B trusts C (%s)" % kind),
            ("C", "D", 90, 3, "C trusts D (%s)" % kind),
        ]

        for src, dst, weight, bond, reason in chain:
            res = node.addtrust(addrs[dst], weight, bond, reason, addrs[src])
            assert_equal(res["from"], addrs[src])
            assert_equal(res["to"], addrs[dst])
            assert_equal(res["weight"], weight)
            self.log.info("  added edge %s -> %s (weight=%d, bond=%d)"
                          % (addrs[src], addrs[dst], weight, bond))

        # --- 2.1-2.4: listtrustrelations returns exact, parseable fields ------
        listing = self.canonical_listing()
        assert_equal(listing["count"], baseline + len(chain))

        by_pair = {}
        for edge in listing["edges"]:
            # Every edge exposes the full expected field set (parseable JSON).
            for field in ("from", "to", "weight", "bond_amount", "timestamp", "reason", "slashed"):
                assert field in edge, "missing field %s in edge %s" % (field, edge)
            by_pair[(edge["from"], edge["to"])] = edge

        for src, dst, weight, bond, reason in chain:
            key = (addrs[src], addrs[dst])
            assert key in by_pair, "edge %s -> %s missing from listtrustrelations" % key
            edge = by_pair[key]
            assert_equal(edge["from"], addrs[src])         # (2.4) correct from
            assert_equal(edge["to"], addrs[dst])           # (2.10) queryable by same address
            assert_equal(edge["weight"], weight)           # (2.1)
            assert_equal(edge["reason"], reason)           # (2.2) exact reason round-trip
            assert_equal(edge["slashed"], False)           # (2.1)
            assert_equal(float(edge["bond_amount"]), float(bond))  # (2.1)

        self.log.info("  listtrustrelations returned parseable JSON with exact fields")

        # --- 2.7: stats total_trust_edges consistent with listing count ------
        stats = node.gettrustgraphstats()
        assert_equal(stats["total_trust_edges"], listing["count"])
        self.log.info("  gettrustgraphstats.total_trust_edges == listtrustrelations count == %d"
                      % listing["count"])

        # --- 2.5, 2.8, 2.9, 2.11, 2.12: 3-hop traversal + reputation ---------
        rep = node.getweightedreputation(addrs["D"], addrs["A"], 3)
        # A path across the A -> B -> C -> D chain is found within depth 3. (2.5)
        assert_greater_than_or_equal(rep["paths_found"], 1)
        # Non-zero reputation derived from the trust-path weights. (2.5)
        assert_greater_than(abs(float(rep["individual_reputation"])), 0.0)
        # viewer echoed as the supplied base58/bech32/quantum address. (2.8)
        assert_equal(rep["viewer"], addrs["A"])
        assert_equal(rep["target"], addrs["D"])
        self.log.info("  getweightedreputation(D, A, 3): paths_found=%d, individual_reputation=%s, viewer=%s"
                      % (rep["paths_found"], rep["individual_reputation"], rep["viewer"]))

        # Direct single-hop lookups are also reachable (viewer == edge.from).
        for src, dst, weight, bond, reason in chain:
            r = node.getweightedreputation(addrs[dst], addrs[src], 1)
            assert_greater_than_or_equal(r["paths_found"], 1)
            assert_equal(r["viewer"], addrs[src])

        self.log.info("  %s single-wallet trust graph stored, listed, and traversed correctly" % kind)

    # ------------------------------------------------------------------
    # On-chain propagation must not corrupt canonical enumeration
    # ------------------------------------------------------------------
    def check_onchain_propagation_does_not_corrupt(self):
        node = self.nodes[0]
        self.log.info("=== On-chain sendtrustrelation propagation integrity ===")

        before = self.assert_count_consistent()

        # Broadcast an on-chain trust relationship. This writes foreign
        # propagation records (trust_prop_* / trust_prop_idx_*) that MUST NOT be
        # misread as canonical TrustEdge records by listtrustrelations /
        # gettrustgraphstats (Bug 1 / Bug 2).
        target = node.getnewaddress("", "legacy")
        res = node.sendtrustrelation(target, 80, 2, "on-chain trust")
        assert "txid" in res
        assert_greater_than_or_equal(res["edges_propagated"], 1)
        node.generatetoaddress(1, self.funding_address, MINE_MAXTRIES)
        self.log.info("  broadcast + mined sendtrustrelation (txid=%s, edges_propagated=%d)"
                      % (res["txid"], res["edges_propagated"]))

        # listtrustrelations still returns a parseable reply and every edge has
        # a valid in-range weight and a string reason (no offset-garbage from a
        # misread foreign record). (2.1, 2.3)
        after = self.canonical_listing()
        for edge in after["edges"]:
            assert isinstance(edge["reason"], str)
            assert -100 <= edge["weight"] <= 100, "weight out of range: %s" % edge["weight"]
            assert_equal(edge["slashed"], False)

        # The canonical count is unchanged by the propagation records and stays
        # consistent with gettrustgraphstats. (2.7)
        self.assert_count_consistent()
        assert_equal(after["count"], before["count"])
        self.log.info("  foreign propagation records excluded; count stable at %d and consistent"
                      % after["count"])


if __name__ == '__main__':
    WoTFixesTest().main()
