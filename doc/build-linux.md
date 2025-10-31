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
  libdb++-dev
```

Hinweise:
- Falls Ihre Distribution nur neuere Berkeley DB‑Versionen bereitstellt, konfigurieren Sie mit `--with-incompatible-bdb` (Wallets sind dann nicht portabel zu offiziellen Releases).
- Qt6‑Tools (`moc`, `uic`, `rcc`, `lrelease`) liegen typischerweise in `/usr/lib/qt6/libexec` bzw. `/usr/bin`.

Build‑Schritte
--------------

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
  LRELEASE=$(command -v lrelease || command -v lrelease-qt6 || echo /usr/lib/qt6/libexec/lrelease) \
  LUPDATE=$(command -v lupdate || command -v lupdate-qt6 || echo /usr/lib/qt6/libexec/lupdate)

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

Troubleshooting
---------------

- Qt6‑Tools nicht gefunden (moc/uic/rcc/lrelease): Stellen Sie sicher, dass die Pakete `qt6-base-dev-tools` und `qt6-tools-dev-tools` installiert sind, und geben Sie deren Pfade wie oben bei `./configure` an.
- Berkeley DB‑Fehlermeldung: Nutzen Sie `--with-incompatible-bdb` (Wallets dann nicht portabel) oder bauen Sie BDB 4.8 separat und linken dagegen.
- Fehlende Libraries: Installieren Sie die entsprechenden `-dev`‑Pakete (siehe obige Liste) und führen Sie `./configure` erneut aus.


