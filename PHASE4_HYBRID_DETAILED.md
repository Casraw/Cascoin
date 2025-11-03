# Phase 4 - Hybrid Approach: Qt Wallet + Local Web Dashboard

**Konzept**: Qt Wallet für Core Funktionen, lokale Webseite für Visualisierung  
**Datum**: 3. November 2025

---

## 🎯 Konzept-Übersicht

### Die Idee:
```
Cascoin-Qt Wallet (Native)
├── Send/Receive/Transactions (Qt Native)
├── 🆕 Trust & Reputation Tab (Qt Native)
│   ├── Simple Forms & Lists
│   └── [📊 Open Dashboard] Button
│       └── Opens → http://localhost:8766
│
└── Local Web Server (Embedded)
    └── Serves: Beautiful Web Dashboard
        ├── Interactive Trust Graph (D3.js)
        ├── Reputation Analytics (Chart.js)
        ├── Vote History Timeline
        └── DAO Governance Panel
```

### User Flow:
1. User öffnet `cascoin-qt`
2. Navigiert zu "Trust & Reputation" Tab
3. Sieht einfache Liste seiner Trust Relations (Qt Native)
4. Klickt auf **"Open Interactive Dashboard"** Button
5. → Browser/QWebEngineView öffnet `http://localhost:8766`
6. → Zeigt schöne, interaktive Visualisierungen
7. → Alle Daten kommen vom lokalen RPC

**Alles bleibt lokal! Keine externen Verbindungen!** ✅

---

## 🏗️ Architektur

```
┌─────────────────────────────────────────────────────┐
│              Cascoin-Qt Wallet (C++)                │
│  ┌──────────────────────────────────────────────┐   │
│  │ Qt Native UI                                 │   │
│  │ - Send Trust Dialog                          │   │
│  │ - Send Vote Dialog                           │   │
│  │ - Trust List (Table)                         │   │
│  │ - [📊 Open Dashboard] Button ────────┐       │   │
│  └──────────────────────────────────────│───────┘   │
│                                          │           │
│  ┌──────────────────────────────────────▼───────┐   │
│  │ Embedded HTTP Server (C++ Microhttpd)       │   │
│  │ - Serves static files from /html/          │   │
│  │ - Port: 8766 (localhost only)               │   │
│  │ - CORS: disabled (same-origin)              │   │
│  └──────────────────────────────────────────────┘   │
│                                                     │
│  ┌──────────────────────────────────────────────┐   │
│  │ JSON-RPC Server (existing)                  │   │
│  │ - Port: 8332 (localhost only)               │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────┐
        │  Browser / QWebEngineView      │
        │  http://localhost:8766         │
        │  ┌─────────────────────────┐   │
        │  │ Web Dashboard (HTML/JS) │   │
        │  │ - D3.js Trust Graph     │   │
        │  │ - Chart.js Analytics    │   │
        │  │ - Real-time Updates     │   │
        │  └─────────────────────────┘   │
        │          │                     │
        │          ▼ JSON-RPC calls     │
        │    localhost:8332             │
        └───────────────────────────────┘
```

---

## 🔧 Technische Implementation

### **Option A: QWebEngineView (Embedded Browser)**

#### Im Qt Wallet eingebettet - kein externer Browser:

```cpp
// src/qt/cvmpage.cpp
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QPushButton>

class CVMPage : public QWidget {
public:
    CVMPage(QWidget *parent = nullptr) : QWidget(parent) {
        QVBoxLayout *layout = new QVBoxLayout(this);
        
        // Native Qt elements
        trustList = new QTableView();
        layout->addWidget(trustList);
        
        // Button to open dashboard
        QPushButton *dashboardBtn = new QPushButton("📊 Open Interactive Dashboard");
        connect(dashboardBtn, &QPushButton::clicked, this, &CVMPage::openDashboard);
        layout->addWidget(dashboardBtn);
    }
    
private slots:
    void openDashboard() {
        // Create embedded browser window
        QWebEngineView *webView = new QWebEngineView();
        webView->setWindowTitle("CVM Dashboard");
        webView->resize(1200, 800);
        
        // Load local dashboard
        webView->load(QUrl("http://localhost:8766/dashboard.html"));
        webView->show();
    }
    
private:
    QTableView *trustList;
};
```

**Vorteile:**
- ✅ Alles in einem Fenster
- ✅ Keine Browser-Fenster außerhalb
- ✅ Konsistentes Look & Feel
- ✅ Kann mit Qt Signals/Slots kommunizieren

**Nachteile:**
- ⚠️ QtWebEngine Dependency (~50MB)
- ⚠️ Mehr RAM Nutzung

---

### **Option B: System Browser (Firefox/Chrome)**

#### Qt öffnet den Standard-Browser:

```cpp
// src/qt/cvmpage.cpp
#include <QDesktopServices>
#include <QUrl>

void CVMPage::openDashboard() {
    // Open in system default browser
    QDesktopServices::openUrl(QUrl("http://localhost:8766/dashboard.html"));
}
```

**Vorteile:**
- ✅ Keine zusätzliche Qt Dependency
- ✅ User nutzt gewohnten Browser
- ✅ DevTools verfügbar (F12)
- ✅ Kleinerer Memory Footprint

**Nachteile:**
- ⚠️ Browser-Fenster außerhalb der Wallet
- ⚠️ User könnte versehentlich schließen

---

## 🌐 Embedded HTTP Server

### Implementation mit libmicrohttpd (C++):

```cpp
// src/httpserver/cvmwebserver.h
#ifndef CASCOIN_CVMWEBSERVER_H
#define CASCOIN_CVMWEBSERVER_H

#include <microhttpd.h>
#include <string>

class CVMWebServer {
public:
    CVMWebServer(int port = 8766);
    ~CVMWebServer();
    
    bool Start();
    void Stop();
    bool IsRunning() const { return running; }
    
private:
    static int HandleRequest(void *cls, struct MHD_Connection *connection,
                            const char *url, const char *method,
                            const char *version, const char *upload_data,
                            size_t *upload_data_size, void **con_cls);
    
    std::string GetMimeType(const std::string& path);
    std::string ReadFile(const std::string& path);
    
    struct MHD_Daemon *daemon;
    int port;
    bool running;
    std::string webRoot; // Path to HTML files
};

#endif
```

```cpp
// src/httpserver/cvmwebserver.cpp
#include <httpserver/cvmwebserver.h>
#include <fs.h>
#include <util.h>
#include <fstream>
#include <sstream>

CVMWebServer::CVMWebServer(int port) : daemon(nullptr), port(port), running(false) {
    // Set web root to share/html or ~/.cascoin/html
    webRoot = (GetDataDir() / "html").string();
}

bool CVMWebServer::Start() {
    if (running) return true;
    
    // Start HTTP server (localhost only!)
    daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        port,
        nullptr, nullptr,  // Accept policy (null = accept all)
        &HandleRequest, this,
        MHD_OPTION_END
    );
    
    if (!daemon) {
        LogPrintf("Failed to start CVM web server on port %d\n", port);
        return false;
    }
    
    running = true;
    LogPrintf("CVM Web Dashboard available at http://localhost:%d\n", port);
    return true;
}

void CVMWebServer::Stop() {
    if (daemon) {
        MHD_stop_daemon(daemon);
        daemon = nullptr;
    }
    running = false;
}

int CVMWebServer::HandleRequest(void *cls, struct MHD_Connection *connection,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
    CVMWebServer *server = static_cast<CVMWebServer*>(cls);
    
    // Only allow GET requests
    if (strcmp(method, "GET") != 0) {
        return MHD_NO;
    }
    
    // Map URL to file
    std::string path = std::string(url);
    if (path == "/" || path == "/dashboard" || path == "/dashboard.html") {
        path = "/dashboard.html";
    }
    
    // Read file
    std::string fullPath = server->webRoot + path;
    std::string content = server->ReadFile(fullPath);
    
    if (content.empty()) {
        // 404 Not Found
        const char *notFound = "<html><body><h1>404 Not Found</h1></body></html>";
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(notFound), (void*)notFound, MHD_RESPMEM_PERSISTENT);
        int ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    // Determine MIME type
    std::string mimeType = server->GetMimeType(path);
    
    // Create response
    struct MHD_Response *response = MHD_create_response_from_buffer(
        content.size(), (void*)content.c_str(), MHD_RESPMEM_MUST_COPY);
    
    MHD_add_response_header(response, "Content-Type", mimeType.c_str());
    
    // CORS headers (for localhost only)
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "http://localhost:8766");
    
    int ret = MHD_queue_response(connection, 200, response);
    MHD_destroy_response(response);
    
    return ret;
}

std::string CVMWebServer::ReadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string CVMWebServer::GetMimeType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".svg")) return "image/svg+xml";
    return "text/plain";
}

CVMWebServer::~CVMWebServer() {
    Stop();
}
```

### Integration in init.cpp:

```cpp
// src/init.cpp
#include <httpserver/cvmwebserver.h>

// Global instance
CVMWebServer* g_cvmWebServer = nullptr;

bool AppInitMain() {
    // ... existing init code ...
    
    // Start CVM Web Server (after RPC server)
    if (gArgs.GetBoolArg("-cvmwebserver", true)) {
        int port = gArgs.GetArg("-cvmwebport", 8766);
        g_cvmWebServer = new CVMWebServer(port);
        
        if (!g_cvmWebServer->Start()) {
            LogPrintf("Warning: Could not start CVM web server\n");
        }
    }
    
    return true;
}

void Shutdown() {
    // ... existing shutdown code ...
    
    if (g_cvmWebServer) {
        g_cvmWebServer->Stop();
        delete g_cvmWebServer;
        g_cvmWebServer = nullptr;
    }
}
```

---

## 📁 Web Dashboard Dateien

### Verzeichnisstruktur:

```
share/html/
├── dashboard.html          # Main dashboard page
├── css/
│   ├── bootstrap.min.css
│   └── dashboard.css
├── js/
│   ├── d3.v7.min.js       # Graph visualization
│   ├── chart.js           # Charts
│   └── dashboard.js       # Custom logic
└── assets/
    ├── logo.png
    └── icons/
```

### Dashboard HTML:

```html
<!-- share/html/dashboard.html -->
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cascoin CVM Dashboard</title>
    
    <!-- Bootstrap CSS -->
    <link href="css/bootstrap.min.css" rel="stylesheet">
    <link href="css/dashboard.css" rel="stylesheet">
    
    <!-- Libraries -->
    <script src="js/d3.v7.min.js"></script>
    <script src="js/chart.js"></script>
</head>
<body>
    <div class="container-fluid">
        <div class="row">
            <!-- Header -->
            <div class="col-12 bg-primary text-white p-3">
                <h1>🔗 Cascoin CVM Dashboard</h1>
                <p class="mb-0">Interactive Trust Network Visualization</p>
            </div>
        </div>
        
        <div class="row mt-3">
            <!-- Stats Cards -->
            <div class="col-md-3">
                <div class="card">
                    <div class="card-body">
                        <h5 class="card-title">My Reputation</h5>
                        <h2 id="myReputation" class="text-primary">--</h2>
                        <div class="progress">
                            <div id="repProgress" class="progress-bar" role="progressbar"></div>
                        </div>
                    </div>
                </div>
            </div>
            
            <div class="col-md-3">
                <div class="card">
                    <div class="card-body">
                        <h5 class="card-title">Trust Relations</h5>
                        <h2 id="trustCount" class="text-success">--</h2>
                        <p class="text-muted mb-0">Outgoing edges</p>
                    </div>
                </div>
            </div>
            
            <div class="col-md-3">
                <div class="card">
                    <div class="card-body">
                        <h5 class="card-title">Votes Submitted</h5>
                        <h2 id="voteCount" class="text-info">--</h2>
                        <p class="text-muted mb-0">Active bonds</p>
                    </div>
                </div>
            </div>
            
            <div class="col-md-3">
                <div class="card">
                    <div class="card-body">
                        <h5 class="card-title">Network Size</h5>
                        <h2 id="networkSize" class="text-warning">--</h2>
                        <p class="text-muted mb-0">Total addresses</p>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="row mt-3">
            <!-- Trust Graph Visualization -->
            <div class="col-md-8">
                <div class="card">
                    <div class="card-header">
                        <h5>🌐 Trust Network Graph</h5>
                    </div>
                    <div class="card-body">
                        <div id="trustGraph" style="height: 500px;"></div>
                    </div>
                </div>
            </div>
            
            <!-- Reputation Distribution -->
            <div class="col-md-4">
                <div class="card">
                    <div class="card-header">
                        <h5>📊 Reputation Distribution</h5>
                    </div>
                    <div class="card-body">
                        <canvas id="repChart" height="400"></canvas>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="row mt-3">
            <!-- Recent Activity -->
            <div class="col-md-6">
                <div class="card">
                    <div class="card-header">
                        <h5>📜 Recent Trust Activity</h5>
                    </div>
                    <div class="card-body">
                        <div id="recentActivity" class="list-group">
                            <!-- Dynamically filled -->
                        </div>
                    </div>
                </div>
            </div>
            
            <!-- Top Reputation -->
            <div class="col-md-6">
                <div class="card">
                    <div class="card-header">
                        <h5>🏆 Highest Reputation</h5>
                    </div>
                    <div class="card-body">
                        <div id="topReputation" class="list-group">
                            <!-- Dynamically filled -->
                        </div>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <!-- JavaScript -->
    <script src="js/dashboard.js"></script>
</body>
</html>
```

### Dashboard JavaScript:

```javascript
// share/html/js/dashboard.js

class CVMDashboard {
    constructor() {
        this.rpcUrl = 'http://localhost:8332';
        this.rpcUser = 'cascoinrpc';
        this.rpcPassword = localStorage.getItem('rpcPassword') || '';
        this.updateInterval = 5000; // 5 seconds
    }
    
    async init() {
        await this.loadStats();
        await this.loadTrustGraph();
        await this.loadCharts();
        
        // Auto-refresh
        setInterval(() => this.loadStats(), this.updateInterval);
    }
    
    async rpcCall(method, params = []) {
        const response = await fetch(this.rpcUrl, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': 'Basic ' + btoa(this.rpcUser + ':' + this.rpcPassword)
            },
            body: JSON.stringify({
                jsonrpc: '2.0',
                id: Date.now(),
                method: method,
                params: params
            })
        });
        
        const data = await response.json();
        if (data.error) {
            throw new Error(data.error.message);
        }
        return data.result;
    }
    
    async loadStats() {
        try {
            // Get trust graph stats
            const stats = await this.rpcCall('gettrustgraphstats');
            
            // Update UI
            document.getElementById('trustCount').textContent = stats.total_trust_edges / 2; // Divide by 2 (forward+reverse)
            document.getElementById('voteCount').textContent = stats.total_votes;
            document.getElementById('networkSize').textContent = 
                stats.total_trust_edges + stats.total_votes;
            
            // Get my reputation (if wallet has addresses)
            try {
                const myAddr = await this.rpcCall('getnewaddress');
                const rep = await this.rpcCall('getreputation', [myAddr]);
                
                document.getElementById('myReputation').textContent = 
                    rep.average_score.toFixed(0) + '/100';
                
                const progress = document.getElementById('repProgress');
                progress.style.width = rep.average_score + '%';
                progress.setAttribute('aria-valuenow', rep.average_score);
            } catch (e) {
                document.getElementById('myReputation').textContent = 'N/A';
            }
            
        } catch (error) {
            console.error('Failed to load stats:', error);
        }
    }
    
    async loadTrustGraph() {
        // D3.js Force-Directed Graph
        const width = document.getElementById('trustGraph').clientWidth;
        const height = 500;
        
        // Create SVG
        const svg = d3.select('#trustGraph')
            .append('svg')
            .attr('width', width)
            .attr('height', height);
        
        // TODO: Load actual trust graph data from RPC
        // For now, demo data
        const nodes = [
            { id: 'Me', group: 1 },
            { id: 'Alice', group: 2 },
            { id: 'Bob', group: 2 },
            { id: 'Carol', group: 3 }
        ];
        
        const links = [
            { source: 'Me', target: 'Alice', weight: 80 },
            { source: 'Me', target: 'Bob', weight: 60 },
            { source: 'Bob', target: 'Carol', weight: 70 }
        ];
        
        // D3 Force Simulation
        const simulation = d3.forceSimulation(nodes)
            .force('link', d3.forceLink(links).id(d => d.id))
            .force('charge', d3.forceManyBody().strength(-300))
            .force('center', d3.forceCenter(width / 2, height / 2));
        
        // Draw links
        const link = svg.append('g')
            .selectAll('line')
            .data(links)
            .enter().append('line')
            .attr('stroke', '#999')
            .attr('stroke-width', d => d.weight / 20);
        
        // Draw nodes
        const node = svg.append('g')
            .selectAll('circle')
            .data(nodes)
            .enter().append('circle')
            .attr('r', 15)
            .attr('fill', d => d.group === 1 ? '#0d6efd' : '#198754')
            .call(d3.drag()
                .on('start', dragstarted)
                .on('drag', dragged)
                .on('end', dragended));
        
        // Labels
        const label = svg.append('g')
            .selectAll('text')
            .data(nodes)
            .enter().append('text')
            .text(d => d.id)
            .attr('font-size', 12)
            .attr('dx', 20);
        
        simulation.on('tick', () => {
            link
                .attr('x1', d => d.source.x)
                .attr('y1', d => d.source.y)
                .attr('x2', d => d.target.x)
                .attr('y2', d => d.target.y);
            
            node
                .attr('cx', d => d.x)
                .attr('cy', d => d.y);
            
            label
                .attr('x', d => d.x)
                .attr('y', d => d.y);
        });
        
        function dragstarted(event, d) {
            if (!event.active) simulation.alphaTarget(0.3).restart();
            d.fx = d.x;
            d.fy = d.y;
        }
        
        function dragged(event, d) {
            d.fx = event.x;
            d.fy = event.y;
        }
        
        function dragended(event, d) {
            if (!event.active) simulation.alphaTarget(0);
            d.fx = null;
            d.fy = null;
        }
    }
    
    async loadCharts() {
        // Reputation Distribution Chart
        const ctx = document.getElementById('repChart').getContext('2d');
        
        new Chart(ctx, {
            type: 'doughnut',
            data: {
                labels: ['High (80-100)', 'Medium (50-79)', 'Low (0-49)'],
                datasets: [{
                    data: [30, 50, 20], // TODO: Real data from RPC
                    backgroundColor: ['#198754', '#ffc107', '#dc3545']
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false
            }
        });
    }
}

// Initialize dashboard on page load
document.addEventListener('DOMContentLoaded', () => {
    const dashboard = new CVMDashboard();
    dashboard.init();
});
```

---

## ⚙️ Configuration

### cascoin.conf additions:

```ini
# CVM Web Dashboard
cvmwebserver=1              # Enable web dashboard (default: 1)
cvmwebport=8766            # Web server port (default: 8766)
cvmwebbind=127.0.0.1       # Only bind to localhost (security!)
```

---

## 🔒 Security Considerations

### WICHTIG! Nur localhost binden:

```cpp
// In CVMWebServer::Start()
daemon = MHD_start_daemon(
    MHD_USE_SELECT_INTERNALLY,
    port,
    // Accept policy: ONLY localhost!
    [](void *cls, const struct sockaddr *addr, socklen_t addrlen) -> int {
        if (addr->sa_family == AF_INET) {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
            // Only accept 127.0.0.1
            return (addr_in->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) ? MHD_YES : MHD_NO;
        }
        return MHD_NO;
    },
    nullptr,
    &HandleRequest, this,
    MHD_OPTION_END
);
```

### RPC Authentication:
```javascript
// User muss RPC Password eingeben beim ersten Start
if (!localStorage.getItem('rpcPassword')) {
    const password = prompt('Enter RPC Password (from cascoin.conf):');
    localStorage.setItem('rpcPassword', password);
}
```

---

## 📊 Vorteile dieser Lösung

### ✅ Pros:
1. **Qt Native für Core** - Einfache Forms bleiben in Qt
2. **Web für Visualisierung** - Schöne Graphs mit D3.js
3. **Alles lokal** - Kein externer Server, keine Cloud
4. **Flexibel** - User kann Browser ODER QWebEngineView wählen
5. **Entwicklerfreundlich** - HTML/CSS/JS ist einfacher als Qt
6. **Erweiterbar** - Leicht neue Features hinzufügen
7. **Offline-fähig** - Funktioniert ohne Internet
8. **Sicher** - Nur localhost, keine externen Verbindungen

### ⚠️ Cons:
1. **Zusätzlicher HTTP Server** - Mehr Code zu maintainen
2. **libmicrohttpd Dependency** - Muss gebaut werden
3. **RPC Authentication** - User muss Password eingeben (oder .cookie file nutzen)
4. **Port Management** - Port 8766 muss frei sein

---

## 🚀 Development Roadmap

### Phase 4A: Core Integration (10-12h)
1. **Embedded HTTP Server** (4-5h)
   - libmicrohttpd integration
   - Basic file serving
   - Security (localhost only)

2. **Qt Integration** (3-4h)
   - "Open Dashboard" Button
   - QWebEngineView OR QDesktopServices
   - Configuration

3. **Basic HTML Dashboard** (3h)
   - Simple stats display
   - Bootstrap layout
   - RPC connection

### Phase 4B: Visualizations (8-10h)
1. **Trust Graph Visualization** (4-5h)
   - D3.js force-directed graph
   - Interactive nodes
   - Real data from RPC

2. **Charts & Analytics** (3-4h)
   - Reputation distribution
   - Vote history timeline
   - Network growth chart

3. **Polish** (1-2h)
   - Styling
   - Animations
   - Error handling

### Total: 18-22 Stunden

---

## 💡 Quick Start Example

### Minimale Implementation für Test:

1. **Add HTTP Server** (2h)
2. **Create dashboard.html** (1h)
3. **Qt Button "Open Dashboard"** (1h)
4. **Test!** (30min)

**Total: ~4.5 Stunden für working prototype!**

---

## 🤔 Entscheidungen

### Option A vs B (Browser):

**QWebEngineView (embedded):**
```cpp
QWebEngineView *view = new QWebEngineView();
view->load(QUrl("http://localhost:8766"));
view->show();
```
- ✅ Integriert
- ⚠️ +50MB dependency

**System Browser:**
```cpp
QDesktopServices::openUrl(QUrl("http://localhost:8766"));
```
- ✅ Keine dependency
- ⚠️ Externes Fenster

**Meine Empfehlung: System Browser für Start, später optional QWebEngineView!**

---

## 📦 Deliverables

Nach Completion:
- ✅ Embedded HTTP Server in cascoind
- ✅ HTML Dashboard mit D3.js Graph
- ✅ Qt Button zum Öffnen
- ✅ Lokale RPC Integration
- ✅ Responsive Design
- ✅ Real-time Updates (5s refresh)

---

**Das wäre der perfekte Hybrid-Approach! Was denkst du?** 🚀

Sollen wir so vorgehen? Ich kann als erstes den Embedded HTTP Server implementieren und einen simplen Dashboard-Prototype erstellen!

