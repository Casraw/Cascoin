# CVM Transaction Broadcasting - Implementation Plan

## Ziel
CVM-Votes und Contract-Deployments als **echte Bitcoin-Transaktionen** implementieren, die:
1. ✅ Im **Mempool** landen
2. ✅ In **Blöcken** gemined werden
3. ✅ **On-Chain** gespeichert sind
4. ✅ **Soft Fork kompatibel** bleiben (OP_RETURN)

---

## Architektur-Übersicht

```
┌─────────────────────────────────────────────────────────────────┐
│                         USER (RPC Call)                         │
│  sendcvmvote <address> <vote> "reason"                         │
│  sendcvmcontract <bytecode> <gaslimit>                         │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Transaction Builder                          │
│  • Select UTXOs (funding)                                       │
│  • Build outputs:                                               │
│    - Output 0: OP_RETURN mit CVM-Daten                         │
│    - Output 1: Change zurück an Sender                         │
│  • Calculate fees                                               │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Transaction Signing                          │
│  • Sign inputs mit Wallet-Keys                                  │
│  • Verify signatures                                            │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Mempool Acceptance                           │
│  • Standard validation (fees, size, etc.)                       │
│  • CVM Soft Fork validation (nur neue Nodes)                   │
│  • Broadcast to network                                         │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Mining / Block Creation                      │
│  • Miner inkludiert TX in Block                                │
│  • Block wird propagiert                                        │
└────────────────────────────┬────────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Block Validation                             │
│  • OLD Nodes: Sehen OP_RETURN → OK ✅                          │
│  • NEW Nodes: Parse CVM data → Validate → Update DB ✅         │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Transaction Builder

### File: `src/cvm/txbuilder.h` + `.cpp`

```cpp
class CVMTransactionBuilder {
public:
    // Build reputation vote transaction
    static CMutableTransaction BuildVoteTransaction(
        CWallet* wallet,
        const uint160& targetAddress,
        int16_t voteValue,
        const std::string& reason,
        CAmount& fee,
        std::string& error
    );
    
    // Build contract deployment transaction
    static CMutableTransaction BuildDeployTransaction(
        CWallet* wallet,
        const std::vector<uint8_t>& bytecode,
        uint64_t gasLimit,
        CAmount& fee,
        std::string& error
    );
    
    // Build contract call transaction
    static CMutableTransaction BuildCallTransaction(
        CWallet* wallet,
        const uint160& contractAddress,
        const std::vector<uint8_t>& callData,
        uint64_t gasLimit,
        CAmount value,
        CAmount& fee,
        std::string& error
    );
    
private:
    // Helper: Select UTXOs for funding
    static bool SelectCoins(
        CWallet* wallet,
        CAmount amount,
        std::vector<COutput>& selected,
        CAmount& total
    );
    
    // Helper: Add change output
    static void AddChangeOutput(
        CMutableTransaction& tx,
        CWallet* wallet,
        CAmount change
    );
    
    // Helper: Calculate transaction fee
    static CAmount CalculateFee(const CMutableTransaction& tx);
};
```

**Wichtig:**
- ✅ Verwendet bestehende Wallet-Funktionen
- ✅ Standard Bitcoin-TX-Format
- ✅ OP_RETURN Output = Soft Fork!

---

## Phase 2: Mempool Integration

### File: `src/validation.cpp` (erweitern)

```cpp
bool AcceptToMemoryPool(...) {
    // ... existing validation ...
    
    // CVM Soft Fork validation (nur wenn aktiv)
    if (CVM::IsCVMSoftForkActive(chainActive.Height(), chainparams.GetConsensus())) {
        std::string cvmError;
        if (!CVM::ValidateCVMSoftFork(tx, chainActive.Height(), 
                                      chainparams.GetConsensus(), cvmError)) {
            // Für Soft Fork: Nur warnen, nicht rejecten!
            LogPrintf("CVM: Soft fork validation warning: %s\n", cvmError);
            // OLD nodes werden hier nichts checken
            // NEW nodes loggen nur
        }
    }
    
    // ... rest of validation ...
}
```

**Soft Fork Safety:**
- ❌ **NICHT** `return false;` bei CVM-Fehlern
- ✅ Nur **loggen** für neue Nodes
- ✅ Alte Nodes sehen nur OP_RETURN → akzeptieren

---

## Phase 3: Block Processing

### File: `src/cvm/blockprocessor.h` + `.cpp`

```cpp
class CVMBlockProcessor {
public:
    // Process all CVM transactions in a block
    static void ProcessBlock(
        const CBlock& block,
        int height,
        CVMDatabase& db
    );
    
private:
    // Process single CVM transaction
    static void ProcessTransaction(
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    // Process reputation vote
    static void ProcessVote(
        const CVMReputationData& vote,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
    
    // Process contract deployment
    static void ProcessDeploy(
        const CVMDeployData& deploy,
        const CTransaction& tx,
        int height,
        CVMDatabase& db
    );
};
```

### Integration in `validation.cpp`:

```cpp
bool ConnectBlock(...) {
    // ... existing block validation ...
    
    // Process CVM transactions (nur neue Nodes)
    if (CVM::IsCVMSoftForkActive(pindex->nHeight, chainparams.GetConsensus())) {
        if (CVM::g_cvmdb) {
            CVM::CVMBlockProcessor::ProcessBlock(block, pindex->nHeight, *CVM::g_cvmdb);
        }
    }
    
    return true;
}
```

---

## Phase 4: RPC Commands

### Neue Commands in `src/rpc/cvm.cpp`:

```cpp
// Broadcast reputation vote (creates real transaction!)
UniValue sendcvmvote(const JSONRPCRequest& request) {
    // 1. Build transaction
    CMutableTransaction tx = CVMTransactionBuilder::BuildVoteTransaction(...);
    
    // 2. Sign transaction
    wallet->SignTransaction(tx);
    
    // 3. Broadcast to mempool
    uint256 txid;
    BroadcastTransaction(tx, txid);
    
    // 4. Return result
    return txid.GetHex();
}

// Broadcast contract deployment
UniValue sendcvmcontract(const JSONRPCRequest& request) {
    // Similar to sendcvmvote
}
```

---

## Phase 5: Testing Plan

### Test 1: Vote Transaction Flow
```bash
# 1. Create vote
$ cascoin-cli -regtest sendcvmvote QiAddress 100 "Good user"
txid: abc123...

# 2. Check mempool
$ cascoin-cli -regtest getrawmempool
["abc123..."]  ✅ Im Mempool!

# 3. Mine block
$ cascoin-cli -regtest generate 1

# 4. Check on-chain
$ cascoin-cli -regtest gettransaction abc123...
"confirmations": 1  ✅ Auf Chain!

# 5. Verify reputation updated
$ cascoin-cli -regtest getreputation QiAddress
"score": 100  ✅ DB updated!
```

### Test 2: Soft Fork Compatibility
```bash
# OLD Node (ohne CVM):
- Sieht OP_RETURN → Accepts block ✅
- Ignoriert CVM-Daten

# NEW Node (mit CVM):
- Sieht OP_RETURN → Accepts block ✅
- Parsed CVM-Daten → Validates ✅
- Updated Reputation DB ✅
```

---

## Sicherheits-Überlegungen

### 1. **Fee-Estimation**
```cpp
CAmount fee = CalculateFee(tx);
if (fee < minRelayTxFee) {
    return error("Fee too low");
}
```

### 2. **Spam-Prevention**
```cpp
// Limit CVM transactions per block
if (cvmTxCount > MAX_CVM_TX_PER_BLOCK) {
    // Reject in mempool (miners decide)
}
```

### 3. **Gas Limits**
```cpp
uint64_t totalGas = 0;
for (auto& cvmTx : blockCVMTxs) {
    totalGas += cvmTx.gasLimit;
    if (totalGas > params.cvmMaxGasPerBlock) {
        return error("Block gas limit exceeded");
    }
}
```

### 4. **Soft Fork Safety**
```cpp
// CRITICAL: Never reject blocks due to CVM validation!
// Only log warnings for monitoring
if (!ValidateCVM(tx)) {
    LogPrintf("CVM Warning: %s\n", error);
    // Continue anyway for soft fork compatibility
}
```

---

## Timeline

| Phase | Zeit | Status |
|-------|------|--------|
| 1. Transaction Builder | 1-2h | Pending |
| 2. Mempool Integration | 1h | Pending |
| 3. Block Processing | 1-2h | Pending |
| 4. RPC Commands | 1h | Pending |
| 5. Testing | 2-3h | Pending |
| **Total** | **6-9h** | |

---

## Nächste Schritte

1. ✅ **Transaction Builder** implementieren
2. ✅ **Wallet-Integration** testen
3. ✅ **Mempool** akzeptiert CVM-TXs
4. ✅ **Block Validation** verarbeitet CVM-TXs
5. ✅ **Ende-zu-Ende Test**: Vote → Mempool → Block → DB

**Bereit zum Start?** 🚀

