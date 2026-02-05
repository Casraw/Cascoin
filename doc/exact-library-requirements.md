# Cascoin Core - Exact Library Requirements

This document specifies the **exact libraries** that must be included in the distribution package.

## Philosophy

- **MINIMAL**: Only libraries absolutely required by Cascoin Core
- **NO EXTRAS**: No additional libraries "just in case"
- **NO Qt6**: Qt6 installed via package manager on target system
- **SPECIFIC VERSIONS**: Include version-specific .so files

## Required Libraries List

### 1. Boost Libraries (5 modules only)
```
libboost_system.so*          # Core Boost system functionality
libboost_filesystem.so*      # File system operations
libboost_chrono.so*          # Time and timing utilities
libboost_thread.so*          # Threading support
libboost_program_options.so* # Command line argument parsing
```

**Excluded Boost modules**: All other Boost libraries (test, regex, serialization, etc.)

### 2. Berkeley DB Libraries
```
libdb_cxx-*.so*             # C++ interface for wallet storage
libdb-*.so*                 # C interface (dependency)
```

**Purpose**: Wallet file storage and management

### 3. OpenSSL Libraries
```
libcrypto.so*               # Cryptographic functions ONLY
```

**Excluded**: `libssl.so*` (SSL/TLS not needed for local wallet)

### 4. libevent Libraries
```
libevent-*.so*              # Core event handling
libevent_pthreads-*.so*     # Pthread support for events
```

**Excluded**: `libevent_core`, `libevent_extra` (not directly needed)

### 5. Protocol and Network Libraries
```
libminiupnpc.so*            # UPnP port forwarding
libprotobuf.so*             # Protocol buffer serialization
libqrencode.so*             # QR code generation for addresses
libzmq.so*                  # ZeroMQ messaging (if enabled)
```

## What is NOT Included

### System Libraries (Available Everywhere)
- `libc.so*`, `libm.so*`, `libpthread.so*`
- `libstdc++.so*`, `libgcc_s.so*`
- `libdl.so*`, `librt.so*`

### GUI Libraries (Install via Package Manager)
- All `libQt6*.so*` libraries
- Qt6 plugins and platform support
- Desktop environment integration

### Optional/Development Libraries
- Test frameworks
- Debug libraries
- Development headers
- Documentation tools

## Implementation

### GitHub Actions (`.github/workflows/build.yml`)
```bash
# Copy exact Boost libraries needed by Cascoin
boost_libs=(
  "libboost_system.so*"
  "libboost_filesystem.so*" 
  "libboost_chrono.so*"
  "libboost_thread.so*"
  "libboost_program_options.so*"
)

for boost_lib in "${boost_libs[@]}"; do
  find /usr/lib/x86_64-linux-gnu -name "$boost_lib" 2>/dev/null | while read lib; do
    # Copy only if not already present
  done
done
```

### Local Build Script (`contrib/ci/build-distribution-local.sh`)
Same implementation as GitHub Actions for consistency.

## Size Impact

| Library Category | Files | Approximate Size |
|------------------|-------|------------------|
| Boost (5 modules) | ~10 files | ~15 MB |
| Berkeley DB | ~4 files | ~3 MB |
| libcrypto | ~2 files | ~4 MB |
| libevent | ~4 files | ~1 MB |
| Protocol/Network | ~8 files | ~5 MB |
| **Total** | **~28 files** | **~28 MB** |

## Verification

### Test Script
```bash
./contrib/ci/test-library-versions.sh
```

### Manual Verification
```bash
# Check what Cascoin actually uses
ldd src/qt/cascoin-qt | grep -E "(boost|db|crypto|event|protobuf|miniupnpc|zmq|qrencode)"
ldd src/cascoind | grep -E "(boost|db|crypto|event|protobuf|miniupnpc|zmq)"
```

### Package Contents
```bash
tar -tzf cascoin-linux-x86_64-distribution-*.tar.gz | grep "\.so"
```

## Target System Requirements

### Ubuntu/Debian
```bash
sudo apt-get install qt6-base-dev qt6-base-dev-tools
```

### Fedora/RHEL
```bash
sudo dnf install qt6-qtbase-devel
```

### Arch Linux
```bash
sudo pacman -S qt6-base
```

## Benefits

1. **Minimal Size**: ~28MB instead of 150+ MB
2. **Fast Installation**: Fewer files to copy
3. **Fewer Conflicts**: No version conflicts with system libraries
4. **Better Compatibility**: Works across different Qt6 versions
5. **Standard Practice**: Follows Linux distribution conventions

## Troubleshooting

### Missing Library Errors
```bash
# Check if all required libraries are present
ls -la /usr/lib/x86_64-linux-gnu/libboost_*.so*
ls -la /usr/lib/x86_64-linux-gnu/libdb*.so*
ls -la /usr/lib/x86_64-linux-gnu/libcrypto.so*
```

### Runtime Issues
```bash
# Test library loading
LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu" ldd src/qt/cascoin-qt
```

This exact specification ensures the distribution package contains **only** what Cascoin Core actually needs, nothing more, nothing less.
