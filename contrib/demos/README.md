# Cascoin Demo Code

Praktische Demo-Beispiele für Cascoin L2 und Smart Contracts. Diese Demos ermöglichen einen schnellen Einstieg in die L2-Entwicklung mit lauffähigen Beispielen.

## Übersicht

| Demo | Beschreibung | Verzeichnis |
|------|--------------|-------------|
| **L2 Demo Chain** | Vollständiges L2-Setup im Regtest/Testnet-Modus | `l2-chain/` |
| **CascoinToken** | ERC-20 Token mit Trust-Integration | `contracts/` |
| **Tutorials** | Schritt-für-Schritt Anleitungen | `tutorials/` |

## Prerequisites

### Erforderlich

- **Cascoin Core** - Gebaut mit L2-Unterstützung
  ```bash
  # Im Repository-Root:
  ./autogen.sh
  ./configure --enable-wallet
  make -j$(nproc)
  ```

- **Bash** - Version 4.0 oder höher
- **jq** - JSON-Prozessor (optional, für erweiterte Ausgabe)
  ```bash
  # Ubuntu/Debian
  sudo apt install jq
  
  # macOS
  brew install jq
  ```

### Optional (für Solidity-Contracts)

- **solc** - Solidity Compiler (Version 0.8.0+)
  ```bash
  # Ubuntu/Debian
  sudo add-apt-repository ppa:ethereum/ethereum
  sudo apt update
  sudo apt install solc
  
  # macOS
  brew install solidity
  
  # Oder via npm
  npm install -g solc
  ```

## Quick Start

### 1. L2 Demo Chain starten

```bash
cd contrib/demos/l2-chain

# Regtest-Modus (empfohlen für erste Tests)
./setup_l2_demo.sh

# Oder Testnet-Modus (öffentliches Testnetzwerk)
./setup_l2_demo.sh --testnet
```

Das Setup-Skript:
- Startet einen Cascoin-Node mit L2-Unterstützung
- Generiert initiale Blöcke (Regtest) oder wartet auf Sync (Testnet)
- Registriert einen Sequencer
- Führt einen L2-Burn durch (Burn-and-Mint Model)

### 2. Token Contract deployen

```bash
cd contrib/demos/contracts

# Deploy mit Standardwerten (1M Token)
./deploy_token.sh

# Oder mit eigener Supply
./deploy_token.sh --supply 5000000
```

### 3. Token Transfer testen

```bash
# Transfer von 100 Token
./demo_transfer.sh

# Oder mit eigener Menge
./demo_transfer.sh --amount 500
```

### 4. Aufräumen

```bash
cd contrib/demos/l2-chain

# Regtest cleanup (löscht alle Daten)
./cleanup.sh

# Testnet cleanup (behält Blockchain-Daten)
./cleanup.sh --testnet
```

## Verzeichnisstruktur

```
contrib/demos/
├── README.md                    # Diese Datei
├── l2-chain/
│   ├── setup_l2_demo.sh        # L2 Chain Setup (Regtest/Testnet)
│   ├── cleanup.sh              # Cleanup Skript
│   └── config/
│       ├── regtest.conf        # Regtest Konfiguration
│       └── testnet.conf        # Testnet Konfiguration
├── contracts/
│   ├── CascoinToken.sol        # ERC-20 Token (Solidity)
│   ├── CascoinToken.cvm        # Token in CVM Opcodes
│   ├── deploy_token.sh         # Deployment Skript
│   ├── demo_transfer.sh        # Transfer Demo
│   └── README.md               # Contract Dokumentation
└── tutorials/
    ├── 01_l2_quickstart.md     # L2 Quick Start
    └── 02_token_creation.md    # Token Creation Guide
```

## Burn-and-Mint Token Model

Cascoin L2 verwendet ein **Burn-and-Mint** Modell für Token-Transfers zwischen L1 und L2:

1. **Burn auf L1**: CAS wird via OP_RETURN-Transaktion verbrannt
2. **Sequencer-Konsens**: 2/3 der Sequencer validieren den Burn
3. **Mint auf L2**: Nach Konsens werden L2-Tokens gemintet

```
┌─────────────┐     Burn TX      ┌─────────────┐
│    L1       │ ───────────────► │  OP_RETURN  │
│  (CAS)      │                  │  (verbrannt)│
└─────────────┘                  └─────────────┘
                                        │
                                        ▼
                               ┌─────────────────┐
                               │ Sequencer       │
                               │ Konsens (2/3)   │
                               └─────────────────┘
                                        │
                                        ▼
                               ┌─────────────────┐
                               │    L2 Tokens    │
                               │   (gemintet)    │
                               └─────────────────┘
```

## Nützliche RPC-Befehle

### L2 Chain Status

```bash
# Chain-Info
cascoin-cli -regtest l2_getchaininfo

# Sequencer-Liste
cascoin-cli -regtest l2_getsequencers

# L2 Balance
cascoin-cli -regtest l2_getbalance <address>

# Total Supply
cascoin-cli -regtest l2_gettotalsupply
```

### Burn-and-Mint

```bash
# Burn-Transaktion erstellen
cascoin-cli -regtest l2_createburntx <amount> <l2_pubkey>

# Pending Burns anzeigen
cascoin-cli -regtest l2_getpendingburns

# Burn-Status prüfen
cascoin-cli -regtest l2_getburnstatus <txid>

# Supply verifizieren
cascoin-cli -regtest l2_verifysupply
```

### Contract-Interaktion

```bash
# Contract deployen
cascoin-cli -regtest deploycontract <bytecode> <gas_limit>

# Contract aufrufen
cascoin-cli -regtest callcontract <address> <function> <args...>

# Contract-Info
cascoin-cli -regtest getcontractinfo <address>
```

## Fehlerbehebung

### Node startet nicht

```bash
# Prüfe ob bereits ein Node läuft
cascoin-cli -regtest getblockchaininfo

# Falls ja, stoppen
cascoin-cli -regtest stop

# Warte kurz und starte neu
sleep 5
./setup_l2_demo.sh
```

### L2 nicht aktiviert

```bash
# Prüfe L2-Status
cascoin-cli -regtest l2_getchaininfo

# Falls "enabled": false, Node mit -l2=1 starten
cascoind -regtest -daemon -l2=1 -l2mode=2
```

### Nicht genug Balance (Testnet)

```bash
# Zeige aktuelle Balance
cascoin-cli -testnet getbalance

# Hole Testnet-CAS vom Faucet:
# https://faucet.cascoin.org/testnet
```

### Contract-Deployment fehlgeschlagen

```bash
# Prüfe Gas-Limit (Standard: 500000)
# Prüfe Wallet-Balance
cascoin-cli -regtest getbalance

# Generiere mehr Blöcke (Regtest)
cascoin-cli -regtest generatetoaddress 10 $(cascoin-cli -regtest getnewaddress)
```

## Weiterführende Dokumentation

- [L2 Developer Guide](../../doc/L2_DEVELOPER_GUIDE.md) - Vollständige L2-Dokumentation
- [CVM Developer Guide](../../doc/CVM_DEVELOPER_GUIDE.md) - Smart Contract Entwicklung
- [L2 Operator Guide](../../doc/L2_OPERATOR_GUIDE.md) - Sequencer-Betrieb

## Tutorials

1. [L2 Quick Start](tutorials/01_l2_quickstart.md) - Erste Schritte mit L2
2. [Token Creation](tutorials/02_token_creation.md) - Eigenen Token erstellen

## Lizenz

Diese Demo-Dateien sind Teil des Cascoin-Projekts und stehen unter der MIT-Lizenz.
