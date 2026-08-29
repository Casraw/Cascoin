#!/bin/bash
# =============================================================================
# CascoinToken Deployment Script
# =============================================================================
#
# Dieses Skript deployed den CascoinToken Contract auf die L2-Chain.
# Es unterstützt sowohl Solidity-Kompilierung (falls solc verfügbar)
# als auch direktes CVM-Bytecode-Deployment.
#
# Verwendung:
#   ./deploy_token.sh [--datadir <path>] [--supply <amount>] [--cvm]
#
# Optionen:
#   --datadir <path>   Datenverzeichnis (Standard: ~/.cascoin-l2-demo)
#   --supply <amount>  Initiale Token-Menge (Standard: 1000000)
#   --cvm              Verwende CVM-Bytecode statt Solidity
#   --testnet          Verwende Testnet statt Regtest
#   --help             Diese Hilfe anzeigen
#
# Requirements: 3.2
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONTRACT_DIR="${SCRIPT_DIR}"
L2_DEMO_DIR="${SCRIPT_DIR}/../l2-chain"

# Standard-Werte
DEFAULT_DATADIR="${HOME}/.cascoin-l2-demo"
DEFAULT_SUPPLY=1000000
USE_CVM=false
NETWORK_MODE="regtest"

# RPC-Credentials (müssen mit setup_l2_demo.sh übereinstimmen)
RPC_USER="demo"
RPC_PASSWORD="demo"

# Gas-Limits
GAS_LIMIT=500000
GAS_PRICE=1

# Farben für Ausgabe
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# =============================================================================
# Hilfsfunktionen
# =============================================================================

print_header() {
    echo -e "${BLUE}=============================================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}=============================================================================${NC}"
}

print_step() {
    echo -e "${GREEN}[STEP]${NC} $1"
}

print_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_contract() {
    echo -e "${CYAN}[CONTRACT]${NC} $1"
}

# Fehlerbehandlung
cleanup_on_error() {
    print_error "Ein Fehler ist aufgetreten auf Zeile $1"
    exit 1
}

trap 'cleanup_on_error $LINENO' ERR

# Finde die Cascoin Binaries
find_binaries() {
    local repo_root="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
    
    if [ -x "${repo_root}/src/cascoin-cli" ]; then
        CASCOIN_CLI="${repo_root}/src/cascoin-cli"
        print_info "Verwende lokale Build-Binary: ${CASCOIN_CLI}"
    elif command -v cascoin-cli &> /dev/null; then
        CASCOIN_CLI="cascoin-cli"
        print_info "Verwende System-Binary: cascoin-cli"
    else
        print_error "cascoin-cli nicht gefunden. Bitte bauen Sie das Projekt mit 'make'."
        exit 1
    fi
}

# Wrapper für cascoin-cli mit RPC-Credentials
cli() {
    if [ "$NETWORK_MODE" = "testnet" ]; then
        $CASCOIN_CLI -testnet -datadir="${DATADIR}" -rpcuser="${RPC_USER}" -rpcpassword="${RPC_PASSWORD}" "$@"
    else
        $CASCOIN_CLI -regtest -datadir="${DATADIR}" -rpcuser="${RPC_USER}" -rpcpassword="${RPC_PASSWORD}" "$@"
    fi
}

# Prüfe ob Node läuft
check_node() {
    if ! cli getblockchaininfo &>/dev/null; then
        print_error "Cascoin Node läuft nicht!"
        print_info "Starten Sie zuerst die L2 Demo Chain:"
        print_info "  cd ${L2_DEMO_DIR} && ./setup_l2_demo.sh"
        exit 1
    fi
}

# Prüfe ob L2 aktiviert ist
check_l2() {
    local result
    result=$(cli l2_getchaininfo 2>&1)
    if [ $? -ne 0 ]; then
        print_error "L2 ist nicht aktiviert!"
        print_info "Starten Sie den Node mit -l2=1"
        exit 1
    fi
    
    if ! echo "$result" | grep -q '"enabled": 1\|"enabled":1\|"enabled": true'; then
        print_error "L2 ist nicht aktiviert!"
        exit 1
    fi
}

# Prüfe ob solc verfügbar ist
check_solc() {
    if command -v solc &> /dev/null; then
        SOLC_VERSION=$(solc --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
        print_info "Solidity Compiler gefunden: solc ${SOLC_VERSION}"
        return 0
    else
        return 1
    fi
}

# Kompiliere Solidity Contract
compile_solidity() {
    local contract_file="$1"
    local output_dir="${SCRIPT_DIR}/build"
    
    mkdir -p "${output_dir}"
    
    print_step "Kompiliere Solidity Contract..."
    print_info "Input: ${contract_file}"
    
    # Kompiliere mit solc
    solc --bin --abi --optimize --optimize-runs 200 \
        -o "${output_dir}" \
        --overwrite \
        "${contract_file}"
    
    # Lese Bytecode
    local contract_name=$(basename "${contract_file}" .sol)
    BYTECODE=$(cat "${output_dir}/${contract_name}.bin")
    ABI=$(cat "${output_dir}/${contract_name}.abi")
    
    print_success "Kompilierung erfolgreich"
    print_info "Bytecode-Länge: ${#BYTECODE} Zeichen"
}

# Generiere CVM Bytecode (vereinfacht)
generate_cvm_bytecode() {
    print_step "Generiere CVM Bytecode..."
    
    # Vereinfachter CVM Bytecode für Token
    # Dies ist eine Demo-Implementation
    
    # Constructor Bytecode:
    # CALLER (0x72) - Get deployer
    # PUSH 0x01 (0x01 0x01 0x01) - Owner storage key
    # SSTORE (0x51) - Store owner
    # PUSH supply (0x01 0x08 ...) - Initial supply
    # PUSH 0x00 (0x01 0x01 0x00) - TotalSupply key
    # SSTORE (0x51) - Store totalSupply
    # CALLER (0x72) - Get deployer
    # PUSH 0x10 (0x01 0x01 0x10) - Balance prefix
    # ADD (0x10) - Calculate balance key
    # PUSH supply - Initial supply
    # SWAP (0x04)
    # SSTORE (0x51) - Store deployer balance
    # STOP (0x44)
    
    # Encode initial supply (1M * 10^18 = 0xD3C21BCECCEDA1000000)
    local supply_hex="D3C21BCECCEDA1000000"
    
    BYTECODE="72"                    # CALLER
    BYTECODE+="010101"               # PUSH 1 0x01
    BYTECODE+="51"                   # SSTORE (owner)
    BYTECODE+="010B${supply_hex}"    # PUSH 11 bytes (supply)
    BYTECODE+="010100"               # PUSH 1 0x00
    BYTECODE+="51"                   # SSTORE (totalSupply)
    BYTECODE+="72"                   # CALLER
    BYTECODE+="010110"               # PUSH 1 0x10
    BYTECODE+="10"                   # ADD
    BYTECODE+="010B${supply_hex}"    # PUSH 11 bytes (supply)
    BYTECODE+="04"                   # SWAP
    BYTECODE+="51"                   # SSTORE (balance)
    BYTECODE+="44"                   # STOP
    
    print_success "CVM Bytecode generiert"
    print_info "Bytecode-Länge: ${#BYTECODE} Zeichen"
}

# =============================================================================
# Argument-Parsing
# =============================================================================

DATADIR="${DEFAULT_DATADIR}"
INITIAL_SUPPLY="${DEFAULT_SUPPLY}"

while [[ $# -gt 0 ]]; do
    case $1 in
        --datadir)
            DATADIR="$2"
            shift 2
            ;;
        --supply)
            INITIAL_SUPPLY="$2"
            shift 2
            ;;
        --cvm)
            USE_CVM=true
            shift
            ;;
        --testnet)
            NETWORK_MODE="testnet"
            if [ "$DATADIR" = "${DEFAULT_DATADIR}" ]; then
                DATADIR="${HOME}/.cascoin-l2-testnet"
            fi
            shift
            ;;
        --help|-h)
            echo "Verwendung: $0 [OPTIONEN]"
            echo ""
            echo "Optionen:"
            echo "  --datadir <path>   Datenverzeichnis (Standard: ${DEFAULT_DATADIR})"
            echo "  --supply <amount>  Initiale Token-Menge (Standard: ${DEFAULT_SUPPLY})"
            echo "  --cvm              Verwende CVM-Bytecode statt Solidity"
            echo "  --testnet          Verwende Testnet statt Regtest"
            echo "  --help, -h         Diese Hilfe anzeigen"
            echo ""
            echo "Beispiele:"
            echo "  $0                           # Deploy mit Standardwerten"
            echo "  $0 --supply 5000000          # Deploy mit 5M Token"
            echo "  $0 --cvm                     # Deploy mit CVM-Bytecode"
            exit 0
            ;;
        *)
            print_error "Unbekannte Option: $1"
            exit 1
            ;;
    esac
done

# =============================================================================
# Hauptprogramm
# =============================================================================

print_header "CascoinToken Deployment"

echo ""
print_info "Konfiguration:"
print_info "  Netzwerk:         ${NETWORK_MODE}"
print_info "  Datenverzeichnis: ${DATADIR}"
print_info "  Initial Supply:   ${INITIAL_SUPPLY} CDEMO"
print_info "  Deployment-Modus: $([ "$USE_CVM" = true ] && echo "CVM Bytecode" || echo "Solidity")"
echo ""

# -----------------------------------------------------------------------------
# Schritt 1: Voraussetzungen prüfen
# -----------------------------------------------------------------------------

print_step "Prüfe Voraussetzungen..."

find_binaries
check_node
check_l2

print_success "Alle Voraussetzungen erfüllt"

# -----------------------------------------------------------------------------
# Schritt 2: Deployer-Adresse erstellen
# -----------------------------------------------------------------------------

print_step "Erstelle Deployer-Adresse..."

DEPLOYER=$(cli getnewaddress "token_deployer")
print_info "Deployer-Adresse: ${DEPLOYER}"

# Prüfe Balance
BALANCE=$(cli getbalance)
print_info "Wallet-Balance: ${BALANCE} CAS"

# Für Regtest: Generiere Blöcke falls nötig
if [ "$NETWORK_MODE" = "regtest" ]; then
    if [ "$(echo "$BALANCE < 1" | bc -l 2>/dev/null || echo "1")" = "1" ]; then
        print_info "Generiere Mining-Rewards..."
        cli generatetoaddress 10 "${DEPLOYER}" > /dev/null
        BALANCE=$(cli getbalance)
        print_info "Neue Balance: ${BALANCE} CAS"
    fi
fi

# -----------------------------------------------------------------------------
# Schritt 3: Contract kompilieren/generieren
# -----------------------------------------------------------------------------

if [ "$USE_CVM" = true ]; then
    generate_cvm_bytecode
else
    if check_solc; then
        compile_solidity "${CONTRACT_DIR}/CascoinToken.sol"
    else
        print_info "solc nicht gefunden - verwende CVM-Bytecode"
        USE_CVM=true
        generate_cvm_bytecode
    fi
fi

# -----------------------------------------------------------------------------
# Schritt 4: Contract deployen
# -----------------------------------------------------------------------------

print_step "Deploye Contract auf L2..."

# Konvertiere Gas-Limit zu Hex
GAS_LIMIT_HEX=$(printf '%x' ${GAS_LIMIT})

# Erstelle Deployment-Transaktion
# Hinweis: Die genaue RPC-Syntax hängt von der CVM-Implementation ab
print_info "Sende Deployment-Transaktion..."

# Versuche verschiedene RPC-Methoden
DEPLOY_RESULT=""

# Methode 1: cas_sendTransaction (EVM-kompatibel)
if [ -z "$DEPLOY_RESULT" ]; then
    DEPLOY_RESULT=$(cli cas_sendTransaction "{
        \"from\": \"${DEPLOYER}\",
        \"data\": \"0x${BYTECODE}\",
        \"gas\": \"0x${GAS_LIMIT_HEX}\"
    }" 2>&1) || DEPLOY_RESULT=""
fi

# Methode 2: deploycontract (CVM-spezifisch)
if [ -z "$DEPLOY_RESULT" ] || echo "$DEPLOY_RESULT" | grep -qi "error\|not found\|unknown"; then
    print_info "Versuche deploycontract RPC..."
    DEPLOY_RESULT=$(cli deploycontract "${BYTECODE}" ${GAS_LIMIT} 2>&1) || DEPLOY_RESULT=""
fi

# Methode 3: sendcvmcontract
if [ -z "$DEPLOY_RESULT" ] || echo "$DEPLOY_RESULT" | grep -qi "error\|not found\|unknown"; then
    print_info "Versuche sendcvmcontract RPC..."
    DEPLOY_RESULT=$(cli sendcvmcontract "${BYTECODE}" ${GAS_LIMIT} 2>&1) || DEPLOY_RESULT=""
fi

# Prüfe Ergebnis
if [ -z "$DEPLOY_RESULT" ] || echo "$DEPLOY_RESULT" | grep -qi "error\|not found\|unknown"; then
    print_error "Contract-Deployment fehlgeschlagen!"
    print_info "Mögliche Ursachen:"
    print_info "  - CVM RPC-Befehle noch nicht implementiert"
    print_info "  - Nicht genug Gas oder Balance"
    print_info "  - L2 nicht korrekt konfiguriert"
    echo ""
    print_info "Deployment-Versuch Ergebnis:"
    echo "${DEPLOY_RESULT}"
    echo ""
    print_info "Für manuelle Deployment-Versuche:"
    print_info "  cascoin-cli -regtest deploycontract <bytecode> <gas_limit>"
    exit 1
fi

# Extrahiere Transaction Hash
TX_HASH=$(echo "${DEPLOY_RESULT}" | grep -oE '[a-f0-9]{64}' | head -1)

if [ -z "$TX_HASH" ]; then
    TX_HASH=$(echo "${DEPLOY_RESULT}" | grep -o '"txid": "[^"]*"' | cut -d'"' -f4)
fi

if [ -z "$TX_HASH" ]; then
    TX_HASH=$(echo "${DEPLOY_RESULT}" | grep -o '"hash": "[^"]*"' | cut -d'"' -f4)
fi

print_info "Transaction Hash: ${TX_HASH:-"(nicht verfügbar)"}"

# -----------------------------------------------------------------------------
# Schritt 5: Auf Bestätigung warten
# -----------------------------------------------------------------------------

print_step "Warte auf Bestätigung..."

if [ "$NETWORK_MODE" = "regtest" ]; then
    # Generiere Block für Bestätigung
    print_info "Generiere Bestätigungsblock..."
    cli generatetoaddress 1 "${DEPLOYER}" > /dev/null
    sleep 1
else
    print_info "Warte auf Testnet-Bestätigung..."
    sleep 30
fi

# -----------------------------------------------------------------------------
# Schritt 6: Contract-Adresse abrufen
# -----------------------------------------------------------------------------

print_step "Rufe Contract-Adresse ab..."

CONTRACT_ADDRESS=""

# Versuche Receipt abzurufen
if [ -n "$TX_HASH" ]; then
    # Methode 1: cas_getTransactionReceipt
    RECEIPT=$(cli cas_getTransactionReceipt "${TX_HASH}" 2>&1) || RECEIPT=""
    
    if [ -n "$RECEIPT" ] && ! echo "$RECEIPT" | grep -qi "error\|not found"; then
        CONTRACT_ADDRESS=$(echo "${RECEIPT}" | grep -o '"contractAddress": "[^"]*"' | cut -d'"' -f4)
    fi
    
    # Methode 2: getcontractinfo
    if [ -z "$CONTRACT_ADDRESS" ]; then
        CONTRACT_INFO=$(cli getcontractinfo "${TX_HASH}" 2>&1) || CONTRACT_INFO=""
        if [ -n "$CONTRACT_INFO" ] && ! echo "$CONTRACT_INFO" | grep -qi "error"; then
            CONTRACT_ADDRESS=$(echo "${CONTRACT_INFO}" | grep -o '"address": "[^"]*"' | cut -d'"' -f4)
        fi
    fi
fi

# Fallback: Berechne Contract-Adresse
if [ -z "$CONTRACT_ADDRESS" ]; then
    print_info "Contract-Adresse konnte nicht abgerufen werden"
    print_info "Die Adresse wird aus Deployer + Nonce berechnet"
    CONTRACT_ADDRESS="(wird nach Bestätigung verfügbar)"
fi

# -----------------------------------------------------------------------------
# Zusammenfassung
# -----------------------------------------------------------------------------

print_header "Deployment abgeschlossen!"

echo ""
print_contract "=== CascoinToken Contract ==="
echo ""
echo "  Name:             Cascoin Demo Token"
echo "  Symbol:           CDEMO"
echo "  Decimals:         18"
echo "  Initial Supply:   ${INITIAL_SUPPLY} CDEMO"
echo ""
echo "  Deployer:         ${DEPLOYER}"
echo "  Contract Address: ${CONTRACT_ADDRESS}"
echo "  Transaction:      ${TX_HASH:-"N/A"}"
echo ""

# Speichere Contract-Info
CONTRACT_INFO_FILE="${DATADIR}/token_contract.sh"
cat > "${CONTRACT_INFO_FILE}" << EOF
# CascoinToken Contract Info
export TOKEN_NAME="Cascoin Demo Token"
export TOKEN_SYMBOL="CDEMO"
export TOKEN_DECIMALS=18
export TOKEN_SUPPLY=${INITIAL_SUPPLY}
export TOKEN_DEPLOYER="${DEPLOYER}"
export TOKEN_ADDRESS="${CONTRACT_ADDRESS}"
export TOKEN_TX="${TX_HASH}"
export TOKEN_DEPLOY_TIME="$(date -Iseconds)"
EOF

print_success "Contract-Info gespeichert in ${CONTRACT_INFO_FILE}"

echo ""
echo "Nächste Schritte:"
echo "  1. Token-Transfer testen: ./demo_transfer.sh"
echo "  2. Balance abfragen:"
NETWORK_FLAG="-regtest"
[ "$NETWORK_MODE" = "testnet" ] && NETWORK_FLAG="-testnet"
echo "     cascoin-cli ${NETWORK_FLAG} callcontract ${CONTRACT_ADDRESS} balanceOf ${DEPLOYER}"
echo ""
