# Requirements Document: L2 Burn-and-Mint Token Model

## Introduction

Diese Spezifikation definiert das **Burn-and-Mint Token-Modell** für Cascoin L2. Anstatt eines unabhängigen Tokens oder einer unsicheren Bridge verwenden wir ein kryptographisch sicheres System, bei dem CAS auf L1 **verbrannt** wird (via OP_RETURN) und entsprechende L2-Tokens **gemintet** werden.

**Warum Burn-and-Mint?**
- **Kryptographisch sicher**: OP_RETURN Outputs sind garantiert unspendable - kein Private Key kann sie zurückholen
- **Konsens-geschützt**: Alle Sequencer validieren die gleichen Burn-Transaktionen
- **Wirtschaftlich verknüpft**: L2-Token Supply = Verbrannte CAS (1:1 Backing)
- **Deflationär für L1**: Jede L2-Token-Erstellung reduziert die L1 CAS Supply permanent
- **Keine manipulierbaren Adressen**: OP_RETURN braucht keine Burn-Adresse die gehackt werden könnte

**Das Burn-and-Mint Modell:**
- User verbrennt CAS auf L1 via OP_RETURN Transaktion
- L2 Netzwerk erkennt und validiert die Burn-Transaktion
- 2/3 Sequencer-Konsens bestätigt den Burn
- L2-Tokens werden 1:1 zum verbrannten Betrag gemintet
- Sequencer erhalten Rewards aus Transaction Fees (nicht aus Minting)

## Glossary

- **Burn_Transaction**: Eine L1 Transaktion mit OP_RETURN Output die CAS permanent zerstört
- **OP_RETURN**: Bitcoin Script Opcode der einen Output als unspendable markiert
- **L2_Token**: Token auf der L2 Chain, 1:1 backed durch verbrannte CAS
- **Burn_Proof**: Nachweis einer gültigen Burn-Transaktion auf L1
- **Mint_Request**: Anfrage L2-Tokens basierend auf einem Burn_Proof zu minten
- **Sequencer_Consensus**: 2/3 Mehrheit der Sequencer die einen Burn validieren
- **Burn_Registry**: On-chain Registry aller verarbeiteten Burn-Transaktionen
- **L1_Confirmations**: Anzahl der Bestätigungen einer L1 Transaktion

## Requirements

### Requirement 1: OP_RETURN Burn Format

**User Story:** Als User möchte ich CAS auf L1 verbrennen um L2-Tokens zu erhalten.

#### Acceptance Criteria

1. WHEN ein User CAS verbrennen möchte THEN SHALL das System eine L1 Transaktion mit OP_RETURN Output erstellen
2. THE OP_RETURN Output SHALL das Format haben: `OP_RETURN "L2BURN" <chain_id:4bytes> <recipient_pubkey:33bytes> <amount:8bytes>`
3. THE Burn-Transaktion SHALL mindestens einen Input haben der die zu verbrennenden CAS enthält
4. THE verbrannte Menge SHALL gleich dem Wert des OP_RETURN Outputs sein (implizit durch Input - andere Outputs)
5. THE System SHALL eine RPC Methode `l2_createburntx` bereitstellen um Burn-Transaktionen zu erstellen
6. THE System SHALL die Burn-Transaktion signieren und an das L1 Netzwerk broadcasten

### Requirement 2: Burn-Transaktion Validierung

**User Story:** Als Sequencer möchte ich Burn-Transaktionen validieren um sicherzustellen dass nur echte Burns L2-Tokens erzeugen.

#### Acceptance Criteria

1. WHEN eine Burn-Transaktion empfangen wird THEN SHALL das System prüfen ob sie das korrekte OP_RETURN Format hat
2. THE System SHALL prüfen ob die L1 Transaktion mindestens 6 Bestätigungen hat
3. THE System SHALL prüfen ob die chain_id mit der aktuellen L2 Chain übereinstimmt
4. THE System SHALL prüfen ob die Burn-Transaktion nicht bereits verarbeitet wurde (Double-Mint Prevention)
5. IF die Validierung fehlschlägt THEN SHALL das System die Mint-Anfrage ablehnen
6. THE System SHALL alle Validierungsfehler loggen

### Requirement 3: Sequencer Konsens für Minting

**User Story:** Als L2 Netzwerk möchte ich dass 2/3 der Sequencer einen Burn bestätigen bevor Tokens gemintet werden.

#### Acceptance Criteria

1. WHEN ein Sequencer eine gültige Burn-Transaktion erkennt THEN SHALL er eine Mint-Bestätigung broadcasten
2. THE Mint-Bestätigung SHALL enthalten: l1_tx_hash, l2_recipient, amount, sequencer_signature
3. THE System SHALL Mint-Bestätigungen von anderen Sequencern sammeln
4. WHEN 2/3 der aktiven Sequencer die gleiche Burn-Transaktion bestätigt haben THEN SHALL das Minting erfolgen
5. IF weniger als 2/3 Bestätigungen innerhalb von 10 Minuten eingehen THEN SHALL die Anfrage als "pending" markiert werden
6. THE System SHALL verhindern dass ein Sequencer mehrfach für die gleiche Burn-Transaktion abstimmt

### Requirement 4: L2 Token Minting

**User Story:** Als User möchte ich L2-Tokens erhalten nachdem mein Burn bestätigt wurde.

#### Acceptance Criteria

1. WHEN Sequencer-Konsens erreicht ist THEN SHALL das System L2-Tokens an die recipient_pubkey minten
2. THE gemintete Menge SHALL exakt der verbrannten CAS Menge entsprechen (1:1)
3. THE System SHALL die Burn-Transaktion in der Burn_Registry als "processed" markieren
4. THE System SHALL ein MintEvent emittieren mit: l1_tx_hash, recipient, amount, timestamp
5. THE geminteten Tokens SHALL sofort im L2 State verfügbar sein
6. THE System SHALL die L2 Total Supply entsprechend erhöhen

### Requirement 5: Burn Registry (Double-Mint Prevention)

**User Story:** Als System möchte ich verhindern dass die gleiche Burn-Transaktion mehrfach verwendet wird.

#### Acceptance Criteria

1. THE System SHALL eine persistente Registry aller verarbeiteten Burn-Transaktionen führen
2. THE Registry SHALL für jede Burn-Transaktion speichern: l1_tx_hash, l2_mint_block, recipient, amount, timestamp
3. WHEN eine Mint-Anfrage eingeht THEN SHALL das System prüfen ob l1_tx_hash bereits in der Registry ist
4. IF l1_tx_hash bereits verarbeitet wurde THEN SHALL das System die Anfrage ablehnen
5. THE Registry SHALL über RPC abfragbar sein (`l2_getburnstatus`)
6. THE Registry SHALL bei L2 Chain Reorgs korrekt aktualisiert werden

### Requirement 6: Sequencer Rewards aus Fees

**User Story:** Als Sequencer möchte ich für meine Arbeit belohnt werden ohne neue Tokens zu minten.

#### Acceptance Criteria

1. THE Sequencer Rewards SHALL ausschließlich aus L2 Transaction Fees kommen
2. THE System SHALL KEINE neuen Tokens für Block-Rewards minten
3. WHEN ein Sequencer einen Block produziert THEN SHALL er die Transaction Fees dieses Blocks erhalten
4. THE Fee-Verteilung SHALL transparent und nachvollziehbar sein
5. IF ein Block keine Transaktionen enthält THEN SHALL der Sequencer keine Rewards erhalten
6. THE System SHALL eine Mindest-Fee pro Transaktion erzwingen um Spam zu verhindern

### Requirement 7: L2 Token Transfers

**User Story:** Als L2 User möchte ich meine Tokens an andere Adressen senden können.

#### Acceptance Criteria

1. WHEN ein User einen Transfer initiiert THEN SHALL das System prüfen ob der Sender genug Balance hat
2. THE Transfer SHALL atomar sein - entweder beide Seiten werden aktualisiert oder keine
3. THE Transfer SHALL eine Fee in L2-Tokens erfordern
4. IF der Sender nicht genug Balance hat (inkl. Fee) THEN SHALL der Transfer abgelehnt werden
5. THE System SHALL Transfers in L2 Transaktionen aufzeichnen
6. THE System SHALL Transfer-Events emittieren

### Requirement 8: Supply Invariante

**User Story:** Als Auditor möchte ich verifizieren können dass die L2 Supply korrekt ist.

#### Acceptance Criteria

1. THE L2 Total Supply SHALL immer gleich der Summe aller verbrannten CAS sein
2. THE System SHALL eine RPC Methode `l2_verifysupply` bereitstellen
3. THE Summe aller L2 Balances SHALL gleich der Total Supply sein
4. THE System SHALL bei jedem Block die Supply-Invariante prüfen
5. IF die Invariante verletzt wird THEN SHALL das System einen kritischen Fehler loggen
6. THE System SHALL die Total Burned CAS und Total Minted L2 separat tracken

### Requirement 9: RPC Interface

**User Story:** Als Developer möchte ich alle Burn-and-Mint Operationen über RPC ausführen können.

#### Acceptance Criteria

1. THE System SHALL `l2_createburntx` bereitstellen um Burn-Transaktionen zu erstellen
2. THE System SHALL `l2_getburnstatus` bereitstellen um den Status einer Burn-TX abzufragen
3. THE System SHALL `l2_getminthistory` bereitstellen um alle Mints abzufragen
4. THE System SHALL `l2_gettotalsupply` bereitstellen um die aktuelle Supply abzufragen
5. THE System SHALL `l2_verifysupply` bereitstellen um die Supply-Invariante zu prüfen
6. THE System SHALL `l2_getpendingburns` bereitstellen um ausstehende Burns anzuzeigen

### Requirement 10: Sicherheit und Konsens

**User Story:** Als Netzwerk möchte ich sicherstellen dass kein einzelner Actor das System manipulieren kann.

#### Acceptance Criteria

1. THE Burn-Adresse SHALL NICHT konfigurierbar sein - OP_RETURN ist per Definition unspendable
2. THE System SHALL KEINE Burn-Adresse aus chainparams verwenden
3. ALL Sequencer SHALL die gleichen Validierungsregeln für Burns verwenden
4. IF ein Sequencer abweichende Regeln verwendet THEN SHALL sein Block von anderen abgelehnt werden
5. THE System SHALL Sequencer die ungültige Mint-Bestätigungen senden als böswillig markieren
6. THE System SHALL mindestens 3 aktive Sequencer für Konsens erfordern

### Requirement 11: Legacy RPC Deprecation

**User Story:** Als System möchte ich alte unsichere RPCs entfernen.

#### Acceptance Criteria

1. THE System SHALL die alten `l2_deposit` und `l2_withdraw` RPCs entfernen
2. IF ein User die alten RPCs aufruft THEN SHALL das System einen Fehler mit Migrationsinformationen zurückgeben
3. THE Fehlermeldung SHALL erklären wie das neue Burn-and-Mint System funktioniert
4. THE System SHALL alle Bridge-bezogenen Code-Pfade entfernen
5. THE Dokumentation SHALL aktualisiert werden um das neue Modell zu erklären

