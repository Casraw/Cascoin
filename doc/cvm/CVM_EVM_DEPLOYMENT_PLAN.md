# CVM-EVM Enhancement Deployment Plan

## Overview

This document outlines the deployment plan for the CVM-EVM enhancement on Cascoin. The deployment follows a phased approach with testnet validation before mainnet activation.

## Deployment Timeline

### Phase 1: Development & Testing (Completed)
- **Status**: ✅ Complete
- **Duration**: Q4 2024 - Q1 2025
- **Activities**:
  - Core implementation of EVMC integration
  - Trust-aware operations implementation
  - Sustainable gas system implementation
  - HAT v2 consensus validator implementation
  - Unit and integration testing
  - Security analysis and hardening

### Phase 2: Testnet Deployment
- **Status**: 🔄 In Progress
- **Target Start**: February 2025 (Unix timestamp: 1745000000)
- **Duration**: 4-6 weeks minimum
- **Activities**:
  - Deploy to Cascoin testnet
  - Community testing and feedback
  - Bug fixes and optimizations
  - Performance benchmarking
  - Security audit completion

### Phase 3: Mainnet Signaling
- **Status**: ⏳ Pending
- **Target Start**: March 2025 (Unix timestamp: 1750000000)
- **Duration**: 2-4 weeks (BIP9 signaling period)
- **Activities**:
  - Miners begin signaling support via version bits
  - Monitor signaling progress
  - Community coordination
  - Final documentation updates

### Phase 4: Mainnet Activation
- **Status**: ⏳ Pending
- **Target**: April 2025 (after 75% miner signaling)
- **Activities**:
  - CVM-EVM features become active
  - Monitor network stability
  - Support early adopters
  - Address any issues

## Network Configuration

### Mainnet Parameters

```cpp
// BIP9 Deployment Configuration
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].bit = 10;
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nStartTime = 1750000000;  // ~March 2025
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nTimeout = 1781536000;    // Start + 1 year

// CVM Parameters (already active)
consensus.cvmActivationHeight = 220000;
consensus.cvmMaxGasPerBlock = 10000000;   // 10M gas per block
consensus.cvmMaxGasPerTx = 1000000;       // 1M gas per transaction
consensus.cvmMaxCodeSize = 24576;         // 24KB max contract size
```

### Testnet Parameters

```cpp
// BIP9 Deployment Configuration
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].bit = 10;
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nStartTime = 1745000000;  // ~February 2025
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nTimeout = 1776536000;    // Start + 1 year

// CVM Parameters (already active)
consensus.cvmActivationHeight = 500;
consensus.cvmMaxGasPerBlock = 10000000;
consensus.cvmMaxGasPerTx = 1000000;
consensus.cvmMaxCodeSize = 24576;
```

### Regtest Parameters

```cpp
// Always active for development/testing
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
consensus.vDeployments[Consensus::DEPLOYMENT_CVM_EVM].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

// CVM Parameters (immediately active)
consensus.cvmActivationHeight = 0;
```

## Activation Mechanism

### BIP9 Version Bits Activation

The CVM-EVM enhancement uses BIP9 version bits for activation:

1. **Signaling Period**: Miners signal support by setting bit 10 in block version
2. **Activation Threshold**: 75% of blocks in a 8064-block window must signal
3. **Lock-in**: Once threshold reached, activation is locked in
4. **Activation**: Features become active after one more retarget period

### Activation States

| State | Description |
|-------|-------------|
| DEFINED | Deployment is defined but not yet started |
| STARTED | Signaling period has begun |
| LOCKED_IN | Threshold reached, activation pending |
| ACTIVE | Features are active |
| FAILED | Timeout reached without activation |

### Checking Activation Status

```bash
# Check deployment status
./src/cascoin-cli -testnet getblockchaininfo

# Look for "cvm_evm" in the "softforks" section
```

## Pre-Deployment Checklist

### Code Readiness

- [x] EVMC integration complete
- [x] Trust-aware operations implemented
- [x] Sustainable gas system implemented
- [x] HAT v2 consensus validator implemented
- [x] Validator attestation system implemented
- [x] P2P protocol integration complete
- [x] RPC interface extended
- [x] Unit tests passing
- [x] Integration tests passing
- [x] Security analysis complete
- [x] Monitoring and observability implemented
- [x] Graceful degradation implemented
- [x] Security audit checklist complete

### Documentation Readiness

- [x] Developer documentation (doc/CVM_EVM_DEVELOPER_GUIDE.md)
- [x] Operator documentation (doc/CVM_EVM_OPERATOR_GUIDE.md)
- [x] Security documentation (doc/CVM_SECURITY_AUDIT_CHECKLIST.md)
- [x] Deployment plan (this document)

### Infrastructure Readiness

- [ ] Testnet seed nodes updated
- [ ] Block explorers updated for EVM transactions
- [ ] Wallet software updated
- [ ] Mining pool software updated (if applicable)

## Testnet Deployment Procedure

### Step 1: Build and Deploy

```bash
# Build with CVM-EVM support
./autogen.sh
./configure --with-gui=qt6 --enable-wallet
make -j$(nproc)

# Start testnet node
./src/cascoind -testnet -daemon

# Verify deployment status
./src/cascoin-cli -testnet getblockchaininfo
```

### Step 2: Verify Activation

```bash
# Check if CVM-EVM is active
./src/cascoin-cli -testnet getblockchaininfo | grep cvm_evm

# Expected output after activation:
# "cvm_evm": {
#   "status": "active",
#   "startTime": 1745000000,
#   "timeout": 1776536000,
#   "since": <block_height>
# }
```

### Step 3: Test Core Functionality

```bash
# Deploy a test contract
./src/cascoin-cli -testnet deploycontract <bytecode> <gas_limit>

# Call a contract
./src/cascoin-cli -testnet callcontract <address> <data> <gas_limit>

# Check reputation-based gas discount
./src/cascoin-cli -testnet cas_getReputationDiscount <address>

# Check free gas allowance
./src/cascoin-cli -testnet cas_getFreeGasAllowance <address>
```

### Step 4: Monitor System Health

```bash
# Check metrics endpoint (if enabled)
curl http://localhost:9100/metrics

# Check graceful degradation status
./src/cascoin-cli -testnet getdegradationstatus

# Check HAT v2 validation status
./src/cascoin-cli -testnet getvalidatorstats
```

## Mainnet Deployment Procedure

### Step 1: Pre-Activation Preparation

1. **Announce Activation**: Notify community of upcoming activation
2. **Update Documentation**: Ensure all docs reflect mainnet parameters
3. **Coordinate with Miners**: Ensure mining pools are ready to signal
4. **Prepare Support**: Set up channels for user support

### Step 2: Monitor Signaling

```bash
# Check signaling progress
./src/cascoin-cli getblockchaininfo

# Monitor version bits in recent blocks
./src/cascoin-cli getblock $(./src/cascoin-cli getbestblockhash) | grep version
```

### Step 3: Post-Activation Monitoring

1. **Network Stability**: Monitor for chain splits or consensus issues
2. **Transaction Processing**: Verify EVM transactions are processed correctly
3. **Gas System**: Confirm reputation-based discounts are applied
4. **HAT v2 Consensus**: Monitor validator participation and consensus rates

## Rollback Procedure

In case of critical issues, the following rollback options are available:

### Option 1: Emergency Soft Fork

If a critical bug is discovered after activation:
1. Release patched version with bug fix
2. Coordinate with miners for rapid deployment
3. Use emergency mode in graceful degradation if needed

### Option 2: Feature Disable

Individual features can be disabled via configuration:
```bash
# Disable specific subsystems
./src/cascoind -disablecvmevm=1
```

### Option 3: Chain Rollback

As a last resort (requires community consensus):
1. Identify safe rollback point
2. Coordinate with miners and exchanges
3. Execute coordinated rollback

## Support and Resources

### Documentation
- Developer Guide: `doc/CVM_EVM_DEVELOPER_GUIDE.md`
- Operator Guide: `doc/CVM_EVM_OPERATOR_GUIDE.md`
- Security Checklist: `doc/CVM_SECURITY_AUDIT_CHECKLIST.md`

### Community Channels
- Discord: [Cascoin Discord Server]
- Telegram: [Cascoin Telegram Group]
- GitHub Issues: [Cascoin Core Repository]

### Emergency Contacts
- Core Developers: [Contact Information]
- Security Team: [Security Contact]

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | December 2024 | Initial deployment plan |

## Conclusion

The CVM-EVM enhancement deployment follows a careful, phased approach to ensure network stability and security. Testnet deployment allows for thorough testing before mainnet activation. The BIP9 activation mechanism ensures community consensus before features go live.

**Current Status**: Ready for testnet deployment. Mainnet activation pending successful testnet validation and external security audit.
