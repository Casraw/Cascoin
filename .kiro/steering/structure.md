# Cascoin Core Project Structure

## Root Directory Layout

**Build & Configuration**:
- `configure.ac`, `Makefile.am` - Autotools build configuration
- `autogen.sh` - Build script generator
- `COPYING` - MIT license
- `README.md` - Project overview and build instructions

**Documentation**:
- `doc/` - Comprehensive documentation (build guides, developer notes, release notes)
- `CASCOIN_WHITEPAPER.md` - Technical whitepaper
- `CONTRIBUTING.md` - Contribution guidelines
- `WEB_OF_TRUST.md` - Web-of-Trust documentation

## Source Code Organization (`src/`)

**Core Blockchain**:
- `validation.cpp/h` - Block and transaction validation
- `chain.cpp/h` - Blockchain data structures
- `chainparams.cpp/h` - Network parameters (mainnet/testnet/regtest)
- `consensus/` - Consensus rules and validation
- `primitives/` - Basic data structures (block, transaction)

**Networking & P2P**:
- `net.cpp/h` - P2P networking layer
- `net_processing.cpp/h` - Message processing
- `addrman.cpp/h` - Peer address management
- `protocol.cpp/h` - Network protocol definitions

**Storage & Database**:
- `txdb.cpp/h` - Transaction database (LevelDB)
- `dbwrapper.cpp/h` - Database abstraction layer
- `coins.cpp/h` - UTXO set management

**Cascoin-Specific Features**:
- `cvm/` - Cascoin Virtual Machine implementation
  - `cvm.cpp/h` - Main VM engine with 40+ opcodes
  - `cvmdb.cpp/h` - LevelDB contract storage interface
  - `opcodes.h` - VM instruction set definitions
  - `vmstate.h` - VM execution state management
  - `contract.h` - Contract data structures
  - `reputation.h` - Reputation system integration
  - `trustgraph.h` - Web-of-Trust graph algorithms
  - `behaviormetrics.h` - HAT v2 behavior analysis
  - `graphanalysis.h` - Trust graph analysis
  - `securehat.h` - Secure Hybrid Adaptive Trust
  - `walletcluster.h` - Wallet clustering algorithms
  - `txbuilder.h` - Transaction building utilities
  - `blockprocessor.h` - Block processing integration
- `pow.cpp/h` - Multi-algorithm proof-of-work system
  - SHA256d, MinotaurX, and Hive mining support
  - LWMA-3 difficulty adjustment
  - Hive bee lifecycle management
- `primitives/block.h` - Block header with PoW type detection
- `crypto/minotaurx/` - MinotaurX algorithm implementation
  - `yespower/` - YesPower hashing backend
  - `minotaur.h` - Minotaur hash function
- `rialto.cpp/h` - Rialto mining system
- `beenft.cpp/h` - Bee NFT functionality for Hive mining (Note: May be updated to "mice and cheese" system in future versions)

**Wallet**:
- `wallet/` - Wallet implementation (optional, `--enable-wallet`)
- `keystore.cpp/h` - Key management

**RPC Interface**:
- `rpc/` - JSON-RPC server implementation
- `httprpc.cpp/h` - HTTP RPC transport
- `httpserver/` - HTTP server with CVM dashboard

**GUI (Qt)**:
- `qt/` - Qt6 GUI implementation (optional, `--with-gui=qt6`)

**Utilities & Tools**:
- `bitcoin-cli.cpp` - Command-line RPC client (→ `cascoin-cli`)
- `bitcoin-tx.cpp` - Transaction manipulation tool (→ `cascoin-tx`)
- `bitcoind.cpp` - Daemon main entry point (→ `cascoind`)

**Cryptography & Utilities**:
- `crypto/` - Cryptographic primitives
- `secp256k1/` - ECDSA signature library (submodule)
- `hash.cpp/h` - Hash functions
- `key.cpp/h` - Private key operations
- `pubkey.cpp/h` - Public key operations

**Support Libraries**:
- `leveldb/` - LevelDB database (embedded)
- `univalue/` - JSON parsing library
- `support/` - Platform compatibility helpers
- `compat/` - Cross-platform compatibility

## Testing Structure

**Unit Tests**:
- `src/test/` - C++ unit tests
- Run with: `make check`

**Functional Tests**:
- `test/functional/` - Python integration tests
- Run with: `test/functional/test_runner.py`

**Utilities**:
- `test/util/` - Test utilities and data

## Build Artifacts

**Generated Binaries**:
- `src/cascoind` - Main daemon
- `src/cascoin-cli` - RPC client
- `src/cascoin-tx` - Transaction tool
- `src/qt/cascoin-qt` - GUI wallet

**Libraries**:
- `src/libbitcoin_*.a` - Static libraries for different components
- `src/libbitcoinconsensus.la` - Consensus library

## Configuration & Dependencies

**External Dependencies**:
- `depends/` - Cross-compilation dependency system
- `contrib/` - Build scripts, packaging, development tools

**Platform-Specific**:
- `share/` - Shared resources (icons, setup scripts)
- `build-aux/` - Autotools auxiliary files

## Naming Conventions

**Executables**: Use "cascoin" prefix (cascoind, cascoin-cli, cascoin-qt)
**Internal Code**: Often retains "bitcoin" naming for compatibility
**Files**: Snake_case for most files, PascalCase for classes
**Directories**: Lowercase with hyphens or underscores

## Key Integration Points

**CVM Integration**: 
- OP_RETURN parsing in validation.cpp for soft-fork compatibility
- Contract storage via LevelDB in separate `cvm/` database
- 26 RPC commands in rpc/cvm.cpp across 6 categories
- Gas metering and execution limits
- Contract address generation using deployer + nonce
- Event logging system for contract interactions

**Web-of-Trust**:
- Trust graph storage with bonded relationships
- Personalized reputation via path-finding algorithms
- Economic security through bond-and-slash mechanism
- DAO dispute resolution with stake-weighted voting
- HAT v2 scoring combining behavior, WoT, economic, and temporal factors
- Wallet clustering for Sybil resistance

**Soft Fork Activation**:
- Block height-based activation (mainnet: 220,000)
- Backward compatibility via OP_RETURN transactions
- Old nodes see valid but unspendable outputs
- New nodes parse and validate CVM/WoT rules
- Feature flags in chainparams for network coordination

**Database Schema**:
- Contract storage: `contract_<addr>` → Contract metadata
- Contract state: `contract_<addr>_storage_<key>` → Storage value
- Trust edges: `trust_<from>_<to>` → Trust relationship
- Reputation votes: `vote_<txhash>` → Bonded vote
- DAO disputes: `dispute_<id>` → Dispute record
- Generic storage: Custom keys for extensions