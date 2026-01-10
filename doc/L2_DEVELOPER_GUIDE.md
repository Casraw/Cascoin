# Cascoin L2 Developer Guide

This guide provides comprehensive documentation for developers building applications on Cascoin Layer 2, including RPC API reference, contract deployment, cross-layer messaging, and integration patterns.

## Table of Contents

1. [Overview](#overview)
2. [Getting Started](#getting-started)
3. [RPC API Reference](#rpc-api-reference)
4. [Contract Deployment](#contract-deployment)
5. [Cross-Layer Messaging](#cross-layer-messaging)
6. [Bridge Operations](#bridge-operations)
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
│  l2_getbalance | l2_deposit | l2_withdraw | l2_deploy ...   │
├─────────────────────────────────────────────────────────────┤
│                    Cascoin L2 Layer                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │   Bridge    │  │  Sequencer  │  │  Cross-Layer Msg    │ │
│  │  Contract   │  │  Consensus  │  │     System          │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    Cascoin L1 (Mainchain)                   │
└─────────────────────────────────────────────────────────────┘
```

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

# Deposit CAS to L2
cascoin-cli l2_deposit "0xYourL2Address" 100

# Withdraw CAS from L2
cascoin-cli l2_withdraw "0xYourL2Address" "YourL1Address" 50
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

### Bridge Operations

#### l2_deposit

Process a deposit from L1 to L2.

```bash
cascoin-cli l2_deposit "l2address" amount ["l1txhash"]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| l2address | string | Yes | L2 recipient address |
| amount | numeric | Yes | Amount to deposit in CAS |
| l1txhash | string | No | L1 transaction hash (for tracking) |

**Response:**
```json
{
  "success": true,
  "depositId": "0xdep123...",
  "l2Recipient": "0xa1b2c3...",
  "amount": "100.00000000",
  "l1TxHash": "0x...",
  "l1BlockNumber": 500000,
  "message": "Deposit processed successfully"
}
```

**Limits:**
- Maximum per transaction: 10,000 CAS
- Maximum daily per address: 100,000 CAS

#### l2_withdraw

Initiate a withdrawal from L2 to L1.

```bash
cascoin-cli l2_withdraw "l2address" "l1address" amount [hatscore]
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| l2address | string | Yes | L2 sender address |
| l1address | string | Yes | L1 recipient address |
| amount | numeric | Yes | Amount to withdraw in CAS |
| hatscore | numeric | No | HAT v2 score for fast withdrawal (default: 0) |

**Response:**
```json
{
  "success": true,
  "withdrawalId": "0xwith123...",
  "l2Sender": "0xa1b2c3...",
  "l1Recipient": "0xdef456...",
  "amount": "50.00000000",
  "status": "PENDING",
  "challengeDeadline": 1704672000,
  "isFastWithdrawal": false,
  "hatScore": 0,
  "challengePeriodSeconds": 604800,
  "challengePeriodDays": 7.0,
  "message": "Withdrawal initiated (standard challenge period)"
}
```

**Fast Withdrawal:**
Users with HAT score ≥ 80 qualify for fast withdrawal with reduced challenge period (1 day instead of 7 days).

```bash
cascoin-cli l2_withdraw "0xsender..." "L1Address..." 50 85
```

#### l2_getwithdrawalstatus

Get the status of a withdrawal request.

```bash
cascoin-cli l2_getwithdrawalstatus "withdrawalid"
```

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| withdrawalid | string | Yes | Withdrawal identifier |

**Response:**
```json
{
  "found": true,
  "withdrawalId": "0xwith123...",
  "l2Sender": "0xa1b2c3...",
  "l1Recipient": "0xdef456...",
  "amount": "50.00000000",
  "status": "PENDING",
  "l2BlockNumber": 12345,
  "stateRoot": "0x...",
  "initiatedAt": 1704067200,
  "challengeDeadline": 1704672000,
  "isFastWithdrawal": false,
  "hatScore": 0,
  "canFinalize": false,
  "timeRemaining": 604800
}
```

**Withdrawal Statuses:**
| Status | Description |
|--------|-------------|
| PENDING | Waiting for challenge period to end |
| CHALLENGED | Under dispute |
| READY | Challenge period passed, ready to claim |
| COMPLETED | Successfully withdrawn |
| CANCELLED | Cancelled due to valid challenge |

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
| `Insufficient L2 balance` | Not enough funds | Deposit more CAS |
| `Amount exceeds maximum` | Over limit | Reduce amount |
| `Daily limit exceeded` | Too many deposits | Wait 24 hours |
| `HAT score too low` | Low reputation | Build L1 reputation |
| `Stake too low` | Insufficient stake | Increase stake |
| `Block not found` | Invalid block | Check current height |
| `Withdrawal not found` | Invalid ID | Verify withdrawal ID |

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
3. **Monitor withdrawals**: Track withdrawal status through challenge period
4. **Use fast withdrawal**: If HAT score qualifies, use fast withdrawal
5. **Handle reorgs**: Be prepared for L1 reorgs affecting L2 state

### Performance

1. **Batch operations**: Group multiple operations when possible
2. **Use appropriate gas**: Don't overpay for gas
3. **Monitor block times**: Adjust timing based on network conditions
4. **Cache state**: Cache frequently accessed state locally

### Integration

1. **Use WebSocket**: Subscribe to real-time updates via WebSocket
2. **Handle async**: All bridge operations are asynchronous
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
    
    def deposit(self, l2_address, amount):
        return self.rpc("l2_deposit", [l2_address, amount])
    
    def withdraw(self, l2_address, l1_address, amount, hat_score=0):
        return self.rpc("l2_withdraw", [l2_address, l1_address, amount, hat_score])
    
    def wait_for_withdrawal(self, withdrawal_id, timeout=604800):
        start = time.time()
        while time.time() - start < timeout:
            status = self.rpc("l2_getwithdrawalstatus", [withdrawal_id])
            if status["canFinalize"]:
                return status
            time.sleep(60)
        raise TimeoutError("Withdrawal challenge period not complete")

# Usage
client = CascoinL2Client("http://localhost:8332", "user", "pass")

# Check balance
balance = client.get_balance("0xa1b2c3...")
print(f"Balance: {balance['balance_cas']} CAS")

# Deposit
deposit = client.deposit("0xa1b2c3...", 100)
print(f"Deposit ID: {deposit['depositId']}")

# Withdraw with fast withdrawal (HAT score 85)
withdrawal = client.withdraw("0xa1b2c3...", "L1Address...", 50, 85)
print(f"Withdrawal ID: {withdrawal['withdrawalId']}")
print(f"Challenge period: {withdrawal['challengePeriodDays']} days")
```

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
| MAX_DEPOSIT_PER_TX | 10,000 CAS | Maximum single deposit |
| MAX_DAILY_DEPOSIT | 100,000 CAS | Maximum daily deposit per address |
| MAX_WITHDRAWAL_PER_TX | 10,000 CAS | Maximum single withdrawal |
| STANDARD_CHALLENGE_PERIOD | 7 days | Normal withdrawal wait time |
| FAST_CHALLENGE_PERIOD | 1 day | Fast withdrawal wait time |
| FAST_WITHDRAWAL_MIN_HAT | 80 | Minimum HAT for fast withdrawal |
| CHALLENGE_BOND | 10 CAS | Bond required to challenge |
| MAX_MESSAGE_DATA_SIZE | 64 KB | Maximum cross-layer message size |
| MAX_MESSAGES_PER_BLOCK | 100 | Maximum messages per L2 block |
| MESSAGE_GAS_LIMIT | 1,000,000 | Gas limit for message execution |

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
| Bridge | `l2_deposit` | Deposit to L2 |
| Bridge | `l2_withdraw` | Withdraw from L2 |
| Bridge | `l2_getwithdrawalstatus` | Get withdrawal status |
