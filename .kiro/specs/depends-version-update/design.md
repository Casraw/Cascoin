# Design Document: Depends System Version Update

## Overview

This document describes the design for updating the Cascoin Core depends system to use current, stable versions of all external dependencies. The depends system provides reproducible builds and cross-compilation support by managing specific versions of libraries.

Based on research conducted on January 5, 2026, the following version updates are planned:

| Package | Current Version | Target Version | Notes |
|---------|----------------|----------------|-------|
| OpenSSL | 3.2.3 | 3.5.0 (LTS) | Long-term support until 2030 |
| Boost | 1.83.0 | 1.87.0 | Latest stable |
| SQLite | 3.44.2 | 3.47.0 | Latest stable |
| Qt6 | 6.7.3 | 6.8.1 | Latest stable |
| freetype | 2.7.1 | 2.13.3 | Major update |
| fontconfig | 2.12.1 | 2.15.0 | Major update |
| expat | 2.2.1 | 2.6.4 | Security updates |
| qrencode | 3.4.4 | 4.1.1 | Major update |
| ccache | 3.3.4 | 4.10.2 | CMake-based build |
| miniupnpc | 2.2.7 | 2.2.8 | Minor update |
| libevent | 2.1.12 | 2.1.12 | Already current |
| ZeroMQ | 4.3.5 | 4.3.5 | Already current |
| zlib | 1.3.1 | 1.3.1 | Already current |
| BDB | 5.3.28 | 5.3.28 | Keep for wallet compatibility |
| EVMC | 12.1.0 | 12.1.0 | Already current |
| evmone | 0.18.0 | 0.18.0 | Already current |
| intx | 0.13.0 | 0.13.0 | Already current |
| protobuf | 21.12 | 21.12 | Keep for API stability |

## Architecture

The depends system uses Makefile-based package definitions in `depends/packages/`. Each `.mk` file defines:

1. **Version and download information**: Version number, download URL, SHA256 hash
2. **Build configuration**: Configure options, compiler flags
3. **Build commands**: Preprocessing, configuration, build, staging steps
4. **Platform-specific settings**: Linux, Windows (MinGW), macOS (Darwin)

### Update Strategy

Updates will be performed in phases to minimize risk:

1. **Phase 1**: Core libraries (OpenSSL, Boost, SQLite)
2. **Phase 2**: GUI libraries (Qt6, freetype, fontconfig, expat, qrencode)
3. **Phase 3**: Build tools (ccache)
4. **Phase 4**: Minor updates (miniupnpc)

## Components and Interfaces

### Package Definition Structure

Each package `.mk` file follows this structure:

```makefile
package=<name>
$(package)_version=<version>
$(package)_download_path=<url>
$(package)_file_name=<filename>
$(package)_sha256_hash=<hash>
$(package)_dependencies=<deps>

define $(package)_set_vars
  # Configuration options
endef

define $(package)_preprocess_cmds
  # Pre-build commands
endef

define $(package)_config_cmds
  # Configure commands
endef

define $(package)_build_cmds
  # Build commands
endef

define $(package)_stage_cmds
  # Install commands
endef
```

### Updated Package Definitions

#### OpenSSL 3.5.0

```makefile
package=openssl
$(package)_version=3.5.0
$(package)_download_path=https://github.com/openssl/openssl/releases/download/openssl-$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Download path changed to GitHub releases
- LTS version for long-term support
- Same configure options should work

#### Boost 1.87.0

```makefile
package=boost
$(package)_version=1_87_0
$(package)_download_path=https://archives.boost.io/release/1.87.0/source/
$(package)_file_name=$(package)_$($(package)_version).tar.bz2
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Version number format remains `1_87_0`
- Download path uses archives.boost.io
- Same build configuration

#### SQLite 3.47.0

```makefile
package=sqlite
$(package)_version=3470000
$(package)_download_path=https://www.sqlite.org/2024/
$(package)_file_name=sqlite-autoconf-$($(package)_version).tar.gz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Version format is `3XXYYZZ` (3.47.0 = 3470000)
- Download path year may need updating

#### Qt 6.8.1

Updates required in `qt_details.mk`:

```makefile
qt_details_version := 6.8.1
qt_details_download_path := https://download.qt.io/archive/qt/6.8/$(qt_details_version)/submodules
qt_details_qtbase_sha256_hash=<to-be-determined>
qt_details_qttranslations_sha256_hash=<to-be-determined>
qt_details_qttools_sha256_hash=<to-be-determined>
```

Key changes:
- All three Qt components need hash updates
- CMakeLists.txt helper files need new hashes
- Patches may need verification for compatibility

#### freetype 2.13.3

```makefile
package=freetype
$(package)_version=2.13.3
$(package)_download_path=https://download.savannah.gnu.org/releases/$(package)
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Major version jump (2.7.1 → 2.13.3)
- File format may have changed to .tar.xz
- Configure options should remain compatible

#### fontconfig 2.15.0

```makefile
package=fontconfig
$(package)_version=2.15.0
$(package)_download_path=https://www.freedesktop.org/software/fontconfig/release/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Major version jump (2.12.1 → 2.15.0)
- CHAR_WIDTH workaround may no longer be needed
- File format changed to .tar.xz

#### expat 2.6.4

```makefile
package=expat
$(package)_version=2.6.4
$(package)_download_path=https://github.com/libexpat/libexpat/releases/download/R_2_6_4
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Download path changed to GitHub releases
- Version format in URL uses underscores (R_2_6_4)
- File format changed to .tar.xz

#### qrencode 4.1.1

```makefile
package=qrencode
$(package)_version=4.1.1
$(package)_download_path=https://fukuchi.org/works/qrencode/
$(package)_file_name=$(package)-$($(package)_version).tar.bz2
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Major version jump (3.4.4 → 4.1.1)
- Same download location
- API should be backward compatible

#### ccache 4.10.2

```makefile
package=native_ccache
$(package)_version=4.10.2
$(package)_download_path=https://github.com/ccache/ccache/releases/download/v$($(package)_version)
$(package)_file_name=ccache-$($(package)_version).tar.xz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Major version jump (3.3.4 → 4.10.2)
- Build system changed from autotools to CMake
- Download path changed to GitHub releases
- Build commands need complete rewrite

#### miniupnpc 2.2.8

```makefile
package=miniupnpc
$(package)_version=2.2.8
$(package)_download_path=https://miniupnp.tuxfamily.org/files
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=<to-be-determined>
```

Key changes:
- Minor version update
- Same build process

## Data Models

### SHA256 Hash Verification

Each package requires a SHA256 hash for verification. The hash must be obtained from:

1. Official release announcements
2. Computing hash from downloaded file: `sha256sum <filename>`
3. Package signing keys where available

### Version Number Formats

Different packages use different version formats:

| Package | Format | Example |
|---------|--------|---------|
| Boost | `X_YY_Z` | `1_87_0` |
| SQLite | `XYYZZPP` | `3470000` |
| Qt | `X.Y.Z` | `6.8.1` |
| Others | `X.Y.Z` | `3.5.0` |

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system—essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

Since this feature involves configuration file updates and build verification rather than runtime behavior, the correctness properties focus on build-time verification:

**Property 1: SHA256 Hash Integrity**
*For any* downloaded package archive, computing its SHA256 hash SHALL produce the exact value specified in the package definition.
**Validates: Requirements 1.2, 2.4, 5.2, 7.3, 9.5**

**Property 2: Build Success**
*For any* updated package, the depends build system SHALL successfully compile and install the package for the target platform.
**Validates: Requirements 1.3, 3.3, 6.4, 8.4, 11.1, 11.2**

**Property 3: Version Consistency**
*For any* package definition, the version string in the filename SHALL match the version variable in the package definition.
**Validates: Requirements 1.1, 2.1, 2.2, 2.3, 3.1, 4.1, 5.1, 6.1, 8.1, 8.2, 8.3, 9.1, 9.2, 9.3, 9.4, 10.1**

## Error Handling

### Download Failures

If a package download fails:
1. Verify the download URL is correct
2. Check if the version exists on the download server
3. Try alternative mirrors if available

### Hash Mismatch

If SHA256 hash verification fails:
1. Re-download the file
2. Verify the hash from official sources
3. Check for file corruption during download

### Build Failures

If a package fails to build:
1. Check compiler error messages
2. Verify configure options are compatible with new version
3. Check for API changes requiring patch updates
4. Document incompatibility if unresolvable

### Patch Compatibility

If existing patches fail to apply:
1. Check if the patch is still needed in new version
2. Update patch line numbers if file structure changed
3. Rewrite patch if significant changes occurred
4. Remove patch if issue was fixed upstream

## Testing Strategy

### Unit Tests

Unit tests verify individual package definitions:

1. **Version format tests**: Verify version strings match expected formats
2. **URL format tests**: Verify download URLs are well-formed
3. **Hash format tests**: Verify SHA256 hashes are 64 hex characters

### Integration Tests

Integration tests verify the complete build process:

1. **Linux native build**: `make -C depends HOST=x86_64-pc-linux-gnu`
2. **Windows cross-compile**: `make -C depends HOST=x86_64-w64-mingw32`
3. **Individual package builds**: `make -C depends <package>`

### Verification Checklist

For each updated package:

- [ ] Version number updated correctly
- [ ] Download URL accessible
- [ ] SHA256 hash verified
- [ ] Package builds successfully on Linux
- [ ] Package builds successfully for Windows cross-compile
- [ ] Dependent packages still build
- [ ] Main project compiles with updated dependencies

### Property-Based Testing

Due to the nature of this feature (configuration updates), property-based testing is not applicable. Verification is performed through:

1. Manual hash verification
2. Build system execution
3. Compilation success/failure

## Implementation Notes

### Hash Acquisition Process

To obtain SHA256 hashes for new versions:

```bash
# Download the file
wget <download_url>

# Compute hash
sha256sum <filename>
```

### Build Verification Process

```bash
# Clean previous builds
make -C depends clean

# Build for Linux
make -C depends HOST=x86_64-pc-linux-gnu -j$(nproc)

# Build for Windows (cross-compile)
make -C depends HOST=x86_64-w64-mingw32 -j$(nproc)
```

### ccache Build System Migration

The ccache package requires special attention as it migrated from autotools to CMake:

```makefile
# Old (autotools)
define $(package)_config_cmds
  $($(package)_autoconf)
endef

# New (CMake)
define $(package)_config_cmds
  mkdir -p build && cd build && cmake .. \
    -DCMAKE_INSTALL_PREFIX=$(host_prefix) \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_TESTING=OFF
endef
```

## Dependencies

### Build Dependencies

- CMake 3.16+ (for ccache, Qt)
- Ninja (optional, for faster Qt builds)
- Cross-compilation toolchain (for Windows builds)

### Package Dependencies

```
fontconfig → freetype, expat
Qt → freetype, fontconfig (Linux only)
```

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| API breaking changes | Build failures | Keep previous version as fallback |
| Missing patches | Build failures | Update or remove patches |
| Download URL changes | Build failures | Use stable mirrors |
| Hash unavailable | Cannot verify | Compute from official download |
