# Requirements Document

## Introduction

Das aktuelle BCT-System (Bee Creation Transaction) scannt beim Start und bei Updates die gesamte Wallet-Transaktionsliste durch ("Deep-Drill"), um BCT-Informationen zu sammeln. Dies skaliert nicht gut bei wachsender Wallet-Größe und Blockchain-Länge. 

Diese Feature-Spezifikation beschreibt die Umstellung auf eine persistente SQLite-Datenbank für BCT-Daten, die inkrementell aktualisiert wird - ähnlich wie die Blockchain selbst nur neue Blöcke verarbeitet, anstatt alles neu zu scannen.

## Glossary

- **BCT (Bee Creation Transaction)**: Eine Transaktion, die "Bees" (Mice) im Hive/Labyrinth-Mining-System erstellt
- **Deep-Drill-Scan**: Der aktuelle vollständige Scan aller Wallet-Transaktionen bei jedem Update
- **SQLite**: Eingebettete relationale Datenbank, die bereits im Bitcoin-Core-Ökosystem verwendet wird
- **Inkrementelles Update**: Nur neue/geänderte Daten werden verarbeitet, nicht der gesamte Datensatz
- **HiveTableModel**: Qt-Model-Klasse, die BCT-Daten für die GUI bereitstellt
- **BCTDatabase**: Aktuelle JSON-basierte Cache-Implementierung für BCT-Daten
- **Wallet Lock**: Mutex-Sperre für thread-sichere Wallet-Operationen

## Requirements

### Requirement 1

**User Story:** Als Benutzer möchte ich, dass die Anwendung schnell startet, ohne lange BCT-Scans durchführen zu müssen, damit ich sofort mit der Wallet arbeiten kann.

#### Acceptance Criteria

1. WHEN the application starts THEN the BCT_Database SHALL load BCT data from the SQLite database within 2 seconds for up to 10,000 BCT entries
2. WHEN the SQLite database file does not exist THEN the BCT_Database SHALL perform an initial full scan and persist results to the database
3. WHEN the application starts with an existing database THEN the BCT_Database SHALL skip the full wallet scan and use cached data
4. WHEN loading cached BCT data THEN the BCT_Database SHALL validate data integrity using stored checksums

### Requirement 2

**User Story:** Als Benutzer möchte ich, dass BCT-Daten automatisch aktualisiert werden, wenn neue Blöcke ankommen, damit meine Labyrinth-Ansicht immer aktuell ist.

#### Acceptance Criteria

1. WHEN a new block is received THEN the BCT_Database SHALL scan only transactions in that block for BCT updates
2. WHEN a BCT status changes (immature→mature→expired) THEN the BCT_Database SHALL update only the affected record in the database
3. WHEN a new BCT is created by the wallet THEN the BCT_Database SHALL insert the new record without rescanning existing data
4. WHEN block rewards are received for a BCT THEN the BCT_Database SHALL update the rewards field incrementally

### Requirement 3

**User Story:** Als Benutzer möchte ich, dass die BCT-Datenbank zuverlässig und konsistent mit der Blockchain bleibt, auch bei Reorganisationen.

#### Acceptance Criteria

1. WHEN a blockchain reorganization occurs THEN the BCT_Database SHALL invalidate affected BCT records and rescan from the fork point
2. WHEN the database version differs from the application version THEN the BCT_Database SHALL perform a migration or full rescan
3. WHEN writing to the database THEN the BCT_Database SHALL use transactions to ensure atomicity
4. WHEN the application crashes during a write THEN the BCT_Database SHALL recover to a consistent state on next startup

### Requirement 4

**User Story:** Als Entwickler möchte ich, dass die BCT-Datenbank das gleiche Verzeichnis wie andere Cascoin-Daten verwendet, damit die Datenverwaltung konsistent ist.

#### Acceptance Criteria

1. WHEN the BCT_Database is initialized THEN the database file SHALL be stored in the standard Cascoin data directory alongside wallet.dat
2. WHEN migrating from the old JSON cache THEN the BCT_Database SHALL import existing data and remove the old cache file
3. WHEN the data directory is specified via command line THEN the BCT_Database SHALL respect the custom path

### Requirement 5

**User Story:** Als Benutzer möchte ich, dass die GUI reaktionsfähig bleibt während BCT-Operationen, damit ich die Anwendung weiter nutzen kann.

#### Acceptance Criteria

1. WHEN performing database operations THEN the BCT_Database SHALL execute queries on a background thread
2. WHEN the GUI requests BCT data THEN the HiveTableModel SHALL return cached data immediately while updates happen asynchronously
3. WHEN a long-running operation is in progress THEN the BCT_Database SHALL provide progress feedback to the GUI

### Requirement 6

**User Story:** Als Benutzer möchte ich BCT-Daten effizient abfragen können, damit Filterung und Sortierung schnell funktionieren.

#### Acceptance Criteria

1. WHEN querying BCTs by status (mature/immature/expired) THEN the BCT_Database SHALL use indexed queries completing within 100ms for up to 10,000 records
2. WHEN sorting BCTs by any column THEN the BCT_Database SHALL use database-level sorting instead of in-memory sorting
3. WHEN filtering dead bees THEN the BCT_Database SHALL use a WHERE clause instead of post-query filtering

### Requirement 7

**User Story:** Als Entwickler möchte ich, dass die Datenbank-Schicht testbar und wartbar ist, damit zukünftige Änderungen sicher durchgeführt werden können.

#### Acceptance Criteria

1. WHEN serializing BCT data to the database THEN the BCT_Database SHALL use a defined schema with version tracking
2. WHEN deserializing BCT data from the database THEN the BCT_Database SHALL produce equivalent BCTInfo objects
3. WHEN the database schema changes THEN the BCT_Database SHALL provide migration scripts for each version upgrade

### Requirement 8

**User Story:** Als Benutzer möchte ich die BCT-Datenbank manuell neu scannen können, falls Daten inkonsistent sind oder ich Probleme beheben muss.

#### Acceptance Criteria

1. WHEN the user starts the application with -rescanbct parameter THEN the BCT_Database SHALL delete the existing database and perform a full rescan
2. WHEN the user calls the rescanbctdatabase RPC command THEN the BCT_Database SHALL rescan BCT data from the specified block range
3. WHEN the user calls rescanbctdatabase without parameters THEN the BCT_Database SHALL rescan from genesis to the current tip
4. WHEN a rescan is triggered via -reindex or -rescanblockchain THEN the BCT_Database SHALL also perform a full BCT rescan
5. WHEN the rescanbctdatabase RPC completes THEN the response SHALL include start_height, stop_height, and bcts_found count
