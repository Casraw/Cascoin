Cascoin Core – Build Instructions (Linux)
==========================================

This guide describes two methods for building Cascoin Core on Linux:
1. **Depends System** (recommended) - Reproducible builds with all dependencies included
2. **System Libraries** - Uses libraries from your distribution

## Method 1: Depends System (Recommended)

The depends system builds all required libraries from source, ensuring reproducible builds and statically linked binaries.

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake libtool pkg-config \
  curl ca-certificates cmake git \
  bsdmainutils
```

### Build Steps

```bash
# Step 1: Build dependencies (this takes a while the first time)
cd depends
make JOBS=$(nproc)
cd ..

# Step 2: Generate build files
./autogen.sh

# Step 3: Configure with depends
CONFIG_SITE=$PWD/depends/x86_64-linux-gnu/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench

# Step 4: Build
make -j$(nproc)
```

### Build with GUI (Qt6)

The depends system can also build Qt6:

```bash
cd depends
make JOBS=$(nproc)
cd ..
./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-linux-gnu/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench
make -j$(nproc)
```

### Advantages of Depends System

- **Reproducible builds**: Same source = same binary
- **Static linking**: Most libraries are built into the executable
- **No system dependency conflicts**: Works on any Linux distribution
- **Controlled versions**: All library versions are pinned

---

## Method 2: System Libraries

Uses libraries from your distribution's package manager. Faster initial build but less portable.

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake libtool pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  libprotobuf-dev protobuf-compiler \
  libevent-dev libboost-all-dev \
  libminiupnpc-dev libssl-dev libzmq3-dev libqrencode-dev \
  libdb++-dev libsqlite3-dev \
  cmake git
```

For Debian 13 (Trixie), also install:
```bash
sudo apt-get install -y libdb5.3++-dev
```

### EVMC and evmone Installation (for EVM Compatibility)

EVMC and evmone must be compiled manually:

```bash
# Additional dependencies
sudo apt-get install -y cmake git ninja-build libgmp-dev

# Install EVMC 12.1.0
cd /tmp
rm -rf evmc evmone
git clone --recursive https://github.com/ethereum/evmc.git
cd evmc
git checkout v12.1.0
mkdir -p build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DEVMC_TESTING=OFF \
  -DEVMC_TOOLS=OFF \
  -DEVMC_EXAMPLES=OFF \
  -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
sudo make install

# Install evmone 0.18.0
cd /tmp
git clone --recursive https://github.com/ethereum/evmone.git
cd evmone
git checkout v0.18.0
mkdir -p build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DEVMONE_TESTING=OFF \
  -DEVMONE_FUZZING=OFF \
  -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
sudo make install

# Update library cache
sudo ldconfig
```

### Build Steps (System Libraries)

```bash
cd /path/to/Cascoin
./autogen.sh

export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"

./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --enable-evmc \
  --with-evmc=/usr/local \
  --with-evmone=/usr/local \
  --with-incompatible-bdb \
  MOC=/usr/lib/qt6/libexec/moc \
  UIC=/usr/lib/qt6/libexec/uic \
  RCC=/usr/lib/qt6/libexec/rcc \
  LRELEASE=$(command -v lrelease || command -v lrelease-qt6 || echo /usr/lib/qt6/libexec/lrelease) \
  LUPDATE=$(command -v lupdate || command -v lupdate-qt6 || echo /usr/lib/qt6/libexec/lupdate)

make -j$(nproc)
```

### Debian 13 (Trixie) Specific

Qt6 tools are in different locations on Debian 13:

```bash
./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --with-incompatible-bdb \
  MOC=/usr/lib/qt6/libexec/moc \
  UIC=/usr/lib/qt6/libexec/uic \
  RCC=/usr/lib/qt6/libexec/rcc \
  LRELEASE=/usr/lib/qt6/bin/lrelease \
  LUPDATE=/usr/lib/qt6/bin/lupdate

make -j$(nproc)
```

---

## Generated Binaries

- GUI Wallet: `src/qt/cascoin-qt`
- Daemon: `src/cascoind`
- CLI: `src/cascoin-cli`
- TX Tool: `src/cascoin-tx`

## Optional Features

| Feature | Configure Flag |
|---------|---------------|
| Without GUI | `--with-gui=no` |
| Disable tests | `--disable-tests` |
| Disable benchmarks | `--disable-bench` |
| QR codes in GUI | `--with-qrencode` |
| ZMQ notifications | `--enable-zmq` |
| EVMC (EVM compat) | `--enable-evmc` |
| Disable EVMC | `--disable-evmc` |

## Troubleshooting

### Qt6 tools not found
Ensure `qt6-base-dev-tools` and `qt6-tools-dev-tools` are installed, then specify paths in configure.

### Berkeley DB error
Use `--with-incompatible-bdb` (wallets won't be portable to official releases).

### EVMC libraries not found
Either compile EVMC/evmone manually (see above) or use `--disable-evmc`.

### Missing libraries
Install the corresponding `-dev` packages and re-run configure.

## Clean Build

```bash
make clean
# For depends system:
rm -rf depends/x86_64-linux-gnu
rm -rf depends/built/x86_64-linux-gnu
```

## Depends System Documentation

For more details on the depends system, see [depends/README.md](../depends/README.md).
