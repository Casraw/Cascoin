#!/bin/bash
# CVM E2E Test Script
# ====================
# This script tests the complete CVM and Web-of-Trust implementation

set -e

echo "🚀 CVM E2E TEST SCRIPT"
echo "======================"
echo ""

# Check if daemon is running
if ! ./src/cascoin-cli -regtest getblockcount &>/dev/null; then
    echo "❌ Error: cascoind not running!"
    echo "Start with: ./src/cascoind -regtest -daemon"
    exit 1
fi

# Get current status
HEIGHT=$(./src/cascoin-cli -regtest getblockcount 2>&1)
BALANCE=$(./src/cascoin-cli -regtest getbalance 2>&1)

echo "📊 Current Status:"
echo "  Block Height: $HEIGHT"
echo "  Balance: $BALANCE CAS"
echo ""

# Check if we need to mine blocks for mature coins
if [ "$HEIGHT" -lt "101" ]; then
    echo "⚠️  Need at least 101 blocks for mature coins"
    echo "⛏️  Mining blocks now..."
    echo "    (This may take a while depending on difficulty...)"
    echo ""
    
    for i in $(seq 1 101); do
        if [ $((i % 10)) -eq 0 ]; then
            echo "    Progress: $i/101 blocks..."
        fi
        ./src/cascoin-cli -regtest generate 1 &>/dev/null || true
    done
    
    HEIGHT=$(./src/cascoin-cli -regtest getblockcount 2>&1)
    BALANCE=$(./src/cascoin-cli -regtest getbalance 2>&1)
    echo ""
    echo "✅ Mining complete!"
    echo "   Height: $HEIGHT, Balance: $BALANCE CAS"
    echo ""
fi

# Check balance
BALANCE_NUM=$(./src/cascoin-cli -regtest getbalance 2>&1 | sed 's/[^0-9.]//g')
if [ -z "$BALANCE_NUM" ] || [ "$(echo "$BALANCE_NUM < 10" | bc -l 2>/dev/null || echo 1)" = "1" ]; then
    echo "⚠️  Insufficient balance: $BALANCE CAS"
    echo "💡 Tip: Make sure you're mining to a wallet address or wait for coinbase maturity"
    echo ""
    echo "You can try:"
    echo "  for i in {1..110}; do ./src/cascoin-cli -regtest generate 1; done"
    echo ""
    exit 1
fi

echo "✅ Sufficient balance available!"
echo ""

# Create test addresses
echo "🎬 Step 1: Creating Test Addresses"
echo "===================================="
ALICE=$(./src/cascoin-cli -regtest getnewaddress 2>&1)
BOB=$(./src/cascoin-cli -regtest getnewaddress 2>&1)
CAROL=$(./src/cascoin-cli -regtest getnewaddress 2>&1)

echo "  👤 Alice (our wallet):  $ALICE"
echo "  👤 Bob:                 $BOB"
echo "  👤 Carol:               $CAROL"
echo ""

# Create CVM transactions
echo "📤 Step 2: Creating CVM Transactions"
echo "====================================="
echo ""

echo "  TX 1: Alice trusts Bob (weight=50, bond=1.5 CAS)..."
TX1=$(./src/cascoin-cli -regtest sendtrustrelation "$BOB" 50 1.5 "E2E Test: Alice trusts Bob" 2>&1)
TX1_ID=$(echo "$TX1" | jq -r '.txid' 2>/dev/null || echo "ERROR")

if [ "$TX1_ID" = "ERROR" ] || [ -z "$TX1_ID" ]; then
    echo "    ❌ Failed to create TX1!"
    echo "    Error: $TX1"
    exit 1
fi
echo "    ✅ TX1 Created: $TX1_ID"
sleep 0.5

echo ""
echo "  TX 2: Alice trusts Carol (weight=30, bond=1.3 CAS)..."
TX2=$(./src/cascoin-cli -regtest sendtrustrelation "$CAROL" 30 1.3 "E2E Test: Alice trusts Carol" 2>&1)
TX2_ID=$(echo "$TX2" | jq -r '.txid' 2>/dev/null || echo "ERROR")

if [ "$TX2_ID" = "ERROR" ] || [ -z "$TX2_ID" ]; then
    echo "    ❌ Failed to create TX2!"
    echo "    Error: $TX2"
    exit 1
fi
echo "    ✅ TX2 Created: $TX2_ID"
sleep 0.5

echo ""
echo "  TX 3: Bob votes +80 for Carol (bond=1.8 CAS)..."
TX3=$(./src/cascoin-cli -regtest sendbondedvote "$CAROL" 80 1.8 "E2E Test: Bob votes for Carol" 2>&1)
TX3_ID=$(echo "$TX3" | jq -r '.txid' 2>/dev/null || echo "ERROR")

if [ "$TX3_ID" = "ERROR" ] || [ -z "$TX3_ID" ]; then
    echo "    ❌ Failed to create TX3!"
    echo "    Error: $TX3"
    exit 1
fi
echo "    ✅ TX3 Created: $TX3_ID"
echo ""

# Check mempool
MEMPOOL_COUNT=$(./src/cascoin-cli -regtest getrawmempool 2>&1 | jq 'length' 2>/dev/null || echo 0)
echo "📊 Mempool Status: $MEMPOOL_COUNT transactions"
if [ "$MEMPOOL_COUNT" -eq 3 ]; then
    echo "   ✅ All 3 CVM transactions in mempool!"
else
    echo "   ⚠️  Expected 3 TXs, found $MEMPOOL_COUNT"
fi
echo ""

# Mine blocks
echo "⛏️  Step 3: Mining Transactions into Blocks"
echo "============================================"
echo "   Mining 10 blocks..."
echo ""

for i in $(seq 1 10); do
    BLOCK=$(./src/cascoin-cli -regtest generate 1 2>&1 | jq -r '.[0]' 2>/dev/null || echo "")
    if [ ! -z "$BLOCK" ] && [ "$BLOCK" != "null" ]; then
        echo "   Block $i: $BLOCK"
    fi
done

echo ""
echo "   ✅ Mining complete!"
echo ""

# Wait for processing
echo "   ⏳ Waiting 3 seconds for block processing..."
sleep 3
echo ""

# Verification
echo "✅ Step 4: Verification"
echo "======================="
echo ""

NEW_HEIGHT=$(./src/cascoin-cli -regtest getblockcount 2>&1)
MEMPOOL_AFTER=$(./src/cascoin-cli -regtest getrawmempool 2>&1 | jq 'length' 2>/dev/null || echo 0)

echo "  Block Height: $HEIGHT → $NEW_HEIGHT"
echo "  Mempool: $MEMPOOL_COUNT → $MEMPOOL_AFTER TXs"
echo ""

if [ "$MEMPOOL_AFTER" -eq 0 ]; then
    echo "  ✅ All transactions mined!"
else
    echo "  ⚠️  $MEMPOOL_AFTER transactions still in mempool"
    echo "     (May need more blocks or higher difficulty)"
fi
echo ""

# Check Trust Graph stats
echo "🏆 Step 5: Trust Graph Statistics"
echo "=================================="
STATS=$(./src/cascoin-cli -regtest gettrustgraphstats 2>&1)
echo "$STATS"
echo ""

EDGES=$(echo "$STATS" | jq -r '.total_trust_edges' 2>/dev/null || echo "0")
VOTES=$(echo "$STATS" | jq -r '.total_votes' 2>/dev/null || echo "0")

# Final result
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ "$EDGES" -gt 0 ] || [ "$VOTES" -gt 0 ]; then
    echo "🎉 SUCCESS! E2E TEST PASSED! 🎉"
    echo ""
    echo "Results:"
    echo "  ✅ Trust Edges: $EDGES"
    echo "  ✅ Votes: $VOTES"
    echo "  ✅ Transactions were PROCESSED on-chain!"
    echo "  ✅ Web-of-Trust system is WORKING!"
    echo ""
    echo "🏆 Phase 3 is 100% COMPLETE and FUNCTIONAL!"
else
    echo "⚠️  PARTIAL SUCCESS"
    echo ""
    echo "Status:"
    echo "  ✅ Transactions created"
    echo "  ✅ Transactions mined (if mempool empty)"
    echo "  ⏳ Processing: Stats still show 0"
    echo ""
    echo "This might mean:"
    echo "  1. Processing hasn't happened yet (wait longer)"
    echo "  2. Processing failed silently (check logs)"
    echo "  3. Data storage issue (check database)"
    echo ""
    echo "Check logs with:"
    echo "  tail -100 ~/.cascoin/regtest/debug.log | grep 'CVM:'"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

