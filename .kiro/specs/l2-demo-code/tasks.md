# Implementation Plan: L2 Demo Code

## Overview

Dieser Implementation Plan beschreibt die schrittweise Erstellung von Demo-Code für Cascoin L2 und Smart Contracts. Die Implementierung erfolgt in Phasen: zuerst die Verzeichnisstruktur, dann die Skripte, dann die Contracts, und schließlich die Dokumentation.

## Tasks

- [x] 1. Erstelle Demo-Verzeichnisstruktur
  - [x] 1.1 Erstelle `contrib/demos/` Hauptverzeichnis
    - Erstelle Verzeichnisstruktur: l2-chain/, contracts/, tutorials/
    - Erstelle leere README.md Dateien als Platzhalter
    - _Requirements: 3.1, 5.3_

- [x] 2. L2 Demo Chain Setup
  - [x] 2.1 Erstelle L2 Setup-Skript (`contrib/demos/l2-chain/setup_l2_demo.sh`)
    - Implementiere Node-Start mit Regtest und L2
    - Implementiere Block-Generierung (101 Blöcke)
    - Implementiere Sequencer-Registrierung
    - Implementiere L2-Deposit
    - Füge Statusmeldungen und Fehlerbehandlung hinzu
    - _Requirements: 1.1, 1.2, 1.3, 1.5_

  - [x] 2.2 Erstelle Cleanup-Skript (`contrib/demos/l2-chain/cleanup.sh`)
    - Implementiere Node-Stop
    - Implementiere Regtest-Daten-Löschung
    - _Requirements: 3.5_

  - [x] 2.3 Erstelle Regtest-Konfiguration (`contrib/demos/l2-chain/config/regtest.conf`)
    - Dokumentiere alle L2-relevanten Konfigurationsoptionen
    - _Requirements: 1.4_

- [x] 3. Checkpoint - L2 Demo Chain funktioniert
  - Ensure setup_l2_demo.sh erfolgreich durchläuft
  - Verify l2_getchaininfo zeigt aktive Chain

- [ ] 4. Token Contract Implementation
  - [ ] 4.1 Erstelle CascoinToken Solidity Contract (`contrib/demos/contracts/CascoinToken.sol`)
    - Implementiere ERC-20 Basis (name, symbol, decimals, totalSupply)
    - Implementiere balanceOf und Transfer
    - Implementiere approve und transferFrom
    - Implementiere Reputation Oracle Integration
    - Implementiere maxTransferAmount basierend auf Reputation
    - Füge ausführliche Inline-Dokumentation hinzu
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5_

  - [ ] 4.2 Erstelle CVM Bytecode Version (`contrib/demos/contracts/CascoinToken.cvm`)
    - Implementiere vereinfachte Token-Logik in CVM Opcodes
    - Dokumentiere Storage Layout
    - _Requirements: 2.1_

  - [ ] 4.3 Erstelle Token Deployment Skript (`contrib/demos/contracts/deploy_token.sh`)
    - Implementiere Solidity-Kompilierung (falls solc verfügbar)
    - Implementiere Deployment via cas_sendTransaction
    - Implementiere Receipt-Abfrage für Contract-Adresse
    - _Requirements: 3.2_

  - [ ] 4.4 Erstelle Token Transfer Demo Skript (`contrib/demos/contracts/demo_transfer.sh`)
    - Implementiere Token-Transfer zwischen Adressen
    - Zeige Balance-Änderungen
    - _Requirements: 3.3_

- [ ] 5. Checkpoint - Token Contract funktioniert
  - Ensure deploy_token.sh erfolgreich deployed
  - Verify Token-Transfers funktionieren

- [ ] 6. Dokumentation
  - [ ] 6.1 Erstelle Haupt-README (`contrib/demos/README.md`)
    - Schreibe Übersicht aller Demos
    - Dokumentiere Prerequisites
    - Füge Quick Start Anleitung hinzu
    - _Requirements: 4.1_

  - [ ] 6.2 Erstelle L2 Quick Start Tutorial (`contrib/demos/tutorials/01_l2_quickstart.md`)
    - Dokumentiere jeden Schritt des L2-Setups
    - Erkläre Konfigurationsoptionen
    - Füge Troubleshooting-Sektion hinzu
    - _Requirements: 4.2, 4.4_

  - [ ] 6.3 Erstelle Token Creation Tutorial (`contrib/demos/tutorials/02_token_creation.md`)
    - Dokumentiere Contract Zeile für Zeile
    - Erkläre Reputation-Integration
    - Zeige Deployment-Prozess
    - _Requirements: 4.3_

  - [ ] 6.4 Erstelle Contract README (`contrib/demos/contracts/README.md`)
    - Dokumentiere Contract-Funktionen
    - Zeige Beispiel-Aufrufe
    - _Requirements: 2.4_

- [ ] 7. Integration mit existierender Dokumentation
  - [ ] 7.1 Aktualisiere L2_DEVELOPER_GUIDE.md
    - Füge Verweis auf Demo-Code hinzu
    - Verlinke zu tutorials/01_l2_quickstart.md
    - _Requirements: 5.1_

  - [ ] 7.2 Aktualisiere CVM_DEVELOPER_GUIDE.md
    - Füge Verweis auf Token-Contract hinzu
    - Verlinke zu tutorials/02_token_creation.md
    - _Requirements: 5.2_

- [ ] 8. Final Checkpoint
  - Ensure alle Skripte funktionieren
  - Verify Dokumentation vollständig
  - Run vollständige Demo von Anfang bis Ende

## Notes

- Alle Skripte verwenden `set -e` für Fehlerabbruch
- Skripte sind für Regtest-Modus konzipiert
- Solidity-Kompilierung erfordert `solc` Installation
- CVM-Version ist eine vereinfachte Alternative ohne Solidity-Abhängigkeit
