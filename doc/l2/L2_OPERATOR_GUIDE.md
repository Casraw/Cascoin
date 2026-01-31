# Cascoin L2 Operator Guide

This guide provides comprehensive instructions for operating Cascoin Layer 2 nodes, including setup, sequencer operation, monitoring, and troubleshooting.

## Table of Contents

1. [Overview](#overview)
2. [System Requirements](#system-requirements)
3. [Node Setup](#node-setup)
4. [L2 Configuration](#l2-configuration)
5. [Sequencer Operation](#sequencer-operation)
6. [Burn-and-Mint Consensus](#burn-and-mint-consensus)
7. [Fee Distribution](#fee-distribution)
8. [Monitoring and Dashboards](#monitoring-and-dashboards)
9. [Security Best Practices](#security-best-practices)
10. [Troubleshooting](#troubleshooting)
11. [Maintenance](#maintenance)

---

## Overview

Cascoin L2 is a native Layer 2 scaling solution that extends the Cascoin blockchain with:

- **High throughput**: 1000+ TPS with sub-second block times
- **Low fees**: 90%+ reduction compared to L1
- **Security**: Burn-and-Mint token model with OP_RETURN burns
- **Reputation integration**: HAT v2 trust scores for sequencer eligibility
- **MEV protection**: Encrypted mempool with threshold decryption
- **Sequencer Consensus**: 2/3 majority required for minting L2 tokens

### Token Model: Burn-and-Mint

Cascoin L2 uses a **Burn-and-Mint** model instead of traditional bridge contracts:

- **Users burn CAS on L1** via OP_RETURN transactions (provably unspendable)
- **Sequencers validate burns** and broadcast confirmations
- **2/3 consensus required** before L2 tokens are minted
- **1:1 backing guaranteed**: L2 supply always equals total burned CAS
- **Sequencer rewards from fees only**: No token inflation

### L2 Node Modes

Cascoin nodes support three L2 operation modes:

| Mode | Description | Storage | Use Case |
|------|-------------|---------|----------|
| **Full Node** | Validates all L2 transactions, stores complete state | High | Sequencers, validators |
| **Light Client** | Verifies state roots only, minimal storage | Low | End users, mobile |
| **Disabled** | L1 only, no L2 participation | None | Legacy nodes |

---

## System Requirements

### Minimum Requirements (Full Node)

| Component | Requirement |
|-----------|-------------|
| CPU | 4 cores, 2.5 GHz+ |
| RAM | 8 GB |
| Storage | 100 GB SSD |
| Network | 25 Mbps, low latency |
| OS | Linux (Ubuntu 20.04+), macOS 12+ |

### Recommended Requirements (Sequencer)

| Component | Requirement |
|-----------|-------------|
| CPU | 8+ cores, 3.0 GHz+ |
| RAM | 32 GB |
| Storage | 500 GB NVMe SSD |
| Network | 100 Mbps, <50ms latency to peers |
| OS | Linux (Ubuntu 22.04 LTS) |

### Sequencer Eligibility Requirements

To operate as a sequencer, you must meet:

- **Minimum HAT v2 Score**: 70 (mainnet), 50 (testnet), 10 (regtest)
- **Minimum Stake**: 100 CAS (mainnet), 10 CAS (testnet), 1 CAS (regtest)
- **Minimum Peers**: 3 connected peers
- **Uptime**: 99%+ recommended

---

## Node Setup

### Building from Source

```bash
# Clone repository
git clone https://github.com/AquaCash/cascoin.git
cd cascoin

# Generate build files
./autogen.sh

# Configure with L2 support (enabled by default)
./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --with-incompatible-bdb \
  MOC=/usr/lib/qt6/libexec/moc \
  UIC=/usr/lib/qt6/libexec/uic \
  RCC=/usr/lib/qt6/libexec/rcc

# Build
make -j$(nproc)

# Run tests (optional but recommended)
make check
```

### Installation

```bash
# Install binaries
sudo make install

# Or run from build directory
./src/cascoind
./src/cascoin-cli
./src/qt/cascoin-qt
```

---

## L2 Configuration

### Command-Line Options

| Option | Default | Description |
|--------|---------|-------------|
| `-l2` | `1` | Enable L2 functionality |
| `-nol2` | - | Disable L2 (equivalent to `-l2=0`) |
| `-l2chainid=<n>` | `1` | L2 chain ID to connect to. Each L2 chain has a unique ID. Use `l2_listchains` to see available chains. |
| `-l2mode=<mode>` | `full` | Node mode: `full`, `light`, `disabled` |

### Configuration File (cascoin.conf)

```ini
# L2 Configuration
l2=1
l2chainid=1        # Which L2 chain to connect to (each chain has unique ID)
l2mode=full

# Sequencer Configuration (if operating as sequencer)
# These are set via RPC, not config file

# Network
listen=1
maxconnections=125

# Performance
dbcache=4096
par=0

# Logging
debug=l2
debug=net
```

**Note on Chain IDs:** Multiple L2 chains can exist on the Cascoin network. Each chain has a unique ID. Use `l2_listchains` to see available chains. The `l2chainid` setting determines which chain your node participates in.

### Network-Specific Parameters

#### Mainnet

```ini
# Default L2 parameters for mainnet
# Min sequencer HAT score: 70
# Min sequencer stake: 100 CAS
# Challenge period: 7 days
# Block time: 500ms
# Max block gas: 30,000,000
```

#### Testnet

```ini
testnet=1
# Min sequencer HAT score: 50
# Min sequencer stake: 10 CAS
# Challenge period: 1 day
# Block time: 500ms
```

#### Regtest (Development)

```ini
regtest=1
# Min sequencer HAT score: 10
# Min sequencer stake: 1 CAS
# Challenge period: 10 minutes
# Block time: 100ms
```

---

## Sequencer Operation

### Becoming a Sequencer

1. **Ensure eligibility requirements are met**:
   - HAT v2 score ≥ 70 (check with `getsecuretrust`)
   - Sufficient CAS for staking (≥ 100 CAS)
   - Node fully synced with L1 and L2

2. **Announce as sequencer**:

```bash
# Announce with stake and HAT score
cascoin-cli l2_announcesequencer 100 75

# With optional public endpoint
cascoin-cli l2_announcesequencer 100 75 "203.0.113.50:8333"
```

3. **Verify announcement**:

```bash
# Check sequencer list
cascoin-cli l2_getsequencers

# Check if eligible
cascoin-cli l2_getsequencers true
```

### Sequencer RPC Commands

| Command | Description |
|---------|-------------|
| `l2_announcesequencer` | Announce this node as sequencer |
| `l2_getsequencers` | List known sequencers |
| `l2_getleader` | Get current leader info |

### Leader Election

Leaders are elected deterministically based on:
- Block height and slot number
- Sequencer set hash
- Reputation-weighted random selection

Each leader produces blocks for a configurable number of slots (default: 10 blocks) before rotation.

### Failover Handling

If the current leader fails to produce a block within the timeout (default: 3 seconds):

1. Other sequencers detect the timeout
2. Next sequencer in backup list claims leadership
3. Consensus is reached on new leader
4. Failed sequencer receives reputation penalty

---

## Burn-and-Mint Consensus

### Overview

As a sequencer, you play a critical role in the burn-and-mint process. When users burn CAS on L1, sequencers must validate and confirm these burns before L2 tokens can be minted.

### Consensus Process

```
┌─────────────────────────────────────────────────────────────────────┐
│                    SEQUENCER CONSENSUS FLOW                         │
│                                                                     │
│  1. L1 Burn TX detected (OP_RETURN "L2BURN" ...)                    │
│     └─► Your node monitors L1 for burn transactions                 │
│                                                                     │
│  2. Wait for 6 L1 confirmations                                     │
│     └─► Ensures burn is final and not subject to reorg              │
│                                                                     │
│  3. Validate burn transaction                                       │
│     ├─► Check OP_RETURN format is correct                           │
│     ├─► Verify chain_id matches your L2 chain                       │
│     ├─► Confirm burn not already processed (double-mint check)      │
│     └─► Calculate burned amount from TX inputs/outputs              │
│                                                                     │
│  4. Sign and broadcast confirmation                                 │
│     └─► Your sequencer signs: (l1_tx_hash, recipient, amount)       │
│                                                                     │
│  5. Collect confirmations from other sequencers                     │
│     └─► Wait for 2/3 (67%) of active sequencers to confirm          │
│                                                                     │
│  6. Mint L2 tokens when consensus reached                           │
│     └─► Tokens credited to recipient address                        │
└─────────────────────────────────────────────────────────────────────┘
```

### Consensus Requirements

| Parameter | Value | Description |
|-----------|-------|-------------|
| L1 Confirmations | 6 | Minimum L1 blocks before consensus starts |
| Consensus Threshold | 67% | Required sequencer confirmations |
| Consensus Timeout | 10 min | Maximum time to reach consensus |
| Minimum Sequencers | 3 | Minimum active sequencers for consensus |

### Monitoring Consensus

```bash
# View pending burns waiting for consensus
cascoin-cli l2_getpendingburns

# Check status of a specific burn
cascoin-cli l2_getburnstatus <l1_tx_hash>

# View total supply and verify invariant
cascoin-cli l2_verifysupply
```

### Consensus States

| State | Description |
|-------|-------------|
| PENDING | Waiting for sequencer confirmations |
| REACHED | 2/3 consensus reached, minting in progress |
| MINTED | L2 tokens successfully minted |
| FAILED | Consensus timeout (10 min without 2/3) |
| REJECTED | Burn transaction invalid |

### Sequencer Responsibilities

As a sequencer, you must:

1. **Stay online**: Your node must be running to participate in consensus
2. **Validate honestly**: Only confirm valid burn transactions
3. **Respond promptly**: Confirmations should be sent within seconds
4. **Maintain connectivity**: Stay connected to other sequencers

**Warning**: Sequencers that confirm invalid burns or fail to participate may be penalized through reputation reduction.

---

## Fee Distribution

### Overview

Sequencer rewards come **exclusively from transaction fees**. There is no token inflation or block rewards that mint new tokens.

### How Fee Distribution Works

```
┌─────────────────────────────────────────────────────────────────────┐
│                    FEE DISTRIBUTION FLOW                            │
│                                                                     │
│  1. Block contains transactions with fees                           │
│     └─► Each L2 transaction pays a fee in L2 tokens                 │
│                                                                     │
│  2. Block producer (current leader) finalizes block                 │
│     └─► All transaction fees are collected                          │
│                                                                     │
│  3. Fees credited to block producer                                 │
│     └─► 100% of fees go to the sequencer who produced the block     │
│                                                                     │
│  4. No new tokens minted                                            │
│     └─► Total supply remains unchanged                              │
└─────────────────────────────────────────────────────────────────────┘
```

### Fee Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Minimum TX Fee | 0.00001 L2 | Minimum fee per transaction |
| Fee Recipient | Block Producer | 100% to current leader |
| Block Reward | 0 | No inflation, fees only |

### Monitoring Fee Earnings

```bash
# Check your sequencer's total fees earned
# (requires knowing your sequencer address)
cascoin-cli l2_getbalance <your_sequencer_address>

# View recent blocks and their fee totals
cascoin-cli l2_getblockbynumber <block_number>
```

### Fee Economics

- **No inflation**: L2 token supply only increases through burns
- **Sustainable rewards**: Sequencers earn from network usage
- **Spam prevention**: Minimum fee prevents transaction spam
- **Transparent**: All fees visible on-chain

### Empty Blocks

If a block contains no transactions:
- The sequencer receives **no reward** for that block
- This incentivizes sequencers to include transactions
- Empty blocks are still valid and advance the chain

---

## Monitoring and Dashboards

### Web Dashboard

Access the L2 dashboard at `http://localhost:8080/l2/dashboard` when the node is running.

The dashboard displays:
- L2 chain status (blocks, TPS, gas usage)
- Sequencer status (leader, uptime, reputation)
- Burn-and-mint status (pending burns, consensus progress)
- Total supply and supply verification
- Recent transactions and blocks
- Security alerts

### RPC Monitoring Commands

```bash
# List all available L2 chains
cascoin-cli l2_listchains

# Get L2 chain info (current chain)
cascoin-cli l2_getchaininfo

# Get info for specific chain by ID
cascoin-cli l2_getchaininfo 1001

# Get current block height and state
cascoin-cli l2_getblockbynumber <height>

# Check sequencer status
cascoin-cli l2_getsequencers

# Check leader status
cascoin-cli l2_getleader

# Monitor burn-and-mint consensus
cascoin-cli l2_getpendingburns
cascoin-cli l2_getburnstatus <l1_tx_hash>

# Verify supply invariant
cascoin-cli l2_verifysupply

# Get total supply
cascoin-cli l2_gettotalsupply

# Get mint history
cascoin-cli l2_getminthistory [from_block] [to_block]
```

### Key Metrics to Monitor

| Metric | Normal Range | Alert Threshold |
|--------|--------------|-----------------|
| Block time | 400-600ms | >2000ms |
| TPS | 100-1000 | <10 |
| Sequencer count | 5+ | <3 |
| Leader uptime | 99%+ | <95% |
| Pending burns | <100 | >500 |
| Consensus time | <60s | >300s |
| Supply invariant | Valid | Invalid |

### Log Files

L2-specific logs are written to the standard debug.log with the `l2` category:

```bash
# View L2 logs
tail -f ~/.cascoin/debug.log | grep "\[l2\]"

# Enable verbose L2 logging
cascoin-cli logging "[\"l2\"]"
```

---

## Security Best Practices

### Key Management

1. **Use separate keys for sequencer operations**:
   - Hot key: Block signing (limited funds)
   - Cold key: Stake management (secure storage)

2. **Enable wallet encryption**:
```bash
cascoin-cli encryptwallet "your-strong-passphrase"
```

3. **Regular key rotation**:
   - Rotate signing keys every 30 days
   - Use `l2_announcesequencer` with new key

### Network Security

1. **Firewall configuration**:
```bash
# Allow P2P port
sudo ufw allow 8333/tcp

# Allow RPC (localhost only)
sudo ufw allow from 127.0.0.1 to any port 8332
```

2. **Use Tor for privacy** (optional):
```ini
# cascoin.conf
proxy=127.0.0.1:9050
listen=1
bind=127.0.0.1
```

### Operational Security

1. **Monitor for anomalies**:
   - Unusual withdrawal patterns
   - Sudden reputation changes
   - Network partition events

2. **Set up alerts**:
   - Leader timeout events
   - Fraud proof submissions
   - Circuit breaker activations

3. **Regular backups**:
```bash
# Backup wallet and L2 state
cascoin-cli backupwallet /path/to/backup/wallet.dat
cp -r ~/.cascoin/l2state /path/to/backup/
```

---

## Troubleshooting

### Common Issues

#### L2 Not Enabled

**Symptom**: RPC commands return "L2 is not enabled"

**Solution**:
```bash
# Check if L2 is enabled
cascoin-cli l2_getchaininfo

# If disabled, restart with L2 enabled
cascoind -l2=1
```

#### Sequencer Not Eligible

**Symptom**: `l2_announcesequencer` fails or shows `eligible: false`

**Causes and Solutions**:

1. **HAT score too low**:
```bash
# Check your HAT score
cascoin-cli getsecuretrust <your-address>
# Build reputation through L1 activity
```

2. **Insufficient stake**:
```bash
# Ensure you have enough CAS
cascoin-cli getbalance
# Increase stake in announcement
cascoin-cli l2_announcesequencer 150 75
```

3. **Not enough peers**:
```bash
# Check peer count
cascoin-cli getpeerinfo | grep -c "addr"
# Add more connections
cascoin-cli addnode "seed.cascoin.org" "add"
```

#### Leader Election Issues

**Symptom**: Node never becomes leader despite being eligible

**Diagnosis**:
```bash
# Check current leader
cascoin-cli l2_getleader

# Check your position in sequencer list
cascoin-cli l2_getsequencers true
```

**Solutions**:
- Ensure clock is synchronized (NTP)
- Check network connectivity to other sequencers
- Verify stake and HAT score are current

#### Sync Issues

**Symptom**: L2 state falls behind or shows incorrect data

**Solutions**:
```bash
# Check sync status
cascoin-cli l2_getchaininfo

# Force resync from L1 data
cascoind -reindex-l2

# Clear L2 state and resync
rm -rf ~/.cascoin/l2state
cascoind
```

#### Bridge Issues

**Symptom**: Burns not being processed or minted

**Diagnosis**:
```bash
# Check burn status
cascoin-cli l2_getburnstatus <l1_tx_hash>

# Check pending burns
cascoin-cli l2_getpendingburns

# Verify supply invariant
cascoin-cli l2_verifysupply
```

**Solutions**:
- Wait for 6 L1 confirmations before consensus starts
- Verify L1 transaction was confirmed
- Check if consensus timeout occurred (10 min)
- Ensure enough sequencers are online (minimum 3)

#### Consensus Issues

**Symptom**: Burns stuck in PENDING state

**Causes and Solutions**:

1. **Not enough sequencers online**:
```bash
# Check active sequencers
cascoin-cli l2_getsequencers true
# Need at least 3 sequencers for consensus
```

2. **Consensus timeout**:
```bash
# Check burn status for timeout
cascoin-cli l2_getburnstatus <l1_tx_hash>
# If FAILED, the burn may need to be resubmitted
```

3. **Network connectivity issues**:
```bash
# Check peer connections
cascoin-cli getpeerinfo | grep -c "addr"
# Ensure connectivity to other sequencers
```

### Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| `L2 is not enabled` | L2 disabled in config | Add `-l2=1` to startup |
| `Stake must be at least X CAS` | Insufficient stake | Increase stake amount |
| `HAT score must be at least X` | Low reputation | Build L1 reputation |
| `Keypool ran out` | No keys available | Generate new keys |
| `Insufficient L2 balance` | Not enough funds | Burn more CAS |
| `Block not found` | Invalid block number | Check current height |
| `Chain not found` | Invalid chain ID | Use `l2_listchains` |
| `L2_CONSENSUS_TIMEOUT` | 10 min without 2/3 | Check sequencer count |
| `L2_MINT_ALREADY_PROCESSED` | Burn already minted | Cannot mint twice |
| `L2_SUPPLY_INVARIANT_VIOLATED` | Supply mismatch | Critical - report bug |

### Getting Help

1. **Check logs**:
```bash
tail -100 ~/.cascoin/debug.log
```

2. **Enable debug logging**:
```bash
cascoin-cli logging "[\"l2\", \"net\", \"rpc\"]"
```

3. **Community support**:
   - Discord: discord.gg/cascoin
   - GitHub Issues: github.com/AquaCash/cascoin/issues

---

## Maintenance

### Regular Tasks

| Task | Frequency | Command/Action |
|------|-----------|----------------|
| Check sync status | Daily | `l2_getchaininfo` |
| Monitor disk space | Weekly | `df -h` |
| Review logs | Weekly | Check debug.log |
| Update software | Monthly | Pull and rebuild |
| Rotate keys | Monthly | Re-announce sequencer |
| Backup wallet | Weekly | `backupwallet` |

### Upgrading

```bash
# Stop node gracefully
cascoin-cli stop

# Pull latest code
cd cascoin
git pull

# Rebuild
make clean
./autogen.sh
./configure [options]
make -j$(nproc)

# Restart
cascoind
```

### Graceful Shutdown

```bash
# Stop accepting new transactions
cascoin-cli l2_updatechainstatus <chainid> "PAUSED"

# Wait for pending operations
sleep 60

# Stop node
cascoin-cli stop
```

---

## Appendix

### L2 Chain Parameters Reference

| Parameter | Mainnet | Testnet | Regtest |
|-----------|---------|---------|---------|
| Min Sequencer HAT Score | 70 | 50 | 10 |
| Min Sequencer Stake | 100 CAS | 10 CAS | 1 CAS |
| Blocks Per Leader | 10 | 10 | 5 |
| Leader Timeout | 3s | 3s | 1s |
| Consensus Threshold | 67% | 67% | 67% |
| Target Block Time | 500ms | 500ms | 100ms |
| Max Block Gas | 30M | 30M | 10M |
| Standard Challenge Period | 7 days | 1 day | 10 min |
| Fast Challenge Period | 1 day | 1 hour | 1 min |
| Fast Withdrawal HAT Threshold | 80 | 70 | 50 |
| Max Deposit Per Tx | 10,000 CAS | 1,000 CAS | 100 CAS |
| Max Daily Deposit | 100,000 CAS | 10,000 CAS | 1,000 CAS |
| L1 Anchor Interval | 100 blocks | 50 blocks | 10 blocks |
| L1 Finality Confirmations | 6 | 3 | 1 |

### RPC Command Quick Reference

```bash
# Chain Info
l2_getchaininfo [chainid]
l2_listchains
l2_deploy "name" [blocktime] [gaslimit] [challengeperiod]

# Account Operations
l2_getbalance "address"
l2_gettransactioncount "address"
l2_getblockbynumber blocknumber [verbose]

# Sequencer Operations
l2_announcesequencer stake hatscore ["endpoint"]
l2_getsequencers [eligibleonly]
l2_getleader

# Burn-and-Mint Operations
l2_createburntx amount "l2_recipient"
l2_sendburntx "hex_tx"
l2_getburnstatus "l1_tx_hash"
l2_getpendingburns
l2_getminthistory [from_block] [to_block]
l2_gettotalsupply
l2_verifysupply
l2_getburnsforaddress "address"

# Registry Operations
l2_registerchain "name" stake [blocktime] [gaslimit] [challengeperiod]
l2_getregisteredchain chainid|"name"
l2_listregisteredchains [activeonly]
l2_updatechainstatus chainid "status"
```
