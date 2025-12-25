# Cascoin CVM Operator Guide

This guide covers node configuration, CVM feature activation, and operational procedures for Cascoin node operators.

---

## Table of Contents

1. [Overview](#overview)
2. [System Requirements](#system-requirements)
3. [Node Configuration](#node-configuration)
4. [Soft Fork Activation](#soft-fork-activation)
5. [Monitoring](#monitoring)
6. [Maintenance](#maintenance)
7. [Troubleshooting](#troubleshooting)

---

## Overview

The CVM enhancement adds EVM compatibility to Cascoin through a soft fork. As a node operator, you need to:

1. **Upgrade your node** to a version supporting EVM features
2. **Configure** EVM-specific settings
3. **Monitor** the soft fork activation
4. **Maintain** contract state and database

### Key Features for Operators

- **Soft Fork Compatibility**: No chain split risk
- **Backward Compatible**: Old nodes continue to work
- **Gradual Activation**: BIP9 version bits activation
- **Resource Management**: Configurable gas limits and pruning

---

## System Requirements

### Minimum Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU | 4 cores | 8+ cores |
| RAM | 8 GB | 16+ GB |
| Storage | 100 GB SSD | 500+ GB NVMe |
| Network | 10 Mbps | 100+ Mbps |

### Additional Storage for EVM

EVM features require additional storage:

| Data Type | Estimated Size |
|-----------|---------------|
| Contract Bytecode | ~50 GB |
| Contract State | ~100 GB |
| Transaction Receipts | ~50 GB |
| Trust Graph | ~10 GB |

### Operating System

- **Linux**: Ubuntu 20.04+, Debian 11+, CentOS 8+ (recommended)
- **macOS**: 12.0+ (Monterey)
- **Windows**: 10/11 with WSL2

---

## Node Configuration

### Configuration File

Create or edit `~/.cascoin/cascoin.conf`:

```ini
# Network
testnet=1                    # Use testnet for testing (remove for mainnet)
listen=1                     # Accept incoming connections
maxconnections=125           # Maximum peer connections

# RPC
server=1                     # Enable RPC server
rpcuser=your_username        # RPC username
rpcpassword=your_password    # RPC password (use strong password!)
rpcallowip=127.0.0.1         # Restrict RPC access
rpcport=18332                # RPC port (testnet)

# CVM/EVM Settings
cvmenable=1                  # Enable CVM/EVM features
cvmgaslimit=10000000         # Block gas limit (10M default)
cvmtxgaslimit=1000000        # Transaction gas limit (1M default)
cvmprune=0                   # State pruning (0=disabled, N=keep N blocks)
cvmdebug=0                   # Debug logging (0=off, 1=on)

# Trust System
trustgraph=1                 # Enable trust graph
hatconsensus=1               # Enable HAT v2 consensus validation

# Performance
dbcache=4096                 # Database cache (MB)
par=4                        # Script verification threads

# Logging
debug=cvm                    # Enable CVM debug logs
logips=1                     # Log peer IPs
logtimestamps=1              # Include timestamps
```

### Command Line Options

```bash
# Start with EVM features
./cascoind -testnet \
    -cvmenable=1 \
    -cvmgaslimit=10000000 \
    -trustgraph=1 \
    -hatconsensus=1 \
    -daemon

# Start with debug logging
./cascoind -testnet \
    -cvmenable=1 \
    -cvmdebug=1 \
    -debug=cvm \
    -printtoconsole
```

### CVM-Specific Options

| Option | Default | Description |
|--------|---------|-------------|
| `-cvmenable` | 1 | Enable CVM/EVM features |
| `-cvmgaslimit` | 10000000 | Maximum gas per block |
| `-cvmtxgaslimit` | 1000000 | Maximum gas per transaction |
| `-cvmprune` | 0 | State pruning (blocks to keep) |
| `-cvmdebug` | 0 | Enable debug logging |
| `-cvmreindex` | 0 | Reindex contract state |
| `-trustgraph` | 1 | Enable trust graph |
| `-hatconsensus` | 1 | Enable HAT v2 consensus |
| `-hatminvalidators` | 10 | Minimum validators for consensus |
| `-hattimeout` | 30 | Validator response timeout (seconds) |

---

## Soft Fork Activation

### Activation Timeline

| Phase | Description | Duration |
|-------|-------------|----------|
| DEFINED | Feature defined, not signaling | - |
| STARTED | Miners can signal support | 2016 blocks |
| LOCKED_IN | 95% threshold reached | 2016 blocks |
| ACTIVE | Feature enforced | Permanent |

### Checking Activation Status

```bash
# Check soft fork status
./cascoin-cli -testnet getblockchaininfo

# Response includes:
{
    "softforks": {
        "cvm_evm": {
            "type": "bip9",
            "bip9": {
                "status": "active",
                "start_time": 1704067200,
                "timeout": 1735689600,
                "since": 100000
            },
            "height": 100000,
            "active": true
        }
    }
}
```

### Activation Heights

| Network | Start Height | Status |
|---------|-------------|--------|
| Mainnet | 500,000 | Pending |
| Testnet | 100,000 | Active |
| Regtest | 0 | Always Active |

### Signaling Support (Miners)

If you're a miner, signal support by including version bits:

```bash
# Check current block version
./cascoin-cli -testnet getblocktemplate '{"rules": ["segwit"]}'

# Version should include bit 28 for CVM_EVM support
# Binary: 0010 0000 0000 0000 0000 0000 0000 0000
# Hex: 0x20000000
```

### Pre-Activation Checklist

Before activation height:

1. ✅ Upgrade to EVM-compatible version
2. ✅ Configure CVM settings
3. ✅ Ensure sufficient disk space
4. ✅ Test on testnet first
5. ✅ Monitor peer connections
6. ✅ Backup wallet and data

### Post-Activation Verification

After activation:

```bash
# Verify EVM transactions are being processed
./cascoin-cli -testnet getmempoolinfo

# Check for CVM transactions
./cascoin-cli -testnet getrawmempool true | grep -i cvm

# Verify contract execution
./cascoin-cli -testnet cas_blockNumber
```

---

## Monitoring

### Health Checks

```bash
# Node status
./cascoin-cli -testnet getnetworkinfo
./cascoin-cli -testnet getblockchaininfo
./cascoin-cli -testnet getmempoolinfo

# CVM-specific status
./cascoin-cli -testnet getcvminfo
```

### CVM Info Response

```json
{
    "enabled": true,
    "version": "1.0.0",
    "activation_height": 100000,
    "current_height": 150000,
    "contracts_deployed": 1234,
    "total_gas_used": 5000000000,
    "mempool_cvm_count": 15,
    "trust_graph_nodes": 50000,
    "hat_validators_active": 150
}
```

### Log Monitoring

```bash
# Watch CVM logs
tail -f ~/.cascoin/testnet3/debug.log | grep -i cvm

# Common log entries:
# CVM: Executing contract 0x... (gas: 100000)
# CVM: Contract deployed at 0x...
# CVM: HAT consensus reached for tx 0x...
# CVM: Block gas used: 5000000/10000000
```

### Prometheus Metrics

If using Prometheus monitoring:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'cascoin'
    static_configs:
      - targets: ['localhost:9100']
    metrics_path: '/metrics'
```

Available metrics:
- `cascoin_cvm_contracts_total`
- `cascoin_cvm_gas_used_total`
- `cascoin_cvm_mempool_size`
- `cascoin_hat_validators_active`
- `cascoin_hat_consensus_success_rate`

### Alerting

Set up alerts for:

| Condition | Threshold | Action |
|-----------|-----------|--------|
| Block gas > 90% | 9M gas | Warning |
| HAT validators < 10 | Critical | Investigate |
| Contract failures > 10% | Warning | Check logs |
| Mempool backlog > 1000 | Warning | Check resources |

---

## Maintenance

### Database Management

```bash
# Check database size
du -sh ~/.cascoin/testnet3/

# Compact database (requires restart)
./cascoin-cli -testnet stop
./cascoind -testnet -reindex-chainstate

# Prune old state (if enabled)
./cascoind -testnet -cvmprune=1000
```

### State Pruning

Enable state pruning to reduce storage:

```ini
# cascoin.conf
cvmprune=1000    # Keep last 1000 blocks of state
```

**Warning**: Pruned nodes cannot serve historical state queries.

### Backup Procedures

```bash
# Stop node
./cascoin-cli -testnet stop

# Backup critical data
tar -czvf cascoin-backup-$(date +%Y%m%d).tar.gz \
    ~/.cascoin/testnet3/wallet.dat \
    ~/.cascoin/testnet3/cvm/ \
    ~/.cascoin/testnet3/trustgraph/

# Restart node
./cascoind -testnet -daemon
```

### Upgrade Procedures

```bash
# 1. Stop node gracefully
./cascoin-cli -testnet stop

# 2. Wait for shutdown
sleep 30

# 3. Backup data
cp -r ~/.cascoin/testnet3 ~/.cascoin/testnet3.backup

# 4. Install new version
make install

# 5. Start with new version
./cascoind -testnet -daemon

# 6. Verify upgrade
./cascoin-cli -testnet getnetworkinfo
```

### Reindexing

If contract state becomes corrupted:

```bash
# Full reindex (slow, rebuilds everything)
./cascoind -testnet -reindex

# CVM-only reindex (faster, rebuilds contract state)
./cascoind -testnet -cvmreindex
```

---

## Troubleshooting

### Common Issues

#### "CVM not enabled"

```
Error: CVM features are not enabled
```

**Solution**: Add `-cvmenable=1` to command line or `cvmenable=1` to config.

#### "Insufficient gas"

```
Error: Transaction gas limit exceeds block gas limit
```

**Solution**: Reduce transaction gas limit or wait for next block.

#### "HAT consensus failed"

```
Error: HAT consensus not reached for transaction
```

**Solution**: 
- Check validator count (`gethatinfo`)
- Verify network connectivity
- Check if transaction reputation is valid

#### "Contract state mismatch"

```
Error: Contract state root mismatch at height X
```

**Solution**: Reindex contract state with `-cvmreindex`.

#### "Database corruption"

```
Error: LevelDB corruption detected
```

**Solution**:
```bash
./cascoin-cli -testnet stop
rm -rf ~/.cascoin/testnet3/cvm/
./cascoind -testnet -cvmreindex
```

### Debug Commands

```bash
# Get detailed CVM info
./cascoin-cli -testnet getcvminfo

# Check specific contract
./cascoin-cli -testnet cas_getCode "0xCONTRACT_ADDRESS"

# Trace transaction execution
./cascoin-cli -testnet debug_traceTransaction "0xTXHASH"

# Check HAT consensus status
./cascoin-cli -testnet gethatinfo

# List active validators
./cascoin-cli -testnet listvalidators
```

### Log Levels

Increase logging for debugging:

```bash
# Maximum CVM logging
./cascoind -testnet \
    -debug=cvm \
    -debug=cvmexec \
    -debug=cvmstate \
    -debug=hat \
    -cvmdebug=1 \
    -printtoconsole
```

### Performance Tuning

For high-traffic nodes:

```ini
# cascoin.conf
dbcache=8192              # Increase DB cache
par=8                     # More verification threads
maxmempool=1000           # Larger mempool
cvmcache=2048             # CVM state cache (MB)
```

### Network Issues

```bash
# Check peer connections
./cascoin-cli -testnet getpeerinfo

# Add specific peers
./cascoin-cli -testnet addnode "node.example.com:18333" "add"

# Check for banned peers
./cascoin-cli -testnet listbanned
```

---

## Security Considerations

### RPC Security

```ini
# Restrict RPC access
rpcallowip=127.0.0.1
rpcbind=127.0.0.1

# Use strong credentials
rpcuser=<random_username>
rpcpassword=<strong_random_password>

# Enable SSL (if exposing RPC)
rpcssl=1
rpcsslcertificatechainfile=/path/to/cert.pem
rpcsslprivatekeyfile=/path/to/key.pem
```

### Firewall Rules

```bash
# Allow P2P connections
ufw allow 18333/tcp  # Testnet P2P

# Restrict RPC to localhost
ufw deny 18332/tcp   # Block external RPC
```

### Validator Security

If running as a validator:

```ini
# Validator settings
validatorkey=/path/to/validator.key
validatorstake=10                    # Minimum stake (CAS)
```

Protect validator keys:
```bash
chmod 600 /path/to/validator.key
chown cascoin:cascoin /path/to/validator.key
```

---

## Support Resources

- **Documentation**: [CASCOIN_DOCS.md](CASCOIN_DOCS.md)
- **Developer Guide**: [CVM_DEVELOPER_GUIDE.md](CVM_DEVELOPER_GUIDE.md)
- **Security Guide**: [CVM_SECURITY_GUIDE.md](CVM_SECURITY_GUIDE.md)
- **Source Code**: `src/cvm/` directory

---

**Last Updated:** December 2025  
**Version:** 1.0  
**Status:** Production Ready ✅
