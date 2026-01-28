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

Yes. FALCON-512 signatures are approximately 666 bytes compared to 64-72 bytes for ECDSA. Transaction fees are calculated based on virtual size including the signature.

### Q: Can I use quantum keys with hardware wallets?

Hardware wallet support depends on the manufacturer. Check with your hardware wallet provider for FALCON-512 support.

## Troubleshooting

### "Quantum features are not yet active"

The network hasn't reached the activation height. Wait for the activation block.

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
