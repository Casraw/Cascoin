# Requirements Document

## Introduction

This document specifies the requirements for updating the Cascoin Core depends system to use current, stable versions of all dependencies. The depends system provides cross-compilation support and reproducible builds by managing external library versions. Many packages are outdated and need updates to improve security, compatibility, and performance.

## Glossary

- **Depends_System**: The build system located in `depends/` that manages external library dependencies for cross-compilation
- **Package_Definition**: A `.mk` file in `depends/packages/` that defines version, download URL, build instructions, and SHA256 hash for a dependency
- **Host_System**: The target platform for which binaries are being built (Linux, Windows, macOS)
- **Build_System**: The system on which compilation is performed
- **SHA256_Hash**: Cryptographic hash used to verify downloaded source archives

## Requirements

### Requirement 1: Update Core Cryptographic Libraries

**User Story:** As a developer, I want to use current versions of cryptographic libraries, so that the build includes the latest security patches and improvements.

#### Acceptance Criteria

1. WHEN updating OpenSSL, THE Depends_System SHALL use version 3.3.x or later (current: 3.2.3)
2. WHEN a new OpenSSL version is configured, THE Package_Definition SHALL include the correct SHA256_Hash for verification
3. WHEN building with updated OpenSSL, THE Depends_System SHALL maintain compatibility with existing configure options

### Requirement 2: Update Networking Libraries

**User Story:** As a developer, I want current networking library versions, so that the build benefits from bug fixes and performance improvements.

#### Acceptance Criteria

1. WHEN updating libevent, THE Depends_System SHALL use version 2.1.12-stable or later (current: 2.1.12-stable - verify if newer exists)
2. WHEN updating ZeroMQ, THE Depends_System SHALL use version 4.3.5 or later (current: 4.3.5 - verify if newer exists)
3. WHEN updating miniupnpc, THE Depends_System SHALL use version 2.2.8 or later if available (current: 2.2.7)
4. WHEN a new library version is configured, THE Package_Definition SHALL include the correct SHA256_Hash

### Requirement 3: Update Boost Library

**User Story:** As a developer, I want the current Boost version, so that I can use modern C++ features and benefit from performance improvements.

#### Acceptance Criteria

1. WHEN updating Boost, THE Depends_System SHALL use version 1.85.0 or later (current: 1.83.0)
2. WHEN a new Boost version is configured, THE Package_Definition SHALL update the download path to match the new version format
3. WHEN building with updated Boost, THE Depends_System SHALL maintain compatibility with required Boost libraries (chrono, filesystem, program_options, system, thread, test)

### Requirement 4: Update Database Libraries

**User Story:** As a developer, I want current database library versions, so that the build includes performance improvements and bug fixes.

#### Acceptance Criteria

1. WHEN updating SQLite, THE Depends_System SHALL use version 3.45.x or later (current: 3.44.2)
2. WHEN a new SQLite version is configured, THE Package_Definition SHALL update the download path year if necessary
3. THE Depends_System SHALL maintain Berkeley DB at version 5.3.28 for wallet compatibility (no update required)

### Requirement 5: Update Compression Libraries

**User Story:** As a developer, I want current compression library versions, so that the build includes the latest optimizations.

#### Acceptance Criteria

1. WHEN updating zlib, THE Depends_System SHALL use version 1.3.1 or later (current: 1.3.1 - verify if newer exists)
2. WHEN a new zlib version is configured, THE Package_Definition SHALL include the correct SHA256_Hash

### Requirement 6: Update Qt Framework

**User Story:** As a developer, I want the current Qt6 version, so that the GUI benefits from bug fixes and improvements.

#### Acceptance Criteria

1. WHEN updating Qt, THE Depends_System SHALL use version 6.8.x or later if stable (current: 6.7.3)
2. WHEN a new Qt version is configured, THE Package_Definition SHALL update all related files (qtbase, qttranslations, qttools)
3. WHEN a new Qt version is configured, THE Package_Definition SHALL update the CMakeLists.txt and helper file hashes
4. WHEN building with updated Qt, THE Depends_System SHALL maintain compatibility with existing Qt patches

### Requirement 7: Update Serialization Libraries

**User Story:** As a developer, I want current serialization library versions, so that the build includes security patches and improvements.

#### Acceptance Criteria

1. WHEN updating Protocol Buffers, THE Depends_System SHALL use version 25.x or later if compatible (current: 21.12)
2. IF Protocol Buffers major version changes, THEN THE Depends_System SHALL verify API compatibility before updating
3. WHEN a new protobuf version is configured, THE Package_Definition SHALL include the correct SHA256_Hash

### Requirement 8: Update EVM Libraries

**User Story:** As a developer, I want current EVM library versions, so that the CVM implementation benefits from upstream improvements.

#### Acceptance Criteria

1. WHEN updating EVMC, THE Depends_System SHALL use version 12.1.0 or later (current: 12.1.0 - verify if newer exists)
2. WHEN updating evmone, THE Depends_System SHALL use version 0.18.0 or later (current: 0.18.0 - verify if newer exists)
3. WHEN updating intx, THE Depends_System SHALL use version 0.13.0 or later (current: 0.13.0 - verify if newer exists)
4. WHEN a new EVM library version is configured, THE Package_Definition SHALL maintain cross-compilation compatibility

### Requirement 9: Update GUI Support Libraries

**User Story:** As a developer, I want current GUI support library versions, so that the Qt GUI builds correctly on all platforms.

#### Acceptance Criteria

1. WHEN updating freetype, THE Depends_System SHALL use version 2.13.x or later (current: 2.7.1)
2. WHEN updating fontconfig, THE Depends_System SHALL use version 2.15.x or later (current: 2.12.1)
3. WHEN updating expat, THE Depends_System SHALL use version 2.6.x or later (current: 2.2.1)
4. WHEN updating qrencode, THE Depends_System SHALL use version 4.1.x or later (current: 3.4.4)
5. WHEN a new library version is configured, THE Package_Definition SHALL include the correct SHA256_Hash

### Requirement 10: Update Build Tools

**User Story:** As a developer, I want current build tool versions, so that the build process is efficient and reliable.

#### Acceptance Criteria

1. WHEN updating ccache, THE Depends_System SHALL use version 4.10.x or later (current: 3.3.4)
2. WHEN a new ccache version is configured, THE Package_Definition SHALL adapt to any build system changes (autotools vs cmake)

### Requirement 11: Verify Build Compatibility

**User Story:** As a developer, I want all updated packages to build successfully, so that the depends system remains functional.

#### Acceptance Criteria

1. WHEN all packages are updated, THE Depends_System SHALL successfully build for Linux x86_64 target
2. WHEN all packages are updated, THE Depends_System SHALL successfully build for Windows x86_64 cross-compilation target
3. IF a package update breaks the build, THEN THE Depends_System SHALL document the incompatibility and provide a workaround

### Requirement 12: Document Version Changes

**User Story:** As a developer, I want clear documentation of version changes, so that I can understand what was updated and why.

#### Acceptance Criteria

1. WHEN packages are updated, THE Depends_System SHALL maintain a changelog or summary of version changes
2. WHEN a package cannot be updated due to compatibility issues, THE documentation SHALL explain the reason
