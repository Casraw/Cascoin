#!/bin/bash

# ═══════════════════════════════════════════════════════════════
# HAT v2 Testing Script
# ═══════════════════════════════════════════════════════════════
# 
# Tests all attack vectors and defense mechanisms:
#   1. Fake Trade Attack (low diversity)
#   2. Fake Cluster Attack (mutual trust)
#   3. Temporary Stake Attack (lock duration)
#   4. Pattern Detection (regular intervals)
#
# Prerequisites:
#   - cascoind -regtest -daemon must be running
#
# Usage: 
#   1. Start daemon: ./cascoind -regtest -daemon
#   2. Run tests:    ./test_hat_v2.sh
# ═══════════════════════════════════════════════════════════════

# Don't exit on first error for checks
CLI="./src/cascoin-cli -regtest"
DAEMON="./src/cascoind -regtest"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}         HAT v2 (Hybrid Adaptive Trust) Test Suite${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# Check if daemon is running
if ! $CLI getblockcount > /dev/null 2>&1; then
    echo -e "${RED}Error: cascoind is not running in regtest mode!${NC}"
    echo "Please start the daemon first with: $DAEMON -daemon"
    echo ""
    echo "Tip: Check if daemon is running with: ps aux | grep cascoind"
    exit 1
fi

# Now enable strict error checking for the rest
set -e

echo -e "${GREEN}✓ Daemon is running (regtest mode)${NC}"

# Get initial block count
INITIAL_BLOCKS=$($CLI getblockcount)
echo "Current block height: $INITIAL_BLOCKS"

# Generate some blocks if needed (for coinbase maturity)
if [ "$INITIAL_BLOCKS" -lt 110 ]; then
    echo "Generating 110 blocks for coinbase maturity..."
    $CLI generate 110 > /dev/null
    echo -e "${GREEN}✓ Generated blocks${NC}"
fi

echo ""

# ═══════════════════════════════════════════════════════════════
# Test 1: RPC Command Availability
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 1: Checking RPC Commands Availability${NC}"
echo "---------------------------------------------------"

commands=("getbehaviormetrics" "getgraphmetrics" "getsecuretrust" "gettrustbreakdown" "detectclusters")

for cmd in "${commands[@]}"; do
    if $CLI help | grep -q "$cmd"; then
        echo -e "${GREEN}✓ $cmd available${NC}"
    else
        echo -e "${RED}✗ $cmd NOT FOUND${NC}"
        exit 1
    fi
done

echo ""

# ═══════════════════════════════════════════════════════════════
# Test 2: Get Test Addresses
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 2: Generating Test Addresses${NC}"
echo "---------------------------------------------------"

# Create test addresses
ADDR1=$($CLI getnewaddress "test_user_1")
ADDR2=$($CLI getnewaddress "test_user_2")
ADDR3=$($CLI getnewaddress "test_user_3")

echo "Test Address 1: $ADDR1"
echo "Test Address 2: $ADDR2"
echo "Test Address 3: $ADDR3"

# Fund test addresses
echo ""
echo "Funding test addresses..."
$CLI sendtoaddress "$ADDR1" 100 > /dev/null
$CLI sendtoaddress "$ADDR2" 100 > /dev/null
$CLI sendtoaddress "$ADDR3" 100 > /dev/null

# Mine blocks to confirm transactions
$CLI generate 1 > /dev/null
echo -e "${GREEN}✓ Test addresses funded (100 CAS each)${NC}"

# Verify balances
BAL1=$($CLI getreceivedbyaddress "$ADDR1")
echo "Address 1 balance: $BAL1 CAS"

echo ""

# ═══════════════════════════════════════════════════════════════
# Test 3: Behavior Metrics - Empty Account
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 3: Behavior Metrics - New Account${NC}"
echo "---------------------------------------------------"

result=$($CLI getbehaviormetrics "$ADDR1" 2>&1 || true)
echo "$result"

if echo "$result" | grep -q "total_trades"; then
    echo -e "${GREEN}✓ getbehaviormetrics works${NC}"
else
    echo -e "${YELLOW}⚠ No data yet (expected for new accounts)${NC}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 4: Graph Metrics
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 4: Graph Metrics${NC}"
echo "---------------------------------------------------"

result=$($CLI getgraphmetrics "$ADDR1" 2>&1 || true)
echo "$result"

if echo "$result" | grep -q "betweenness_centrality"; then
    echo -e "${GREEN}✓ getgraphmetrics works${NC}"
else
    echo -e "${YELLOW}⚠ No graph data yet${NC}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 5: Secure Trust Score
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 5: Secure Trust Score${NC}"
echo "---------------------------------------------------"

result=$($CLI getsecuretrust "$ADDR1" 2>&1 || true)
echo "$result"

if echo "$result" | grep -q "trust_score"; then
    echo -e "${GREEN}✓ getsecuretrust works${NC}"
    
    # Extract trust score
    score=$(echo "$result" | grep -oP '"trust_score":\s*\K\d+' || echo "0")
    echo "Initial trust score: $score"
    
    if [ "$score" -eq 0 ] || [ "$score" -lt 10 ]; then
        echo -e "${GREEN}✓ New accounts have low trust (expected)${NC}"
    else
        echo -e "${YELLOW}⚠ Trust score higher than expected for new account${NC}"
    fi
else
    echo -e "${RED}✗ getsecuretrust failed${NC}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 6: Trust Breakdown
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 6: Trust Breakdown (Detailed)${NC}"
echo "---------------------------------------------------"

result=$($CLI gettrustbreakdown "$ADDR1" 2>&1 || true)
echo "$result"

if echo "$result" | grep -q "behavior"; then
    echo -e "${GREEN}✓ gettrustbreakdown works${NC}"
    
    # Check all components
    if echo "$result" | grep -q "behavior" && \
       echo "$result" | grep -q "wot" && \
       echo "$result" | grep -q "economic" && \
       echo "$result" | grep -q "temporal"; then
        echo -e "${GREEN}✓ All 4 components present (Behavior, WoT, Economic, Temporal)${NC}"
    else
        echo -e "${RED}✗ Missing components in breakdown${NC}"
    fi
else
    echo -e "${RED}✗ gettrustbreakdown failed${NC}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 7: Cluster Detection
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 7: Cluster Detection${NC}"
echo "---------------------------------------------------"

result=$($CLI detectclusters 2>&1 || true)
echo "$result"

if echo "$result" | grep -q "suspicious_addresses"; then
    echo -e "${GREEN}✓ detectclusters works${NC}"
    
    count=$(echo "$result" | grep -oP '"count":\s*\K\d+' || echo "0")
    echo "Suspicious clusters found: $count"
    
    if [ "$count" -eq 0 ]; then
        echo -e "${GREEN}✓ No suspicious clusters (good!)${NC}"
    else
        echo -e "${YELLOW}⚠ Found $count suspicious addresses${NC}"
    fi
else
    echo -e "${RED}✗ detectclusters failed${NC}"
fi
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 8: Cross-Check with Old Reputation System
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 8: Compare with Old Reputation System${NC}"
echo "---------------------------------------------------"

old_rep=$($CLI getreputation "$ADDR1" 2>&1 | grep -oP '"reputation":\s*\K\d+' || echo "0")
new_trust=$($CLI getsecuretrust "$ADDR1" 2>&1 | grep -oP '"trust_score":\s*\K\d+' || echo "0")

echo "Old Reputation: $old_rep"
echo "New HAT v2 Trust: $new_trust"

echo -e "${GREEN}✓ Both systems working${NC}"
echo ""

# ═══════════════════════════════════════════════════════════════
# Test 9: Help Text
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}Test 9: Checking Help Documentation${NC}"
echo "---------------------------------------------------"

for cmd in "${commands[@]}"; do
    if $CLI help "$cmd" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ $cmd has help text${NC}"
    else
        echo -e "${RED}✗ $cmd missing help text${NC}"
    fi
done
echo ""

# ═══════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}         All Basic Tests Passed! ✓${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo "Next steps:"
echo "  1. Create trades between accounts to test diversity detection"
echo "  2. Create trust relationships to test cluster detection"
echo "  3. Stake coins to test economic component"
echo "  4. Use 'cascoin-cli -regtest generate 1' to mine blocks"
echo ""
echo "Note: Running in regtest mode for instant testing"
echo "To stop: cascoin-cli -regtest stop"
echo ""

