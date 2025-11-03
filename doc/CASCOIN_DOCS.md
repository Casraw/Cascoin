# Cascoin Documentation Index

Welcome to the Cascoin documentation! This directory contains comprehensive guides for users and developers.

---

## 📚 User Documentation

### [HAT v2 User Guide](HAT_V2_USER_GUIDE.md) ⭐ **START HERE**
Complete guide for using the HAT v2 (Hybrid Adaptive Trust) reputation system:
- How to check your trust score
- Understanding the 4 components
- RPC commands reference
- Dashboard features
- Security information
- FAQ and troubleshooting

**For:** End users who want to understand and use the trust system

---

## 🔒 Security Documentation

### [HAT v2 Technical Documentation](HAT_V2.md)
Complete technical implementation details:
- Architecture overview
- ~1,700 lines of code
- All defense mechanisms
- Security analysis (99%+ protection)
- Testing procedures

**For:** Developers and security auditors

### [Trust Security Analysis](TRUST_SECURITY_ANALYSIS.md)
Deep dive into security:
- Attack vector analysis
- Defense mechanisms
- Mathematical proofs
- Manipulation resistance

**For:** Security researchers

### [WoT Security Analysis](WOT_SECURITY_ANALYSIS.md)
Web-of-Trust specific security:
- Sybil attack resistance
- Cluster detection
- Graph analysis

**For:** Network security analysis

### [WoT Scam Protection](WOT_SCAM_PROTECTION.md)
Practical scam prevention:
- Common attack patterns
- Protection strategies
- Real-world examples

**For:** Security-conscious users

---

## 🔧 Development Documentation

### Build Instructions
- [build-linux.md](build-linux.md) - Linux build guide
- [build-unix.md](build-unix.md) - Unix/BSD systems
- [build-osx.md](build-osx.md) - macOS
- [build-windows.md](build-windows.md) - Windows

### Developer Resources
- [developer-notes.md](developer-notes.md) - Development guidelines
- [dependencies.md](dependencies.md) - Required dependencies
- [release-process.md](release-process.md) - Release procedures

### API Documentation
- [REST-interface.md](REST-interface.md) - REST API
- [zmq.md](zmq.md) - ZeroMQ notifications

---

## 🚀 Quick Start

### For Users:
1. Read [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md)
2. Start daemon: `cascoind -daemon`
3. Check score: `cascoin-cli getsecuretrust "YourAddress"`
4. Open dashboard: http://localhost:8332/dashboard

### For Developers:
1. Build: See [build-linux.md](build-linux.md)
2. Test: `./test_hat_v2.sh`
3. Read: [HAT_V2.md](HAT_V2.md) for technical details

---

## 📖 Main Project Documentation

See the root directory for:
- **README.md** - Project overview
- **CASCOIN_WHITEPAPER.md** - Technical whitepaper
- **CONTRIBUTING.md** - How to contribute
- **INSTALL.md** - Installation guide

---

## 🎯 Documentation by Topic

### Trust & Reputation
- [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md) - User guide
- [HAT_V2.md](HAT_V2.md) - Technical docs
- [TRUST_SECURITY_ANALYSIS.md](TRUST_SECURITY_ANALYSIS.md) - Security analysis

### Web-of-Trust
- [WOT_SECURITY_ANALYSIS.md](WOT_SECURITY_ANALYSIS.md) - WoT security
- [WOT_SCAM_PROTECTION.md](WOT_SCAM_PROTECTION.md) - Scam protection

### Network & Protocol
- [REST-interface.md](REST-interface.md) - REST API
- [zmq.md](zmq.md) - ZMQ notifications
- [tor.md](tor.md) - Tor support

### Building & Testing
- [build-*.md](build-linux.md) - Build guides
- [fuzzing.md](fuzzing.md) - Fuzzing tests
- [benchmarking.md](benchmarking.md) - Performance

---

## 💡 Need Help?

### Common Questions
1. **How to check my trust score?**
   → See [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md#check-your-trust-score)

2. **Why is my score low?**
   → See [HAT_V2_USER_GUIDE.md](HAT_V2_USER_GUIDE.md#why-is-my-score-so-low)

3. **How to build from source?**
   → See [build-linux.md](build-linux.md)

4. **Security concerns?**
   → See [TRUST_SECURITY_ANALYSIS.md](TRUST_SECURITY_ANALYSIS.md)

### Getting Support
- Read the relevant documentation first
- Check existing issues on GitHub
- Ask in community channels

---

## 📝 Documentation Status

| Document | Status | Last Updated |
|----------|--------|--------------|
| HAT_V2_USER_GUIDE.md | ✅ Complete | Nov 2025 |
| HAT_V2.md | ✅ Complete | Nov 2025 |
| TRUST_SECURITY_ANALYSIS.md | ✅ Complete | Nov 2025 |
| WOT_SECURITY_ANALYSIS.md | ✅ Complete | Nov 2025 |
| WOT_SCAM_PROTECTION.md | ✅ Complete | Nov 2025 |

---

## 🔄 Contributing to Documentation

See [../CONTRIBUTING.md](../CONTRIBUTING.md) for:
- Documentation standards
- How to submit improvements
- Style guidelines

---

**Last Updated:** November 3, 2025  
**Version:** 2.0  
**Status:** Production Ready ✅

