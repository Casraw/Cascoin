# Cascoin Documentation

Welcome to Cascoin! This directory contains all user and developer documentation.

---

## 📚 Quick Links

### **For Users**
- **[HAT v2 User Guide](HAT_V2_USER_GUIDE.md)** ⭐ - Start here! Complete guide to the trust system
- **[Documentation Index](CASCOIN_DOCS.md)** - All documentation organized by topic

### **For Developers**
- **[Build Instructions](build-linux.md)** - How to build from source
- **[Developer Notes](developer-notes.md)** - Development guidelines
- **[HAT v2 Technical Docs](HAT_V2.md)** - Implementation details

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

## 📖 Main Documentation

### User Guides
- **[HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md)** - Complete trust system guide
- **[CASCOIN_DOCS.md](CASCOIN_DOCS.md)** - Documentation index

### Security
- **[TRUST_SECURITY_ANALYSIS.md](TRUST_SECURITY_ANALYSIS.md)** - Security analysis
- **[WOT_SECURITY_ANALYSIS.md](WOT_SECURITY_ANALYSIS.md)** - Web-of-Trust security
- **[WOT_SCAM_PROTECTION.md](WOT_SCAM_PROTECTION.md)** - Scam protection

### Technical
- **[HAT_V2.md](HAT_V2.md)** - HAT v2 implementation details
- **[developer-notes.md](developer-notes.md)** - Development guidelines
- **[REST-interface.md](REST-interface.md)** - REST API documentation

---

## 🔧 Building from Source

### Dependencies
See [dependencies.md](dependencies.md) for required libraries.

### Platform-Specific Guides
- [Linux](build-linux.md)
- [Unix/BSD](build-unix.md)
- [macOS](build-osx.md)
- [Windows](build-windows.md)

---

## 💡 Getting Help

### Documentation
1. Check [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md) for trust system questions
2. See [CASCOIN_DOCS.md](CASCOIN_DOCS.md) for topic index
3. Read platform-specific build guides

### Community
- GitHub Issues for bug reports
- Community forums for discussions

---

## 🎯 Key Features

### HAT v2 Trust System
- **99%+ manipulation-resistant**
- Multi-layer security (4 components)
- Personalized Web-of-Trust
- Real-time dashboard
- Complete RPC API

### Documentation
- Comprehensive user guides
- Technical implementation docs
- Security analysis
- API references

---

## 📝 Documentation Structure

```
doc/
├── README.md                # This file
├── CASCOIN_DOCS.md          # Documentation index
├── HAT_V2_USER_GUIDE.md     # User guide ⭐
├── HAT_V2.md                # Technical docs
├── TRUST_SECURITY_ANALYSIS.md
├── WOT_SECURITY_ANALYSIS.md
├── WOT_SCAM_PROTECTION.md
├── build-*.md               # Build guides
├── developer-notes.md
└── REST-interface.md
```

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
