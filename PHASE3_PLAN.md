# Phase 3: On-Chain Integration - Implementation Plan

**Goal**: Enable Web-of-Trust transactions to be broadcast, included in blocks, and validated by the network.

**Status**: 🔄 IN PROGRESS

---

## 1. Architecture Overview

### Current State (Off-Chain):
```
User → RPC → Local DB → Done
           ❌ No network propagation
           ❌ Not in blocks
           ❌ Not validated by peers
```

### Target State (On-Chain):
```
User → RPC → Build TX → Sign → Broadcast → Mempool
                                              ↓
                                         Mining → Block
                                              ↓
                                    All nodes process & validate
```

---

## 2. Components to Implement

### 2.1 Transaction Builder Extensions

**File**: `src/cvm/txbuilder.cpp`

**New Functions**:
```cpp
// Build trust relationship transaction
CMutableTransaction BuildTrustTransaction(
    CWallet* wallet,
    uint160 toAddress,
    int16_t weight,
    CAmount bondAmount,
    string reason,
    CAmount& feeOut,
    string& errorOut
);

// Build bonded reputation vote transaction
CMutableTransaction BuildBondedVoteTransaction(
    CWallet* wallet,
    uint160 targetAddress,
    int16_t voteValue,
    CAmount bondAmount,
    string reason,
    CAmount& feeOut,
    string& errorOut
);
```

**Key Features**:
- Real UTXO selection from wallet
- Proper fee calculation
- Bond output (locked CAS)
- OP_RETURN with WoT data
- Change output back to wallet

---

### 2.2 Block Processor Extensions

**File**: `src/cvm/blockprocessor.cpp`

**New Functions**:
```cpp
// Process trust edge from block
bool ProcessTrustEdge(
    const CVMTrustEdgeData& trustData,
    const CTransaction& tx,
    int blockHeight,
    CVMDatabase& db
);

// Process bonded vote from block
bool ProcessBondedVote(
    const CVMBondedVoteData& voteData,
    const CTransaction& tx,
    int blockHeight,
    CVMDatabase& db
);

// Validate bond is properly locked
bool ValidateBond(
    const CTransaction& tx,
    CAmount expectedBond,
    uint160& bondAddress
);
```

**Responsibilities**:
- Parse OP_RETURN data from transactions
- Validate bond amounts
- Update trust graph in database
- Update reputation scores
- Handle reorgs (disconnect)

---

### 2.3 RPC Command Updates

**File**: `src/rpc/cvm.cpp`

**New Commands**:
```cpp
// Broadcast trust relationship
UniValue sendtrustrelation(const JSONRPCRequest& request);

// Broadcast bonded vote
UniValue sendbondedvote(const JSONRPCRequest& request);
```

**Flow**:
1. Build transaction (txbuilder)
2. Sign transaction (wallet)
3. Validate locally
4. Broadcast to mempool
5. Return txid

---

### 2.4 Validation Integration

**File**: `src/validation.cpp`

**Changes**:
```cpp
// In AcceptToMemoryPool or CheckTransaction
bool CheckWoTTransaction(
    const CTransaction& tx,
    CValidationState& state
) {
    // Check if it's a WoT transaction
    // Validate OP_RETURN format
    // Check bond amounts
    // Verify soft fork is active
    return true;
}
```

**Integration Points**:
- `AcceptToMemoryPool()` - Mempool acceptance
- `CheckBlock()` - Block validation
- `ConnectBlock()` - Block processing
- `DisconnectBlock()` - Reorg handling

---

## 3. Transaction Format

### 3.1 Trust Edge Transaction

```
Transaction Structure:
  Inputs:
    [0] Funding input from user wallet (signs the TX)
  
  Outputs:
    [0] OP_RETURN with trust edge data (0 CAS)
    [1] Bond output (locked, timelocked script) (bondAmount CAS)
    [2] Change back to wallet (remainder CAS)
  
  OP_RETURN Data:
    Magic: 0x43564d31 ("CVM1")
    OpType: 0x04 (TRUST_EDGE)
    Data: Serialized CVMTrustEdgeData
      - fromAddress (uint160)
      - toAddress (uint160)
      - weight (int16)
      - bondAmount (CAmount)
      - timestamp (uint32)
```

**Example**:
```
Input: 5.0 CAS (user's UTXO)
Output 0: OP_RETURN (0 CAS) - Trust data
Output 1: P2SH bond script (1.8 CAS) - Locked for 1440 blocks
Output 2: Change (3.0 CAS) - Back to user
Fee: 0.2 CAS
```

### 3.2 Bonded Vote Transaction

```
Transaction Structure:
  Inputs:
    [0] Funding input from user wallet
  
  Outputs:
    [0] OP_RETURN with vote data (0 CAS)
    [1] Bond output (locked) (bondAmount CAS)
    [2] Change back to wallet
  
  OP_RETURN Data:
    Magic: 0x43564d31
    OpType: 0x05 (BONDED_VOTE)
    Data: Serialized CVMBondedVoteData
      - voter (uint160)
      - target (uint160)
      - voteValue (int16)
      - bondAmount (CAmount)
      - timestamp (uint32)
```

---

## 4. Bond Locking Mechanism

### 4.1 Bond Script

**Purpose**: Lock CAS for a period to ensure skin-in-the-game

**Script Type**: P2SH with timelock

```
Script:
  OP_IF
    <blockHeight + 1440> OP_CHECKLOCKTIMEVERIFY OP_DROP
    <userPubKeyHash> OP_CHECKSIG
  OP_ELSE
    <DAO multisig> OP_CHECKMULTISIG
  OP_ENDIF
```

**Paths**:
1. **Normal return** (after 1440 blocks): User can reclaim bond
2. **Slash** (before timeout): DAO multisig can burn/redistribute

### 4.2 Bond Lifecycle

```
1. Creation:
   User locks bondAmount in P2SH script
   
2. Active Period (0-1440 blocks):
   Bond is locked
   Can be slashed if DAO votes to slash
   
3. After Timeout (1440+ blocks):
   If not slashed: User can reclaim
   If slashed: Bond burned/redistributed
```

---

## 5. Implementation Steps

### Step 1: Transaction Builder ✅ IN PROGRESS
- [ ] Implement UTXO selection helper
- [ ] Implement fee calculation
- [ ] Create bond script generator
- [ ] Build trust transaction
- [ ] Build bonded vote transaction

### Step 2: Block Processor
- [ ] Parse trust edge from OP_RETURN
- [ ] Parse bonded vote from OP_RETURN
- [ ] Validate bond outputs
- [ ] Store in trust graph database
- [ ] Handle disconnects (reorg)

### Step 3: RPC Commands
- [ ] `sendtrustrelation` implementation
- [ ] `sendbondedvote` implementation
- [ ] Error handling
- [ ] Help text

### Step 4: Validation
- [ ] Mempool acceptance checks
- [ ] Block validation checks
- [ ] Soft fork activation checks

### Step 5: Integration
- [ ] Hook into `ConnectBlock()`
- [ ] Hook into `DisconnectBlock()`
- [ ] Hook into `AcceptToMemoryPool()`

### Step 6: Testing
- [ ] Create trust transaction in regtest
- [ ] Verify it goes to mempool
- [ ] Mine block with trust TX
- [ ] Verify all nodes see trust edge
- [ ] Test bond locking
- [ ] Test reorg handling

---

## 6. Success Criteria

### Functional Tests

**Test 1: Trust Edge On-Chain**
```bash
# Alice creates trust relationship with Bob
cascoin-cli sendtrustrelation "QBobAddr..." 80 2.0 "Friend"

# Verify in mempool
cascoin-cli getrawmempool
# Should show: ["txid123..."]

# Mine block
cascoin-cli generate 1

# Verify on-chain
cascoin-cli gettrustgraphstats
# Should show: "total_trust_edges": 1

# Verify bond locked
cascoin-cli gettxout "txid123..." 1
# Should show: locked P2SH output
```

**Test 2: Bonded Vote On-Chain**
```bash
# Alice votes on Bob's reputation
cascoin-cli sendbondedvote "QBobAddr..." 100 1.5 "Trusted"

# Verify in mempool
cascoin-cli getrawmempool

# Mine block
cascoin-cli generate 1

# Verify on-chain
cascoin-cli getweightedreputation "QBobAddr..." "QAliceAddr..."
# Should show updated reputation
```

**Test 3: Network Sync**
```bash
# Node A creates trust edge
nodeA$ cascoin-cli sendtrustrelation "QAddr..." 80 2.0 "Test"

# Node B should see in mempool
nodeB$ cascoin-cli getrawmempool
# Should show same txid

# Node C mines block
nodeC$ cascoin-cli generate 1

# All nodes should have trust edge
nodeA$ cascoin-cli gettrustgraphstats
nodeB$ cascoin-cli gettrustgraphstats
nodeC$ cascoin-cli gettrustgraphstats
# All should show: "total_trust_edges": 1
```

---

## 7. Technical Challenges

### Challenge 1: UTXO Selection
**Problem**: Need to select UTXOs with enough balance for bond + fee
**Solution**: Use existing `CWallet::SelectCoins()` with custom `CCoinControl`

### Challenge 2: Bond Script
**Problem**: Need secure P2SH script for bond locking
**Solution**: Use `OP_CHECKLOCKTIMEVERIFY` + multisig for DAO slash path

### Challenge 3: Reorg Safety
**Problem**: Trust edges might be reorganized out
**Solution**: Store block height with edges, mark as "unconfirmed" until 6+ confirmations

### Challenge 4: DAO Multisig
**Problem**: Need trusted DAO members for slash path
**Solution**: Bootstrap with founder keys, transition to elected DAO over time

### Challenge 5: Soft Fork Compatibility
**Problem**: Old nodes must accept new transactions
**Solution**: OP_RETURN is always valid for old nodes, they just ignore WoT data

---

## 8. Estimated Timeline

**Week 1**: Transaction Builder (20 hours)
- UTXO selection
- Fee calculation
- Bond scripts
- Transaction construction

**Week 2**: Block Processor (20 hours)
- OP_RETURN parsing
- Database updates
- Validation logic
- Reorg handling

**Week 3**: RPC Commands (10 hours)
- sendtrustrelation
- sendbondedvote
- Testing infrastructure

**Week 4**: Validation Integration (15 hours)
- Mempool checks
- Block validation
- Consensus rules

**Week 5**: Testing & Debugging (25 hours)
- Unit tests
- Integration tests
- Regtest scenarios
- Bug fixes

**Week 6**: Polish & Documentation (10 hours)
- Code cleanup
- Documentation
- Release notes
- Final testing

**Total**: 100 hours (~6-10 weeks at 2-4 hours/day)

---

## 9. Next Actions

**Immediate** (Today):
1. ✅ Start with BuildTrustTransaction in txbuilder.cpp
2. Implement UTXO selection helper
3. Create bond script generator
4. Test basic transaction creation

**This Week**:
- Complete transaction builder
- Start block processor
- Basic RPC commands

**Next Week**:
- Validation integration
- End-to-end testing
- Bug fixing

---

## 10. Risk Mitigation

**Risk**: Breaking existing functionality
**Mitigation**: Feature flag, can disable with `-nowot` option

**Risk**: Security vulnerabilities in bond script
**Mitigation**: Extensive testing, security audit before mainnet

**Risk**: Network splits if validation wrong
**Mitigation**: Soft fork design, thorough testing on testnet

**Risk**: Performance degradation
**Mitigation**: Benchmark, optimize database queries

---

**Document Status**: Living Document
**Last Updated**: November 3, 2025
**Phase**: 3 (On-Chain Integration)
**Progress**: 0% → Target: 100% by December 2025

