#!/bin/bash
# =============================================================================
# Cascoin L2 Demo Chain Setup Script
# =============================================================================
# 
# Dieses Skript startet eine vollständige L2-Demo-Umgebung.
# Unterstützt Regtest (Standard) und Testnet (mit --testnet Flag).
#
# Es führt folgende Schritte aus:
#   1. Startet den Cascoin-Node mit L2-Unterstützung
#   2. Generiert initiale Blöcke für Mining-Rewards (nur Regtest)
#   3. Registriert einen Sequencer
#   4. Führt einen L2-Burn durch (Burn-and-Mint Model)
#   5. Zeigt Consensus-Status und L2-Balance
#
# Verwendung: 
#   ./setup_l2_demo.sh [--datadir <path>] [--stake <amount>] [--hatscore <score>]
#   ./setup_l2_demo.sh --testnet [--datadir <path>] [--stake <amount>]
#
# Requirements: 1.1, 1.2, 1.3, 1.5, 11.5
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_DIR="${SCRIPT_DIR}/config"

# Netzwerk-Modus (regtest oder testnet)
NETWORK_MODE="regtest"

# Standard-Werte
DEFAULT_DATADIR_REGTEST="${HOME}/.cascoin-l2-demo"
DEFAULT_DATADIR_TESTNET="${HOME}/.cascoin-l2-testnet"
DEFAULT_STAKE=10
DEFAULT_HATSCORE=50
# Burn-and-Mint: CAS wird auf L1 verbrannt und L2-Tokens werden gemintet
DEFAULT_BURN_AMOUNT=10
DEFAULT_INITIAL_BLOCKS=101

# RPC-Credentials
RPC_USER="demo"
RPC_PASSWORD="demo"

# RPC-Ports
RPC_PORT_REGTEST=19443
RPC_PORT_TESTNET=19332

# Farben für Ausgabe
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Fehlerbehandlung
cleanup_on_error() {
    print_error "Ein Fehler ist aufgetreten auf Zeile $1"
    print_info "Führe Cleanup durch..."
    
    # Versuche Node zu stoppen falls er läuft
    if [ -n "$CASCOIN_CLI" ]; then
        cli stop 2>/dev/null || true
    fi
    
    exit 1
}

trap 'cleanup_on_error $LINENO' ERR

# Finde die Cascoin Binaries (lokal gebaut oder im PATH)
find_binaries() {
    # Versuche zuerst lokale Build-Binaries zu finden
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local repo_root="$(cd "${script_dir}/../../.." && pwd)"
    
    if [ -x "${repo_root}/src/cascoind" ] && [ -x "${repo_root}/src/cascoin-cli" ]; then
        CASCOIND="${repo_root}/src/cascoind"
        CASCOIN_CLI="${repo_root}/src/cascoin-cli"
        print_info "Verwende lokale Build-Binaries aus ${repo_root}/src/"
    elif command -v cascoind &> /dev/null && command -v cascoin-cli &> /dev/null; then
        CASCOIND="cascoind"
        CASCOIN_CLI="cascoin-cli"
        print_info "Verwende System-Binaries"
    else
        print_error "Cascoin Binaries nicht gefunden. Bitte bauen Sie das Projekt mit 'make'."
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

# Warte auf Node-Start
wait_for_node() {
    local max_attempts=30
    local attempt=0
    
    print_info "Warte auf Node-Start..."
    
    while [ $attempt -lt $max_attempts ]; do
        if cli getblockchaininfo &>/dev/null; then
            return 0
        fi
        sleep 1
        attempt=$((attempt + 1))
    done
    
    print_error "Node konnte nicht gestartet werden (Timeout nach ${max_attempts}s)"
    return 1
}

# Generiere Blöcke mit Wiederholung bei Fehlschlag
generate_blocks() {
    local target_count=$1
    local address=$2
    local max_attempts_per_block=10
    
    local start_height
    start_height=$(cli getblockcount)
    local target_height=$((start_height + target_count))
    
    print_info "Aktuelle Höhe: ${start_height}, Ziel: ${target_height}"
    
    local blocks_generated=0
    local total_attempts=0
    local max_total_attempts=$((target_count * max_attempts_per_block))
    
    while [ "$blocks_generated" -lt "$target_count" ] && [ "$total_attempts" -lt "$max_total_attempts" ]; do
        local remaining=$((target_count - blocks_generated))
        
        local batch_size=$remaining
        if [ "$batch_size" -gt 10 ]; then
            batch_size=10
        fi
        
        local result
        result=$(cli generatetoaddress "$batch_size" "$address" 2>/dev/null) || true
        
        local generated_this_round=0
        if [ -n "$result" ] && [ "$result" != "[]" ]; then
            generated_this_round=$(echo "$result" | grep -oE '[a-f0-9]{64}' | wc -l)
        fi
        
        if [ "$generated_this_round" -gt 0 ]; then
            blocks_generated=$((blocks_generated + generated_this_round))
            
            if [ $((blocks_generated % 10)) -eq 0 ] || [ "$blocks_generated" -ge "$target_count" ]; then
                local current_height
                current_height=$(cli getblockcount)
                print_info "Fortschritt: ${blocks_generated}/${target_count} Blöcke (Höhe: ${current_height})"
            fi
        else
            sleep 0.1
        fi
        
        total_attempts=$((total_attempts + 1))
    done
    
    if [ "$blocks_generated" -ge "$target_count" ]; then
        print_success "${blocks_generated} Blöcke erfolgreich generiert"
        return 0
    else
        print_error "Konnte nicht alle Blöcke generieren. Generiert: ${blocks_generated}/${target_count}"
        return 1
    fi
}

# Prüfe ob L2 aktiviert ist
check_l2_enabled() {
    local max_attempts=30
    local attempt=0
    
    print_info "Warte auf L2-Initialisierung..."
    
    while [ $attempt -lt $max_attempts ]; do
        local result
        result=$(cli l2_getchaininfo 2>&1)
        if [ $? -eq 0 ]; then
            if echo "$result" | grep -q '"enabled": 1' || echo "$result" | grep -q '"enabled":1' || echo "$result" | grep -q '"enabled": true'; then
                return 0
            fi
        fi
        sleep 1
        attempt=$((attempt + 1))
    done
    
    print_error "L2 ist nicht aktiviert."
    return 1
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

STAKE="${DEFAULT_STAKE}"
HATSCORE="${DEFAULT_HATSCORE}"
BURN_AMOUNT="${DEFAULT_BURN_AMOUNT}"
INITIAL_BLOCKS="${DEFAULT_INITIAL_BLOCKS}"

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
        --stake)
            STAKE="$2"
            shift 2
            ;;
        --hatscore)
            HATSCORE="$2"
            shift 2
            ;;
        --burn)
            BURN_AMOUNT="$2"
            shift 2
            ;;
        --blocks)
            INITIAL_BLOCKS="$2"
            shift 2
            ;;
        --help|-h)
            echo "Verwendung: $0 [OPTIONEN]"
            echo ""
            echo "Netzwerk-Modi:"
            echo "  (ohne Flag)         Regtest-Modus (Standard, isoliertes Testnetzwerk)"
            echo "  --testnet           Testnet-Modus (öffentliches Testnetzwerk)"
            echo ""
            echo "Optionen:"
            echo "  --datadir <path>    Datenverzeichnis"
            echo "                      Regtest: ${DEFAULT_DATADIR_REGTEST}"
            echo "                      Testnet: ${DEFAULT_DATADIR_TESTNET}"
            echo "  --stake <amount>    Sequencer-Stake in CAS (Standard: ${DEFAULT_STAKE})"
            echo "  --hatscore <score>  HAT v2 Score 0-100 (Standard: ${DEFAULT_HATSCORE})"
            echo "  --burn <amount>     CAS zu verbrennen für L2-Tokens (Standard: ${DEFAULT_BURN_AMOUNT})"
            echo "  --blocks <count>    Initiale Blöcke - nur Regtest (Standard: ${DEFAULT_INITIAL_BLOCKS})"
            echo "  --help, -h          Diese Hilfe anzeigen"
            echo ""
            echo "Burn-and-Mint Model:"
            echo "  CAS wird auf L1 via OP_RETURN verbrannt und L2-Tokens werden"
            echo "  nach 2/3 Sequencer-Konsens gemintet."
            echo ""
            echo "Beispiele:"
            echo "  $0                          # Regtest mit Standardwerten"
            echo "  $0 --testnet                # Testnet mit Standardwerten"
            echo "  $0 --testnet --stake 50     # Testnet mit 50 CAS Stake"
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

if [ "$NETWORK_MODE" = "testnet" ]; then
    print_header "Cascoin L2 Demo Chain Setup - TESTNET (Burn-and-Mint Model)"
else
    print_header "Cascoin L2 Demo Chain Setup - REGTEST (Burn-and-Mint Model)"
fi

echo ""
print_info "Konfiguration:"
print_info "  Netzwerk-Modus:   ${NETWORK_MODE}"
print_info "  Datenverzeichnis: ${DATADIR}"
print_info "  Sequencer-Stake:  ${STAKE} CAS"
print_info "  HAT Score:        ${HATSCORE}"
print_info "  L2-Burn:          ${BURN_AMOUNT} CAS"
if [ "$NETWORK_MODE" = "regtest" ]; then
    print_info "  Initiale Blöcke:  ${INITIAL_BLOCKS}"
fi
echo ""

if [ "$NETWORK_MODE" = "testnet" ]; then
    print_warning "TESTNET-MODUS: Sie benötigen Testnet-CAS für diese Demo!"
    print_info "Testnet-Faucet: https://faucet.cascoin.org/testnet"
    echo ""
fi

# -----------------------------------------------------------------------------
# Schritt 0: Voraussetzungen prüfen
# -----------------------------------------------------------------------------

print_step "Prüfe Voraussetzungen..."
find_binaries
print_success "Alle Voraussetzungen erfüllt"

# -----------------------------------------------------------------------------
# Schritt 1: Datenverzeichnis vorbereiten
# -----------------------------------------------------------------------------

print_step "Bereite Datenverzeichnis vor..."

NETWORK_SUBDIR="${NETWORK_MODE}"
if [ "$NETWORK_MODE" = "testnet" ]; then
    NETWORK_SUBDIR="testnet3"
fi

if [ -d "${DATADIR}/${NETWORK_SUBDIR}" ]; then
    print_info "Existierendes ${NETWORK_MODE}-Verzeichnis gefunden"
    if [ "$NETWORK_MODE" = "regtest" ]; then
        read -p "Möchten Sie es löschen und neu beginnen? (j/N) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Jj]$ ]]; then
            rm -rf "${DATADIR}/${NETWORK_SUBDIR}"
            print_info "${NETWORK_MODE}-Daten gelöscht"
        fi
    else
        print_info "Testnet-Daten werden beibehalten (Blockchain-Sync)"
    fi
fi

mkdir -p "${DATADIR}"

# Kopiere passende Konfiguration
if [ "$NETWORK_MODE" = "testnet" ]; then
    if [ -f "${CONFIG_DIR}/testnet.conf" ]; then
        cp "${CONFIG_DIR}/testnet.conf" "${DATADIR}/cascoin.conf"
        print_info "Testnet-Konfiguration kopiert"
    fi
else
    if [ -f "${CONFIG_DIR}/regtest.conf" ]; then
        cp "${CONFIG_DIR}/regtest.conf" "${DATADIR}/cascoin.conf"
        print_info "Regtest-Konfiguration kopiert"
    fi
fi

print_success "Datenverzeichnis bereit: ${DATADIR}"

# -----------------------------------------------------------------------------
# Schritt 2: Node starten
# -----------------------------------------------------------------------------

print_step "Starte Cascoin-Node mit L2-Unterstützung..."

if cli getblockchaininfo &>/dev/null; then
    print_info "Node läuft bereits"
else
    if [ "$NETWORK_MODE" = "testnet" ]; then
        $CASCOIND -testnet \
            -daemon \
            -datadir="${DATADIR}" \
            -l2=1 \
            -l2mode=2 \
            -server=1 \
            -rpcuser="${RPC_USER}" \
            -rpcpassword="${RPC_PASSWORD}" \
            -rpcallowip=127.0.0.1 \
            -fallbackfee=0.0001
    else
        $CASCOIND -regtest \
            -daemon \
            -datadir="${DATADIR}" \
            -l2=1 \
            -l2mode=2 \
            -server=1 \
            -rpcuser="${RPC_USER}" \
            -rpcpassword="${RPC_PASSWORD}" \
            -rpcallowip=127.0.0.1 \
            -fallbackfee=0.0001
    fi
    
    wait_for_node
fi

print_success "Node gestartet"

# Bei Testnet: Warte auf Sync
if [ "$NETWORK_MODE" = "testnet" ]; then
    print_step "Prüfe Blockchain-Synchronisation..."
    
    SYNC_COMPLETE=false
    MAX_SYNC_WAIT=60
    SYNC_WAIT=0
    
    while [ "$SYNC_COMPLETE" = false ] && [ $SYNC_WAIT -lt $MAX_SYNC_WAIT ]; do
        BLOCKCHAIN_INFO=$(cli getblockchaininfo 2>/dev/null || echo "{}")
        VERIFICATION_PROGRESS=$(echo "$BLOCKCHAIN_INFO" | grep -o '"verificationprogress": [0-9.]*' | cut -d' ' -f2 || echo "0")
        
        # Prüfe ob Sync > 99%
        if [ "$(echo "$VERIFICATION_PROGRESS > 0.99" | bc -l 2>/dev/null || echo "0")" = "1" ]; then
            SYNC_COMPLETE=true
        else
            PROGRESS_PCT=$(echo "$VERIFICATION_PROGRESS * 100" | bc -l 2>/dev/null | cut -d'.' -f1 || echo "?")
            print_info "Sync-Fortschritt: ${PROGRESS_PCT}% (warte max. $((MAX_SYNC_WAIT - SYNC_WAIT))s)"
            sleep 5
            SYNC_WAIT=$((SYNC_WAIT + 5))
        fi
    done
    
    if [ "$SYNC_COMPLETE" = true ]; then
        print_success "Blockchain synchronisiert"
    else
        print_warning "Blockchain noch nicht vollständig synchronisiert"
        print_info "Die Demo kann trotzdem fortgesetzt werden, aber einige Funktionen"
        print_info "funktionieren möglicherweise erst nach vollständiger Synchronisation."
    fi
fi

check_l2_enabled
print_success "L2 ist aktiviert"

# -----------------------------------------------------------------------------
# Schritt 3: Initiale Blöcke generieren (nur Regtest) / Balance prüfen (Testnet)
# -----------------------------------------------------------------------------

MINING_ADDR=$(cli getnewaddress "mining")
print_info "Mining-Adresse: ${MINING_ADDR}"

if [ "$NETWORK_MODE" = "regtest" ]; then
    print_step "Generiere ${INITIAL_BLOCKS} initiale Blöcke..."
    
    generate_blocks ${INITIAL_BLOCKS} "${MINING_ADDR}"
    
    BALANCE=$(cli getbalance)
    print_success "Block-Generierung abgeschlossen"
    print_info "Wallet-Balance: ${BALANCE} CAS"
else
    print_step "Prüfe Wallet-Balance..."
    
    BALANCE=$(cli getbalance)
    print_info "Wallet-Balance: ${BALANCE} CAS"
    
    # Prüfe ob genug Balance für Stake + Burn vorhanden
    REQUIRED_BALANCE=$(echo "${STAKE} + ${BURN_AMOUNT} + 1" | bc)
    
    if [ "$(echo "$BALANCE < $REQUIRED_BALANCE" | bc -l 2>/dev/null || echo "1")" = "1" ]; then
        print_warning "Nicht genug Balance für Demo!"
        print_info "Benötigt: mindestens ${REQUIRED_BALANCE} CAS (Stake: ${STAKE}, Burn: ${BURN_AMOUNT}, Fees: ~1)"
        print_info "Vorhanden: ${BALANCE} CAS"
        echo ""
        print_info "Bitte holen Sie sich Testnet-CAS vom Faucet:"
        print_info "  https://faucet.cascoin.org/testnet"
        print_info "  Adresse: ${MINING_ADDR}"
        echo ""
        read -p "Fortfahren trotz niedriger Balance? (j/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Jj]$ ]]; then
            print_info "Abgebrochen. Bitte Balance aufladen und erneut starten."
            exit 0
        fi
    fi
fi

# -----------------------------------------------------------------------------
# Schritt 4: Sequencer registrieren
# -----------------------------------------------------------------------------

print_step "Registriere Sequencer..."

SEQUENCER_RESULT=$(cli l2_announcesequencer ${STAKE} ${HATSCORE})
SEQUENCER_ADDR=$(echo "${SEQUENCER_RESULT}" | grep -o '"address": "[^"]*"' | cut -d'"' -f4)

print_info "Sequencer-Adresse: ${SEQUENCER_ADDR}"

if [ "$NETWORK_MODE" = "regtest" ]; then
    print_info "Generiere 6 Bestätigungsblöcke..."
    generate_blocks 6 "${MINING_ADDR}"
else
    print_info "Warte auf 6 Bestätigungen im Testnet..."
    print_info "(Dies kann einige Minuten dauern)"
    
    # Warte auf Bestätigungen
    MAX_CONFIRM_WAIT=300  # 5 Minuten
    CONFIRM_WAIT=0
    
    while [ $CONFIRM_WAIT -lt $MAX_CONFIRM_WAIT ]; do
        SEQUENCER_INFO=$(cli l2_getsequencers 2>/dev/null || echo "[]")
        if echo "$SEQUENCER_INFO" | grep -q '"isEligible": true'; then
            break
        fi
        sleep 10
        CONFIRM_WAIT=$((CONFIRM_WAIT + 10))
        print_info "Warte auf Bestätigungen... ($((CONFIRM_WAIT))s / ${MAX_CONFIRM_WAIT}s)"
    done
fi

print_success "Sequencer registriert"

# -----------------------------------------------------------------------------
# Schritt 5: L2-Burn durchführen (Burn-and-Mint Model)
# -----------------------------------------------------------------------------

print_step "Führe L2-Burn durch (${BURN_AMOUNT} CAS)..."

print_info "Burn-and-Mint Model:"
print_info "  1. CAS wird auf L1 via OP_RETURN verbrannt"
print_info "  2. Sequencer validieren den Burn"
print_info "  3. Nach 2/3 Konsens werden L2-Tokens gemintet"

L2_ADDR=$(cli getnewaddress "l2")
print_info "L2-Empfänger-Adresse: ${L2_ADDR}"

# Extrahiere den Public Key für die Adresse (benötigt für l2_createburntx)
ADDR_INFO=$(cli validateaddress "${L2_ADDR}")
L2_PUBKEY=$(echo "${ADDR_INFO}" | grep -o '"pubkey": "[^"]*"' | head -1 | cut -d'"' -f4)

if [ -z "$L2_PUBKEY" ]; then
    # Versuche embedded pubkey
    L2_PUBKEY=$(echo "${ADDR_INFO}" | grep -A20 '"embedded"' | grep -o '"pubkey": "[^"]*"' | head -1 | cut -d'"' -f4)
fi

if [ -z "$L2_PUBKEY" ]; then
    print_warning "Konnte Public Key nicht extrahieren - überspringe Burn-Schritt"
    BURN_TX_RESULT=""
else
    print_info "L2-Empfänger-PubKey: ${L2_PUBKEY}"

    # Erstelle Burn-Transaktion mit Public Key
    print_info "Erstelle Burn-Transaktion..."
    BURN_TX_RESULT=$(cli l2_createburntx ${BURN_AMOUNT} "${L2_PUBKEY}" 2>&1) || {
        print_warning "l2_createburntx fehlgeschlagen: ${BURN_TX_RESULT}"
        print_info "Die Burn-RPC-Befehle sind möglicherweise noch nicht implementiert."
        BURN_TX_RESULT=""
    }
fi

if [ -n "$BURN_TX_RESULT" ] && echo "$BURN_TX_RESULT" | grep -q '"hex"'; then
    BURN_TX_HEX=$(echo "${BURN_TX_RESULT}" | grep -o '"hex": "[^"]*"' | cut -d'"' -f4)

    # Signiere die Transaktion
    print_info "Signiere Burn-Transaktion..."
    SIGNED_TX=$(cli signrawtransactionwithwallet "${BURN_TX_HEX}")
    SIGNED_TX_HEX=$(echo "${SIGNED_TX}" | grep -o '"hex": "[^"]*"' | cut -d'"' -f4)

    # Sende Burn-Transaktion
    print_info "Sende Burn-Transaktion an L1..."
    SEND_RESULT=$(cli l2_sendburntx "${SIGNED_TX_HEX}" 2>&1) || {
        print_warning "l2_sendburntx fehlgeschlagen"
        SEND_RESULT=""
    }
    
    if [ -n "$SEND_RESULT" ] && echo "$SEND_RESULT" | grep -q '"txid"'; then
        L1_TXID=$(echo "${SEND_RESULT}" | grep -o '"txid": "[^"]*"' | cut -d'"' -f4)
        print_info "L1 Burn TX: ${L1_TXID}"

        # Generiere Blöcke für L1-Bestätigungen (nur Regtest)
        if [ "$NETWORK_MODE" = "regtest" ]; then
            print_info "Generiere 6 Blöcke für L1-Bestätigungen..."
            generate_blocks 6 "${MINING_ADDR}"
        else
            print_info "Warte auf 6 L1-Bestätigungen im Testnet..."
            sleep 30  # Warte initial
        fi

        # Warte auf Konsens
        print_step "Warte auf Sequencer-Konsens..."

        MAX_CONSENSUS_ATTEMPTS=10
        CONSENSUS_REACHED=false

        for i in $(seq 1 $MAX_CONSENSUS_ATTEMPTS); do
            print_info "Konsens-Versuch ${i}/${MAX_CONSENSUS_ATTEMPTS}..."
            
            BURN_STATUS=$(cli l2_getburnstatus "${L1_TXID}" 2>/dev/null || echo "{}")
            MINT_STATUS=$(echo "${BURN_STATUS}" | grep -o '"mintStatus": "[^"]*"' | cut -d'"' -f4)
            CONSENSUS_STATUS=$(echo "${BURN_STATUS}" | grep -o '"consensusStatus": "[^"]*"' | cut -d'"' -f4)
            
            print_info "  Konsens: ${CONSENSUS_STATUS:-pending}, Mint: ${MINT_STATUS:-pending}"
            
            if [ "${MINT_STATUS}" = "MINTED" ]; then
                CONSENSUS_REACHED=true
                print_success "L2-Tokens wurden gemintet!"
                break
            fi
            
            if [ "$NETWORK_MODE" = "regtest" ]; then
                print_info "  Generiere 2 weitere Blöcke..."
                generate_blocks 2 "${MINING_ADDR}"
            else
                print_info "  Warte auf weitere Blöcke..."
                sleep 30
            fi
            
            sleep 1
        done

        if [ "${CONSENSUS_REACHED}" = false ]; then
            print_info "Konsens noch nicht erreicht - prüfe Status manuell"
        fi
    else
        print_warning "Burn-Transaktion konnte nicht gesendet werden"
        L1_TXID="N/A"
    fi
else
    print_warning "Burn-Transaktion konnte nicht erstellt werden"
    L1_TXID="N/A"
fi

# -----------------------------------------------------------------------------
# Schritt 6: Status anzeigen
# -----------------------------------------------------------------------------

print_header "L2 Demo Chain Status"

echo ""
print_step "L2 Chain Info:"
cli l2_getchaininfo

echo ""
print_step "L2 Balance:"
cli l2_getbalance "${L2_ADDR}" 2>/dev/null || print_info "Balance-Abfrage nicht verfügbar"

echo ""
print_step "L2 Total Supply:"
cli l2_gettotalsupply 2>/dev/null || print_info "Total Supply nicht verfügbar"

echo ""
print_step "Sequencer-Liste:"
cli l2_getsequencers 2>/dev/null || print_info "Sequencer-Liste nicht verfügbar"

if [ "${L1_TXID}" != "N/A" ]; then
    echo ""
    print_step "Burn-Status:"
    cli l2_getburnstatus "${L1_TXID}" 2>/dev/null || print_info "Burn-Status nicht verfügbar"
fi

# -----------------------------------------------------------------------------
# Zusammenfassung
# -----------------------------------------------------------------------------

print_header "Setup abgeschlossen!"

echo ""
echo "Die L2 Demo Chain ist jetzt bereit (Burn-and-Mint Model)."
echo ""
echo "Wichtige Adressen:"
echo "  Mining-Adresse:    ${MINING_ADDR}"
echo "  Sequencer-Adresse: ${SEQUENCER_ADDR}"
echo "  L2-Adresse:        ${L2_ADDR}"
if [ "${L1_TXID}" != "N/A" ]; then
    echo "  Burn TX (L1):      ${L1_TXID}"
fi
NETWORK_FLAG="-regtest"
if [ "$NETWORK_MODE" = "testnet" ]; then
    NETWORK_FLAG="-testnet"
fi

echo ""
echo "Nützliche Befehle:"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_getchaininfo"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_getbalance <address>"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_getsequencers"
echo ""
echo "Burn-and-Mint Befehle:"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_createburntx <amount> <l2_address>"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_getpendingburns"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_gettotalsupply"
echo "  cascoin-cli ${NETWORK_FLAG} -datadir=${DATADIR} l2_verifysupply"
echo ""
echo "Zum Beenden:"
if [ "$NETWORK_MODE" = "testnet" ]; then
    echo "  ./cleanup.sh --testnet --datadir ${DATADIR}"
else
    echo "  ./cleanup.sh --datadir ${DATADIR}"
fi
echo ""

# Speichere Konfiguration
cat > "${DATADIR}/demo_config.sh" << EOF
# L2 Demo Konfiguration (Burn-and-Mint Model)
export DEMO_NETWORK_MODE="${NETWORK_MODE}"
export DEMO_DATADIR="${DATADIR}"
export DEMO_MINING_ADDR="${MINING_ADDR}"
export DEMO_SEQUENCER_ADDR="${SEQUENCER_ADDR}"
export DEMO_L2_ADDR="${L2_ADDR}"
export DEMO_BURN_TXID="${L1_TXID}"
EOF

print_success "Konfiguration gespeichert in ${DATADIR}/demo_config.sh"
