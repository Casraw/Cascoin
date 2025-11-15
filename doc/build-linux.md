Cascoin Core – Build-Anleitung (Linux)
======================================

Diese Anleitung beschreibt den Build von Cascoin Core unter Linux mit Wallet und Qt6‑GUI – ausschließlich mit Systembibliotheken (ohne das `depends/`‑System).

Voraussetzungen (Ubuntu/Debian)
--------------------------------

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential autoconf automake libtool pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  libprotobuf-dev protobuf-compiler \
  libevent-dev libboost-all-dev \
  libminiupnpc-dev libssl-dev libzmq3-dev libqrencode-dev \
  libdb++-dev \
  cmake git
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

Hinweise:
- Falls Ihre Distribution nur neuere Berkeley DB‑Versionen bereitstellt, konfigurieren Sie mit `--with-incompatible-bdb` (Wallets sind dann nicht portabel zu offiziellen Releases).
- Qt6‑Tools (`moc`, `uic`, `rcc`, `lrelease`) liegen typischerweise in `/usr/lib/qt6/libexec` bzw. `/usr/bin`.

Build‑Schritte
--------------

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

Erzeugte Binaries
-----------------

- GUI‑Wallet: `src/qt/cascoin-qt`
- Daemon: `src/cascoind`
- CLI: `src/cascoin-cli`
- TX‑Tool: `src/cascoin-tx`

Optionale Features
------------------

- Ohne GUI: `./configure --with-gui=no`
- Tests/Bench deaktivieren: `--disable-tests --disable-bench`
- QR‑Codes in der GUI: `libqrencode-dev` installieren und `--with-qrencode` setzen
- ZMQ: `libzmq3-dev` installieren und `--enable-zmq` setzen
- EVMC (EVM-Kompatibilität): EVMC 12.1.0 und evmone 0.18.0 manuell kompilieren (siehe Anleitung oben) und `--enable-evmc` setzen
- EVMC deaktivieren: `--disable-evmc` (falls EVM-Features nicht benötigt werden)

Troubleshooting
---------------

- Qt6‑Tools nicht gefunden (moc/uic/rcc/lrelease): Stellen Sie sicher, dass die Pakete `qt6-base-dev-tools` und `qt6-tools-dev-tools` installiert sind, und geben Sie deren Pfade wie oben bei `./configure` an.
- Berkeley DB‑Fehlermeldung: Nutzen Sie `--with-incompatible-bdb` (Wallets dann nicht portabel) oder bauen Sie BDB 4.8 separat und linken dagegen.
- EVMC‑Libraries nicht gefunden: Kompilieren Sie EVMC und evmone manuell (siehe Anleitung oben) oder nutzen Sie `--disable-evmc` um EVM-Features zu deaktivieren.
- Fehlende Libraries: Installieren Sie die entsprechenden `-dev`‑Pakete (siehe obige Liste) und führen Sie `./configure` erneut aus.


