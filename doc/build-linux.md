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
  libdb++-dev libsqlite3-dev
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

Notes:
- If your distribution only provides newer Berkeley DB versions, configure with `--with-incompatible-bdb` (wallets will then not be portable to official releases).
- Qt6 tools (`moc`, `uic`, `rcc`, `lrelease`) are typically located in `/usr/lib/qt6/libexec` or `/usr/bin`.

Build Steps
-----------

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

Troubleshooting
---------------

- Qt6 tools not found (moc/uic/rcc/lrelease): Make sure the packages `qt6-base-dev-tools` and `qt6-tools-dev-tools` are installed, and specify their paths as shown above in `./configure`.
- Berkeley DB error message: Use `--with-incompatible-bdb` (wallets will then not be portable) or build BDB 4.8 separately and link against it.
- Missing libraries: Install the corresponding `-dev` packages (see list above) and run `./configure` again.


