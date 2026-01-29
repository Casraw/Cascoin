# Quantum Migration Guide

This guide explains how to migrate your Cascoin holdings from legacy (ECDSA) addresses to quantum-safe (FALCON-512) addresses.

## Overview

Cascoin implements Post-Quantum Cryptography (PQC) using FALCON-512, a lattice-based digital signature algorithm selected by NIST for post-quantum standardization. This upgrade provides protection against future quantum computing threats while maintaining full backward compatibility with existing transactions.

## Activation Timeline

| Network  | Activation Height | Estimated Date |
|----------|-------------------|----------------|
| Mainnet  | 350,000           | TBD            |
| Testnet  | 50,000            | TBD            |
| Regtest  | 1                 | Immediate      |

Current block heights (approximate):
- Mainnet: ~276,606
- Testnet: ~5,377

## Address Formats

### Legacy Addresses (ECDSA)
- Base58 addresses starting with `L` or `C`
- Bech32 addresses starting with `cas1` (witness v0/v1)

### Quantum Addresses (FALCON-512)
- Bech32m addresses with distinct Human Readable Parts (HRP):
  - **Mainnet**: `casq1...`
  - **Testnet**: `tcasq1...`
  - **Regtest**: `rcasq1...`

Quantum addresses use witness version 2 and are visually distinguishable from legacy addresses.

## Migration Process

### Step 1: Update Your Node

Ensure you're running a quantum-enabled version of Cascoin Core:

```bash
cascoind --version
```

The version should indicate quantum support is enabled.

### Step 2: Check Activation Status

Verify that quantum features are active on the network:

```bash
cascoin-cli getblockcount
```

Compare the result with the activation height for your network.

### Step 3: Generate a Quantum Address

After activation, `getnewaddress` returns quantum addresses by default:

```bash
# Get a new quantum address (default after activation)
cascoin-cli getnewaddress

# Explicitly request a quantum address
cascoin-cli getnewaddress "" "quantum"

# Get a legacy address if needed
cascoin-cli getlegacyaddress
```

### Step 4: Migrate Your Funds

Use the `migrate_to_quantum` command to sweep legacy UTXOs to a quantum address:

```bash
# Migrate all legacy UTXOs to a new quantum address
cascoin-cli migrate_to_quantum

# Migrate to a specific quantum address
cascoin-cli migrate_to_quantum "casq1..."

# Migrate only confirmed UTXOs
cascoin-cli migrate_to_quantum "" false
```

The command returns:
```json
{
  "txid": "...",
  "destination": "casq1...",
  "amount": 100.00000000,
  "fee": 0.00001234,
  "utxos_migrated": 5
}
```

## RPC Commands Reference

### getnewaddress

Returns a new address for receiving payments.

```
getnewaddress ( "account" "address_type" )
```

**Arguments:**
1. `account` (string, optional) - DEPRECATED. Account name.
2. `address_type` (string, optional) - Options: "legacy", "p2sh-segwit", "bech32", "quantum". Default is "quantum" after activation.

**Example:**
```bash
cascoin-cli getnewaddress "" "quantum"
```

### getlegacyaddress

Returns a new legacy (ECDSA) address for backward compatibility.

```
getlegacyaddress ( "account" "address_type" )
```

**Arguments:**
1. `account` (string, optional) - DEPRECATED. Account name.
2. `address_type` (string, optional) - Options: "legacy", "p2sh-segwit", "bech32".

**Example:**
```bash
cascoin-cli getlegacyaddress
```

### migrate_to_quantum

Migrates funds from legacy UTXOs to a quantum-safe address.

```
migrate_to_quantum ( "destination" include_all )
```

**Arguments:**
1. `destination` (string, optional) - Quantum destination address. If not provided, generates a new one.
2. `include_all` (boolean, optional, default=true) - Include unconfirmed UTXOs.

**Example:**
```bash
cascoin-cli migrate_to_quantum
```

### importquantumkey

Imports a quantum (FALCON-512) private key.

```
importquantumkey "quantumkey" ( "label" ) ( rescan )
```

**Arguments:**
1. `quantumkey` (string, required) - Key in format: `QKEY:<hex_privkey>:<hex_pubkey>`
2. `label` (string, optional) - Label for the address.
3. `rescan` (boolean, optional, default=true) - Rescan wallet for transactions.

**Example:**
```bash
cascoin-cli importquantumkey "QKEY:..." "my_imported_key" false
```

## Wallet Backup

After migrating to quantum addresses, create a new wallet backup:

```bash
cascoin-cli dumpwallet "wallet_backup.txt"
```

The backup includes both ECDSA and quantum keys. Quantum keys are stored in the format:
```
QKEY:<hex_privkey>:<hex_pubkey>
```

## Hive Mining with Quantum Keys

After activation, new Hive mining agents default to quantum keys:

1. **New agents**: Automatically use FALCON-512 keys
2. **Existing agents**: Continue to work with ECDSA keys
3. **Migration**: Existing agents can be migrated to quantum keys

## CVM Smart Contracts

CVM contracts support both signature types:

| Opcode | Gas Cost | Description |
|--------|----------|-------------|
| `VERIFY_SIG` | 60-3000 | Auto-detects signature type |
| `VERIFY_SIG_ECDSA` (0x61) | 60 | Explicit ECDSA verification |
| `VERIFY_SIG_QUANTUM` (0x60) | 3000 | Explicit FALCON-512 verification |

Signature type is detected by size:
- ≤72 bytes: ECDSA
- >100 bytes: FALCON-512

## Technical Specifications

### FALCON-512 Public Key Registry

The Public Key Registry provides storage optimization for quantum transactions by storing 897-byte FALCON-512 public keys once on-chain and referencing them by 32-byte SHA256 hash in subsequent transactions.

#### Transaction Types

| Type | Marker | Witness Format | Size |
|------|--------|----------------|------|
| Registration | 0x51 | `[0x51][897-byte PubKey][Signature]` | ~1598 bytes |
| Reference | 0x52 | `[0x52][32-byte Hash][Signature]` | ~733 bytes |

**Storage Savings:** ~55% reduction (from ~1563 to ~698 bytes after initial registration)

#### How It Works

1. **First Transaction**: When you send from a quantum address for the first time, the full 897-byte public key is included in the witness (0x51 marker). The node automatically registers this key in the registry.

2. **Subsequent Transactions**: All following transactions from the same address use only the 32-byte hash reference (0x52 marker). Validators look up the full public key from the registry.

3. **Automatic Process**: This optimization is completely automatic. Wallets handle the registration and reference logic transparently.

#### Registry RPC Commands

```bash
# Look up a registered public key by hash
cascoin-cli getquantumpubkey "hash"

# Check registry statistics
cascoin-cli getquantumregistrystats

# Check if a public key is registered
cascoin-cli isquantumpubkeyregistered "hash"
```

#### Performance

- LRU Cache: 1000 entries for frequently accessed keys
- Cache lookup: <1ms
- Database lookup: <10ms
- Database location: `{datadir}/quantum_pubkeys`

#### Recovery

If the registry database becomes corrupted, rebuild it from blockchain data:
```bash
cascoind -rebuildquantumregistry
```

### Key Sizes
| Type | Private Key | Public Key | Signature |
|------|-------------|------------|-----------|
| ECDSA | 32 bytes | 33/65 bytes | 64-72 bytes |
| FALCON-512 | 1,281 bytes | 897 bytes | ~666 bytes (max 700) |

### Security Level
- FALCON-512 provides NIST Level 1 security (128-bit quantum security)
- Constant-time operations prevent timing attacks
- Canonical signature validation prevents malleability

### Performance
- Signature generation: <10ms
- Signature verification: <2ms

## FAQ

### Q: Do I need to migrate immediately?

No. Legacy addresses remain fully functional. However, migrating to quantum addresses provides protection against future quantum computing threats.

### Q: Will my existing transactions be affected?

No. All pre-activation transactions remain valid. The upgrade is backward compatible.

### Q: Can I still receive funds at legacy addresses?

Yes. Legacy addresses continue to work normally. You can use `getlegacyaddress` to generate new legacy addresses if needed.

### Q: What happens if I send to a quantum address before activation?

Before activation, witness version 2 outputs are treated as anyone-can-spend for soft fork compatibility. Do not send to quantum addresses before activation.

### Q: How do I know if my node supports quantum?

Check the version message or use:
```bash
cascoin-cli getnetworkinfo
```
Look for `NODE_QUANTUM` in the service flags.

### Q: Are quantum transactions larger?

Yes, but only for the first transaction. FALCON-512 signatures are approximately 666 bytes compared to 64-72 bytes for ECDSA. However, the Public Key Registry optimizes subsequent transactions:

- **First transaction**: ~1598 bytes (includes full 897-byte public key)
- **Subsequent transactions**: ~733 bytes (uses 32-byte hash reference)

This represents a ~55% size reduction after the initial registration. Transaction fees are calculated based on virtual size including the signature.

### Q: Can I use quantum keys with hardware wallets?

Hardware wallet support depends on the manufacturer. Check with your hardware wallet provider for FALCON-512 support.

## Troubleshooting

### "Quantum features are not yet active"

The network hasn't reached the activation height. Wait for the activation block.

### "Quantum public key not registered"

This error occurs when a reference transaction (0x52) tries to use a public key hash that isn't in the registry. This can happen if:
- The registry database is corrupted (use `-rebuildquantumregistry` to fix)
- The transaction was created incorrectly (should use 0x51 for first transaction)

### "Invalid quantum witness marker"

The witness contains an invalid marker byte (not 0x51 or 0x52). This indicates a malformed transaction.

### "Public key does not match quantum address"

The SHA256 hash of the public key doesn't match the quantum address program. This could indicate:
- A corrupted public key in the registry
- An incorrectly constructed transaction

### "Quantum keypool ran out"

Run `keypoolrefill` to generate more quantum keys:
```bash
cascoin-cli keypoolrefill
```

### "No legacy UTXOs available to migrate"

All your funds are already in quantum addresses, or you have no confirmed UTXOs.

### "Insufficient funds for migration"

The migration requires a transaction fee. Ensure you have enough balance to cover the fee.

## Support

For additional help:
- GitHub Issues: https://github.com/AquaCash/cascoin/issues
- Documentation: https://github.com/AquaCash/cascoin/tree/master/doc

## See Also

- [CVM Developer Guide](CVM_DEVELOPER_GUIDE.md)
- [Web-of-Trust Documentation](../WEB_OF_TRUST.md)
- [Build Instructions](build-linux.md)
