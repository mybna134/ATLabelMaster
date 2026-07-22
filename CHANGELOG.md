# Changelog

All notable changes to ATLabelMaster will be documented in this file.

## [Unreleased]

### Fixed
- Show dataset scanning progress immediately instead of waiting for the filesystem model, keep the UI responsive, and reuse the initial scan during conversion.
- Corrected V4 to its 36-class encoding and V5 to its fixed 14-class encoding.

### Added - Filtering review mode
- Added an exact `color / size / class` combination selector with 64 independently selectable combinations.
- Move matching image/label pairs into sibling `filtering/images` and `filtering/labels` queues with a manifest for exact path restoration.
- Added a review dialog with detection overlays, brightness enhancement, restore, direct delete, resumable queues, and automatic exit when the filtering queue becomes empty.

### Added - LabelMaster V6
- Added automatic import for the 9-field UnionSecret format, including Small Base at class IDs 7/20/33 and Big Base at 8/21/34.
- Added explicit V1/UPC and UnionSecret/NWPU format choosers for ambiguous inputs; UnionSecret/NWPU IDs 39–63 identify NWPU automatically.
- Added resumable `stage/images` and `stage/labels` conflict handling for malformed class ranges and field counts, with lenient recovery of valid rows, source-format/V6 dual validation, automatic source-to-V6 conversion, and a warned force-merge escape hatch.
- Added the new 19-field LabelMaster V6 layout: `color size class bbox (x y visibility)[4]`.
- Renamed the former 17-field V6 / YOLO Pose layout to V5 and retained its 14-class reader for migration.
- Added per-keypoint visibility editing with `0=不可见`, `1=不在范围内`, and `2=可见`.
- Added editable bbox corner handles for bbox-capable output formats.
- Added direct, non-converting V6 opening and safe normalization of every supported legacy format to V6.
- Inputs without visibility information now normalize with all four keypoints set to `2`.
- Added strict line validation and atomic label replacement to prevent malformed imports from clearing labels.
- Added dataset-wide automatic format detection and removed manual input/output conversion controls.
- Added safe conflict rejection plus a title-bar `?` dialog describing every supported format.
- Allowed keypoints and bbox values outside the image range; negative/out-of-range label values are preserved during reading and conversion. Masks remain clipped to the image.
- Added spatial WASD Detector selection, direct color/class/size editing shortcuts, and a full-canvas crosshair that remains visible outside the displayed image.
- Added J/K/N/M keypoint-visibility shortcuts with single/double-press handling and hollow rendering for invisible or out-of-range selected keypoints.

### Added - Pixel Art UI System
- **Theme Manager** - Complete theme system with JSON configuration
- **Three Themes**
  - Retro Gaming - 8-bit/16-bit inspired pixel art style
  - Dark Modern - VSCode-style dark theme
  - Classic - Traditional Qt appearance
- **Theme Selector** - Switch themes from Settings dialog
- **Pixel Widgets**
  - PixelButton - Block-style buttons with sharp corners
  - PixelSlider - Pixel-styled slider with square handle
  - PixelCheckBox - Square checkboxes with blocky checkmark
  - PixelRadioButton - Square radio buttons
  - PixelDialog - Base class for pixel-styled dialogs
  - PixelCanvas - Image canvas with pixel-perfect rendering

### Added - Performance & Reliability
- **Image Cache** - LRU cache for efficient image loading
  - Configurable size limits (default: 100MB, 100 images)
  - Thread-safe implementation
  - Cache statistics tracking
- **Crash Handler** - Automatic crash recovery
  - Signal handlers for Unix systems
  - Stack trace capture
  - State persistence for recovery
  - AutoSaveGuard for automatic saves

### Added - User Experience
- **Keyboard Manager** - Customizable keyboard shortcuts
  - 18+ configurable actions
  - Persistence in QSettings
  - Easy reset to defaults
- **Installer Scripts** - Cross-platform installation
  - Universal installer for all Linux distributions
  - Automatic dependency detection
  - Clean uninstall script

### Added - Build System
- **CI/CD Pipeline** - GitHub Actions workflows
  - Build on Ubuntu 22.04, Arch Linux, Fedora 40
  - Automated testing
  - Automated package building on tags
- **Package Builders**
  - Arch Linux PKGBUILD
  - Debian/Ubuntu build script
  - RPM spec file for Fedora/RHEL
  - Unified build script for all packages

### Changed
- Updated CMakeLists.txt to install theme assets
- Enhanced AppSettings with theme persistence
- Updated main.cpp to initialize theme system
- Added theme selector UI in Settings dialog

### Fixed
- Build issues with various Qt6 versions
- Compatibility with GCC 11+ and Clang 13+

### Technical Details
- **New Files**: 30+ new source files
- **Lines of Code**: ~3,000+ new lines
- **Themes**: JSON-based with 20+ color definitions each
- **Cache Size**: Configurable (default 100MB)
- **Supported Distributions**: Arch, Debian/Ubuntu, Fedora/RHEL

---

## [1.2.2] - 2024-01-XX

### Original Features
- AI-assisted armor plate annotation
- Batch label replacement
- Histogram equalization
- Smart detection with OpenVINO
- Multi-format export (RMML compatible)
