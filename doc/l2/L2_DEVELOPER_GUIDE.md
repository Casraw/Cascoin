# Cascoin L2 Developer Guide

This guide provides comprehensive documentation for developers building applications on Cascoin Layer 2, including RPC API reference, contract deployment, cross-layer messaging, and integration patterns.

## Table of Contents

1. [Overview](#overview)
2. [Getting Started](#getting-started)
3. [RPC API Reference](#rpc-api-reference)
4. [Contract Deployment](#contract-deployment)
5. [Cross-Layer Messaging](#cross-layer-messaging)
6. [Burn-and-Mint Token Model](#burn-and-mint-token-model)
7. [Transaction Types](#transaction-types)
8. [MEV Protection](#mev-protection)
9. [Reputation Integration](#reputation-integration)
10. [Error Handling](#error-handling)
11. [Best Practices](#best-practices)

---

## Overview

Cascoin L2 is a native Layer 2 scaling solution that provides:

- **High throughput**: 1000+ TPS with 500ms block times
- **Low fees**: 90%+ reduction compared to L1
- **CVM compatibility**: Full Cascoin Virtual Machine support
- **Reputation integration**: HAT v2 trust scores for enhanced features
- **MEV protection**: Encrypted mempool with threshold decryption

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Your Application                        │
├─────────────────────────────────────────────────────────────┤
│                      L2 RPC Interface                       │
│  l2_getbalance | l2_createburntx | l2_getburnstatus | ...   │
├─────────────────────────────────────────────────────────────┤
│                    Cascoin L2 Layer                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ Burn-Mint   │  │  Sequencer  │  │  Cross-Layer Msg    │ │
│  │   System    │  │  Consensus  │  │     System          │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    Cascoin L1 (Mainchain)                   │
│              OP_RETURN Burn Transactions                    │
└─────────────────────────────────────────────────────────────┘
```

### Token Model: Burn-and-Mint

Cascoin L2 uses a **Burn-and-Mint** token model for secure cross-layer value transfer:

- **Burn on L1**: CAS is permanently destroyed via OP_RETURN transactions
- **Mint on L2**: Equivalent L2 tokens are minted after 2/3 sequencer consensus
- **1:1 Backing**: L2 token supply always equals total burned CAS
- **Cryptographically Secure**: OP_RETURN outputs are provably unspendable

This model eliminates the security risks of traditional bridge contracts.

---

## Getting Started

### Prerequisites

- Cascoin node running with L2 enabled (`-l2=1`)
- Node fully synced with L1 blockchain
- RPC access configured

### Quick Start

```bash
# Check L2 status (uses default chain or -l2chainid setting)
cascoin-cli l2_getchaininfo

# List all available L2 chains
cascoin-cli l2_listchains

# Get info for a specific chain
cascoin-cli l2_getchaininfo 1001

# Get L2 balance
cascoin-cli l2_getbalance "0xYourAddress"

# Create a burn transaction to get L2 tokens
cascoin-cli l2_createburntx 100 "0xYourL2Address"

# Send the burn transaction to L1
cascoin-cli l2_sendburntx "<signed_tx_hex>"

# Check burn status and consensus progress
cascoin-cli l2_getburnstatus "<l1_tx_hash>"

# Get total L2 token supply
cascoin-cli l2_gettotalsupply

# Verify supply invariant
cascoin-cli l2_verifysupply
```

**Important:** When running a node, you connect to a specific L2 chain via the `-l2chainid` parameter. All operations without explicit chain ID use this configured chain.

### Connecting via JSON-RPC

```python
import requests
import json

def rpc_call(method, params=[]):
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "params": params
    }
    response = requests.post(
        "http://localhost:8332",
        auth=("rpcuser", "rpcpassword"),
        json=payload
    )
    return response.json()

# Example: Get L2 balance
result = rpc_call("l2_getbalance", ["0xa1b2c3d4e5f6..."])
print(f"Balance: {result['result']['balance_cas']} CAS")
```

---

## RPC API Reference

### Chain Information

#### l2_getchaininfo

Get information about an L2 chain. Each L2 chain has a unique chain ID.

```bash
# Get info for current chain (configured via -l2chainid)
cascoin-cli l2_getchaininfo

# Get info for a specific chain by ID
cascoin-cli l2_getchaininfo 1001

# Get info for a specific chain by name
cascoin-cli l2_getregisteredchain "MyL2Chain"
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| chainid | numeric | No | L2 chain ID. If omitted, uses the chain configured at node startup via `-l2chainid` (default: 1) |

**Note:** Multiple L2 chains can exist. Use `l2_listchains` to see all available chains, then query specific chains by their ID.

**Response:**
```json
{
  "chainId": 1,
  "name": "default",
  "enabled": true,
  "mode": "FULL_NODE",
  "blockHeight": 12345,
  "stateRoot": "0xabc123...",
  "accountCount": 1000,
  "sequencerCount": 5,
  "eligibleSequencers": 3,
  "params": {
    "minSequencerHATScore": 70,
    "minSequencerStake": "100.00000000",
    "blocksPerLeader": 10,
    "leaderTimeoutSeconds": 3,
    "targetBlockTimeMs": 500,
    "maxBlockGas": 30000000,
    "standardChallengePeriod": 604800,
    "fastChallengePeriod": 86400,
    "fastWithdrawalHATThreshold": 80,
    "consensusThresholdPercent": 67
  }
}
```

#### l2_listchains

List all registered L2 chains.

```bash
cascoin-cli l2_listchains
```

**Response:**
```json
[
  {
    "chainId": 1,
    "name": "default"
  },
  {
    "chainId": 1001,
    "name": "MyL2Chain"
  }
]
```

#### l2_getblockbynumber

Get an L2 block by its number.

```bash
cascoin-cli l2_getblockbynumber <blocknumber> [verbose]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| blocknumber | numeric | Yes | L2 block number |
| verbose | boolean | No | Include full transaction data (default: true) |

**Response:**
```json
{
  "number": 100,
  "hash": "0xdef456...",
  "parentHash": "0xabc123...",
  "stateRoot": "0x789xyz...",
  "transactionsRoot": "0x...",
  "receiptsRoot": "0x...",
  "sequencer": "0xseq123...",
  "timestamp": 1704067200,
  "gasLimit": 30000000,
  "gasUsed": 1500000,
  "l2ChainId": 1,
  "l1AnchorBlock": 500000,
  "l1AnchorHash": "0x...",
  "slotNumber": 10,
  "transactionCount": 50,
  "signatureCount": 3,
  "isFinalized": true,
  "transactions": [...]
}
```

### Account Operations

#### l2_getbalance

Get the L2 balance for an address.

```bash
cascoin-cli l2_getbalance "address"
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| address | string | Yes | L2 address (hex with 0x prefix or base58) |

**Response:**
```json
{
  "address": "0xa1b2c3d4e5f6...",
  "balance": 10000000000,
  "balance_cas": "100.00000000",
  "nonce": 5
}
```

#### l2_gettransactioncount

Get the transaction count (nonce) for an L2 address.

```bash
cascoin-cli l2_gettransactioncount "address"
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| address | string | Yes | L2 address |

**Response:**
```
5
```

### Deployment Operations

#### l2_deploy

Deploy a new L2 chain instance.

```bash
cascoin-cli l2_deploy "name" [blocktime] [gaslimit] [challengeperiod]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| name | string | Yes | Name for the L2 chain |
| blocktime | numeric | No | Target block time in ms (default: 500) |
| gaslimit | numeric | No | Max gas per block (default: 30000000) |
| challengeperiod | numeric | No | Challenge period in seconds (default: 604800) |

**Response:**
```json
{
  "chainId": 1001,
  "name": "MyL2Chain",
  "blockTime": 500,
  "gasLimit": 30000000,
  "challengePeriod": 604800,
  "genesisHash": "0x...",
  "status": "deployed"
}
```

#### l2_registerchain

Register a new L2 chain in the global registry with stake.

```bash
cascoin-cli l2_registerchain "name" stake [blocktime] [gaslimit] [challengeperiod] [minseqstake] [minseqhatscore]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| name | string | Yes | Chain name |
| stake | numeric | Yes | Deployer stake in CAS (min 1000) |
| blocktime | numeric | No | Target block time in ms |
| gaslimit | numeric | No | Max gas per block |
| challengeperiod | numeric | No | Challenge period in seconds |
| minseqstake | numeric | No | Min sequencer stake in CAS |
| minseqhatscore | numeric | No | Min sequencer HAT score |

**Response:**
```json
{
  "success": true,
  "chainId": 1002,
  "name": "MyChain",
  "deployer": "0x...",
  "stake": "1000.00000000",
  "status": "BOOTSTRAPPING",
  "deploymentBlock": 500100,
  "params": {...},
  "message": "L2 chain registered successfully"
}
```

### Sequencer Operations

#### l2_announcesequencer

Announce this node as an L2 sequencer candidate.

```bash
cascoin-cli l2_announcesequencer stake hatscore ["endpoint"]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| stake | numeric | Yes | Stake amount in CAS |
| hatscore | numeric | Yes | HAT v2 score (0-100) |
| endpoint | string | No | Public endpoint for connectivity |

**Response:**
```json
{
  "success": true,
  "address": "0xseq123...",
  "stake": "100.00000000",
  "hatScore": 75,
  "eligible": true,
  "message": "Sequencer announcement broadcast successfully"
}
```

#### l2_getsequencers

Get list of known L2 sequencers.

```bash
cascoin-cli l2_getsequencers [eligibleonly]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| eligibleonly | boolean | No | Only show eligible sequencers (default: false) |

**Response:**
```json
[
  {
    "address": "0xseq123...",
    "stake": "150.00000000",
    "hatScore": 85,
    "peerCount": 10,
    "endpoint": "192.168.1.1:8333",
    "isVerified": true,
    "isEligible": true,
    "blocksProduced": 1000,
    "blocksMissed": 5,
    "uptimePercent": 99.5,
    "weight": 12750,
    "lastAnnouncement": 1704067200,
    "attestationCount": 3
  }
]
```

#### l2_getleader

Get information about the current L2 sequencer leader.

```bash
cascoin-cli l2_getleader
```

**Response:**
```json
{
  "hasLeader": true,
  "address": "0xleader...",
  "slotNumber": 100,
  "validUntilBlock": 1010,
  "electionSeed": "0x...",
  "electionTimestamp": 1704067200,
  "isLocalNode": false,
  "backupCount": 2,
  "failoverInProgress": false,
  "backupSequencers": ["0xbackup1...", "0xbackup2..."]
}
```

### Burn-and-Mint Operations

#### l2_createburntx

Create a burn transaction to convert CAS to L2 tokens.

```bash
cascoin-cli l2_createburntx <amount> <l2_recipient_address>
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| amount | numeric | Yes | Amount of CAS to burn |
| l2_recipient_address | string | Yes | L2 address to receive minted tokens |

**Response:**
```json
{
  "hex": "0100000001...",
  "burnAmount": "100.00000000",
  "l2Recipient": "0xa1b2c3...",
  "chainId": 1,
  "opReturnData": "4c324255524e..."
}
```

#### l2_sendburntx

Broadcast a signed burn transaction to the L1 network.

```bash
cascoin-cli l2_sendburntx <hex_tx>
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| hex_tx | string | Yes | Signed transaction hex from l2_createburntx |

**Response:**
```json
{
  "txid": "abc123...",
  "success": true,
  "message": "Burn transaction broadcast successfully"
}
```

#### l2_getburnstatus

Get the status of a burn transaction and its consensus progress.

```bash
cascoin-cli l2_getburnstatus <l1_tx_hash>
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| l1_tx_hash | string | Yes | L1 burn transaction hash |

**Response:**
```json
{
  "found": true,
  "l1TxHash": "abc123...",
  "l1Confirmations": 8,
  "requiredConfirmations": 6,
  "burnAmount": "100.00000000",
  "l2Recipient": "0xa1b2c3...",
  "consensusStatus": "REACHED",
  "confirmationCount": 4,
  "totalSequencers": 5,
  "confirmationRatio": 0.8,
  "mintStatus": "MINTED",
  "l2MintBlock": 12345,
  "l2MintTxHash": "def456..."
}
```

**Consensus Statuses:**
| Status | Description |
|--------|-------------|
| PENDING | Waiting for sequencer confirmations |
| REACHED | 2/3 consensus reached, minting in progress |
| MINTED | L2 tokens successfully minted |
| FAILED | Consensus failed (timeout or invalid) |
| REJECTED | Burn transaction rejected as invalid |

#### l2_getpendingburns

Get list of burns waiting for consensus.

```bash
cascoin-cli l2_getpendingburns
```

**Response:**
```json
[
  {
    "l1TxHash": "abc123...",
    "burnAmount": "100.00000000",
    "l2Recipient": "0xa1b2c3...",
    "confirmationCount": 2,
    "totalSequencers": 5,
    "firstSeenTime": 1704067200,
    "status": "PENDING"
  }
]
```

#### l2_getminthistory

Get history of minted L2 tokens.

```bash
cascoin-cli l2_getminthistory [from_block] [to_block]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| from_block | numeric | No | Start block (default: 0) |
| to_block | numeric | No | End block (default: current) |

**Response:**
```json
[
  {
    "l1TxHash": "abc123...",
    "l2Recipient": "0xa1b2c3...",
    "amount": "100.00000000",
    "l2MintBlock": 12345,
    "l2MintTxHash": "def456...",
    "timestamp": 1704067200
  }
]
```

#### l2_gettotalsupply

Get the current total L2 token supply.

```bash
cascoin-cli l2_gettotalsupply
```

**Response:**
```json
{
  "totalSupply": "1000000.00000000",
  "totalBurnedL1": "1000000.00000000",
  "burnCount": 5000
}
```

#### l2_verifysupply

Verify the supply invariant (total_supply == sum(balances) == total_burned_l1).

```bash
cascoin-cli l2_verifysupply
```

**Response:**
```json
{
  "valid": true,
  "totalSupply": "1000000.00000000",
  "sumOfBalances": "1000000.00000000",
  "totalBurnedL1": "1000000.00000000",
  "discrepancy": "0.00000000"
}
```

#### l2_getburnsforaddress

Get all burns for a specific L2 address.

```bash
cascoin-cli l2_getburnsforaddress <address>
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| address | string | Yes | L2 address |

**Response:**
```json
[
  {
    "l1TxHash": "abc123...",
    "amount": "100.00000000",
    "l2MintBlock": 12345,
    "timestamp": 1704067200
  }
]
```

---

## Contract Deployment

### Deploying CVM Contracts on L2

L2 supports full CVM (Cascoin Virtual Machine) compatibility. Contracts deployed on L1 can be deployed on L2 with the same bytecode.

```bash
# Deploy contract using L1 RPC (works on L2 too)
cascoin-cli deploycontract <bytecode_hex>

# Get contract info
cascoin-cli getcontractinfo <contract_address>
```

### Contract Addresses

Contract addresses on L2 are derived from:
- Deployer address
- Deployer nonce
- L2 chain ID

```
contract_address = hash(deployer_address + nonce + chain_id)[0:20]
```

### Gas Costs

L2 gas costs are similar to L1 but significantly cheaper due to batching:

| Operation | L2 Gas | Approximate L2 Cost |
|-----------|--------|---------------------|
| Transfer | 21,000 | ~0.0001 CAS |
| Contract Deploy | 32,000 + bytecode | ~0.001 CAS |
| Storage Write | 5,000 | ~0.00025 CAS |
| Storage Read | 200 | ~0.00001 CAS |

---

## Cross-Layer Messaging

### Overview

Cross-layer messaging enables communication between L1 and L2:

- **L1→L2**: Guaranteed delivery within next L2 block
- **L2→L1**: Requires challenge period before finalization

### L1 to L2 Messages

Messages from L1 to L2 are processed automatically when detected by sequencers.

```cpp
// Message structure
struct L1ToL2Message {
    uint256 messageId;      // Unique identifier
    uint160 l1Sender;       // L1 sender address
    uint160 l2Target;       // L2 target contract
    vector<uint8> data;     // Message payload
    CAmount value;          // CAS to transfer
    uint64 l1BlockNumber;   // L1 block of origin
    uint256 l1TxHash;       // L1 transaction hash
};
```

**Delivery Guarantee:**
- Messages are queued for next block execution
- Reentrancy protection prevents recursive calls
- Maximum 100 messages per block

### L2 to L1 Messages

Messages from L2 to L1 go through a challenge period:

```cpp
// Message structure
struct L2ToL1Message {
    uint256 messageId;          // Unique identifier
    uint160 l2Sender;           // L2 sender address
    uint160 l1Target;           // L1 target contract
    vector<uint8> data;         // Message payload
    CAmount value;              // CAS to transfer
    uint64 l2BlockNumber;       // L2 block of origin
    uint256 stateRoot;          // State root at creation
    vector<uint8> merkleProof;  // Inclusion proof
    uint64 challengeDeadline;   // When challenge period ends
};
```

**Challenge Period:**
- Standard: 7 days
- Fast (HAT ≥ 80): 1 day

### Message Proofs

L2→L1 messages include Merkle proofs for verification:

```cpp
// Verify message proof
bool verified = CrossLayerMessaging::VerifyMessageProof(
    message,
    proof,
    stateRoot
);
```

### Reentrancy Protection

Cross-layer calls are protected against reentrancy:

1. Messages are queued for next block (no same-block execution)
2. Mutex prevents recursive cross-layer calls
3. Checks-effects-interactions pattern enforced

---

## Burn-and-Mint Token Model

### Overview

Cascoin L2 uses a **Burn-and-Mint** model for secure cross-layer value transfer. This model provides cryptographic guarantees that traditional bridge contracts cannot offer.

### How It Works

```
┌──────────────────────────────────────────────────────────────────┐
│                    BURN-AND-MINT FLOW                            │
│                                                                  │
│  1. User creates Burn TX on L1                                   │
│     ┌─────────────────────────────────────────────────────────┐  │
│     │  Input: 100.001 CAS (100 to burn + 0.001 TX Fee)        │  │
│     │  Output 0: OP_RETURN "L2BURN" <chain_id> <pubkey> <amt> │  │
│     │                                                          │  │
│     │  Effect: 100 CAS are PERMANENTLY DESTROYED               │  │
│     └─────────────────────────────────────────────────────────┘  │
│                                                                  │
│  2. L1 TX is included in block                                   │
│     └─► Wait for 6 confirmations                                 │
│                                                                  │
│  3. Sequencers detect the Burn TX                                │
│     ├─► Sequencer 1: "I see valid burn, signing"                 │
│     ├─► Sequencer 2: "I see valid burn, signing"                 │
│     └─► Sequencer 3: "I see valid burn, signing"                 │
│                                                                  │
│  4. 2/3 Consensus reached                                        │
│     └─► L2 State Manager mints 100 L2-Tokens                     │
│                                                                  │
│  5. User has 100 L2-Tokens                                       │
│     └─► Can transfer on L2, use smart contracts, etc.            │
└──────────────────────────────────────────────────────────────────┘
```

### OP_RETURN Burn Format

The burn transaction uses a specific OP_RETURN format:

```
┌─────────────────────────────────────────────────────────────────┐
│                    OP_RETURN BURN FORMAT                        │
│                                                                 │
│  Byte 0:      OP_RETURN (0x6a)                                  │
│  Byte 1:      OP_PUSHDATA (length of data)                      │
│  Bytes 2-7:   "L2BURN" (6 bytes, ASCII marker)                  │
│  Bytes 8-11:  chain_id (4 bytes, little-endian uint32)          │
│  Bytes 12-44: recipient_pubkey (33 bytes, compressed)           │
│  Bytes 45-52: amount (8 bytes, little-endian int64, satoshis)   │
│                                                                 │
│  Total: 53 bytes (1 + 1 + 6 + 4 + 33 + 8)                       │
└─────────────────────────────────────────────────────────────────┘
```

### Why OP_RETURN is Secure

OP_RETURN outputs are **provably unspendable** because:

1. **Bitcoin Script Semantics**: OP_RETURN immediately terminates script execution. Any script starting with OP_RETURN always fails.

2. **No Private Key Attack**: Unlike burn addresses, there's no "key" to find. Even quantum computers can't spend OP_RETURN outputs.

3. **Node Consensus**: All nodes agree OP_RETURN outputs are unspendable. This is part of consensus rules, not configurable.

| Burn Address | OP_RETURN |
|--------------|-----------|
| Configurable | Hardcoded in consensus |
| Could be hacked | Mathematically impossible |
| Trust required | Trustless verification |
| Key might exist | No key concept |

### Sequencer Consensus

Before L2 tokens are minted, 2/3 of active sequencers must confirm the burn:

1. Each sequencer independently validates the L1 burn transaction
2. Sequencers broadcast signed confirmations to the network
3. When 2/3 threshold is reached, minting is triggered
4. The burn is recorded in the registry to prevent double-minting

**Consensus Requirements:**
- Minimum 6 L1 confirmations before consensus starts
- 2/3 (67%) of active sequencers must confirm
- 10-minute timeout for consensus
- Each sequencer can only confirm once per burn

### Supply Invariant

The system maintains a strict supply invariant:

```
L2_Total_Supply == Total_Burned_CAS_on_L1 == Sum_of_All_L2_Balances
```

This invariant is verified:
- At every L2 block
- Via the `l2_verifysupply` RPC command
- Any violation triggers a critical error

### Example: Burning CAS for L2 Tokens

```python
import requests
import json
import time

def rpc_call(method, params=[]):
    payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    response = requests.post(
        "http://localhost:8332",
        auth=("rpcuser", "rpcpassword"),
        json=payload
    )
    return response.json()

# Step 1: Create burn transaction
burn_tx = rpc_call("l2_createburntx", [100, "0xa1b2c3d4e5f6..."])
print(f"Burn TX created: {burn_tx['result']['hex'][:50]}...")

# Step 2: Sign the transaction (using wallet)
signed = rpc_call("signrawtransactionwithwallet", [burn_tx['result']['hex']])
print(f"Transaction signed")

# Step 3: Broadcast to L1
sent = rpc_call("l2_sendburntx", [signed['result']['hex']])
l1_txid = sent['result']['txid']
print(f"Burn TX broadcast: {l1_txid}")

# Step 4: Wait for confirmations and consensus
while True:
    status = rpc_call("l2_getburnstatus", [l1_txid])
    result = status['result']
    
    print(f"L1 Confirmations: {result['l1Confirmations']}/6")
    print(f"Consensus: {result['confirmationCount']}/{result['totalSequencers']}")
    print(f"Status: {result['consensusStatus']}")
    
    if result['mintStatus'] == 'MINTED':
        print(f"✓ L2 tokens minted at block {result['l2MintBlock']}")
        break
    
    time.sleep(10)

# Step 5: Check L2 balance
balance = rpc_call("l2_getbalance", ["0xa1b2c3d4e5f6..."])
print(f"L2 Balance: {balance['result']['balance_cas']} CAS")
```

### Deprecated: Old Bridge RPCs

The following RPCs have been removed and will return an error:

| Deprecated RPC | Replacement |
|----------------|-------------|
| `l2_deposit` | `l2_createburntx` + `l2_sendburntx` |
| `l2_withdraw` | Not yet implemented in burn-and-mint model |

If you call these deprecated RPCs, you will receive:
```json
{
  "error": {
    "code": -32601,
    "message": "l2_deposit is deprecated. Use l2_createburntx and l2_sendburntx for the new burn-and-mint model."
  }
}
```

---

## Transaction Types

L2 supports multiple transaction types:

| Type | Value | Description |
|------|-------|-------------|
| TRANSFER | 0 | Standard value transfer |
| CONTRACT_DEPLOY | 1 | Deploy new contract |
| CONTRACT_CALL | 2 | Call existing contract |
| DEPOSIT | 3 | L1→L2 deposit (bridge) |
| WITHDRAWAL | 4 | L2→L1 withdrawal |
| CROSS_LAYER_MSG | 5 | Cross-layer message |
| SEQUENCER_ANNOUNCE | 6 | Sequencer announcement |
| FORCED_INCLUSION | 7 | Forced inclusion from L1 |

### Transaction Structure

```cpp
struct L2Transaction {
    // Core fields
    uint160 from;           // Sender address
    uint160 to;             // Recipient address
    CAmount value;          // Value to transfer
    uint64 nonce;           // Replay protection
    uint64 gasLimit;        // Max gas
    CAmount gasPrice;       // Gas price
    vector<uint8> data;     // Call data
    
    // L2-specific
    L2TxType type;          // Transaction type
    uint64 l2ChainId;       // Chain ID
    bool isEncrypted;       // MEV protection
    
    // Signature
    vector<uint8> signature;
    uint8 recoveryId;
};
```

### Creating Transactions

```cpp
// Transfer
L2Transaction tx = CreateTransferTx(
    from, to, value, nonce, gasPrice, chainId
);

// Contract deployment
L2Transaction tx = CreateDeployTx(
    from, bytecode, nonce, gasLimit, gasPrice, chainId
);

// Contract call
L2Transaction tx = CreateCallTx(
    from, to, calldata, value, nonce, gasLimit, gasPrice, chainId
);

// Withdrawal
L2Transaction tx = CreateWithdrawalTx(
    from, l1Recipient, amount, nonce, gasPrice, chainId
);
```

---

## MEV Protection

### Encrypted Mempool

L2 implements MEV protection through an encrypted mempool:

1. **Submission**: Transactions are encrypted before submission
2. **Ordering**: Transactions ordered by commitment hash (not content)
3. **Decryption**: Threshold decryption requires 2/3+ sequencers
4. **Execution**: Decrypted transactions executed in random order

### How It Works

```
User → Encrypt(tx) → Encrypted Mempool → Block Inclusion
                                              ↓
                     Threshold Decryption (2/3 sequencers)
                                              ↓
                     Random Ordering → Execution
```

### Benefits

- **No front-running**: Sequencers can't see transaction content
- **No sandwich attacks**: Transaction ordering is randomized
- **Fair execution**: All users treated equally

### Encrypted Transaction Structure

```cpp
struct EncryptedPayload {
    vector<uint8> ciphertext;     // Encrypted data
    uint256 commitmentHash;        // Hash for ordering
    vector<uint8> nonce;           // Encryption nonce
    uint8 schemeVersion;           // Encryption version
};
```

---

## Reputation Integration

### HAT v2 Score Benefits

L2 integrates with Cascoin's HAT v2 reputation system:

| HAT Score | Benefits |
|-----------|----------|
| ≥ 70 | Increased rate limits (500 tx/block vs 100) |
| ≥ 70 | Gas discount (up to 50%) |
| ≥ 80 | Fast withdrawal (1 day vs 7 days) |
| ≥ 80 | Instant soft-finality |
| ≥ 70 | Sequencer eligibility |

### Checking Reputation

```bash
# Get HAT v2 score
cascoin-cli getsecuretrust <address>

# Get reputation breakdown
cascoin-cli gettrustbreakdown <address>
```

### Reputation Inheritance

L2 inherits reputation from L1:
- New L2 users start with their L1 HAT score
- L2 activity builds separate L2 reputation
- Aggregated score used for cross-layer operations

---

## Error Handling

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `L2 is not enabled` | L2 disabled | Start with `-l2=1` |
| `Invalid address format` | Bad address | Use 0x prefix or base58 |
| `Insufficient L2 balance` | Not enough funds | Burn more CAS to get L2 tokens |
| `Amount exceeds maximum` | Over limit | Reduce amount |
| `HAT score too low` | Low reputation | Build L1 reputation |
| `Stake too low` | Insufficient stake | Increase stake |
| `Block not found` | Invalid block | Check current height |

### Burn-and-Mint Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `L2_BURN_INVALID_FORMAT` | OP_RETURN format wrong | Check burn script format |
| `L2_BURN_WRONG_MARKER` | Missing "L2BURN" marker | Use l2_createburntx RPC |
| `L2_BURN_WRONG_CHAIN` | chain_id mismatch | Verify L2 chain ID |
| `L2_BURN_INVALID_PUBKEY` | Invalid recipient pubkey | Check recipient address |
| `L2_BURN_ZERO_AMOUNT` | Amount is zero | Specify amount > 0 |
| `L2_BURN_AMOUNT_MISMATCH` | Encoded != actual burned | Verify transaction inputs |
| `L2_CONSENSUS_NOT_SEQUENCER` | Signer not a sequencer | Only sequencers can confirm |
| `L2_CONSENSUS_INVALID_SIG` | Bad signature | Verify sequencer key |
| `L2_CONSENSUS_DUPLICATE` | Already confirmed | Each sequencer confirms once |
| `L2_CONSENSUS_TIMEOUT` | 10 min without 2/3 | Wait for more sequencers |
| `L2_MINT_ALREADY_PROCESSED` | Burn already minted | Cannot mint twice |
| `L2_MINT_NO_CONSENSUS` | Consensus not reached | Wait for 2/3 confirmations |
| `L2_MINT_INSUFFICIENT_CONF` | < 6 L1 confirmations | Wait for more L1 blocks |
| `L2_SUPPLY_INVARIANT_VIOLATED` | Supply != sum(balances) | Critical error - report bug |

### Error Response Format

```json
{
  "error": {
    "code": -1,
    "message": "Insufficient L2 balance. Have: 50.00 CAS, Need: 100.00 CAS"
  }
}
```

---

## Best Practices

### Security

1. **Validate addresses**: Always verify address format before transactions
2. **Check balances**: Verify sufficient balance before operations
3. **Monitor burn status**: Track burn transactions through consensus
4. **Verify supply**: Periodically check supply invariant with `l2_verifysupply`
5. **Handle reorgs**: Be prepared for L1 reorgs affecting burn confirmations

### Performance

1. **Batch operations**: Group multiple operations when possible
2. **Use appropriate gas**: Don't overpay for gas
3. **Monitor block times**: Adjust timing based on network conditions
4. **Cache state**: Cache frequently accessed state locally

### Integration

1. **Use WebSocket**: Subscribe to real-time updates via WebSocket
2. **Handle async**: All burn-and-mint operations are asynchronous
3. **Implement retries**: Network issues may require retries
4. **Log everything**: Maintain detailed logs for debugging

### Example Integration

```python
import requests
import time

class CascoinL2Client:
    def __init__(self, rpc_url, rpc_user, rpc_pass):
        self.rpc_url = rpc_url
        self.auth = (rpc_user, rpc_pass)
    
    def rpc(self, method, params=[]):
        response = requests.post(
            self.rpc_url,
            auth=self.auth,
            json={"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
        )
        result = response.json()
        if "error" in result:
            raise Exception(result["error"]["message"])
        return result["result"]
    
    def get_balance(self, address):
        return self.rpc("l2_getbalance", [address])
    
    def create_burn_tx(self, amount, l2_recipient):
        """Create a burn transaction to convert CAS to L2 tokens."""
        return self.rpc("l2_createburntx", [amount, l2_recipient])
    
    def send_burn_tx(self, signed_tx_hex):
        """Broadcast a signed burn transaction to L1."""
        return self.rpc("l2_sendburntx", [signed_tx_hex])
    
    def get_burn_status(self, l1_tx_hash):
        """Get the status of a burn transaction."""
        return self.rpc("l2_getburnstatus", [l1_tx_hash])
    
    def get_pending_burns(self):
        """Get list of burns waiting for consensus."""
        return self.rpc("l2_getpendingburns", [])
    
    def get_total_supply(self):
        """Get current L2 token supply."""
        return self.rpc("l2_gettotalsupply", [])
    
    def verify_supply(self):
        """Verify the supply invariant."""
        return self.rpc("l2_verifysupply", [])
    
    def wait_for_mint(self, l1_tx_hash, timeout=600):
        """Wait for burn to be minted (with timeout)."""
        start = time.time()
        while time.time() - start < timeout:
            status = self.get_burn_status(l1_tx_hash)
            if status.get("mintStatus") == "MINTED":
                return status
            if status.get("consensusStatus") in ["FAILED", "REJECTED"]:
                raise Exception(f"Burn failed: {status.get('consensusStatus')}")
            time.sleep(10)
        raise TimeoutError("Mint timeout - consensus not reached")

# Usage
client = CascoinL2Client("http://localhost:8332", "user", "pass")

# Check balance
balance = client.get_balance("0xa1b2c3...")
print(f"Balance: {balance['balance_cas']} CAS")

# Create and send burn transaction
burn_tx = client.create_burn_tx(100, "0xa1b2c3...")
print(f"Burn TX created")

# Sign transaction (requires wallet RPC)
signed = client.rpc("signrawtransactionwithwallet", [burn_tx['hex']])

# Send burn transaction
result = client.send_burn_tx(signed['hex'])
l1_txid = result['txid']
print(f"Burn TX sent: {l1_txid}")

# Wait for mint
try:
    mint_result = client.wait_for_mint(l1_txid)
    print(f"Minted at L2 block {mint_result['l2MintBlock']}")
except TimeoutError:
    print("Mint timed out - check pending burns")
    pending = client.get_pending_burns()
    print(f"Pending burns: {len(pending)}")

# Verify supply invariant
supply = client.verify_supply()
print(f"Supply valid: {supply['valid']}")
print(f"Total supply: {supply['totalSupply']} CAS")
```

---

## Demo Code and Tutorials

For hands-on learning, Cascoin provides ready-to-run demo code and step-by-step tutorials:

### Quick Start Demo

The fastest way to get started with L2 development is the demo setup script:

```bash
cd contrib/demos/l2-chain
./setup_l2_demo.sh
```

This script automatically:
- Starts a Cascoin node with L2 enabled
- Generates initial blocks (regtest mode)
- Registers a sequencer
- Performs an initial burn-and-mint operation

### Available Tutorials

| Tutorial | Description |
|----------|-------------|
| [L2 Quick Start](../contrib/demos/tutorials/01_l2_quickstart.md) | Complete L2 setup walkthrough with configuration options and troubleshooting |
| [Token Creation](../contrib/demos/tutorials/02_token_creation.md) | Create ERC-20 tokens with trust-aware features |

### Demo Files Location

All demo code is located in `contrib/demos/`:

```
contrib/demos/
├── README.md                    # Overview and quick start
├── l2-chain/
│   ├── setup_l2_demo.sh        # L2 chain setup script
│   ├── cleanup.sh              # Cleanup script
│   └── config/regtest.conf     # Regtest configuration
├── contracts/
│   ├── CascoinToken.sol        # ERC-20 token with trust features
│   ├── CascoinToken.cvm        # CVM bytecode version
│   ├── deploy_token.sh         # Token deployment script
│   └── demo_transfer.sh        # Token transfer demo
└── tutorials/
    ├── 01_l2_quickstart.md     # L2 setup tutorial
    └── 02_token_creation.md    # Token creation tutorial
```

### Related Documentation

- [CVM Developer Guide](CVM_DEVELOPER_GUIDE.md) - Smart contract development
- [L2 Operator Guide](L2_OPERATOR_GUIDE.md) - Running L2 infrastructure

---

## Appendix

### Address Formats

L2 supports two address formats:

1. **Hex format**: `0x` prefix + 40 hex characters
   - Example: `0xa1b2c3d4e5f6789012345678901234567890abcd`

2. **Base58 format**: Standard Cascoin address
   - Example: `DXG7YxPqLmN...`

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| REQUIRED_L1_CONFIRMATIONS | 6 | L1 confirmations before consensus |
| CONSENSUS_THRESHOLD | 67% | Required sequencer confirmations |
| CONSENSUS_TIMEOUT | 10 min | Timeout for consensus |
| MIN_SEQUENCERS | 3 | Minimum sequencers for consensus |
| STANDARD_CHALLENGE_PERIOD | 7 days | Normal withdrawal wait time |
| FAST_CHALLENGE_PERIOD | 1 day | Fast withdrawal wait time |
| FAST_WITHDRAWAL_MIN_HAT | 80 | Minimum HAT for fast withdrawal |
| CHALLENGE_BOND | 10 CAS | Bond required to challenge |
| MAX_MESSAGE_DATA_SIZE | 64 KB | Maximum cross-layer message size |
| MAX_MESSAGES_PER_BLOCK | 100 | Maximum messages per L2 block |
| MESSAGE_GAS_LIMIT | 1,000,000 | Gas limit for message execution |
| MIN_TRANSACTION_FEE | 0.00001 L2 | Minimum L2 transaction fee |

### RPC Command Summary

| Category | Command | Description |
|----------|---------|-------------|
| Chain | `l2_getchaininfo` | Get chain information |
| Chain | `l2_listchains` | List registered chains |
| Chain | `l2_getblockbynumber` | Get block by number |
| Account | `l2_getbalance` | Get account balance |
| Account | `l2_gettransactioncount` | Get account nonce |
| Deploy | `l2_deploy` | Deploy new L2 chain |
| Deploy | `l2_registerchain` | Register chain with stake |
| Deploy | `l2_getregisteredchain` | Get registered chain info |
| Deploy | `l2_listregisteredchains` | List registered chains |
| Deploy | `l2_updatechainstatus` | Update chain status |
| Sequencer | `l2_announcesequencer` | Announce as sequencer |
| Sequencer | `l2_getsequencers` | List sequencers |
| Sequencer | `l2_getleader` | Get current leader |
| Burn-Mint | `l2_createburntx` | Create burn transaction |
| Burn-Mint | `l2_sendburntx` | Broadcast burn transaction |
| Burn-Mint | `l2_getburnstatus` | Get burn/consensus status |
| Burn-Mint | `l2_getpendingburns` | List pending burns |
| Burn-Mint | `l2_getminthistory` | Get mint history |
| Burn-Mint | `l2_gettotalsupply` | Get total L2 supply |
| Burn-Mint | `l2_verifysupply` | Verify supply invariant |
| Burn-Mint | `l2_getburnsforaddress` | Get burns for address |
