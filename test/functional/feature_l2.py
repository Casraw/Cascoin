#!/usr/bin/env python3
# Copyright (c) 2026 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional tests for the Cascoin L2 (burn-and-mint, on-chain transfers).

Encodes, as a repeatable regression suite, the properties verified manually:
  1. Burn-and-mint: burning CAS on L1 mints L2 tokens 1:1 after confirmations.
  2. On-chain transfers: l2_transfer is included in a sequencer-produced L2
     block and moves balances; supply invariant holds.
  3. Signature authentication: a tampered/forged L2 transaction is rejected.
  4. Persistence: minted balances and transfers survive a node restart.
  5. L1-reorg rollback: invalidating an L1 block undoes the mint; reconsidering
     it re-applies the mint.
  6. L1 anchoring: an L2COMMIT commitment is recorded and matches the block's
     state root.
  7. Fraud proof: a bogus committed state root is detected and slashed; an
     honest commitment is not.
"""

import time
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    wait_until,
)

COIN = 10000000  # Cascoin uses 7 decimal places (1 CAS = 1e7 base units)


class L2FeatureTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.l2_args = [
            '-l2=1', '-l2mode=2', '-l2sequencer=1',
            '-l2minsequencerstake=1', '-l2minsequencerhatscore=10',
            '-l2blockintervalms=200', '-l2challengeseconds=120',
            '-datacarriersize=1000', '-fallbackfee=0.0001',
        ]
        self.extra_args = [self.l2_args]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    # --- helpers -----------------------------------------------------------

    def gen(self, n):
        """Reliably mine n blocks to a legacy miner address (works around the
        fact that a single generatetoaddress call may not always add a block)."""
        node = self.nodes[0]
        target = node.getblockcount() + n
        attempts = 0
        cap = n * 60 + 300
        while node.getblockcount() < target and attempts < cap:
            try:
                node.generatetoaddress(1, self.miner)
            except Exception:
                pass
            attempts += 1
        assert node.getblockcount() >= target, "failed to mine %d blocks" % n

    def l2bal(self, addr):
        return self.nodes[0].l2_getbalance(addr)['balance']

    def make_burn(self, wallet_node, amount):
        """Create+sign+broadcast a burn of `amount` CAS; return the L2 recipient."""
        node = self.nodes[0]
        leg = node.getnewaddress('', 'legacy')
        pub = node.validateaddress(leg)['pubkey']
        chg = node.getnewaddress('', 'legacy')
        j = node.l2_createburntx(amount, pub, chg)
        signed = node.signrawtransaction(j['hex'])
        assert_equal(signed['complete'], True)
        node.l2_sendburntx(signed['hex'])
        return j['l2RecipientAddress']

    # --- test --------------------------------------------------------------

    def run_test(self):
        node = self.nodes[0]
        self.miner = node.getnewaddress('', 'legacy')
        self.log.info("Maturing coinbase (legacy) ...")
        self.gen(120)

        self.test_burn_and_mint()
        self.test_onchain_transfer_and_signature()
        self.test_block_signed()
        self.test_transfer_fees()
        self.test_nonce_ordering()
        self.test_data_availability_reindex()
        self.test_persistence_across_restart()
        self.test_sequencer_registry()
        self.test_commitment_and_fraud_proof()
        self.test_l1_reorg_rollback()
        self.test_auto_commit()
        self.test_antispam_cap()
        self.log.info("All L2 functional tests passed!")

    def test_burn_and_mint(self):
        node = self.nodes[0]
        self.log.info("1) Burn-and-mint ...")
        supply_before = node.l2_gettotalsupply()['totalSupply']
        self.mint_addr = self.make_burn(node, 20)
        self.gen(8)  # >= REQUIRED_CONFIRMATIONS (6)
        assert_equal(self.l2bal(self.mint_addr), 20 * COIN)
        supply_after = node.l2_gettotalsupply()['totalSupply']
        assert_equal(supply_after - supply_before, Decimal('20'))
        vs = node.l2_verifysupply()
        assert_equal(vs['valid'], True)
        self.log.info("   minted 20, supply invariant valid")

    def test_onchain_transfer_and_signature(self):
        node = self.nodes[0]
        self.log.info("2/3) On-chain transfer + signature auth ...")
        frm = self.mint_addr
        to = '0x1111111111111111111111111111111111111111'
        before_from = self.l2bal(frm)
        res = node.l2_transfer(frm, to, 5)
        assert_equal(res['status'], 'pending')
        rawtx = res['rawtx']
        # Sequencer produces a block and applies the transfer.
        wait_until(lambda: self.l2bal(to) == 5 * COIN, timeout=30)
        assert_equal(self.l2bal(frm), before_from - 5 * COIN)
        # It landed in a produced L2 block (block 1 contains the transfer).
        blk = node.l2_getblockbynumber(1, True)
        assert_greater_than(blk['transactionCount'], 0)

        # Signature authentication: tamper the signed raw tx (flip first byte of
        # the `from` field) so its signature no longer matches the sender.
        first = rawtx[0:2]
        flipped = '%02x' % (int(first, 16) ^ 0xff)
        tampered = flipped + rawtx[2:]
        assert_raises_rpc_error(-26, "Invalid or missing signature",
                                node.l2_sendtransaction, tampered)
        self.log.info("   transfer applied; tampered tx rejected")

    def test_block_signed(self):
        node = self.nodes[0]
        self.log.info("Sig) Produced L2 block is signed by its sequencer ...")
        # Block 1 (the first non-genesis block, produced in the transfer test)
        # must carry a recoverable sequencer signature. This is what lets any
        # peer authenticate the block and reject forgeries at ingest.
        blk = node.l2_getblockbynumber(1, False)
        seq = blk['sequencer']
        # A real sequencer address, not the zero (genesis) address.
        assert seq != '0x' + '0' * 40, "block 1 has no sequencer"
        assert_greater_than(blk['signatureCount'], 0)
        sigs = blk['signatures']
        assert any(s['sequencer'] == seq for s in sigs), \
            "no signature from the block's sequencer"
        # Single active sequencer -> the 2/3 quorum is 1, so the block is final
        # immediately (M3 finality reduces to the sole signer here).
        assert_equal(blk['isFinalized'], True)
        # Remember for the registry test: the sequencer identity must persist
        # across node restarts (validator key persistence).
        self.block1_seq = seq
        self.log.info("   block 1 signed + finalized by sequencer %s" % seq)

    def test_transfer_fees(self):
        node = self.nodes[0]
        self.log.info("Fees) Transfer fee is charged and credited to the sequencer ...")
        # The sequencer's L2 address is the one that signed block 1; it receives
        # transaction fees.
        seq = node.l2_getblockbynumber(1, False)['sequencer']
        frm = self.mint_addr
        to = '0x2222222222222222222222222222222222222222'
        # A fee of 0.0021 CAS == gasPrice(1) * gasLimit(21000) base units, so the
        # effective fee (floored to a multiple of the gas limit) is exact.
        fee = Decimal('0.0021')
        fee_base = 21000
        amt = 3
        before_from = self.l2bal(frm)
        before_to = self.l2bal(to)
        before_seq = self.l2bal(seq)
        res = node.l2_transfer(frm, to, amt, fee)
        assert_equal(res['status'], 'pending')
        assert_equal(Decimal(str(res['fee'])), fee)
        # Sequencer applies the transfer in the next L2 block.
        wait_until(lambda: self.l2bal(to) == before_to + amt * COIN, timeout=30)
        # Sender is debited amount + fee; sequencer is credited the fee.
        assert_equal(self.l2bal(frm), before_from - amt * COIN - fee_base)
        assert_equal(self.l2bal(seq), before_seq + fee_base)
        self.log.info("   fee of %s CAS debited from sender and credited to sequencer" % fee)

    def test_nonce_ordering(self):
        node = self.nodes[0]
        self.log.info("Nonce) Many queued transfers apply in nonce order ...")
        frm = self.mint_addr
        to = '0x4444444444444444444444444444444444444444'
        n = 5
        before_from = self.l2bal(frm)
        before_to = self.l2bal(to)
        start_nonce = node.l2_getbalance(frm)['nonce']
        # Enqueue several transfers back-to-back; each must get the next nonce.
        nonces = []
        for _ in range(n):
            r = node.l2_transfer(frm, to, 1)
            assert_equal(r['status'], 'pending')
            nonces.append(r['nonce'])
        assert_equal(nonces, list(range(start_nonce, start_nonce + n)))
        # All apply; recipient receives the full amount and the sender's nonce
        # advances by exactly n (no gaps, no reordering).
        wait_until(lambda: self.l2bal(to) == before_to + n * COIN, timeout=30)
        assert_equal(self.l2bal(frm), before_from - n * COIN)
        assert_equal(node.l2_getbalance(frm)['nonce'], start_nonce + n)
        self.log.info("   %d transfers applied in order; nonce advanced by %d" % (n, n))

    def test_data_availability_reindex(self):
        node = self.nodes[0]
        self.log.info("DA) L2 data on L1 -> transfers survive -reindex ...")
        # Restart with fast data-availability posting so the sequencer anchors
        # every L2 block's data to L1 quickly.
        self.restart_node(0, extra_args=self.l2_args + ['-l2datainterval=2'])

        # A recipient that ONLY ever received tokens via an L2 transfer (never a
        # burn/mint). Its balance can therefore only be restored after -reindex
        # if the transfer data was available on L1 and reconstructed - which is
        # exactly what M1 Data Availability provides.
        da_addr = '0x7777777777777777777777777777777777777777'
        before = self.l2bal(da_addr)
        node.l2_transfer(self.mint_addr, da_addr, 2)
        wait_until(lambda: self.l2bal(da_addr) == before + 2 * COIN, timeout=30)
        expected = self.l2bal(da_addr)

        # Wait until the sequencer reports all block data posted, mining L1
        # blocks so the L2DATA transactions get confirmed on-chain.
        def data_posted_and_mined():
            self.gen(1)
            time.sleep(0.5)
            return node.l2_getdatastatus()['dataComplete']
        wait_until(data_posted_and_mined, timeout=90)
        # A couple more blocks so the last data txs are safely confirmed.
        self.gen(3)

        # Reindex wipes the L2 database and rebuilds L2 state purely from L1.
        self.restart_node(0, extra_args=self.l2_args + ['-reindex'])
        assert_equal(self.l2bal(da_addr), expected)
        assert_equal(node.l2_verifysupply()['valid'], True)
        self.log.info("   transfer recipient reconstructed from L1 data after -reindex")

    def test_persistence_across_restart(self):
        node = self.nodes[0]
        self.log.info("4) Persistence across restart ...")
        bal_from = self.l2bal(self.mint_addr)
        bal_to = self.l2bal('0x1111111111111111111111111111111111111111')
        supply = node.l2_gettotalsupply()['totalSupply']
        self.restart_node(0, extra_args=self.l2_args)
        assert_equal(self.l2bal(self.mint_addr), bal_from)
        assert_equal(self.l2bal('0x1111111111111111111111111111111111111111'), bal_to)
        assert_equal(node.l2_gettotalsupply()['totalSupply'], supply)
        self.log.info("   balances and supply survived restart")

    def test_sequencer_registry(self):
        node = self.nodes[0]
        self.log.info("Reg) On-chain sequencer registration ...")
        # This node's own sequencer registers on-chain by burning CAS as stake.
        res = node.l2_registersequencer(5)
        addr = res['address']
        assert_equal(res['action'], 'register')
        # The sequencer identity must be the SAME as when block 1 was produced,
        # despite the DA-reindex and persistence restarts in between. This proves
        # the validator key persists across restarts (regression: it used to be
        # regenerated every restart because the key file was saved incorrectly).
        assert_equal(addr, self.block1_seq)
        self.gen(2)  # confirm the registration on L1
        seqs = node.l2_listsequencers()
        match = [s for s in seqs if s['address'] == addr]
        assert len(match) == 1, "sequencer not registered on-chain"
        assert_equal(match[0]['active'], True)
        assert_equal(match[0]['excluded'], False)
        assert_equal(match[0]['stake'], Decimal('5'))
        # Remember for later milestones/tests.
        self.seq_addr = addr
        self.log.info("   sequencer %s registered on-chain with 5 CAS burned stake" % addr)

    def test_commitment_and_fraud_proof(self):
        node = self.nodes[0]
        self.log.info("6/7) L1 anchoring + fraud proof ...")
        # Commit the latest L2 block's state root honestly.
        j = node.l2_createcommitment()
        block_num = j['l2BlockNumber']
        honest_root = j['honestStateRoot']
        funded = node.fundrawtransaction(j['hex'])
        signed = node.signrawtransaction(funded['hex'])
        node.sendrawtransaction(signed['hex'])
        self.gen(2)
        c = node.l2_getcommitment(block_num)
        assert_equal(c['found'], True)
        assert_equal(c['stateRoot'], honest_root)
        # Honest commitment -> no fraud.
        fp = node.l2_submitfraudproof(block_num)
        assert_equal(fp['fraudProven'], False)

        # Fraudulent commitment (override with a bogus root) -> fraud proven.
        bogus = 'de' * 32
        j2 = node.l2_createcommitment(block_num, bogus)
        funded2 = node.fundrawtransaction(j2['hex'])
        signed2 = node.signrawtransaction(funded2['hex'])
        node.sendrawtransaction(signed2['hex'])
        self.gen(2)
        fp2 = node.l2_submitfraudproof(block_num)
        assert_equal(fp2['fraudProven'], True)
        assert_greater_than(fp2['slashedAmount'], 0)
        # M4: proven fraud excludes the sequencer from the active set (exclusion
        # model - the burned stake is simply lost, no payout).
        assert_equal(fp2['sequencerExcluded'], True)
        seqs_after = node.l2_listsequencers()
        assert not any(s['address'] == self.seq_addr for s in seqs_after), \
            "excluded sequencer still listed as active"
        self.log.info("   honest commit clean; bogus commit slashed + sequencer excluded")

    def test_l1_reorg_rollback(self):
        node = self.nodes[0]
        self.log.info("5) L1-reorg rollback ...")
        supply0 = node.l2_gettotalsupply()['totalSupply']
        recipient = self.make_burn(node, 7)
        self.gen(1)
        burn_height = node.getblockcount()
        self.gen(8)  # mature -> mint
        supply1 = node.l2_gettotalsupply()['totalSupply']
        assert_equal(supply1 - supply0, Decimal('7'))
        assert_equal(self.l2bal(recipient), 7 * COIN)

        # Invalidate the block right after the burn block -> revert to the
        # snapshot at burn_height (burn seen but not yet minted) -> mint undone.
        inv_hash = node.getblockhash(burn_height + 1)
        node.invalidateblock(inv_hash)
        wait_until(lambda: node.l2_gettotalsupply()['totalSupply'] == supply0, timeout=30)
        assert_equal(self.l2bal(recipient), 0)
        self.log.info("   invalidateblock undid the mint")

        # Reconsider -> reconnect -> burn re-matures -> mint re-applied.
        node.reconsiderblock(inv_hash)
        self.gen(1)
        wait_until(lambda: node.l2_gettotalsupply()['totalSupply'] == supply1, timeout=30)
        assert_equal(self.l2bal(recipient), 7 * COIN)
        assert_equal(node.l2_verifysupply()['valid'], True)
        self.log.info("   reconsiderblock re-applied the mint")

    def test_auto_commit(self):
        node = self.nodes[0]
        self.log.info("Auto) Daemon auto-anchors the L2 tip on L1 ...")
        # Restart with a fast auto-commit interval so the scheduler posts a
        # commitment on its own (no manual l2_createcommitment RPC).
        self.restart_node(0, extra_args=self.l2_args + ['-l2commitinterval=2'])
        pre = node.l2_getcommitment()
        pre_num = int(pre['l2BlockNumber']) if pre['found'] else -1
        # Produce a new (uncommitted) L2 block above pre_num with a transfer.
        to = '0x5555555555555555555555555555555555555555'
        before_to = self.l2bal(to)
        node.l2_transfer(self.mint_addr, to, 1)
        wait_until(lambda: self.l2bal(to) == before_to + 1 * COIN, timeout=30)

        # The daemon should broadcast an L2COMMIT for the new tip on its own.
        # Mine L1 blocks so the auto-commit transaction is confirmed + recorded.
        def committed():
            self.gen(1)
            c = node.l2_getcommitment()
            return c['found'] and int(c['l2BlockNumber']) > pre_num
        wait_until(committed, timeout=120)
        c = node.l2_getcommitment()
        blk = node.l2_getblockbynumber(int(c['l2BlockNumber']), False)
        assert_equal(c['stateRoot'], blk['stateRoot'])
        assert_greater_than(c['l1Height'], 0)
        self.log.info("   daemon auto-committed L2 block %d at L1 height %d"
                      % (int(c['l2BlockNumber']), int(c['l1Height'])))

    def test_antispam_cap(self):
        node = self.nodes[0]
        self.log.info("Spam) Per-sender pending cap rejects excess transactions ...")
        # Restart with a low per-sender cap and a long L2 block interval so the
        # pending pool does not drain while we submit the burst.
        cap = 3
        self.restart_node(0, extra_args=self.l2_args + [
            '-l2maxpersender=%d' % cap, '-l2blockintervalms=600000'])
        frm = self.mint_addr
        to = '0x6666666666666666666666666666666666666666'
        # The first `cap` transfers are accepted; the next one is rejected.
        for _ in range(cap):
            r = node.l2_transfer(frm, to, 1)
            assert_equal(r['status'], 'pending')
        assert_raises_rpc_error(-26, "Too many pending transactions",
                                node.l2_transfer, frm, to, 1)
        self.log.info("   sender capped at %d pending transactions" % cap)


if __name__ == '__main__':
    L2FeatureTest().main()
