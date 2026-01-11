#!/bin/bash
# =============================================================================
# Cascoin L2 Demo Chain Cleanup Script
# =============================================================================
# 
# Dieses Skript bereinigt die L2-Demo-Umgebung:
#   1. Stoppt den laufenden Cascoin-Node
#   2. Löscht die Regtest-Daten (optional)
#
# Verwendung: ./cleanup.sh [--datadir <path>] [--keep-data] [--force]
#
# Requirements: 3.5
# =============================================================================

set -e

# =============================================================================
# Konfiguration
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_DATADIR="${HOME}/.cascoin-l2-demo"

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
    $CASCOIN_CLI -regtest -datadir="${DATADIR}" -rpcuser=demo -rpcpassword=demo "$@"
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

DATADIR="${DEFAULT_DATADIR}"
KEEP_DATA=false
FORCE=false

while [[ $# -gt 0 ]]; do
    case $1 in
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
            echo "Optionen:"
            echo "  --datadir <path>  Datenverzeichnis (Standard: ${DEFAULT_DATADIR})"
            echo "  --keep-data       Regtest-Daten behalten (nur Node stoppen)"
            echo "  --force, -f       Keine Bestätigung vor Löschung"
            echo "  --help, -h        Diese Hilfe anzeigen"
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

print_header "Cascoin L2 Demo Chain Cleanup"

echo ""
print_info "Konfiguration:"
print_info "  Datenverzeichnis: ${DATADIR}"
print_info "  Daten behalten:   ${KEEP_DATA}"
print_info "  Force-Modus:      ${FORCE}"
echo ""

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
    print_step "Lösche Regtest-Daten..."
    
    REGTEST_DIR="${DATADIR}/regtest"
    
    if [ -d "$REGTEST_DIR" ]; then
        # Bestätigung anfordern (außer im Force-Modus)
        if [ "$FORCE" = false ]; then
            echo ""
            print_warning "ACHTUNG: Folgende Daten werden gelöscht:"
            echo "  ${REGTEST_DIR}"
            echo ""
            
            # Zeige Größe
            if command -v du &> /dev/null; then
                SIZE=$(du -sh "$REGTEST_DIR" 2>/dev/null | cut -f1)
                print_info "Größe: ${SIZE}"
            fi
            
            echo ""
            read -p "Sind Sie sicher? (j/N) " -n 1 -r
            echo
            
            if [[ ! $REPLY =~ ^[Jj]$ ]]; then
                print_info "Abgebrochen. Daten wurden nicht gelöscht."
                exit 0
            fi
        fi
        
        # Lösche Regtest-Verzeichnis
        rm -rf "$REGTEST_DIR"
        print_success "Regtest-Daten gelöscht: ${REGTEST_DIR}"
        
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
        print_info "Keine Regtest-Daten gefunden in: ${REGTEST_DIR}"
    fi
else
    print_info "Daten werden behalten (--keep-data)"
fi

# -----------------------------------------------------------------------------
# Schritt 3: Aufräumen von temporären Dateien
# -----------------------------------------------------------------------------

print_step "Räume temporäre Dateien auf..."

# Lösche Lock-Dateien falls vorhanden
LOCK_FILE="${DATADIR}/regtest/.lock"
if [ -f "$LOCK_FILE" ]; then
    rm -f "$LOCK_FILE"
    print_info "Lock-Datei entfernt"
fi

# Lösche Debug-Log falls sehr groß
DEBUG_LOG="${DATADIR}/regtest/debug.log"
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
    echo "  ./setup_l2_demo.sh --datadir ${DATADIR}"
else
    echo "Der Node wurde gestoppt und alle Demo-Daten wurden gelöscht."
    echo ""
    echo "Für einen Neustart:"
    echo "  ./setup_l2_demo.sh"
fi
echo ""

print_success "Cleanup erfolgreich"
