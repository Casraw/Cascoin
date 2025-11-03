// Lightweight Trust Graph Visualization (No D3.js needed!)
// Pure JavaScript + SVG

class TrustGraphVisualization {
    constructor(containerId, width = 800, height = 400) {
        this.container = document.getElementById(containerId);
        this.width = width;
        this.height = height;
        this.nodes = [];
        this.links = [];
        this.svg = null;
        this.simulation = null;
    }
    
    // Initialize SVG canvas
    init() {
        this.container.innerHTML = '';
        this.svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
        this.svg.setAttribute("width", this.width);
        this.svg.setAttribute("height", this.height);
        this.svg.style.background = '#f8fafc';
        this.svg.style.borderRadius = '8px';
        this.container.appendChild(this.svg);
    }
    
    // Set graph data
    setData(nodes, links) {
        this.nodes = nodes.map((n, i) => ({
            ...n,
            x: Math.random() * this.width,
            y: Math.random() * this.height,
            vx: 0,
            vy: 0,
            index: i
        }));
        this.links = links.map(l => ({
            ...l,
            source: typeof l.source === 'object' ? l.source : this.nodes.find(n => n.id === l.source),
            target: typeof l.target === 'object' ? l.target : this.nodes.find(n => n.id === l.target)
        }));
    }
    
    // Render the graph
    render() {
        if (!this.svg) this.init();
        
        // Clear SVG
        while (this.svg.firstChild) {
            this.svg.removeChild(this.svg.firstChild);
        }
        
        // Create groups for links and nodes
        const linksGroup = document.createElementNS("http://www.w3.org/2000/svg", "g");
        const nodesGroup = document.createElementNS("http://www.w3.org/2000/svg", "g");
        
        // Draw links
        this.links.forEach(link => {
            if (!link.source || !link.target) return;
            
            const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
            line.setAttribute("x1", link.source.x);
            line.setAttribute("y1", link.source.y);
            line.setAttribute("x2", link.target.x);
            line.setAttribute("y2", link.target.y);
            line.setAttribute("stroke", "#cbd5e0");
            line.setAttribute("stroke-width", Math.sqrt(link.weight / 10) || 2);
            line.setAttribute("stroke-opacity", "0.6");
            linksGroup.appendChild(line);
        });
        
        // Draw nodes
        this.nodes.forEach((node, i) => {
            const group = document.createElementNS("http://www.w3.org/2000/svg", "g");
            group.setAttribute("class", "node-group");
            group.style.cursor = "pointer";
            
            // Circle
            const circle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
            circle.setAttribute("cx", node.x);
            circle.setAttribute("cy", node.y);
            circle.setAttribute("r", 20);
            circle.setAttribute("fill", this.getReputationColor(node.reputation || 50));
            circle.setAttribute("stroke", "#ffffff");
            circle.setAttribute("stroke-width", "2");
            
            // Label
            const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
            text.setAttribute("x", node.x);
            text.setAttribute("y", node.y + 35);
            text.setAttribute("text-anchor", "middle");
            text.setAttribute("font-size", "10");
            text.setAttribute("fill", "#1e293b");
            text.textContent = node.label || node.id.substring(0, 8) + '...';
            
            // Hover effects
            group.onmouseenter = () => {
                circle.setAttribute("r", 25);
                circle.setAttribute("stroke", "#2563eb");
                circle.setAttribute("stroke-width", "3");
                this.showTooltip(node, event);
            };
            group.onmouseleave = () => {
                circle.setAttribute("r", 20);
                circle.setAttribute("stroke", "#ffffff");
                circle.setAttribute("stroke-width", "2");
                this.hideTooltip();
            };
            
            // Drag support
            let isDragging = false;
            let dragStartX, dragStartY;
            
            group.onmousedown = (e) => {
                isDragging = true;
                dragStartX = e.clientX - node.x;
                dragStartY = e.clientY - node.y;
                e.preventDefault();
            };
            
            document.addEventListener('mousemove', (e) => {
                if (isDragging) {
                    node.x = e.clientX - dragStartX;
                    node.y = e.clientY - dragStartY;
                    this.render();
                }
            });
            
            document.addEventListener('mouseup', () => {
                isDragging = false;
            });
            
            group.appendChild(circle);
            group.appendChild(text);
            nodesGroup.appendChild(group);
        });
        
        this.svg.appendChild(linksGroup);
        this.svg.appendChild(nodesGroup);
    }
    
    // Force-directed layout simulation (simple)
    startSimulation(iterations = 100) {
        for (let i = 0; i < iterations; i++) {
            // Apply forces
            this.applyForces();
            
            // Update positions
            this.nodes.forEach(node => {
                node.x += node.vx;
                node.y += node.vy;
                
                // Damping
                node.vx *= 0.8;
                node.vy *= 0.8;
                
                // Bounds
                node.x = Math.max(30, Math.min(this.width - 30, node.x));
                node.y = Math.max(30, Math.min(this.height - 30, node.y));
            });
            
            // Render every 10 iterations
            if (i % 10 === 0) {
                setTimeout(() => this.render(), i * 10);
            }
        }
        
        // Final render
        setTimeout(() => this.render(), iterations * 10);
    }
    
    applyForces() {
        const centerX = this.width / 2;
        const centerY = this.height / 2;
        
        // Center force
        this.nodes.forEach(node => {
            node.vx += (centerX - node.x) * 0.01;
            node.vy += (centerY - node.y) * 0.01;
        });
        
        // Repulsion between nodes
        for (let i = 0; i < this.nodes.length; i++) {
            for (let j = i + 1; j < this.nodes.length; j++) {
                const a = this.nodes[i];
                const b = this.nodes[j];
                const dx = b.x - a.x;
                const dy = b.y - a.y;
                const dist = Math.sqrt(dx * dx + dy * dy) || 1;
                
                if (dist < 100) {
                    const force = (100 - dist) * 0.5;
                    const fx = (dx / dist) * force;
                    const fy = (dy / dist) * force;
                    
                    a.vx -= fx;
                    a.vy -= fy;
                    b.vx += fx;
                    b.vy += fy;
                }
            }
        }
        
        // Attraction for connected nodes
        this.links.forEach(link => {
            if (!link.source || !link.target) return;
            
            const dx = link.target.x - link.source.x;
            const dy = link.target.y - link.source.y;
            const dist = Math.sqrt(dx * dx + dy * dy) || 1;
            const force = (dist - 100) * 0.1;
            
            const fx = (dx / dist) * force;
            const fy = (dy / dist) * force;
            
            link.source.vx += fx;
            link.source.vy += fy;
            link.target.vx -= fx;
            link.target.vy -= fy;
        });
    }
    
    getReputationColor(reputation) {
        // Red (0) -> Yellow (50) -> Green (100)
        if (reputation < 50) {
            // Red to Yellow
            const r = 239;
            const g = Math.round(68 + (159 - 68) * (reputation / 50));
            const b = 68;
            return `rgb(${r}, ${g}, ${b})`;
        } else {
            // Yellow to Green
            const r = Math.round(245 - (245 - 16) * ((reputation - 50) / 50));
            const g = Math.round(158 + (185 - 158) * ((reputation - 50) / 50));
            const b = Math.round(11 + (129 - 11) * ((reputation - 50) / 50));
            return `rgb(${r}, ${g}, ${b})`;
        }
    }
    
    showTooltip(node, event) {
        // Create tooltip if doesn't exist
        let tooltip = document.getElementById('graph-tooltip');
        if (!tooltip) {
            tooltip = document.createElement('div');
            tooltip.id = 'graph-tooltip';
            tooltip.style.position = 'absolute';
            tooltip.style.background = 'rgba(0, 0, 0, 0.9)';
            tooltip.style.color = 'white';
            tooltip.style.padding = '10px';
            tooltip.style.borderRadius = '4px';
            tooltip.style.fontSize = '12px';
            tooltip.style.pointerEvents = 'none';
            tooltip.style.zIndex = '1000';
            document.body.appendChild(tooltip);
        }
        
        tooltip.innerHTML = `
            <strong>${node.id}</strong><br>
            Reputation: ${node.reputation || 50}<br>
            Connections: ${this.links.filter(l => 
                l.source.id === node.id || l.target.id === node.id
            ).length}
        `;
        tooltip.style.display = 'block';
        tooltip.style.opacity = '1';
        tooltip.style.left = (event.pageX + 10) + 'px';
        tooltip.style.top = (event.pageY - 10) + 'px';
    }
    
    hideTooltip() {
        const tooltip = document.getElementById('graph-tooltip');
        if (tooltip) {
            tooltip.style.opacity = '0';
            setTimeout(() => tooltip.style.display = 'none', 200);
        }
    }
}

// Example usage:
/*
const graph = new TrustGraphVisualization('trustGraph', 800, 400);
graph.setData(
    [
        { id: 'QcPLC...', reputation: 75, label: 'Alice' },
        { id: 'QXabc...', reputation: 60, label: 'Bob' },
        { id: 'QYdef...', reputation: 85, label: 'Carol' }
    ],
    [
        { source: 'QcPLC...', target: 'QXabc...', weight: 80 },
        { source: 'QXabc...', target: 'QYdef...', weight: 60 }
    ]
);
graph.render();
graph.startSimulation(100);
*/

