# Qt6 Plugins Optimization for Distribution Package

This document explains the Qt6 plugin selection strategy for the Cascoin Linux distribution package.

## Problem

Qt6 installs **hundreds of plugins** by default, including many specialized plugins for:
- Multiple database drivers (PostgreSQL, MySQL, SQLite, etc.)
- Various image formats (TIFF, WebP, HEIF, etc.)
- Different input methods and platforms
- Print drivers, audio codecs, multimedia formats
- Wayland/X11 specific integrations

Including all plugins would:
- **Increase package size** significantly (100+ MB extra)
- **Slow down** plugin loading
- **Create unnecessary dependencies** on libraries not needed by Cascoin

## Solution: Essential Plugins Only

### Selected Plugin Categories

#### 1. Platform Plugins (`platforms/`)
**Purpose**: Core display system integration
- `libqxcb.so` - **X11 support** (primary Linux desktop)
- `libqwayland*.so` - **Wayland support** (modern Linux desktop)
- `libqminimal.so` - **Headless/fallback** support

#### 2. Image Format Plugins (`imageformats/`)
**Purpose**: Logo, icons, QR codes display
- `libqjpeg.so` - **JPEG images** (common format)
- `libqpng.so` - **PNG images** (icons, logos)
- `libqsvg.so` - **SVG images** (scalable icons)
- `libqico.so` - **ICO files** (Windows compatibility)

#### 3. Icon Engine Plugins (`iconengines/`)
**Purpose**: System icon rendering
- `libqsvgicon.so` - **SVG icon rendering** (modern UI)

#### 4. Platform Theme Plugins (`platformthemes/`)
**Purpose**: Desktop environment integration
- `libqgtk3.so` - **GTK3 theme integration** (GNOME, etc.)
- `libqxdgdesktopportal.so` - **XDG Desktop Portal** (sandboxed environments)

### Excluded Plugin Categories

#### Not Included (Save Space):
- **SQL Drivers**: Cascoin uses Berkeley DB, not SQL databases
- **Print Support**: Cryptocurrency wallet doesn't need printing
- **Audio/Video**: No multimedia functionality required
- **Input Methods**: Standard keyboard/mouse sufficient
- **Additional Image Formats**: TIFF, WebP, HEIF not needed
- **Legacy Platforms**: EGL, Android, iOS specific plugins
- **Development Tools**: Designer plugins not needed in runtime

## Size Comparison

| Plugin Set | File Count | Approximate Size |
|------------|------------|------------------|
| All Qt6 Plugins | 150+ files | ~120 MB |
| Essential Only | 12 files | ~8 MB |
| **Savings** | **93% fewer files** | **93% smaller** |

## Runtime Benefits

### Faster Startup
- **Reduced plugin scanning** time
- **Fewer dependency checks** during Qt initialization
- **Lower memory footprint** from unused plugins

### Better Compatibility
- **Minimal dependencies** reduce chances of library conflicts
- **Essential-only** approach ensures core functionality works
- **Cross-distribution** compatibility improved

## Implementation

### GitHub Actions (`.github/workflows/build.yml`)
```bash
# Copy only essential Qt6 plugins
mkdir -p "$DIST_DIR"/usr/lib/qt6/plugins/platforms
cp /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/libqxcb.so "$DIST_DIR"/usr/lib/qt6/plugins/platforms/
# ... (essential plugins only)
```

### Local Build (`contrib/ci/build-distribution-local.sh`)
```bash
# Essential platform plugins
mkdir -p "$DIST_DIR/usr/lib/qt6/plugins/platforms"
cp /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms/libqxcb.so "$DIST_DIR/usr/lib/qt6/plugins/platforms/"
# ... (essential plugins only)
```

## Testing

To verify the plugin selection works:

```bash
# Test Qt6 plugin loading
QT_DEBUG_PLUGINS=1 ./cascoin-qt-wrapper

# Check available image formats
./cascoin-qt-wrapper -platform minimal -quit
```

## Future Considerations

### If Additional Formats Needed:
- **Database**: Add SQLite plugin if needed for future features
- **Images**: Add WebP plugin for modern image support
- **Printing**: Add CUPS plugin if transaction/receipt printing needed

### Platform-Specific:
- **ARM64**: Same plugin selection should work
- **Older Systems**: May need additional compatibility plugins
- **Embedded**: Could reduce further to platform+minimal only

## Debugging Plugin Issues

### Common Problems:
1. **"Platform plugin not found"** → Check `platforms/` directory
2. **"Image format not supported"** → Add to `imageformats/`
3. **"Theme not applied"** → Check `platformthemes/`

### Environment Variables:
```bash
export QT_DEBUG_PLUGINS=1          # Debug plugin loading
export QT_PLUGIN_PATH="/usr/lib/qt6/plugins"  # Plugin path
export QT_QPA_PLATFORM="xcb"       # Force X11 platform
```

This optimization reduces the distribution package size significantly while maintaining full GUI functionality for Cascoin Core.
