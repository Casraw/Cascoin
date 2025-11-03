# Phase 4 - Quick Fixes

## Problem 1: Port Issue ✅

**Ursache**: `cascoin.conf` hat custom `rpcport=45789`

**Lösung**: Dashboard nutzt den gleichen Port wie RPC Server!

## Problem 2: HTML Files Installation ❌

**Aktuell**: HTML-Dateien müssen manuell nach `~/.cascoin/html/` kopiert werden

**Warum problematisch**:
- ❌ User muss manuell kopieren
- ❌ Updates müssen manuell eingespielt werden
- ❌ Nicht developer-friendly

**Bessere Lösungen**:

### Option A: Embedded HTML (Beste Lösung)
```cpp
// HTML direkt im C++ Code als const char*
const char* DASHBOARD_HTML = R"(
<!DOCTYPE html>
<html>...
)";
```
**Vorteile**: Keine Installation nötig, alles im Binary

### Option B: Share Directory
```cpp
// Suche in mehreren Pfaden:
1. ~/.cascoin/html/          (User override)
2. ./share/html/             (Development)
3. /usr/share/cascoin/html/  (System install)
```
**Vorteile**: Einfach zu updaten, standard Linux-Pfade

### Option C: Makefile Install Target
```makefile
install-data-local:
	$(INSTALL) -d $(datadir)/html
	$(INSTALL_DATA) share/html/*.html $(datadir)/html/
```
**Vorteile**: Standard `make install` installiert alles

---

## Ich empfehle: Option B (Multiple Search Paths)

Das ist der beste Kompromiss!

