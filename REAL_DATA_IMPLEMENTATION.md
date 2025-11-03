# 🔄 Real Data Implementation für Trust Graph - ✅ KOMPLETT

## Problem

**Aktuell:** Trust Graph zeigt Dummy-Daten (Alice, Bob, Carol, Dave)
**Soll:** Trust Graph zeigt echte Daten aus dem Wallet/Blockchain

---

## Lösung: Neues RPC Kommando

### `listtrustrelations` - Gibt alle Trust Edges zurück

**Input:** Keine Parameter (oder optional: max_count, from_address)

**Output:**
```json
{
  "edges": [
    {
      "from": "QcPLCRajUcRXEBBpRyc8unJVABAB4ERAgd",
      "to": "QXabc123...",
      "weight": 80,
      "bond_amount": 1000000,
      "timestamp": 1699123456,
      "reason": "Trusted trader"
    },
    ...
  ],
  "reputations": {
    "QcPLCRajUcRXEBBpRyc8unJVABAB4ERAgd": 75,
    "QXabc123...": 60,
    ...
  }
}
```

---

## Implementation Steps

### 1. Add RPC Command in `src/rpc/cvm.cpp`

```cpp
/**
 * listtrustrelations - List all trust relationships in the graph
 */
UniValue listtrustrelations(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listtrustrelations [max_count]\n"
            "\nList all trust relationships in the Web-of-Trust graph.\n"
            "\nArguments:\n"
            "1. max_count    (numeric, optional, default=100) Maximum number to return\n"
            "\nResult:\n"
            "{\n"
            "  \"edges\": [...],\n"
            "  \"reputations\": {...}\n"
            "}\n"
        );
    
    if (!CVM::g_cvmdb) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "CVM database not initialized");
    }
    
    int maxCount = 100;
    if (request.params.size() > 0) {
        maxCount = request.params[0].get_int();
    }
    
    CVM::TrustGraph trustGraph(*CVM::g_cvmdb);
    
    // Get all trust edges (we need to implement this)
    std::vector<CVM::TrustEdge> edges = trustGraph.GetAllTrustEdges(maxCount);
    
    UniValue edgesArray(UniValue::VARR);
    std::set<uint160> addresses;
    
    for (const auto& edge : edges) {
        UniValue edgeObj(UniValue::VOBJ);
        edgeObj.pushKV("from", edge.fromAddress.ToString());
        edgeObj.pushKV("to", edge.toAddress.ToString());
        edgeObj.pushKV("weight", edge.trustWeight);
        edgeObj.pushKV("bond_amount", ValueFromAmount(edge.bondAmount));
        edgeObj.pushKV("timestamp", edge.timestamp);
        edgeObj.pushKV("reason", edge.reason);
        
        edgesArray.push_back(edgeObj);
        addresses.insert(edge.fromAddress);
        addresses.insert(edge.toAddress);
    }
    
    // Get reputations for all addresses
    UniValue reputations(UniValue::VOBJ);
    for (const auto& addr : addresses) {
        // Get reputation (simplified - would use reputation system)
        double rep = 50.0; // Placeholder
        reputations.pushKV(addr.ToString(), rep);
    }
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("edges", edgesArray);
    result.pushKV("reputations", reputations);
    result.pushKV("count", (int64_t)edges.size());
    
    return result;
}
```

---

### 2. Add Method to TrustGraph

In `src/cvm/trustgraph.h`:
```cpp
std::vector<TrustEdge> GetAllTrustEdges(int maxCount = 100) const;
```

In `src/cvm/trustgraph.cpp`:
```cpp
std::vector<TrustEdge> TrustGraph::GetAllTrustEdges(int maxCount) const {
    std::vector<TrustEdge> edges;
    
    // List all keys with "trust_" prefix
    std::vector<std::string> keys = database.ListKeysWithPrefix("trust_");
    
    for (const auto& key : keys) {
        // Skip reverse index keys
        if (key.find("trust_in_") != std::string::npos) continue;
        
        // Read edge
        TrustEdge edge;
        if (database.ReadGeneric(key, edge)) {
            edges.push_back(edge);
            
            if ((int)edges.size() >= maxCount) break;
        }
    }
    
    return edges;
}
```

---

### 3. Update Dashboard JavaScript

In `cvmdashboard_html.h`, replace mock data with real RPC call:

```javascript
async function loadTrustGraph() {
    try {
        // Get real data from RPC
        const data = await window.dashboard.rpcCall('listtrustrelations', [50]);
        
        if (!data || !data.edges || data.edges.length === 0) {
            // Show "No data" message
            showNoDataMessage();
            return;
        }
        
        // Convert to graph format
        const nodes = [];
        const links = [];
        const nodeMap = new Map();
        
        // Build nodes
        for (const [address, rep] of Object.entries(data.reputations || {})) {
            const node = {
                id: address,
                rep: Math.round(rep),
                label: address.substring(0, 8) + '...'
            };
            nodes.push(node);
            nodeMap.set(address, node);
        }
        
        // Build links
        for (const edge of data.edges) {
            links.push({
                source: edge.from,
                target: edge.to,
                weight: edge.weight
            });
        }
        
        // Set data and render
        window.trustGraph.setData(nodes, links);
        window.trustGraph.render();
        window.trustGraph.simulate(50);
        
    } catch (error) {
        console.error('Failed to load trust graph:', error);
        // Fallback to mock data
        loadMockData();
    }
}

function loadMockData() {
    const mockNodes = [
        {id: 'QcPLC...', rep: 75, label: 'Alice'},
        {id: 'QXabc...', rep: 60, label: 'Bob'},
        {id: 'QYdef...', rep: 85, label: 'Carol'},
        {id: 'QZghi...', rep: 45, label: 'Dave'}
    ];
    const mockLinks = [
        {source: 'QcPLC...', target: 'QXabc...', weight: 80},
        {source: 'QXabc...', target: 'QYdef...', weight: 60},
        {source: 'QYdef...', target: 'QZghi...', weight: 70},
        {source: 'QcPLC...', target: 'QZghi...', weight: 50}
    ];
    window.trustGraph.setData(mockNodes, mockLinks);
    window.trustGraph.render();
    window.trustGraph.simulate(50);
}

// On DOMContentLoaded
document.addEventListener('DOMContentLoaded', () => {
    window.dashboard = new CVMDashboard();
    window.dashboard.init();
    
    window.trustGraph = new TrustGraphViz('trustGraph', 750, 380);
    window.trustGraph.init();
    
    // Load REAL data!
    loadTrustGraph();
});
```

---

## Current State

**Was wir haben:**
- ✅ `gettrustgraphstats` - Stats über den Graph
- ✅ `addtrust` - Trust Edge hinzufügen
- ✅ `getweightedreputation` - Reputation von einem Viewer aus
- ✅ `sendtrustrelation` - On-chain Trust broadcast

**Was fehlt:**
- ❌ `listtrustrelations` - Alle Edges abrufen

---

## Quick Solution (Für jetzt)

**Option A: Mock Data mit Info**
- Keep mock data
- Add banner: "⚠️ Demo Data - Use RPC to add real trust relations"

**Option B: Implement RPC**
- Add `listtrustrelations` RPC
- Load real data
- Fallback to mock if empty

**Option C: Hybrid**
- Try to load real data
- If empty, show mock data with note

---

## Empfehlung

**Für Phase 4:** Option C (Hybrid)
- Zeigt echte Daten wenn vorhanden
- Zeigt Demo-Daten wenn noch leer
- User sieht sofort wie es aussieht

**Für Production:** Option B
- Nur echte Daten
- Empty state wenn keine Daten

---

## Next Steps

1. **Add RPC Command** (`listtrustrelations`)
2. **Update Dashboard** (load real data)
3. **Test with real data** (create some trust relations)
4. **Add empty state** (when no data)

---

**Soll ich das jetzt implementieren?** 🔧

