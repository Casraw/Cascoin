# Build Cache Optimization Guide

This document describes the caching strategies implemented in Cascoin Core builds to reduce build times and improve CI/CD performance.

## Cache Types

### 1. APT Package Cache
- **Path**: `/var/cache/apt/archives`
- **Purpose**: Cache downloaded .deb packages
- **Benefit**: Faster dependency installation
- **Key**: Based on workflow file hash

### 2. CCache (Compiler Cache)
- **Path**: `~/.ccache`
- **Purpose**: Cache compiled object files
- **Benefit**: 80-90% reduction in compile time for incremental builds
- **Configuration**: 1-2GB size limit
- **Key**: Based on git SHA

### 3. Autotools Cache
- **Path**: `autom4te.cache`, `config.cache`
- **Purpose**: Cache autoconf/automake results
- **Benefit**: Faster configure step
- **Key**: Based on configure.ac and autogen.sh

### 4. Build Objects Cache
- **Path**: `src/**/*.o`, `src/**/*.lo`, `.deps`
- **Purpose**: Cache compiled object files
- **Benefit**: Faster incremental builds
- **Key**: Based on source file hashes

### 5. Qt MOC Cache
- **Path**: `src/qt/moc_*.cpp`, `src/qt/qrc_*.cpp`, `src/qt/ui_*.h`
- **Purpose**: Cache Qt Meta-Object Compiler files
- **Benefit**: Faster Qt-specific builds
- **Key**: Based on Qt source files

## Local Development

### Setup CCache

```bash
# Install ccache
sudo apt-get install ccache

# Configure ccache
ccache --max-size=2G
ccache --show-config

# Add to build environment
export CCACHE_DIR=$HOME/.ccache
export PATH="/usr/lib/ccache:$PATH"
```

### Build with Cache

```bash
# Configure with ccache support
./configure --enable-ccache ...

# Build
make -j$(nproc)

# Check cache stats
ccache --show-stats
```

## GitHub Actions Cache

The CI workflow implements multiple cache layers:

```yaml
- name: Cache ccache
  uses: actions/cache@v4
  with:
    path: ~/.ccache
    key: ${{ runner.os }}-ccache-${{ github.sha }}
    restore-keys: |
      ${{ runner.os }}-ccache-
```

## Performance Improvements

| Cache Type | First Build | Incremental Build | Savings |
|------------|-------------|-------------------|---------|
| No Cache   | 45-60 min   | 45-60 min        | 0%      |
| APT Cache  | 40-55 min   | 40-55 min        | 10%     |
| + CCache   | 40-55 min   | 5-15 min         | 75%     |
| + Full     | 35-50 min   | 3-8 min          | 85%     |

## Cache Maintenance

### Local Cleanup
```bash
# Clean ccache
ccache --cleanup
ccache --clear

# Clean autotools cache
rm -rf autom4te.cache

# Clean build objects
make clean
```

### CI Cache Limits
- **GitHub Actions**: 10GB total per repository
- **Cache Expiration**: 7 days if not accessed
- **Strategy**: Use restore-keys for fallback

## Troubleshooting

### Cache Miss Issues
1. Check cache key generation
2. Verify file paths exist
3. Review restore-keys hierarchy

### Build Inconsistencies
1. Clear caches: `ccache --clear`
2. Clean build: `make clean`
3. Reconfigure: `./autogen.sh && ./configure ...`

### Disk Space Issues
1. Reduce ccache size: `ccache --max-size=512M`
2. Clean old caches: `ccache --cleanup`
3. Monitor disk usage: `ccache --show-stats`

## Best Practices

1. **Layer Caches**: Use multiple cache types together
2. **Key Strategy**: Include relevant file hashes in cache keys
3. **Size Limits**: Balance cache size vs. available storage
4. **Cleanup**: Regular maintenance prevents corruption
5. **Monitoring**: Track cache hit rates and build times

## Environment Variables

```bash
# CCache configuration
export CCACHE_DIR=$HOME/.ccache
export CCACHE_MAXSIZE=2G
export CCACHE_COMPRESS=1
export CCACHE_COMPRESSLEVEL=6

# Build optimization
export MAKEFLAGS="-j$(nproc)"
export CXXFLAGS="-O2 -g0 -pipe"
```
