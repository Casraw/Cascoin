WINDOWS BUILD NOTES
====================

Below are some notes on how to build Cascoin Core for Windows.

The recommended method is cross-compiling on Linux using the Mingw-w64 toolchain.

## Prerequisites

Install the required dependencies on Ubuntu/Debian:

```bash
sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git gperf cmake
sudo apt install g++-mingw-w64-x86-64
sudo apt install libxcb1-dev libxcb-util-dev libxcb-cursor-dev libxcb-render-util0-dev \
  libxcb-keysyms1-dev libxcb-image0-dev libxcb-icccm4-dev libxcb-xkb-dev \
  libxkbcommon-dev libxkbcommon-x11-dev
```

Note: 
- `gperf` is required for building fontconfig 2.15.0+ in the depends system.
- The `libxcb-*` and `libxkbcommon-*` packages are required for building native_qt (Qt6 host tools).

For Ubuntu 16.04+ and WSL, set the default mingw32 g++ compiler to posix:

```bash
sudo update-alternatives --config x86_64-w64-mingw32-g++
# Select the posix option
```

## Building for 64-bit Windows

### Step 1: Build Dependencies

The depends system builds all required libraries for cross-compilation:

```bash
cd depends
make HOST=x86_64-w64-mingw32 JOBS=$(nproc)
cd ..
```

This will build all dependencies including:
- Boost
- OpenSSL
- libevent
- ZeroMQ
- Qt6 (for GUI)
- Berkeley DB
- EVMC/evmone (for CVM)
- and more...

The `JOBS` parameter controls parallel builds. Use `$(nproc)` to use all CPU cores.

### Step 2: Configure

```bash
./autogen.sh  # Only needed if building from git, not from tarball
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/ --disable-tests --disable-bench --with-incompatible-bdb
```

### Step 3: Build

```bash
make -j$(nproc)
```

The resulting executables will be in `src/`:
- `src/cascoind.exe` - Main daemon
- `src/cascoin-cli.exe` - RPC client
- `src/cascoin-tx.exe` - Transaction tool

### Building with Qt6 GUI

To build with the Qt6 GUI:

```bash
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site ./configure \
  --prefix=/ \
  --disable-tests \
  --disable-bench \
  --with-gui=qt6 \
  --with-incompatible-bdb
  --enable-quantum
  --enable-cvm

make -j$(nproc)
```

The GUI executable will be at `src/qt/cascoin-qt.exe`.

## Building for 32-bit Windows

Install the 32-bit toolchain:

```bash
sudo apt install g++-mingw-w64-i686 mingw-w64-i686-dev
sudo update-alternatives --config i686-w64-mingw32-g++  # Select posix
```

Then build:

```bash
cd depends
make HOST=i686-w64-mingw32 JOBS=$(nproc)
cd ..
./autogen.sh
CONFIG_SITE=$PWD/depends/i686-w64-mingw32/share/config.site ./configure --prefix=/ --disable-tests --disable-bench --with-incompatible-bdb
make -j$(nproc)
```

## Windows Subsystem for Linux (WSL)

When using WSL, ensure the source code is in the Linux filesystem (e.g., `/home/user/cascoin`), NOT in `/mnt/c/` or similar Windows paths. The autoconf scripts will fail if the source is on the Windows filesystem.

Also strip problematic Windows PATH entries:

```bash
PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g')
```

## Installation

To install the built executables to a Windows-accessible location:

```bash
make install DESTDIR=/mnt/c/workspace/cascoin
```

## Troubleshooting

### Missing config.site

If `depends/x86_64-w64-mingw32/share/config.site` doesn't exist, the depends build didn't complete successfully. Re-run:

```bash
make -C depends HOST=x86_64-w64-mingw32 JOBS=$(nproc)
```

### Linker errors with evmone

If you see undefined reference errors for `ethash_keccak256` or similar, ensure the evmone package was built correctly with all submodules. The depends system should handle this automatically.

### Compiler threading issues

The win32 threading model conflicts with C++11 std::mutex. Always use the posix threading model:

```bash
sudo update-alternatives --config x86_64-w64-mingw32-g++
# Select: /usr/bin/x86_64-w64-mingw32-g++-posix
```

## Depends System

For more information about the depends system, see [depends/README.md](../depends/README.md).

## Clean Build

To start fresh:

```bash
make clean
rm -rf depends/x86_64-w64-mingw32
# Then repeat from Step 1
```

To also clean the depends cache:

```bash
rm -rf depends/built/x86_64-w64-mingw32
rm -rf depends/work
```
