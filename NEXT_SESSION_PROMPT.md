# 🚀 Prompt für nächste Session - HAT v2 Completion

## COPY-PASTE THIS TO START NEXT SESSION:

```
Hi! Wir arbeiten am Cascoin Projekt und implementieren gerade HAT v2 (Hybrid Adaptive Trust) - ein manipulationssicheres Reputation-System.

## Was bereits FERTIG ist (1001 Zeilen):

✅ Phase 1: BehaviorMetrics (behaviormetrics.h/cpp)
   - Trade Partner Diversity Detection
   - Volume Analysis (Logarithmic Scaling)
   - Suspicious Pattern Detection (CV Analysis)
   - Base + Final Reputation Calculation

✅ Phase 2: GraphAnalysis (graphanalysis.h/cpp)
   - Cluster Detection (tightly connected fakes)
   - Betweenness Centrality (shortest paths)
   - Degree & Closeness Centrality
   - Entry Point Detection

✅ Phase 3-5 Header: SecureHAT (securehat.h)
   - StakeInfo struct
   - TemporalMetrics struct
   - TrustBreakdown struct
   - SecureHAT class definition

Alle Files kompilieren erfolgreich!

## Was JETZT zu tun ist:

Wir müssen Phase 3-8 fertigstellen:

### Phase 3-5: SecureHAT Implementation
1. Erstelle `src/cvm/securehat.cpp` mit:
   - TemporalMetrics::CalculateActivityScore()
   - SecureHAT::CalculateFinalTrust()
   - SecureHAT::CalculateWithBreakdown()
   - Storage/retrieval methods

   Formula für Final Trust:
   ```
   40% * secure_behavior +
   30% * secure_wot +
   20% * secure_economic +
   10% * secure_temporal
   ```

### Phase 6: RPC Commands
2. Update `src/rpc/cvm.cpp` mit neuen RPCs:
   - getbehaviormetrics <address>
   - getgraphmetrics <address>
   - getsecuretrust <target> [viewer]
   - gettrustbreakdown <target>
   - detectclusters

### Phase 7: Testing
3. Erstelle Test-Script für Attack Vectors:
   - Fake Trade Attack (2 accounts, 100 trades)
   - Fake Cluster Attack (10 accounts, mutual trust)
   - Temporary Stake Attack
   - Pattern Detection Test

### Phase 8: Dashboard Integration
4. Update Dashboard um HAT v2 Score zu zeigen:
   - Score breakdown visualization
   - Component indicators
   - Warning für low scores

## Wichtige Dateien zum Lesen:

- HAT_IMPLEMENTATION_PLAN.md (detaillierter Plan)
- HAT_V2_STATUS.md (aktueller Status)
- TRUST_SECURITY_ANALYSIS.md (Security Analyse)
- INNOVATIVE_TRUST_CONCEPTS.md (Konzepte & Theorie)

## Architektur:

HAT v2 = Multi-Layer Defense:
- Layer 1: Behavior Metrics (40%) - Objektive Metriken
- Layer 2: Web-of-Trust (30%) - Subjektive, personalisiert
- Layer 3: Economic Stake (20%) - Skin in the game
- Layer 4: Temporal (10%) - Activity & Time

Defense Mechanisms:
- Diversity Check: 100 trades, 2 partners = 0.2x penalty
- Cluster Detection: Mutual trust > 0.9 = suspicious
- Stake Lock: 6 Monate minimum
- Activity Tracking: Gaps > 6 Monate = penalty

## Ziel dieser Session:

Fertigstellung von Phase 3-8, sodass wir ein vollständig funktionierendes, 
getestetes, und dokumentiertes HAT v2 System haben.

Geschätzter Aufwand: 12-15 Stunden intensive Arbeit.

Können wir loslegen? Starte bitte mit Phase 3: SecureHAT Implementation!
```

---

## ODER kürzer, wenn du willst:

```
Hi! Wir implementieren HAT v2 (Hybrid Adaptive Trust) für Cascoin.

Status: Phase 1-2 fertig (BehaviorMetrics + GraphAnalysis, 1001 Zeilen), 
        kompiliert erfolgreich!

Jetzt: Phase 3-8 fertigstellen
- securehat.cpp implementieren (Calculator)
- RPC Commands (getsecuretrust etc.)
- Testing (Attack Vectors)
- Dashboard Integration

Lies bitte:
- HAT_V2_STATUS.md (Status)
- HAT_IMPLEMENTATION_PLAN.md (Plan)
- TRUST_SECURITY_ANALYSIS.md (Security)

Starte mit: Phase 3 - SecureHAT Implementation!
```

---

Welchen Prompt möchtest du? Lang (detailliert) oder kurz (kompakt)?

