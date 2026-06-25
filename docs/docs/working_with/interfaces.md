# ILLIXR Python Bridge YAML Schema

This document describes the YAML files used to define Python bridge plugins for the
ILLIXR XR runtime. The bridge generator (`generate_python_bridges.py`) reads these files
at CMake configure time and produces C++ plugin sources and struct headers.

---

## Directory structure

```
interfaces/
├── python/
│   ├── python_profiles.yaml          # master profile (hand-written)
│   ├── generate_python_bridges.py    # generator (do not edit generated files directly)
│   ├── bridges/
│   │   └── <bridge_name>.yaml        # one file per bridge (hand-written)
│   └── profiles/                     # auto-generated per-profile yamls (committed)
│       └── <profile_name>.yaml
└── data/
    └── <namespace>/                  # one directory per namespace
        └── <type_name>.yaml          # one file per struct type (hand-written)
```

---

## Master profile (`python_profiles.yaml`)

Defines named profiles, each listing the bridges it activates.

```yaml
my_xr:
  bridges: semantic_my_xr

offload:
  bridges: offload_frames, offload_response
```

A profile is selected at CMake configure time with `-DPYTHON_BRIDGE_PROFILE=my_xr.yaml`.

---

## Bridge descriptor (`bridges/<bridge_name>.yaml`)

Defines a single bridge plugin — one Python script, its inputs, and its outputs.
The filename stem is the bridge name.

```yaml
script: scripts/my_script.py      # path to the Python script (required)

inputs:
  - topic: semantic_data           # switchboard topic name
    type: semantic_xr.semantic_data  # dotted type name (namespace.struct)
    alias: semantic_data           # optional: Python global name suffix
                                   # defaults to the topic name

outputs:
  - topic: semantic_response
    type: semantic_xr.query_response
    network: tcp                   # optional: tcp or udp (omit for local)
    alias: semantic_response       # optional
```

The `alias` controls the name of the global injected into the Python script:

- Input alias `foo` -> `illixr_foo_reader`
- Output alias `bar` -> `illixr_bar_writer`

---

## Type definition (`data/<namespace>/<type_name>.yaml`)

Defines a C++ struct that is exchanged over the switchboard. The dotted name
`namespace.type_name` is derived from the directory path and filename.

```yaml
fields:
  field_name:
    type: <type>          # required
    # --- optional modifiers ---
    container: <container>
    shape: <shape>
    channels: <int>       # mat types only — see below
```

### Scalar types

| YAML `type`               | C++ type      | Python type |
|---------------------------|---------------|-------------|
| `int8`                    | `int8_t`      | `int`       |
| `int16`                   | `int16_t`     | `int`       |
| `int` / `int32`           | `int32_t`     | `int`       |
| `int64`                   | `int64_t`     | `int`       |
| `uint8` / `byte` / `char` | `uint8_t`     | `int`       |
| `uint16`                  | `uint16_t`    | `int`       |
| `uint32`                  | `uint32_t`    | `int`       |
| `uint64`                  | `uint64_t`    | `int`       |
| `float` / `float32`       | `float`       | `float`     |
| `double` / `float64`      | `double`      | `float`     |
| `bool`                    | `bool`        | `bool`      |
| `string` / `str`          | `std::string` | `str`       |

### Nested bridge-defined struct

```yaml
type: semantic_xr.point_cloud    # dotted name referencing another type YAML
```

The struct must be defined in `data/semantic_xr/point_cloud.yaml`. In Python
the field is a nested `dict` with the same keys as the nested struct.

### ILLIXR system types

Any struct defined in `include/illixr/data_format/` can be used directly
as a field type without a YAML definition. The generator automatically
discovers all available structs via a libclang scan at CMake configure time —
no manual registration is required. Use the bare struct name (without a namespace):

```yaml
fields:
  pose:
    type: combined_pose
  audio:
    type: audio_data
    container: vector
```

If libclang is not available, the scan is skipped and only bridge-defined
types are usable. Install `libclang-dev` and the `clang` Python package
to enable system type discovery.

---

## Field modifiers

### `container`

Makes the field a collection. The C++ type becomes `std::vector<T>` in all cases.
In Python the field is a `list`.

| YAML value     | C++ type                             |
|----------------|--------------------------------------|
| `vector`       | `std::vector<T>`                     |
| `list`         | `std::vector<T>`                     |
| `dict` / `map` | `std::unordered_map<std::string, T>` |

```yaml
fields:
  scores:
    type: float
    container: vector        # std::vector<float> in C++, list in Python

  tags:
    type: string
    container: dict          # std::unordered_map<std::string, std::string>
```

For a 1-D dynamically sized array use `container: vector` with no `shape`.
For multidimensional arrays use `shape` instead (see below).

### `shape` — fixed or dynamic multi-dimensional arrays

`shape` defines how a field is laid out as a multidimensional array.
It takes a list of one to three entries. Each entry is either:

- A **positive integer** — a compile-time constant dimension
- A **field name** (lowercase snake_case string) — a runtime dimension stored
  in a sibling integer field that is synthesized automatically

#### Fixed compile-time array (all integers, no `container`)

```yaml
fields:
  matrix:
    type: float
    shape: [3, 3]            # float matrix_[3][3] in C++; numpy (9,) in Python
  rotation:
    type: float
    shape: [3, 3, 3]         # float rotation_[3][3][3]; numpy (27,) in Python
  weights:
    type: float
    shape: [128]             # float weights_[128]; numpy (128,) in Python
```

Fixed arrays are serialized with Boost and presented to Python as a flat
1-D numpy array (the dimensions are unfolded).

#### Dynamic numpy array (with `container: vector` or any string dimension)

When `shape` is combined with `container: vector`, or when any dimension entry
is a field name, the field is stored as `std::vector<T>` and presented to
Python as a shaped numpy array. Dimension fields are synthesized automatically
as `int32_t` — you do not need to declare them separately.

```yaml
fields:
  # Fully dynamic — all three dimensions known only at runtime
  voxels:
    type: uint8
    shape: [dim_x, dim_y, dim_z]      # synthesizes int32_t dim_x_, dim_y_, dim_z_

  # Mixed — two runtime dims, one compile-time constant
  feature_map:
    type: float
    shape: [height, width, 512]       # synthesizes int32_t height_, width_

  # Fully fixed shape stored as a vector (unusual but valid)
  lut:
    type: float
    container: vector
    shape: [256, 3]                   # no fields synthesized
```

The numpy array is C-contiguous (row-major). The shape in Python matches the
YAML entry order: `shape: [dim_x, dim_y, dim_z]` -> numpy shape `(dim_x, dim_y, dim_z)`.

On the writer side (Python -> C++), pass a numpy array of the correct shape.
The dimension fields are populated automatically from `array.shape` and do not
need to be included in the Python dict.

### `type: image` — packed image arrays

`type: image` is a specialization for camera images stored as packed uint8 RGB/gray.
It always uses `shape: [width, height, channels]` where `channels` is 1, 2, or 3.

```yaml
fields:
  rgb:
    type: image
    shape: [image_width, image_height, 3]    # synthesizes int32_t image_width_, image_height_

  depth:
    type: image
    shape: [depth_width, depth_height, 1]    # single-channel
```

- C++ storage: `std::vector<uint8_t>`
- Dimension fields: synthesized as `int32_t` (or use integer literals for fixed sizes)
- Python numpy shape: **(height, width, channels)** — the standard image convention
  used by PIL, OpenCV, and matplotlib, regardless of YAML entry order
- Strides: C-contiguous `(width * channels, channels, 1)` — matches the output
  of `entry_to_numpy` and standard video decoder output

```python
frame = illixr_semantic_data_reader.get()
img = frame["rgb"]           # shape (H, W, 3), dtype uint8
Image.fromarray(img).save("frame.png")       # works directly
cv2.imshow("frame", img[:, :, ::-1])         # BGR swap for OpenCV
```

On the writer side, pass a `(H, W, ch)` numpy array. The width and height
fields are populated automatically from `array.shape`.

### `channels` — OpenCV Mat types

Mat types (`mat_8u`, `mat_16u`, `mat_32f`, `mat_64f`, etc.) store an OpenCV
`cv::Mat` and require a `channels` key (1-4).

```yaml
fields:
  frame:
    type: mat_8u
    channels: 3              # cv::Mat, 3-channel uint8
  depth:
    type: mat_32f
    channels: 1
```

| YAML `type`  | Element C++ type | numpy dtype |
|--------------|------------------|-------------|
| `mat_8u`     | `uint8_t`        | `uint8`     |
| `mat_8s`     | `int8_t`         | `int8`      |
| `mat_16u`    | `uint16_t`       | `uint16`    |
| `mat_16s`    | `int16_t`        | `int16`     |
| `mat_32s`    | `int32_t`        | `int32`     |
| `mat_32f`    | `float`          | `float32`   |
| `mat_64f`    | `double`         | `float64`   |

In Python, mat fields are presented as numpy arrays via a zero-copy capsule.
Empty mats are returned as `None`.

---

## Complete example

### `data/semantic_xr/point_cloud.yaml`

```yaml
fields:
  points:
    type: float
    container: list          # std::vector<float>; Python list
  centroid:
    type: float
    container: list
  num_points:
    type: int
```

### `data/semantic_xr/semantic_data.yaml`

```yaml
fields:
  image:
    type: image
    shape: [image_width, image_height, 3]   # synthesizes image_width_, image_height_
  intrinsics:
    type: float
    shape: [4]               # fixed 1-D array; numpy (4,)
  depth:
    type: float
    container: vector        # plain 1-D vector, size unknown at compile time
  depth_width:
    type: int
  depth_height:
    type: int
  frame_number:
    type: int
```

### `data/semantic_xr/query_response.yaml`

```yaml
fields:
  query_id:
    type: uint64
  point_clouds:
    type: semantic_xr.point_cloud
    container: list          # vector of nested bridge structs
  colors:
    type: float
    container: list
  num_point_clouds:
    type: int
  server_query_processing:
    type: float
  text_query:
    type: string
```

### `bridges/semantic_my_xr.yaml`

```yaml
script: scripts/semantic_xr.py

inputs:
  - topic: semantic_data
    type: semantic_xr.semantic_data
  - topic: semantic_query
    type: semantic_xr.voice_query
    alias: query

outputs:
  - topic: semantic_response
    type: semantic_xr.query_response
    network: tcp
```

---

## Python interface

The generator injects the following globals into the script's namespace:

```python
# --- Readers ---
# illixr_<alias>_reader.get() -> dict or None

frame = illixr_semantic_data_reader.get()
if frame is not None:
    img        = frame["image"]          # numpy (H, W, 3) uint8
    intrinsics = frame["intrinsics"]     # numpy (4,) float32
    depth      = frame["depth"]          # Python list of floats
    fn         = frame["frame_number"]   # int

# --- Writers ---
# illixr_<alias>_writer.put(dict)

illixr_semantic_response_writer.put({
    "query_id":                query_id,
    "point_clouds": [
        {
            "points":     [x, y, z, ...],   # flat list, 3 floats per point
            "centroid":   [cx, cy, cz],
            "num_points": n,
        }
    ],
    "colors":                  [0.8, 0.2, 0.2],
    "num_point_clouds":        1,
    "server_query_processing": 0.012,
    "text_query":              "chair",
})

# --- Subscribe ---
# illixr_subscribe(alias, callback)
# callback receives the same dict as .get()

def on_frame(d):
    process(d["image"])

illixr_subscribe("semantic_data", on_frame)
```

### Dict conventions

- Field names in dicts match the YAML field names exactly (no trailing underscore).
- Missing keys in a `put()` dict leave the corresponding struct field at its
  default-constructed value.
- Nested bridge structs appear as nested dicts.
- Vectors of bridge structs appear as Python lists of dicts.
- Fixed-shape arrays and dynamic numpy arrays appear as numpy arrays.
- `type: image` fields appear as `(H, W, ch)` numpy arrays (height first).
- Mat fields appear as numpy arrays or `None` if empty.

---

## Serialization and network transport

Serialization is only required for fields on output topics that specify
`network: tcp` or `network: udp`. It is never needed for local (non-network)
topics or for input topics.

> **Important:** The Python bridge generator supports **Boost serialization
> only**. Some ILLIXR networked data types use Protobuf serialization written
> at the plugin level rather than Boost. Such types cannot be used on network
> output topics in a Python bridge. If you attempt to use one, the generator
> will correctly raise a "serialization header not found" error since these
> types have no Boost serialization header in `data_format/serialization/`.
> Use a bridge-defined type with Boost serialization for any data that must
> travel over the network from a Python bridge.

**Bridge-defined types** (declared in `interfaces/data/`) always have
Boost serialization generated automatically — no extra steps needed.

**ILLIXR system types** (from `include/illixr/data_format/`) require a
`boost::serialization::serialize()` (or `save`/`load`) specialization in
a header under `include/illixr/data_format/serialization/`. If a system
type is used on a network output and no Boost serialization header exists
— including types that use Protobuf serialization instead — the generator
raises a clear error:

```
FATAL_ERROR: Bridge 'my_bridge' output topic 'pose_out' uses ILLIXR type
'pose_data' over network transport, but no serialization header was found
for it in data_format/serialization/. Add a boost::serialization::serialize()
specialisation for ILLIXR::pose_data to a header in that directory.
```

To fix this, either add a Boost serialization header to
`include/illixr/data_format/serialization/` for the type, or define a
bridge-specific type in `interfaces/data/` and convert the data there.

---

## Staleness and regeneration

The generator uses a JSON state file (`${CMAKE_BINARY_DIR}/.py_bridge_state.json`)
to track MD5 hashes of all bridge and type YAML files. Only bridges whose YAML
files have changed since the last CMake run are regenerated. The libclang scan
for ILLIXR serialization headers runs only when regeneration is needed, keeping
up to date cmake runs fast (under one second).

To force full regeneration, delete the state file or the build directory.
