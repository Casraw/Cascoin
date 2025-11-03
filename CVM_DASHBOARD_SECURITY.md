# CVM Dashboard - Security Configuration

**WICHTIG**: Der CVM Dashboard Web-Server ist **standardmäßig DEAKTIVIERT** aus Sicherheitsgründen!

---

## 🔒 Security First

Der Dashboard-Server ist **OPT-IN**, nicht OPT-OUT:
- ✅ Standardmäßig: **AUS** (sicher)
- ⚠️ User muss explizit aktivieren
- 🔐 Läuft NUR auf localhost (127.0.0.1)
- 🚫 KEINE externen Verbindungen möglich

---

## ⚙️ Aktivierung

### Option 1: cascoin.conf
```ini
# CVM Dashboard aktivieren
cvmdashboard=1
```

### Option 2: Kommandozeile
```bash
./cascoind -cvmdashboard=1

# Oder mit cascoin-qt
./cascoin-qt -cvmdashboard=1
```

### Option 3: Regtest/Testnet (für Development)
```bash
./cascoind -regtest -cvmdashboard=1
```

---

## 🌐 Zugriff

Nach Aktivierung ist Dashboard erreichbar unter:
- **Mainnet**: http://localhost:8332/dashboard/
- **Testnet**: http://localhost:18332/dashboard/
- **Regtest**: http://localhost:18443/dashboard/

---

## 🔐 Security Features

### 1. Localhost Only
```cpp
// Server bindet NUR auf 127.0.0.1
// KEINE externen Verbindungen möglich!
```

### 2. Directory Traversal Protection
```cpp
// Verhindert Zugriff außerhalb von /html/
fs::path canonicalPath = fs::weakly_canonical(filePath);
if (canonicalPath.string().find(htmlDir.string()) != 0) {
    return ""; // Blocked!
}
```

### 3. RPC Authentication Required
```javascript
// Dashboard nutzt RPC mit Authentication
'Authorization': 'Basic ' + btoa(user + ':' + password)
```

### 4. Read-Only
```
Dashboard kann:
- ✅ Daten LESEN (via RPC)
- ❌ KEINE Transaktionen senden
- ❌ KEINE Wallet-Operationen
- ❌ NUR Read-Only RPC Calls
```

---

## 🚨 Warnung

**NIEMALS den RPC Port öffentlich zugänglich machen!**

```ini
# SICHER (nur localhost):
rpcbind=127.0.0.1
rpcallowip=127.0.0.1

# GEFÄHRLICH (nicht tun!):
# rpcbind=0.0.0.0
# rpcallowip=0.0.0.0/0
```

---

## ✅ Best Practices

### Für normale User:
```ini
# Dashboard NUR wenn nötig aktivieren
cvmdashboard=1

# RPC nur localhost
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
```

### Für Server (ohne GUI):
```ini
# Dashboard NICHT aktivieren
# cvmdashboard=0  # (ist default)

# RPC nur für lokale Tools
rpcbind=127.0.0.1
```

### Für Development:
```ini
# Regtest mit Dashboard
cvmdashboard=1
server=1
```

---

## 📊 Status prüfen

```bash
# Ist Dashboard aktiv?
grep "CVM Dashboard" ~/.cascoin/debug.log

# Erwartete Ausgabe wenn AKTIV:
# CVM Dashboard enabled - handlers registered
# CVM Dashboard available at http://localhost:8332/dashboard/

# Erwartete Ausgabe wenn INAKTIV:
# CVM Dashboard disabled (use -cvmdashboard=1 to enable)
```

---

## 🔧 Troubleshooting

### Dashboard lädt nicht:
1. Ist `-cvmdashboard=1` gesetzt? ✅
2. Ist RPC Server gestartet? ✅
3. Sind HTML-Dateien vorhanden? ✅
4. Port korrekt? (8332/18332/18443) ✅

### "Connection Failed":
1. Ist cascoind/cascoin-qt gestartet? ✅
2. RPC Username/Password korrekt? ✅
3. Browser blockiert localhost? (sollte nicht) ✅

---

## 📁 Dateien

Dashboard Dateien sind hier:
```
~/.cascoin/html/           # User DataDir (falls vorhanden)
  oder
/usr/share/cascoin/html/   # System Installation
  oder
./share/html/              # Source Directory
```

---

## 🎯 Zusammenfassung

**Default: AUS** ✅
- Sicher by default
- User muss bewusst aktivieren
- Nur localhost access
- RPC auth required
- Read-only operations
- Directory traversal blocked

**So soll es sein!** 🔒

