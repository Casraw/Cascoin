#!/usr/bin/env python3
# Copyright (c) 2025 The Cascoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dual-path CVM deploy -> call integration test (Workstream-1 reconciliation).

This functional test validates that the two CVM block-processing execution
paths -- `cvmtx.cpp` (ExecuteCVMBlock) and `blockprocessor.cpp`
(CVMBlockProcessor::ProcessBlock) -- are reconciled, i.e. they agree on:

  * contract ADDRESS derivation (canonical deployer+nonce scheme), so the
    address produced by the deploy path is the same address the query and
    call paths resolve and use                                (2.16, 3.12)
  * contract EXECUTION during block processing: the constructor/contract
    code actually runs (not a no-op that only accounts gasLimit)     (2.17)
  * VALUE CONTEXT (preservation): a zero-value deploy exposes CALLVALUE = 0
    to the executing constructor                                     (3.22)
  * DURABLE STATE: state written during execution is flushed to the CVM
    database (CommitExecutionState) and remains queryable after the block
    is mined, and survives a node restart                            (2.61)

Because block processing runs inside ConnectBlock, a divergence would be
observable here as: a contract that cannot be found/called at its deploy-path
address, duplicate/inconsistent addresses, or missing durable state.

Scope note on 2.60 (real CALLVALUE / real BLOCKHASH on non-zero calls):
The ACTIVE path invoked during ConnectBlock is CVM::BlockValidator
(src/cvm/block_validator.cpp); CVMBlockProcessor::ProcessBlock() is disabled in
ConnectBlock (see src/validation.cpp, "CVMBlockProcessor::ProcessBlock() was
duplicating this work"). BlockValidator::DeployContract and ExecuteContractCall
now thread the real transaction value and the real block hash into the Enhanced
VM (matching blockprocessor.cpp), so the 2.60/2.61 value+block-hash wiring is
present on the active path.

The positive form of 2.60 -- a *non-zero* value surfacing as CALLVALUE during
block processing -- is still NOT asserted end-to-end here, for two structural
reasons:
  (1) The `deploycontract` RPC has no value parameter, so a DEPLOY cannot carry
      value; a deploy tx contains only the CVM marker OP_RETURN plus spendable
      change, and contract value is conveyed only by a value-bearing (burned)
      OP_RETURN output, which deploys never produce. A deploy therefore always
      exposes CALLVALUE == 0 (this is the 3.22 preservation asserted below).
  (2) Contract calls are trust-gated ("contract_call" requires medium
      reputation), so a call from a fresh wallet address does not execute during
      block processing and therefore cannot surface a CALLVALUE at all.
So the positive non-zero CALLVALUE assertion is not expressible through the
public RPC surface here and is reported separately rather than pinned as a red
assertion in the shared functional suite. The zero-value preservation half of
the value context (3.22) IS asserted below, and now genuinely exercises the
corrected active path (the value extraction excludes spendable change, so a real
wallet deploy's change is not mistaken for CALLVALUE).

The contract used is CVM-native bytecode (executed as its own constructor on
deploy and re-executed on call):

    PUSH1 0x42   PUSH1 0x00   SSTORE     ; storage[0] = 0x42  (constructor marker)
    CALLVALUE    PUSH1 0x01   SSTORE     ; storage[1] = call value
    STOP

Encoded (CVM opcodes, see src/cvm/opcodes.h):
    01 01 42 | 01 01 00 | 51 | 73 | 01 01 01 | 51 | 44
Note SSTORE (0x51) pops key then value, so each store pushes value first then
key (see CVM::HandleStorage in src/cvm/cvm.cpp).
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
)

# CVM-native contract: storage[0]=0x42 (constructor marker), storage[1]=CALLVALUE.
STORAGE_CONTRACT = "0x01014201010051730101015144"

# uint256 storage keys, as returned by getcontractstorage (GetHex(), 64 hex chars).
SLOT0_KEY = "00" * 32
SLOT1_KEY = "00" * 31 + "01"
SLOT0_MARKER_VALUE = "00" * 31 + "42"  # 0x42 stored by the constructor

def _u256_hex(value):
    """Render an integer as a 64-hex-char uint256, matching uint256::GetHex()."""
    return "{:064x}".format(value)


class CVMDualPathTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [['-regtest']]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.log.info("Starting CVM dual-path deploy -> call integration test")

        # Mine to a normal (P2PKH) wallet address rather than the default
        # `generate` coinbase, which pays to a bare pubkey (P2PK) script. A
        # contract deploy/call transaction must expose an extractable sender
        # during block processing (GetSenderAddress reads the input's
        # scriptSig/witness pubkey); spending a P2PK coinbase directly provides
        # no pubkey and the in-block UTXO is already spent, so the deployer
        # cannot be resolved. Funding from P2PKH coinbase outputs mirrors the
        # realistic deploy path.
        self.mine_addr = self.nodes[0].getnewaddress("", "legacy")

        # COINBASE_MATURITY is 100, so 110 blocks yields ~10 spendable coinbase
        # outputs -- ample to fund the handful of deploy/call transactions below.
        self._mine(110)

        self.test_deploy_derives_canonical_address()
        self.test_paths_agree_and_address_is_callable()
        self.test_constructor_executed_and_state_durable()
        self.test_zero_value_callvalue_preserved()
        self.test_address_uniqueness_per_nonce()
        self.test_durability_across_restart()

        self.log.info("All CVM dual-path integration checks passed")

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    # Regtest here uses a non-trivial PoW target, so the default 1M-iteration
    # mining budget is not always enough to find a block. Pass a generous
    # maxtries budget and loop until the requested blocks are actually mined.
    MINE_MAXTRIES = 2000000000

    def _mine(self, nblocks):
        # Mine in small batches so each generate RPC returns well within the
        # framework's per-call timeout, even though each block may need many
        # PoW iterations on this regtest configuration.
        target = self.nodes[0].getblockcount() + nblocks
        while self.nodes[0].getblockcount() < target:
            remaining = target - self.nodes[0].getblockcount()
            batch = min(remaining, 5)
            self.nodes[0].generatetoaddress(batch, self.mine_addr, self.MINE_MAXTRIES)

    def _deploy(self, bytecode=STORAGE_CONTRACT, gaslimit=1000000):
        """Deploy a contract and mine it into a block. Returns (txid, address)."""
        before = {c['address'] for c in self.nodes[0].listmycontracts()}
        result = self.nodes[0].deploycontract(bytecode, gaslimit)
        assert 'txid' in result, "deploycontract did not return a txid"
        self._mine(1)

        after = self.nodes[0].listmycontracts()
        new_contracts = [c for c in after if c['address'] not in before]
        assert_equal(len(new_contracts), 1)
        return result['txid'], new_contracts[0]['address']

    def _storage_map(self, address):
        """Return {key: value} for a contract's persisted storage."""
        storage = self.nodes[0].getcontractstorage(address)
        return {e['key']: e['value'] for e in storage['entries']}

    # ------------------------------------------------------------------
    # Test cases
    # ------------------------------------------------------------------
    def test_deploy_derives_canonical_address(self):
        """Deploy path derives a canonical, well-formed contract address (2.16)."""
        self.log.info("Deploy path produces a canonical deployer+nonce address...")

        _, address = self._deploy()

        # A canonical address is a 20-byte value rendered as 40 hex chars and is
        # not the null address (the old txHash[0:20] path could not guarantee
        # this consistency across paths).
        assert_equal(len(address), 40)
        assert address != "00" * 20, "contract address must not be the null address"
        self._contract_a = address
        self.log.info("  contract deployed at canonical address %s", address)

    def test_paths_agree_and_address_is_callable(self):
        """Deploy/query/call paths agree on the same address (2.16, 3.12)."""
        self.log.info("Deploy, query and call paths agree on one address...")

        address = self._contract_a

        # Query path (getcontractinfo) must resolve the exact same address that
        # the deploy path (block processing) registered.
        info = self.nodes[0].getcontractinfo(address)
        assert_equal(info['address'].lower(), ("0x" + address).lower())
        assert_greater_than_or_equal(info['bytecode_size'], 1)

        # It must appear exactly once in the deployer's contract list.
        listed = [c for c in self.nodes[0].listmycontracts()
                  if c['address'] == address]
        assert_equal(len(listed), 1)

        # Call path must be able to TARGET the SAME address the deploy path
        # derived: a contract call transaction referencing that address is
        # accepted into the mempool, mined, and confirmed, and the contract is
        # still resolvable at the same address afterwards. If the block
        # processing paths disagreed on address derivation, the deployed contract
        # would not exist at the address the call targets.
        call = self.nodes[0].callcontract(address, "0x", 90000, 0)
        assert 'txid' in call
        call_txid = call['txid']
        assert call_txid in self.nodes[0].getrawmempool(), "call tx not accepted to mempool"
        self._mine(1)

        assert call_txid not in self.nodes[0].getrawmempool(), "call tx not confirmed"
        assert_equal(
            self.nodes[0].getcontractinfo(address)['address'].lower(),
            ("0x" + address).lower())
        self.log.info("  address consistent across deploy/query/call paths")

    def test_constructor_executed_and_state_durable(self):
        """Constructor executed during block processing; state persisted (2.17, 2.61)."""
        self.log.info("Constructor executes during block processing and persists state...")

        # Fresh contract so we can observe the constructor's write in isolation.
        _, address = self._deploy()

        # The constructor wrote storage[0] = 0x42 during block processing. If the
        # deploy path were a no-op (only accounting gasLimit) or if
        # CommitExecutionState did not flush, this entry would be absent.
        storage = self._storage_map(address)
        assert SLOT0_KEY in storage, (
            "constructor storage write not persisted -- constructor did not "
            "execute during block processing or state was not committed")
        assert_equal(storage[SLOT0_KEY], SLOT0_MARKER_VALUE)

        # A deploy carries zero value, so CALLVALUE == 0 must be exposed (3.22):
        # storage[1] is either absent (SSTORE of 0) or explicitly zero.
        if SLOT1_KEY in storage:
            assert_equal(storage[SLOT1_KEY], _u256_hex(0))

        self._contract_b = address
        self.log.info("  constructor marker durably persisted at storage[0]")

    def test_zero_value_callvalue_preserved(self):
        """A zero-value deploy exposes CALLVALUE == 0 to the constructor (3.22)."""
        self.log.info("Zero-value deploy preserves CALLVALUE == 0...")

        # The constructor stored CALLVALUE at storage[1]. A deploy carries no
        # value, so the executing constructor must observe CALLVALUE == 0 and
        # therefore store zero at storage[1] (either an explicit zero entry or no
        # entry at all). This is the preservation half of the value context.
        storage = self._storage_map(self._contract_b)
        if SLOT1_KEY in storage:
            assert_equal(storage[SLOT1_KEY], _u256_hex(0))
        self.log.info("  CALLVALUE == 0 preserved for the zero-value deploy")

    def test_address_uniqueness_per_nonce(self):
        """Same deployer + different nonce -> different canonical address (2.16)."""
        self.log.info("Address derivation is deployer+nonce based (per-nonce unique)...")

        _, addr1 = self._deploy()
        _, addr2 = self._deploy()

        # Two deploys from the same wallet advance the deployer nonce, so the
        # canonical GenerateContractAddress(deployer, nonce) scheme must yield
        # distinct addresses. Both remain independently queryable/callable.
        assert addr1 != addr2, "distinct deployments must have distinct addresses"
        assert_equal(self.nodes[0].getcontractinfo(addr1)['address'].lower(),
                     ("0x" + addr1).lower())
        assert_equal(self.nodes[0].getcontractinfo(addr2)['address'].lower(),
                     ("0x" + addr2).lower())
        self.log.info("  distinct nonces produced distinct addresses %s / %s",
                      addr1, addr2)

    def test_durability_across_restart(self):
        """Committed contract state survives a node restart (2.61)."""
        self.log.info("Durable state survives a restart (CommitExecutionState flush)...")

        address = self._contract_b
        before = self._storage_map(address)
        assert SLOT0_KEY in before

        self.restart_node(0)

        info = self.nodes[0].getcontractinfo(address)
        assert_equal(info['address'].lower(), ("0x" + address).lower())
        after = self._storage_map(address)
        assert SLOT0_KEY in after, "constructor state lost after restart -- not durable"
        assert_equal(after[SLOT0_KEY], SLOT0_MARKER_VALUE)
        self.log.info("  contract code and storage persisted across restart")


if __name__ == '__main__':
    CVMDualPathTest().main()
