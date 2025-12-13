## Cascoin Core v3.2.0 – BCT Persistent Database

### Overview
- **Major Feature**: SQLite-basierte persistente Datenbank für BCT (Bee Creation Transaction) Daten
- **Performance**: Drastisch schnellere Startzeiten durch Vermeidung vollständiger Wallet-Scans
- **Inkrementelle Updates**: Neue Blöcke werden effizient verarbeitet ohne kompletten Rescan
- **Reorg-Handling**: Automatische Behandlung von Blockchain-Reorganisationen
- **GUI-Integration**: Asynchrone Datenbankabfragen für flüssige Benutzeroberfläche

### Highlights
- **SQLite-Integration**: Neue `bct_database.sqlite` Datei im Cascoin-Datenverzeichnis
- **Schneller Start**: BCT-Daten werden aus Datenbank geladen statt Wallet zu scannen
- **Inkrementelle Block-Verarbeitung**: Nur neue Blöcke werden analysiert
- **JSON-Migration**: Automatische Migration von alter `bct_cache.json` Datei
- **Neue RPC-Befehle**: `rescanbctdatabase` für manuelle Rescans
- **Startup-Parameter**: `-rescanbct` für vollständigen Datenbank-Rescan

### Neue Features

#### SQLite-basierte BCT-Datenbank
- **Persistente Speicherung**: BCT-Daten überleben Neustarts ohne erneuten Scan
- **Effiziente Abfragen**: Indizierte Suche nach Status, Höhe und Honey-Adresse
- **Atomare Transaktionen**: Datenintegrität durch SQLite-Transaktionen garantiert
- **Checksummen-Validierung**: Automatische Erkennung korrupter Datensätze

#### Inkrementelle Block-Verarbeitung
- **ValidationInterface**: Automatische Verarbeitung neuer Blöcke
- **BlockConnected**: Erkennung neuer BCTs und Hive-Rewards
- **BlockDisconnected**: Korrekte Behandlung von Reorgs
- **Status-Updates**: Automatische Aktualisierung (immature → mature → expired)

#### Rescan-Funktionalität
- **`-rescanbct` Parameter**: Vollständiger Rescan beim Start
- **`rescanbctdatabase` RPC**: Manueller Rescan mit optionalen Höhengrenzen
  ```
  rescanbctdatabase [start_height] [stop_height]
  ```
- **Integration mit `-reindex`**: BCT-Rescan wird automatisch ausgelöst
- **Integration mit `rescanblockchain`**: BCT-Rescan nach Wallet-Rescan

#### GUI-Verbesserungen
- **Asynchrone Abfragen**: Datenbankoperationen im Hintergrund-Thread
- **Kein GUI-Freeze**: Benutzeroberfläche bleibt während Abfragen responsiv
- **Sofortige Anzeige**: In-Memory-Cache für schnelle Darstellung
- **Automatische Aktualisierung**: GUI reagiert auf Datenbank-Updates

### Technische Details

#### Datenbankschema
```sql
-- BCT-Haupttabelle
CREATE TABLE bcts (
    txid TEXT PRIMARY KEY,
    honey_address TEXT,
    status TEXT,
    bee_count INTEGER,
    creation_height INTEGER,
    maturity_height INTEGER,
    expiration_height INTEGER,
    timestamp INTEGER,
    cost INTEGER,
    blocks_found INTEGER,
    rewards_paid INTEGER,
    profit INTEGER,
    checksum TEXT,
    updated_at INTEGER
);

-- Rewards-Tabelle
CREATE TABLE rewards (
    coinbase_txid TEXT PRIMARY KEY,
    bct_txid TEXT,
    amount INTEGER,
    height INTEGER
);

-- Sync-Status
CREATE TABLE sync_state (
    key TEXT PRIMARY KEY,
    value TEXT
);

-- Schema-Version für Migrationen
CREATE TABLE schema_version (
    version INTEGER
);
```

#### Indizes für Performance
- `idx_bcts_status` - Schnelle Filterung nach Status
- `idx_bcts_creation_height` - Effiziente Höhen-Abfragen
- `idx_bcts_honey_address` - Suche nach Honey-Adresse
- `idx_rewards_bct_txid` - Reward-Zuordnung
- `idx_rewards_height` - Höhenbasierte Reward-Abfragen

#### Neue Dateien
- `src/bctdb.h` - BCTDatabaseSQLite Klasse und BCTRecord Struktur
- `src/bctdb.cpp` - SQLite-Implementierung
- `src/test/bctdb_tests.cpp` - Unit-Tests und Property-Tests

#### Entfernte Dateien
- `src/qt/bctdatabase.h` - Alte JSON-basierte Implementierung
- `src/qt/bctdatabase.cpp` - Alte JSON-basierte Implementierung

### RPC-Änderungen

#### Neuer Befehl: `rescanbctdatabase`
```json
rescanbctdatabase [start_height] [stop_height]

Argumente:
  start_height (optional): Starthöhe für Rescan (Standard: 0)
  stop_height (optional): Endhöhe für Rescan (Standard: aktuelle Höhe)

Rückgabe:
{
  "start_height": 0,
  "stop_height": 500000,
  "bcts_found": 42
}
```

### Startup-Parameter

#### `-rescanbct`
Löscht die BCT-Datenbank und führt einen vollständigen Rescan durch.
```bash
./cascoind -rescanbct
```

### Migration von v3.1.x

**Automatische Migration**:
- Beim ersten Start wird `bct_cache.json` automatisch nach SQLite migriert
- Nach erfolgreicher Migration wird die JSON-Datei gelöscht
- Keine manuelle Aktion erforderlich

**Fallback-Verhalten**:
- Falls Datenbank nicht existiert: Vollständiger Scan aus Wallet
- Falls Datenbank korrupt: Automatischer Rescan der betroffenen Einträge

### Performance-Vergleich

| Operation | v3.1.x (JSON) | v3.2.0 (SQLite) |
|-----------|---------------|-----------------|
| Startup mit 1000 BCTs | ~30s | <1s |
| Neuer Block | Full Rescan | Inkrementell |
| GUI-Refresh | Blockierend | Asynchron |
| Reorg-Handling | Manuell | Automatisch |

### Build-Änderungen

**Neue Abhängigkeit**: SQLite3
- Linux: `sudo apt-get install libsqlite3-dev`
- macOS: `brew install sqlite3`
- Windows: Automatisch via depends

**Makefile-Änderungen**:
- `$(SQLITE3_LIBS)` zu allen relevanten Binaries hinzugefügt
- Neue Quelldateien in `src/Makefile.am`

### Changelog

- [Feature][Database] SQLite-basierte persistente BCT-Datenbank
- [Feature][Database] Inkrementelle Block-Verarbeitung via ValidationInterface
- [Feature][Database] Automatische Reorg-Behandlung
- [Feature][Database] Checksummen-Validierung für Datenintegrität
- [Feature][RPC] Neuer `rescanbctdatabase` Befehl
- [Feature][Startup] Neuer `-rescanbct` Parameter
- [Feature][Migration] Automatische JSON-zu-SQLite Migration
- [Enhancement][GUI] Asynchrone Datenbankabfragen
- [Enhancement][GUI] In-Memory-Cache für schnelle Anzeige
- [Enhancement][Performance] Indizierte Datenbankabfragen
- [Enhancement][Integration] BCT-Rescan bei `-reindex` und `rescanblockchain`
- [Cleanup] Entfernung der alten JSON-basierten BCTDatabase

### Bug Fixes (Post-Release)

- [Bugfix][Database] `updateStatus()` prüft jetzt auf gültige Heights - Records mit Height 0 (z.B. von JSON-Migration) werden nicht mehr fälschlicherweise als "expired" markiert
- [Bugfix][Database] `updateAllStatuses()` ignoriert Records mit `creation_height = 0` - SQL-Queries aktualisieren nur Records mit gültigen Height-Werten
- [Bugfix][Database] `initializeOnStartup()` erkennt Records mit fehlenden Heights und löst automatisch einen vollständigen Rescan aus
- [Bugfix][Database] `performInitialScan()` scannt jetzt auch Hive-Coinbase-Transaktionen für Rewards - zuvor wurden nur BCTs gescannt, aber keine Rewards erfasst
- [Bugfix][Database] Korrekte Erkennung von Hive-Coinbase-Transaktionen mit `IsHiveCoinBase()` statt `IsCoinBase()`
- [Bugfix][GUI] Fallback zu Wallet-Scan wenn SQLite-Datenbank leer ist

---

### Upgrade-Hinweise

**Empfohlen für alle Benutzer**:
- Deutlich schnellere Startzeiten
- Keine GUI-Freezes mehr bei BCT-Abfragen
- Automatische Behandlung von Blockchain-Reorgs

**Keine Datenmigration erforderlich**:
- Alte JSON-Daten werden automatisch migriert
- Wallet-Daten bleiben unverändert

**Bei Problemen**:
- `-rescanbct` für vollständigen Datenbank-Neuaufbau
- `rescanbctdatabase` RPC für partiellen Rescan

**Bekanntes Problem (behoben)**:
Falls nach dem Upgrade auf v3.2.0 keine Mäuse in der GUI angezeigt werden, liegt das an Records mit fehlenden Height-Daten aus der JSON-Migration. Die aktuelle Version erkennt dies automatisch und führt einen Rescan durch. Falls das Problem weiterhin besteht:
```bash
rm ~/.cascoin/bct_database.sqlite
./cascoin-qt
```

### Credits

Thanks to everyone who directly contributed to this release:

- Alexander Bergmann
