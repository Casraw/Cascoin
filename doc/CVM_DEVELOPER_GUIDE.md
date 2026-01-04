# Cascoin CVM Developer Guide

This guide covers deploying and interacting with smart contracts on Cascoin's CVM (Cascoin Virtual Machine), which provides full EVM compatibility while adding unique trust-aware features.

---

## Table of Contents

1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Deploying Contracts](#deploying-contracts)
4. [Calling Contracts](#calling-contracts)
5. [Trust-Aware Features](#trust-aware-features)
6. [Gas System](#gas-system)
7. [RPC Reference](#rpc-reference)
8. [Developer Tools](#developer-tools)
9. [Examples](#examples)
10. [Troubleshooting](#troubleshooting)

---

## Overview

Cascoin's Enhanced CVM (Cascoin Virtual Machine) provides full EVM compatibility while adding unique trust-aware features:

- **Full EVM Compatibility**: Deploy standard Solidity contracts without modification
- **Trust Integration**: Automatic reputation context injection for all operations
- **Sustainable Gas**: 100x lower base costs than Ethereum with reputation-based discounts
- **Free Gas**: Addresses with 80+ reputation get free gas allowances
- **HAT v2 Consensus**: Distributed reputation verification prevents fraud

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Enhanced CVM Engine                       │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐    ┌─────────────────────────────────┐ │
│  │   Bytecode      │    │   Trust Context Manager         │ │
│  │   Dispatcher    │◄──►│   - Reputation Injection        │ │
│  │                 │    │   - Trust Score Caching         │ │
│  └─────────────────┘    └─────────────────────────────────┘ │
│           │                                                  │
│           ▼                                                  │
│  ┌─────────────────┐    ┌─────────────────────────────────┐ │
│  │  CVM Engine     │    │       EVM Engine                │ │
│  │  (Register)     │    │       (Stack)                   │ │
│  │  - 40+ opcodes  │    │       - 140+ opcodes            │ │
│  └─────────────────┘    └─────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Quick Start

### Prerequisites

- Cascoin Core node running (testnet recommended for development)
- Solidity compiler (solc) or Remix IDE
- Basic understanding of Ethereum smart contracts

### Start Your Node

```bash
# Start daemon in testnet mode (ALWAYS use testnet for development)
./src/cascoind -testnet -daemon

# Verify node is running
./src/cascoin-cli -testnet getblockchaininfo
```

### Deploy Your First Contract

```bash
# Simple storage contract
cat > SimpleStorage.sol << 'EOF'
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract SimpleStorage {
    uint256 private value;
    
    function set(uint256 _value) public {
        value = _value;
    }
    
    function get() public view returns (uint256) {
        return value;
    }
}
EOF

# Compile with solc
solc --bin --abi SimpleStorage.sol -o build/

# Deploy using RPC
./src/cascoin-cli -testnet cas_sendTransaction '{
    "from": "YOUR_ADDRESS",
    "data": "0x<BYTECODE_FROM_BUILD>",
    "gas": "0x100000"
}'
```

---

## Deploying Contracts

### Transaction Format

Contract deployments use the standard Ethereum transaction format with Cascoin extensions:

```json
{
    "from": "0x...",           // Deployer address
    "data": "0x...",           // Contract bytecode + constructor args
    "gas": "0x100000",         // Gas limit (hex)
    "gasPrice": "0x...",       // Optional: gas price (auto-calculated if omitted)
    "value": "0x0",            // Optional: CAS to send to contract
    "nonce": "0x0"             // Optional: transaction nonce (auto-calculated)
}
```

### Using RPC

```bash
# Deploy contract
TXHASH=$(./src/cascoin-cli -testnet cas_sendTransaction '{
    "from": "CsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
    "data": "0x608060405234801561001057600080fd5b50...",
    "gas": "0x100000"
}')

# Get receipt (includes contract address)
./src/cascoin-cli -testnet cas_getTransactionReceipt "$TXHASH"
```

### Response

```json
{
    "transactionHash": "0x...",
    "contractAddress": "0x...",
    "gasUsed": "0x...",
    "status": "0x1",
    "logs": []
}
```

### Bytecode Format Detection

Cascoin automatically detects bytecode format:

- **EVM Bytecode**: Standard Solidity compiler output (detected by PUSH patterns)
- **CVM Bytecode**: Register-based Cascoin VM format
- **AUTO**: System auto-detects and routes to appropriate engine

---

## Calling Contracts

### Read-Only Calls (No Gas)

```bash
# Call view/pure function (free, no transaction)
./src/cascoin-cli -testnet cas_call '{
    "to": "0xCONTRACT_ADDRESS",
    "data": "0x6d4ce63c"
}' "latest"
```

### State-Changing Calls

```bash
# Call function that modifies state (requires gas)
./src/cascoin-cli -testnet cas_sendTransaction '{
    "from": "YOUR_ADDRESS",
    "to": "0xCONTRACT_ADDRESS",
    "data": "0x60fe47b10000000000000000000000000000000000000000000000000000000000000042",
    "gas": "0x50000"
}'
```

### ABI Encoding

Function calls use standard Ethereum ABI encoding:

```
Function: set(uint256)
Selector: 0x60fe47b1 (first 4 bytes of keccak256("set(uint256)"))
Parameter: 0x0000000000000000000000000000000000000000000000000000000000000042 (value 66)
Full data: 0x60fe47b10000000000000000000000000000000000000000000000000000000000000042
```

---

## Trust-Aware Features

Cascoin's unique value proposition is automatic trust integration. Every contract operation has access to reputation data.

### Automatic Trust Context

When any contract function executes, the CVM automatically injects:

- **Caller Reputation**: HAT v2 score (0-100) of the transaction sender
- **Contract Reputation**: Trust score of the contract itself
- **Trust Paths**: Web-of-Trust connections between parties

### Reputation-Based Gas Discounts

| Reputation Score | Gas Discount |
|-----------------|--------------|
| 90-100          | 50% off      |
| 80-89           | 40% off      |
| 70-79           | 30% off      |
| 60-69           | 20% off      |
| 50-59           | 10% off      |
| 0-49            | No discount  |

### Free Gas Eligibility

Addresses with reputation ≥ 80 receive free gas allowances:

```bash
# Check free gas allowance
./src/cascoin-cli -testnet cas_getFreeGasAllowance "YOUR_ADDRESS"
```

Response:
```json
{
    "address": "CsXXXX...",
    "reputation": 85,
    "eligible": true,
    "dailyAllowance": 1000000,
    "remaining": 750000,
    "resetsAt": 1735084800
}
```

### Querying Reputation

```bash
# Get reputation score
./src/cascoin-cli -testnet getsecuretrust "ADDRESS"

# Get reputation discount for gas
./src/cascoin-cli -testnet cas_getReputationDiscount "ADDRESS"
```

### Trust-Gated Functions

Contracts can implement reputation requirements:

```solidity
// Example: Require minimum reputation for sensitive operations
contract TrustGated {
    // Cascoin precompile for reputation queries
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    modifier requireReputation(uint8 minScore) {
        (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
            abi.encodeWithSignature("getReputation(address)", msg.sender)
        );
        require(success, "Reputation query failed");
        uint8 score = abi.decode(data, (uint8));
        require(score >= minScore, "Insufficient reputation");
        _;
    }
    
    function sensitiveOperation() external requireReputation(70) {
        // Only callable by addresses with reputation >= 70
    }
}
```

---

## Gas System

### Sustainable Gas Pricing

Cascoin's gas system is designed for sustainability:

- **Base Cost**: 100x lower than Ethereum mainnet
- **Max Variation**: 2x during peak usage (vs 100x+ on Ethereum)
- **Reputation Discounts**: Up to 50% off for high-reputation addresses
- **Free Gas**: Available for 80+ reputation addresses

### Gas Costs

| Operation | Cascoin Gas | Ethereum Gas | Savings |
|-----------|-------------|--------------|---------|
| Transfer  | 210         | 21,000       | 100x    |
| SSTORE    | 50          | 5,000        | 100x    |
| SLOAD     | 2           | 200          | 100x    |
| CREATE    | 320         | 32,000       | 100x    |

### Estimating Gas

```bash
# Estimate gas for transaction
./src/cascoin-cli -testnet cas_estimateGas '{
    "from": "YOUR_ADDRESS",
    "to": "CONTRACT_ADDRESS",
    "data": "0x..."
}'
```

### Gas Price

```bash
# Get current gas price
./src/cascoin-cli -testnet cas_gasPrice
```

---

## RPC Reference

### Primary Methods (cas_*)

| Method | Description |
|--------|-------------|
| `cas_sendTransaction` | Send transaction (deploy or call) |
| `cas_call` | Execute read-only call |
| `cas_estimateGas` | Estimate gas for transaction |
| `cas_getCode` | Get contract bytecode |
| `cas_getStorageAt` | Get storage value |
| `cas_getTransactionReceipt` | Get transaction receipt |
| `cas_blockNumber` | Get current block number |
| `cas_getBalance` | Get address balance |
| `cas_getTransactionCount` | Get nonce |
| `cas_gasPrice` | Get current gas price |

### Ethereum-Compatible Aliases (eth_*)

All `cas_*` methods have `eth_*` aliases for tool compatibility:

```bash
# These are equivalent:
./src/cascoin-cli -testnet cas_blockNumber
./src/cascoin-cli -testnet eth_blockNumber
```

### Trust-Aware Methods

| Method | Description |
|--------|-------------|
| `cas_getReputationDiscount` | Get gas discount for address |
| `cas_getFreeGasAllowance` | Get free gas allowance |
| `getsecuretrust` | Get HAT v2 trust score |
| `gettrustbreakdown` | Get detailed trust components |

### Developer Tools

| Method | Description |
|--------|-------------|
| `debug_traceTransaction` | Trace transaction execution |
| `debug_traceCall` | Trace simulated call |
| `cas_snapshot` | Create state snapshot |
| `cas_revert` | Revert to snapshot |
| `cas_mine` | Mine blocks (regtest only) |

---

## Developer Tools

### Hardhat Integration

Cascoin is compatible with Hardhat. Configure your `hardhat.config.js`:

```javascript
module.exports = {
  networks: {
    cascoin_testnet: {
      url: "http://localhost:18332",  // Testnet RPC port
      accounts: ["YOUR_PRIVATE_KEY"],
      chainId: 12345  // Cascoin testnet chain ID
    }
  },
  solidity: "0.8.19"
};
```

### Foundry Integration

Configure `foundry.toml`:

```toml
[profile.default]
src = "src"
out = "out"
libs = ["lib"]

[rpc_endpoints]
cascoin_testnet = "http://localhost:18332"
```

### Remix IDE

1. Install MetaMask or similar wallet
2. Add Cascoin testnet as custom network:
   - RPC URL: `http://localhost:18332`
   - Chain ID: `12345`
   - Currency: `CAS`
3. Deploy contracts normally through Remix

### Web Dashboard

Access the built-in web dashboard at `http://localhost:8332/dashboard` for:

- Contract deployment interface
- Contract interaction
- Gas estimation
- Reputation display
- Transaction history

---

## Examples

### ERC-20 Token with Trust Features

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/token/ERC20/ERC20.sol";

contract TrustToken is ERC20 {
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    constructor() ERC20("TrustToken", "TRUST") {
        _mint(msg.sender, 1000000 * 10**18);
    }
    
    // Transfers require minimum reputation
    function transfer(address to, uint256 amount) public override returns (bool) {
        require(_getReputation(msg.sender) >= 30, "Sender reputation too low");
        return super.transfer(to, amount);
    }
    
    // Higher reputation = higher transfer limits
    function maxTransfer(address account) public view returns (uint256) {
        uint8 rep = _getReputation(account);
        if (rep >= 90) return type(uint256).max;
        if (rep >= 70) return 100000 * 10**18;
        if (rep >= 50) return 10000 * 10**18;
        return 1000 * 10**18;
    }
    
    function _getReputation(address account) internal view returns (uint8) {
        (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
            abi.encodeWithSignature("getReputation(address)", account)
        );
        if (!success) return 0;
        return abi.decode(data, (uint8));
    }
}
```

### Reputation-Gated Marketplace

```solidity
// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract TrustMarketplace {
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    struct Listing {
        address seller;
        uint256 price;
        bool active;
    }
    
    mapping(uint256 => Listing) public listings;
    uint256 public nextListingId;
    
    // Only 50+ reputation can list items
    function createListing(uint256 price) external returns (uint256) {
        require(_getReputation(msg.sender) >= 50, "Reputation too low to sell");
        
        uint256 id = nextListingId++;
        listings[id] = Listing(msg.sender, price, true);
        return id;
    }
    
    // Buyers need 30+ reputation
    function purchase(uint256 listingId) external payable {
        require(_getReputation(msg.sender) >= 30, "Reputation too low to buy");
        
        Listing storage listing = listings[listingId];
        require(listing.active, "Listing not active");
        require(msg.value >= listing.price, "Insufficient payment");
        
        listing.active = false;
        payable(listing.seller).transfer(listing.price);
        
        // Refund excess
        if (msg.value > listing.price) {
            payable(msg.sender).transfer(msg.value - listing.price);
        }
    }
    
    function _getReputation(address account) internal view returns (uint8) {
        (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
            abi.encodeWithSignature("getReputation(address)", account)
        );
        if (!success) return 0;
        return abi.decode(data, (uint8));
    }
}
```

---

## Troubleshooting

### Common Issues

**"Insufficient gas"**
- Increase gas limit in transaction
- Check if you have free gas allowance remaining
- Verify your reputation for discounts

**"Contract deployment failed"**
- Verify bytecode is valid EVM bytecode
- Check constructor parameters are correctly encoded
- Ensure sufficient balance for gas

**"Transaction not found"**
- Wait for block confirmation
- Check transaction hash is correct
- Verify node is synced

**"Reputation query failed"**
- Ensure reputation oracle address is correct
- Check if address has any reputation history
- Verify node has trust graph data

### Debug Tools

```bash
# Trace failed transaction
./src/cascoin-cli -testnet debug_traceTransaction "TXHASH"

# Check contract code
./src/cascoin-cli -testnet cas_getCode "CONTRACT_ADDRESS"

# Verify contract storage
./src/cascoin-cli -testnet cas_getStorageAt "CONTRACT_ADDRESS" "0x0"
```

### Getting Help

- Read [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md) for reputation system details
- Check [CASCOIN_DOCS.md](CASCOIN_DOCS.md) for documentation index
- Review source code in `src/cvm/` for implementation details

---

## Next Steps

- [Blockchain Integration Guide](CVM_BLOCKCHAIN_INTEGRATION.md) - Transaction format and mempool details
- [Operator Guide](CVM_OPERATOR_GUIDE.md) - Node configuration and activation
- [Security Guide](CVM_SECURITY_GUIDE.md) - HAT v2 consensus and validator operations

---

**Last Updated:** December 2025  
**Version:** 1.0  
**Status:** Production Ready ✅
