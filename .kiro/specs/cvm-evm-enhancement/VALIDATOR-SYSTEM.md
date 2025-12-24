# HAT v2 Validator System

## Overview

Permissionless validator network for HAT v2 consensus. No registration, no approval - meet criteria and participate.

## Eligibility Criteria

- **Stake**: ≥10 CAS, aged 70+ days, from 3+ sources
- **History**: 70+ days on-chain, 100+ transactions, 20+ unique interactions  
- **Network**: 1000+ blocks connected, 1+ peer (home users welcome)
- **Anti-Sybil**: No wallet clustering, diverse network topology

## Attestation System

Validators prove eligibility through distributed attestations (like 2FA):

1. Validator announces eligibility with evidence
2. 10+ random attestors verify claims from their WoT perspective
3. 80%+ agreement on objective criteria + average HAT v2 score ≥50 = eligible
4. Attestations cached for ~17 hours (10,000 blocks)

## Validator Selection

For each transaction requiring validation:
1. Deterministic seed from txHash + blockHeight
2. Query eligible validators from database
3. Shuffle with deterministic randomness
4. Select minimum 10 validators
5. Broadcast challenge via gossip protocol

## Gas Fee Compensation (70/30 Split)

- **70% → Miner**: Block reward + gas fees
- **30% → Validators**: Split equally among participants

**Passive Income Example** (1000 tx/day, 0.1 CAS/tx, 50% participation):
- Daily: ~1.5 CAS per validator
- Annual: ~547 CAS (5,475% APY on 10 CAS stake)

## Sybil Protection

**Cross-Validation**: Minimum 40% non-WoT validators verify objective components
**Network Diversity**: Different IP subnets, <50% peer overlap
**History Requirements**: 70+ days presence, 50+ validations with 85%+ accuracy
**Economic Cost**: Creating 100 Sybil validators costs ~$50,000+ and 70+ days

## Key Files

- `src/cvm/validator_attestation.cpp/h` - Attestation system
- `src/cvm/validator_compensation.cpp/h` - Fee distribution
- `src/cvm/hat_consensus.cpp/h` - Consensus validation
- `src/cvm/securehat.cpp/h` - HAT v2 scoring
