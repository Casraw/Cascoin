#!/bin/bash
# =============================================================================
# Cascoin L2 Quick Mining Script
# =============================================================================
# 
# Einfaches Mining-Script mit direkter Datadir-Angabe.
#
# Verwendung: 
#   ./mine.sh <datadir> [blocks]              # Regtest
#   ./mine.sh <datadir> [blocks] --testnet    # Testnet (zeigt nur Status)
#
# Beispiele:
#   ./mine.sh ~/.cascoin-l2-demo              # 10 Blöcke in Regtest
#   ./mine.sh ~/.cascoin-l2-demo 50           # 50 Blöcke in Regtest
#   ./mine.sh ~/.cascoin-l2-testnet --testnet # Status im Testnet
#
# =============================================================================

set -e

# Farben
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# =============================================================================
# Argument-Parsing
# =============================================================================

DATADIR=""
BLOCKS=10
NETWORK_MODE="regtest"

# Parse Argumente
for arg in "$@"; do
    if [ "$arg" = "--testnet" ]; then
        NETWORK_MODE="testnet"
    elif [ "$arg" = "--help" ] || [ "$arg" = "-h" ]; then
        echo "Verwendung: $0 <datadir> [blocks] [--testnet]"
        echo ""
        echo "Argumente:"
        echo "  <datadir>     Datenverzeichnis (erforderlich)"
        echo "  [blocks]      Anzahl Blöcke (Standard: 10, nur Regtest)"
        echo "  --testnet     Testnet-Modus (zeigt nur Status)"
        echo ""
        echo "Beispiele:"
        echo "  $0 ~/.cascoin-l2-demo              # 10 Blöcke"
        echo "  $0 ~/.cascoin-l2-demo 50           # 50 Blöcke"
        echo "  $0 ~/.cascoin-l2-testnet --testnet # Testnet Status"
        exit 0
    elif [ -z "$DATADIR" ]; then
        DATADIR="$arg"
    elif [[ "$arg" =~ ^[0-9]+$ ]]; then
        BLOCKS="$arg"
    fi
done

if [ -z "$DATADIR" ]; then
    echo -e "${RED}Fehler: Datadir erforderlich${NC}"
    echo "Verwendung: $0 <datadir> [blocks] [--testnet]"
    exit 1
fi

# Expandiere ~ falls vorhanden
DATADIR="${DATADIR/#\~/$HOME}"

# =============================================================================
# Binaries finden
# =============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

if [ -x "${REPO_ROOT}/src/cascoin-cli" ]; then
    CLI="${REPO_ROOT}/src/cascoin-cli"
elif command -v cascoin-cli &> /dev/null; then
    CLI="cascoin-cli"
else
    echo -e "${RED}Fehler: cascoin-cli nicht gefunden${NC}"
    exit 1
fi

# =============================================================================
# CLI Wrapper
# =============================================================================

cli() {
    if [ "$NETWORK_MODE" = "testnet" ]; then
        $CLI -testnet -datadir="${DATADIR}" -rpcuser=demo -rpcpassword=demo "$@"
    else
        $CLI -regtest -datadir="${DATADIR}" -rpcuser=demo -rpcpassword=demo "$@"
    fi
}

# =============================================================================
# Hauptprogramm
# =============================================================================

echo -e "${GREEN}=== Cascoin L2 Mining (${NETWORK_MODE}) ===${NC}"
echo "Datadir: ${DATADIR}"

# Prüfe ob Node läuft
if ! cli getblockchaininfo &>/dev/null; then
    echo -e "${RED}Fehler: Node läuft nicht${NC}"
    exit 1
fi

# Hole Mining-Adresse
ADDR=$(cli getnewaddress "mining" 2>/dev/null || cli getnewaddress 2>/dev/null)

if [ "$NETWORK_MODE" = "regtest" ]; then
    echo "Generiere ${BLOCKS} Blöcke..."
    
    START=$(cli getblockcount)
    
    # Generiere Blöcke
    GENERATED=0
    while [ "$GENERATED" -lt "$BLOCKS" ]; do
        RESULT=$(cli generatetoaddress 1 "$ADDR" 2>/dev/null) || true
        if [ -n "$RESULT" ] && echo "$RESULT" | grep -qE '[a-f0-9]{64}'; then
            GENERATED=$((GENERATED + 1))
            if [ $((GENERATED % 5)) -eq 0 ] || [ "$GENERATED" -eq "$BLOCKS" ]; then
                echo -ne "\r  ${GENERATED}/${BLOCKS} Blöcke    "
            fi
        fi
    done
    echo ""
    
    END=$(cli getblockcount)
    echo -e "${GREEN}Fertig: ${START} -> ${END}${NC}"
else
    echo -e "${YELLOW}Testnet: Keine Block-Generierung möglich${NC}"
fi

# Status anzeigen
echo ""
echo "=== L2 Status ==="
cli l2_getchaininfo 2>/dev/null | grep -E '"enabled"|"blockHeight"|"totalSupply"' || echo "L2 nicht verfügbar"

SUPPLY=$(cli l2_gettotalsupply 2>/dev/null | grep -o '"totalSupply": [^,]*' | cut -d' ' -f2 || echo "?")
echo "Total Supply: ${SUPPLY}"

PENDING=$(cli l2_getpendingburns 2>/dev/null | grep -c '"l1TxHash"' || echo "0")
echo "Pending Burns: ${PENDING}"

echo -e "${GREEN}Done.${NC}"
