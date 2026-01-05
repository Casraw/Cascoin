Cascoin Core – Build Instructions (Linux)
==========================================

This guide describes two methods for building Cascoin Core on Linux:
1. **System Libraries** (recommended for development) - Uses Qt6 and libraries from your distribution
2. **Depends System** - Reproducible builds with all dependencies included (for releases/cross-compilation)

## Method 1: System Libraries (Recommended for Development)

Uses libraries from your distribution's package manager. Faster builds and simpler setup.

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

### Build Steps (System Libraries)

```bash
cd /path/to/Cascoin
./autogen.sh

./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
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

## Method 2: Depends System (For Releases/Cross-Compilation)

The depends system builds all required libraries from source, ensuring reproducible builds and statically linked binaries. Best for release builds and cross-compilation.

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake libtool pkg-config \
  curl ca-certificates cmake git \
  bsdmainutils gperf \
  libxcb1-dev libxcb-util-dev libxcb-cursor-dev libxcb-render-util0-dev \
  libxcb-keysyms1-dev libxcb-image0-dev libxcb-icccm4-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev
```

Note: 
- `gperf` is required for building fontconfig 2.15.0+ in the depends system.
- The `libxcb-*` and `libxkbcommon-*` packages are required for Qt6 XCB platform support.

### Build Steps (Daemon Only - Simplest)

```bash
# Step 1: Build dependencies (this takes a while the first time)
cd depends
make HOST=x86_64-pc-linux-gnu -j$(nproc)
cd ..

# Step 2: Generate build files
./autogen.sh

# Step 3: Configure with depends (daemon only)
CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench \
  --with-incompatible-bdb \
  --with-gui=no

# Step 4: Build
make -j$(nproc)
```

### Build with GUI (Qt6) - No System Qt6 Installed

If you do NOT have system Qt6 development packages installed, the depends Qt6 will be used automatically:

```bash
cd depends
make HOST=x86_64-pc-linux-gnu -j$(nproc)
cd ..
./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench \
  --with-incompatible-bdb
make -j$(nproc)
```

### Build with GUI (Qt6) - System Qt6 IS Installed

If you have system Qt6 development packages installed (e.g., `qt6-base-dev`), you must explicitly override the Qt6 paths to use depends Qt6:

```bash
cd depends
make HOST=x86_64-pc-linux-gnu -j$(nproc)
cd ..
./autogen.sh

DEPENDS=$PWD/depends/x86_64-pc-linux-gnu
CONFIG_SITE=$DEPENDS/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench \
  --with-incompatible-bdb \
  QT6_CFLAGS="-I$DEPENDS/include/QtCore -I$DEPENDS/include -I$DEPENDS/include/QtGui -I$DEPENDS/include/QtWidgets -I$DEPENDS/include/QtNetwork -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_NETWORK_LIB" \
  QT_INCLUDES="-I$DEPENDS/include/QtCore -I$DEPENDS/include -I$DEPENDS/include/QtGui -I$DEPENDS/include/QtWidgets -I$DEPENDS/include/QtNetwork -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_NETWORK_LIB"

make -j$(nproc)
```

### Advantages of Depends System

- **Reproducible builds**: Same source = same binary
- **Static linking**: Most libraries are built into the executable
- **No system dependency conflicts**: Works on any Linux distribution
- **Controlled versions**: All library versions are pinned

---

## EVMC and evmone Installation (for EVM Compatibility)

EVMC and evmone must be compiled manually for EVM compatibility features:

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

### Build with EVMC (System Libraries)

After installing EVMC/evmone, configure with EVMC support:

```bash
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

### System Qt6 conflicts with depends Qt6

When using the depends system and you have system Qt6 development packages installed (e.g., `qt6-base-dev`), the build may pick up system Qt6 headers instead of depends Qt6 headers. This causes C++20 compatibility errors because system Qt6 (6.9.x) uses C++20 features while Cascoin builds with C++17.

**Solutions (choose one):**

1. **Use system Qt6 directly** (recommended for development):
   Don't use the depends system for Qt6. Instead, follow "Method 1: System Libraries" above.

2. **Build daemon only** (no GUI):
   ```bash
   CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site ./configure \
     --prefix=/ --disable-tests --disable-bench --with-incompatible-bdb --with-gui=no
   ```

3. **Remove system Qt6 dev packages temporarily**:
   ```bash
   sudo apt-get remove qt6-base-dev qt6-base-dev-tools
   # Then build with depends system normally
   ```

4. **Override Qt6 paths explicitly** (see "Build with GUI - System Qt6 IS Installed" section above)

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
rm -rf depends/x86_64-pc-linux-gnu
rm -rf depends/built/x86_64-pc-linux-gnu
```

## Depends System Documentation

For more details on the depends system, see [depends/README.md](../depends/README.md).
