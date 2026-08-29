Cascoin Core – Build Instructions (macOS)
==========================================

This guide describes building Cascoin Core on macOS (Apple Silicon / ARM64) with Wallet and Qt6 GUI using Homebrew.

Prerequisites
--------------

Install [Homebrew](https://brew.sh/) if not already present:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/opt/homebrew/bin/brew shellenv)"
```

Install the required dependencies:

```bash
brew update
brew install autoconf automake libtool pkg-config \
  boost berkeley-db@5 miniupnpc openssl@3 \
  qt@6 qrencode zeromq libevent protobuf python
```

Notes:
- Berkeley DB 5 is required for wallet compatibility. Homebrew provides `berkeley-db@5`.
- Qt6 is installed as `qt@6` (keg-only). The build steps below set the correct paths.

Build Steps
-----------

```bash
cd /path/to/Cascoin
./autogen.sh

BOOST_ROOT="$(brew --prefix boost)"
BDB_PREFIX="$(brew --prefix berkeley-db@5)"
QT_PREFIX="$(brew --prefix qt@6)"
QT_HOST_BINS="$QT_PREFIX/share/qt/libexec"

export CPPFLAGS="-I$BDB_PREFIX/include -I$BOOST_ROOT/include"
export LDFLAGS="-L$BDB_PREFIX/lib -L$BOOST_ROOT/lib"
export PKG_CONFIG_PATH="$QT_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
export PATH="$QT_PREFIX/bin:$PATH"

./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-incompatible-bdb \
  --with-boost="$BOOST_ROOT" \
  MOC="$QT_HOST_BINS/moc" \
  UIC="$QT_HOST_BINS/uic" \
  RCC="$QT_HOST_BINS/rcc"

make -j"$(sysctl -n hw.ncpu)"
```

Generated Binaries
------------------

- GUI Wallet: `src/qt/cascoin-qt`
- Daemon: `src/cascoind`
- CLI: `src/cascoin-cli`
- TX Tool: `src/cascoin-tx`

Optional Features
-----------------

- Without GUI: `./configure --with-gui=no`
- Disable tests/bench: `--disable-tests --disable-bench`
- QR codes in GUI: Already included via `qrencode`, set `--with-qrencode`
- ZMQ: Already included via `zeromq`, set `--enable-zmq`

Troubleshooting
---------------

- Qt6 tools not found (moc/uic/rcc): Verify `qt@6` is installed (`brew list qt@6`) and that `QT_HOST_BINS` points to the correct path. Run `ls "$(brew --prefix qt@6)/share/qt/libexec"` to check.
- Berkeley DB error: Use `--with-incompatible-bdb` (wallets will not be portable to official releases) or build BDB 4.8 separately.
- Boost system library warning: Can be safely ignored. Modern Boost versions use header-only `boost_system`.
- OpenSSL conflicts: Homebrew's `openssl@3` is keg-only. If configure can't find it, add `$(brew --prefix openssl@3)` to `CPPFLAGS` and `LDFLAGS`.
