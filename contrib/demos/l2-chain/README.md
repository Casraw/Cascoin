# L2 Demo Chain (Burn-and-Mint Model)

Dieses Verzeichnis enthält Skripte und Konfiguration für das L2 Demo Chain Setup mit dem **Burn-and-Mint Token Model**.

## Burn-and-Mint Model

Das Burn-and-Mint Model ist ein kryptographisch sicheres System für Cross-Layer Value Transfer:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    BURN-AND-MINT FLOW                               │
│                                                                     │
│  1. User verbrennt CAS auf L1 via OP_RETURN                         │
│     └─► OP_RETURN Outputs sind kryptographisch unspendable          │
│                                                                     │
│  2. Sequencer validieren den Burn                                   │
│     └─► Prüfen Format, Chain-ID, Bestätigungen                      │
│                                                                     │
│  3. 2/3 Sequencer-Konsens                                           │
│     └─► Mindestens 67% der Sequencer müssen bestätigen              │
│                                                                     │
│  4. L2-Tokens werden gemintet                                       │
│     └─► 1:1 zum verbrannten Betrag                                  │
└─────────────────────────────────────────────────────────────────────┘
```

**Vorteile:**
- **Kryptographisch sicher**: OP_RETURN kann nicht ausgegeben werden
- **Keine Bridge-Hacks**: Kein Smart Contract der gehackt werden kann
- **1:1 Backing**: L2 Supply = Verbrannte CAS
- **Dezentraler Konsens**: 2/3 Sequencer müssen zustimmen

## Dateien

- `setup_l2_demo.sh` - Startet eine vollständige L2-Demo-Umgebung
- `cleanup.sh` - Bereinigt Regtest-Daten
- `config/regtest.conf` - Regtest-Konfiguration mit L2-Optionen

## Schnellstart

```bash
# L2 Demo starten
./setup_l2_demo.sh

# Mit benutzerdefinierten Optionen
./setup_l2_demo.sh --burn 50 --stake 20

# Aufräumen
./cleanup.sh
```

## Demo Workflow

### 1. Demo starten

```bash
./setup_l2_demo.sh
```

Das Skript führt automatisch folgende Schritte aus:
1. Startet Cascoin-Node mit L2-Unterstützung
2. Generiert initiale Blöcke für Mining-Rewards
3. Registriert einen Sequencer
4. Führt einen Burn durch (CAS → L2-Tokens)
5. Wartet auf Konsens und zeigt Status

### 2. Burn-Status prüfen

Nach dem Setup können Sie den Burn-Status prüfen:

```bash
# Burn-Status abfragen
cascoin-cli -regtest l2_getburnstatus "<l1_tx_hash>"
```

Beispiel-Ausgabe:
```json
{
  "found": true,
  "l1TxHash": "abc123...",
  "l1Confirmations": 8,
  "requiredConfirmations": 6,
  "burnAmount": "10.00000000",
  "l2Recipient": "0xa1b2c3...",
  "consensusStatus": "REACHED",
  "confirmationCount": 1,
  "totalSequencers": 1,
  "mintStatus": "MINTED",
  "l2MintBlock": 115
}
```

### 3. L2-Balance prüfen

```bash
# L2-Balance abfragen
cascoin-cli -regtest l2_getbalance "<l2_address>"
```

Beispiel-Ausgabe:
```json
{
  "address": "0xa1b2c3...",
  "balance": 1000000000,
  "balance_cas": "10.00000000",
  "nonce": 0
}
```

### 4. Weitere Burns durchführen

```bash
# Neue L2-Adresse erstellen
L2_ADDR=$(cascoin-cli -regtest getnewaddress "l2_new")

# Burn-Transaktion erstellen
BURN_TX=$(cascoin-cli -regtest l2_createburntx 25 "$L2_ADDR")

# Transaktion signieren
BURN_HEX=$(echo $BURN_TX | jq -r '.hex')
SIGNED=$(cascoin-cli -regtest signrawtransactionwithwallet "$BURN_HEX")
SIGNED_HEX=$(echo $SIGNED | jq -r '.hex')

# Burn-Transaktion senden
cascoin-cli -regtest l2_sendburntx "$SIGNED_HEX"

# Blöcke generieren für Bestätigungen
MINING_ADDR=$(cascoin-cli -regtest getnewaddress)
cascoin-cli -regtest generatetoaddress 10 "$MINING_ADDR"
```

### 5. Supply verifizieren

```bash
# Total Supply abfragen
cascoin-cli -regtest l2_gettotalsupply

# Supply-Invariante prüfen
cascoin-cli -regtest l2_verifysupply
```

Beispiel-Ausgabe für `l2_verifysupply`:
```json
{
  "valid": true,
  "totalSupply": "35.00000000",
  "sumOfBalances": "35.00000000",
  "totalBurnedL1": "35.00000000",
  "discrepancy": "0.00000000"
}
```

## RPC-Befehle

### Burn-and-Mint Befehle

| Befehl | Beschreibung |
|--------|--------------|
| `l2_createburntx <amount> <l2_addr>` | Erstellt Burn-Transaktion |
| `l2_sendburntx <hex>` | Sendet signierte Burn-TX |
| `l2_getburnstatus <l1_txid>` | Prüft Burn/Konsens-Status |
| `l2_getpendingburns` | Zeigt ausstehende Burns |
| `l2_getminthistory` | Zeigt Mint-Historie |
| `l2_gettotalsupply` | Zeigt L2 Total Supply |
| `l2_verifysupply` | Verifiziert Supply-Invariante |
| `l2_getburnsforaddress <addr>` | Burns für Adresse |

### Allgemeine L2-Befehle

| Befehl | Beschreibung |
|--------|--------------|
| `l2_getchaininfo` | L2 Chain-Informationen |
| `l2_getbalance <addr>` | L2-Balance abfragen |
| `l2_getsequencers` | Sequencer-Liste |
| `l2_announcesequencer <stake> <hat>` | Als Sequencer registrieren |

## Konsens beobachten

In einer Single-Node Demo wird der Konsens automatisch erreicht (1 von 1 = 100%).

In einer Multi-Node Umgebung können Sie den Konsens-Fortschritt beobachten:

```bash
# Pending Burns anzeigen
cascoin-cli -regtest l2_getpendingburns

# Beispiel-Ausgabe:
[
  {
    "l1TxHash": "abc123...",
    "burnAmount": "10.00000000",
    "l2Recipient": "0xa1b2c3...",
    "confirmationCount": 2,
    "totalSequencers": 5,
    "confirmationRatio": 0.4,
    "status": "PENDING"
  }
]
```

Der Konsens wird erreicht wenn `confirmationRatio >= 0.67` (2/3).

## Konfiguration

Die Demo verwendet `config/regtest.conf` mit folgenden L2-Einstellungen:

```ini
# L2 aktivieren
l2=1
l2mode=2  # Full Node

# Burn-and-Mint Parameter (Regtest-Defaults)
# l2burnconfirmations=6      # L1-Bestätigungen vor Konsens
# l2consensusthreshold=67    # 2/3 Mehrheit
# l2consensustimeout=600     # 10 Minuten Timeout
```

## Fehlerbehebung

### "L2 is not enabled"
```bash
# Prüfen ob L2 aktiviert ist
cascoin-cli -regtest l2_getchaininfo

# Node mit L2 neu starten
cascoind -regtest -l2=1 -daemon
```

### Konsens wird nicht erreicht
```bash
# Mehr Blöcke generieren
cascoin-cli -regtest generatetoaddress 10 $(cascoin-cli -regtest getnewaddress)

# Pending Burns prüfen
cascoin-cli -regtest l2_getpendingburns
```

### Burn-Status zeigt "PENDING"
- Warten Sie auf mindestens 6 L1-Bestätigungen
- Generieren Sie weitere Blöcke
- In Multi-Node Setup: Prüfen Sie ob genug Sequencer online sind

## Aufräumen

```bash
# Demo beenden und Daten löschen
./cleanup.sh

# Oder mit benutzerdefiniertem Datenverzeichnis
./cleanup.sh --datadir /pfad/zum/verzeichnis
```
