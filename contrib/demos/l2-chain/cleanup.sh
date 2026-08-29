#!/bin/bash
# =============================================================================
# Cascoin L2 Demo Chain Cleanup Script
# =============================================================================
# 
# Dieses Skript bereinigt die L2-Demo-Umgebung:
#   1. Stoppt den laufenden Cascoin-Node
#   2. Löscht die Regtest/Testnet-Daten (optional)
#
# Unterstützt Regtest (Standard) und Testnet (mit --testnet Flag).
#
# Verwendung: 
#   ./cleanup.sh [--datadir <path>] [--keep-data] [--force]
#   ./cleanup.sh --testnet [--datadir <path>] [--keep-data] [--force]
#
# Requirements: 3.5
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_DATADIR_REGTEST="${HOME}/.cascoin-l2-demo"
DEFAULT_DATADIR_TESTNET="${HOME}/.cascoin-l2-testnet"

# Netzwerk-Modus (regtest oder testnet)
NETWORK_MODE="regtest"

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

# Finde die Cascoin Binaries (lokal gebaut oder im PATH)
find_binaries() {
    # Versuche zuerst lokale Build-Binaries zu finden
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local repo_root="$(cd "${script_dir}/../../.." && pwd)"
    
    if [ -x "${repo_root}/src/cascoind" ] && [ -x "${repo_root}/src/cascoin-cli" ]; then
        CASCOIND="${repo_root}/src/cascoind"
        CASCOIN_CLI="${repo_root}/src/cascoin-cli"
    elif command -v cascoind &> /dev/null && command -v cascoin-cli &> /dev/null; then
        CASCOIND="cascoind"
        CASCOIN_CLI="cascoin-cli"
    else
        # Fallback - versuche trotzdem fortzufahren
        CASCOIND="cascoind"
        CASCOIN_CLI="cascoin-cli"
    fi
}

# Wrapper für cascoin-cli mit RPC-Credentials
cli() {
    if [ "$NETWORK_MODE" = "testnet" ]; then
        $CASCOIN_CLI -testnet -datadir="${DATADIR}" -rpcuser=demo -rpcpassword=demo "$@"
    else
        $CASCOIN_CLI -regtest -datadir="${DATADIR}" -rpcuser=demo -rpcpassword=demo "$@"
    fi
}

# Warte auf Node-Stop
wait_for_node_stop() {
    local max_attempts=30
    local attempt=0
    
    print_info "Warte auf Node-Stop..."
    
    while [ $attempt -lt $max_attempts ]; do
        if ! cli getblockchaininfo &>/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        attempt=$((attempt + 1))
    done
    
    print_warning "Node reagiert noch nach ${max_attempts}s"
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

KEEP_DATA=false
FORCE=false

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
        --keep-data)
            KEEP_DATA=true
            shift
            ;;
        --force|-f)
            FORCE=true
            shift
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
            echo "  --keep-data         Netzwerk-Daten behalten (nur Node stoppen)"
            echo "                      Im Testnet ist dies der Standard!"
            echo "  --force, -f         Daten wirklich löschen (auch im Testnet)"
            echo "  --help, -h          Diese Hilfe anzeigen"
            echo ""
            echo "Beispiele:"
            echo "  $0                          # Regtest cleanup"
            echo "  $0 --testnet                # Testnet cleanup"
            echo "  $0 --testnet --keep-data    # Testnet Node stoppen, Daten behalten"
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

print_header "Cascoin L2 Demo Chain Cleanup (${NETWORK_MODE})"

echo ""
print_info "Konfiguration:"
print_info "  Netzwerk-Modus:   ${NETWORK_MODE}"
print_info "  Datenverzeichnis: ${DATADIR}"
print_info "  Daten behalten:   ${KEEP_DATA}"
print_info "  Force-Modus:      ${FORCE}"
echo ""

# Lade Netzwerk-Modus aus Config falls vorhanden
if [ -f "${DATADIR}/demo_config.sh" ]; then
    source "${DATADIR}/demo_config.sh"
    if [ -n "$DEMO_NETWORK_MODE" ]; then
        NETWORK_MODE="${DEMO_NETWORK_MODE}"
        print_info "Netzwerk-Modus aus Config: ${NETWORK_MODE}"
    fi
fi

# Finde Binaries
find_binaries

# -----------------------------------------------------------------------------
# Schritt 1: Node stoppen
# -----------------------------------------------------------------------------

print_step "Stoppe Cascoin-Node..."

# Prüfe ob Node läuft
if cli getblockchaininfo &>/dev/null 2>&1; then
    print_info "Node läuft, sende Stop-Befehl..."
    
    # Sende Stop-Befehl
    cli stop 2>/dev/null || true
    
    # Warte auf Stop
    if wait_for_node_stop; then
        print_success "Node gestoppt"
    else
        print_warning "Node konnte nicht sauber gestoppt werden"
        
        # Versuche mit SIGTERM
        if [ -f "${DATADIR}/regtest/cascoind.pid" ]; then
            PID=$(cat "${DATADIR}/regtest/cascoind.pid" 2>/dev/null)
            if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
                print_info "Sende SIGTERM an PID ${PID}..."
                kill -TERM "$PID" 2>/dev/null || true
                sleep 3
                
                # Falls immer noch läuft, SIGKILL
                if kill -0 "$PID" 2>/dev/null; then
                    print_warning "Node reagiert nicht, sende SIGKILL..."
                    kill -KILL "$PID" 2>/dev/null || true
                fi
            fi
        fi
        
        print_success "Node-Prozess beendet"
    fi
else
    print_info "Node läuft nicht"
fi

# -----------------------------------------------------------------------------
# Schritt 2: Regtest-Daten löschen (optional)
# -----------------------------------------------------------------------------

if [ "$KEEP_DATA" = false ]; then
    # Im Testnet: Niemals Daten löschen (nur mit --force)
    if [ "$NETWORK_MODE" = "testnet" ] && [ "$FORCE" = false ]; then
        print_info "Testnet-Modus: Daten werden automatisch behalten"
        print_info "(Blockchain-Sync kann Stunden dauern)"
        print_info "Verwenden Sie --force um Testnet-Daten zu löschen"
        KEEP_DATA=true
    fi
fi

if [ "$KEEP_DATA" = false ]; then
    print_step "Lösche ${NETWORK_MODE}-Daten..."
    
    NETWORK_SUBDIR="${NETWORK_MODE}"
    if [ "$NETWORK_MODE" = "testnet" ]; then
        NETWORK_SUBDIR="testnet3"
    fi
    
    NETWORK_DIR="${DATADIR}/${NETWORK_SUBDIR}"
    
    if [ -d "$NETWORK_DIR" ]; then
        # Bestätigung anfordern (außer im Force-Modus)
        if [ "$FORCE" = false ]; then
            echo ""
            print_warning "ACHTUNG: Folgende Daten werden gelöscht:"
            echo "  ${NETWORK_DIR}"
            echo ""
            
            # Zeige Größe
            if command -v du &> /dev/null; then
                SIZE=$(du -sh "$NETWORK_DIR" 2>/dev/null | cut -f1)
                print_info "Größe: ${SIZE}"
            fi
            
            if [ "$NETWORK_MODE" = "testnet" ]; then
                print_warning "WARNUNG: Testnet-Daten enthalten die synchronisierte Blockchain!"
                print_warning "Eine Neusynchronisation kann mehrere Stunden dauern."
            fi
            
            echo ""
            read -p "Sind Sie sicher? (j/N) " -n 1 -r
            echo
            
            if [[ ! $REPLY =~ ^[Jj]$ ]]; then
                print_info "Abgebrochen. Daten wurden nicht gelöscht."
                exit 0
            fi
        fi
        
        # Lösche Netzwerk-Verzeichnis
        rm -rf "$NETWORK_DIR"
        print_success "${NETWORK_MODE}-Daten gelöscht: ${NETWORK_DIR}"
        
        # Lösche auch die Demo-Konfiguration
        if [ -f "${DATADIR}/demo_config.sh" ]; then
            rm -f "${DATADIR}/demo_config.sh"
            print_info "Demo-Konfiguration gelöscht"
        fi
        
        # Lösche cascoin.conf falls es von uns erstellt wurde
        if [ -f "${DATADIR}/cascoin.conf" ]; then
            # Prüfe ob es unsere Konfiguration ist
            if grep -q "# L2 Demo" "${DATADIR}/cascoin.conf" 2>/dev/null; then
                rm -f "${DATADIR}/cascoin.conf"
                print_info "Konfigurationsdatei gelöscht"
            fi
        fi
        
        # Lösche leeres Datenverzeichnis
        if [ -d "$DATADIR" ] && [ -z "$(ls -A "$DATADIR" 2>/dev/null)" ]; then
            rmdir "$DATADIR"
            print_info "Leeres Datenverzeichnis entfernt"
        fi
        
    else
        print_info "Keine ${NETWORK_MODE}-Daten gefunden in: ${NETWORK_DIR}"
    fi
else
    print_info "Daten werden behalten (--keep-data)"
fi

# -----------------------------------------------------------------------------
# Schritt 3: Aufräumen von temporären Dateien
# -----------------------------------------------------------------------------

print_step "Räume temporäre Dateien auf..."

# Lösche Lock-Dateien falls vorhanden
NETWORK_SUBDIR="${NETWORK_MODE}"
if [ "$NETWORK_MODE" = "testnet" ]; then
    NETWORK_SUBDIR="testnet3"
fi

LOCK_FILE="${DATADIR}/${NETWORK_SUBDIR}/.lock"
if [ -f "$LOCK_FILE" ]; then
    rm -f "$LOCK_FILE"
    print_info "Lock-Datei entfernt"
fi

# Lösche Debug-Log falls sehr groß
DEBUG_LOG="${DATADIR}/${NETWORK_SUBDIR}/debug.log"
if [ -f "$DEBUG_LOG" ]; then
    SIZE=$(stat -f%z "$DEBUG_LOG" 2>/dev/null || stat -c%s "$DEBUG_LOG" 2>/dev/null || echo "0")
    if [ "$SIZE" -gt 104857600 ]; then  # > 100MB
        print_info "Debug-Log ist groß ($(numfmt --to=iec $SIZE 2>/dev/null || echo "${SIZE} bytes"))"
        if [ "$FORCE" = true ] || [ "$KEEP_DATA" = false ]; then
            rm -f "$DEBUG_LOG"
            print_info "Debug-Log gelöscht"
        fi
    fi
fi

print_success "Temporäre Dateien aufgeräumt"

# -----------------------------------------------------------------------------
# Zusammenfassung
# -----------------------------------------------------------------------------

print_header "Cleanup abgeschlossen!"

echo ""
if [ "$KEEP_DATA" = true ]; then
    echo "Der Node wurde gestoppt, aber die Daten wurden behalten."
    echo ""
    echo "Zum Neustart:"
    if [ "$NETWORK_MODE" = "testnet" ]; then
        echo "  ./setup_l2_demo.sh --testnet"
    else
        echo "  ./setup_l2_demo.sh --datadir ${DATADIR}"
    fi
else
    echo "Der Node wurde gestoppt und alle Demo-Daten wurden gelöscht."
    echo ""
    echo "Für einen Neustart:"
    if [ "$NETWORK_MODE" = "testnet" ]; then
        echo "  ./setup_l2_demo.sh --testnet"
    else
        echo "  ./setup_l2_demo.sh"
    fi
fi
echo ""

print_success "Cleanup erfolgreich"
