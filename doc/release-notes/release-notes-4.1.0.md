## Cascoin Core v4.1.0 – Quantum-Resistant Cryptography

### Übersicht

Cascoin Core v4.1.0 führt Post-Quantum-Kryptographie (PQC) basierend auf FALCON-512 ein. Dieses Update bietet Schutz gegen zukünftige Quantencomputer-Bedrohungen bei voller Abwärtskompatibilität mit bestehenden Transaktionen.

### Highlights

- **FALCON-512 Signaturen**: NIST-standardisierter, gitterbasierter Signaturalgorithmus
- **Neue Quantum-Adressen**: Bech32m-kodierte Adressen mit eigenem HRP (casq/tcasq/rcasq)
- **Soft-Fork Aktivierung**: Witness Version 2 für Quantum-Transaktionen
- **Automatische Migration**: RPC-Befehl zum Migrieren von Legacy-UTXOs
- **CVM Integration**: Quantum-Signaturverifikation in Smart Contracts
- **Public Key Registry**: Speicheroptimierung für Quantum-Transaktionen (~55% Größenreduktion)

### Aktivierungshöhen

| Netzwerk | Aktivierungshöhe |
|----------|------------------|
| Mainnet  | 350,000          |
| Testnet  | 5,680            |
| Regtest  | 1                |

### Neue Adressformate

**Quantum-Adressen (FALCON-512)**:
- Mainnet: `casq1...`
- Testnet: `tcasq1...`
- Regtest: `rcasq1...`

Quantum-Adressen verwenden Witness Version 2 und sind visuell von Legacy-Adressen unterscheidbar.

### Neue RPC-Befehle

#### getnewaddress (erweitert)
Nach Aktivierung werden standardmäßig Quantum-Adressen zurückgegeben:
```bash
cascoin-cli getnewaddress "" "quantum"
```

#### getlegacyaddress
Generiert weiterhin Legacy-Adressen (ECDSA) für Abwärtskompatibilität:
```bash
cascoin-cli getlegacyaddress
```

#### migrate_to_quantum
Migriert Legacy-UTXOs zu einer Quantum-Adresse:
```bash
cascoin-cli migrate_to_quantum ["destination"] [include_all]
```

**Rückgabe:**
```json
{
  "txid": "...",
  "destination": "casq1...",
  "amount": 100.00000000,
  "fee": 0.00001234,
  "utxos_migrated": 5
}
```

#### importquantumkey
Importiert einen Quantum-Privatschlüssel:
```bash
cascoin-cli importquantumkey "QKEY:<hex_privkey>:<hex_pubkey>" "label" false
```

### Quantum Public Key Registry RPC-Befehle

Die Public Key Registry ermöglicht kompakte Quantum-Transaktionen durch einmalige Speicherung der 897-Byte FALCON-512 Public Keys.

#### getquantumpubkey
Ruft einen registrierten Public Key anhand seines SHA256-Hashes ab:
```bash
cascoin-cli getquantumpubkey "hash"
```

**Rückgabe:**
```json
{
  "pubkey": "hex",
  "hash": "hex"
}
```

#### getquantumregistrystats
Zeigt Statistiken der Public Key Registry:
```bash
cascoin-cli getquantumregistrystats
```

**Rückgabe:**
```json
{
  "total_keys": 1234,
  "database_size": 1234567,
  "cache_hits": 5678,
  "cache_misses": 123
}
```

#### isquantumpubkeyregistered
Prüft, ob ein Public Key Hash registriert ist:
```bash
cascoin-cli isquantumpubkeyregistered "hash"
```

**Rückgabe:** `true` oder `false`

### Technische Spezifikationen

#### FALCON-512 Public Key Registry

Die Registry speichert FALCON-512 Public Keys einmalig on-chain und ermöglicht Referenzierung per 32-Byte SHA256-Hash in Folgetransaktionen.

**Transaktionsformate:**

| Typ | Witness-Format | Größe |
|-----|----------------|-------|
| Registration (0x51) | `[0x51][897-byte PubKey][Signatur]` | ~1598 Bytes |
| Reference (0x52) | `[0x52][32-byte Hash][Signatur]` | ~733 Bytes |

**Speicherersparnis:** ~55% (von ~1563 auf ~698 Bytes nach Erstregistrierung)

**Performance:**
- LRU-Cache: 1000 Einträge für häufig genutzte Keys
- Cache-Lookup: <1ms
- Datenbank-Lookup: <10ms

**Datenbank:** LevelDB unter `{datadir}/quantum_pubkeys`

#### Schlüsselgrößen
| Typ | Privatschlüssel | Öffentlicher Schlüssel | Signatur |
|-----|-----------------|------------------------|----------|
| ECDSA | 32 Bytes | 33/65 Bytes | 64-72 Bytes |
| FALCON-512 | 1.281 Bytes | 897 Bytes | ~666 Bytes (max 700) |

#### Sicherheitsniveau
- FALCON-512 bietet NIST Level 1 Sicherheit (128-bit Quantensicherheit)
- Konstante Zeitoperationen verhindern Timing-Angriffe
- Kanonische Signaturvalidierung verhindert Malleabilität

#### Performance
- Signaturgenerierung: <10ms
- Signaturverifikation: <2ms

### CVM Smart Contract Integration

Neue Opcodes für Signaturverifikation:

| Opcode | Gas-Kosten | Beschreibung |
|--------|------------|--------------|
| `VERIFY_SIG` | 60-3000 | Automatische Erkennung des Signaturtyps |
| `VERIFY_SIG_ECDSA` (0x61) | 60 | Explizite ECDSA-Verifikation |
| `VERIFY_SIG_QUANTUM` (0x60) | 3000 | Explizite FALCON-512-Verifikation |

Signaturtyp wird anhand der Größe erkannt:
- ≤72 Bytes: ECDSA
- >100 Bytes: FALCON-512

### Neue Quelldateien

- `src/address_quantum.h` - Quantum-Adress-Encoding/Decoding
- `src/address_quantum.cpp` - Implementierung der Quantum-Adressfunktionen
- `src/crypto/falcon/` - FALCON-512 Kryptographie-Bibliothek
- `src/quantum_registry.h` - Public Key Registry Header
- `src/quantum_registry.cpp` - Public Key Registry Implementierung
- `src/rpc/quantum.cpp` - Quantum Registry RPC-Befehle

### Änderungen an bestehenden Dateien

- `src/bech32.h/cpp` - Quantum-HRP-Konstanten und Bech32m-Unterstützung
- `src/pubkey.h/cpp` - Quantum-Schlüssel-Unterstützung
- `src/key.h/cpp` - FALCON-512 Privatschlüssel-Operationen
- `src/wallet/rpcwallet.cpp` - Neue RPC-Befehle
- `src/chainparams.cpp` - Aktivierungshöhen
- `src/validation.cpp` - Quantum Witness Parsing und Registry-Integration
- `src/init.cpp` - Registry Initialisierung und Shutdown
- `src/rpc/register.h` - Quantum RPC Registrierung

### Upgrade-Hinweise

**Empfohlen für alle Benutzer**:
- Quantum-Schutz für zukünftige Sicherheit
- Volle Abwärtskompatibilität mit Legacy-Adressen
- Keine sofortige Migration erforderlich

**Public Key Registry**:
- Erste Transaktion von einer Quantum-Adresse registriert automatisch den Public Key
- Folgetransaktionen nutzen kompakte 32-Byte Hash-Referenzen
- Bei Datenbankkorruption: `cascoind -rebuildquantumregistry` zum Neuaufbau

**Wallet-Backup**:
Nach der Migration zu Quantum-Adressen sollte ein neues Wallet-Backup erstellt werden:
```bash
cascoin-cli dumpwallet "wallet_backup.txt"
```

**Wichtig**:
- Senden Sie NICHT an Quantum-Adressen vor der Aktivierung
- Legacy-Adressen bleiben vollständig funktionsfähig
- Hive-Mining unterstützt automatisch Quantum-Schlüssel nach Aktivierung

### Dokumentation

- [Quantum Migration Guide](QUANTUM_MIGRATION_GUIDE.md) - Vollständige Migrationsanleitung
- [CVM Developer Guide](CVM_DEVELOPER_GUIDE.md) - Smart Contract Integration

### Changelog

- [Feature][Crypto] FALCON-512 Post-Quantum-Signaturen
- [Feature][Address] Neue Quantum-Adressformate (casq/tcasq/rcasq)
- [Feature][Registry] FALCON-512 Public Key Registry für Speicheroptimierung
- [Feature][RPC] `getnewaddress` mit Quantum-Unterstützung
- [Feature][RPC] `getlegacyaddress` für Legacy-Adressen
- [Feature][RPC] `migrate_to_quantum` für UTXO-Migration
- [Feature][RPC] `importquantumkey` für Schlüsselimport
- [Feature][RPC] `getquantumpubkey` für Registry-Abfragen
- [Feature][RPC] `getquantumregistrystats` für Registry-Statistiken
- [Feature][RPC] `isquantumpubkeyregistered` für Registrierungsprüfung
- [Feature][CVM] `VERIFY_SIG_QUANTUM` Opcode (0x60)
- [Feature][CVM] Automatische Signaturtyp-Erkennung
- [Feature][Wallet] Quantum-Keypool-Verwaltung
- [Enhancement][Bech32] Bech32m-Encoding für Witness v2
- [Enhancement][Security] Konstante Zeit-Operationen
- [Enhancement][Security] Kanonische Signaturvalidierung
- [Enhancement][Performance] LRU-Cache für Public Key Registry (1000 Einträge)
- [Enhancement][Recovery] `-rebuildquantumregistry` Startup-Option

### Credits

Dank an alle, die direkt zu diesem Release beigetragen haben:

- Casraw

