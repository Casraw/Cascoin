# Bitcoin Core HTTP Server - Architektur Erklärung

## 🏗️ Wie es wirklich funktioniert

### In Bitcoin Core gibt es EINEN HTTP Server:

```
HTTP Server (Port 8332/18332/18443/45789)
├── Handler für /                    → RPC Calls (JSON-RPC)
├── Handler für /rest/*              → REST API
└── Handler für /dashboard/*         → CVM Dashboard (NEU)
```

**Alle teilen sich den gleichen HTTP Server und Port!**

---

## ⚠️ Das Problem

Du hast gefragt: **"Der HTTP Server soll doch nicht auf dem RPC port laufen"**

**Antwort**: In Bitcoin Core **IST** der HTTP Server und RPC Server das gleiche!

### Bitcoin Core Design:
```cpp
// Es gibt EINEN libevent HTTP Server:
InitHTTPServer();        // Startet HTTP auf Port X

// Verschiedene Handler registrieren sich:
RegisterHTTPHandler("/", HTTPReq_JSONRPC);     // RPC
RegisterHTTPHandler("/rest", rest_handler);    // REST API
RegisterHTTPHandler("/dashboard", CVMDashboard); // Dashboard
```

**Alles läuft auf dem GLEICHEN Port!**

---

## 🤔 Ist das ein Problem?

### NEIN, weil:

1. **Sicherheit bleibt gleich**:
   - Nur localhost (127.0.0.1)
   - RPC Authentication weiterhin aktiv
   - Dashboard ist Read-Only

2. **Standard Bitcoin Core Pattern**:
   - REST API macht das auch so (`/rest/*`)
   - ZMQ Notifications auch
   - Ist das normale Design

3. **Keine Konflikte**:
   - Jeder Handler hat eigenen Pfad
   - `/` = RPC
   - `/rest/` = REST
   - `/dashboard/` = Dashboard

---

## 🔧 ABER: Wenn du einen separaten Port willst...

### Option A: Separater HTTP Server (Mehr Aufwand)

```cpp
// Zweiter HTTP Server auf anderem Port
struct MHD_Daemon *dashboard_daemon = 
    MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        8766,  // ANDERER Port!
        ...
    );
```

**Vorteile**:
- ✅ Komplett getrennt von RPC
- ✅ Eigener Port (z.B. 8766)
- ✅ Kann unabhängig aktiviert/deaktiviert werden

**Nachteile**:
- ⚠️ Neue Dependency (libmicrohttpd)
- ⚠️ Mehr Code zu maintainen
- ⚠️ Zweiter Server-Prozess
- ⚠️ Nicht das Bitcoin Core Pattern

---

### Option B: Current Implementation (Empfohlen)

```cpp
// Nutzt bestehenden HTTP Server
RegisterHTTPHandler("/dashboard", CVMDashboard);
```

**Vorteile**:
- ✅ Keine neue Dependency
- ✅ Minimal Code
- ✅ Bitcoin Core Standard Pattern
- ✅ Teilt Security Infrastructure

**Nachteile**:
- ⚠️ Teilt sich Port mit RPC
- ⚠️ Wenn RPC aus ist, ist Dashboard auch aus

---

## 💡 Meine Empfehlung

### Für Production: Option B (Current)

**Warum?**
1. **Standard Pattern**: REST API macht genau das gleiche
2. **Sicherheit**: Gleiche Security wie RPC (localhost-only)
3. **Einfachheit**: Minimal Code, keine neue Dependency
4. **Wartbarkeit**: Folgt Bitcoin Core Conventions

### Für Future: Option A (Falls gewünscht)

Wenn die Community wirklich einen **separaten Port** will, können wir das später hinzufügen:

```ini
# cascoin.conf
cvmdashboard=1
cvmdashboardport=8766  # Separater Port
```

Aber das ist **NICHT kritisch** für Phase 4!

---

## 🎯 Zusammenfassung

**Aktuelle Implementation**:
- ✅ Dashboard nutzt bestehenden HTTP Server
- ✅ Läuft auf gleichem Port wie RPC
- ✅ Separater Handler Pfad (`/dashboard/`)
- ✅ Gleiche Security wie RPC
- ✅ Standard Bitcoin Core Pattern

**Ist das OK?**

Ja! Weil:
1. REST API macht das genauso
2. Localhost-only bleibt gleich
3. RPC Auth bleibt aktiv
4. Ist das normale Design

**Brauchst du wirklich einen separaten Port?**

Dann müssen wir:
1. libmicrohttpd hinzufügen (neue Dependency)
2. Zweiten HTTP Server implementieren
3. Port-Konflikte managen
4. Mehr Code maintainen

**Ich empfehle**: Bleib bei der aktuellen Lösung (wie REST API)!

---

## 📊 Vergleich mit REST API

Bitcoin Core REST API:
```
Port: 8332 (gleich wie RPC!)
Pfad: /rest/*
Security: Gleich wie RPC
```

CVM Dashboard:
```
Port: 8332 (gleich wie RPC!)
Pfad: /dashboard/*
Security: Gleich wie RPC
```

**Exakt das gleiche Pattern!** ✅

---

**Was denkst du? Sollen wir bei der aktuellen Lösung bleiben oder einen separaten Port implementieren?**

