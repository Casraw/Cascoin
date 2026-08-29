# Implementation Plan: CVM Dashboard Contract Management

## Übersicht

Inkrementelle Implementierung der Contract-Management-Funktionalität: zuerst die RPC-Backend-Endpunkte mit Tests, dann die Dashboard-UI-Sektion, und schließlich die Integration.

## Tasks

- [x] 1. RPC-Endpunkt `listmycontracts` implementieren
  - [x] 1.1 Neue RPC-Funktion `listmycontracts` in `src/rpc/cvm.cpp` hinzufügen
    - Wallet-Referenz über `GetWalletForJSONRPCRequest()` holen
    - `CVMDatabase::ListContracts()` aufrufen, für jeden Contract die Deployment-TX laden
    - Deployer-Adresse aus TX-Inputs extrahieren, mit `IsMine()` gegen Wallet prüfen
    - Matching Contracts als JSON-Array mit Feldern: address, deployer, deploymentHeight, deploymentTx, codeSize, format, isCleanedUp zurückgeben
    - Fehlerbehandlung: Wallet nicht verfügbar (RPC_WALLET_ERROR), CVMDatabase nicht initialisiert (RPC_DATABASE_ERROR)
    - RPC-Befehl in der Command-Tabelle am Ende von `cvm.cpp` registrieren
    - _Requirements: 1.1, 1.3, 1.4, 1.5, 2.1, 2.2, 2.3, 7.3_

  - [x] 1.2 Property-Test für Contract-Ownership-Filterung schreiben
    - **Property 1: Contract-Ownership-Filterung**
    - Testdatei: `src/test/cvm_dashboard_contracts_tests.cpp`
    - Generiere zufällige Contract-Sets mit zufälligen Deployer-Adressen und Wallet-Adress-Sets
    - Prüfe, dass die Filterlogik genau die Contracts zurückgibt, deren Deployer in der Wallet ist
    - Mindestens 100 Iterationen
    - **Validates: Requirements 1.1, 2.1, 2.2**

  - [x] 1.3 Property-Test für Response-Vollständigkeit schreiben
    - **Property 2: listmycontracts-Response-Vollständigkeit**
    - Generiere zufällige Contracts, serialisiere zu JSON, prüfe Pflichtfelder (address, deploymentHeight, format)
    - Mindestens 100 Iterationen
    - **Validates: Requirements 1.5**

- [x] 2. RPC-Endpunkt `getcontractstorage` implementieren
  - [x] 2.1 Neue RPC-Funktion `getcontractstorage` in `src/rpc/cvm.cpp` hinzufügen
    - Contract-Adresse als Parameter parsen und validieren
    - `CVMDatabase::Exists()` prüfen, bei Fehler RPC_INVALID_ADDRESS_OR_KEY zurückgeben
    - Über `CVMDatabase::ListKeysWithPrefix()` alle Storage-Schlüssel mit Prefix `'S' + contractAddr` auflisten
    - Für jeden Schlüssel den Wert über `CVMDatabase::Load()` laden
    - JSON-Response mit entries-Array (key/value Hex-Paare) und count zurückgeben
    - RPC-Befehl in der Command-Tabelle registrieren
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 2.2 Property-Test für Storage-Read-Write-Round-Trip schreiben
    - **Property 5: Storage-Read-Write-Round-Trip**
    - Testdatei: `src/test/cvm_dashboard_contracts_tests.cpp`
    - Generiere zufällige Contract-Adressen und Storage-Key-Value-Paare
    - Schreibe in CVMDatabase, lese über Abfrage-Logik, prüfe Gleichheit
    - Mindestens 100 Iterationen
    - **Validates: Requirements 5.1**

- [x] 3. Contract-Receipt-Index und `getcontractreceipts` implementieren
  - [x] 3.1 Contract-Receipt-Index in `src/cvm/cvmdb.h` und `src/cvm/cvmdb.cpp` hinzufügen
    - Neuen DB-Key `DB_CONTRACT_RECEIPTS = 'T'` definieren
    - Methoden `WriteContractReceiptIndex()`, `ReadContractReceiptIndex()`, `AppendContractReceiptIndex()` implementieren
    - `WriteReceipt()` erweitern, um den Index automatisch zu aktualisieren
    - _Requirements: 6.1, 6.2_

  - [x] 3.2 Neue RPC-Funktion `getcontractreceipts` in `src/rpc/cvm.cpp` hinzufügen
    - Contract-Adresse und optionalen count-Parameter parsen
    - Über `ReadContractReceiptIndex()` TX-Hashes laden
    - Für jeden Hash das Receipt über `ReadReceipt()` laden
    - Nach blockNumber absteigend sortieren, auf count begrenzen
    - JSON-Array mit txHash, from, to, blockNumber, gasUsed, status, revertReason, logCount zurückgeben
    - RPC-Befehl in der Command-Tabelle registrieren
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 3.3 Property-Test für Receipt-Abruf und Sortierung schreiben
    - **Property 6: Receipt-Abruf-Vollständigkeit und Sortierung**
    - Testdatei: `src/test/cvm_dashboard_contracts_tests.cpp`
    - Generiere zufällige Receipts mit verschiedenen Contract-Adressen und Blockhöhen
    - Schreibe in DB, frage ab, prüfe Vollständigkeit und absteigende Sortierung
    - Mindestens 100 Iterationen
    - **Validates: Requirements 6.1, 6.2, 6.3**

- [x] 4. Checkpoint - Backend-Tests prüfen
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. Dashboard-UI: Contract-Liste und Detail-Ansicht
  - [x] 5.1 Neue Datei `src/httpserver/cvmdashboard_contracts.h` erstellen
    - HTML-Sektion "Meine Contracts" mit Contract-Liste (Tabelle mit Adresse, Format, Block)
    - Contract-Detail-Ansicht mit allen Metadaten-Feldern
    - Tab-Navigation: Interagieren, Storage, Transaktionen
    - JavaScript: `loadMyContracts()` über `rpcCall('listmycontracts')`, `selectContract()` über `rpcCall('getcontractinfo')`
    - Auto-Refresh alle 30 Sekunden für die Contract-Liste
    - Leere-Liste-Hinweis und Fehlerbehandlung mit Retry-Button
    - _Requirements: 1.1, 1.2, 1.4, 3.1, 3.2, 7.2_

  - [x] 5.2 Dashboard-Integration in `src/httpserver/cvmdashboard.cpp`
    - `#include` für `cvmdashboard_contracts.h` hinzufügen
    - In `BuildCompleteDashboardHTML()` die neue Sektion einfügen (vor der EVM-Sektion)
    - JavaScript der neuen Sektion in den Script-Block einfügen
    - _Requirements: 1.1_

- [x] 6. Dashboard-UI: Contract-Interaktion
  - [x] 6.1 ABI-basierte Interaktions-UI in `cvmdashboard_contracts.h` implementieren
    - ABI-Eingabefeld mit Parse-Button
    - Automatische Kategorisierung in Read-Funktionen (view/pure) und Write-Funktionen
    - Für jede Funktion: Name, Parameter-Typen, Eingabefelder für Argumente
    - Read-Funktionen: Ausführen-Button, Ergebnis-Anzeige mit dekodiertem Rückgabewert
    - Write-Funktionen: Gas-Limit-Feld, Senden-Button, TX-Hash-Anzeige
    - Raw-Hex-Fallback: Data-Eingabefeld, Gas-Limit, Value, Read/Send-Buttons
    - Fehleranzeige bei fehlgeschlagenen Aufrufen mit Revert-Grund
    - Nutzt bestehende `encodeABIParameters()`, `decodeABIResult()`, `encodeFunctionCall()` aus `cvmdashboard_evm.h`
    - _Requirements: 4.1, 4.2, 4.3, 4.4, 4.5_

  - [x] 6.2 Property-Test für ABI-Funktions-Kategorisierung schreiben
    - **Property 3: ABI-Funktions-Kategorisierung**
    - Testdatei: `src/test/cvm_dashboard_contracts_tests.cpp`
    - Generiere zufällige ABI-Arrays mit verschiedenen stateMutability-Werten
    - Prüfe korrekte Aufteilung in read/write
    - Mindestens 100 Iterationen
    - **Validates: Requirements 4.1**

  - [x] 6.3 Property-Test für ABI-Encoding-Round-Trip schreiben
    - **Property 4: ABI-Parameter-Encoding-Round-Trip**
    - Testdatei: `src/test/cvm_dashboard_contracts_tests.cpp`
    - Generiere zufällige Typen (uint256, address, bool, bytes32) mit zufälligen Werten
    - Encode zu Hex, decode zurück, prüfe Gleichheit
    - Mindestens 100 Iterationen
    - **Validates: Requirements 4.2**

- [x] 7. Dashboard-UI: Storage-Browser und Transaktionshistorie
  - [x] 7.1 Storage-Browser-Tab in `cvmdashboard_contracts.h` implementieren
    - JavaScript: `loadContractStorage()` über `rpcCall('getcontractstorage', [contractAddr])`
    - Tabelle mit Slot (Key) und Value als Hex
    - Hinweistext bei leerem Storage
    - _Requirements: 5.1, 5.3_

  - [x] 7.2 Transaktionshistorie-Tab in `cvmdashboard_contracts.h` implementieren
    - JavaScript: `loadContractReceipts()` über `rpcCall('getcontractreceipts', [contractAddr])`
    - Tabelle mit Block, TX-Hash, Von, Gas, Status (✅/❌)
    - Fehler-Receipts mit Revert-Grund anzeigen
    - _Requirements: 6.1, 6.3_

- [x] 8. Fehlerbehandlung und Wallet-Status
  - [x] 8.1 Wallet-Status-Prüfung im Dashboard implementieren
    - Bei gesperrter Wallet: Interaktions-Buttons deaktivieren, Hinweis anzeigen
    - Bei nicht erreichbarer RPC: Fehlermeldung mit Retry-Button
    - Bei fehlender Wallet: Hinweis, dass Wallet benötigt wird
    - _Requirements: 7.1, 7.2, 7.3_

- [x] 9. Build-System-Integration
  - [x] 9.1 `Makefile.am` aktualisieren
    - `src/httpserver/cvmdashboard_contracts.h` zu den Header-Dateien hinzufügen
    - `src/test/cvm_dashboard_contracts_tests.cpp` zu den Test-Quellen hinzufügen
    - _Requirements: alle_

- [ ] 10. Final Checkpoint - Alle Tests prüfen
  - Ensure all tests pass, ask the user if questions arise.

## Hinweise

- Alle Tasks sind erforderlich (keine optionalen Tasks)
- Jeder Task referenziert spezifische Anforderungen für Nachverfolgbarkeit
- Checkpoints stellen inkrementelle Validierung sicher
- Property-Tests validieren universelle Korrektheitseigenschaften
- Unit-Tests validieren spezifische Beispiele und Randfälle
