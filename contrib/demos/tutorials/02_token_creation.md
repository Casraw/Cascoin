# Token Creation Tutorial

Dieses Tutorial erklärt Schritt für Schritt, wie Sie einen ERC-20-kompatiblen Token mit Cascoin Trust-Integration erstellen.

## Inhaltsverzeichnis

1. [Übersicht](#übersicht)
2. [Contract-Struktur](#contract-struktur)
3. [Code-Erklärung](#code-erklärung)
4. [Reputation-Integration](#reputation-integration)
5. [Deployment-Prozess](#deployment-prozess)
6. [Token-Interaktion](#token-interaktion)

---

## Übersicht

Der **CascoinToken** ist ein ERC-20-kompatibler Token mit zusätzlichen Trust-Features:

- Standard ERC-20 Funktionen (transfer, approve, transferFrom)
- Reputation-basierte Transfer-Limits
- Integration mit Cascoin's Web-of-Trust System
- Mindest-Reputation für Transfers

### Dateien

| Datei | Beschreibung |
|-------|--------------|
| `CascoinToken.sol` | Vollständiger Solidity-Contract |
| `CascoinToken.cvm` | Vereinfachte CVM-Bytecode-Version |
| `deploy_token.sh` | Deployment-Skript |
| `demo_transfer.sh` | Transfer-Demo |

---

## Contract-Struktur

### Solidity-Version (CascoinToken.sol)

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract CascoinToken {
    // Cascoin Reputation Oracle Precompile
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    // ERC-20 Standard
    string public name = "Cascoin Demo Token";
    string public symbol = "CDEMO";
    uint8 public decimals = 18;
    uint256 public totalSupply;
    
    // Trust Integration
    uint8 public minTransferReputation = 30;
    address public owner;
    
    // Balances und Allowances
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;
    
    // Events
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
    
    // ... Funktionen
}
```

---

## Code-Erklärung

### 1. Konstanten und State Variables

```solidity
// Cascoin Reputation Oracle Precompile
address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
```

**Erklärung**: Dies ist die Adresse des Cascoin Reputation Oracle Precompile. Precompiles sind spezielle Adressen, die native Funktionen bereitstellen. Das Reputation Oracle ermöglicht Smart Contracts den Zugriff auf das Web-of-Trust System.

```solidity
string public name = "Cascoin Demo Token";
string public symbol = "CDEMO";
uint8 public decimals = 18;
```

**Erklärung**: Standard ERC-20 Metadaten:
- `name`: Vollständiger Token-Name
- `symbol`: Kurzes Ticker-Symbol (wie bei Börsen)
- `decimals`: Anzahl Dezimalstellen (18 ist Standard, wie bei ETH)

```solidity
uint8 public minTransferReputation = 30;
```

**Erklärung**: Mindest-Reputation für Transfers. Adressen mit niedrigerer Reputation können keine Token senden. Die Skala ist 0-100.

### 2. Constructor

```solidity
constructor(uint256 initialSupply) {
    owner = msg.sender;
    totalSupply = initialSupply * 10**decimals;
    balanceOf[msg.sender] = totalSupply;
    emit Transfer(address(0), msg.sender, totalSupply);
}
```

**Erklärung**:
1. `owner = msg.sender` - Der Deployer wird Owner
2. `totalSupply = initialSupply * 10**decimals` - Berechnet tatsächliche Supply mit Dezimalstellen
3. `balanceOf[msg.sender] = totalSupply` - Alle Token gehen an Deployer
4. `emit Transfer(...)` - Mint-Event (von Adresse 0)

**Beispiel**: `initialSupply = 1000000` ergibt 1,000,000 Token mit 18 Dezimalstellen.

### 3. Transfer-Funktion

```solidity
function transfer(address to, uint256 amount) public returns (bool) {
    // Hole Reputation des Senders
    uint8 senderRep = _getReputation(msg.sender);
    
    // Prüfe Mindest-Reputation
    if (senderRep < minTransferReputation) {
        emit TransferRejectedLowReputation(msg.sender, senderRep, minTransferReputation);
        revert("CascoinToken: sender reputation too low");
    }
    
    // Prüfe Guthaben
    require(balanceOf[msg.sender] >= amount, "CascoinToken: insufficient balance");
    
    // Prüfe Reputation-basiertes Limit
    uint256 maxAmount = maxTransferAmount(msg.sender);
    require(amount <= maxAmount, "CascoinToken: amount exceeds reputation limit");
    
    // Führe Transfer durch
    balanceOf[msg.sender] -= amount;
    balanceOf[to] += amount;
    
    emit Transfer(msg.sender, to, amount);
    return true;
}
```

**Erklärung**:
1. **Reputation prüfen**: Holt Reputation vom Oracle
2. **Mindest-Reputation**: Lehnt Transfer ab wenn < 30
3. **Balance prüfen**: Standard ERC-20 Check
4. **Limit prüfen**: Reputation-basiertes Maximum
5. **Transfer**: Aktualisiert Balances
6. **Event**: Emittiert Transfer-Event

### 4. Reputation-Integration

```solidity
function _getReputation(address account) internal view returns (uint8) {
    (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
        abi.encodeWithSignature("getReputation(address)", account)
    );
    
    if (!success || data.length == 0) {
        return 50; // Default für Demo
    }
    
    return abi.decode(data, (uint8));
}
```

**Erklärung**:
- `staticcall`: Read-only Aufruf (kein Gas für State-Änderung)
- `abi.encodeWithSignature`: Encodiert Funktionsaufruf
- Fallback auf 50 wenn Oracle nicht verfügbar (Demo-Modus)

### 5. Reputation-basierte Limits

```solidity
function maxTransferAmount(address account) public view returns (uint256) {
    uint8 rep = _getReputation(account);
    
    if (rep >= 90) return type(uint256).max;  // Unbegrenzt
    if (rep >= 70) return 100000 * 10**decimals;  // 100k Token
    if (rep >= 50) return 10000 * 10**decimals;   // 10k Token
    if (rep >= 30) return 1000 * 10**decimals;    // 1k Token
    return 0;  // Kein Transfer
}
```

**Reputation-Stufen**:

| Reputation | Max Transfer |
|------------|--------------|
| 90-100 | Unbegrenzt |
| 70-89 | 100,000 Token |
| 50-69 | 10,000 Token |
| 30-49 | 1,000 Token |
| 0-29 | 0 (gesperrt) |

---

## Reputation-Integration

### Wie funktioniert das Web-of-Trust?

Cascoin's Web-of-Trust System berechnet Reputation basierend auf:

1. **Trust-Beziehungen**: Wer vertraut wem?
2. **Bonded Votes**: Reputation-Votes mit CAS-Stake
3. **Behavior Metrics**: On-Chain Verhalten
4. **Graph Analysis**: Position im Trust-Netzwerk

### Reputation im Contract nutzen

```solidity
// Reputation abfragen
uint8 rep = _getReputation(someAddress);

// Entscheidungen basierend auf Reputation
if (rep >= 80) {
    // Privilegierte Aktion
}

// Dynamische Limits
uint256 limit = rep * 1000 * 10**decimals;
```

### Eigene Trust-Checks implementieren

```solidity
// Beispiel: Nur vertrauenswürdige Adressen können minten
function trustedMint(address to, uint256 amount) public {
    require(_getReputation(msg.sender) >= 80, "Not trusted enough");
    require(_getReputation(to) >= 50, "Recipient not trusted");
    
    totalSupply += amount;
    balanceOf[to] += amount;
    emit Transfer(address(0), to, amount);
}
```

---

## Deployment-Prozess

### Voraussetzungen

1. L2 Demo Chain läuft (siehe [L2 Quick Start](01_l2_quickstart.md))
2. Wallet hat ausreichend CAS für Gas

### Schritt 1: Deployment-Skript ausführen

```bash
cd contrib/demos/contracts

# Mit Standardwerten (1M Token)
./deploy_token.sh

# Mit eigener Supply
./deploy_token.sh --supply 5000000

# Mit CVM-Bytecode (ohne solc)
./deploy_token.sh --cvm
```

### Schritt 2: Deployment verifizieren

Das Skript zeigt nach erfolgreichem Deployment:

```
=============================================================================
Deployment abgeschlossen!
=============================================================================

=== CascoinToken Contract ===

  Name:             Cascoin Demo Token
  Symbol:           CDEMO
  Decimals:         18
  Initial Supply:   1000000 CDEMO

  Deployer:         <address>
  Contract Address: <contract_address>
  Transaction:      <tx_hash>
```

### Manuelles Deployment

Falls das Skript nicht funktioniert:

```bash
# 1. Solidity kompilieren (falls solc installiert)
solc --bin --abi CascoinToken.sol -o build/

# 2. Bytecode lesen
BYTECODE=$(cat build/CascoinToken.bin)

# 3. Contract deployen
cascoin-cli -regtest deploycontract $BYTECODE 500000

# 4. Block generieren
cascoin-cli -regtest generatetoaddress 1 $(cascoin-cli -regtest getnewaddress)
```

---

## Token-Interaktion

### Balance abfragen

```bash
cascoin-cli -regtest callcontract <contract_address> "balanceOf(address)" <address>
```

### Token transferieren

```bash
# Via Demo-Skript
./demo_transfer.sh --amount 100

# Manuell
cascoin-cli -regtest callcontract <contract_address> \
    "transfer(address,uint256)" \
    <recipient> \
    <amount_in_wei> \
    --from <sender>
```

### Allowance setzen

```bash
cascoin-cli -regtest callcontract <contract_address> \
    "approve(address,uint256)" \
    <spender> \
    <amount_in_wei> \
    --from <owner>
```

### TransferFrom (delegierter Transfer)

```bash
cascoin-cli -regtest callcontract <contract_address> \
    "transferFrom(address,address,uint256)" \
    <from> \
    <to> \
    <amount_in_wei> \
    --from <spender>
```

### Reputation prüfen

```bash
cascoin-cli -regtest callcontract <contract_address> \
    "getReputation(address)" \
    <address>
```

### Max Transfer Amount

```bash
cascoin-cli -regtest callcontract <contract_address> \
    "maxTransferAmount(address)" \
    <address>
```

---

## CVM-Bytecode Version

Für Umgebungen ohne Solidity-Compiler gibt es eine vereinfachte CVM-Version.

### Storage Layout

```
Key                    | Value
-----------------------|---------------------------
0x00                   | totalSupply
0x01                   | owner
0x10 + address         | balances[address]
0x20 + hash(from,to)   | allowances[from][to]
```

### Deployment

```bash
# CVM-Version deployen
./deploy_token.sh --cvm
```

### Einschränkungen

Die CVM-Version hat einige Einschränkungen:
- Keine Reputation-Integration
- Keine Overflow-Checks
- Vereinfachte Logik

Für Produktion wird die Solidity-Version empfohlen.

---

## Eigenen Token erstellen

### 1. Contract anpassen

```solidity
// Eigene Werte setzen
string public name = "Mein Token";
string public symbol = "MTK";
uint8 public decimals = 8;  // Weniger Dezimalstellen

// Eigene Reputation-Schwelle
uint8 public minTransferReputation = 50;  // Höher
```

### 2. Zusätzliche Features

```solidity
// Burn-Funktion
function burn(uint256 amount) public {
    require(balanceOf[msg.sender] >= amount, "Insufficient balance");
    balanceOf[msg.sender] -= amount;
    totalSupply -= amount;
    emit Transfer(msg.sender, address(0), amount);
}

// Mint-Funktion (nur Owner)
function mint(address to, uint256 amount) public onlyOwner {
    totalSupply += amount;
    balanceOf[to] += amount;
    emit Transfer(address(0), to, amount);
}
```

### 3. Kompilieren und deployen

```bash
# Kompilieren
solc --bin --optimize MeinToken.sol -o build/

# Deployen
cascoin-cli -regtest deploycontract $(cat build/MeinToken.bin) 500000
```

---

## Best Practices

### Sicherheit

1. **Overflow-Schutz**: Solidity 0.8+ hat eingebauten Schutz
2. **Reentrancy**: Keine externen Calls vor State-Updates
3. **Access Control**: `onlyOwner` Modifier für privilegierte Funktionen

### Gas-Optimierung

1. **Storage minimieren**: Storage-Writes sind teuer (5000 Gas)
2. **Events nutzen**: Günstiger als Storage für Logs
3. **Batch-Operationen**: Mehrere Transfers in einer TX

### Testing

1. **Regtest zuerst**: Immer in Regtest testen
2. **Edge Cases**: Leere Balances, Max-Werte, Zero-Transfers
3. **Reputation-Szenarien**: Verschiedene Reputation-Levels testen

---

## Nächste Schritte

- [CVM Developer Guide](../../../doc/CVM_DEVELOPER_GUIDE.md) - Fortgeschrittene Contract-Entwicklung
- [L2 Developer Guide](../../../doc/L2_DEVELOPER_GUIDE.md) - L2-spezifische Features
- [Contract README](../contracts/README.md) - API-Referenz
