# 🔒 Web-of-Trust Security Analysis

## ❌ Aktueller Status: NICHT manipulationssicher!

---

## Problem

**Frage:** Wenn ich eine Adresse künstlich hoch truste, sehen andere das auch?

**Antwort:** JA! Und das ist das Problem! ❌

### Aktuelles Verhalten:

```
User A: A → X (trust: 100) ← Künstlich hoch!
User B schaut Dashboard
Dashboard zeigt: X hat Reputation von 100
→ User B wird manipuliert! ❌
```

---

## Die Lösung existiert bereits im Code! ✅

### Vorhanden: `GetWeightedReputation(viewer, target)`

```cpp
// src/cvm/trustgraph.cpp - Line 154
double TrustGraph::GetWeightedReputation(
    const uint160& viewer,    // Wer schaut?
    const uint160& target,    // Wen wird angeschaut?
    int maxDepth              // Max Trust-Pfad Tiefe
) {
    // 1. Finde Trust-Pfade von viewer → target
    std::vector<TrustPath> paths = FindTrustPaths(viewer, target, maxDepth);
    
    if (paths.empty()) {
        // ⚠️ KEIN Pfad gefunden!
        // Fallback: Globale Reputation (PROBLEM!)
        return GlobalAverageReputation(target);
    }
    
    // 2. Berechne gewichtete Reputation basierend auf Pfaden
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    
    for (const auto& path : paths) {
        // Nur Votes die über Trust-Pfade erreichbar sind!
        double pathWeight = path.totalWeight;
        weightedSum += vote.voteValue * pathWeight;
        totalWeight += pathWeight;
    }
    
    return weightedSum / totalWeight;
}
```

**Key Point:** Reputation ist NUR relevant wenn ein Trust-Pfad existiert!

---

## Warum ist das nicht manipulierbar?

### Szenario: Eve versucht Manipulation

```
Echtes Netzwerk:
  Alice → Bob → Carol

Eve's Versuch:
  Eve → Mallory (trust: 100) ← Künstlich hoch!
```

### Alice's Sicht (KORREKT):

```javascript
// Alice fragt: "Was ist Mallory's Reputation?"
GetWeightedReputation(alice_address, mallory_address, 3)

// System sucht Pfade: Alice → ... → Mallory
// Gefunden: KEINE Pfade!

// Return: 0.0 oder "Unbekannt"
```

**Ergebnis:**
- ✅ Eve's künstlicher Trust hat KEINE Wirkung auf Alice!
- ✅ Mallory bleibt "unbekannt" für Alice
- ✅ Manipulation fehlgeschlagen!

### Bob's Sicht (AUCH KORREKT):

```javascript
// Bob fragt: "Was ist Carol's Reputation?"
GetWeightedReputation(bob_address, carol_address, 3)

// System findet: Bob → Carol (direkt)
// Trust Weight: 70%

// Return: 70.0
```

**Ergebnis:**
- ✅ Nur Bob's eigener Trust zählt!
- ✅ Andere User's Trust (wie Eve) hat keine Wirkung
- ✅ Personalisierte Reputation!

---

## Das Problem im Dashboard

### Aktueller Code:

```javascript
// src/httpserver/cvmdashboard_html.h
const result = await window.dashboard.rpcCall('listtrustrelations', [100]);

// Zeigt ALLE Trust-Edges
// Berechnet Reputation GLOBAL

const rep = result.reputations[address] || 50;
```

**Problem:**
- ❌ Keine "Viewer"-Perspektive
- ❌ Zeigt globale Reputation
- ❌ Jeder sieht das gleiche
- ❌ Manipulierbar!

### Wie es sein sollte:

```javascript
// Get MY wallet address
const myAddress = await rpcCall('getmyaddress');

// Get reputation from MY perspective
const rep = await rpcCall('getweightedreputation', [targetAddress, myAddress, 3]);

// Result: Nur Trust-Pfade die von MIR ausgehen!
```

**Ergebnis:**
- ✅ Personalisierte Reputation
- ✅ Nur meine Trust-Pfade zählen
- ✅ NICHT manipulierbar!

---

## Trust-Pfad Beispiele

### Direkte Verbindung:
```
Alice → Bob (trust: 80)

Alice's Sicht auf Bob: 80%
→ Direkte Verbindung, volle Gewichtung
```

### 2-Hop Pfad:
```
Alice → Bob (80) → Carol (70)

Alice's Sicht auf Carol: 80% * 70% = 56%
→ Trust wird durch Pfad-Länge geschwächt
```

### Kein Pfad:
```
Alice → Bob
Eve → Mallory ← Kein Pfad von Alice!

Alice's Sicht auf Mallory: 0% / "Unbekannt"
→ NICHT vertrauenswürdig aus Alice's Sicht!
```

### Multiple Pfade:
```
Alice → Bob → Carol (56%)
Alice → Dave → Carol (48%)

Alice's Sicht auf Carol: Durchschnitt = 52%
→ Mehrere Pfade verstärken Trust
```

---

## Sicherheits-Features

### ✅ Was schon funktioniert:

1. **Trust-Pfad Suche**
   - `FindTrustPaths(viewer, target, maxDepth)`
   - Findet alle Pfade mit max 3 Hops
   - Ignoriert Zyklen (visited set)

2. **Gewichtete Reputation**
   - `GetWeightedReputation(viewer, target)`
   - Berechnet basierend auf Pfad-Stärke
   - Kombiniert multiple Pfade

3. **Slash-Protection**
   - `edge.slashed` Flag
   - DAO kann schlechte Votes slashen
   - Geslashte Edges werden ignoriert

4. **Bond-Requirement**
   - Jeder Trust kostet Bond (CAS)
   - Verhindert Spam
   - Macht Manipulation teuer

### ❌ Was fehlt:

1. **Dashboard Integration**
   - Dashboard nutzt noch GLOBALE Reputation
   - Keine "Viewer"-Perspektive
   - Zeigt alle Trust-Edges (nicht gefiltert)

2. **RPC Enhancement**
   - `listtrustrelations` sollte Viewer-Parameter haben
   - Nur relevante Edges zurückgeben
   - Reputation personalisieren

---

## Vergleich: Global vs. Personalisiert

### Global (Aktuell - UNSICHER):

```
Trust Edges on-chain:
  Eve → Mallory (100) ← Manipuliert!
  Alice → Bob (80)
  Bob → Carol (70)

Jeder User sieht:
  Mallory: Reputation 100 ❌
  Carol: Reputation 70
```

**Problem:** Eve's Manipulation beeinflusst ALLE User!

### Personalisiert (Web-of-Trust - SICHER):

```
Trust Edges on-chain:
  Eve → Mallory (100)
  Alice → Bob (80)
  Bob → Carol (70)

Alice's Sicht:
  Mallory: Unbekannt (kein Pfad) ✅
  Carol: 56% (via Bob) ✅

Eve's Sicht:
  Mallory: 100% (direkter Trust)
  Carol: Unbekannt (kein Pfad)
```

**Lösung:** Jeder User hat seine eigene Reputation-Sicht!

---

## Fix-Strategie

### Phase 1: RPC Enhancement

```cpp
// New RPC: listmytrustnetwork
UniValue listmytrustnetwork(const JSONRPCRequest& request) {
    // Get my wallet address
    std::string myAddr = GetMyAddress();
    
    // Find all addresses reachable via trust paths
    TrustGraph graph(*g_cvmdb);
    std::set<uint160> reachable = graph.FindReachableAddresses(myAddr, 3);
    
    // Return only relevant trust edges
    UniValue edges(UniValue::VARR);
    for (const auto& addr : reachable) {
        // Get edges for this address
        auto outgoing = graph.GetOutgoingTrust(addr);
        for (const auto& edge : outgoing) {
            if (reachable.count(edge.toAddress)) {
                // This edge is relevant for my network!
                edges.push_back(EdgeToJSON(edge));
            }
        }
    }
    
    return edges;
}
```

### Phase 2: Dashboard Integration

```javascript
// Load MY trust network (not global)
async function loadMyTrustNetwork() {
    const myAddr = await rpcCall('getmyaddress');
    const result = await rpcCall('listmytrustnetwork');
    
    // Build graph from MY perspective
    result.edges.forEach(edge => {
        // Get reputation from MY perspective
        const rep = await rpcCall('getweightedreputation', 
                                   [edge.to, myAddr, 3]);
        // ...
    });
}
```

### Phase 3: UI Indicator

```html
<div class="trust-indicator">
    🔒 Personalized View
    <small>Only showing nodes in your trust network</small>
</div>
```

---

## Soll ich das fixen?

**Option 1: Quick Fix (30 min)**
- Dashboard zeigt "Global View" Warning
- Erklärt dass es nicht personalisiert ist

**Option 2: Full Fix (2-3 Stunden)**
- Neues RPC: `listmytrustnetwork`
- Dashboard zeigt NUR mein Trust-Netzwerk
- Personalisierte Reputation
- Manipulations-sicher! ✅

**Option 3: Advanced (1 Tag)**
- Toggle zwischen "My View" / "Global View"
- Trust-Pfad Visualisierung
- Interactive: "Wie würde X aussehen wenn ich Y truste?"
- Full Web-of-Trust Experience

---

## Fazit

**Aktuelle Antwort auf deine Frage:**
❌ NEIN, aktuell ist es NICHT bullet-proof gegen Manipulation!

**ABER:**
✅ Die Infrastruktur existiert bereits im Code!
✅ `GetWeightedReputation` macht es schon richtig!
✅ Nur das Dashboard nutzt es noch nicht!

**Lösung:**
Dashboard muss personalisierte Reputation zeigen, nicht globale!

---

Soll ich Option 2 implementieren? 🚀

