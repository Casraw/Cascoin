# 🐛 Dashboard Crash Bug - GEFIXT

## Problem
**Symptom**: Das gesamte Wallet stürzt ab sobald man das Dashboard im Browser aufruft

## Root Cause
```cpp
// Line 55 in src/httpserver/cvmdashboard.cpp
fs::path canonicalHtmlDir = fs::canonical(htmlDir);
```

**`fs::canonical()` wirft eine Exception wenn der Pfad nicht existiert!**

Da das HTML-Directory noch nicht existierte:
1. Browser ruft `/dashboard/` auf
2. `ReadDashboardFile()` wird aufgerufen
3. `htmlDir` existiert nicht
4. `fs::canonical(htmlDir)` wirft `fs::filesystem_error` Exception
5. **Exception wird nicht gecatcht**
6. Wallet crashed! 💥

---

## Fix

### 1. Exception Handling hinzugefügt

```cpp
// Check if html directory exists BEFORE calling canonical
if (!fs::exists(htmlDir)) {
    LogPrintf("CVM Dashboard: HTML directory not found: %s\n", htmlDir.string());
    return "";
}

// Security: Prevent directory traversal
// Use try-catch as fs::canonical can throw if path doesn't exist
try {
    fs::path canonicalHtmlDir = fs::canonical(htmlDir);
    fs::path canonicalFilePath = fs::weakly_canonical(filePath);
    
    if (canonicalFilePath.string().find(canonicalHtmlDir.string()) != 0) {
        LogPrintf("CVM Dashboard: Blocked directory traversal attempt: %s\n", filename);
        return "";
    }
} catch (const fs::filesystem_error& e) {
    LogPrintf("CVM Dashboard: Filesystem error: %s\n", e.what());
    return "";
}
```

**Änderungen**:
- ✅ Prüfe ob `htmlDir` existiert **BEVOR** `fs::canonical()` aufgerufen wird
- ✅ Wrap `fs::canonical()` in try-catch Block
- ✅ Graceful error handling statt Crash

---

### 2. HTML Files Installation

HTML-Dateien müssen ins richtige Verzeichnis kopiert werden:

**Für Regtest**:
```bash
mkdir -p ~/.cascoin/regtest/html
cp -r share/html/* ~/.cascoin/regtest/html/
```

**Für Testnet**:
```bash
mkdir -p ~/.cascoin/testnet3/html
cp -r share/html/* ~/.cascoin/testnet3/html/
```

**Für Mainnet**:
```bash
mkdir -p ~/.cascoin/html
cp -r share/html/* ~/.cascoin/html/
```

---

## Testing

```bash
# Start daemon mit Dashboard
./cascoind -regtest -cvmdashboard=1 -daemon

# Test im Terminal
PORT=$(grep rpcport ~/.cascoin/cascoin.conf | cut -d= -f2)
curl http://localhost:$PORT/dashboard/

# Test im Browser
firefox http://localhost:45789/dashboard/
```

**Erwartetes Resultat**: ✅ Dashboard lädt ohne Crash!

---

## Lessons Learned

1. **Immer Exception Handling für Filesystem-Operationen!**
   - `fs::canonical()` kann werfen
   - `fs::exists()` ist dein Freund

2. **Installation von Daten-Dateien**
   - HTML/CSS/JS müssen installiert werden
   - Nicht nur compiled Code!

3. **Robustness**
   - Graceful degradation statt Crash
   - Aussagekräftige Error-Logs

---

## Status: ✅ GEFIXT

- [x] Crash Bug gefixt
- [x] Exception Handling hinzugefügt
- [x] HTML Files installiert
- [x] Getestet in regtest

**Dashboard ist jetzt stabil und funktioniert!** 🎉

