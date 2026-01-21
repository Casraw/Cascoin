#!/bin/bash
# =============================================================================
# Cascoin L2 Mining Script
# =============================================================================
# 
# Dieses Skript generiert Blöcke und stellt sicher, dass L2-Burns gemintet werden.
# Es wartet auf Sequencer-Konsens und zeigt den Fortschritt an.
#
# Unterstützt Regtest (Standard) und Testnet (mit --testnet Flag).
#
# Verwendung: 
#   ./mine_l2_blocks.sh [--blocks <count>] [--datadir <path>]
#   ./mine_l2_blocks.sh --testnet [--datadir <path>]
#
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

DEFAULT_DATADIR_REGTEST="${HOME}/.cascoin-l2-demo"
DEFAULT_DATADIR_TESTNET="${HOME}/.cascoin-l2-testnet"
DEFAULT_BLOCKS=18
RPC_USER="demo"
RPC_PASSWORD="demo"

# Netzwerk-Modus (regtest oder testnet)
NETWORK_MODE="regtest"

# Farben
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# Finde Binaries
find_binaries() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local repo_root="$(cd "${script_dir}/../../.." && pwd)"
    
    if [ -x "${repo_root}/src/cascoin-cli" ]; then
        CASCOIN_CLI="${repo_root}/src/cascoin-cli"
    elif command -v cascoin-cli &> /dev/null; then
        CASCOIN_CLI="cascoin-cli"
    else
        print_error "cascoin-cli nicht gefunden"
        exit 1
    fi
}

# CLI Wrapper
cli() {
    if [ "$NETWORK_MODE" = "testnet" ]; then
        $CASCOIN_CLI -testnet -datadir="${DATADIR}" -rpcuser="${RPC_USER}" -rpcpassword="${RPC_PASSWORD}" "$@"
    else
        $CASCOIN_CLI -regtest -datadir="${DATADIR}" -rpcuser="${RPC_USER}" -rpcpassword="${RPC_PASSWORD}" "$@"
    fi
}

# Generiere Blöcke
generate_blocks() {
    local count=$1
    local address=$2
    local generated=0
    local attempts=0
    local max_attempts=$((count * 10))
    
    while [ "$generated" -lt "$count" ] && [ "$attempts" -lt "$max_attempts" ]; do
        local result
        result=$(cli generatetoaddress 1 "$address" 2>/dev/null) || true
        
        if [ -n "$result" ] && echo "$result" | grep -qE '[a-f0-9]{64}'; then
            generated=$((generated + 1))
            if [ $((generated % 5)) -eq 0 ] || [ "$generated" -eq "$count" ]; then
                local height=$(cli getblockcount)
                echo -ne "\r  Blöcke: ${generated}/${count} (Höhe: ${height})    "
            fi
        fi
        attempts=$((attempts + 1))
    done
    echo ""
    
    if [ "$generated" -ge "$count" ]; then
        return 0
    else
        return 1
    fi
}

# =============================================================================
# Argument-Parsing
# =============================================================================

# Erst prüfen ob --testnet gesetzt ist, um Default-Datadir zu bestimmen
for arg in "$@"; do
    if [ "$arg" = "--testnet" ]; then
        NETWORK_MODE="testnet"
        break
    fi
done

# Standard-Datadir basierend auf Netzwerk
if [ "$NETWORK_MODE" = "testnet" ]; then
    DATADIR="${DEFAULT_DATADIR_TESTNET}"
else
    DATADIR="${DEFAULT_DATADIR_REGTEST}"
fi

BLOCKS="${DEFAULT_BLOCKS}"

while [[ $# -gt 0 ]]; do
    case $1 in
        --testnet)
            NETWORK_MODE="testnet"
            # Aktualisiere Datadir falls nicht explizit gesetzt
            if [ "$DATADIR" = "${DEFAULT_DATADIR_REGTEST}" ]; then
                DATADIR="${DEFAULT_DATADIR_TESTNET}"
            fi
            shift
            ;;
        --datadir)
            DATADIR="$2"
            shift 2
            ;;
        --blocks)
            BLOCKS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Verwendung: $0 [OPTIONEN]"
            echo ""
            echo "Netzwerk-Modi:"
            echo "  (ohne Flag)         Regtest-Modus (Standard)"
            echo "  --testnet           Testnet-Modus"
            echo ""
            echo "Optionen:"
            echo "  --datadir <path>    Datenverzeichnis"
            echo "                      Regtest: ${DEFAULT_DATADIR_REGTEST}"
            echo "                      Testnet: ${DEFAULT_DATADIR_TESTNET}"
            echo "  --blocks <count>    Anzahl Blöcke zu generieren - nur Regtest (Standard: ${DEFAULT_BLOCKS})"
            echo "  --help, -h          Diese Hilfe anzeigen"
            echo ""
            echo "Hinweis: Im Testnet werden keine Blöcke generiert, sondern nur der Status angezeigt."
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

print_header "Cascoin L2 Mining Script (${NETWORK_MODE})"

find_binaries

# Lade Netzwerk-Modus aus Config falls vorhanden
if [ -f "${DATADIR}/demo_config.sh" ]; then
    source "${DATADIR}/demo_config.sh"
    if [ -n "$DEMO_NETWORK_MODE" ]; then
        NETWORK_MODE="${DEMO_NETWORK_MODE}"
    fi
fi

# Prüfe ob Node läuft
if ! cli getblockchaininfo &>/dev/null; then
    print_error "Node läuft nicht. Starte zuerst ./setup_l2_demo.sh"
    if [ "$NETWORK_MODE" = "testnet" ]; then
        print_info "Für Testnet: ./setup_l2_demo.sh --testnet"
    fi
    exit 1
fi

# Hole Mining-Adresse
if [ -f "${DATADIR}/demo_config.sh" ]; then
    source "${DATADIR}/demo_config.sh"
    MINING_ADDR="${DEMO_MINING_ADDR}"
fi

if [ -z "$MINING_ADDR" ]; then
    MINING_ADDR=$(cli getnewaddress "mining")
fi

print_info "Mining-Adresse: ${MINING_ADDR}"

# Zeige Status vor dem Mining
echo ""
print_step "Status vor Mining:"
PENDING_BURNS=$(cli l2_getpendingburns 2>/dev/null || echo "[]")
PENDING_COUNT=$(echo "$PENDING_BURNS" | grep -c '"l1TxHash"' || echo "0")
print_info "Pending Burns: ${PENDING_COUNT}"

SUPPLY_BEFORE=$(cli l2_gettotalsupply 2>/dev/null | grep -o '"totalMintedL2": [^,]*' | cut -d' ' -f2 || echo "0")
print_info "L2 Total Supply: ${SUPPLY_BEFORE}"

# Generiere Blöcke (nur Regtest)
if [ "$NETWORK_MODE" = "regtest" ]; then
    echo ""
    print_step "Generiere ${BLOCKS} Blöcke..."
    START_HEIGHT=$(cli getblockcount)

    generate_blocks ${BLOCKS} "${MINING_ADDR}"

    END_HEIGHT=$(cli getblockcount)
    GENERATED=$((END_HEIGHT - START_HEIGHT))
    print_success "${GENERATED} Blöcke generiert (Höhe: ${START_HEIGHT} -> ${END_HEIGHT})"
else
    echo ""
    print_info "Testnet-Modus: Keine Block-Generierung möglich"
    print_info "Warte auf neue Blöcke vom Netzwerk..."
    
    START_HEIGHT=$(cli getblockcount)
    print_info "Aktuelle Block-Höhe: ${START_HEIGHT}"
    
    # Warte kurz auf neue Blöcke
    sleep 10
    
    END_HEIGHT=$(cli getblockcount)
    if [ "$END_HEIGHT" -gt "$START_HEIGHT" ]; then
        print_success "$((END_HEIGHT - START_HEIGHT)) neue Blöcke empfangen"
    else
        print_info "Keine neuen Blöcke in den letzten 10 Sekunden"
    fi
fi

# Zeige Status nach dem Mining
echo ""
print_step "Status nach Mining:"

# L2 Chain Info
L2_INFO=$(cli l2_getchaininfo 2>/dev/null)
L2_BLOCK_HEIGHT=$(echo "$L2_INFO" | grep -o '"blockHeight": [0-9]*' | cut -d' ' -f2 || echo "0")
print_info "L2 Block Height: ${L2_BLOCK_HEIGHT}"

# Total Supply
SUPPLY_AFTER=$(cli l2_gettotalsupply 2>/dev/null)
TOTAL_SUPPLY=$(echo "$SUPPLY_AFTER" | grep -o '"totalSupply": [^,]*' | cut -d' ' -f2 || echo "0")
TOTAL_MINTED=$(echo "$SUPPLY_AFTER" | grep -o '"totalMintedL2": [^,]*' | cut -d' ' -f2 || echo "0")
BURN_COUNT=$(echo "$SUPPLY_AFTER" | grep -o '"burnCount": [0-9]*' | cut -d' ' -f2 || echo "0")
print_info "L2 Total Supply: ${TOTAL_SUPPLY}"
print_info "Total Minted: ${TOTAL_MINTED}"
print_info "Burn Count: ${BURN_COUNT}"

# Pending Burns
PENDING_BURNS=$(cli l2_getpendingburns 2>/dev/null || echo "[]")
PENDING_COUNT=$(echo "$PENDING_BURNS" | grep -c '"l1TxHash"' || echo "0")
print_info "Pending Burns: ${PENDING_COUNT}"

# Zeige Pending Burns Details falls vorhanden
if [ "$PENDING_COUNT" -gt 0 ]; then
    echo ""
    print_info "Pending Burn Details:"
    echo "$PENDING_BURNS" | grep -E '"l1TxHash"|"burnAmount"|"status"|"confirmationCount"' | head -20
fi

# Prüfe ob Burn TX aus demo_config existiert und zeige Status
if [ -n "$DEMO_BURN_TXID" ] && [ "$DEMO_BURN_TXID" != "N/A" ]; then
    echo ""
    print_step "Burn TX Status (${DEMO_BURN_TXID:0:16}...):"
    BURN_STATUS=$(cli l2_getburnstatus "$DEMO_BURN_TXID" 2>/dev/null || echo "{}")
    
    CONFIRMATIONS=$(echo "$BURN_STATUS" | grep -o '"confirmations": [0-9]*' | cut -d' ' -f2 || echo "?")
    CONSENSUS=$(echo "$BURN_STATUS" | grep -o '"consensusStatus": "[^"]*"' | cut -d'"' -f4 || echo "?")
    MINT_STATUS=$(echo "$BURN_STATUS" | grep -o '"mintStatus": "[^"]*"' | cut -d'"' -f4 || echo "?")
    
    print_info "Confirmations: ${CONFIRMATIONS}"
    print_info "Consensus: ${CONSENSUS}"
    print_info "Mint Status: ${MINT_STATUS}"
    
    # In Regtest: Force mint wenn genug Confirmations aber noch nicht gemintet
    if [ "$NETWORK_MODE" = "regtest" ] && [ "$MINT_STATUS" = "NOT_MINTED" ] && [ "$CONFIRMATIONS" -ge 6 ] 2>/dev/null; then
        print_info "Versuche Force-Mint (Regtest)..."
        FORCE_RESULT=$(cli l2_forcemint "$DEMO_BURN_TXID" 2>&1) || true
        
        if echo "$FORCE_RESULT" | grep -q '"success": true'; then
            print_success "L2 Tokens wurden erfolgreich gemintet (Force-Mint)!"
            MINT_STATUS="MINTED"
        else
            print_info "Force-Mint nicht verfügbar: $FORCE_RESULT"
        fi
    fi
    
    if [ "$MINT_STATUS" = "MINTED" ]; then
        print_success "L2 Tokens wurden erfolgreich gemintet!"
    fi
fi

# Sequencer Info
echo ""
print_step "Sequencer Status:"
SEQUENCERS=$(cli l2_getsequencers 2>/dev/null || echo "[]")
SEQ_COUNT=$(echo "$SEQUENCERS" | grep -c '"address"' || echo "0")
print_info "Aktive Sequencer: ${SEQ_COUNT}"

if [ "$SEQ_COUNT" -gt 0 ]; then
    echo "$SEQUENCERS" | grep -E '"address"|"blocksProduced"|"isEligible"' | head -10
fi

print_header "Mining abgeschlossen"
