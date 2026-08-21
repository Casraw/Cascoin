#!/usr/bin/env python3
# Copyright (c) 2026 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Multi-node functional tests for the trustless Cascoin L2.

Verifies the cross-node trustless properties added in the M1-M4 hardening:
  1. Data Availability + independent reconstruction: a second node that is NOT
     a sequencer, and never produced any L2 block, arrives at exactly the same
     L2 state (balances + total supply) as the sequencer purely by following the
     L1 chain. This includes balances that exist ONLY because of L2 transfers
     (recipients that never received an on-chain burn/mint), which can only be
     reconstructed from the L2 block data posted to L1.
  2. State-root agreement: the verifier's reconstructed L2 blocks carry the same
     state roots as the sequencer's, i.e. it re-executed and accepted them.
"""

import time
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    sync_blocks,
    wait_until,
)

COIN = 10000000  # Cascoin uses 7 decimal places (1 CAS = 1e7 base units)


class L2MultiNodeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        base = [
            '-l2=1', '-l2mode=2',
            '-l2minsequencerstake=1', '-l2minsequencerhatscore=10',
            '-l2blockintervalms=200', '-l2challengeseconds=120',
            '-datacarriersize=1000', '-fallbackfee=0.0001',
            '-l2datainterval=2', '-l2commitinterval=2',
        ]
        self.extra_args = [
            base + ['-l2sequencer=1'],   # node0: the sequencer
            base + ['-l2sequencer=0'],   # node1: a plain verifier (never produces)
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    # --- helpers -----------------------------------------------------------

    def gen(self, n):
        """Reliably mine n blocks on the sequencer and sync them to the verifier."""
        seq = self.nodes[0]
        target = seq.getblockcount() + n
        attempts = 0
        cap = n * 60 + 300
        while seq.getblockcount() < target and attempts < cap:
            try:
                seq.generatetoaddress(1, self.miner)
            except Exception:
                pass
            attempts += 1
        assert seq.getblockcount() >= target, "failed to mine %d blocks" % n
        sync_blocks(self.nodes)

    def seqbal(self, addr):
        return self.nodes[0].l2_getbalance(addr)['balance']

    def verbal(self, addr):
        return self.nodes[1].l2_getbalance(addr)['balance']

    def make_burn(self, amount):
        """Create+sign+broadcast a burn of `amount` CAS on the sequencer."""
        seq = self.nodes[0]
        leg = seq.getnewaddress('', 'legacy')
        pub = seq.validateaddress(leg)['pubkey']
        chg = seq.getnewaddress('', 'legacy')
        j = seq.l2_createburntx(amount, pub, chg)
        signed = seq.signrawtransaction(j['hex'])
        assert_equal(signed['complete'], True)
        seq.l2_sendburntx(signed['hex'])
        return j['l2RecipientAddress']

    # --- test --------------------------------------------------------------

    def run_test(self):
        seq, ver = self.nodes
        self.miner = seq.getnewaddress('', 'legacy')
        self.log.info("Maturing coinbase on the sequencer ...")
        self.gen(120)

        # 1) Burn + mint on the sequencer.
        self.log.info("Sequencer: burn -> mint, then transfer ...")
        mint_addr = self.make_burn(20)
        self.gen(8)  # mature -> mint
        wait_until(lambda: self.seqbal(mint_addr) == 20 * COIN, timeout=30)

        # 2) A transfer to a recipient that NEVER received an on-chain burn.
        #    Its balance exists only in L2 and can only reach the verifier via
        #    the block data posted to L1 (Data Availability).
        da_addr = '0x8888888888888888888888888888888888888888'
        seq.l2_transfer(mint_addr, da_addr, 6)
        wait_until(lambda: self.seqbal(da_addr) == 6 * COIN, timeout=30)

        # 3) Let the sequencer post L2 block data to L1, mining + syncing so the
        #    verifier receives those L1 blocks and reconstructs the L2 chain.
        self.log.info("Posting L2 data to L1 and syncing to the verifier ...")
        def data_complete():
            self.gen(1)
            time.sleep(0.5)
            return seq.l2_getdatastatus()['dataComplete']
        wait_until(data_complete, timeout=120)
        self.gen(3)

        # 4) The verifier - which never produced a block - must independently
        #    arrive at the SAME L2 state as the sequencer.
        self.log.info("Verifying the non-sequencer node reconstructed the L2 state ...")
        wait_until(lambda: self.verbal(da_addr) == 6 * COIN, timeout=120)
        assert_equal(self.verbal(mint_addr), self.seqbal(mint_addr))
        assert_equal(self.verbal(da_addr), self.seqbal(da_addr))
        assert_equal(
            ver.l2_gettotalsupply()['totalSupply'],
            seq.l2_gettotalsupply()['totalSupply'])

        # 5) State-root agreement: the verifier accepted the same L2 blocks (it
        #    re-executed each one and its state root matches the sequencer's).
        seq_tip = seq.l2_getdatastatus()['l2Tip']
        ver_tip = ver.l2_getdatastatus()['l2Tip']
        assert ver_tip >= 1, "verifier did not reconstruct any L2 block"
        common = min(seq_tip, ver_tip)
        for n in range(0, common + 1):
            sb = seq.l2_getblockbynumber(n, False)
            vb = ver.l2_getblockbynumber(n, False)
            assert_equal(vb['stateRoot'], sb['stateRoot'])
            assert_equal(vb['hash'], sb['hash'])
        self.log.info("   verifier independently reconstructed L2 state up to block %d "
                      "with matching state roots" % common)

        self.log.info("All L2 multi-node tests passed!")


if __name__ == '__main__':
    L2MultiNodeTest().main()
