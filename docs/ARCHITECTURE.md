# ATLabelMaster Architecture Documentation

## Overview

ATLabelMaster is a Qt-based application for annotating armor plates in RoboMaster competition images. The application follows a Model-View-Controller (MVC) pattern with signal/slot communication.

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│                        UI Layer                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ MainWindow   │  │ ImageCanvas  │  │  Settings    │    │
│  │  (Coordinator)│  │  (Viewer)    │  │   Dialog     │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │ signals/slots
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     Service Layer                            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ FileService  │  │SmartDetector │  │Application   │    │
│  │  (I/O Mgr)   │  │ (AI/CV)      │  │   Wiring     │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
                            ▲
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                      Utility Layer                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │SvgConstants  │  │ColorMapper   │  │ ImageCache   │    │
│  │Result<T>     │  │IdConvert     │  │CrashHandler  │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Component Overview

### UI Layer

#### MainWindow
- **Responsibility**: Application coordinator and main window
- **Key Signals**:
  - `sigOpenFolderRequested` - Open image folder dialog
  - `sigFileActivated(QModelIndex)` - User selected a file
  - `sigNextRequested` / `sigPrevRequested` - Navigation
  - `sigDeleteRequested` - Delete current file
- **Key Slots**:
  - `setFileModel(QAbstractItemModel*)` - Set file tree model
  - `showImage(const QImage&)` - Display image in canvas
  - `setStatus(const QString&, int)` - Show status message

#### ImageCanvas
- **Responsibility**: Image display and annotation editing
- **Sub-components** (after refactoring):
  - `ImageRenderer` - Coordinate transforms, scaling, panning
  - `AnnotationManager` - Armor data CRUD
  - `InteractionHandler` - Mouse/keyboard event handling
  - `SvgOverlayRenderer` - SVG template rendering
  - `MaskManager` - Mask region management
- **Key Signals**:
  - `detectRequested(const QImage&)` - Request AI detection
  - `annotationCommitted(const Armor&)` - User finished creating annotation
  - `annotationsPublished(...)` - Request save annotations
- **Key Slots**:
  - `setDetections(const QVector<Armor>&)` - Load annotations
  - `histEqualize()` - Apply histogram equalization

### Service Layer

#### FileService
- **Responsibility**: File system operations and label I/O
- **Sub-components** (after refactoring):
  - `FileManager` - Directory navigation
  - `LabelIO` - Label file read/write
  - `DatasetFormatHandler` - Format abstraction
  - `BatchProcessor` - Batch operations
- **Key Signals**:
  - `modelReady(QAbstractItemModel*)` - File tree ready
  - `imageReady(const QImage&)` - Image loaded
  - `labelsLoaded(const QVector<Armor>&)` - Annotations loaded
  - `status(const QString&, int)` - Status message
- **Key Slots**:
  - `openFolderDialog(DataSet)` - Open folder
  - `openIndex(QModelIndex)` - Open specific file
  - `saveData(...)` - Save annotations
  - `forceMergeCurrentConflict()` - Return a staged sample without source/V6 validation after warning

- **Import conflict state**:
  - Malformed samples are moved to `<dataset>/stage/images` and `<dataset>/stage/labels`.
  - `<dataset>/stage/manifest.json` persists source format and original paths across restarts.
  - Stage entry first checks both the persisted source format and LabelMaster V6. Valid V6 files return unchanged; valid source files convert to V6 and return; only files matching neither format enter manual editing.
  - Conflict-mode saves write and re-read LabelMaster V6 before returning a manually repaired sample.
  - When stage becomes empty, `FileService` automatically reopens the original image directory.

#### SmartDetector
- **Responsibility**: AI and traditional CV detection
- **Modes**:
  - `Traditional` - OpenCV-based detection
  - `AI` - OpenVINO YOLO model
- **Key Signals**:
  - `detected(const QVector<Armor>&)` - Detection results
  - `debugImages(...)` - Debug visualization
  - `error(const QString&)` - Detection error

#### ApplicationWiring
- **Responsibility**: Centralize signal/slot connections
- **Purpose**: Makes wiring explicit and testable
- **Methods**:
  - `wire()` - Establish all connections
  - `connectMainWindowToFileService()` - UI→Service connections
  - `connectFileServiceToMainWindow()` - Service→UI connections
  - etc.

### Utility Layer

#### SvgConstants
- **Purpose**: Centralized SVG template data
- **Data**:
  - Small armor: 557×516, anchors at specific positions
  - Big armor: 871×478, anchors at specific positions
- **Usage**: Perspective transformation for bbox calculation

#### ColorMapper
- **Purpose**: Color-to-class mapping
- **Mapping**:
  - 'R' → Red (255, 70, 70)
  - 'B' → Blue (61, 165, 255)
  - 'G' → Gray (170, 170, 180)
  - 'P' → Pink (255, 192, 203)

#### ImageCache
- **Purpose**: LRU cache for loaded images
- **Thread Safety**: Mutex-protected
- **Eviction**: LRU policy

#### CrashHandler
- **Purpose**: Crash recovery and logging
- **Mechanism**: Lock file + state JSON
- **Thread Safety**: `std::call_once` for singleton

## Signal/Slot Flow Diagrams

### Opening an Image Folder

```
User → MainWindow.sigOpenFolderRequested
         ↓
       FileService.openFolderDialog()
         ↓
       FileService.openDir()
         ↓
       FileService.modelReady(QAbstractItemModel*)
         ↓
       MainWindow.setFileModel()
         ↓
       FileService.tryOpenFirstAfterLoaded()
         ↓
       FileService.imageReady(QImage)
         ↓
       MainWindow.showImage()
         ↓
       ImageCanvas.setImage()
```

### Creating an Annotation

```
User (drag on canvas)
         ↓
    ImageCanvas.mousePressEvent()
         ↓
    ImageCanvas.mouseMoveEvent() [update drag]
         ↓
    ImageCanvas.mouseReleaseEvent()
         ↓
    ImageCanvas.annotationCommitted(Armor)
         ↓
    ImageCanvas.annotationsPublished(armors, image, needSaveImg)
         ↓
    FileService.saveData()
         ↓
    LabelIO.writeLabelFile()
         ↓
    FileService.status("已保存标注")
```

### AI Detection Flow

```
User clicks "Smart Detect"
         ↓
    ImageCanvas.detectRequested(QImage)
         ↓
    SmartDetector.detect()
         ↓
    [AI Model / OpenCV Detection]
         ↓
    SmartDetector.detected(QVector<Armor>)
         ↓
    ImageCanvas.setDetections()
         ↓
    [Update display]
```

## Data Format

### Label File Format

#### LabelMasterV6 Format (19 fields, default)
```
color size class center_x center_y width height x0 y0 v0 x1 y1 v1 x2 y2 v2 x3 y3 v3
```
- Bounding boxes and keypoints use unbounded finite normalized coordinates; bbox width/height remain non-negative
- Point order is `TL, BL, BR, TR`
- Per-keypoint visibility is `0=invisible`, `1=out of bounds`, or `2=visible`
- `color`, `size`, and `class` use the native V2/V3 definitions
- V6 is the only output format that exposes per-keypoint visibility controls
- Bbox-capable formats expose editable square bbox handles
- Dataset format is detected from every non-empty label file; ambiguous encodings require a user choice, while per-file violations of the selected format are isolated in `stage`
- V6 is opened directly; V1/V2/V3/V4/V5, HITSZ, UPC, UnionSecret, and NWPU are normalized to V6
- Formats without visibility fields receive visibility `2` for all four keypoints

### Filtering review workflow

- The Tools menu opens a hierarchical checkbox selector whose leaves represent exact combinations of 4 colors, 2 sizes, and 8 classes.
- An image is selected when at least one of its V6 annotations matches a checked combination.
- Matching pairs are moved to `<image-parent>/filtering/images` and `filtering/labels`; `manifest.json` preserves their exact original paths.
- A modal review dialog reuses `ImageCanvas` for annotation overlays and brightness enhancement. Restore moves both files back, while delete removes both files directly.
- The manifest is updated after each operation. Empty queues remove the empty filtering directories and close the review dialog automatically; non-empty queues can be resumed later.

#### LabelMasterV5 Format (17 fields, legacy)
```
class_id center_x center_y width height x0 y0 v0 x1 y1 v1 x2 y2 v2 x3 y3 v3
```
- This is the former V6 / YOLO Pose layout and uses the fixed 14-class encoding (`class_id` 0–13).
- It is read only for migration and is converted to V6

#### LabelMaster2 Format (11 fields)
```
color size cls x0 y0 x1 y1 x2 y2 x3 y3
```
- Corner points use normalized coordinates and may lie outside [0, 1]
- `x0,y0,x1,y1,x2,y2,x3,y3`: Four corner points (TL, BL, BR, TR)

#### LabelMasterV4 Format (13 fields)
```
cls x_c y_c w h x0 y0 x1 y1 x2 y2 x3 y3
```
- `cls`: Numeric V4 36-class id encoded as `color * 9 + tag`
- `x_c,y_c,w,h`: Normalized bounding box; center/extent may cross the image boundary
- `x0,y0,x1,y1,x2,y2,x3,y3`: Normalized four corner points; out-of-image values are preserved
- `color = cls / 9`; within each color, tags are `G/Big 1/2/3/4/5/O/Small Base/Big Base`

#### Extended Format (15 fields)
```
color size cls x y w h x0 y0 x1 y1 x2 y2 x3 y3
```
- Adds bounding box: `x,y,w,h` (center x, center y, width, height)
- Bounding boxes and corner points may cross the image boundary

### Armor Structure
```cpp
struct Armor {
    QString cls;      // Internal class token: G/1/2/3/4/5/O/B
    QString color;    // Color letter (R/B/G/P)
    int size;         // 0=small, 1=big
    std::array<int, 4> keypointVisibility;  // V5/V6 TL/BL/BR/TR; values 0/1/2
    QPointF p0, p1, p2, p3;  // Corner points (TL, BL, BR, TR)
    double norm_x, norm_y, norm_w, norm_h;  // Normalized bbox (optional)
};
```

## Coordinate Systems

### Image Coordinates
- Origin: Top-left of image
- Units: Pixels
- Used for: Storage, calculations

### Widget Coordinates
- Origin: Top-left of ImageCanvas widget
- Units: Pixels
- Used for: Rendering, mouse input

### Normalized Coordinates
- Bbox and keypoint positions are unbounded finite normalized values; bbox width/height are non-negative
- Used for: Label file storage
- Transform: `normalized = image_pixel / image_size`

### SVG Coordinates
- Origin: Top-left of SVG template
- Used for: Perspective transformation

## Key Algorithms

### Perspective Transformation (SVG → Image)

1. Get SVG template (small or big armor)
2. Get SVG anchor points
3. Get image anchor points (from user annotations)
4. Compute `QTransform::quadToQuad(svg_anchors, img_anchors)`
5. Transform SVG bounding box to image space

### BBox Calculation from Four Points

1. Transform SVG corners using perspective transform
2. Find min/max of transformed corners
3. Calculate center and dimensions

### Point-in-Polygon Test

```cpp
bool pointInsidePoly(const QPolygonF& poly, const QPointF& point) {
    return poly.containsPoint(point, Qt::OddEvenFill);
}
```

## Thread Safety

### ImageCache
- Protected by `QMutex mutex_`
- All public methods use `QMutexLocker`

### CrashHandler
- Singleton using `std::call_once`
- Signal handler is async-signal-safe

### FileService
- Operations are sequential (single-threaded UI context)
- No explicit mutex needed

## Build System

### CMake Structure
```
CMakeLists.txt
├── GLOB_RECURSE for all .cpp/.hpp/.ui files
├── Qt6::Widgets, Core, Gui, Svg
├── OpenCV, OpenVINO, Eigen3
└── AUTOMOC, AUTOUIC enabled
```

### Adding New Files
Due to `GLOB_RECURSE`, new files are automatically picked up after:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

## Refactoring Progress

### Phase 1: Bug Fixes ✅
- Fixed "BIg" typo
- Fixed file handle leaks
- Fixed CrashHandler race condition
- Simplified signal handler

### Phase 2: Code Quality ✅
- Created `SvgConstants` class
- Created `ColorMapper` class
- Created `IDetector` interface
- Created `Result<T>` error handling

### Phase 3: UX Improvements ✅
- Increased status message duration to 3000ms
- Verified dialog lifecycle management

### Phase 4: Architecture Refactoring ✅
- Created `ApplicationWiring` class ✅
- Created ImageCanvas sub-components ✅
- Created `DatasetFormatHandler` hierarchy ✅
- Created `LabelIO` class (partial)

### Phase 5: Documentation & Testing (In Progress)
- Architecture documentation ✅ (this file)
- Unit tests
- Integration tests

### Recent Additions
- Added `LabelMasterV4` format support (13 fields: cls x_c y_c w h pts[4,2]) ✅

## Future Work

1. Complete ImageCanvas refactoring (integrate sub-components)
2. Complete FileService refactoring (integrate LabelIO)
3. Add mode indicator for modifier keys (Alt/Shift/Ctrl)
4. Implement undo/redo with Command pattern
5. Add comprehensive unit tests
6. Add integration tests
7. Performance profiling
8. Memory leak detection with valgrind
