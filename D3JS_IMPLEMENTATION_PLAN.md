# 📊 D3.js Trust Graph Visualization - Implementation Plan

## 🎯 Goal

**Interactive Trust Network Visualization mit D3.js**

Zeige das Web-of-Trust Netzwerk als interaktiven Graph:
- Nodes = Adressen
- Edges = Trust Relations
- Farbe = Reputation Score
- Größe = Trust Weight

---

## 🏗️ Architecture

### Option A: CDN (Einfach, aber externe Abhängigkeit)
```html
<script src="https://d3js.org/d3.v7.min.js"></script>
```

❌ **Problem:** Externe Abhängigkeit, kein Offline-Betrieb

---

### Option B: Embedded D3.js (Empfohlen!)
```cpp
// In cvmdashboard_html.h
const std::string D3_JS = R"JS(
    // D3.js v7 minified code here (~70KB)
)JS";
```

✅ **Vorteile:**
- Keine externe Abhängigkeit
- Offline-fähig
- Immer die richtige Version
- Konsistent mit embedded HTML

---

## 📐 Implementation Steps

### 1. D3.js Library einbetten
```cpp
// src/httpserver/cvmdashboard_html.h

namespace CVMDashboardHTML {

// D3.js v7 (minified, ~70KB)
static const std::string D3_JS_MIN = R"JS(
!function(t,n){"object"==typeof exports&&"undefined"!=typeof module...
// ... D3.js minified code ...
)JS";

// Main HTML with embedded D3
static const std::string INDEX_HTML = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <style>/* CSS */</style>
</head>
<body>
    <!-- HTML -->
    
    <script>
    )HTML" + D3_JS_MIN + R"HTML(
    </script>
    
    <script>
    // Dashboard JS
    </script>
</body>
</html>
)HTML";

}
```

---

### 2. Graph Container im HTML
```html
<div class="card large">
    <div class="card-header">
        <h2>🌐 Trust Network Graph</h2>
        <button class="btn-secondary" onclick="refreshGraph()">🔄 Refresh</button>
    </div>
    <div class="card-body">
        <div id="trustGraph" class="graph-container">
            <!-- D3.js will render SVG here -->
        </div>
    </div>
</div>
```

---

### 3. Graph Data Structure
```javascript
const graphData = {
    nodes: [
        { id: "QcPLCRajUcRXEBBpRyc8unJVABAB4ERAgd", reputation: 75, label: "QcPLC..." },
        { id: "QXabc123...", reputation: 60, label: "QXabc..." },
        // ...
    ],
    links: [
        { source: "QcPLCRajUcRXEBBpRyc8unJVABAB4ERAgd", target: "QXabc123...", weight: 80 },
        // ...
    ]
};
```

---

### 4. D3.js Force Layout
```javascript
async function renderTrustGraph() {
    // Load data from RPC
    const trustEdges = await loadTrustEdges();
    const graphData = buildGraphData(trustEdges);
    
    // D3 Setup
    const width = 800;
    const height = 400;
    
    const svg = d3.select("#trustGraph")
        .html("") // Clear
        .append("svg")
        .attr("width", width)
        .attr("height", height);
    
    // Force simulation
    const simulation = d3.forceSimulation(graphData.nodes)
        .force("link", d3.forceLink(graphData.links).id(d => d.id).distance(100))
        .force("charge", d3.forceManyBody().strength(-300))
        .force("center", d3.forceCenter(width / 2, height / 2))
        .force("collision", d3.forceCollide().radius(30));
    
    // Draw edges
    const link = svg.append("g")
        .selectAll("line")
        .data(graphData.links)
        .join("line")
        .attr("stroke", "#cbd5e0")
        .attr("stroke-width", d => Math.sqrt(d.weight / 10));
    
    // Draw nodes
    const node = svg.append("g")
        .selectAll("circle")
        .data(graphData.nodes)
        .join("circle")
        .attr("r", 20)
        .attr("fill", d => getReputationColor(d.reputation))
        .call(drag(simulation));
    
    // Add labels
    const label = svg.append("g")
        .selectAll("text")
        .data(graphData.nodes)
        .join("text")
        .text(d => d.label)
        .attr("font-size", 10)
        .attr("dx", 25)
        .attr("dy", 5);
    
    // Update positions on tick
    simulation.on("tick", () => {
        link
            .attr("x1", d => d.source.x)
            .attr("y1", d => d.source.y)
            .attr("x2", d => d.target.x)
            .attr("y2", d => d.target.y);
        
        node
            .attr("cx", d => d.x)
            .attr("cy", d => d.y);
        
        label
            .attr("x", d => d.x)
            .attr("y", d => d.y);
    });
}
```

---

### 5. RPC Data Loading
```javascript
async function loadTrustEdges() {
    try {
        // Get all trust edges from RPC
        const result = await rpcCall('gettrustgraphstats');
        
        // In future: Add RPC method to get actual edges
        // For now: Mock data
        return mockTrustEdges();
    } catch (error) {
        console.error('Failed to load trust edges:', error);
        return [];
    }
}

function buildGraphData(trustEdges) {
    const nodes = new Map();
    const links = [];
    
    for (const edge of trustEdges) {
        // Add nodes
        if (!nodes.has(edge.fromAddress)) {
            nodes.set(edge.fromAddress, {
                id: edge.fromAddress,
                label: edge.fromAddress.substring(0, 8) + "...",
                reputation: edge.fromReputation || 50
            });
        }
        if (!nodes.has(edge.toAddress)) {
            nodes.set(edge.toAddress, {
                id: edge.toAddress,
                label: edge.toAddress.substring(0, 8) + "...",
                reputation: edge.toReputation || 50
            });
        }
        
        // Add edge
        links.push({
            source: edge.fromAddress,
            target: edge.toAddress,
            weight: edge.trustWeight
        });
    }
    
    return {
        nodes: Array.from(nodes.values()),
        links: links
    };
}
```

---

### 6. Color by Reputation
```javascript
function getReputationColor(reputation) {
    // Red (0) -> Yellow (50) -> Green (100)
    if (reputation < 50) {
        // Red to Yellow
        const ratio = reputation / 50;
        return d3.interpolate("#ef4444", "#f59e0b")(ratio);
    } else {
        // Yellow to Green
        const ratio = (reputation - 50) / 50;
        return d3.interpolate("#f59e0b", "#10b981")(ratio);
    }
}
```

---

### 7. Interactivity

**Drag:**
```javascript
function drag(simulation) {
    function dragstarted(event) {
        if (!event.active) simulation.alphaTarget(0.3).restart();
        event.subject.fx = event.subject.x;
        event.subject.fy = event.subject.y;
    }
    
    function dragged(event) {
        event.subject.fx = event.x;
        event.subject.fy = event.y;
    }
    
    function dragended(event) {
        if (!event.active) simulation.alphaTarget(0);
        event.subject.fx = null;
        event.subject.fy = null;
    }
    
    return d3.drag()
        .on("start", dragstarted)
        .on("drag", dragged)
        .on("end", dragended);
}
```

**Hover:**
```javascript
node
    .on("mouseover", function(event, d) {
        d3.select(this)
            .transition()
            .duration(200)
            .attr("r", 25);
        
        // Show tooltip
        tooltip
            .style("opacity", 1)
            .html(`
                <strong>${d.id}</strong><br>
                Reputation: ${d.reputation}<br>
                Connections: ${d.connections}
            `)
            .style("left", (event.pageX + 10) + "px")
            .style("top", (event.pageY - 10) + "px");
    })
    .on("mouseout", function() {
        d3.select(this)
            .transition()
            .duration(200)
            .attr("r", 20);
        
        tooltip.style("opacity", 0);
    });
```

**Zoom:**
```javascript
const zoom = d3.zoom()
    .scaleExtent([0.5, 5])
    .on("zoom", (event) => {
        svg.selectAll("g").attr("transform", event.transform);
    });

svg.call(zoom);
```

---

## 📦 D3.js Size

**Options:**

1. **D3.js Full (v7):** ~250KB minified
2. **D3.js Custom Build:** ~70KB (only force, selection, transition)
3. **D3 Force Only:** ~30KB

**Empfehlung:** Custom Build (~70KB)
- Force Layout
- Selection
- Transition
- Drag
- Zoom

---

## 🎨 Styling

```css
.graph-container {
    min-height: 400px;
    background: var(--bg-color);
    border-radius: 8px;
    overflow: hidden;
}

.graph-container svg {
    width: 100%;
    height: 100%;
}

.node {
    stroke: #ffffff;
    stroke-width: 2px;
    cursor: pointer;
}

.node:hover {
    stroke: var(--primary-color);
    stroke-width: 3px;
}

.link {
    stroke: #cbd5e0;
    stroke-opacity: 0.6;
}

.tooltip {
    position: absolute;
    background: rgba(0, 0, 0, 0.9);
    color: white;
    padding: 10px;
    border-radius: 4px;
    font-size: 12px;
    pointer-events: none;
    opacity: 0;
    transition: opacity 0.2s;
}
```

---

## 🚀 Implementation Order

1. **Download D3.js minified**
   ```bash
   curl -o d3.min.js https://d3js.org/d3.v7.min.js
   ```

2. **Embed in cvmdashboard_html.h**
   - Copy D3.js code
   - Add as separate string constant
   - Include in INDEX_HTML

3. **Add Graph Container**
   - Update HTML
   - Add CSS for graph

4. **Implement Basic Graph**
   - Mock data first
   - Test with fixed nodes
   - Verify rendering

5. **Add Force Layout**
   - D3 force simulation
   - Link and node positioning
   - Test interactivity

6. **Add Styling**
   - Color by reputation
   - Size by trust weight
   - Smooth animations

7. **Add RPC Integration**
   - Load real data
   - Update on refresh
   - Error handling

8. **Polish & Test**
   - Performance optimization
   - Browser testing
   - Mobile responsive

---

## 🎯 Success Criteria

- ✅ D3.js embedded (no CDN)
- ✅ Graph renders correctly
- ✅ Nodes colored by reputation
- ✅ Edges sized by trust weight
- ✅ Drag & drop works
- ✅ Zoom & pan works
- ✅ Hover shows details
- ✅ Data loads from RPC
- ✅ Smooth animations
- ✅ Mobile responsive

---

## 📊 Timeline

**Total:** ~2 hours

- D3.js Embedding: 20 min
- Basic Graph: 30 min
- Force Layout: 30 min
- Styling & Colors: 20 min
- RPC Integration: 20 min
- Polish & Test: 20 min

---

**Let's build it!** 🚀

