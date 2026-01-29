# Cascoin Documentation Index

Welcome to the Cascoin documentation! This directory contains comprehensive guides for users and developers.

---

## 📚 User Documentation

### [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md) ⭐ **START HERE**
Complete guide for using the HAT v2 (Hybrid Adaptive Trust) reputation system:
- How to check your trust score
- Understanding the 4 components
- RPC commands reference
- Dashboard features
- Security information
- FAQ and troubleshooting

**For:** End users who want to understand and use the trust system

### [CVM User Guide](cvm/CVM_USER_GUIDE.md) ⭐ **NEW**
Complete user guide for Cascoin CVM:
- All essential commands
- Becoming a validator
- Using smart contracts
- Web-of-Trust & Reputation
- Gas & Fees
- DAO & Disputes

**For:** All Cascoin users

### [Quantum Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md) 🔐 **NEW**
Post-quantum cryptography guide:
- FALCON-512 quantum-safe addresses
- Public Key Registry for optimized transactions
- Migration from legacy addresses
- RPC commands reference

**For:** Users wanting quantum-safe security

---

## 🔒 Security Documentation

### [HAT v2 Technical Documentation](wot/HAT_V2.md)
Complete technical implementation details:
- Architecture overview
- ~1,700 lines of code
- All defense mechanisms
- Security analysis (99%+ protection)
- Testing procedures

**For:** Developers and security auditors

### [Trust Security Analysis](wot/TRUST_SECURITY_ANALYSIS.md)
Deep dive into security:
- Attack vector analysis
- Defense mechanisms
- Mathematical proofs
- Manipulation resistance

**For:** Security researchers

### [WoT Security Analysis](wot/WOT_SECURITY_ANALYSIS.md)
Web-of-Trust specific security:
- Sybil attack resistance
- Cluster detection
- Graph analysis

**For:** Network security analysis

### [WoT Scam Protection](wot/WOT_SCAM_PROTECTION.md)
Practical scam prevention:
- Common attack patterns
- Protection strategies
- Real-world examples

**For:** Security-conscious users

---

## 🔧 Development Documentation

### Build Instructions
- [build-linux.md](build/build-linux.md) - Linux build guide
- [build-unix.md](build/build-unix.md) - Unix/BSD systems
- [build-osx.md](build/build-osx.md) - macOS
- [build-windows.md](build/build-windows.md) - Windows
- [build-netbsd.md](build/build-netbsd.md) - NetBSD
- [build-openbsd.md](build/build-openbsd.md) - OpenBSD

### Developer Resources
- [developer-notes.md](developer/developer-notes.md) - Development guidelines
- [dependencies.md](build/dependencies.md) - Required dependencies
- [release-process.md](developer/release-process.md) - Release procedures
- [benchmarking.md](developer/benchmarking.md) - Performance testing
- [fuzzing.md](developer/fuzzing.md) - Fuzz testing

### API Documentation
- [REST-interface.md](REST-interface.md) - REST API
- [zmq.md](zmq.md) - ZeroMQ notifications

---

## 🔗 CVM/Smart Contract Documentation

### [CVM Developer Guide](cvm/CVM_DEVELOPER_GUIDE.md) ⭐ **NEW**
Complete guide for deploying smart contracts on Cascoin's CVM:
- Quick start tutorial
- Contract deployment and calling
- Trust-aware features
- Gas system and discounts
- RPC reference
- Code examples

**For:** Smart contract developers

### [CVM Blockchain Integration](cvm/CVM_BLOCKCHAIN_INTEGRATION.md)
Technical documentation for blockchain integration:
- Transaction format (OP_RETURN structure)
- Soft fork compatibility
- Mempool integration
- Block validation
- Receipt storage

**For:** Protocol developers and integrators

### [CVM Operator Guide](cvm/CVM_OPERATOR_GUIDE.md)
Node operator documentation:
- System requirements
- Configuration options
- Soft fork activation
- Monitoring and maintenance
- Troubleshooting

**For:** Node operators and validators

### [CVM Security Guide](cvm/CVM_SECURITY_GUIDE.md)
Security documentation:
- HAT v2 consensus security model
- Validator operations
- Attack vectors and mitigations
- Security best practices
- Incident response

**For:** Security auditors and validator operators

---

## ⚡ Layer 2 Documentation

### [L2 Developer Guide](l2/L2_DEVELOPER_GUIDE.md)
Layer 2 development documentation:
- Architecture overview
- Bridge contracts
- State management
- Integration guide

**For:** L2 developers

### [L2 Operator Guide](l2/L2_OPERATOR_GUIDE.md)
Layer 2 operator documentation:
- Node setup
- Configuration
- Monitoring
- Troubleshooting

**For:** L2 node operators

---

## 🔐 Post-Quantum Cryptography

### [Quantum Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md)
Complete guide for quantum-safe transactions:
- FALCON-512 signature algorithm
- New address formats (casq1...)
- Public Key Registry (~55% size reduction)
- Migration process
- RPC commands

**For:** All users wanting future-proof security

---

## 🚀 Quick Start

### For Users:
1. Read [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md)
2. Start daemon: `cascoind -daemon`
3. Check score: `cascoin-cli getsecuretrust "YourAddress"`
4. Open dashboard: http://localhost:8332/dashboard

### For Developers:
1. Build: See [build-linux.md](build/build-linux.md)
2. Test: `./test_hat_v2.sh`
3. Read: [HAT v2 Technical](wot/HAT_V2.md) for technical details

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
- [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md) - User guide
- [HAT v2 Technical](wot/HAT_V2.md) - Technical docs
- [Trust Security Analysis](wot/TRUST_SECURITY_ANALYSIS.md) - Security analysis

### Web-of-Trust
- [WoT Security Analysis](wot/WOT_SECURITY_ANALYSIS.md) - WoT security
- [WoT Scam Protection](wot/WOT_SCAM_PROTECTION.md) - Scam protection
- [Wallet Clustering](wot/WALLET_CLUSTERING.md) - Cluster detection

### Post-Quantum
- [Quantum Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md) - FALCON-512 & Registry

### Network & Protocol
- [REST-interface.md](REST-interface.md) - REST API
- [zmq.md](zmq.md) - ZMQ notifications
- [tor.md](tor.md) - Tor support

### Building & Testing
- [Build Guides](build/) - Platform-specific builds
- [Fuzzing](developer/fuzzing.md) - Fuzzing tests
- [Benchmarking](developer/benchmarking.md) - Performance

---

## 💡 Need Help?

### Common Questions
1. **How to check my trust score?**
   → See [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md#check-your-trust-score)

2. **Why is my score low?**
   → See [HAT v2 User Guide](wot/HAT_V2_USER_GUIDE.md#why-is-my-score-so-low)

3. **How to build from source?**
   → See [build-linux.md](build/build-linux.md)

4. **How to use quantum addresses?**
   → See [Quantum Migration Guide](quantum/QUANTUM_MIGRATION_GUIDE.md)

5. **Security concerns?**
   → See [Trust Security Analysis](wot/TRUST_SECURITY_ANALYSIS.md)

### Getting Support
- Read the relevant documentation first
- Check existing issues on GitHub
- Ask in community channels

---

## 📝 Documentation Status

| Document | Location | Status | Last Updated |
|----------|----------|--------|--------------|
| HAT v2 User Guide | wot/ | ✅ Complete | Nov 2025 |
| HAT v2 Technical | wot/ | ✅ Complete | Nov 2025 |
| Trust Security Analysis | wot/ | ✅ Complete | Nov 2025 |
| WoT Security Analysis | wot/ | ✅ Complete | Nov 2025 |
| WoT Scam Protection | wot/ | ✅ Complete | Nov 2025 |
| CVM User Guide | cvm/ | ✅ Complete | Dec 2025 |
| CVM Developer Guide | cvm/ | ✅ Complete | Dec 2025 |
| CVM Blockchain Integration | cvm/ | ✅ Complete | Dec 2025 |
| CVM Operator Guide | cvm/ | ✅ Complete | Dec 2025 |
| CVM Security Guide | cvm/ | ✅ Complete | Dec 2025 |
| Quantum Migration Guide | quantum/ | ✅ Complete | Jan 2026 |
| L2 Developer Guide | l2/ | ✅ Complete | Jan 2026 |
| L2 Operator Guide | l2/ | ✅ Complete | Jan 2026 |

---

## 🔄 Contributing to Documentation

See [../CONTRIBUTING.md](../CONTRIBUTING.md) for:
- Documentation standards
- How to submit improvements
- Style guidelines

---

**Last Updated:** January 2026  
**Version:** 3.0  
**Status:** Production Ready ✅
