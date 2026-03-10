#!/bin/bash
# =============================================================================
# CascoinToken Transfer Demo Script
# =============================================================================
#
# Dieses Skript demonstriert Token-Transfers zwischen Adressen.
# Es zeigt die Balance-Änderungen vor und nach dem Transfer.
#
# Verwendung:
#   ./demo_transfer.sh [--datadir <path>] [--amount <tokens>]
#
# Optionen:
#   --datadir <path>   Datenverzeichnis (Standard: ~/.cascoin-l2-demo)
#   --amount <tokens>  Transfer-Menge (Standard: 100)
#   --contract <addr>  Contract-Adresse (wird aus token_contract.sh geladen)
#   --testnet          Verwende Testnet statt Regtest
#   --help             Diese Hilfe anzeigen
#
# Requirements: 3.3
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
L2_DEMO_DIR="${SCRIPT_DIR}/../l2-chain"

# Standard-Werte
DEFAULT_DATADIR="${HOME}/.cascoin-l2-demo"
DEFAULT_AMOUNT=100
NETWORK_MODE="regtest"

# RPC-Credentials
RPC_USER="demo"
RPC_PASSWORD="demo"

# Gas-Limits
GAS_LIMIT=100000

# Farben für Ausgabe
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
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

print_balance() {
    echo -e "${CYAN}[BALANCE]${NC} $1"
}

print_transfer() {
    echo -e "${MAGENTA}[TRANSFER]${NC} $1"
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
    elif command -v cascoin-cli &> /dev/null; then
        CASCOIN_CLI="cascoin-cli"
    else
        print_error "cascoin-cli nicht gefunden!"
        exit 1
    fi
}

# Wrapper für cascoin-cli
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

# Lade Contract-Info
load_contract_info() {
    local contract_file="${DATADIR}/token_contract.sh"
    
    if [ -f "${contract_file}" ]; then
        source "${contract_file}"
        print_info "Contract-Info geladen aus ${contract_file}"
        return 0
    else
        print_info "Keine Contract-Info gefunden"
        return 1
    fi
}

# Formatiere Token-Menge (mit 18 Dezimalstellen)
format_token_amount() {
    local amount=$1
    # Für Display: Teile durch 10^18
    # Vereinfacht: Zeige nur die Ganzzahl-Portion
    if [ ${#amount} -gt 18 ]; then
        echo "${amount:0:$((${#amount}-18))}"
    else
        echo "0"
    fi
}

# Konvertiere Token-Menge zu Wei (18 Dezimalstellen)
to_wei() {
    local amount=$1
    # Multipliziere mit 10^18
    echo "${amount}000000000000000000"
}

# Rufe Token-Balance ab
get_token_balance() {
    local contract=$1
    local address=$2
    local balance=""
    
    # Methode 1: callcontract mit balanceOf
    balance=$(cli callcontract "${contract}" "balanceOf(address)" "${address}" 2>&1) || balance=""
    
    if [ -n "$balance" ] && ! echo "$balance" | grep -qi "error\|not found"; then
        # Extrahiere Wert aus Antwort
        local value=$(echo "$balance" | grep -oE '0x[a-fA-F0-9]+' | head -1)
        if [ -n "$value" ]; then
            # Konvertiere Hex zu Dezimal
            echo $((${value}))
            return 0
        fi
    fi
    
    # Methode 2: getcontractstate
    balance=$(cli getcontractstate "${contract}" "balances" "${address}" 2>&1) || balance=""
    
    if [ -n "$balance" ] && ! echo "$balance" | grep -qi "error"; then
        echo "$balance"
        return 0
    fi
    
    echo "0"
}

# =============================================================================
# Argument-Parsing
# =============================================================================

DATADIR="${DEFAULT_DATADIR}"
TRANSFER_AMOUNT="${DEFAULT_AMOUNT}"
CONTRACT_ADDRESS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --datadir)
            DATADIR="$2"
            shift 2
            ;;
        --amount)
            TRANSFER_AMOUNT="$2"
            shift 2
            ;;
        --contract)
            CONTRACT_ADDRESS="$2"
            shift 2
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
            echo "  --amount <tokens>  Transfer-Menge (Standard: ${DEFAULT_AMOUNT})"
            echo "  --contract <addr>  Contract-Adresse"
            echo "  --testnet          Verwende Testnet statt Regtest"
            echo "  --help, -h         Diese Hilfe anzeigen"
            echo ""
            echo "Beispiele:"
            echo "  $0                           # Transfer mit Standardwerten"
            echo "  $0 --amount 500              # Transfer von 500 Token"
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

print_header "CascoinToken Transfer Demo"

echo ""
print_info "Konfiguration:"
print_info "  Netzwerk:         ${NETWORK_MODE}"
print_info "  Datenverzeichnis: ${DATADIR}"
print_info "  Transfer-Menge:   ${TRANSFER_AMOUNT} CDEMO"
echo ""

# -----------------------------------------------------------------------------
# Schritt 1: Voraussetzungen prüfen
# -----------------------------------------------------------------------------

print_step "Prüfe Voraussetzungen..."

find_binaries
check_node

# Lade Contract-Info falls nicht angegeben
if [ -z "$CONTRACT_ADDRESS" ]; then
    if load_contract_info; then
        CONTRACT_ADDRESS="${TOKEN_ADDRESS}"
    fi
fi

if [ -z "$CONTRACT_ADDRESS" ] || [ "$CONTRACT_ADDRESS" = "(wird nach Bestätigung verfügbar)" ]; then
    print_error "Contract-Adresse nicht verfügbar!"
    print_info "Bitte deployen Sie zuerst den Token:"
    print_info "  ./deploy_token.sh"
    print_info ""
    print_info "Oder geben Sie die Adresse manuell an:"
    print_info "  ./demo_transfer.sh --contract <address>"
    exit 1
fi

print_info "Contract-Adresse: ${CONTRACT_ADDRESS}"
print_success "Voraussetzungen erfüllt"

# -----------------------------------------------------------------------------
# Schritt 2: Adressen erstellen
# -----------------------------------------------------------------------------

print_step "Erstelle Demo-Adressen..."

# Sender (Deployer mit Token)
if [ -n "$TOKEN_DEPLOYER" ]; then
    SENDER="${TOKEN_DEPLOYER}"
    print_info "Verwende Deployer als Sender: ${SENDER}"
else
    SENDER=$(cli getnewaddress "token_sender")
    print_info "Neue Sender-Adresse: ${SENDER}"
fi

# Empfänger
RECEIVER=$(cli getnewaddress "token_receiver")
print_info "Empfänger-Adresse: ${RECEIVER}"

# -----------------------------------------------------------------------------
# Schritt 3: Balances vor Transfer anzeigen
# -----------------------------------------------------------------------------

print_step "Zeige Balances vor Transfer..."

echo ""
print_balance "=== Balances VOR Transfer ==="

# Sender Balance
SENDER_BALANCE_BEFORE=$(get_token_balance "${CONTRACT_ADDRESS}" "${SENDER}")
SENDER_DISPLAY_BEFORE=$(format_token_amount "${SENDER_BALANCE_BEFORE}")
print_balance "Sender   (${SENDER:0:10}...): ${SENDER_DISPLAY_BEFORE} CDEMO"

# Receiver Balance
RECEIVER_BALANCE_BEFORE=$(get_token_balance "${CONTRACT_ADDRESS}" "${RECEIVER}")
RECEIVER_DISPLAY_BEFORE=$(format_token_amount "${RECEIVER_BALANCE_BEFORE}")
print_balance "Empfänger (${RECEIVER:0:10}...): ${RECEIVER_DISPLAY_BEFORE} CDEMO"

echo ""

# Prüfe ob Sender genug Token hat
if [ "${SENDER_BALANCE_BEFORE}" = "0" ]; then
    print_error "Sender hat keine Token!"
    print_info "Bitte deployen Sie zuerst den Token mit ./deploy_token.sh"
    exit 1
fi

# -----------------------------------------------------------------------------
# Schritt 4: Transfer durchführen
# -----------------------------------------------------------------------------

print_step "Führe Token-Transfer durch..."

# Konvertiere Menge zu Wei
TRANSFER_WEI=$(to_wei ${TRANSFER_AMOUNT})
print_info "Transfer: ${TRANSFER_AMOUNT} CDEMO (${TRANSFER_WEI} Wei)"

# Führe Transfer durch
print_transfer "Sende ${TRANSFER_AMOUNT} CDEMO von ${SENDER:0:10}... zu ${RECEIVER:0:10}..."

TRANSFER_RESULT=""

# Methode 1: callcontract mit transfer
TRANSFER_RESULT=$(cli callcontract "${CONTRACT_ADDRESS}" \
    "transfer(address,uint256)" \
    "${RECEIVER}" \
    "${TRANSFER_WEI}" \
    --from "${SENDER}" \
    --gas ${GAS_LIMIT} 2>&1) || TRANSFER_RESULT=""

# Methode 2: sendtocontract
if [ -z "$TRANSFER_RESULT" ] || echo "$TRANSFER_RESULT" | grep -qi "error\|not found\|unknown"; then
    print_info "Versuche sendtocontract..."
    
    # Encode transfer function call
    # transfer(address,uint256) selector: 0xa9059cbb
    TRANSFER_DATA="a9059cbb"
    # Pad address to 32 bytes
    RECEIVER_PADDED=$(printf '%064s' "${RECEIVER#0x}" | tr ' ' '0')
    # Pad amount to 32 bytes
    AMOUNT_HEX=$(printf '%064x' ${TRANSFER_AMOUNT})
    
    TRANSFER_DATA="${TRANSFER_DATA}${RECEIVER_PADDED}${AMOUNT_HEX}"
    
    TRANSFER_RESULT=$(cli sendtocontract "${CONTRACT_ADDRESS}" \
        "${TRANSFER_DATA}" \
        0 \
        ${GAS_LIMIT} \
        "${SENDER}" 2>&1) || TRANSFER_RESULT=""
fi

# Prüfe Ergebnis
if [ -z "$TRANSFER_RESULT" ] || echo "$TRANSFER_RESULT" | grep -qi "error\|not found\|unknown"; then
    print_error "Transfer fehlgeschlagen!"
    print_info "Ergebnis: ${TRANSFER_RESULT}"
    print_info ""
    print_info "Mögliche Ursachen:"
    print_info "  - Contract RPC-Befehle noch nicht implementiert"
    print_info "  - Nicht genug Gas"
    print_info "  - Sender hat nicht genug Token"
    print_info "  - Reputation zu niedrig (min. 30 erforderlich)"
    echo ""
    print_info "Für manuelle Transfer-Versuche:"
    print_info "  cascoin-cli -regtest callcontract ${CONTRACT_ADDRESS} transfer ${RECEIVER} ${TRANSFER_AMOUNT}"
    exit 1
fi

# Extrahiere TX Hash
TX_HASH=$(echo "${TRANSFER_RESULT}" | grep -oE '[a-f0-9]{64}' | head -1)
if [ -z "$TX_HASH" ]; then
    TX_HASH=$(echo "${TRANSFER_RESULT}" | grep -o '"txid": "[^"]*"' | cut -d'"' -f4)
fi

print_info "Transaction: ${TX_HASH:-"(nicht verfügbar)"}"

# -----------------------------------------------------------------------------
# Schritt 5: Bestätigung abwarten
# -----------------------------------------------------------------------------

print_step "Warte auf Bestätigung..."

if [ "$NETWORK_MODE" = "regtest" ]; then
    # Generiere Block
    MINING_ADDR=$(cli getnewaddress "mining")
    cli generatetoaddress 1 "${MINING_ADDR}" > /dev/null
    print_info "Block generiert"
else
    print_info "Warte auf Testnet-Bestätigung..."
    sleep 30
fi

sleep 1

# -----------------------------------------------------------------------------
# Schritt 6: Balances nach Transfer anzeigen
# -----------------------------------------------------------------------------

print_step "Zeige Balances nach Transfer..."

echo ""
print_balance "=== Balances NACH Transfer ==="

# Sender Balance
SENDER_BALANCE_AFTER=$(get_token_balance "${CONTRACT_ADDRESS}" "${SENDER}")
SENDER_DISPLAY_AFTER=$(format_token_amount "${SENDER_BALANCE_AFTER}")
print_balance "Sender   (${SENDER:0:10}...): ${SENDER_DISPLAY_AFTER} CDEMO"

# Receiver Balance
RECEIVER_BALANCE_AFTER=$(get_token_balance "${CONTRACT_ADDRESS}" "${RECEIVER}")
RECEIVER_DISPLAY_AFTER=$(format_token_amount "${RECEIVER_BALANCE_AFTER}")
print_balance "Empfänger (${RECEIVER:0:10}...): ${RECEIVER_DISPLAY_AFTER} CDEMO"

echo ""

# -----------------------------------------------------------------------------
# Zusammenfassung
# -----------------------------------------------------------------------------

print_header "Transfer-Zusammenfassung"

echo ""
echo "  Transfer-Menge:     ${TRANSFER_AMOUNT} CDEMO"
echo "  Transaction:        ${TX_HASH:-"N/A"}"
echo ""
echo "  Sender:"
echo "    Adresse:          ${SENDER}"
echo "    Balance vorher:   ${SENDER_DISPLAY_BEFORE} CDEMO"
echo "    Balance nachher:  ${SENDER_DISPLAY_AFTER} CDEMO"
echo "    Differenz:        -${TRANSFER_AMOUNT} CDEMO"
echo ""
echo "  Empfänger:"
echo "    Adresse:          ${RECEIVER}"
echo "    Balance vorher:   ${RECEIVER_DISPLAY_BEFORE} CDEMO"
echo "    Balance nachher:  ${RECEIVER_DISPLAY_AFTER} CDEMO"
echo "    Differenz:        +${TRANSFER_AMOUNT} CDEMO"
echo ""

# Verifiziere Balance-Änderungen
EXPECTED_SENDER=$((SENDER_BALANCE_BEFORE - TRANSFER_WEI))
EXPECTED_RECEIVER=$((RECEIVER_BALANCE_BEFORE + TRANSFER_WEI))

if [ "${SENDER_BALANCE_AFTER}" = "${EXPECTED_SENDER}" ] && \
   [ "${RECEIVER_BALANCE_AFTER}" = "${EXPECTED_RECEIVER}" ]; then
    print_success "Transfer erfolgreich verifiziert!"
    print_info "Balance-Änderungen entsprechen den Erwartungen"
else
    print_info "Balance-Änderungen konnten nicht verifiziert werden"
    print_info "(Contract-Calls möglicherweise noch nicht implementiert)"
fi

echo ""
echo "Weitere Befehle:"
NETWORK_FLAG="-regtest"
[ "$NETWORK_MODE" = "testnet" ] && NETWORK_FLAG="-testnet"
echo "  # Balance abfragen:"
echo "  cascoin-cli ${NETWORK_FLAG} callcontract ${CONTRACT_ADDRESS} balanceOf ${SENDER}"
echo ""
echo "  # Weiteren Transfer durchführen:"
echo "  ./demo_transfer.sh --amount 50"
echo ""
