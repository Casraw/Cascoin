# Cascoin Documentation

Welcome to Cascoin! This directory contains all user and developer documentation.

---

## 📚 Quick Links

### **For Users**
- **[HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md)** ⭐ - Start here! Complete guide to the trust system
- **[Quantum Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md)** - Post-quantum cryptography guide
- **[Documentation Index](CASCOIN_DOCS.md)** - All documentation organized by topic

### **For Developers**
- **[Build Instructions](build/build-linux.md)** - How to build from source
- **[Developer Notes](developer/developer-notes.md)** - Development guidelines
- **[CVM Developer Guide](cvm/CVM_DEVELOPER_GUIDE.md)** - Smart contract development

---

## 🚀 Getting Started

### Running Cascoin

```bash
# GUI
./bin/cascoin-qt

# Daemon (headless)
./bin/cascoind -daemon

# Command line interface
./bin/cascoin-cli help
```

### Check Your Trust Score

```bash
# Get your HAT v2 trust score
./bin/cascoin-cli getsecuretrust "YourAddress"

# Get detailed breakdown
./bin/cascoin-cli gettrustbreakdown "YourAddress"
```

### View Dashboard

Open in browser: `http://localhost:8332/dashboard`

---

## 📁 Documentation Structure

```
doc/
├── README.md                    # This file
├── CASCOIN_DOCS.md              # Documentation index
├── INSTALL.md                   # Installation guide
│
├── build/                       # 🔧 Build Instructions
│   ├── build-linux.md           # Linux build guide
│   ├── build-osx.md             # macOS build guide
│   ├── build-windows.md         # Windows build guide
│   ├── build-unix.md            # Unix/BSD build guide
│   ├── build-netbsd.md          # NetBSD build guide
│   ├── build-openbsd.md         # OpenBSD build guide
│   ├── dependencies.md          # Required dependencies
│   ├── gitian-building.md       # Reproducible builds
│   └── ...
│
├── cvm/                         # 💻 Cascoin Virtual Machine
│   ├── CVM_DEVELOPER_GUIDE.md   # Smart contract development
│   ├── CVM_USER_GUIDE.md        # User guide for CVM
│   ├── CVM_OPERATOR_GUIDE.md    # Node operator guide
│   ├── CVM_SECURITY_GUIDE.md    # Security best practices
│   ├── CVM_SECURITY_AUDIT_CHECKLIST.md
│   ├── CVM_BLOCKCHAIN_INTEGRATION.md
│   └── CVM_EVM_DEPLOYMENT_PLAN.md
│
├── quantum/                     # 🔐 Post-Quantum Cryptography
│   └── QUANTUM_MIGRATION_GUIDE.md  # FALCON-512 migration guide
│
├── l2/                          # ⚡ Layer 2 Solutions
│   ├── L2_DEVELOPER_GUIDE.md    # L2 development guide
│   └── L2_OPERATOR_GUIDE.md     # L2 operator guide
│
├── wot/                         # 🤝 Web-of-Trust & HAT v2
│   ├── HAT_V2_USER_GUIDE.md     # HAT v2 user guide ⭐
│   ├── HAT_V2.md                # Technical implementation
│   ├── TRUST_SECURITY_ANALYSIS.md
│   ├── WOT_SECURITY_ANALYSIS.md
│   ├── WOT_SCAM_PROTECTION.md
│   └── WALLET_CLUSTERING.md
│
├── developer/                   # 👨‍💻 Developer Resources
│   ├── developer-notes.md       # Development guidelines
│   ├── release-process.md       # Release procedures
│   ├── benchmarking.md          # Performance testing
│   ├── fuzzing.md               # Fuzz testing
│   ├── bips.md                  # BIP implementations
│   └── ...
│
├── release-notes/               # 📋 Release Notes
│   └── release-notes-*.md       # Version-specific notes
│
└── man/                         # 📖 Man Pages
    └── ...
```

---

## 📖 Documentation by Topic

### 🔧 Building from Source
- [Linux](build/build-linux.md)
- [macOS](build/build-osx.md)
- [Windows](build/build-windows.md)
- [Unix/BSD](build/build-unix.md)
- [Dependencies](build/dependencies.md)

### 💻 CVM (Smart Contracts)
- [Developer Guide](cvm/CVM_DEVELOPER_GUIDE.md)
- [User Guide](cvm/CVM_USER_GUIDE.md)
- [Operator Guide](cvm/CVM_OPERATOR_GUIDE.md)
- [Security Guide](cvm/CVM_SECURITY_GUIDE.md)

### 🔐 Post-Quantum Cryptography
- [Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md) - FALCON-512 addresses & Public Key Registry

### ⚡ Layer 2
- [Developer Guide](l2/L2_DEVELOPER_GUIDE.md)
- [Operator Guide](l2/L2_OPERATOR_GUIDE.md)

### 🤝 Web-of-Trust & HAT v2
- [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md) ⭐
- [HAT v2 Technical](wot/HAT_V2.md)
- [Security Analysis](wot/TRUST_SECURITY_ANALYSIS.md)
- [Scam Protection](wot/WOT_SCAM_PROTECTION.md)

### 👨‍💻 Developer Resources
- [Developer Notes](developer/developer-notes.md)
- [Release Process](developer/release-process.md)
- [Benchmarking](developer/benchmarking.md)

### 🌐 Network & APIs
- [REST Interface](REST-interface.md)
- [ZMQ Notifications](zmq.md)
- [Tor Support](tor.md)
- [DNS Seed Policy](dnsseed-policy.md)

---

## 🎯 Key Features

### HAT v2 Trust System
- **99%+ manipulation-resistant**
- Multi-layer security (4 components)
- Personalized Web-of-Trust
- Real-time dashboard
- Complete RPC API

### Post-Quantum Security
- **FALCON-512** signatures (NIST standardized)
- Public Key Registry for ~55% transaction size reduction
- Quantum-safe addresses (casq1...)

### CVM Smart Contracts
- 40+ opcodes
- Gas-metered execution
- LevelDB storage
- EVM compatibility layer

---

## 💡 Getting Help

### Documentation
1. Check [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md) for trust system questions
2. See [CASCOIN_DOCS.md](CASCOIN_DOCS.md) for topic index
3. Read platform-specific build guides in `build/`

### Community
- GitHub Issues for bug reports
- Community forums for discussions

---

## 🔄 Contributing

See [CONTRIBUTING.md](../CONTRIBUTING.md) in the root directory for:
- How to contribute
- Code style guidelines
- Documentation standards

---

## 📜 License

Distributed under the [MIT software license](/COPYING).

---

**For detailed information, start with [CASCOIN_DOCS.md](CASCOIN_DOCS.md)!**
