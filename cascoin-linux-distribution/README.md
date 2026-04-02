# Cascoin Core Linux Distribution Package

This package contains Cascoin Core binaries with essential libraries.
Qt6 must be installed separately via your distribution's package manager.

## Prerequisites

Install Qt6 before running the GUI:

```bash
# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-base-dev-tools

# Fedora/RHEL
sudo dnf install qt6-qtbase-devel

# Arch Linux
sudo pacman -S qt6-base
```

## Installation

```bash
sudo ./install.sh
```

## Usage

After installation:

```bash
cascoin-qt-wrapper    # GUI wallet (requires Qt6)
cascoind-wrapper      # Daemon
cascoin-cli           # CLI tool
cascoin-tx            # Transaction tool
```

## System Requirements

- Linux x86_64
- GLIBC 2.31+ (Ubuntu 20.04+)
- Qt6 6.0+ (for GUI)
- X11 or Wayland display server (for GUI)

          ## Included Libraries (ONLY required ones)
          
          - Boost (system, filesystem, chrono, thread, program_options)
          - Berkeley DB (C++ interface for wallet)
          - libcrypto (OpenSSL crypto functions)
          - libevent + libevent_pthreads (networking)
          - libminiupnpc (UPnP support)
          - libprotobuf (serialization)
          - libqrencode (QR code generation)
          - libzmq (ZeroMQ messaging)
          
          Qt6 libraries are NOT included - install via package manager.
