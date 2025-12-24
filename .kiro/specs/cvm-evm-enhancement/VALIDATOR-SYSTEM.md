# HAT v2 Validator System

## Overview

Fully automatic, permissionless validator network for HAT v2 consensus. 

**Key Principle: NO registration, NO announcement required!**

Validators are automatically discovered from on-chain data and randomly selected for validation tasks. Users don't need to do anything - if their address meets the eligibility criteria, they automatically become part of the validator pool.

## How It Works

### Automatic Discovery
1. System scans blockchain for addresses meeting eligibility criteria
2. Eligible addresses are automatically added to validator pool
3. Pool is updated every 100 blocks (~4 hours)
4. No user action required - completely passive

### Random Selection
1. For each validation task, a deterministic seed is created from:
   - Transaction hash
   - Block height  
   - Block hash (for additional randomness)
2. Validators are selected using Fisher-Yates shuffle with deterministic randomness
3. Minimum 10 validators selected per task
4. Selection is verifiable by any node (same seed = same selection)

### Validation Flow
1. Task is broadcast to network
2. Selected validators automatically receive notification
3. Validators respond with their verdict (valid/invalid + confidence)
4. Consensus requires 2/3 majority with 60%+ response rate
5. Participating validators receive compensation

## Eligibility Criteria (Computed Automatically)

All criteria are verified from on-chain data - no self-reporting:

- **Stake**: ≥10 CAS in UTXOs
- **Stake Age**: Oldest UTXO ≥70 days (40,320 blocks)
- **History**: First transaction ≥70 days ago
- **Transactions**: 100+ total transactions
- **Interactions**: 20+ unique addresses interacted with

## Gas Fee Compensation (70/30 Split)

- **70% → Miner**: Block reward + gas fees
- **30% → Validators**: Split equally among responding validators

**Passive Income Example** (1000 tx/day, 0.1 CAS/tx, 50% participation):
- Daily: ~1.5 CAS per validator
- Annual: ~547 CAS (5,475% APY on 10 CAS stake)

## Sybil Protection

**Economic Cost**: Creating eligible Sybil addresses requires:
- 10 CAS × N addresses = significant capital
- 70+ days waiting period per address
- 100+ real transactions per address
- 20+ unique interactions per address

**Estimated Cost for 100 Sybil Validators**: ~$50,000+ and 70+ days minimum

## Key Files

- `src/cvm/validator_attestation.cpp/h` - Automatic validator selection
- `src/cvm/validator_compensation.cpp/h` - Fee distribution
- `src/cvm/hat_consensus.cpp/h` - Consensus validation

## Benefits of Automatic Selection

1. **No Gatekeeping**: Anyone meeting criteria participates automatically
2. **Truly Decentralized**: No registration = no central authority
3. **Fair**: Deterministic randomness ensures equal opportunity
4. **Passive**: Users earn fees without active management
5. **Sybil Resistant**: On-chain history requirements are expensive to fake
