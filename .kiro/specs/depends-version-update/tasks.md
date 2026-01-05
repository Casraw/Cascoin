# Implementation Plan: Depends System Version Update

## Overview

This implementation plan updates the Cascoin Core depends system to use current, stable versions of external dependencies. Updates are organized in phases to minimize risk and allow incremental verification.

## Tasks

- [x] 1. Phase 1: Core Libraries Update
  - Update critical libraries that other packages depend on
  - _Requirements: 1.1, 1.2, 1.3, 3.1, 3.2, 3.3, 4.1, 4.2_

  - [x] 1.1 Update OpenSSL to 3.5.0 LTS
    - Update `depends/packages/openssl.mk` with new version
    - Change download path to GitHub releases
    - Compute and add SHA256 hash
    - Verify configure options compatibility
    - _Requirements: 1.1, 1.2, 1.3_

  - [x] 1.2 Update Boost to 1.87.0
    - Update `depends/packages/boost.mk` with new version
    - Update download path format
    - Compute and add SHA256 hash
    - Verify required libraries still build (chrono, filesystem, program_options, system, thread, test)
    - _Requirements: 3.1, 3.2, 3.3_

  - [x] 1.3 Update SQLite to 3.47.0
    - Update `depends/packages/sqlite.mk` with new version
    - Update version number format (3470000)
    - Update download path year if needed
    - Compute and add SHA256 hash
    - _Requirements: 4.1, 4.2_

  - [x] 1.4 Verify Phase 1 builds
    - Build all Phase 1 packages for Linux x86_64
    - Build all Phase 1 packages for Windows cross-compile
    - Document any issues encountered
    - _Requirements: 11.1, 11.2_

- [x] 2. Phase 2: GUI Support Libraries Update
  - Update libraries required for Qt GUI
  - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5_

  - [x] 2.1 Update freetype to 2.13.3
    - Update `depends/packages/freetype.mk` with new version
    - Update file format if changed (.tar.xz)
    - Compute and add SHA256 hash
    - Verify configure options compatibility
    - _Requirements: 9.1, 9.5_

  - [x] 2.2 Update fontconfig to 2.15.0
    - Update `depends/packages/fontconfig.mk` with new version
    - Update file format (.tar.xz)
    - Compute and add SHA256 hash
    - Check if CHAR_WIDTH workaround is still needed
    - _Requirements: 9.2, 9.5_

  - [x] 2.3 Update expat to 2.6.4
    - Update `depends/packages/expat.mk` with new version
    - Change download path to GitHub releases
    - Update URL format (R_2_6_4)
    - Compute and add SHA256 hash
    - _Requirements: 9.3, 9.5_

  - [x] 2.4 Update qrencode to 4.1.1
    - Update `depends/packages/qrencode.mk` with new version
    - Compute and add SHA256 hash
    - Verify API compatibility
    - _Requirements: 9.4, 9.5_

  - [x] 2.5 Verify Phase 2 builds
    - Build all Phase 2 packages for Linux x86_64
    - Build all Phase 2 packages for Windows cross-compile
    - Verify fontconfig builds with updated freetype
    - _Requirements: 11.1, 11.2_

- [x] 3. Phase 3: Qt6 Framework Update
  - Update Qt6 to latest stable version
  - _Requirements: 6.1, 6.2, 6.3, 6.4_

  - [x] 3.1 Update Qt6 version details
    - Update `depends/packages/qt_details.mk` with version 6.8.1
    - Update download path for new version
    - Compute SHA256 hashes for qtbase, qttranslations, qttools
    - _Requirements: 6.1, 6.2_

  - [x] 3.2 Update Qt6 CMake helper files
    - Download new CMakeLists.txt from Qt repository
    - Download new ECMOptionalAddSubdirectory.cmake
    - Download new QtTopLevelHelpers.cmake
    - Compute and update SHA256 hashes
    - _Requirements: 6.3_

  - [x] 3.3 Verify Qt6 patches compatibility
    - Check each patch in `depends/patches/qt/` for compatibility
    - Update patches if line numbers changed
    - Remove patches if issues fixed upstream
    - _Requirements: 6.4_

  - [x] 3.4 Verify Phase 3 builds
    - Build Qt6 for Linux x86_64
    - Build Qt6 for Windows cross-compile
    - Verify cascoin-qt compiles with updated Qt
    - _Requirements: 11.1, 11.2_

- [x] 4. Phase 4: Build Tools Update
  - Update build acceleration tools
  - _Requirements: 10.1, 10.2_

  - [x] 4.1 Update ccache to 4.10.2
    - Update `depends/packages/native_ccache.mk` with new version
    - Change download path to GitHub releases
    - Migrate build system from autotools to CMake
    - Update config_cmds, build_cmds, stage_cmds
    - Compute and add SHA256 hash
    - _Requirements: 10.1, 10.2_

  - [x] 4.2 Verify Phase 4 builds
    - Build ccache for native platform
    - Verify ccache works with depends build system
    - _Requirements: 11.1_

- [x] 5. Phase 5: Minor Updates
  - Update packages with minor version changes
  - _Requirements: 2.3, 2.4_

  - [x] 5.1 Update miniupnpc to 2.2.8
    - Update `depends/packages/miniupnpc.mk` with new version
    - Compute and add SHA256 hash
    - _Requirements: 2.3, 2.4_

  - [x] 5.2 Verify Phase 5 builds
    - Build miniupnpc for Linux x86_64
    - Build miniupnpc for Windows cross-compile
    - _Requirements: 11.1, 11.2_

- [-] 6. Final Integration Verification
  - Verify complete depends system and main project build
  - _Requirements: 11.1, 11.2, 11.3, 12.1_

  - [x] 6.1 Full depends build for Linux
    - Clean depends directory
    - Build all packages: `make -C depends HOST=x86_64-pc-linux-gnu -j$(nproc)`
    - Document build time and any warnings
    - _Requirements: 11.1_

  - [-] 6.2 Full depends build for Windows
    - Clean depends directory
    - Build all packages: `make -C depends HOST=x86_64-w64-mingw32 -j$(nproc)`
    - Document build time and any warnings
    - _Requirements: 11.2_

  - [ ] 6.3 Build main project with updated depends
    - Configure with updated depends: `./configure --prefix=$(pwd)/depends/x86_64-pc-linux-gnu`
    - Build cascoind and cascoin-qt
    - Run unit tests: `make check`
    - _Requirements: 11.1, 11.3_

  - [ ] 6.4 Document version changes
    - Create summary of all version updates
    - Document any packages that could not be updated
    - Note any compatibility issues encountered
    - _Requirements: 12.1, 12.2_

- [ ] 7. Checkpoint - Final Review
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- SHA256 hashes must be computed from official downloads
- Build verification should be done incrementally after each phase
- If a package update causes issues, document and consider keeping previous version
- Berkeley DB (5.3.28) and Protocol Buffers (21.12) intentionally not updated for compatibility
- libevent, ZeroMQ, zlib, and EVM packages already at current versions
