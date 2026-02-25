# Cascoin Core Technology Stack

## Build System

**Primary Build System**: GNU Autotools (autoconf/automake)
- `./autogen.sh` - Generate configure script
- `./configure` - Configure build with options
- `make` - Compile the project
- `make check` - Run unit tests

**Dependencies Management**: 
- System libraries (recommended for Linux)
- `depends/` system for cross-compilation and static builds

## Core Technologies

**Language**: C++17 (required for Qt6 compatibility)
**Database**: LevelDB for blockchain and contract storage
**Networking**: Boost.Asio for P2P networking
**Cryptography**: OpenSSL, secp256k1 for signatures
**GUI Framework**: Qt6 (Qt5 deprecated)
**Serialization**: Custom Bitcoin-style serialization + Protocol Buffers

## Key Libraries

- **Boost**: System, filesystem, program options, thread, chrono (minimum 1.47.0)
- **Berkeley DB 4.8**: Wallet storage (use `--with-incompatible-bdb` for newer versions)
- **libevent**: HTTP server and networking
- **ZMQ**: Optional message queue notifications
- **miniupnpc**: Optional UPnP support
- **qrencode**: Optional QR code support in GUI

## Cascoin-Specific Components

**CVM (Cascoin Virtual Machine)**:
- Location: `src/cvm/`
- Register-based VM with stack operations
- 40+ opcodes across 8 categories
- Gas-metered execution (max 1M gas/tx, 10M gas/block)
- LevelDB integration for contract storage
- Soft-fork compatible via OP_RETURN transactions
- Max contract size: 24KB
- Max stack size: 1024 elements

**Web-of-Trust System**:
- Trust graph with bonded relationships
- Personalized reputation calculation
- Path-finding algorithms (max depth 3-10)
- DAO dispute resolution
- Economic security via bond-and-slash
- HAT v2 (Hybrid Adaptive Trust) scoring

**RPC Interface**:
- 26 new RPC commands across 5 categories
- CVM: contract deployment and execution
- Reputation: voting and scoring
- WoT: trust relationships and weighted reputation
- HAT: behavior analysis and secure trust
- DAO: dispute creation and voting

## Mining Algorithms

**SHA256d (Bitcoin-compatible)**:
- Standard Bitcoin proof-of-work algorithm
- Compatible with existing Bitcoin mining hardware (ASICs)
- Uses double SHA256 hashing
- Activated post-fork with nVersion >= 0x20000000

**MinotaurX (YesPower-based)**:
- CPU-friendly mining algorithm based on YesPower
- Uses Minotaur hashing function with YesPower backend
- Designed to be ASIC-resistant
- Activated via version bits in block header

**Hive Mining (Labyrinth System)**:
- Unique "bee" creation and management system (Note: Future versions may use "mice and cheese" terminology)
- Miners create virtual "bees" by burning CAS coins
- Bees have lifecycle: gestation (48 blocks), maturation, lifespan (14 days)
- Bees can mine blocks without continuous hashing power
- Uses special nonce marker (192) to identify Hive blocks
- Target: 1 Hive block every 2-3 regular PoW blocks

**Difficulty Adjustment**:
- **LWMA-3 Algorithm**: Modified Zawy LWMA for post-MinotaurX blocks
- **Dark Gravity Wave**: For SHA256d blocks pre-MinotaurX
- **Separate targets**: Each algorithm maintains independent difficulty
- **Dynamic adjustment**: Responds quickly to hashrate changes

## Common Build Commands

```bash
# Full build with GUI and wallet
./autogen.sh
./configure --with-gui=qt6 --enable-wallet --with-qrencode --enable-zmq
make -j$(nproc)

# Daemon only
./configure --with-gui=no --enable-wallet
make -j$(nproc)

# Development build with debug symbols
./configure --enable-debug --with-gui=qt6
make -j$(nproc)

# Run tests
make check
test/functional/test_runner.py

# Run specific unit test suite (faster than make check)
src/test/test_cascoin --run_test=quantum_tests
src/test/test_cascoin --run_test=crypto_tests

# Clean build
make clean
```

## Mining Configuration

**Hive Mining Parameters**:
- `sethiveparams` - Configure Hive mining optimization
- `gethiveparams` - Get current Hive parameters
- `-hivecheckdelay` - Time between Hive checks (default: 1ms)
- `-hivecheckthreads` - Threads for bee checking (default: -2)
- `-hiveearlyout` - Early abort on new blocks (default: true)

**Algorithm Selection**:
- `-powalgo` - Choose mining algorithm (sha256d, minotaurx)
- `getnetworkhashps` - Get network hashrate by algorithm
- Algorithm auto-detection based on block version bits

## Platform-Specific Notes

**Linux**: Use system packages for dependencies
**macOS**: Use Homebrew for dependencies, requires Xcode
**Windows**: Cross-compile from Linux using depends system
**Cross-compilation**: Use `depends/` system for reproducible builds

## Development Tools

- **clang-format**: Code formatting (config in `src/.clang-format`)
- **ccache**: Build acceleration (auto-detected)
- **lcov**: Code coverage analysis
- **valgrind**: Memory debugging (suppression file: `contrib/valgrind.supp`)

## Configuration Options

Key configure flags:
- `--enable-wallet` / `--disable-wallet`: Wallet functionality
- `--with-gui=qt6|no`: GUI support
- `--enable-debug`: Debug build
- `--enable-tests` / `--disable-tests`: Unit tests
- `--enable-zmq`: ZMQ notifications
- `--with-qrencode`: QR code support
- `--enable-reduce-exports`: Reduce binary size

## CVM Virtual Machine Capabilities

**Instruction Set (40+ Opcodes)**:
- **Stack Operations**: PUSH, POP, DUP, SWAP
- **Arithmetic**: ADD, SUB, MUL, DIV, MOD
- **Logical**: AND, OR, XOR, NOT
- **Comparison**: EQ, NE, LT, GT, LE, GE
- **Control Flow**: JUMP, JUMPI, CALL, RETURN, STOP
- **Storage**: SLOAD, SSTORE (persistent contract storage)
- **Cryptographic**: SHA256, VERIFY_SIG, PUBKEY
- **Context**: ADDRESS, BALANCE, CALLER, CALLVALUE, TIMESTAMP, BLOCKHASH, BLOCKHEIGHT, GAS

**Gas Costs**:
- Base operations: 1-10 gas
- Storage read: 200 gas
- Storage write: 5,000 gas
- Cryptographic operations: 60-3,000 gas
- Contract calls: 700 gas

**Limits**:
- Max contract size: 24KB
- Max stack size: 1,024 elements
- Max gas per transaction: 1,000,000
- Max gas per block: 10,000,000

## RPC Commands (26 Total)

**CVM Category (4 commands)**:
- `deploycontract` - Prepare contract deployment
- `callcontract` - Prepare contract call
- `getcontractinfo` - Query contract metadata
- `sendcvmcontract` - Broadcast contract deployment

**Reputation Category (4 commands)**:
- `getreputation` - Get reputation score for address
- `votereputation` - Cast reputation vote (off-chain)
- `sendcvmvote` - Broadcast reputation vote transaction
- `listreputations` - List addresses with reputation scores

**Web-of-Trust Category (6 commands)**:
- `addtrust` - Add trust relationship (off-chain)
- `getweightedreputation` - Get personalized reputation via trust paths
- `gettrustgraphstats` - Get WoT graph statistics
- `listtrustrelations` - List all trust relationships
- `sendtrustrelation` - Broadcast trust relationship transaction
- `sendbondedvote` - Broadcast bonded reputation vote

**HAT v2 Category (4 commands)**:
- `getbehaviormetrics` - Get behavior analysis for address
- `getgraphmetrics` - Get graph analysis metrics
- `getsecuretrust` - Get secure HAT v2 trust score
- `gettrustbreakdown` - Get detailed trust calculation breakdown

**Wallet Clustering Category (3 commands)**:
- `buildwalletclusters` - Analyze blockchain for wallet clusters
- `getwalletcluster` - Get addresses in same wallet cluster
- `geteffectivetrust` - Get trust score considering clustering

**DAO Category (4 commands)**:
- `createdispute` - Challenge a bonded vote
- `listdisputes` - List DAO disputes
- `getdispute` - Get dispute details
- `votedispute` - Vote on dispute as DAO member

**Additional Commands**:
- `detectclusters` - Detect suspicious clusters in trust graph


## Testing

**Unit Test Binary**: `src/test/test_cascoin` (NOT test_bitcoin)
- Run all tests: `src/test/test_cascoin`
- Run specific suite: `src/test/test_cascoin --run_test=quantum_tests`
- Run with verbose output: `src/test/test_cascoin --log_level=all`

**Functional Tests**: `test/functional/test_runner.py`
- Run all: `test/functional/test_runner.py`
- Run specific: `test/functional/test_runner.py feature_cvm.py`

**IMPORTANT**: The test binary is named `test_cascoin`, not `test_bitcoin`. This is a common mistake due to the Bitcoin codebase heritage.
