# Cascoin CVM Security Guide

This guide documents the HAT v2 consensus security model and provides guidance for validator node operators.

---

## Table of Contents

1. [Security Overview](#security-overview)
2. [HAT v2 Consensus Security Model](#hat-v2-consensus-security-model)
3. [Validator Operations](#validator-operations)
4. [Attack Vectors and Mitigations](#attack-vectors-and-mitigations)
5. [Security Best Practices](#security-best-practices)
6. [Incident Response](#incident-response)

---

## Security Overview

Cascoin's CVM implementation includes multiple security layers:

1. **HAT v2 Consensus**: Distributed reputation verification
2. **Permissionless Validators**: Decentralized validation network
3. **Anti-Sybil Measures**: Wallet clustering and behavior analysis
4. **DAO Dispute Resolution**: Community-based arbitration
5. **Economic Security**: Stake requirements and slashing

### Security Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Security Layer Stack                              │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 5: DAO Dispute Resolution                                    │
│  - Community arbitration for disputed transactions                  │
│  - Stake-weighted voting                                            │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 4: Anti-Manipulation Detection                               │
│  - Vote manipulation detection                                      │
│  - Trust graph manipulation detection                               │
│  - Coordinated attack detection                                     │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 3: Eclipse/Sybil Protection                                  │
│  - Network topology diversity                                       │
│  - Peer connection diversity                                        │
│  - Stake source diversity                                           │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 2: HAT v2 Consensus Validation                               │
│  - Random validator selection                                       │
│  - Challenge-response protocol                                      │
│  - Component-based verification                                     │
├─────────────────────────────────────────────────────────────────────┤
│  Layer 1: Cryptographic Foundation                                  │
│  - Digital signatures                                               │
│  - Hash commitments                                                 │
│  - Deterministic randomness                                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## HAT v2 Consensus Security Model

### Overview

HAT v2 (Hybrid Adaptive Trust) consensus prevents reputation fraud through distributed verification. When a transaction claims a reputation score, multiple validators independently verify the claim.

### Consensus Flow

```
Transaction Submission
        │
        ▼
┌───────────────────┐
│ Self-Reported     │
│ HAT v2 Score      │
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ Random Validator  │
│ Selection (10+)   │
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ Challenge-Response│
│ Protocol          │
└───────────────────┘
        │
        ▼
┌───────────────────┐
│ Component-Based   │
│ Verification      │
└───────────────────┘
        │
        ├─────────────────────┐
        ▼                     ▼
┌───────────────┐     ┌───────────────┐
│ Consensus     │     │ No Consensus  │
│ Reached (70%+)│     │ (<70%)        │
└───────────────┘     └───────────────┘
        │                     │
        ▼                     ▼
┌───────────────┐     ┌───────────────┐
│ Transaction   │     │ DAO Dispute   │
│ Validated     │     │ Escalation    │
└───────────────┘     └───────────────┘
```

### HAT v2 Score Components

The HAT v2 score consists of four components:

| Component | Weight | Description |
|-----------|--------|-------------|
| Behavior | 40% | Transaction patterns, diversity, volume |
| Web-of-Trust | 30% | Trust paths from validator's perspective |
| Economic | 20% | Stake amount, stake age, time weight |
| Temporal | 10% | Account age, activity patterns |

### Component-Based Verification

Validators verify scores based on their connectivity:

**Validators WITH WoT Connection:**
- Verify ALL components (Behavior, WoT, Economic, Temporal)
- WoT tolerance: ±5 points
- Non-WoT tolerance: ±3 points per component
- Vote confidence: 1.0

**Validators WITHOUT WoT Connection:**
- Verify only non-WoT components (Behavior, Economic, Temporal)
- WoT component is IGNORED
- Non-WoT tolerance: ±3 points per component
- Vote confidence: 0.6

### Consensus Thresholds

| Threshold | Value | Description |
|-----------|-------|-------------|
| Minimum Validators | 10 | Required for consensus |
| Acceptance Rate | 70% | Weighted votes to accept |
| WoT Coverage | 30% | Minimum validators with WoT |
| Non-WoT Minimum | 40% | Minimum validators without WoT |
| Response Timeout | 30s | Maximum wait for responses |

### Weighted Voting

```
Weighted Accept = Σ(validator_confidence × accept_vote)
Weighted Reject = Σ(validator_confidence × reject_vote)
Total Weight = Weighted Accept + Weighted Reject

Consensus = (Weighted Accept / Total Weight) >= 0.70
```

---

## Validator Operations

### Becoming a Validator

The validator network is permissionless. Any node can become a validator by meeting eligibility criteria:

**Eligibility Requirements:**

| Requirement | Value | Description |
|-------------|-------|-------------|
| HAT v2 Score | ≥50 | From attestor consensus |
| Stake | ≥10 CAS | Minimum stake amount |
| Stake Age | ≥70 days | Stake must be aged |
| On-Chain History | ≥70 days | Account presence |
| Transactions | ≥100 | Historical transactions |
| Unique Interactions | ≥20 | Different addresses |
| Network Participation | ≥1000 blocks | Connected to network |

### Validator Registration

```bash
# Generate validator key
./cascoin-cli -testnet generatevalidatorkey

# Announce validator status
./cascoin-cli -testnet announcevalidator

# Check validator status
./cascoin-cli -testnet getvalidatorinfo
```

### Validator Key Management

```bash
# Key location
~/.cascoin/testnet3/validator.key

# Secure permissions
chmod 600 ~/.cascoin/testnet3/validator.key

# Backup key securely
gpg -c ~/.cascoin/testnet3/validator.key
```

**Security Best Practices:**
- Never share validator private key
- Use hardware security module (HSM) for production
- Rotate keys periodically
- Monitor for unauthorized access

### Validator Configuration

```ini
# cascoin.conf
validator=1                      # Enable validator mode
validatorkey=/path/to/key        # Validator key file
validatorstake=10                # Stake amount (CAS)
validatortimeout=30              # Response timeout (seconds)
validatormaxpending=100          # Max pending validations
```

### Validator Compensation

Validators earn fees for participation:

| Fee Type | Distribution |
|----------|--------------|
| Gas Fees | 30% to validators (split equally) |
| Miner Fees | 70% to block miner |

```bash
# Check validator earnings
./cascoin-cli -testnet getvalidatorearnings "VALIDATOR_ADDRESS"

# Get network-wide validator stats
./cascoin-cli -testnet getvalidatorstats
```

### Validator Accountability

Validators are held accountable for accuracy:

| Behavior | Consequence |
|----------|-------------|
| Accurate validation | Reputation increase |
| Inaccurate validation | Reputation decrease |
| Consistent accuracy (85%+) | Eligibility maintained |
| Low accuracy (<85%) | Eligibility revoked |
| Malicious behavior | Stake slashing |

---

## Attack Vectors and Mitigations

### 1. Sybil Attacks

**Attack**: Create multiple fake identities to control validation.

**Mitigations:**
- Stake requirements (10 CAS minimum)
- Stake age requirements (70 days)
- Wallet clustering detection
- Network topology diversity
- Peer connection diversity

**Detection:**
```bash
# Check for Sybil patterns
./cascoin-cli -testnet detectsybilnetwork "ADDRESS"

# Get Sybil risk score
./cascoin-cli -testnet getsybilriskscore "ADDRESS"
```

### 2. Eclipse Attacks

**Attack**: Isolate a node from honest network to manipulate its view.

**Mitigations:**
- IP subnet diversity (different /16 subnets)
- Peer connection diversity (<50% overlap)
- Multiple validator perspectives
- Cross-group agreement requirements

**Configuration:**
```ini
# cascoin.conf
maxconnections=125               # More connections = harder to eclipse
dnsseed=1                        # Use DNS seeds
addnode=trusted-node.example.com # Add trusted peers
```

### 3. Reputation Inflation

**Attack**: Artificially inflate reputation score to gain benefits.

**Mitigations:**
- Component-based verification
- Multiple validator perspectives
- Behavior analysis
- Temporal decay
- Fraud recording

**Detection:**
```bash
# Analyze reputation manipulation
./cascoin-cli -testnet analyzereputation "ADDRESS"

# Check fraud history
./cascoin-cli -testnet getfraudrecords "ADDRESS"
```

### 4. Vote Manipulation

**Attack**: Coordinate validators to approve fraudulent transactions.

**Mitigations:**
- Random validator selection
- Minimum 40% non-WoT validators
- Cross-group agreement (60% threshold)
- Timing correlation detection
- Pattern similarity detection

**Detection:**
```bash
# Detect vote manipulation
./cascoin-cli -testnet detectvotemanipulation "TXHASH"

# Analyze voting patterns
./cascoin-cli -testnet analyzevotingpatterns "VALIDATOR_ADDRESS"
```

### 5. Trust Graph Manipulation

**Attack**: Create artificial trust paths to inflate WoT scores.

**Mitigations:**
- Trust edge bonding
- Path strength decay
- Cluster detection
- Temporal analysis
- DAO oversight

**Detection:**
```bash
# Detect trust graph manipulation
./cascoin-cli -testnet detecttrustmanipulation "ADDRESS"

# Analyze trust paths
./cascoin-cli -testnet analyzetrustpaths "FROM" "TO"
```

### 6. Validator Collusion

**Attack**: Multiple validators coordinate to approve/reject transactions.

**Mitigations:**
- Random selection (unpredictable)
- Minimum 10 validators
- Diverse perspectives required
- DAO escalation for disputes
- Economic penalties

**Detection:**
```bash
# Detect validator collusion
./cascoin-cli -testnet detectvalidatorcollusion "TXHASH"
```

### Attack Cost Analysis

| Attack | Without Protection | With Protection |
|--------|-------------------|-----------------|
| Sybil Network | ~$1,000, 1 day | ~$50,000+, 70+ days |
| Eclipse Attack | ~$500, hours | ~$10,000+, weeks |
| Reputation Inflation | ~$100, days | ~$5,000+, months |
| Validator Collusion | ~$1,000, days | ~$100,000+, months |

---

## Security Best Practices

### For Node Operators

1. **Keep Software Updated**
   ```bash
   # Check for updates regularly
   git pull origin master
   make clean && make
   ```

2. **Secure RPC Access**
   ```ini
   rpcallowip=127.0.0.1
   rpcbind=127.0.0.1
   rpcuser=<strong_random>
   rpcpassword=<strong_random>
   ```

3. **Enable Firewall**
   ```bash
   ufw allow 18333/tcp  # P2P
   ufw deny 18332/tcp   # Block external RPC
   ```

4. **Monitor Logs**
   ```bash
   tail -f ~/.cascoin/testnet3/debug.log | grep -E "(WARN|ERROR|SECURITY)"
   ```

5. **Regular Backups**
   ```bash
   # Daily backup script
   tar -czvf backup-$(date +%Y%m%d).tar.gz \
       ~/.cascoin/testnet3/wallet.dat \
       ~/.cascoin/testnet3/validator.key
   ```

### For Validators

1. **Protect Validator Keys**
   - Use hardware security modules (HSM)
   - Never expose keys to network
   - Rotate keys periodically

2. **Maintain High Uptime**
   - Use reliable infrastructure
   - Implement monitoring and alerting
   - Have failover procedures

3. **Stay Connected**
   - Maintain diverse peer connections
   - Monitor network health
   - Respond to challenges promptly

4. **Monitor Accuracy**
   ```bash
   # Check your accuracy rate
   ./cascoin-cli -testnet getvalidatorstats "YOUR_ADDRESS"
   ```

### For Developers

1. **Audit Smart Contracts**
   - Use formal verification tools
   - Get third-party audits
   - Test on testnet first

2. **Handle Reputation Safely**
   ```solidity
   // Always check reputation query success
   (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(...);
   require(success, "Reputation query failed");
   ```

3. **Implement Rate Limiting**
   - Limit sensitive operations
   - Use reputation thresholds
   - Monitor for abuse

---

## Incident Response

### Security Incident Types

| Type | Severity | Response Time |
|------|----------|---------------|
| Consensus Failure | Critical | Immediate |
| Sybil Attack Detected | High | < 1 hour |
| Validator Compromise | High | < 1 hour |
| Reputation Fraud | Medium | < 24 hours |
| DoS Attack | Medium | < 1 hour |

### Response Procedures

#### Consensus Failure

1. **Identify**: Check logs for consensus errors
   ```bash
   grep "consensus" ~/.cascoin/testnet3/debug.log
   ```

2. **Isolate**: Stop accepting new transactions
   ```bash
   ./cascoin-cli -testnet setnetworkactive false
   ```

3. **Investigate**: Analyze validator responses
   ```bash
   ./cascoin-cli -testnet gethatinfo
   ```

4. **Resolve**: Restart with additional validators or escalate to DAO

#### Sybil Attack

1. **Detect**: Monitor Sybil alerts
   ```bash
   ./cascoin-cli -testnet listsybilalerts
   ```

2. **Analyze**: Get cluster information
   ```bash
   ./cascoin-cli -testnet getwalletcluster "ADDRESS"
   ```

3. **Mitigate**: Apply penalties
   ```bash
   ./cascoin-cli -testnet applysybilpenalty "CLUSTER_ID"
   ```

4. **Report**: Create DAO dispute if needed

#### Validator Compromise

1. **Revoke**: Immediately revoke validator status
   ```bash
   ./cascoin-cli -testnet revokevalidator "ADDRESS"
   ```

2. **Rotate**: Generate new validator key
   ```bash
   ./cascoin-cli -testnet generatevalidatorkey
   ```

3. **Investigate**: Check for unauthorized access

4. **Report**: Notify network of compromise

### Emergency Contacts

- **Security Team**: security@cascoin.example
- **DAO Council**: dao@cascoin.example
- **Bug Bounty**: bounty@cascoin.example

### Bug Bounty Program

Report security vulnerabilities responsibly:

| Severity | Reward |
|----------|--------|
| Critical | Up to 50,000 CAS |
| High | Up to 20,000 CAS |
| Medium | Up to 5,000 CAS |
| Low | Up to 1,000 CAS |

---

## Security Monitoring

### Key Metrics to Monitor

| Metric | Normal Range | Alert Threshold |
|--------|--------------|-----------------|
| Validator Count | 50-500 | < 20 |
| Consensus Success Rate | > 95% | < 90% |
| Average Response Time | < 5s | > 15s |
| Sybil Alerts/Day | 0-5 | > 20 |
| Fraud Records/Day | 0-2 | > 10 |

### Monitoring Commands

```bash
# Overall security status
./cascoin-cli -testnet getsecuritystatus

# HAT consensus metrics
./cascoin-cli -testnet gethatmetrics

# Validator network health
./cascoin-cli -testnet getvalidatornetworkhealth

# Recent security events
./cascoin-cli -testnet listsecurityevents 100
```

### Alerting Configuration

```yaml
# Example Prometheus alerting rules
groups:
  - name: cascoin_security
    rules:
      - alert: LowValidatorCount
        expr: cascoin_validators_active < 20
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "Low validator count"
          
      - alert: HighSybilAlerts
        expr: rate(cascoin_sybil_alerts_total[1h]) > 20
        for: 10m
        labels:
          severity: high
        annotations:
          summary: "High Sybil alert rate"
```

---

## Additional Resources

- [HAT v2 Technical Documentation](HAT_V2.md)
- [Trust Security Analysis](TRUST_SECURITY_ANALYSIS.md)
- [WoT Security Analysis](WOT_SECURITY_ANALYSIS.md)
- [Developer Guide](CVM_DEVELOPER_GUIDE.md)
- [Operator Guide](CVM_OPERATOR_GUIDE.md)

---

**Last Updated:** December 2025  
**Version:** 1.0  
**Status:** Production Ready ✅
