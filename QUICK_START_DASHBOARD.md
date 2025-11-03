# 🚀 CVM Dashboard - Quick Start Guide

## ⚡ Schnellstart (1 Schritt!)

### Dashboard aktivieren

**cascoin.conf editieren:**
```ini
# Aktiviere CVM Dashboard
cvmdashboard=1
```

**Oder per Command-Line:**
```bash
./cascoind -regtest -cvmdashboard=1 -daemon
```

**Das war's!** ✅ Keine Installation nötig!

---

## 🌐 Dashboard öffnen

### Port herausfinden:
```bash
PORT=$(grep rpcport ~/.cascoin/cascoin.conf | cut -d= -f2)
echo "Dashboard URL: http://localhost:$PORT/dashboard/"
```

### Im Browser öffnen:
```
http://localhost:45789/dashboard/
```
*(ersetze 45789 mit deinem RPC Port)*

---

## ✨ Embedded HTML - Keine Installation!

**Das Dashboard ist jetzt komplett im Binary eingebettet!**

```
✅ Keine HTML-Dateien kopieren
✅ Keine CSS-Dateien kopieren  
✅ Keine JS-Dateien kopieren
✅ Alles aus dem Binary!
```

**Einfach starten und nutzen!** 🎉

---

## 🔍 Troubleshooting

### Problem: Connection Refused
**Lösung**: Dashboard nicht aktiviert
```bash
# Prüfe ob Dashboard aktiviert ist:
grep cvmdashboard ~/.cascoin/cascoin.conf

# Aktiviere es:
echo "cvmdashboard=1" >> ~/.cascoin/cascoin.conf

# Restart daemon:
./cascoin-cli -regtest stop
./cascoind -regtest -daemon
```

---

### Problem: Port nicht gefunden
**Lösung**: RPC Port prüfen
```bash
# Zeige RPC Port:
grep rpcport ~/.cascoin/cascoin.conf

# Standard Ports:
# Mainnet: 8332
# Testnet: 18332
# Regtest: 18443 (oder custom)
```

---

### Problem: Wallet crashed beim Öffnen (alte Version)
**Lösung**: Auf neue Version updaten
```bash
# Update auf neueste Version mit embedded HTML:
git pull
make -j$(nproc)
```

---

## 📊 Features

### Aktuell implementiert:
- ✅ Blockchain Info (Höhe, Difficulty, etc.)
- ✅ CVM Status (Activation, Gas Info)
- ✅ WoT Statistics (Trust Edges, Votes, Disputes)
- ✅ Auto-Refresh (alle 5 Sekunden)
- ✅ Embedded HTML/CSS/JS (keine Installation!)

### Geplant (Phase 4):
- 🔜 D3.js Trust Graph Visualization
- 🔜 Qt Button im Wallet
- 🔜 Transaction History
- 🔜 Interactive Voting Interface

---

## 🔒 Sicherheit

### Dashboard ist OFF by default!
```ini
# Default: AUS
cvmdashboard=0  # oder nicht gesetzt

# Muss explizit aktiviert werden:
cvmdashboard=1
```

### Localhost-only:
- Dashboard läuft nur auf `127.0.0.1`
- Nicht von extern erreichbar
- Gleiche Security wie RPC

### Gleiches Authentication wie RPC:
- Nutzt bestehenden HTTP Server
- RPC Username/Password Protection
- Kein separater Auth-Layer nötig

---

## 💡 Entwickler Info

### Architektur:
```
HTTP Server (Port: rpcport)
├── /          → RPC JSON Calls
├── /rest/*    → REST API
└── /dashboard/* → CVM Dashboard (embedded HTML)
```

**Alle auf dem gleichen Port!** (wie REST API)

### Code-Struktur:
```
src/httpserver/cvmdashboard.h        # Handler Header
src/httpserver/cvmdashboard.cpp      # Handler Logic (48 Zeilen!)
src/httpserver/cvmdashboard_html.h   # Embedded HTML/CSS/JS
```

### Integration:
```cpp
// src/init.cpp
if (gArgs.GetBoolArg("-cvmdashboard", false)) {
    InitCVMDashboardHandlers();
}
```

---

## 🎯 Vergleich: Vorher vs. Nachher

### Vorher (❌ Kompliziert):
```bash
# Schritt 1: Dateien kopieren
mkdir -p ~/.cascoin/regtest/html
cp -r share/html/* ~/.cascoin/regtest/html/

# Schritt 2: Daemon starten
./cascoind -regtest -cvmdashboard=1 -daemon

# Schritt 3: Dashboard öffnen
firefox http://localhost:45789/dashboard/
```

### Nachher (✅ Einfach):
```bash
# Schritt 1: Daemon starten
./cascoind -regtest -cvmdashboard=1 -daemon

# Schritt 2: Dashboard öffnen
firefox http://localhost:45789/dashboard/
```

**Von 3 Schritten auf 2!** 🚀

---

## ✅ Status

**Phase 4 Progress**:
- [x] HTTP Server Setup
- [x] Basic HTML Dashboard
- [x] Security (OFF by default)
- [x] Integration & Compilation
- [x] Testing in regtest
- [x] **Embedded HTML (keine Installation!)**
- [ ] Qt Button
- [ ] D3.js Graph Visualization

---

## 🎉 Dashboard ist LIVE!

```bash
# Start daemon
./cascoind -regtest -cvmdashboard=1 -daemon

# Open in browser
firefox http://localhost:45789/dashboard/
```

**Keine Dateien kopieren! Alles aus dem Binary!** ✅

---

## 📝 Technical Notes

### Embedded HTML Size
- **14 KB** im Binary
- Vernachlässigbar
- Immer die richtige Version

### Single-Page Application
- Alle Assets in einer HTML-Datei
- CSS im `<style>` Tag
- JavaScript im `<script>` Tag
- Zero dependencies

### Production Ready
```bash
# Deployment:
scp cascoind server:/usr/local/bin/

# Fertig! Keine HTML-Dateien nötig.
```

---

**Viel Spaß mit dem CVM Dashboard!** 🚀

**WICHTIG:** Dashboard ist standardmäßig AUS. Mit `-cvmdashboard=1` aktivieren!
