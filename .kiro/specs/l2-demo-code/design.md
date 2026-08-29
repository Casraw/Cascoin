# Design Document: L2 Demo Code

## Overview

Dieses Design beschreibt die Implementierung von Demo-Code für Cascoin L2 und Smart Contracts. Die Demos ermöglichen Entwicklern einen schnellen Einstieg in die L2-Entwicklung mit praktischen, lauffähigen Beispielen.

Die Demo-Suite besteht aus:
1. **L2 Demo Chain** - Vollständiges Setup einer L2-Chain im Regtest-Modus
2. **CascoinToken Contract** - ERC-20-kompatibler Token mit Trust-Features
3. **Automatisierte Skripte** - Bash-Skripte für Setup, Deployment und Testing
4. **Dokumentation** - Tutorials und Referenzdokumentation

## Architecture

```
contrib/demos/
├── README.md                    # Übersicht und Quick Start
├── l2-chain/
│   ├── setup_l2_demo.sh        # L2 Chain Setup Skript
│   ├── cleanup.sh              # Cleanup Skript
│   └── config/
│       └── regtest.conf        # Regtest Konfiguration
├── contracts/
│   ├── CascoinToken.sol        # ERC-20 Token Contract (Solidity)
│   ├── CascoinToken.cvm        # CVM Bytecode Version
│   ├── deploy_token.sh         # Deployment Skript
│   └── README.md               # Contract Dokumentation
└── tutorials/
    ├── 01_l2_quickstart.md     # L2 Quick Start Tutorial
    ├── 02_token_creation.md    # Token Creation Tutorial
    └── 03_cross_layer.md       # Cross-Layer Messaging Tutorial
```

## Components and Interfaces

### L2 Demo Chain Setup

Das Setup-Skript `setup_l2_demo.sh` führt folgende Schritte aus:

```bash
#!/bin/bash
# 1. Start Regtest Node
cascoind -regtest -daemon -l2=1

# 2. Generate initial blocks
cascoin-cli -regtest generate 101

# 3. Create L2 address
L2_ADDR=$(cascoin-cli -regtest getnewaddress)

# 4. Register as Sequencer
cascoin-cli -regtest l2_announcesequencer 10 50

# 5. Deposit CAS to L2
cascoin-cli -regtest l2_deposit "$L2_ADDR" 1000

# 6. Verify L2 is running
cascoin-cli -regtest l2_getchaininfo
```

### CascoinToken Contract (Solidity)

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

/**
 * @title CascoinToken
 * @dev ERC-20 Token mit Cascoin Trust-Integration
 * 
 * Features:
 * - Standard ERC-20 Funktionalität
 * - Reputation-basierte Transfer-Limits
 * - Trust-gated Minting für hohe Reputation
 */
contract CascoinToken {
    // Cascoin Reputation Oracle Precompile
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    string public name = "Cascoin Demo Token";
    string public symbol = "CDEMO";
    uint8 public decimals = 18;
    uint256 public totalSupply;
    
    // Minimum Reputation für Transfers
    uint8 public minTransferReputation = 30;
    
    mapping(address => uint256) public balanceOf;
    mapping(address => mapping(address => uint256)) public allowance;
    
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
    
    constructor(uint256 initialSupply) {
        totalSupply = initialSupply * 10**decimals;
        balanceOf[msg.sender] = totalSupply;
        emit Transfer(address(0), msg.sender, totalSupply);
    }
    
    /**
     * @dev Transfer mit Reputation-Check
     */
    function transfer(address to, uint256 amount) public returns (bool) {
        require(_getReputation(msg.sender) >= minTransferReputation, 
                "Sender reputation too low");
        require(balanceOf[msg.sender] >= amount, "Insufficient balance");
        
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        emit Transfer(msg.sender, to, amount);
        return true;
    }
    
    function approve(address spender, uint256 amount) public returns (bool) {
        allowance[msg.sender][spender] = amount;
        emit Approval(msg.sender, spender, amount);
        return true;
    }
    
    function transferFrom(address from, address to, uint256 amount) public returns (bool) {
        require(_getReputation(from) >= minTransferReputation,
                "Sender reputation too low");
        require(balanceOf[from] >= amount, "Insufficient balance");
        require(allowance[from][msg.sender] >= amount, "Insufficient allowance");
        
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        allowance[from][msg.sender] -= amount;
        emit Transfer(from, to, amount);
        return true;
    }
    
    /**
     * @dev Reputation-basiertes Transfer-Limit
     */
    function maxTransferAmount(address account) public view returns (uint256) {
        uint8 rep = _getReputation(account);
        if (rep >= 90) return type(uint256).max;
        if (rep >= 70) return 100000 * 10**decimals;
        if (rep >= 50) return 10000 * 10**decimals;
        if (rep >= 30) return 1000 * 10**decimals;
        return 0; // Unter 30 kein Transfer erlaubt
    }
    
    /**
     * @dev Interne Funktion zum Abrufen der Reputation
     */
    function _getReputation(address account) internal view returns (uint8) {
        (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
            abi.encodeWithSignature("getReputation(address)", account)
        );
        if (!success || data.length == 0) return 0;
        return abi.decode(data, (uint8));
    }
}
```

### CVM Bytecode Version

Für native CVM-Ausführung wird eine vereinfachte Version bereitgestellt:

```
# CascoinToken.cvm - Simplified CVM Token
# Storage Layout:
# 0x00: totalSupply
# 0x01: owner
# 0x10+addr: balances[addr]

# Constructor: Mint initial supply to deployer
CALLER              # Get deployer address
PUSH 1 0x01         # Storage key for owner
SSTORE              # Store owner

PUSH 8 0x00000000000F4240  # 1,000,000 tokens
PUSH 1 0x00         # Storage key for totalSupply
SSTORE              # Store totalSupply

CALLER              # Get deployer address
PUSH 1 0x10         # Balance storage prefix
ADD                 # Calculate balance key
PUSH 8 0x00000000000F4240  # Initial balance
SWAP                # Swap for SSTORE order
SSTORE              # Store balance

STOP
```

### Deployment Script

```bash
#!/bin/bash
# deploy_token.sh - Deploy CascoinToken to L2

set -e

# Configuration
INITIAL_SUPPLY=1000000
GAS_LIMIT=500000

echo "=== CascoinToken Deployment ==="

# Get deployer address
DEPLOYER=$(cascoin-cli -regtest getnewaddress)
echo "Deployer: $DEPLOYER"

# Compile contract (requires solc)
echo "Compiling contract..."
BYTECODE=$(solc --bin contracts/CascoinToken.sol 2>/dev/null | tail -1)

# Deploy via RPC
echo "Deploying to L2..."
TX_HASH=$(cascoin-cli -regtest cas_sendTransaction "{
    \"from\": \"$DEPLOYER\",
    \"data\": \"0x${BYTECODE}\",
    \"gas\": \"0x$(printf '%x' $GAS_LIMIT)\"
}")

echo "Transaction: $TX_HASH"

# Wait for confirmation
sleep 2
cascoin-cli -regtest generate 1

# Get contract address
RECEIPT=$(cascoin-cli -regtest cas_getTransactionReceipt "$TX_HASH")
CONTRACT=$(echo "$RECEIPT" | jq -r '.contractAddress')

echo "=== Deployment Complete ==="
echo "Contract Address: $CONTRACT"
echo "Initial Supply: $INITIAL_SUPPLY CDEMO"
```

## Data Models

### Token State

```cpp
struct TokenState {
    std::string name;
    std::string symbol;
    uint8_t decimals;
    uint256_t totalSupply;
    uint8_t minTransferReputation;
    std::map<uint160, uint256_t> balances;
    std::map<uint160, std::map<uint160, uint256_t>> allowances;
};
```

### Demo Configuration

```cpp
struct DemoConfig {
    std::string network;           // "regtest"
    uint64_t initialBlocks;        // 101
    CAmount sequencerStake;        // 10 CAS
    uint8_t sequencerHatScore;     // 50
    CAmount l2DepositAmount;       // 1000 CAS
    uint256_t tokenInitialSupply;  // 1,000,000
};
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

### Property 1: Token Transfer Balance Conservation

*For any* token transfer between two addresses, the sum of sender and receiver balances before the transfer SHALL equal the sum after the transfer (conservation of tokens).

**Validates: Requirements 2.1**

### Property 2: Reputation-Based Transfer Rejection

*For any* address with reputation below the minimum threshold (30), all transfer attempts SHALL be rejected and balances SHALL remain unchanged.

**Validates: Requirements 2.2, 2.5**

### Property 3: Initial Supply Consistency

*For any* newly deployed token contract, the deployer's balance SHALL equal the total supply, and all other balances SHALL be zero.

**Validates: Requirements 2.3**

## Error Handling

### Script Error Handling

```bash
# Alle Skripte verwenden set -e für Fehlerabbruch
set -e

# Fehlerbehandlung mit trap
trap 'echo "Error on line $LINENO"; cleanup' ERR

# Prüfung auf laufenden Node
check_node() {
    if ! cascoin-cli -regtest getblockchaininfo &>/dev/null; then
        echo "ERROR: Cascoin node not running"
        echo "Start with: cascoind -regtest -daemon -l2=1"
        exit 1
    fi
}

# Prüfung auf L2 Aktivierung
check_l2() {
    if ! cascoin-cli -regtest l2_getchaininfo &>/dev/null; then
        echo "ERROR: L2 not enabled"
        echo "Ensure node started with -l2=1"
        exit 1
    fi
}
```

### Contract Error Handling

```solidity
// Revert mit aussagekräftigen Fehlermeldungen
require(balanceOf[msg.sender] >= amount, "CascoinToken: insufficient balance");
require(allowance[from][msg.sender] >= amount, "CascoinToken: insufficient allowance");
require(_getReputation(msg.sender) >= minTransferReputation, 
        "CascoinToken: sender reputation too low");
```

## Testing Strategy

### Unit Tests

Unit Tests validieren spezifische Funktionen:

1. **Token Balance Tests**
   - Test: Deployer erhält initial supply
   - Test: Transfer aktualisiert Balances korrekt
   - Test: Approve/TransferFrom funktioniert

2. **Reputation Integration Tests**
   - Test: Transfer mit niedriger Reputation wird abgelehnt
   - Test: Transfer mit ausreichender Reputation funktioniert
   - Test: maxTransferAmount gibt korrekte Limits zurück

3. **Script Tests**
   - Test: Setup-Skript startet L2 erfolgreich
   - Test: Cleanup entfernt alle Regtest-Daten
   - Test: Deployment-Skript deployed Contract

### Property-Based Tests

Property-Based Tests verwenden die fast-check Bibliothek (oder ähnlich) um universelle Eigenschaften zu validieren:

```javascript
// Property 1: Balance Conservation
fc.assert(
  fc.property(
    fc.record({
      sender: fc.hexaString({minLength: 40, maxLength: 40}),
      receiver: fc.hexaString({minLength: 40, maxLength: 40}),
      amount: fc.bigUint()
    }),
    ({sender, receiver, amount}) => {
      const beforeSum = getBalance(sender) + getBalance(receiver);
      transfer(sender, receiver, amount);
      const afterSum = getBalance(sender) + getBalance(receiver);
      return beforeSum === afterSum;
    }
  )
);
```

**Konfiguration:**
- Minimum 100 Iterationen pro Property Test
- Tag-Format: **Feature: l2-demo-code, Property N: [property_text]**
