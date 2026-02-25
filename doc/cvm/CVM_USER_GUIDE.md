# Cascoin CVM User Guide

This guide contains all essential commands for Cascoin users, including smart contracts, reputation, validator operations, and Web-of-Trust.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Wallet Commands](#wallet-commands)
3. [Reputation & Trust Score](#reputation--trust-score)
4. [Web-of-Trust Commands](#web-of-trust-commands)
5. [Becoming a Validator](#becoming-a-validator)
6. [Smart Contracts](#smart-contracts)
7. [Gas & Fees](#gas--fees)
8. [DAO & Disputes](#dao--disputes)
9. [Network & Status](#network--status)
10. [FAQ](#faq)

---

## Getting Started

### Starting the Node

```bash
# Start daemon (Mainnet)
./cascoind -daemon

# Start daemon (Testnet - recommended for testing!)
./cascoind -testnet -daemon

# Start GUI wallet
./cascoin-qt

# GUI wallet (Testnet)
./cascoin-qt -testnet
```

### Stopping the Node

```bash
./cascoin-cli stop
./cascoin-cli -testnet stop  # Testnet
```

### Check Status

```bash
# Blockchain info
./cascoin-cli getblockchaininfo

# Network info
./cascoin-cli getnetworkinfo

# Wallet info
./cascoin-cli getwalletinfo

# Sync status
./cascoin-cli getblockcount
```

---

## Wallet Commands

### Managing Addresses

```bash
# Create new address
./cascoin-cli getnewaddress

# New address with label
./cascoin-cli getnewaddress "MyLabel"

# List all addresses
./cascoin-cli listaddressgroupings

# Validate address
./cascoin-cli validateaddress "CsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
```

### Check Balance

```bash
# Total balance
./cascoin-cli getbalance

# Balance of specific address
./cascoin-cli getreceivedbyaddress "CsXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

# Unconfirmed transactions
./cascoin-cli getunconfirmedbalance

# Detailed UTXO list
./cascoin-cli listunspent
```

### Send CAS

```bash
# Simple transfer
./cascoin-cli sendtoaddress "RECIPIENT_ADDRESS" 10.0

# With comment
./cascoin-cli sendtoaddress "RECIPIENT_ADDRESS" 10.0 "Payment for service"

# Multiple recipients
./cascoin-cli sendmany "" '{"ADDRESS1": 10.0, "ADDRESS2": 5.0}'
```

### View Transactions

```bash
# Recent transactions
./cascoin-cli listtransactions

# Last 20 transactions
./cascoin-cli listtransactions "*" 20

# Transaction details
./cascoin-cli gettransaction "TXID"

# Raw transaction
./cascoin-cli getrawtransaction "TXID" true
```

---

## Reputation & Trust Score

### Check Your Trust Score

```bash
# Get HAT v2 Trust Score (0-100)
./cascoin-cli getsecuretrust "YOUR_ADDRESS"
```

**Example output:**
```json
{
    "address": "CsXXXX...",
    "score": 75,
    "components": {
        "behavior": 32,
        "wot": 22,
        "economic": 14,
        "temporal": 7
    },
    "eligible_free_gas": false,
    "gas_discount": 30
}
```

### Understanding Trust Score Components

| Component | Weight | Description |
|-----------|--------|-------------|
| Behavior | 40% | Transaction patterns, activity |
| Web-of-Trust | 30% | Trust relationships |
| Economic | 20% | Stake, transaction volume |
| Temporal | 10% | Account age, activity patterns |

### Detailed Trust Analysis

```bash
# Full trust breakdown
./cascoin-cli gettrustbreakdown "ADDRESS"

# Behavior metrics
./cascoin-cli getbehaviormetrics "ADDRESS"

# Graph metrics
./cascoin-cli getgraphmetrics "ADDRESS"
```

### Query Reputation (Other Addresses)

```bash
# Reputation of an address
./cascoin-cli getreputation "ADDRESS"

# Weighted reputation (from your WoT perspective)
./cascoin-cli getweightedreputation "ADDRESS"

# List all reputations
./cascoin-cli listreputations
```

### Vote on Reputation

```bash
# Simple vote (no bond)
./cascoin-cli votereputation "TARGET_ADDRESS" 50

# Send vote
./cascoin-cli sendcvmvote "TARGET_ADDRESS" 50

# Bonded vote (with collateral)
./cascoin-cli sendbondedvote "TARGET_ADDRESS" 75 1.0
# Parameters: Address, Value (-100 to +100), Bond amount in CAS
```

---

## Web-of-Trust Commands

### Create Trust Relationship

```bash
# Add trust relationship (local)
./cascoin-cli addtrust "TRUSTED_ADDRESS" 80

# Send trust relationship to blockchain (with bond)
./cascoin-cli sendtrustrelation "TRUSTED_ADDRESS" 80 0.5
# Parameters: Address, Weight (0-100), Bond in CAS
```

### View Trust Relationships

```bash
# List all trust relationships
./cascoin-cli listtrustrelations

# Trust graph statistics
./cascoin-cli gettrustgraphstats
```

**Example output:**
```json
{
    "total_nodes": 15234,
    "total_edges": 45678,
    "average_connections": 3.0,
    "max_path_length": 6,
    "clustering_coefficient": 0.42
}
```

### Calculate Effective Trust

```bash
# Trust considering wallet clustering
./cascoin-cli geteffectivetrust "ADDRESS"
```

---

## Becoming a Validator

### Check Requirements

To become a validator, you need:
- HAT v2 Score ≥ 50
- Minimum 10 CAS stake
- Stake age ≥ 70 days
- On-chain presence ≥ 70 days
- At least 100 transactions
- At least 20 unique interactions

```bash
# Check your validator eligibility
./cascoin-cli getvalidatorinfo

# Detailed validator statistics
./cascoin-cli getvalidatorstats "YOUR_ADDRESS"
```

### Create Validator Key

```bash
# Generate new validator key
./cascoin-cli generatevalidatorkey

# Key is saved to: ~/.cascoin/validator.key
```

### Register as Validator

```bash
# Announce validator status
./cascoin-cli announcevalidator

# Check validator status
./cascoin-cli getvalidatorinfo
```

**Example output:**
```json
{
    "address": "CsXXXX...",
    "is_validator": true,
    "stake": 15.0,
    "stake_age_blocks": 50000,
    "hat_score": 72,
    "total_validations": 1234,
    "accuracy_rate": 0.95,
    "earnings_total": 12.5,
    "status": "active"
}
```

### Check Validator Earnings

```bash
# Your validator earnings
./cascoin-cli getvalidatorearnings "YOUR_ADDRESS"

# Network-wide validator statistics
./cascoin-cli getvalidatorstats
```

**Example output:**
```json
{
    "address": "CsXXXX...",
    "total_earnings": 12.5,
    "pending_earnings": 0.25,
    "validations_performed": 1234,
    "last_validation_block": 150000,
    "average_earnings_per_block": 0.001
}
```

### Validator Configuration

In `~/.cascoin/cascoin.conf`:

```ini
# Enable validator
validator=1

# Validator key path
validatorkey=/home/user/.cascoin/validator.key

# Stake amount (CAS)
validatorstake=10

# Response timeout (seconds)
validatortimeout=30
```

### Monitor Validator Status

```bash
# Active validators in network
./cascoin-cli listvalidators

# Your validation history
./cascoin-cli getvalidationhistory "YOUR_ADDRESS" 100
```

---

## Smart Contracts

### Deploy Contract

```bash
# Deploy contract
./cascoin-cli cas_sendTransaction '{
    "from": "YOUR_ADDRESS",
    "data": "0x608060405234801561001057600080fd5b50...",
    "gas": "0x100000"
}'

# Alternative: deploycontract
./cascoin-cli deploycontract "BYTECODE_HEX" 1000000
# Parameters: Bytecode, Gas limit
```

### Call Contract

```bash
# Read-only call (free)
./cascoin-cli cas_call '{
    "to": "CONTRACT_ADDRESS",
    "data": "0x6d4ce63c"
}' "latest"

# State-changing call
./cascoin-cli cas_sendTransaction '{
    "from": "YOUR_ADDRESS",
    "to": "CONTRACT_ADDRESS",
    "data": "0x60fe47b10000000000000000000000000000000000000000000000000000000000000042",
    "gas": "0x50000"
}'

# Alternative: callcontract
./cascoin-cli callcontract "CONTRACT_ADDRESS" "FUNCTION_DATA" 500000
```

### Contract Information

```bash
# Get contract code
./cascoin-cli cas_getCode "CONTRACT_ADDRESS"

# Read contract storage
./cascoin-cli cas_getStorageAt "CONTRACT_ADDRESS" "0x0"

# Contract info
./cascoin-cli getcontractinfo "CONTRACT_ADDRESS"
```

### Transaction Receipt

```bash
# Get receipt
./cascoin-cli cas_getTransactionReceipt "TX_HASH"
```

**Example output:**
```json
{
    "transactionHash": "0x...",
    "blockHash": "0x...",
    "blockNumber": "0x24b6e",
    "contractAddress": "0x...",
    "gasUsed": "0x5208",
    "status": "0x1",
    "logs": []
}
```

---

## Gas & Fees

### Check Gas Price

```bash
# Current gas price
./cascoin-cli cas_gasPrice

# Estimate gas
./cascoin-cli cas_estimateGas '{
    "from": "YOUR_ADDRESS",
    "to": "CONTRACT_ADDRESS",
    "data": "0x..."
}'
```

### Reputation-Based Discounts

```bash
# Check your gas discount
./cascoin-cli cas_getReputationDiscount "YOUR_ADDRESS"
```

**Discount Table:**

| Reputation | Discount |
|------------|----------|
| 90-100 | 50% |
| 80-89 | 40% |
| 70-79 | 30% |
| 60-69 | 20% |
| 50-59 | 10% |
| 0-49 | 0% |

### Free Gas

```bash
# Check free gas allowance
./cascoin-cli cas_getFreeGasAllowance "YOUR_ADDRESS"
```

**Example output:**
```json
{
    "address": "CsXXXX...",
    "reputation": 85,
    "eligible": true,
    "daily_allowance": 1000000,
    "remaining": 750000,
    "resets_at": 1735084800
}
```

**Requirement:** Reputation ≥ 80

---

## DAO & Disputes

### Create Dispute

```bash
# Create dispute against a bonded vote
./cascoin-cli createdispute "VOTE_TX_HASH" "Reason for dispute" 0.5
# Parameters: TX hash of vote, Reason, Challenge bond in CAS
```

### View Disputes

```bash
# All open disputes
./cascoin-cli listdisputes

# Dispute details
./cascoin-cli getdispute "DISPUTE_ID"
```

**Example output:**
```json
{
    "dispute_id": "abc123...",
    "original_vote_tx": "def456...",
    "challenger": "CsXXXX...",
    "challenged_voter": "CsYYYY...",
    "reason": "Suspected manipulation",
    "challenge_bond": 0.5,
    "status": "voting",
    "votes_for_slash": 15,
    "votes_against_slash": 5,
    "deadline_block": 160000
}
```

### Vote on Dispute

```bash
# Vote for slash (confiscate voter's bond)
./cascoin-cli votedispute "DISPUTE_ID" true 1.0
# Parameters: Dispute ID, Slash (true/false), Stake in CAS

# Vote against slash (keep bond)
./cascoin-cli votedispute "DISPUTE_ID" false 1.0
```

---

## Network & Status

### Blockchain Status

```bash
# Current block height
./cascoin-cli getblockcount

# Block info
./cascoin-cli getblock "BLOCK_HASH"
./cascoin-cli getblockhash 150000

# Mempool info
./cascoin-cli getmempoolinfo

# CVM status
./cascoin-cli getcvminfo
```

### Peer Connections

```bash
# Connected peers
./cascoin-cli getpeerinfo

# Add peer
./cascoin-cli addnode "node.example.com:9333" "add"

# Network hashrate
./cascoin-cli getnetworkhashps
```

### Soft Fork Status

```bash
# Soft fork activation status
./cascoin-cli getblockchaininfo
```

Look for `softforks` → `cvm_evm` in the output.

---

## FAQ

### How do I increase my Trust Score?

1. **Be active**: Perform regular transactions
2. **Build trust relationships**: Create trust edges with other users
3. **Hold stake**: Keep CAS for longer periods
4. **Become a validator**: Active network participation
5. **Don't lose disputes**: Honest behavior

### Why is my score low?

```bash
# Detailed analysis
./cascoin-cli gettrustbreakdown "YOUR_ADDRESS"
```

Common reasons:
- New account (low Temporal score)
- Few transactions (low Behavior score)
- No trust relationships (low WoT score)
- Low stake (low Economic score)

### How do I become a Validator?

1. Check requirements: `./cascoin-cli getvalidatorinfo`
2. Generate key: `./cascoin-cli generatevalidatorkey`
3. Configure node (see above)
4. Restart node
5. Register: `./cascoin-cli announcevalidator`

### How much do I earn as a Validator?

- 30% of gas fees go to validators
- Split equally among participating validators
- Check with: `./cascoin-cli getvalidatorearnings "ADDRESS"`

### What happens during a Dispute?

1. Challenger creates dispute with bond
2. DAO members vote
3. If slash: Voter's bond is confiscated
4. If rejected: Challenger's bond is confiscated

---

## Quick Reference

### Most Important Commands

| Action | Command |
|--------|---------|
| Check trust score | `getsecuretrust "ADDRESS"` |
| Validator info | `getvalidatorinfo` |
| Become validator | `announcevalidator` |
| Deploy contract | `cas_sendTransaction {...}` |
| Call contract | `cas_call {...}` |
| Check gas discount | `cas_getReputationDiscount "ADDRESS"` |
| Check free gas | `cas_getFreeGasAllowance "ADDRESS"` |
| Add trust | `sendtrustrelation "ADDRESS" VALUE BOND` |
| Vote | `sendbondedvote "ADDRESS" VALUE BOND` |
| Create dispute | `createdispute "TX" "REASON" BOND` |

### Testnet vs Mainnet

**IMPORTANT:** Always use testnet for testing!

```bash
# Testnet
./cascoind -testnet -daemon
./cascoin-cli -testnet COMMAND

# Mainnet (Caution!)
./cascoind -daemon
./cascoin-cli COMMAND
```

---

## Additional Documentation

- [CVM Developer Guide](CVM_DEVELOPER_GUIDE.md) - For developers
- [CVM Operator Guide](CVM_OPERATOR_GUIDE.md) - For node operators
- [CVM Security Guide](CVM_SECURITY_GUIDE.md) - Security
- [HAT v2 User Guide](HAT_V2_USER_GUIDE.md) - Trust system details
- [Web-of-Trust](../WEB_OF_TRUST.md) - WoT documentation

---

**Last Updated:** December 2025  
**Version:** 1.0  
**Status:** Production Ready ✅
