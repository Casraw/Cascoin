# Smart Contract Demos

Dieses Verzeichnis enthält Smart Contract Beispiele für Cascoin L2.

## Übersicht

Die Demo-Contracts zeigen die Integration von Cascoin's Web-of-Trust Reputation System in Smart Contracts. Der Haupt-Contract ist ein ERC-20-kompatibler Token mit Reputation-basierten Transfer-Limits.

## Dateien

| Datei | Beschreibung |
|-------|--------------|
| `CascoinToken.sol` | ERC-20 Token mit Trust-Integration (Solidity) |
| `CascoinToken.cvm` | Vereinfachte CVM Bytecode Version |
| `deploy_token.sh` | Deployment-Skript |
| `demo_transfer.sh` | Transfer-Demo-Skript |

---

## CascoinToken Contract

### Features

- **ERC-20 Kompatibilität**: Standard-Funktionen (transfer, approve, transferFrom, balanceOf)
- **Reputation-Integration**: Nutzt Cascoin's Web-of-Trust für Transfer-Limits
- **Trust-gated Transfers**: Mindest-Reputation von 30 für Transfers erforderlich
- **Gestaffelte Limits**: Höhere Reputation ermöglicht größere Transfers

### Reputation-Stufen

| Reputation | Max. Transfer |
|------------|---------------|
| 90-100 | Unbegrenzt |
| 70-89 | 100,000 CDEMO |
| 50-69 | 10,000 CDEMO |
| 30-49 | 1,000 CDEMO |
| 0-29 | Kein Transfer |

---

## Contract-Funktionen (API)

### ERC-20 Standard Funktionen

#### `transfer(address to, uint256 amount) → bool`

Transferiert Token an eine andere Adresse.

**Parameter:**
- `to`: Empfänger-Adresse
- `amount`: Zu transferierende Menge (in Wei, d.h. mit 18 Dezimalstellen)

**Returns:** `true` bei Erfolg

**Reverts wenn:**
- Sender-Reputation < 30
- Sender-Balance < amount
- amount > maxTransferAmount(sender)

**Beispiel-Aufruf:**
```bash
# Transfer von 100 Token (100 * 10^18 Wei)
cascoin-cli -regtest callcontract <contract> \
    "transfer(address,uint256)" \
    "0x1234567890abcdef1234567890abcdef12345678" \
    "100000000000000000000" \
    --from <sender_address>
```

---

#### `approve(address spender, uint256 amount) → bool`

Erlaubt einer Adresse, Token im Namen des Callers auszugeben.

**Parameter:**
- `spender`: Adresse die Erlaubnis erhält
- `amount`: Erlaubter Betrag (in Wei)

**Returns:** `true` bei Erfolg

**Beispiel-Aufruf:**
```bash
# Erlaube DEX 1000 Token auszugeben
cascoin-cli -regtest callcontract <contract> \
    "approve(address,uint256)" \
    "<dex_address>" \
    "1000000000000000000000" \
    --from <owner_address>
```

---

#### `transferFrom(address from, address to, uint256 amount) → bool`

Transferiert Token im Namen einer anderen Adresse (erfordert vorherige Approval).

**Parameter:**
- `from`: Adresse von der Token abgezogen werden
- `to`: Empfänger-Adresse
- `amount`: Zu transferierende Menge (in Wei)

**Returns:** `true` bei Erfolg

**Reverts wenn:**
- from-Reputation < 30
- from-Balance < amount
- allowance[from][caller] < amount
- amount > maxTransferAmount(from)

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "transferFrom(address,address,uint256)" \
    "<from_address>" \
    "<to_address>" \
    "50000000000000000000" \
    --from <spender_address>
```

---

#### `balanceOf(address account) → uint256`

Gibt das Token-Guthaben einer Adresse zurück.

**Parameter:**
- `account`: Abzufragende Adresse

**Returns:** Token-Balance in Wei

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "balanceOf(address)" \
    "<address>"
```

**Beispiel-Ausgabe:**
```json
{
  "result": "1000000000000000000000000",
  "gasUsed": 2100
}
```

---

#### `allowance(address owner, address spender) → uint256`

Gibt die erlaubte Ausgabemenge für einen Spender zurück.

**Parameter:**
- `owner`: Token-Besitzer
- `spender`: Adresse mit Ausgabe-Erlaubnis

**Returns:** Erlaubter Betrag in Wei

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "allowance(address,address)" \
    "<owner_address>" \
    "<spender_address>"
```

---

### Cascoin Trust-Funktionen

#### `maxTransferAmount(address account) → uint256`

Berechnet das maximale Transfer-Limit basierend auf Reputation.

**Parameter:**
- `account`: Adresse deren Limit berechnet wird

**Returns:** Maximaler Transfer-Betrag in Wei

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "maxTransferAmount(address)" \
    "<address>"
```

**Beispiel-Ausgabe (Reputation 70):**
```json
{
  "result": "100000000000000000000000",
  "gasUsed": 2500
}
```
(= 100,000 Token)

---

#### `getReputation(address account) → uint8`

Gibt die Reputation einer Adresse zurück (0-100).

**Parameter:**
- `account`: Abzufragende Adresse

**Returns:** Reputation Score (0-100)

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "getReputation(address)" \
    "<address>"
```

**Beispiel-Ausgabe:**
```json
{
  "result": "50",
  "gasUsed": 2800
}
```

---

#### `canTransfer(address account, uint256 amount) → (bool, string)`

Prüft ob ein Transfer möglich wäre und gibt den Grund zurück.

**Parameter:**
- `account`: Sender-Adresse
- `amount`: Gewünschter Transfer-Betrag

**Returns:**
- `canTransfer`: true/false
- `reason`: Erklärung ("Transfer allowed", "Reputation too low", etc.)

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "canTransfer(address,uint256)" \
    "<address>" \
    "1000000000000000000000"
```

**Beispiel-Ausgabe:**
```json
{
  "result": [true, "Transfer allowed"],
  "gasUsed": 3200
}
```

---

### Owner-Funktionen

#### `setMinTransferReputation(uint8 newMinReputation)`

Ändert die Mindest-Reputation für Transfers. Nur durch Owner aufrufbar.

**Parameter:**
- `newMinReputation`: Neuer Schwellenwert (0-100)

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "setMinTransferReputation(uint8)" \
    "40" \
    --from <owner_address>
```

---

#### `transferOwnership(address newOwner)`

Überträgt Ownership an eine neue Adresse. Nur durch Owner aufrufbar.

**Parameter:**
- `newOwner`: Neue Owner-Adresse

**Beispiel-Aufruf:**
```bash
cascoin-cli -regtest callcontract <contract> \
    "transferOwnership(address)" \
    "<new_owner_address>" \
    --from <current_owner>
```

---

### Read-Only Properties

| Property | Typ | Beschreibung |
|----------|-----|--------------|
| `name` | string | "Cascoin Demo Token" |
| `symbol` | string | "CDEMO" |
| `decimals` | uint8 | 18 |
| `totalSupply` | uint256 | Gesamtmenge aller Token |
| `minTransferReputation` | uint8 | Mindest-Reputation (Standard: 30) |
| `owner` | address | Contract Owner |

**Beispiel-Aufrufe:**
```bash
# Name abfragen
cascoin-cli -regtest callcontract <contract> "name()"

# Symbol abfragen
cascoin-cli -regtest callcontract <contract> "symbol()"

# Total Supply abfragen
cascoin-cli -regtest callcontract <contract> "totalSupply()"
```

---

## Events

### Transfer

```solidity
event Transfer(address indexed from, address indexed to, uint256 value);
```

Emittiert bei jedem Token-Transfer. `from = 0x0` bedeutet Minting.

### Approval

```solidity
event Approval(address indexed owner, address indexed spender, uint256 value);
```

Emittiert bei Allowance-Änderungen.

### TransferRejectedLowReputation

```solidity
event TransferRejectedLowReputation(address indexed from, uint8 reputation, uint8 required);
```

Emittiert wenn ein Transfer wegen niedriger Reputation abgelehnt wird.

### MinReputationChanged

```solidity
event MinReputationChanged(uint8 oldValue, uint8 newValue);
```

Emittiert wenn die Mindest-Reputation geändert wird.

---

## Verwendung

### Voraussetzungen

1. L2 Demo Chain muss laufen:
   ```bash
   cd ../l2-chain
   ./setup_l2_demo.sh
   ```

2. Optional: Solidity Compiler für Solidity-Version:
   ```bash
   # Ubuntu/Debian
   sudo apt install solc
   
   # macOS
   brew install solidity
   ```

### Token deployen

```bash
# Mit Standardwerten (1M Token)
./deploy_token.sh

# Mit eigener Supply
./deploy_token.sh --supply 5000000

# Mit CVM-Bytecode (ohne solc)
./deploy_token.sh --cvm

# Auf Testnet
./deploy_token.sh --testnet
```

### Token transferieren

```bash
# Standard-Transfer (100 Token)
./demo_transfer.sh

# Eigene Menge
./demo_transfer.sh --amount 500

# Mit spezifischer Contract-Adresse
./demo_transfer.sh --contract 0x1234...
```

---

## Vollständiges Beispiel-Szenario

```bash
# 1. L2 Chain starten
cd ../l2-chain
./setup_l2_demo.sh
cd ../contracts

# 2. Token deployen
./deploy_token.sh --supply 1000000
# Ausgabe: Contract Address: 0xABC...

# 3. Contract-Adresse speichern
CONTRACT="0xABC..."

# 4. Deployer-Balance prüfen
cascoin-cli -regtest callcontract $CONTRACT "balanceOf(address)" $(cascoin-cli -regtest getnewaddress)

# 5. Neue Empfänger-Adresse erstellen
RECIPIENT=$(cascoin-cli -regtest getnewaddress)

# 6. Transfer durchführen
./demo_transfer.sh --amount 1000

# 7. Empfänger-Balance prüfen
cascoin-cli -regtest callcontract $CONTRACT "balanceOf(address)" $RECIPIENT

# 8. Reputation prüfen
cascoin-cli -regtest callcontract $CONTRACT "getReputation(address)" $RECIPIENT

# 9. Max Transfer Limit prüfen
cascoin-cli -regtest callcontract $CONTRACT "maxTransferAmount(address)" $RECIPIENT
```

---

## CVM Bytecode Version

Die `CascoinToken.cvm` Datei enthält eine vereinfachte Token-Implementation in nativen CVM-Opcodes.

### Eigenschaften

- Benötigt keinen Solidity-Compiler
- Ist kleiner und effizienter
- Hat keine Reputation-Integration (nur Basis-Token)
- Dient hauptsächlich zu Lernzwecken

### Storage Layout

```
Key                    | Value
-----------------------|------------------
0x00                   | totalSupply
0x01                   | owner
0x03                   | decimals (18)
0x10 + address         | balances[address]
0x20 + hash(from,to)   | allowances[from][to]
```

### Gas-Kosten

| Operation | Gas |
|-----------|-----|
| Constructor | ~21,500 |
| Transfer | ~10,500 |
| BalanceOf | ~250 |
| Approve | ~5,500 |

---

## Troubleshooting

### "Contract-Adresse nicht verfügbar"

Der Contract wurde noch nicht deployed. Führen Sie zuerst aus:
```bash
./deploy_token.sh
```

### "Sender reputation too low"

Die Sender-Adresse hat eine Reputation unter 30. Im Regtest-Modus wird standardmäßig eine Reputation von 50 verwendet.

### "Amount exceeds reputation limit"

Der Transfer-Betrag überschreitet das Reputation-basierte Limit. Prüfen Sie mit:
```bash
cascoin-cli -regtest callcontract <contract> "maxTransferAmount(address)" <sender>
```

### "solc nicht gefunden"

Installieren Sie den Solidity-Compiler oder verwenden Sie die CVM-Version:
```bash
./deploy_token.sh --cvm
```

### "Node läuft nicht"

Starten Sie die L2 Demo Chain:
```bash
cd ../l2-chain
./setup_l2_demo.sh
```

---

## Weiterführende Dokumentation

- [L2 Quick Start Tutorial](../tutorials/01_l2_quickstart.md)
- [Token Creation Tutorial](../tutorials/02_token_creation.md)
- [CVM Developer Guide](../../../doc/CVM_DEVELOPER_GUIDE.md)
- [L2 Developer Guide](../../../doc/L2_DEVELOPER_GUIDE.md)
