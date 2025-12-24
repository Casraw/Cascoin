# Validator Compensation

## Gas Fee Split: 70/30

- **70% → Miner**: Mines block, assembles transactions
- **30% → Validators**: Verify reputation, split equally among participants

## Coinbase Structure

```
Output 0: Miner (block reward + 70% gas fees)
Output 1-N: Validators (share of 30% gas fees each)
```

## Passive Income

**Setup**: 10 CAS stake + node running (~$10/month VPS)

**Returns** (at 1000 tx/day, 0.1 CAS/tx):
- Daily: ~1.5 CAS
- Monthly: ~45 CAS  
- Annual: ~547 CAS (5,475% APY)

## Implementation

- `src/cvm/validator_compensation.cpp/h` - Fee calculation
- `src/cvm/cvmdb.cpp` - Validator participation tracking
- Consensus rules verify correct payments in coinbase
