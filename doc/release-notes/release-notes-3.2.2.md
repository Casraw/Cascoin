Cascoin Core version 3.2.2 is now available.

This is a minor release that adds official macOS (Apple Silicon) support, fixes a critical Labyrinth display bug, and includes cross-platform build improvements.

## macOS (Apple Silicon) Support

Cascoin Core can now be built and run natively on macOS with Apple Silicon (ARM64). Pre-built binaries for macOS are included in the release assets.

### Installing dependencies on macOS

Install [Homebrew](https://brew.sh/) and the required packages:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
eval "$(/opt/homebrew/bin/brew shellenv)"

brew update
brew install autoconf automake libtool pkg-config \
  boost berkeley-db@5 miniupnpc openssl@3 \
  qt@6 qrencode zeromq libevent protobuf python
```

### Building on macOS

```bash
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

See `doc/build-macos.md` for full details and troubleshooting.

## Labyrinth Display Fix

A combination of three issues caused the Labyrinth tab and Overview page to show zero values for blocks found, rewards, costs, and mice counts after extended uptime:

- **SQLite busy timeout missing**: The BCT database did not set a `busy_timeout`, causing concurrent read queries from the GUI thread to fail silently with `SQLITE_BUSY`. A 5-second busy timeout has been added.

- **Race condition in table model updates**: The `HiveTableModel` summary variables were reset to zero before new data was loaded from the background thread. Summary values are now accumulated in local variables and written atomically.

- **Expired mice excluded from totals**: The Labyrinth tab and Overview page obtained totals from the table model, which only loads non-expired mice. Both pages now query the database summary directly.

## Additional Fixes

- Fixed missing `cs_main` lock in reward rescan during startup
- Extended `BCTSummary` with per-status mice counts (`immatureBees`, `matureBees`, `expiredBees`)
- Renamed internal comments from "Hive" / "Bee" to "Labyrinth" / "Mice"

## Cross-Platform Build Fixes

- Fixed `scrypt.h` / `scrypt.cpp` endian helper functions to compile on macOS without runtime source patching
- Fixed missing `#include <QElapsedTimer>` in `src/qt/bitcoin.cpp` for Qt6
- Changed `ax_boost_system.m4` to emit a warning instead of a fatal error for header-only Boost

## CI/CD

- Added `build-macos` job to GitHub Actions (`macos-14` runner, ARM64)
- macOS build artifacts are now included in release drafts

## Dependencies

- Updated zlib from 1.3.1 to 1.3.2 (fixes 404 on cross-compilation for Windows)

## Upgrade Notes

This is a recommended upgrade for all users. No database migration is required. This is a drop-in replacement for 3.2.1.x.
