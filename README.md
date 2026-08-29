# Cascoin Core

Cascoin Core is the full node software that makes up the backbone of the CAS network.

## What is Cascoin?

Cascoin is a cryptocurrency supporting dual PoW (SHA256/MinotaurX) and Labyrinth Mining (Hive) - a unique hybrid consensus mechanism that combines traditional mining with "mice" creation for block rewards.

### Key Specifications

| Parameter | Value |
|-----------|-------|
| Block Time | 2.5 minutes |
| Max Supply | 84 million CAS |
| Halving Interval | 840,000 blocks |
| PoW Block Reward | 25 CAS |
| Hive Block Reward | 75 CAS |
| PoW Algorithm | SHA256 (ASIC) / MinotaurX (CPU) |
| Default Port | 22222 |
| Address Prefix | H (mainnet) |

### Features

- **Hybrid Consensus**: Combines PoW with Labyrinth Mining (Hive)
- **Labyrinth Mining**: Create "mice" that can mine blocks without hardware
- **Dual PoW**: SHA256 for ASIC miners, MinotaurX for CPU miners
- **SegWit**: Native Segregated Witness support
- **Fast Sync**: SQLite-based BCT database for quick startup

## Getting Started

### Download

Prebuilt binaries for Windows, Mac and Linux are available on the [GitHub Releases](https://github.com/Casraw/Cascoin/releases) page.

### Build from Source

- **Linux**: See [doc/build-linux.md](doc/build-linux.md)
- **Windows**: See [doc/build-windows.md](doc/build-windows.md)
- **macOS**: See [doc/build-osx.md](doc/build-osx.md)
- **Unix**: See [doc/build-unix.md](doc/build-unix.md)

## Development

The `main` branch is regularly built and tested, but is not guaranteed to be completely stable. [Tags](https://github.com/Casraw/Cascoin/tags) are created regularly to indicate new official, stable release versions.

### Contributing

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

### Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code.

```bash
# Run unit tests
make check

# Run functional tests
test/functional/test_runner.py
```

## Documentation

- [Release Notes](doc/release-notes/)
- [Build Instructions](doc/)
- [Memory Leak Fixes](doc/memory-leak-fixes.md)
- [Windows Qt Fix](doc/windows-qt-platform-plugin-fix.md)

## Links

- 🌐 Website: [cascoin.net](https://cascoin.net)
- 🔍 Explorer: [casplorer.com](https://casplorer.com)
- ⛏ Pool: [mining-pool.io](https://mining-pool.io)
- � Ditscord: [discord.gg/J2NxATBS8z](https://discord.gg/J2NxATBS8z)
- 📂 GitHub: [github.com/Casraw/Cascoin](https://github.com/Casraw/Cascoin)

## License

Cascoin Core is released under the terms of the MIT license. See [COPYING](COPYING) for more information.
