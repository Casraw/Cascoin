# L2 Quick Start Tutorial

Dieses Tutorial führt Sie Schritt für Schritt durch das Setup einer Cascoin L2 Demo Chain.

## Inhaltsverzeichnis

1. [Voraussetzungen](#voraussetzungen)
2. [L2 Chain starten](#l2-chain-starten)
3. [Konfigurationsoptionen](#konfigurationsoptionen)
4. [L2 Status prüfen](#l2-status-prüfen)
5. [Burn-and-Mint verstehen](#burn-and-mint-verstehen)
6. [Troubleshooting](#troubleshooting)

---

## Voraussetzungen

### Cascoin Core bauen

Bevor Sie die L2 Demo starten können, muss Cascoin Core gebaut sein:

```bash
# Repository klonen (falls noch nicht geschehen)
git clone https://github.com/AquariusCoin/cascoin.git
cd cascoin

# Build-System generieren
./autogen.sh

# Konfigurieren mit Wallet-Unterstützung
./configure --enable-wallet

# Kompilieren (nutzt alle CPU-Kerne)
make -j$(nproc)
```

Nach erfolgreichem Build finden Sie die Binaries in `src/`:
- `src/cascoind` - Der Daemon
- `src/cascoin-cli` - Das CLI-Tool

### Systemanforderungen

- **RAM**: Mindestens 2 GB (4 GB empfohlen)
- **Disk**: 500 MB für Regtest, 10+ GB für Testnet
- **OS**: Linux, macOS, oder Windows (WSL)

---

## L2 Chain starten

### Schritt 1: Zum Demo-Verzeichnis wechseln

```bash
cd contrib/demos/l2-chain
```

### Schritt 2: Setup-Skript ausführen

**Regtest-Modus** (empfohlen für erste Tests):

```bash
./setup_l2_demo.sh
```

**Testnet-Modus** (öffentliches Testnetzwerk):

```bash
./setup_l2_demo.sh --testnet
```

### Was passiert beim Setup?

Das Skript führt automatisch folgende Schritte aus:

1. **Node starten**
   ```
   [STEP] Starte Cascoin-Node mit L2-Unterstützung...
   ```
   Der Node wird mit `-l2=1 -l2mode=2` gestartet (Full Node Modus).

2. **Blöcke generieren** (nur Regtest)
   ```
   [STEP] Generiere 101 initiale Blöcke...
   ```
   101 Blöcke werden generiert, um Mining-Rewards freizuschalten.

3. **Sequencer registrieren**
   ```
   [STEP] Registriere Sequencer...
   ```
   Ein Sequencer wird mit 10 CAS Stake und HAT-Score 50 registriert.

4. **L2-Burn durchführen**
   ```
   [STEP] Führe L2-Burn durch (10 CAS)...
   ```
   CAS wird auf L1 verbrannt und L2-Tokens werden gemintet.

### Schritt 3: Erfolg verifizieren

Nach erfolgreichem Setup sehen Sie:

```
=============================================================================
Setup abgeschlossen!
=============================================================================

Die L2 Demo Chain ist jetzt bereit (Burn-and-Mint Model).

Wichtige Adressen:
  Mining-Adresse:    <address>
  Sequencer-Adresse: <address>
  L2-Adresse:        <address>
```

---

## Konfigurationsoptionen

### Setup-Skript Parameter

| Parameter | Beschreibung | Standard |
|-----------|--------------|----------|
| `--datadir <path>` | Datenverzeichnis | `~/.cascoin-l2-demo` |
| `--stake <amount>` | Sequencer-Stake in CAS | 10 |
| `--hatscore <score>` | HAT v2 Score (0-100) | 50 |
| `--burn <amount>` | CAS zu verbrennen | 10 |
| `--blocks <count>` | Initiale Blöcke (Regtest) | 101 |
| `--testnet` | Testnet statt Regtest | - |

### Beispiele

```bash
# Mehr Stake für Sequencer
./setup_l2_demo.sh --stake 50

# Eigenes Datenverzeichnis
./setup_l2_demo.sh --datadir /tmp/l2-test

# Testnet mit höherem Stake
./setup_l2_demo.sh --testnet --stake 100
```

### Konfigurationsdatei (regtest.conf)

Die wichtigsten Optionen in `config/regtest.conf`:

```ini
# L2 aktivieren
l2=1

# L2 Node-Modus
# 1 = Light Client (nur Header)
# 2 = Full Node (vollständige Validierung)
l2mode=2

# RPC-Server aktivieren
server=1
rpcuser=demo
rpcpassword=demo

# Fallback-Gebühr für Regtest
fallbackfee=0.0001
```

### Fortgeschrittene L2-Parameter

```ini
# Minimaler HAT-Score für Sequencer
l2minsequencerhatscore=50

# Minimaler Stake für Sequencer
l2minsequencerstake=10

# Blöcke pro Leader-Rotation
l2blocksperleader=10

# Ziel-Blockzeit (ms)
l2targetblocktime=500

# Konsens-Schwellenwert (%)
l2consensusthreshold=67
```

---

## L2 Status prüfen

### Chain-Info abrufen

```bash
cascoin-cli -regtest l2_getchaininfo
```

Ausgabe:
```json
{
  "enabled": true,
  "chainId": 1001,
  "blockHeight": 42,
  "stateRoot": "0x...",
  "sequencerCount": 1,
  "totalSupply": "10.00000000"
}
```

### Sequencer-Liste

```bash
cascoin-cli -regtest l2_getsequencers
```

Ausgabe:
```json
[
  {
    "address": "...",
    "stake": "10.00000000",
    "hatScore": 50,
    "isEligible": true,
    "blocksProduced": 5
  }
]
```

### L2 Balance prüfen

```bash
cascoin-cli -regtest l2_getbalance <l2_address>
```

### Total Supply

```bash
cascoin-cli -regtest l2_gettotalsupply
```

---

## Burn-and-Mint verstehen

### Das Modell

Cascoin L2 verwendet ein **Burn-and-Mint** Modell:

1. **Burn**: CAS wird auf L1 via OP_RETURN verbrannt
2. **Konsens**: 2/3 der Sequencer validieren den Burn
3. **Mint**: L2-Tokens werden nach Konsens gemintet

### Manueller Burn

```bash
# 1. Burn-Transaktion erstellen
cascoin-cli -regtest l2_createburntx 5 <l2_pubkey>

# 2. Transaktion signieren
cascoin-cli -regtest signrawtransactionwithwallet <hex>

# 3. Transaktion senden
cascoin-cli -regtest l2_sendburntx <signed_hex>

# 4. Status prüfen
cascoin-cli -regtest l2_getburnstatus <txid>
```

### Burn-Status

```bash
cascoin-cli -regtest l2_getburnstatus <txid>
```

Ausgabe:
```json
{
  "txid": "...",
  "amount": "5.00000000",
  "l2Address": "...",
  "l1Confirmations": 6,
  "consensusStatus": "CONFIRMED",
  "mintStatus": "MINTED",
  "sequencerVotes": 3
}
```

### Supply verifizieren

```bash
cascoin-cli -regtest l2_verifysupply
```

Prüft, dass L2 Total Supply = Summe aller Burns.

---

## Troubleshooting

### Problem: Node startet nicht

**Symptom**: `error: couldn't connect to server`

**Lösung**:
```bash
# Prüfe ob bereits ein Node läuft
pgrep -f cascoind

# Falls ja, stoppen
cascoin-cli -regtest stop

# Warte und starte neu
sleep 5
./setup_l2_demo.sh
```

### Problem: L2 nicht aktiviert

**Symptom**: `l2_getchaininfo` zeigt `"enabled": false`

**Lösung**:
```bash
# Node mit L2-Flag starten
cascoind -regtest -daemon -l2=1 -l2mode=2

# Oder cleanup und neu starten
./cleanup.sh
./setup_l2_demo.sh
```

### Problem: Sequencer-Registrierung fehlgeschlagen

**Symptom**: `error: insufficient funds`

**Lösung**:
```bash
# Mehr Blöcke generieren für Mining-Rewards
cascoin-cli -regtest generatetoaddress 50 $(cascoin-cli -regtest getnewaddress)

# Balance prüfen
cascoin-cli -regtest getbalance
```

### Problem: Burn-Konsens nicht erreicht

**Symptom**: `consensusStatus: "PENDING"` bleibt

**Lösung**:
```bash
# Mehr Blöcke generieren (Regtest)
cascoin-cli -regtest generatetoaddress 10 $(cascoin-cli -regtest getnewaddress)

# Prüfe Sequencer-Anzahl (min. 3 für Konsens)
cascoin-cli -regtest l2_getsequencers
```

### Problem: RPC-Verbindung fehlgeschlagen

**Symptom**: `error: Authorization failed`

**Lösung**:
```bash
# Prüfe RPC-Credentials in cascoin.conf
cat ~/.cascoin-l2-demo/cascoin.conf

# Sollte enthalten:
# rpcuser=demo
# rpcpassword=demo
```

### Problem: Testnet-Sync dauert lange

**Symptom**: `verificationprogress: 0.xx`

**Lösung**:
- Testnet-Sync kann mehrere Stunden dauern
- Prüfe Fortschritt: `cascoin-cli -testnet getblockchaininfo`
- Stelle sicher, dass Port 19333 offen ist

### Debug-Logging aktivieren

```bash
# In cascoin.conf hinzufügen:
debug=l2
debug=rpc
logtimestamps=1

# Log anzeigen
tail -f ~/.cascoin-l2-demo/regtest/debug.log
```

---

## Nächste Schritte

Nach erfolgreichem L2-Setup können Sie:

1. **Token deployen**: [Token Creation Tutorial](02_token_creation.md)
2. **Contracts entwickeln**: [CVM Developer Guide](../../../doc/CVM_DEVELOPER_GUIDE.md)
3. **Sequencer betreiben**: [L2 Operator Guide](../../../doc/L2_OPERATOR_GUIDE.md)

---

## Cleanup

Wenn Sie fertig sind:

```bash
# Regtest (löscht alle Daten)
./cleanup.sh

# Testnet (behält Blockchain-Daten)
./cleanup.sh --testnet --keep-data
```
