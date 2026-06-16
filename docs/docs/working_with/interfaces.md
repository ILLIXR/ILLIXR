# ILLIXR Bridge Data Type YAML Reference

This document describes how to write data type description files for the
ILLIXR Python bridge interface.  These files live in `interfaces/data/` and
are referenced by bridge descriptor files (`interfaces/python/bridges/*.yaml`).
Each file defines a single C++ struct that the bridge system generates at
cmake configure time.

---

## Table of Contents

- [Overview](#overview)
- [File Location and Naming](#file-location-and-naming)
- [Namespacing with Subdirectories](#namespacing-with-subdirectories)
- [Top-Level Structure](#top-level-structure)
- [Scalar Types](#scalar-types)
- [Fixed-Size Array Fields](#fixed-size-array-fields)
- [Container Fields](#container-fields)
- [OpenCV Mat Fields](#opencv-mat-fields)
- [Nested Struct Fields](#nested-struct-fields)
- [Serialization](#serialization)
- [Allowed and Disallowed Combinations](#allowed-and-disallowed-combinations)
- [Complete Example](#complete-example)
- [Using Generated Headers in C++ Plugins](#using-generated-headers-in-c-plugins)
- [Python-Side Usage](#python-side-usage)
    - [Bridge-Defined Types](#bridge-defined-types)
    - [ILLIXR System Types](#illixr-system-types)

---

## Overview

There are two categories of data types available to Python bridge scripts:

**Bridge-defined types** are described in YAML files in `interfaces/data/`.
The cmake configure step generates a C++ struct header and pybind11 bindings
for each one.  All bridge-defined types live in the `ILLIXR::bridge` C++
namespace (with additional nesting for subdirectories) and are imported in
Python under `illixr.bridge`.

**ILLIXR system types** are the existing C++ structs in
`include/illixr/data_format/`.  The script `generate_system_bindings.py` is
run manually to produce pre-generated pybind11 bindings checked into git under
`interfaces/python/system_bindings/`.  These are compiled into every bridge
plugin automatically and are accessible in Python under `illixr` (without the
`bridge` sub-package).

The `illixr.bridge` and `illixr` namespaces are strictly separate, so there
is no risk of name collision between bridge-defined and system types.

---

## File Location and Naming

```
interfaces/
└── data/
    ├── camera_intrinsics.yaml        # root-level type
    ├── sensor_frame.yaml
    └── geometry/
        └── camera_intrinsics.yaml   # same stem, different namespace
```

- Files must be placed in `interfaces/data/` or a subdirectory of it.
- **The struct name is derived from the filename stem.**  A file named
  `sensor_frame.yaml` defines a struct named `sensor_frame`.  There is no
  `name:` key inside the file — including one is an error.
- Every filename and subdirectory name must be lowercase snake_case: letters,
  digits, and underscores only, starting with a letter.
- Two files in different directories may share the same stem without conflict
  because each gains a distinct namespace from its path.

---

## Namespacing with Subdirectories

The directory path under `interfaces/data/` maps directly and consistently
to C++ namespaces, Python import paths, generated header paths, and
serialization macro names.  The root of the mapping is always `ILLIXR::bridge`
in C++ and `illixr.bridge` in Python.

| File path | Dotted name (in YAML) | C++ qualified name | Python import | Generated header |
|---|---|---|---|---|
| `interfaces/data/camera_intrinsics.yaml` | `camera_intrinsics` | `ILLIXR::bridge::camera_intrinsics` | `illixr.bridge.camera_intrinsics` | `illixr/bridge/camera_intrinsics.hpp` |
| `interfaces/data/geometry/camera_intrinsics.yaml` | `geometry.camera_intrinsics` | `ILLIXR::bridge::geometry::camera_intrinsics` | `illixr.bridge.geometry.camera_intrinsics` | `illixr/bridge/geometry/camera_intrinsics.hpp` |
| `interfaces/data/pose/fast_pose.yaml` | `pose.fast_pose` | `ILLIXR::bridge::pose::fast_pose` | `illixr.bridge.pose.fast_pose` | `illixr/bridge/pose/fast_pose.hpp` |
| `interfaces/data/geometry/shapes/point.yaml` | `geometry.shapes.point` | `ILLIXR::bridge::geometry::shapes::point` | `illixr.bridge.geometry.shapes.point` | `illixr/bridge/geometry/shapes/point.hpp` |

Nesting depth is unlimited.  Every directory level adds one C++ namespace and
one Python sub-package level.

### Dotted names in bridge descriptor files

Types are referenced in bridge descriptor YAML using their dotted name.  The
resolution is exact — no fallback search is performed.

```yaml
# interfaces/python/bridges/my_bridge.yaml
types:
  - camera_intrinsics             # must exist at interfaces/data/camera_intrinsics.yaml
  - geometry.camera_intrinsics    # must exist at interfaces/data/geometry/camera_intrinsics.yaml

inputs:
  - topic: frames
    type: sensor_frame            # root-level sensor_frame
    alias: frames

  - topic: geo_frames
    type: geometry.sensor_frame   # geometry-namespaced sensor_frame
    alias: geo_frames
```

If a bare name like `camera_intrinsics` is used but no
`interfaces/data/camera_intrinsics.yaml` exists at the root level, the
generator produces a `FATAL_ERROR` at configure time — it does **not** search
subdirectories for a match.

---

## Top-Level Structure

Every data type file has exactly one required top-level key:

```yaml
# interfaces/data/sensor_frame.yaml
# Struct name: sensor_frame  (from filename)
# C++ type:    ILLIXR::bridge::sensor_frame
# Python:      illixr.bridge.sensor_frame

fields:
  field_one:
    type: int
  field_two:
    type: float
```

```yaml
# interfaces/data/geometry/sensor_frame.yaml
# Struct name: sensor_frame  (same stem, different namespace)
# C++ type:    ILLIXR::bridge::geometry::sensor_frame
# Python:      illixr.bridge.geometry.sensor_frame

fields:
  field_one:
    type: int
  field_two:
    type: float
```

| Key      | Required | Description |
|----------|----------|-------------|
| `name`   | **forbidden** | Must not be present. The struct name and namespace come from the file path. |
| `fields` | yes      | Non-empty mapping of field name to field definition. |

Field names must be lowercase snake_case.

---

## Scalar Types

A scalar field has a `type` key and no `container`, `shape`, or `channels` key.

```yaml
fields:
  frame_number:
    type: int

  confidence:
    type: float

  label:
    type: string
```

The following scalar types are supported.  Aliases are accepted interchangeably.

| YAML type  | Aliases        | C++ type       | Python / numpy type |
|------------|----------------|----------------|---------------------|
| `int8`     |                | `int8_t`       | `np.int8`           |
| `int16`    |                | `int16_t`      | `np.int16`          |
| `int`      | `int32`        | `int32_t`      | `np.int32`          |
| `int64`    |                | `int64_t`      | `np.int64`          |
| `uint8`    | `byte`, `char` | `uint8_t`      | `np.uint8`          |
| `uint16`   |                | `uint16_t`     | `np.uint16`         |
| `uint32`   |                | `uint32_t`     | `np.uint32`         |
| `uint64`   |                | `uint64_t`     | `np.uint64`         |
| `float`    | `float32`      | `float`        | `np.float32`        |
| `double`   | `float64`      | `double`       | `np.float64`        |
| `bool`     |                | `bool`         | `bool`              |
| `string`   | `str`          | `std::string`  | `str`               |

---

## Fixed-Size Array Fields

A field with a `shape` key becomes a C-style fixed-size array.  Shape must be
a list of one or two positive integer literals.  Three or higher dimensions are
not supported.

```yaml
fields:
  intrinsics:
    type: float
    shape: [4]          # → float intrinsics_[4]

  projection_matrix:
    type: float64
    shape: [4, 4]       # → double projection_matrix_[4][4]
```

On the Python side, 1D and 2D fixed arrays of scalar types are exposed as numpy
arrays (zero-copy read, copy on write).

**Restrictions on `shape`:**

- `shape` and `container` are mutually exclusive on the same field.
- `string` and `bool` may not be used with `shape`.  Use `vector` instead.
- Two-dimensional shape is not allowed for nested struct types.
  For nested struct 1D arrays see [Nested Struct Fields](#nested-struct-fields).

---

## Container Fields

A field with a `container` key becomes a dynamically-sized collection.  The
`container` and `shape` keys are mutually exclusive.

### Vector (dynamic list)

```yaml
fields:
  keypoint_scores:
    type: float
    container: vector   # std::vector<float>

  tags:
    type: string
    container: list     # alias for vector; std::vector<std::string>
```

`vector` and `list` are interchangeable aliases.  The C++ type is
`std::vector<T>`.  On the Python side this is a plain `list`.

### Dict (string-keyed map)

```yaml
fields:
  metadata:
    type: string
    container: dict     # std::unordered_map<std::string, std::string>
```

`dict` and `map` are interchangeable aliases.  Keys are always `std::string`.
The C++ type is `std::unordered_map<std::string, T>`.  On the Python side this
is a plain `dict`.

**Restrictions on containers:**

| Combination            | Allowed | Reason |
|------------------------|---------|--------|
| `vector<bool>`         | **no**  | `std::vector<bool>` is a bitfield specialisation; use `vector<uint8>` instead |
| `dict<bool>`           | **no**  | Same underlying concern as `vector<bool>` |
| `dict<uint8..uint64>`  | **no**  | Small integer dict values are almost always a design error; use `vector` |
| `vector` of `mat_*`    | **no**  | Use multiple named fields instead |
| `dict` of `mat_*`      | **no**  | `cv::Mat` cannot be a dict value |
| Nested containers      | **no**  | `vector<vector<T>>`, `dict<dict<T>>`, etc. are not supported |

---

## OpenCV Mat Fields

A field with a `mat_*` type holds a `cv::Mat`.  The `channels` key is required
and must be an integer from 1 to 4.

```yaml
fields:
  color_frame:
    type: mat_8u
    channels: 3       # CV_8UC3; numpy shape (H, W, 3), dtype uint8

  depth_frame:
    type: mat_32f
    channels: 1       # CV_32FC1; numpy shape (H, W),    dtype float32

  ir_frame:
    type: mat_16u
    channels: 1       # CV_16UC1; numpy shape (H, W),    dtype uint16
```

Supported mat types:

| YAML type  | OpenCV depth | C++ element type | numpy dtype  |
|------------|--------------|------------------|--------------|
| `mat_8u`   | `CV_8U`      | `uint8_t`        | `np.uint8`   |
| `mat_8s`   | `CV_8S`      | `int8_t`         | `np.int8`    |
| `mat_16u`  | `CV_16U`     | `uint16_t`       | `np.uint16`  |
| `mat_16s`  | `CV_16S`     | `int16_t`        | `np.int16`   |
| `mat_32s`  | `CV_32S`     | `int32_t`        | `np.int32`   |
| `mat_32f`  | `CV_32F`     | `float`          | `np.float32` |
| `mat_64f`  | `CV_64F`     | `double`         | `np.float64` |

On the Python side, `mat_*` fields are exposed as numpy arrays.  The getter
returns a zero-copy view backed by the `cv::Mat` data; the setter accepts a
numpy array of matching dtype and copies it into the `cv::Mat`.

**Restrictions on `mat_*` fields:**

- `shape` is not valid with `mat_*` types; the Mat encodes its own dimensions.
- `container` is not valid with `mat_*` types.
- `channels` is required and must be 1, 2, 3, or 4.

---

## Nested Struct Fields

A field may use another bridge-defined struct as its type.  The type is
specified using its **dotted name**, which must match the file's path relative
to `interfaces/data/`.  The dependency must be listed before the dependent type
in the bridge descriptor's `types:` list.

```yaml
# interfaces/data/sensor_frame.yaml

fields:
  # Root-level struct — bare name
  color_intrinsics:
    type: camera_intrinsics
    # C++: ILLIXR::bridge::camera_intrinsics color_intrinsics_

  # Geometry-namespaced struct — dotted name
  geo_intrinsics:
    type: geometry.camera_intrinsics
    # C++: ILLIXR::bridge::geometry::camera_intrinsics geo_intrinsics_

  # 1D fixed array of a namespaced struct
  camera_rig:
    type: geometry.camera_intrinsics
    shape: [4]
    # C++: ILLIXR::bridge::geometry::camera_intrinsics camera_rig_[4]

  # Dynamic list of a root struct
  detected_cameras:
    type: camera_intrinsics
    container: vector
    # C++: std::vector<ILLIXR::bridge::camera_intrinsics>
```

On the Python side:

- A plain nested struct is a Python object with the same attribute names as the
  nested type.
- A 1D struct array is a Python `list` of objects.
- A `vector` of structs is a Python `list` of objects.

**Restrictions on nested struct fields:**

| Combination | Allowed | Reason |
|---|---|---|
| 1D `shape` | **yes** | Fixed-length list on the Python side |
| 2D `shape` | **no**  | No clean numpy/Python representation |
| `container: vector` | **yes** | Dynamic list on the Python side |
| `container: dict` | **no**  | Unclear Python semantics |
| `channels` | **no**  | Only valid for `mat_*` types |
| Self-referential or cyclic structs | **no**  | Generator cannot resolve include order |
| Bare name that has no root-level yaml | **no**  | Generator does not search subdirectories; use the full dotted name |
| Struct shared across plugin `.so` boundaries via switchboard | **no** | Each plugin compiles its own copy; types are distinct at link time.  Promote to a proper ILLIXR data format header if sharing is needed. |

The bridge descriptor's `types:` list must name dependencies before dependents
(e.g. `camera_intrinsics` before `sensor_frame`, or
`geometry.camera_intrinsics` before `geometry.sensor_frame`).

---

## Serialization

Serialization code is always generated inside the struct header, guarded by an
`#ifdef`.  The macro name is derived from the dotted type name with dots
replaced by underscores and the whole name uppercased:

| Dotted name | Serialization macro |
|---|---|
| `sensor_frame` | `ILLIXR_SERIALIZE_SENSOR_FRAME` |
| `geometry.camera_intrinsics` | `ILLIXR_SERIALIZE_GEOMETRY_CAMERA_INTRINSICS` |
| `pose.fast_pose` | `ILLIXR_SERIALIZE_POSE_FAST_POSE` |

```cpp
#ifdef ILLIXR_SERIALIZE_GEOMETRY_CAMERA_INTRINSICS
    template<typename Archive>
    void serialize(Archive& ar_, const unsigned int) { ... }
    friend class boost::serialization::access;
#endif
```

The define is activated per output topic in the bridge descriptor:

```yaml
# interfaces/python/bridges/my_bridge.yaml
outputs:
  - topic: processed_frames
    type: geometry.sensor_frame
    network: tcp    # activates ILLIXR_SERIALIZE_GEOMETRY_SENSOR_FRAME
```

Accepted `network` values: `tcp`, `TCP`, `udp`, `UDP`, `true`, `false` (default).

When serialization is activated for a type, it is also activated transitively
for all nested bridge-defined struct types it depends on.  Both
`BOOST_CLASS_EXPORT_KEY` and `BOOST_CLASS_EXPORT_IMPLEMENT` are placed in the
same header.  This is safe because each generated header is included only
within its own plugin's translation units.

---

## Allowed and Disallowed Combinations

The following table is a concise summary of every rule enforced by the generator.
Violations produce a CMake `FATAL_ERROR` at configure time.

### File and path rules

| Situation | Allowed |
|-----------|---------|
| `name:` key present in the file | **✗** struct name and namespace come from the file path only |
| Filename not lowercase snake_case | **✗** |
| Subdirectory name not lowercase snake_case | **✗** |
| Bare type name with no matching root-level yaml | **✗** generator does not search subdirectories |

### Field-level rules

| Field definition | Allowed |
|------------------|---------|
| Scalar with no `shape`, `container`, or `channels` | ✓ |
| Scalar with 1D `shape` | ✓ (except `bool` and `string`) |
| Scalar with 2D `shape` | ✓ (except `bool` and `string`) |
| Scalar with `container: vector` | ✓ (except `bool`) |
| Scalar with `container: dict` | ✓ (except `bool` and unsigned integer types) |
| `shape` and `container` on the same field | **✗** mutually exclusive |
| `mat_*` with `channels` 1–4 | ✓ |
| `mat_*` with `shape` | **✗** |
| `mat_*` with `container` | **✗** |
| `mat_*` without `channels` | **✗** |
| `channels` on a non-`mat_*` field | **✗** |
| Nested struct using dotted name, plain | ✓ |
| Nested struct using dotted name, 1D `shape` | ✓ |
| Nested struct using dotted name, 2D `shape` | **✗** |
| Nested struct with `container: vector` | ✓ |
| Nested struct with `container: dict` | **✗** |
| `vector<bool>` | **✗** use `vector<uint8>` |
| `dict` with `bool`, `uint8`–`uint64` values | **✗** |
| Nested containers (`vector<vector<T>>` etc.) | **✗** |
| Unknown type name | **✗** |
| Field name not lowercase snake_case | **✗** |

### Struct-level rules

| Situation | Allowed |
|-----------|---------|
| Inheriting from another bridge-defined struct | **✗** all structs inherit only from `switchboard::event` |
| Using another bridge-defined struct as a data member | ✓ (see Nested Struct Fields) |
| Self-referential or cyclic struct dependency | **✗** |
| Duplicate struct names in the same namespace | **✗** |
| Same stem in different namespaces | ✓ each gains a distinct C++ namespace from its path |
| Pointer members | **✗** not representable in YAML |

---

## Complete Example

The following shows a realistic type file using all supported field categories,
including fields that reference both a root-level and a namespaced version of
`camera_intrinsics`.

```yaml
# interfaces/data/sensor_frame.yaml
# C++ type: ILLIXR::bridge::sensor_frame
# Python:   illixr.bridge.sensor_frame

fields:
  # --- scalars ---
  frame_number:
    type: int

  timestamp:
    type: double

  valid:
    type: bool

  sensor_id:
    type: string

  # --- 1D fixed array ---
  distortion_coeffs:
    type: float
    shape: [5]

  # --- 2D fixed array ---
  projection_matrix:
    type: float
    shape: [4, 4]

  # --- dynamic containers ---
  keypoint_scores:
    type: float
    container: vector

  tags:
    type: string
    container: vector

  properties:
    type: string
    container: dict

  # --- OpenCV mats ---
  color_frame:
    type: mat_8u
    channels: 3

  depth_frame:
    type: mat_32f
    channels: 1

  # --- root-level nested struct ---
  color_intrinsics:
    type: camera_intrinsics
    # C++: ILLIXR::bridge::camera_intrinsics

  # --- geometry-namespaced nested struct ---
  geo_intrinsics:
    type: geometry.camera_intrinsics
    # C++: ILLIXR::bridge::geometry::camera_intrinsics

  # --- 1D array of a namespaced struct ---
  camera_rig:
    type: geometry.camera_intrinsics
    shape: [4]

  # --- dynamic list of a root struct ---
  detected_cameras:
    type: camera_intrinsics
    container: vector
```

The bridge descriptor for a plugin using this type must list all dependencies
before `sensor_frame`:

```yaml
types:
  - camera_intrinsics              # interfaces/data/camera_intrinsics.yaml
  - geometry.camera_intrinsics     # interfaces/data/geometry/camera_intrinsics.yaml
  - sensor_frame                   # interfaces/data/sensor_frame.yaml
```

---

## Using Generated Headers in C++ Plugins

Generated headers are placed under `${CMAKE_BINARY_DIR}/include/`, mirroring
the dotted namespace in the path:

| Dotted name | Generated header path |
|---|---|
| `sensor_frame` | `${CMAKE_BINARY_DIR}/include/illixr/bridge/sensor_frame.hpp` |
| `geometry.camera_intrinsics` | `${CMAKE_BINARY_DIR}/include/illixr/bridge/geometry/camera_intrinsics.hpp` |

To use a generated type in a pure C++ plugin, add the header to the plugin's
source list and add `${CMAKE_BINARY_DIR}/include` to its include directories:

**`plugins/my_plugin/CMakeLists.txt`**
```cmake
add_illixr_plugin(my_plugin
    SOURCES
        plugin.cpp
        ${CMAKE_BINARY_DIR}/include/illixr/bridge/sensor_frame.hpp
        ${CMAKE_BINARY_DIR}/include/illixr/bridge/geometry/camera_intrinsics.hpp
)

target_include_directories(plugin.my_plugin PRIVATE
    ${CMAKE_BINARY_DIR}/include
)
```

**`plugins/my_plugin/plugin.cpp`**
```cpp
#include "illixr/bridge/sensor_frame.hpp"
#include "illixr/bridge/geometry/camera_intrinsics.hpp"

// Use the types via their fully-qualified C++ names
void example() {
    ILLIXR::bridge::sensor_frame                  frame;
    ILLIXR::bridge::geometry::camera_intrinsics   geo_cam;
}
```

The `${CMAKE_BINARY_DIR}/include` path mirrors the convention used for
non-generated ILLIXR headers under `${CMAKE_SOURCE_DIR}/include`.

---

## Python-Side Usage

### Bridge-Defined Types

Bridge-defined types are imported under the `illixr.bridge` package.  The
import path mirrors the dotted name used in the YAML:

```python
import illixr.bridge.camera_intrinsics          as root_cam
import illixr.bridge.geometry.camera_intrinsics as geo_cam
import illixr.bridge.sensor_frame               as sf_types
```

The `inputs` and `outputs` dicts are provided automatically by the bridge.
Struct types must be imported explicitly only when constructing output objects.

```python
# scripts/my_bridge.py

import illixr.bridge.camera_intrinsics          as root_cam
import illixr.bridge.geometry.camera_intrinsics as geo_cam
import illixr.bridge.sensor_frame               as sf_types

def run(inputs, outputs):
    frame = inputs["frames"]()
    if frame is None:
        return

    # Scalar fields — C++ trailing underscore is preserved
    fn = frame.frame_number_
    ts = frame.timestamp_

    # numpy arrays — mat_* and fixed-shape arrays are zero-copy reads
    color = frame.color_frame_          # np.ndarray (H, W, 3) uint8
    depth = frame.depth_frame_          # np.ndarray (H, W)    float32
    proj  = frame.projection_matrix_    # np.ndarray (16,)     float32

    # Nested struct fields — fully distinct types despite sharing the name
    rx = frame.color_intrinsics_.fx_    # ILLIXR::bridge::camera_intrinsics
    gx = frame.geo_intrinsics_.fx_      # ILLIXR::bridge::geometry::camera_intrinsics

    # vector and dict fields
    scores = frame.keypoint_scores_     # list of float
    props  = frame.properties_          # dict str->str

    # Construct output using keyword arguments (all default to zero/empty)
    result = sf_types.sensor_frame(
        frame_number     = fn + 1,
        color_frame      = color,
        depth_frame      = depth,
        color_intrinsics = root_cam.camera_intrinsics(
            fx=525.0, fy=525.0, cx=320.0, cy=240.0,
            width=640, height=480),
        geo_intrinsics   = geo_cam.camera_intrinsics(
            fx=525.0, fy=525.0, cx=320.0, cy=240.0,
            width=640, height=480, skew=0.0),
    )

    outputs["frames_out"](result)
```

All C++ member names retain their trailing underscore on the Python side.

### ILLIXR System Types

ILLIXR system types (from `include/illixr/data_format/`) are accessed through
the `illixr` package (not `illixr.bridge`), which is compiled into every bridge
plugin automatically.

```python
import illixr.head_pose as head_pose
import illixr.pose      as pose_types
```

Importing `illixr.head_pose` automatically imports `illixr.pose` and
`illixr.fast_pose` first, mirroring the C++ `#include` chain so base classes
are always registered before derived classes.

```python
import illixr.head_pose as head_pose

def run(inputs, outputs):
    raw = inputs["pose_in"]()
    if raw is None:
        return

    # System struct field names have no trailing underscore
    x = raw.x
    y = raw.y

    out = head_pose.fast_head_pose_type(
        imu_bias_x=0.01,
        imu_bias_y=0.02,
        imu_bias_z=0.00,
    )
    outputs["pose_out"](out)
```

The complete list of available system types, their fields, and the required
import order is in `interfaces/python/system_bindings/system_types.json`.

#### Namespace summary

| Type category | C++ namespace | Python import prefix |
|---|---|---|
| Bridge-defined, root level | `ILLIXR::bridge` | `illixr.bridge` |
| Bridge-defined, subdirectory | `ILLIXR::bridge::<subdir>` | `illixr.bridge.<subdir>` |
| ILLIXR system types | `ILLIXR` | `illixr` |

#### Regenerating System Bindings

System bindings are pre-generated and checked into git.  Regenerate them
whenever `include/illixr/data_format/` headers change:

```sh
# Minimal — uses source include/ only
python3 interfaces/python/generate_system_bindings.py

# With cmake cache for auto-discovered dependency paths (Eigen, OpenCV, etc.)
python3 interfaces/python/generate_system_bindings.py --build-dir build/

# With an extra include path not found via the cache
python3 interfaces/python/generate_system_bindings.py \
    --build-dir build/ \
    --include-dir /usr/include/eigen3
```

The script derives all paths from its own location:

| Path | How it is determined |
|------|----------------------|
| Source root | Two directories above the script |
| `include/illixr/data_format/` | Relative to source root |
| `interfaces/python/system_bindings/` (output) | Sibling of the script directory |

After regenerating, commit the updated files in
`interfaces/python/system_bindings/` to git.
