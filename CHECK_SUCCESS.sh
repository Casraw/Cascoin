#!/bin/bash
# Quick Check Script - Did CVM TXs get processed?

echo "🔍 CHECKING CVM STATUS"
echo "====================="
echo ""

# 1. Check mempool
echo "1️⃣ Mempool Status:"
MEMPOOL_COUNT=$(./src/cascoin-cli -regtest getrawmempool 2>/dev/null | jq '. | length' 2>/dev/null || echo "0")
if [ "$MEMPOOL_COUNT" = "0" ]; then
    echo "   ✅ Mempool is EMPTY (TXs were mined!)"
else
    echo "   ⏳ Mempool has $MEMPOOL_COUNT TXs (keep generating blocks)"
fi
echo ""

# 2. Check block height
echo "2️⃣ Block Height:"
BLOCKS=$(./src/cascoin-cli -regtest getblockcount 2>/dev/null)
echo "   📊 Current: $BLOCKS blocks"
echo ""

# 3. Check Trust Graph Stats
echo "3️⃣ Trust Graph Statistics:"
./src/cascoin-cli -regtest gettrustgraphstats 2>/dev/null | grep -E "(total_trust_edges|total_votes)" | sed 's/^/   /'

EDGES=$(./src/cascoin-cli -regtest gettrustgraphstats 2>/dev/null | jq -r '.total_trust_edges' 2>/dev/null || echo "0")
VOTES=$(./src/cascoin-cli -regtest gettrustgraphstats 2>/dev/null | jq -r '.total_votes' 2>/dev/null || echo "0")

echo ""
if [ "$EDGES" -gt "0" ] || [ "$VOTES" -gt "0" ]; then
    echo "   🎉 SUCCESS! CVM TXs were processed!"
    echo "   ✅ Trust Edges: $EDGES"
    echo "   ✅ Votes: $VOTES"
else
    echo "   ⏳ Not processed yet (stats still 0)"
    echo "   💡 Keep generating blocks or check logs below"
fi
echo ""

# 4. Check recent CVM logs
echo "4️⃣ Recent CVM Activity (last 10 lines):"
tail -200 ~/.cascoin/regtest/debug.log 2>/dev/null | grep "CVM:" | tail -10 | sed 's/^/   /'
echo ""

# 5. Summary
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ "$MEMPOOL_COUNT" = "0" ] && [ "$EDGES" -gt "0" ]; then
    echo "✅ ALLES FUNKTIONIERT PERFEKT! 🎉"
    echo ""
    echo "Deine CVM Transaktionen wurden:"
    echo "  ✅ Gemined (aus Mempool)"
    echo "  ✅ Verarbeitet (von CVMBlockProcessor)"
    echo "  ✅ Gespeichert (in Database)"
elif [ "$MEMPOOL_COUNT" = "0" ]; then
    echo "⏳ TXs sind gemined, aber noch nicht verarbeitet"
    echo "   Generiere noch ein paar Blocks..."
else
    echo "⏳ TXs sind noch im Mempool"
    echo "   Generiere weiter Blocks mit:"
    echo "   ./src/cascoin-cli -regtest generate 10"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

