# E2E Test Instructions

## 🎯 Ziel

Vollständiger End-to-End Test des CVM (Cascoin Virtual Machine) und Web-of-Trust Systems.

---

## 📋 Voraussetzungen

1. **Cascoin Daemon läuft**:
```bash
./src/cascoind -regtest -daemon
```

2. **Genug Balance für Tests** (mindestens ~10 CAS):
```bash
# Methode A: Einfach (wenn generate funktioniert)
for i in {1..110}; do
    ./src/cascoin-cli -regtest generate 1
done

# Methode B: Deine Loop-Methode
for i in {1..500}; do
    ./src/cascoin-cli -regtest generatetoaddress 100000 $(./src/cascoin-cli -regtest getnewaddress)
done

# Check balance
./src/cascoin-cli -regtest getbalance
```

---

## 🚀 Test Ausführen

### Option 1: Einfach (wenn du schon Balance hast)
```bash
./run_e2e_test.sh
```

###Option 2: Mit parallel Mining
```bash
# Terminal 1: Mining im Hintergrund
while true; do
    ./src/cascoin-cli -regtest generate 1
    sleep 1
done

# Terminal 2: Test ausführen
./run_e2e_test.sh
```

---

## ✅ Erwartete Ergebnisse

### Bei Erfolg:
```
🎉 SUCCESS! E2E TEST PASSED! 🎉

Results:
  ✅ Trust Edges: 2
  ✅ Votes: 1
  ✅ Transactions were PROCESSED on-chain!
  ✅ Web-of-Trust system is WORKING!

🏆 Phase 3 is 100% COMPLETE and FUNCTIONAL!
```

### Das bedeutet:
- ✅ 3 CVM Transaktionen wurden erstellt
- ✅ Alle 3 TXs wurden gemined
- ✅ CVMBlockProcessor hat sie verarbeitet
- ✅ Daten wurden in der Database gespeichert
- ✅ Trust Graph ist funktionsfähig
- ✅ **ALLES FUNKTIONIERT!** 🎊

---

## 🔍 Manueller Check

Falls du Details sehen willst:

```bash
# 1. Trust Graph Stats
./src/cascoin-cli -regtest gettrustgraphstats

# 2. CVM Processing Logs
tail -100 ~/.cascoin/regtest/debug.log | grep "CVM:"

# 3. Blocks mit CVM TXs finden
for i in {1..50}; do
    HASH=$(./src/cascoin-cli -regtest getblockhash $i 2>/dev/null)
    TX_COUNT=$(./src/cascoin-cli -regtest getblock "$HASH" 1 2>/dev/null | jq '.tx | length')
    if [ "$TX_COUNT" -gt 1 ]; then
        echo "Block $i: $TX_COUNT transactions"
    fi
done

# 4. Einzelne TX analysieren
./src/cascoin-cli -regtest getrawtransaction <txid> true
```

---

## 🐛 Troubleshooting

### Problem: "Insufficient funds"
**Lösung**: Mine mehr Blocks für mature coins
```bash
for i in {1..110}; do
    ./src/cascoin-cli -regtest generate 1
done
```

### Problem: "Transactions not being mined"
**Mögliche Ursachen**:
1. Difficulty zu hoch → Mine mehr Blocks
2. TXs haben zu niedrige Fees → Sollte nicht passieren (wir haben 0.001 CAS fee)
3. Mining Problem → Verwende `generate 1` statt `generatetoaddress`

**Lösung**:
```bash
# Mine einzeln, mehrmals
for i in {1..20}; do
    ./src/cascoin-cli -regtest generate 1
    sleep 0.5
done
```

### Problem: "Stats show 0 even after mining"
**Das bedeutet**: TXs wurden gemined, aber NICHT verarbeitet.

**Debug Steps**:
```bash
# 1. Check if TXs are in blocks
./src/cascoin-cli -regtest getrawmempool  # Should be empty

# 2. Find which block has the TXs
for i in {1..50}; do
    HASH=$(./src/cascoin-cli -regtest getblockhash $i 2>/dev/null)
    BLOCK=$(./src/cascoin-cli -regtest getblock "$HASH" 2 2>/dev/null)
    TX_COUNT=$(echo "$BLOCK" | jq '.tx | length')
    if [ "$TX_COUNT" -gt 1 ]; then
        echo "Block $i has $TX_COUNT TXs"
        echo "$BLOCK" | jq '.tx[1]'  # Show non-coinbase TX
    fi
done

# 3. Check if block processing was called
tail -200 ~/.cascoin/regtest/debug.log | grep "Processing block"

# 4. Check for errors
tail -200 ~/.cascoin/regtest/debug.log | grep -i "error\|fail"
```

---

## 📊 Was wird getestet?

### 1. Transaction Creation
- ✅ `sendtrustrelation`: Alice trusts Bob (weight 50, bond 1.5 CAS)
- ✅ `sendtrustrelation`: Alice trusts Carol (weight 30, bond 1.3 CAS)
- ✅ `sendbondedvote`: Bob votes +80 for Carol (bond 1.8 CAS)

### 2. OP_RETURN Format
- ✅ Magic bytes: `43 56 4d 31` ("CVM1")
- ✅ OpType: `01 04` (TRUST_EDGE) oder `02 04` (BONDED_VOTE)
- ✅ Data serialization: 54 bytes für Trust Edge, 53 bytes für Vote

### 3. Mining
- ✅ TXs werden von Mempool akzeptiert
- ✅ TXs werden in Blocks inkludiert
- ✅ Blocks werden auf Chain geschrieben

### 4. Block Processing
- ✅ `CVMBlockProcessor::ProcessBlock` wird aufgerufen
- ✅ `ParseCVMOpReturn` extrahiert Daten
- ✅ `ProcessTrustEdge` speichert Trust Relations
- ✅ `ProcessBondedVote` speichert Votes

### 5. Database
- ✅ `CVMDatabase::WriteGeneric` speichert Daten
- ✅ `TrustGraph::AddTrustEdge` funktioniert
- ✅ `TrustGraph::RecordBondedVote` funktioniert
- ✅ Stats werden korrekt aktualisiert

---

## 🎯 Erfolgs-Kriterien

| Test | Erfolgskriterium |
|------|------------------|
| TX Creation | 3 TXIDs zurückgegeben |
| Mempool | 3 TXs im Mempool |
| Mining | Mempool leer nach Mining |
| Processing | `total_trust_edges >= 2` |
| Processing | `total_votes >= 1` |
| **GESAMT** | **ALLE KRITERIEN ERFÜLLT** |

---

## 💡 Tipps

1. **Wenn Mining langsam ist**: Das ist normal in Cascoin regtest. Sei geduldig oder verwende deine Loop-Methode.

2. **Wenn generate nicht funktioniert**: Manchmal hilft ein Neustart:
   ```bash
   ./src/cascoin-cli -regtest stop
   rm ~/.cascoin/regtest/mempool.dat
   ./src/cascoind -regtest -daemon
   ```

3. **Logs in Echtzeit beobachten**:
   ```bash
   tail -f ~/.cascoin/regtest/debug.log | grep "CVM:"
   ```

4. **Balance checken**:
   ```bash
   ./src/cascoin-cli -regtest getwalletinfo | jq '{balance, immature_balance}'
   ```

---

## 🏆 Wenn Alles Funktioniert

**GLÜCKWUNSCH!** 🎉

Du hast erfolgreich getestet:
- ✅ CVM Transaction Building
- ✅ OP_RETURN Serialization
- ✅ Mempool Integration
- ✅ Mining Integration
- ✅ Block Processing
- ✅ Web-of-Trust System
- ✅ Database Storage

**Phase 3 ist 100% komplett und funktionsfähig!**

Nächste Schritte:
1. Testnet Deployment
2. Community Testing
3. Mainnet Activation (Block 220,000)

---

**Viel Erfolg! 🚀**

