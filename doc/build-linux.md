Cascoin Core – Build Instructions (Linux)
==========================================

This guide describes building Cascoin Core on Linux with Wallet and Qt6 GUI – exclusively using system libraries (without the `depends/` system).

Prerequisites (Ubuntu/Debian)
------------------------------

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

For Debian 13 add this: 'libdb5.3++-dev'

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake libtool pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  libprotobuf-dev protobuf-compiler \
  libevent-dev libboost-all-dev \
  libminiupnpc-dev libssl-dev libzmq3-dev libqrencode-dev \
  libdb++-dev libsqlite3-dev libdb5.3++-dev
```

### EVMC und evmone Installation (für EVM-Kompatibilität)

Da EVMC und evmone nicht in den Standard-Repositories verfügbar sind, müssen sie manuell kompiliert werden:

```bash
# Zusätzliche Abhängigkeiten für EVMC/evmone
sudo apt-get install -y cmake git ninja-build libgmp-dev

# EVMC 12.1.0 installieren
cd /tmp
rm -rf evmc evmone  # Aufräumen falls vorhanden
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

# evmone 0.18.0 installieren
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

# Bibliotheken aktualisieren
sudo ldconfig

# Installation verifizieren
echo "EVMC-Installation prüfen..."
pkg-config --exists evmc && echo "✓ EVMC gefunden" || echo "✗ EVMC nicht gefunden"
pkg-config --modversion evmc || echo "EVMC-Version nicht verfügbar"
ls -la /usr/local/lib/libevmc* || echo "EVMC-Bibliotheken nicht gefunden"
ls -la /usr/local/lib/libevmone* || echo "evmone-Bibliotheken nicht gefunden"
```

Notes:
- If your distribution only provides newer Berkeley DB versions, configure with `--with-incompatible-bdb` (wallets will then not be portable to official releases).
- Qt6 tools (`moc`, `uic`, `rcc`, `lrelease`) are typically located in `/usr/lib/qt6/libexec` or `/usr/bin`.

Build Steps
-----------

### Ubuntu / Debian (general)

```bash
cd /pfad/zu/Cascoin
./autogen.sh

# PKG_CONFIG_PATH für EVMC setzen
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"

# Konfiguration mit EVMC-Unterstützung
PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH" \
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

# Konfiguration verifizieren
echo "EVMC-Konfiguration prüfen..."
grep -i "evmc" config.log | tail -5 || echo "Keine EVMC-Einträge in config.log"
grep "ENABLE_EVMC" config.h || echo "ENABLE_EVMC nicht in config.h gefunden"

make -j"$(nproc)"
```

### Debian 13 (Trixie)

On Debian 13, the Qt6 translation tools (`lrelease`, `lupdate`) are located in `/usr/lib/qt6/bin/` instead of `/usr/lib/qt6/libexec/`:

```bash
cd /pfad/zu/Cascoin
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
  LRELEASE=/usr/lib/qt6/bin/lrelease \
  LUPDATE=/usr/lib/qt6/bin/lupdate

make -j"$(nproc)"
```

Generated Binaries
-----------------

- GUI Wallet: `src/qt/cascoin-qt`
- Daemon: `src/cascoind`
- CLI: `src/cascoin-cli`
- TX Tool: `src/cascoin-tx`

Optional Features
-----------------

- Without GUI: `./configure --with-gui=no`
- Disable tests/bench: `--disable-tests --disable-bench`
- QR codes in GUI: Install `libqrencode-dev` and set `--with-qrencode`
- ZMQ: Install `libzmq3-dev` and set `--enable-zmq`
- EVMC (EVM compatibility): Compile EVMC 12.1.0 and evmone 0.18.0 manually (see instructions above) and set `--enable-evmc`
- Disable EVMC: `--disable-evmc` (if EVM features are not needed)

Troubleshooting
---------------

- Qt6 tools not found (moc/uic/rcc/lrelease): Make sure the packages `qt6-base-dev-tools` and `qt6-tools-dev-tools` are installed, and specify their paths as shown above in `./configure`.
- Berkeley DB error message: Use `--with-incompatible-bdb` (wallets will then not be portable) or build BDB 4.8 separately and link against it.
- EVMC libraries not found: Compile EVMC and evmone manually (see instructions above) or use `--disable-evmc` to disable EVM features.
- Missing libraries: Install the corresponding `-dev` packages (see list above) and run `./configure` again.
