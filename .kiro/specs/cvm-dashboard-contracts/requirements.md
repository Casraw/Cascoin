# Anforderungsdokument

## Einleitung

Dieses Dokument beschreibt die Anforderungen für die Contract-Management-Funktionalität im CVM-Dashboard. Das bestehende Dashboard (`src/httpserver/cvmdashboard_evm.h`) bietet bereits Formulare zum Deployen, Aufrufen und Lesen von Contracts sowie Gas-Management und Trust-Aware-Interaktionen. Es fehlt jedoch eine zentrale Übersicht der eigenen Contracts: Benutzer können aktuell nicht sehen, welche Contracts zu ihrer Wallet gehören, und müssen Contract-Adressen manuell eingeben. Dieses Feature ergänzt das Dashboard um eine wallet-bezogene Contract-Liste, Detail-Ansichten, Storage-Einsicht und Transaktionshistorie.

## Glossar

- **Dashboard**: Die webbasierte CVM-Benutzeroberfläche in `src/httpserver/`, erreichbar über `/dashboard` oder `/cvm`, bestehend aus `cvmdashboard_html.h` (Haupt-UI) und `cvmdashboard_evm.h` (EVM-Erweiterung mit Deploy/Call/Read-Formularen)
- **Contract**: Ein auf der CVM deployter Smart Contract, gespeichert als `CVM::Contract`-Objekt in der CVMDatabase mit Adresse, Bytecode, Deployment-Höhe und Deployment-Transaktion
- **Wallet**: Die lokale Cascoin-Wallet, die private Schlüssel und Adressen verwaltet
- **CVMDatabase**: Die LevelDB-basierte Datenbank (`src/cvm/cvmdb.h`), die Contract-Metadaten unter dem Schlüssel `DB_CONTRACT = 'C'` und Storage unter `DB_STORAGE = 'S'` speichert; bietet `ListContracts()`, `ReadContract()`, `LoadContract()` und Storage-Zugriff
- **Contract_Liste**: Die neue UI-Komponente im Dashboard, die alle Contracts des Benutzers auflistet
- **Contract_Detail_Ansicht**: Die neue UI-Komponente, die detaillierte Informationen zu einem einzelnen Contract anzeigt
- **RPC_Schnittstelle**: Die JSON-RPC-API in `src/rpc/cvm.cpp`, die bereits `deploycontract`, `callcontract`, `getcontractinfo` und `sendcvmcontract` bereitstellt
- **Deployer_Adresse**: Die Wallet-Adresse, die einen Contract deployt hat, gespeichert im `CVM::Contract`-Objekt (indirekt über die Deployment-Transaktion ermittelbar) und in `ContractMetadata::deployer` im Bytecode-Detector
- **Receipt**: Die Transaktionsquittung (`CVM::TransactionReceipt` in `src/cvm/receipt.h`), die Ergebnis, Logs, Gas-Verbrauch und Status einer Contract-Ausführung enthält
- **Bestehende_EVM_Section**: Die bereits implementierte "EVM Smart Contracts"-Sektion in `cvmdashboard_evm.h` mit Deploy-, Call- und Read-Formularen, ABI-Parsing und Funktionsauswahl per Dropdown

## Anforderungen

### Anforderung 1: Contracts der eigenen Wallet auflisten

**User Story:** Als Wallet-Besitzer möchte ich alle Smart Contracts sehen, die von meinen Wallet-Adressen deployt wurden, damit ich einen Überblick über meine Contracts habe, ohne Adressen manuell eingeben zu müssen.

#### Akzeptanzkriterien

1. WHEN das Dashboard geladen wird, THE Contract_Liste SHALL alle Contracts anzeigen, deren Deployer_Adresse zu einer Adresse der aktuellen Wallet gehört
2. WHEN ein neuer Contract über die Wallet deployt wird, THE Contract_Liste SHALL den neuen Contract innerhalb von 30 Sekunden anzeigen, ohne dass ein manueller Refresh nötig ist
3. THE RPC_Schnittstelle SHALL einen neuen Endpunkt `listmycontracts` bereitstellen, der alle Contracts zurückgibt, deren Deployer_Adresse in der aktuellen Wallet enthalten ist
4. WHEN die Wallet keine Contracts deployt hat, THE Contract_Liste SHALL eine leere Liste mit einem Hinweistext anzeigen
5. WHEN ein Contract in der Contract_Liste angezeigt wird, THE Contract_Liste SHALL für jeden Eintrag die Contract-Adresse, die Deployment-Blockhöhe und den Bytecode-Typ (CVM/EVM) darstellen

### Anforderung 2: Ältere Contracts erkennen

**User Story:** Als Wallet-Besitzer möchte ich, dass auch ältere, bereits vor der Dashboard-Nutzung deployte Contracts erkannt und angezeigt werden, damit kein Contract verloren geht.

#### Akzeptanzkriterien

1. WHEN der `listmycontracts`-Endpunkt aufgerufen wird, THE RPC_Schnittstelle SHALL alle existierenden Contracts in der CVMDatabase durchsuchen und diejenigen zurückgeben, deren Deployer_Adresse zur aktuellen Wallet gehört
2. WHEN ein Contract in der CVMDatabase existiert und die Deployer_Adresse zur Wallet gehört, THE Contract_Liste SHALL diesen Contract anzeigen, unabhängig vom Deployment-Zeitpunkt
3. THE RPC_Schnittstelle SHALL die Deployer_Adresse aus der Deployment-Transaktion des Contracts ermitteln, indem die Transaktion aus der Blockchain geladen und der Absender extrahiert wird

### Anforderung 3: Contract-Details anzeigen

**User Story:** Als Wallet-Besitzer möchte ich detaillierte Informationen zu jedem meiner Contracts einsehen können, damit ich den Status und die Eigenschaften meiner Contracts verstehe.

#### Akzeptanzkriterien

1. WHEN ein Benutzer einen Contract in der Contract_Liste auswählt, THE Contract_Detail_Ansicht SHALL folgende Informationen anzeigen: Contract-Adresse, Deployer_Adresse, Deployment-Blockhöhe, Deployment-Transaktions-Hash, Bytecode-Größe in Bytes und Bytecode-Format (CVM oder EVM)
2. WHEN ein Contract als bereinigt (isCleanedUp) markiert ist, THE Contract_Detail_Ansicht SHALL diesen Status mit einem visuellen Indikator kennzeichnen
3. WHEN ein Contract ausgewählt wird, THE Contract_Detail_Ansicht SHALL die Daten über den bestehenden `getcontractinfo`-RPC-Endpunkt laden

### Anforderung 4: Contract-Funktionen direkt ausführen

**User Story:** Als Wallet-Besitzer möchte ich die Funktionen meiner Contracts direkt aus der Detail-Ansicht aufrufen und lesen können, damit ich mit meinen Contracts interagieren kann, ohne Adressen manuell kopieren oder zur EVM-Sektion wechseln zu müssen.

#### Akzeptanzkriterien

1. WHEN ein Benutzer einen Contract in der Contract_Detail_Ansicht öffnet und eine ABI im JSON-Format eingibt, THE Dashboard SHALL alle verfügbaren Funktionen des Contracts auflisten, aufgeteilt in schreibende Funktionen (state-changing) und lesende Funktionen (view/pure)
2. WHEN ein Benutzer eine lesende Funktion auswählt und die Parameter eingibt, THE Dashboard SHALL den Aufruf über den bestehenden `callcontract`-RPC-Endpunkt als Read-Only-Call ausführen und den dekodierten Rückgabewert anzeigen
3. WHEN ein Benutzer eine schreibende Funktion auswählt und die Parameter eingibt, THE Dashboard SHALL den Aufruf als Transaktion über den bestehenden `callcontract`-RPC-Endpunkt senden und das Ergebnis mit Transaktions-Hash, verbrauchtem Gas und erzeugten Log-Einträgen anzeigen
4. IF ein Contract-Aufruf fehlschlägt, THEN THE Dashboard SHALL die Fehlermeldung und den Revert-Grund aus dem Receipt anzeigen
5. WHEN ein Contract keine ABI hinterlegt hat, THE Dashboard SHALL ein Eingabefeld für Raw-Hex-Daten bereitstellen, über das der Benutzer einen manuellen Aufruf mit beliebigen Call-Daten senden kann

### Anforderung 5: Contract-Storage einsehen

**User Story:** Als Wallet-Besitzer möchte ich den persistenten Storage meiner Contracts einsehen können, damit ich den aktuellen Zustand meiner Contracts nachvollziehen kann.

#### Akzeptanzkriterien

1. WHEN ein Benutzer die Storage-Ansicht eines Contracts in der Contract_Detail_Ansicht öffnet, THE Dashboard SHALL alle Storage-Schlüssel-Wert-Paare des Contracts laden und als Tabelle anzeigen
2. THE RPC_Schnittstelle SHALL einen neuen Endpunkt `getcontractstorage` bereitstellen, der alle Storage-Einträge eines Contracts als Schlüssel-Wert-Paare im Hex-Format zurückgibt
3. WHEN ein Contract keine Storage-Einträge besitzt, THE Dashboard SHALL einen Hinweistext anzeigen, dass kein Storage vorhanden ist

### Anforderung 6: Transaktionshistorie eines Contracts

**User Story:** Als Wallet-Besitzer möchte ich die Transaktionshistorie meiner Contracts einsehen können, damit ich nachvollziehen kann, welche Aufrufe stattgefunden haben.

#### Akzeptanzkriterien

1. WHEN ein Benutzer die Transaktionshistorie eines Contracts in der Contract_Detail_Ansicht öffnet, THE Dashboard SHALL alle Receipts anzeigen, die mit der Contract-Adresse verknüpft sind
2. THE RPC_Schnittstelle SHALL einen neuen Endpunkt `getcontractreceipts` bereitstellen, der alle Transaktions-Receipts für eine gegebene Contract-Adresse zurückgibt, sortiert nach Blockhöhe absteigend
3. WHEN ein Receipt angezeigt wird, THE Dashboard SHALL Transaktions-Hash, Absender-Adresse, verbrauchtes Gas, Status (Erfolg oder Fehler) und Blockhöhe darstellen

### Anforderung 7: Fehlerbehandlung und Validierung

**User Story:** Als Wallet-Besitzer möchte ich bei fehlerhaften Eingaben oder Systemfehlern verständliche Rückmeldungen erhalten, damit ich Probleme selbst beheben kann.

#### Akzeptanzkriterien

1. IF die Wallet gesperrt ist, THEN THE Dashboard SHALL bei Contract-Interaktionen den Benutzer auffordern, die Wallet zu entsperren
2. IF die RPC_Schnittstelle nicht erreichbar ist, THEN THE Contract_Liste SHALL eine Fehlermeldung anzeigen und einen Retry-Button bereitstellen
3. IF der `listmycontracts`-Endpunkt keine Wallet-Adressen ermitteln kann, THEN THE RPC_Schnittstelle SHALL einen Fehlercode mit einer beschreibenden Meldung zurückgeben
