macOS Build Instructions (Qt6 + Wallet)
=======================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the OS X command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

Dependencies
----------------------

Install Homebrew, then:

```
brew update
brew install autoconf automake libtool boost miniupnpc openssl@3 pkg-config protobuf python qt libevent zeromq qrencode
```

Optional (for `make deploy` .dmg):

```
brew install librsvg
```

Notes:
- We use Qt6 for the GUI. Qt host tools (moc/uic/rcc) are resolved via pkg-config.
- We use Berkeley DB 5 (Homebrew `berkeley-db@5`) with `--with-incompatible-bdb`.
  Do not mix DB4 headers with DB5 libraries (can cause wallet creation failures).

Berkeley DB
-----------
We prefer Homebrew `berkeley-db@5`. If not installed, build will fall back to a
local BDB 5.3.28 (see `contrib/build-macos-qt6.sh`).

Troubleshooting: If you see an early wallet crash like

```
GenerateNewHDMasterKey: AddKeyPubKey failed
```

or `BDB0055 illegal flag specified` in `db.log`, you likely mixed DB4 and DB5.
Fix by unlinking DB4 and rebuilding:

```
brew unlink berkeley-db@4 || true
make clean
```

Build Cascoin Core
------------------

1. Clone the cascoin source code and cd into `cascoin`

        git clone https://github.com/cascoin-project/cascoin
        cd cascoin

2. Build cascoin-core (Qt6 GUI + wallet):

   You can disable the GUI build by passing `--without-gui` to configure.

```
./autogen.sh

# Resolve Qt6 host tools path (moc/uic/rcc)
QT_HOST_BINS="$(pkg-config --variable=host_bins Qt6Core)"

# Prefer Homebrew BDB5
BDB_PREFIX="$(brew --prefix berkeley-db@5 2>/dev/null || true)"

CPPFLAGS="${CPPFLAGS:-} ${BDB_PREFIX:+-I$BDB_PREFIX/include}"
LDFLAGS="${LDFLAGS:-} ${BDB_PREFIX:+-L$BDB_PREFIX/lib}"

./configure \
  --with-gui=qt6 \
  --enable-wallet \
  --with-qrencode \
  --enable-zmq \
  --with-incompatible-bdb \
  --with-qt-bindir="$QT_HOST_BINS" \
  MOC="$QT_HOST_BINS/moc" \
  UIC="$QT_HOST_BINS/uic" \
  RCC="$QT_HOST_BINS/rcc"

make -j"$(sysctl -n hw.ncpu)"
```

3.  It is recommended to build and run the unit tests:

        make check

4. You can also create a .dmg that contains the .app bundle (optional):

        make deploy

5.  Installation into user directories (optional):

        make install

    or

        cd ~/cascoin/src
        cp cascoind /usr/local/bin/
        cp cascoin-cli /usr/local/bin/

Running
-------

Cascoin Core binaries (after a successful build):

- Daemon: `./src/cascoind`
- GUI app: `./src/qt/cascoin-qt`

Before running, it's recommended you create an RPC configuration file.

    echo -e "rpcuser=cascoinrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/Cascoin/cascoin.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/Cascoin/cascoin.conf"

The first time you run cascoind, it will start downloading the blockchain. This process could take several hours.

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/Cascoin/debug.log

Other commands:
-------

    ./src/cascoind -daemon # Starts the cascoin daemon.
    ./src/cascoin-cli --help # Outputs a list of command-line options.
    ./src/cascoin-cli help # Outputs a list of RPC commands when the daemon is running.

Using Qt Creator as IDE
------------------------
You can use Qt Creator as an IDE, for cascoin development.
Download and install the community edition of [Qt Creator](https://www.qt.io/download/).
Uncheck everything except Qt Creator during the installation process.

1. Make sure you installed everything through Homebrew mentioned above
2. Do a proper ./configure --enable-debug
3. In Qt Creator do "New Project" -> Import Project -> Import Existing Project
4. Enter "cascoin-qt" as project name, enter src/qt as location
5. Leave the file selection as it is
6. Confirm the "summary page"
7. In the "Projects" tab select "Manage Kits..."
8. Select the default "Desktop" kit and select "Clang (x86 64bit in /usr/bin)" as compiler
9. Select LLDB as debugger (you might need to set the path to your installation)
10. Start debugging with Qt Creator

Notes
-----

* Tested on Apple Silicon (arm64) with Homebrew.
* Ensure only one Berkeley DB major version is used (prefer DB5).
* Qt6 host tools are resolved via pkg-config; make sure `qt` formula is installed.
