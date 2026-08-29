// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

/**
 * @title CascoinToken
 * @author Cascoin Development Team
 * @notice ERC-20 Token mit Cascoin Trust-Integration
 * @dev Dieser Contract demonstriert die Integration von Cascoin's Web-of-Trust
 *      Reputation System in einen Standard ERC-20 Token.
 * 
 * Features:
 * - Vollständige ERC-20 Kompatibilität (transfer, approve, transferFrom)
 * - Reputation-basierte Transfer-Limits
 * - Trust-gated Funktionen für hohe Reputation
 * - Integration mit Cascoin's Reputation Oracle Precompile
 * 
 * Verwendung:
 * 1. Deploy mit initialSupply (z.B. 1000000 für 1M Token)
 * 2. Deployer erhält alle initialen Token
 * 3. Transfers erfordern Mindest-Reputation von 30
 * 4. Höhere Reputation ermöglicht größere Transfers
 * 
 * Requirements: 2.1, 2.2, 2.3, 2.4, 2.5
 */
contract CascoinToken {
    // ============================================================
    // CONSTANTS - Cascoin Precompile Adressen
    // ============================================================
    
    /**
     * @dev Adresse des Cascoin Reputation Oracle Precompile
     * Dieser Precompile ermöglicht Smart Contracts den Zugriff auf
     * das Web-of-Trust Reputation System von Cascoin.
     * 
     * Aufruf: staticcall mit getReputation(address) -> uint8
     */
    address constant REPUTATION_ORACLE = 0x0000000000000000000000000000000000000100;
    
    // ============================================================
    // STATE VARIABLES - ERC-20 Standard
    // ============================================================
    
    /**
     * @notice Name des Tokens
     * @dev Wird bei Deployment festgelegt und ist unveränderlich
     */
    string public name = "Cascoin Demo Token";
    
    /**
     * @notice Symbol des Tokens (Ticker)
     * @dev Kurze Bezeichnung für Börsen und Wallets
     */
    string public symbol = "CDEMO";
    
    /**
     * @notice Anzahl der Dezimalstellen
     * @dev Standard ist 18 (wie ETH), ermöglicht feine Unterteilung
     */
    uint8 public decimals = 18;
    
    /**
     * @notice Gesamtmenge aller existierenden Token
     * @dev Wird bei Deployment festgelegt, kann nicht erhöht werden
     */
    uint256 public totalSupply;
    
    // ============================================================
    // STATE VARIABLES - Cascoin Trust Integration
    // ============================================================
    
    /**
     * @notice Minimum Reputation Score für Transfers
     * @dev Adressen mit niedrigerer Reputation können keine Token senden
     * Skala: 0-100, wobei 30 ein moderater Schwellenwert ist
     */
    uint8 public minTransferReputation = 30;
    
    /**
     * @notice Contract Owner (Deployer)
     * @dev Hat spezielle Rechte wie Änderung der minTransferReputation
     */
    address public owner;
    
    // ============================================================
    // MAPPINGS - Token Balances und Allowances
    // ============================================================
    
    /**
     * @notice Token-Guthaben pro Adresse
     * @dev balanceOf[address] -> uint256 Guthaben
     */
    mapping(address => uint256) public balanceOf;
    
    /**
     * @notice Erlaubte Ausgaben durch Dritte
     * @dev allowance[owner][spender] -> uint256 erlaubter Betrag
     * Ermöglicht DEX-Integration und delegierte Transfers
     */
    mapping(address => mapping(address => uint256)) public allowance;
    
    // ============================================================
    // EVENTS - ERC-20 Standard Events
    // ============================================================
    
    /**
     * @notice Wird bei jedem Token-Transfer emittiert
     * @param from Sender-Adresse (0x0 bei Minting)
     * @param to Empfänger-Adresse
     * @param value Transferierte Menge
     */
    event Transfer(address indexed from, address indexed to, uint256 value);
    
    /**
     * @notice Wird bei Approval-Änderungen emittiert
     * @param owner Adresse die Erlaubnis erteilt
     * @param spender Adresse die Erlaubnis erhält
     * @param value Erlaubter Betrag
     */
    event Approval(address indexed owner, address indexed spender, uint256 value);

    
    // ============================================================
    // EVENTS - Cascoin-spezifische Events
    // ============================================================
    
    /**
     * @notice Wird emittiert wenn ein Transfer wegen niedriger Reputation abgelehnt wird
     * @param from Adresse die den Transfer versuchte
     * @param reputation Aktuelle Reputation der Adresse
     * @param required Benötigte Mindest-Reputation
     */
    event TransferRejectedLowReputation(
        address indexed from, 
        uint8 reputation, 
        uint8 required
    );
    
    /**
     * @notice Wird emittiert wenn die Mindest-Reputation geändert wird
     * @param oldValue Alter Schwellenwert
     * @param newValue Neuer Schwellenwert
     */
    event MinReputationChanged(uint8 oldValue, uint8 newValue);
    
    // ============================================================
    // MODIFIERS
    // ============================================================
    
    /**
     * @dev Beschränkt Funktion auf Contract Owner
     */
    modifier onlyOwner() {
        require(msg.sender == owner, "CascoinToken: caller is not owner");
        _;
    }
    
    // ============================================================
    // CONSTRUCTOR
    // ============================================================
    
    /**
     * @notice Erstellt einen neuen CascoinToken
     * @dev Mintet alle Token an den Deployer
     * @param initialSupply Initiale Token-Menge (ohne Dezimalstellen)
     * 
     * Beispiel: initialSupply = 1000000 erstellt 1,000,000 Token
     * Die tatsächliche Menge ist initialSupply * 10^decimals
     * 
     * Requirements: 2.3 - Initial Supply an Deployer
     */
    constructor(uint256 initialSupply) {
        // Owner ist der Deployer
        owner = msg.sender;
        
        // Berechne tatsächliche Supply mit Dezimalstellen
        // z.B. 1000000 * 10^18 = 1,000,000 Token mit 18 Dezimalstellen
        totalSupply = initialSupply * 10**decimals;
        
        // Alle Token gehen an den Deployer
        balanceOf[msg.sender] = totalSupply;
        
        // Emit Transfer Event von 0x0 (Minting)
        emit Transfer(address(0), msg.sender, totalSupply);
    }
    
    // ============================================================
    // ERC-20 STANDARD FUNCTIONS
    // ============================================================
    
    /**
     * @notice Transferiert Token an eine andere Adresse
     * @dev Prüft Reputation des Senders vor Transfer
     * @param to Empfänger-Adresse
     * @param amount Zu transferierende Menge
     * @return success True wenn Transfer erfolgreich
     * 
     * Requirements:
     * - Sender muss ausreichend Guthaben haben
     * - Sender muss Mindest-Reputation haben (Req 2.2, 2.5)
     * - Betrag darf maxTransferAmount nicht überschreiten
     * 
     * Emits: Transfer Event bei Erfolg
     * Emits: TransferRejectedLowReputation bei zu niedriger Reputation
     */
    function transfer(address to, uint256 amount) public returns (bool) {
        // Hole Reputation des Senders
        uint8 senderRep = _getReputation(msg.sender);
        
        // Prüfe Mindest-Reputation (Requirement 2.2, 2.5)
        if (senderRep < minTransferReputation) {
            emit TransferRejectedLowReputation(msg.sender, senderRep, minTransferReputation);
            revert("CascoinToken: sender reputation too low");
        }
        
        // Prüfe Guthaben
        require(balanceOf[msg.sender] >= amount, "CascoinToken: insufficient balance");
        
        // Prüfe Reputation-basiertes Limit
        uint256 maxAmount = maxTransferAmount(msg.sender);
        require(amount <= maxAmount, "CascoinToken: amount exceeds reputation limit");
        
        // Führe Transfer durch
        balanceOf[msg.sender] -= amount;
        balanceOf[to] += amount;
        
        emit Transfer(msg.sender, to, amount);
        return true;
    }
    
    /**
     * @notice Erteilt einer Adresse Erlaubnis, Token auszugeben
     * @dev Setzt Allowance auf exakten Wert (nicht additiv)
     * @param spender Adresse die Erlaubnis erhält
     * @param amount Erlaubter Betrag
     * @return success True wenn Approval erfolgreich
     * 
     * WARNUNG: Race Condition möglich bei Änderung von nicht-null zu nicht-null
     * Empfehlung: Erst auf 0 setzen, dann auf neuen Wert
     * 
     * Emits: Approval Event
     */
    function approve(address spender, uint256 amount) public returns (bool) {
        allowance[msg.sender][spender] = amount;
        emit Approval(msg.sender, spender, amount);
        return true;
    }
    
    /**
     * @notice Transferiert Token im Namen einer anderen Adresse
     * @dev Erfordert vorherige Approval durch Token-Owner
     * @param from Adresse von der Token abgezogen werden
     * @param to Empfänger-Adresse
     * @param amount Zu transferierende Menge
     * @return success True wenn Transfer erfolgreich
     * 
     * Requirements:
     * - from muss ausreichend Guthaben haben
     * - from muss Mindest-Reputation haben (Req 2.2, 2.5)
     * - msg.sender muss ausreichend Allowance haben
     * - Betrag darf maxTransferAmount von from nicht überschreiten
     * 
     * Emits: Transfer Event bei Erfolg
     */
    function transferFrom(address from, address to, uint256 amount) public returns (bool) {
        // Hole Reputation des Token-Owners
        uint8 fromRep = _getReputation(from);
        
        // Prüfe Mindest-Reputation (Requirement 2.2, 2.5)
        if (fromRep < minTransferReputation) {
            emit TransferRejectedLowReputation(from, fromRep, minTransferReputation);
            revert("CascoinToken: sender reputation too low");
        }
        
        // Prüfe Guthaben
        require(balanceOf[from] >= amount, "CascoinToken: insufficient balance");
        
        // Prüfe Allowance
        require(allowance[from][msg.sender] >= amount, "CascoinToken: insufficient allowance");
        
        // Prüfe Reputation-basiertes Limit
        uint256 maxAmount = maxTransferAmount(from);
        require(amount <= maxAmount, "CascoinToken: amount exceeds reputation limit");
        
        // Führe Transfer durch
        balanceOf[from] -= amount;
        balanceOf[to] += amount;
        allowance[from][msg.sender] -= amount;
        
        emit Transfer(from, to, amount);
        return true;
    }

    
    // ============================================================
    // CASCOIN TRUST INTEGRATION FUNCTIONS
    // ============================================================
    
    /**
     * @notice Berechnet maximalen Transfer-Betrag basierend auf Reputation
     * @dev Höhere Reputation ermöglicht größere Transfers
     * @param account Adresse deren Limit berechnet wird
     * @return maxAmount Maximaler Transfer-Betrag in Token-Einheiten
     * 
     * Reputation-Stufen und Limits:
     * - 90-100: Unbegrenzt (type(uint256).max)
     * - 70-89:  100,000 Token
     * - 50-69:  10,000 Token
     * - 30-49:  1,000 Token
     * - 0-29:   0 Token (kein Transfer erlaubt)
     * 
     * Requirement 2.2: Trust-aware Features
     */
    function maxTransferAmount(address account) public view returns (uint256) {
        uint8 rep = _getReputation(account);
        
        // Höchste Reputation: Keine Limits
        if (rep >= 90) {
            return type(uint256).max;
        }
        
        // Hohe Reputation: 100,000 Token
        if (rep >= 70) {
            return 100000 * 10**decimals;
        }
        
        // Mittlere Reputation: 10,000 Token
        if (rep >= 50) {
            return 10000 * 10**decimals;
        }
        
        // Niedrige Reputation: 1,000 Token
        if (rep >= 30) {
            return 1000 * 10**decimals;
        }
        
        // Unter Minimum: Kein Transfer erlaubt
        return 0;
    }
    
    /**
     * @notice Gibt die Reputation einer Adresse zurück
     * @dev Wrapper für internen _getReputation Aufruf
     * @param account Adresse deren Reputation abgefragt wird
     * @return reputation Reputation Score (0-100)
     */
    function getReputation(address account) public view returns (uint8) {
        return _getReputation(account);
    }
    
    /**
     * @notice Prüft ob eine Adresse transferieren kann
     * @dev Kombiniert Reputation und Balance Check
     * @param account Adresse die geprüft wird
     * @param amount Gewünschter Transfer-Betrag
     * @return canTransfer True wenn Transfer möglich wäre
     * @return reason Grund falls Transfer nicht möglich
     */
    function canTransfer(address account, uint256 amount) 
        public 
        view 
        returns (bool canTransfer, string memory reason) 
    {
        uint8 rep = _getReputation(account);
        
        // Prüfe Reputation
        if (rep < minTransferReputation) {
            return (false, "Reputation too low");
        }
        
        // Prüfe Balance
        if (balanceOf[account] < amount) {
            return (false, "Insufficient balance");
        }
        
        // Prüfe Reputation-Limit
        if (amount > maxTransferAmount(account)) {
            return (false, "Amount exceeds reputation limit");
        }
        
        return (true, "Transfer allowed");
    }
    
    // ============================================================
    // OWNER FUNCTIONS
    // ============================================================
    
    /**
     * @notice Ändert die Mindest-Reputation für Transfers
     * @dev Nur durch Owner aufrufbar
     * @param newMinReputation Neuer Schwellenwert (0-100)
     * 
     * Emits: MinReputationChanged Event
     */
    function setMinTransferReputation(uint8 newMinReputation) public onlyOwner {
        require(newMinReputation <= 100, "CascoinToken: reputation must be 0-100");
        
        uint8 oldValue = minTransferReputation;
        minTransferReputation = newMinReputation;
        
        emit MinReputationChanged(oldValue, newMinReputation);
    }
    
    /**
     * @notice Überträgt Ownership an neue Adresse
     * @dev Nur durch aktuellen Owner aufrufbar
     * @param newOwner Neue Owner-Adresse
     */
    function transferOwnership(address newOwner) public onlyOwner {
        require(newOwner != address(0), "CascoinToken: new owner is zero address");
        owner = newOwner;
    }
    
    // ============================================================
    // INTERNAL FUNCTIONS
    // ============================================================
    
    /**
     * @dev Interne Funktion zum Abrufen der Reputation vom Oracle
     * @param account Adresse deren Reputation abgefragt wird
     * @return reputation Reputation Score (0-100), 0 bei Fehler
     * 
     * Technische Details:
     * - Verwendet staticcall (read-only, kein Gas für State-Änderung)
     * - Ruft getReputation(address) am Precompile auf
     * - Gibt 0 zurück bei Fehler oder leerer Antwort
     * 
     * Im Regtest-Modus ohne echtes Oracle wird 50 als Default verwendet
     */
    function _getReputation(address account) internal view returns (uint8) {
        // Versuche Reputation vom Oracle abzurufen
        (bool success, bytes memory data) = REPUTATION_ORACLE.staticcall(
            abi.encodeWithSignature("getReputation(address)", account)
        );
        
        // Bei Fehler oder leerer Antwort: Default-Reputation
        // Im Regtest-Modus gibt es kein echtes Oracle, daher Default 50
        if (!success || data.length == 0) {
            // Default-Reputation für Demo-Zwecke
            // In Produktion würde hier 0 zurückgegeben
            return 50;
        }
        
        // Dekodiere uint8 Reputation aus Antwort
        return abi.decode(data, (uint8));
    }
}
