#!/usr/bin/env python3
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""
generate_python_bridges.py

Generator for the ILLIXR Python bridge interface.  Called from PythonBridge.cmake
via execute_process() at cmake configure time.

Two-tier operation
------------------
Tier 1 Profile YAML generation - (controlled by CMake, not this script)
       PythonBridge.cmake handles the timestamp check for python_profiles.yaml
       and passes --write-profiles when generation is needed.

Tier 2 Struct headers and plugin sources - (always runs when called)
       Scoped to the bridges listed in the selected PYTHON_BRIDGE_PROFILE.

Invocation
----------
  # Tier 1 only (profile YAMLs)
  python3 generate_python_bridges.py --write-profiles
      <master_profile> <source_dir>

  # Tier 2 only (headers and sources for selected profile)
  python3 generate_python_bridges.py --generate
      <bridge_profile> <build_dir> <source_dir>

  # Both tiers in one call (used when python_profiles.yaml has changed)
  python3 generate_python_bridges.py --write-profiles --generate
      <master_profile> <bridge_profile> <build_dir> <source_dir>

Type naming and namespacing
---------------------------
Types are referenced in YAML using dotted notation that mirrors the directory
structure under interfaces/data/:

  camera_intrinsics -> interfaces/data/camera_intrinsics.yaml
                      C++: ILLIXR::bridge::camera_intrinsics
                      Python: illixr.bridge.camera_intrinsics

  geometry.camera_intrinsics -> interfaces/data/geometry/camera_intrinsics.yaml
                               C++: ILLIXR::bridge::geometry::camera_intrinsics
                               Python: illixr.bridge.geometry.camera_intrinsics

Each directory level adds one nested C++ namespace inside ILLIXR::bridge and
one Python subpackage level inside illixr.bridge.

Outputs (Tier 2)
----------------
  <build>/include/illixr/bridge[/<ns>]/<name>.hpp (generated struct headers)
  <build>/generated/plugins/<bridge>/plugin.hpp
  <build>/generated/plugins/<bridge>/plugin.cpp
  <build>/generated/plugins/<bridge>/bindings_<flat_name>.cpp
  <build>/generated/plugins/<bridge>/illixr/bridge[/<ns>/]__init__.py
  <build>/generated/plugins/<bridge>/illixr/bridge[/<ns>/]<name>.py

CMake variables emitted to stdout (Tier 2 only)
-----------------------------------------------
  PY_BRIDGE_NAMES              semicolon list of bridge plugin names
  PY_BRIDGE_PLUGIN_DIRS        semicolon list of plugin source directories
  PY_BRIDGE_PLUGIN_CPPS        semicolon list of plugin.cpp paths
  PY_BRIDGE_BINDING_CPPS       semicolon list of all bindings_*.cpp paths
  PY_BRIDGE_STRUCT_HDRS        semicolon list of generated header paths
  PY_BRIDGE_STRUCT_INCLUDE_DIR root include dir  (build_dir/include)
  PY_BRIDGE_SERIALIZE_DEFS     semicolon list of "name:DEFINE1;DEFINE2" entries
  PY_BRIDGE_HAS_NETWORK        semicolon list of TRUE/FALSE per bridge
  PY_BRIDGE_COUNT              number of bridges
"""

import argparse
import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

try:
    import clang.cindex as _clang
    _CLANG_AVAILABLE = True
except ImportError:
    _CLANG_AVAILABLE = False

try:
    import yaml
except ImportError:
    print('message(FATAL_ERROR "generate_python_bridges.py requires PyYAML: '
          'pip install pyyaml")')
    sys.exit(1)

YEAR = str(datetime.date.today().year)

# ---------------------------------------------------------------------------
# Type system
# ---------------------------------------------------------------------------

SCALAR_TYPES = {
    "int8": ("int8_t", "np.int8", []),
    "int16": ("int16_t", "np.int16", []),
    "int": ("int32_t", "np.int32", []),
    "int32": ("int32_t", "np.int32", []),
    "int64": ("int64_t", "np.int64", []),
    "uint8": ("uint8_t", "np.uint8", []),
    "uint16": ("uint16_t", "np.uint16", []),
    "uint32": ("uint32_t", "np.uint32", []),
    "uint64": ("uint64_t", "np.uint64", []),
    "byte": ("uint8_t", "np.uint8", []),
    "char": ("uint8_t", "np.uint8", []),
    "float": ("float", "np.float32", []),
    "float32": ("float", "np.float32", []),
    "double": ("double", "np.float64", []),
    "float64": ("double", "np.float64", []),
    "bool": ("bool", "bool", []),
    "string": ("std::string", "str", ["<boost/serialization/string.hpp>"]),
    "str": ("std::string", "str", ["<boost/serialization/string.hpp>"]),
}

SCALAR_ALIASES = {
    "int32": "int",
    "byte": "uint8",
    "char": "uint8",
    "float32": "float",
    "float64": "double",
    "str": "string",
}

# Image types: underlying storage is std::vector<uint8_t>.
# shape: [width, height, channels] in YAML (user-natural order).
# numpy output: (height, width, channels) matching entry_to_numpy convention.
IMAGE_TYPES = {"image"}

MAT_TYPES = {
    "mat_8u": ("CV_8UC", "np.uint8", "uint8_t"),
    "mat_8s": ("CV_8SC", "np.int8", "int8_t"),
    "mat_16u": ("CV_16UC", "np.uint16", "uint16_t"),
    "mat_16s": ("CV_16SC", "np.int16", "int16_t"),
    "mat_32s": ("CV_32SC", "np.int32", "int32_t"),
    "mat_32f": ("CV_32FC", "np.float32", "float"),
    "mat_64f": ("CV_64FC", "np.float64", "double"),
}

CONTAINER_ALIASES = {
    "list": "vector",
    "vector": "vector",
    "dict": "dict",
    "map": "dict",
}

DICT_FORBIDDEN_VALUE_TYPES = {"bool", "uint8", "uint16", "uint32", "uint64"}
SHAPE_FORBIDDEN_SCALAR = {"string", "str", "bool"}
VECTOR_FORBIDDEN = {"bool"}

# Known existing ILLIXR system types -> header path (relative to include/)
KNOWN_ILLIXR_TYPES = {
    "semantic_data": "illixr/data_format/semantic_data.hpp",
    "voice_query": "illixr/data_format/voice_query.hpp",
    "query_response": "illixr/data_format/query_response.hpp",
    "compressed_frame": "illixr/data_format/compressed_frame.hpp",
    "dual_frames": "illixr/data_format/dual_frames.hpp",
    "combined_pose": "illixr/data_format/combined_pose.hpp",
    "audio_data": "illixr/data_format/audio_data.hpp",
    "frame_meta": "illixr/data_format/frame_meta.hpp",
}


# ---------------------------------------------------------------------------
# Dotted-name helpers
#
# A dotted type name like "geometry.camera_intrinsics" encodes both the
# directory path relative to interfaces/data/ and the C++ namespace nesting
# inside ILLIXR::bridge.
#
# All internal representations keep the dotted form as the canonical key.
# ---------------------------------------------------------------------------

def dotted_to_path(dotted: str) -> Path:
    """'geometry.camera_intrinsics' -> Path('geometry/camera_intrinsics')"""
    return Path(*dotted.split("."))


def dotted_to_cpp_ns(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' -> 'ILLIXR::bridge::geometry::camera_intrinsics'
    'camera_intrinsics'          -> 'ILLIXR::bridge::camera_intrinsics'
    """
    parts = dotted.split(".")
    return "::".join(["ILLIXR", "bridge"] + parts)


def dotted_to_open_namespaces(dotted: str) -> list[str]:
    """
    Returns the sequence of 'namespace X {' lines needed to open the
    namespace for this type, outermost first.
    'geometry.camera_intrinsics' -> ['namespace ILLIXR {',
                                     'namespace bridge {',
                                     'namespace geometry {']
    """
    parts = ["ILLIXR", "bridge"] + dotted.split(".")[:-1]
    return [f"namespace {p} {{" for p in parts]


def dotted_to_close_namespaces(dotted: str) -> list[str]:
    """Matching close lines, innermost first."""
    parts = ["ILLIXR", "bridge"] + dotted.split(".")[:-1]
    return [f"}} // namespace {p}" for p in reversed(parts)]


def dotted_stem(dotted: str) -> str:
    """'geometry.camera_intrinsics' -> 'camera_intrinsics'"""
    return dotted.split(".")[-1]


def dotted_to_header_path(dotted: str) -> str:
    """
    Include path relative to the build include root.
    'geometry.camera_intrinsics' -> 'illixr/bridge/geometry/camera_intrinsics.hpp'
    """
    rel = dotted_to_path(dotted)
    return f"illixr/bridge/{rel}.hpp"


def dotted_to_module_name(dotted: str) -> str:
    """
    pybind11 embedded module identifier (no dots/slashes allowed).
    'geometry.camera_intrinsics' -> 'illixr_bridge_geometry_camera_intrinsics'
    """
    flat = dotted.replace(".", "_")
    return f"illixr_bridge_{flat}"


def dotted_to_python_import(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' -> 'illixr.bridge.geometry.camera_intrinsics'
    """
    return f"illixr.bridge.{dotted}"


def dotted_to_serialize_define(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' -> 'ILLIXR_SERIALIZE_GEOMETRY_CAMERA_INTRINSICS'
    """
    return "ILLIXR_SERIALIZE_" + dotted.upper().replace(".", "_")


def dotted_to_binding_filename(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' -> 'bindings_geometry_camera_intrinsics.cpp'
    """
    flat = dotted.replace(".", "_")
    return f"bindings_{flat}.cpp"


# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------

class SchemaError(Exception):
    pass


def canonical_type(t):
    return SCALAR_ALIASES.get(t, t)


def is_scalar(t):
    return canonical_type(t) in SCALAR_TYPES


def is_mat(t):
    return t in MAT_TYPES


def cpp_scalar(ctype):
    return SCALAR_TYPES[ctype][0]


def validate_field(field_name, field_def, known_dotted_names):
    """
    known_dotted_names: set of dotted type names known in this context
    (e.g. {'camera_intrinsics', 'geometry.point'})
    """
    if not isinstance(field_def, dict):
        raise SchemaError(f"Field '{field_name}': definition must be a mapping")
    field_type = field_def.get("type")
    if field_type is None:
        raise SchemaError(f"Field '{field_name}': missing required 'type' key")

    container = CONTAINER_ALIASES.get(field_def.get("container", ""), None)
    if "container" in field_def and container is None:
        raise SchemaError(
            f"Field '{field_name}': unknown container '{field_def['container']}'. "
            "Use: vector, list, dict, map")

    shape  = field_def.get("shape",  None)
    image  = field_def.get("image",  None)
    channels = field_def.get("channels", None)

    if shape is not None:
        if container not in ["vector", "list", None]:
            raise SchemaError(f"Field '{field_name}': 'shape' and 'container' are mutually exclusive for non-vector/list types.")

        # If shape contains string entries, this is a dynamic numpy array.
        # Infer container=vector so all downstream code works without change.
        if any(isinstance(e, str) for e in (shape or [])):
            container = "vector"

    if field_type in IMAGE_TYPES:
        # type: image - stored as std::vector<uint8_t>.
        # Requires shape: [width_dim, height_dim, channels].
        # numpy output: (height, width, channels) matching entry_to_numpy.
        # Note: container may have been inferred as "vector" by the shape
        # inference above - check field_def directly for user-specified container.
        if field_def.get("container") is not None:
            raise SchemaError(
                f"Field '{field_name}': type 'image' implies vector storage, "
                "do not specify container")
        if channels is not None:
            raise SchemaError(
                f"Field '{field_name}': use shape: [w, h, ch] for type 'image', not 'channels'")
        if shape is None or not isinstance(shape, list) or len(shape) != 3:
            raise SchemaError(
                f"Field '{field_name}': type 'image' requires shape: [width, height, channels]")
        w_dim, h_dim, ch = shape
        for dim_name, dim_val in [("width", w_dim), ("height", h_dim)]:
            if not isinstance(dim_val, (str, int)):
                raise SchemaError(
                    f"Field '{field_name}': image {dim_name} must be a field name or positive integer")
            if isinstance(dim_val, int) and dim_val < 1:
                raise SchemaError(
                    f"Field '{field_name}': image {dim_name} literal must be positive")
            if isinstance(dim_val, str) and not re.match(r'^[a-z][a-z0-9_]*$', dim_val):
                raise SchemaError(
                    f"Field '{field_name}': image {dim_name} '{dim_val}' must be lowercase snake_case")
        if not isinstance(ch, int) or ch not in (1, 2, 3):
            raise SchemaError(
                f"Field '{field_name}': image channels must be 1, 2, or 3, got '{ch}'")
        # Store as vector<uint8_t>; image=shape tells generators to use
        # (H, W, ch) stride layout rather than plain vector handling.
        return dict(field_def, type="uint8", container="vector",
                    shape=shape, image=shape)

    if is_mat(field_type):
        if shape is not None:
            raise SchemaError(f"Field '{field_name}': 'shape' is not valid with mat_* types")
        if container is not None:
            raise SchemaError(f"Field '{field_name}': 'container' is not valid with mat_* types")
        if channels is None:
            raise SchemaError(f"Field '{field_name}': mat_* types require a 'channels' key (1-4)")
        if not isinstance(channels, int) or not (1 <= channels <= 4):
            raise SchemaError(
                f"Field '{field_name}': 'channels' must be an integer between 1 and 4")
        return dict(field_def, type=field_type, container=None, shape=None)

    # Bridge-defined struct (dotted name)
    if field_type in known_dotted_names:
        if channels is not None:
            raise SchemaError(f"Field '{field_name}': 'channels' is only valid for mat_* types")
        if container is not None and container != "vector":
            raise SchemaError(
                f"Field '{field_name}': bridge-defined struct types only support "
                "container: vector")
        if shape is not None:
            if not isinstance(shape, list) or len(shape) != 1:
                raise SchemaError(
                    f"Field '{field_name}': bridge-defined struct types only support 1D shape")
            if not isinstance(shape[0], int) or shape[0] < 1:
                raise SchemaError(
                    f"Field '{field_name}': shape dimensions must be positive integers")
        return dict(field_def, container=container, shape=shape)

    if not is_scalar(field_type):
        raise SchemaError(
            f"Field '{field_name}': unknown type '{field_type}'. Must be a scalar type, "
            "a mat_* type, or a bridge-defined struct dotted name "
            "(e.g. 'camera_intrinsics' or 'geometry.camera_intrinsics')")

    if channels is not None:
        raise SchemaError(f"Field '{field_name}': 'channels' is only valid for mat_* types")

    ctype = canonical_type(field_type)

    if shape is not None:
        # 'shape' on a vector field may contain field name strings and/or int
        # literals, defining a dynamic multi-dimensional numpy array.
        # 'shape' on a plain (non-vector) scalar field may only contain ints
        # and defines a fixed compile-time array (validated above).
        # Both are stored under the same "shape" key; the vector+string case
        # is distinguished at code-generation time by checking container.
        if container == "vector":
            if ctype in SHAPE_FORBIDDEN_SCALAR or ctype == "bool":
                raise SchemaError(
                    f"Field '{field_name}': dynamic 'shape' is not valid with type '{field_type}'")
            if not isinstance(shape, list) or not (1 <= len(shape) <= 3):
                raise SchemaError(
                    f"Field '{field_name}': 'shape' must be a list of 1-3 entries")
            for sf in shape:
                if isinstance(sf, int):
                    if sf < 1:
                        raise SchemaError(
                            f"Field '{field_name}': shape literal '{sf}' "
                            "must be a positive integer")
                elif not isinstance(sf, str) or not re.match(r"^[a-zA-Z][a-zA-Z0-9_]*$", sf):
                    raise SchemaError(f"Field '{field_name}': shape entry '{sf}' must be "
                                      "a field name or a positive integer literal")

        else:
            # Fixed compile-time array: shape must be a list of 1 or 2 positive ints.
            # Dynamic numpy shape (container: vector) is validated separately below.
            if ctype in SHAPE_FORBIDDEN_SCALAR:
                raise SchemaError(
                    f"Field '{field_name}': type '{field_type}' cannot be used with 'shape'")
            if not isinstance(shape, list) or len(shape) not in (1, 2, 3):
                raise SchemaError(
                    f"Field '{field_name}': 'shape' must be a list of 1, 2, or 3 positive integers")
            for dim in shape:
                if not isinstance(dim, int) or dim < 1:
                    raise SchemaError(
                        f"Field '{field_name}': shape dimensions must be positive integers")

    elif container is not None:
        if container == "vector" and ctype in VECTOR_FORBIDDEN:
            raise SchemaError(
                f"Field '{field_name}': vector<bool> is not allowed; "
                "use vector<uint8> instead")
        if container == "dict" and ctype in DICT_FORBIDDEN_VALUE_TYPES:
            raise SchemaError(
                f"Field '{field_name}': dict with value type '{field_type}' is not allowed")

    return dict(field_def, type=ctype, container=container, shape=shape, image=None)


def validate_type_yaml(data, path, data_dir, all_dotted_names):
    """
    path: absolute path to the YAML file
    data_dir: absolute path to interfaces/data/
    all_dotted_names: set of all known dotted type names in this profile

    Derives the dotted name from the file's path relative to data_dir.
    """
    rel = Path(path).relative_to(data_dir)
    # rel = geometry/camera_intrinsics.yaml -> dotted = geometry.camera_intrinsics
    parts = list(rel.with_suffix("").parts)
    dotted = ".".join(parts)

    # Validate each segment is lowercase snake_case
    for part in parts:
        if not re.match(r'^[a-z][a-z0-9_]*$', part):
            raise SchemaError(
                f"{path}: path segment '{part}' must be lowercase snake_case")

    if "name" in data:
        raise SchemaError(
            f"{path}: type YAML files must not contain a 'name' field; "
            f"the struct name is derived from the filename ('{dotted}')")

    fields_raw = data.get("fields")
    if not fields_raw or not isinstance(fields_raw, dict):
        raise SchemaError(f"{path}: 'fields' must be a non-empty mapping")

    peers = set(all_dotted_names) - {dotted}
    fields = {}
    for field_name, field_def in fields_raw.items():
        if not re.match(r'^[a-z][a-z0-9_]*$', field_name):
            raise SchemaError(
                f"{path}: field name '{field_name}' must be lowercase snake_case")
        fields[field_name] = validate_field(field_name, field_def, peers)

    # Process 'image' entries: synthesize missing width/height dimension fields.
    for fname, fdef in list(fields.items()):
        image = fdef.get("image")
        if not image:
            continue
        w_dim, h_dim, _ = image
        for dim_val in (w_dim, h_dim):
            if isinstance(dim_val, str) and dim_val not in fields:
                fields[dim_val] = {"type": "int", "container": None,
                                   "shape": None, "image": None}
            elif isinstance(dim_val, str):
                sf_type = fields[dim_val].get("type", "")
                sf_cpp  = cpp_scalar(canonical_type(sf_type))
                _int_cpp_types = {"int8_t", "int16_t", "int32_t", "int64_t",
                                  "uint8_t", "uint16_t", "uint32_t", "uint64_t"}
                if sf_cpp not in _int_cpp_types:
                    raise SchemaError(
                        f"{path}: field '{fname}' image width/height field '{dim_val}' "
                        f"must be an integer field, got '{sf_type}'")

    # Process dynamic shape entries (shape on a vector field containing strings):
    # synthesize any missing dimension fields as int32_t, or validate that
    # explicitly declared dimension fields are integers.
    _int_cpp_types = {"int8_t", "int16_t", "int32_t", "int64_t",
                      "uint8_t", "uint16_t", "uint32_t", "uint64_t"}
    for fname, fdef in list(fields.items()):
        if fdef.get("container") != "vector":
            continue
        sf_names = [e for e in (fdef.get("shape") or []) if isinstance(e, str)]
        for sf in sf_names:
            if sf not in fields:
                # Synthesize the dimension field as int32_t
                fields[sf] = {"type": "int", "container": None, "shape": None}
            else:
                sf_type = fields[sf].get("type", "")
                sf_cpp  = cpp_scalar(canonical_type(sf_type))
                if sf_cpp not in _int_cpp_types:
                    raise SchemaError(
                        f"{path}: field '{fname}' shape['{sf}'] must be "
                        f"an integer field, got '{sf_type}' (C++ type '{sf_cpp}')")

    return {"dotted": dotted, "fields": fields}


def normalize_network(val):
    if val is None or val is False or str(val).lower() == "false":
        return "none"
    v = str(val).lower()
    if v == "tcp":
        return "tcp"
    if v == "udp":
        return "udp"
    if v is True or v == "true":
        return "any"
    raise SchemaError(
        f"Invalid network value '{val}'. Use: tcp, TCP, udp, UDP, true, false")


def validate_dotted_name(name: str, context: str):
    """Validate that a dotted type name has only lowercase snake_case segments."""
    for part in name.split("."):
        if not re.match(r'^[a-z][a-z0-9_]*$', part):
            raise SchemaError(
                f"{context}: '{name}' contains invalid segment '{part}'; "
                "each segment must be lowercase snake_case")


def validate_bridge_yaml(data, path, all_type_names):
    """
    Validate a bridge descriptor YAML file.

    Bridge name is derived from the filename stem - no 'name:' key needed.
    Types are discovered automatically from topic 'type:' fields - no
    'types:' key needed. 'script:' is optional; the runtime env var
    ILLIXR_<NAME>_SCRIPT overrides whatever default is compiled in.
    'alias:' on each topic entry is optional; defaults to the topic name.

    all_type_names: set of all valid dotted type names discoverable in
    interfaces/data/ for this profile, plus KNOWN_ILLIXR_TYPES keys.
    """
    # Bridge name comes from the filename, not a key inside the file.
    name = Path(path).stem
    if not re.match(r"^[a-z][a-z0-9_]*$", name):
        raise SchemaError(
            f"{path}: filename stem '{name}' must be lowercase snake_case")

    if "name" in data:
        raise SchemaError(
            f"{path}: bridge yaml files must not contain a 'name' key; "
            f"the bridge name is derived from the filename ('{name}')")

    if "types" in data:
        raise SchemaError(
            f"{path}: bridge yaml files must not contain a 'types' key; "
            "required types are discovered automatically from the 'type:' "
            "fields of each input and output entry")

    # script: is optional - ILLIXR_<NAME>_SCRIPT env var overrides at runtime
    script = data.get("script", "")

    inputs = data.get("inputs", [])
    outputs = data.get("outputs", [])
    if not isinstance(inputs, list):
        raise SchemaError(f"{path}: 'inputs' must be a list")
    if not isinstance(outputs, list):
        raise SchemaError(f"{path}: 'outputs' must be a list")

    validated_inputs = []
    for i, inp in enumerate(inputs):
        topic = inp.get("topic")
        input_type = inp.get("type")
        alias = inp.get("alias") or topic  # default alias to topic name
        if not topic:
            raise SchemaError(f"{path}: input[{i}] missing 'topic'")
        if not input_type:
            raise SchemaError(f"{path}: input[{i}] missing 'type'")
        if not re.match(r"^[a-z][a-z0-9_]*$", alias):
            raise SchemaError(
                f"{path}: input[{i}] alias '{alias}' must be lowercase snake_case")
        if input_type not in all_type_names:
            raise SchemaError(
                f"{path}: input[{i}] type '{input_type}' was not found. "
                f"Check that interfaces/data/{dotted_to_path(input_type)}.yaml exists. "
                "Use dotted notation for types in subdirectories "
                "(e.g. 'semantic_xr.semantic_data')")
        validated_inputs.append({"topic": topic, "type": input_type, "alias": alias})

    validated_outputs = []
    for i, out in enumerate(outputs):
        topic = out.get("topic")
        out_type = out.get("type")
        alias = out.get("alias") or topic  # default alias to topic name
        network = normalize_network(out.get("network", False))
        if not topic:
            raise SchemaError(f"{path}: output[{i}] missing 'topic'")
        if not out_type:
            raise SchemaError(f"{path}: output[{i}] missing 'type'")
        if not re.match(r"^[a-z][a-z0-9_]*$", alias):
            raise SchemaError(
                f"{path}: output[{i}] alias '{alias}' must be lowercase snake_case")
        if out_type not in all_type_names:
            raise SchemaError(
                f"{path}: output[{i}] type '{out_type}' was not found. "
                f"Check that interfaces/data/{dotted_to_path(out_type)}.yaml exists. "
                "Use dotted notation for types in subdirectories "
                "(e.g. 'semantic_xr.query_response')")
        validated_outputs.append(
            {"topic": topic, "type": out_type, "alias": alias, "network": network})

    # Collect the bridge-defined dotted type names used by this bridge
    used_gen_types = set()
    for entry in validated_inputs + validated_outputs:
        if entry["type"] not in KNOWN_ILLIXR_TYPES:
            used_gen_types.add(entry["type"])

    return {
        "name": name,
        "script": script,
        "type_names": sorted(used_gen_types),
        "inputs": validated_inputs,
        "outputs": validated_outputs,
    }


# ---------------------------------------------------------------------------
# Topological sort
# ---------------------------------------------------------------------------

def topo_sort(type_defs):
    """Sort by dotted name key; dependencies before dependents."""
    name_to_def = {td["dotted"]: td for td in type_defs}
    visited = set()
    order = []

    def visit(dotted):
        if dotted in visited or dotted not in name_to_def:
            return
        visited.add(dotted)
        for field_def in name_to_def[dotted]["fields"].values():
            visit(field_def.get("type", ""))
        order.append(name_to_def[dotted])

    for td in type_defs:
        visit(td["dotted"])
    return order


# ---------------------------------------------------------------------------
# C++ code generation helpers
# ---------------------------------------------------------------------------

def _cpp_type_ref(field_type, gen_dotted_names):
    """
    Return the C++ type spelling to use in a field declaration.
    For bridge types, emits the fully qualified ILLIXR::bridge::... name.
    """
    if field_type in gen_dotted_names:
        return dotted_to_cpp_ns(field_type)
    return field_type  # scalar, already a C++ type name


def field_decl(field_name, field_def, gen_dotted_names):
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    cpp_t = _cpp_type_ref(field_type, gen_dotted_names)

    if shape and not container:
        if len(shape) == 1:
            return f"    {cpp_t} {field_name}_[{shape[0]}];"
        if len(shape) == 2:
            return f"    {cpp_t} {field_name}_[{shape[0]}][{shape[1]}];"
        return f"    {cpp_t} {field_name}_[{shape[0]}][{shape[1]}][{shape[2]}];"

    if is_mat(field_type):
        return f"    cv::Mat {field_name}_;"

    if field_type in gen_dotted_names:
        if container == "vector":
            return f"    std::vector<{cpp_t}> {field_name}_;"
        return f"    {cpp_t} {field_name}_;"

    t = cpp_scalar(canonical_type(field_type))
    if container == "vector":
        return f"    std::vector<{t}> {field_name}_;"
    if container == "dict":
        return f"    std::unordered_map<std::string, {t}> {field_name}_;"
    return f"    {t} {field_name}_;"


def required_includes(td, gen_dotted_names):
    illixr = set()
    system = set()
    has_vec = has_map = has_str = has_cstdint = has_mat = False

    for field_def in td["fields"].values():
        field_type = field_def["type"]
        container = field_def.get("container")

        if is_mat(field_type):
            has_mat = has_cstdint = True
            system.add("<boost/serialization/split_member.hpp>")
            continue

        if field_type in gen_dotted_names:
            # Cross-include: path mirrors the dotted namespace
            illixr.add(f'"illixr/bridge/{dotted_to_path(field_type)}.hpp"')
            if container == "vector":
                has_vec = True
            continue

        ctype = canonical_type(field_type)
        for h in SCALAR_TYPES[ctype][2]:
            system.add(h)

        if ctype in ("int8_t", "int16_t", "int32_t", "int64_t",
                     "uint8_t", "uint16_t", "uint32_t", "uint64_t"):
            has_cstdint = True
        if ctype == "std::string":
            has_str = True
        if container == "vector":
            has_vec = True
        if container == "dict":
            has_map = has_str = True

    if has_vec:
        system.update(["<vector>", "<boost/serialization/vector.hpp>"])
    if has_map:
        system.update(["<unordered_map>",
                       "<boost/serialization/unordered_map.hpp>"])
    if has_str:
        system.add("<string>")
    if has_cstdint:
        system.add("<cstdint>")
    if has_mat:
        system.add("<opencv2/core.hpp>")

    return sorted(illixr), sorted(system)


def serialize_stmt(field_name, field_def):
    """Return the ar & data.field_ statement for a free-function serializer."""
    field_type = field_def["type"]
    shape = field_def.get("shape")
    container = field_def.get("container")

    if is_mat(field_type):
        return None  # handled by split save/load

    if shape and not container:
        if len(shape) == 1:
            return (f"    ar & boost::serialization::"
                    f"make_array(data.{field_name}_, {shape[0]});")
        if len(shape) == 2:
            n = shape[0] * shape[1]
            return (f"    ar & boost::serialization::"
                    f"make_array(&data.{field_name}_[0][0], {n});")
        n = shape[0] * shape[1] * shape[2]
        return (f"    ar & boost::serialization::"
                f"make_array(&data.{field_name}_[0][0][0], {n});")

    return f"    ar & data.{field_name}_;"


def _mat_save_lines(field_name):
    return [
        "    {",
        f"        int rows = data.{field_name}_.rows, cols = data.{field_name}_.cols, "
        f"typ = data.{field_name}_.type();",
        f"        ar & rows; ar & cols; ar & typ;",
        f"        if (data.{field_name}_.isContinuous()) {{",
        f"            std::size_t sz = data.{field_name}_.total() * data.{field_name}_.elemSize();",
        f"            ar & boost::serialization::make_array(",
        f"                reinterpret_cast<const uint8_t*>(data.{field_name}_.data), sz);",
        "        }",
        "    }",
    ]


def _mat_load_lines(field_name):
    return [
        "    {",
        "        int rows, cols, typ;",
        f"        ar & rows; ar & cols; ar & typ;",
        f"        data.{field_name}_.create(rows, cols, typ);",
        f"        std::size_t sz = data.{field_name}_.total() * data.{field_name}_.elemSize();",
        f"        ar & boost::serialization::make_array(",
        f"            reinterpret_cast<uint8_t*>(data.{field_name}_.data), sz);",
        "    }",
    ]


# ---------------------------------------------------------------------------
# Struct header generation
# ---------------------------------------------------------------------------

def gen_struct_header(td, gen_dotted_names_so_far):
    dotted = td["dotted"]
    stem = dotted_stem(dotted)
    fields = td["fields"]
    gen_set = set(gen_dotted_names_so_far)

    illixr_includes, system_includes = required_includes(td, gen_set)

    open_ns = dotted_to_open_namespaces(dotted)
    close_ns = dotted_to_close_namespaces(dotted)

    header = []
    header.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    header.append("// SPDX-License-Identifier: BSL-1.0")
    header.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    header.append(f"// Bridge type: {dotted_to_python_import(dotted)}")
    header.append("")
    header.append("#pragma once")
    header.append("")
    header.append('#include "illixr/switchboard.hpp"')
    if illixr_includes:
        header.append("")
        for h in illixr_includes:
            header.append(f"#include {h}")
    if system_includes:
        header.append("")
        for h in system_includes:
            header.append(f"#include {h}")
    header.append("")

    # Open nested namespaces - struct definition only, no serialization inside
    for ns_line in open_ns:
        header.append(ns_line)
    header.append("")

    header.append(f"struct {stem} : switchboard::event {{")
    for field_name, field_def in fields.items():
        header.append(field_decl(field_name, field_def, gen_set))
    header.append("")
    header.append(f"    {stem}() = default;")
    header.append("};")
    header.append("")

    # Close the struct's namespaces
    for ns_line in close_ns:
        header.append(ns_line)
    header.append("")

    return "\n".join(header)

def build_illixr_serialization_map(source_dir: Path) -> dict[str, str]:
    """
    Scan include/illixr/data_format/serialization/*.hpp and build a map:
        bare_type_name -> include_path_relative_to_source_include_root

    Uses libclang to find free functions named serialize/save/load in
    namespace boost::serialization and extracts the type of the second
    parameter (the data argument).

    Falls back to an empty dict if libclang is not available, in which
    case no serialization includes will be added for ILLIXR system types
    (bridge-defined types are unaffected since they generate their own).
    """
    if not _CLANG_AVAILABLE:
        return {}

    ser_dir = (source_dir / "include" / "illixr" /
               "data_format" / "serialization")
    if not ser_dir.exists():
        return {}

    include_root = source_dir / "include"
    result: dict[str, str] = {}

    index = _clang.Index.create()

    for hdr in sorted(ser_dir.glob("*.hpp")):
        if hdr.stem == "openxr":
            continue   # excluded per project convention

        tu = index.parse(
            str(hdr),
            args=["-std=c++17", "-x", "c++",
                  "-I", str(include_root),
                  "-undef"],          # all #ifdefs evaluate false
            options=(
                _clang.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES |
                _clang.TranslationUnit.PARSE_INCOMPLETE
            ),
        )

        # Include path relative to the include root, e.g.
        # "illixr/data_format/serialization/pose.hpp"
        rel_include = str(hdr.relative_to(include_root)).replace("\\", "/")

        def _walk(cursor: _clang.Cursor,
                  in_boost_ser: bool = False) -> None:
            if cursor.kind == _clang.CursorKind.NAMESPACE:
                ns = cursor.spelling
                parent = (cursor.semantic_parent.spelling
                          if cursor.semantic_parent else "")
                enter = (in_boost_ser
                         or (parent == "boost" and ns == "serialization")
                         or ns == "boost")
                for child in cursor.get_children():
                    _walk(child, enter)
                return

            if (in_boost_ser
                    and cursor.kind in (
                        _clang.CursorKind.FUNCTION_TEMPLATE,
                        _clang.CursorKind.FUNCTION_DECL,
                    )
                    and cursor.spelling in ("serialize", "save", "load")):
                data_params = [
                    p for p in cursor.get_children()
                    if p.kind == _clang.CursorKind.PARM_DECL
                ]
                if len(data_params) >= 2:
                    type_spell = data_params[1].type.spelling
                    type_spell = re.sub(r"\bconst\b", "", type_spell)
                    type_spell = re.sub(r"\bstruct\b", "", type_spell)
                    type_spell = type_spell.replace("&", "").strip()
                    # Strip leading ILLIXR:: to get the bare type name
                    bare = re.sub(r"^ILLIXR::", "", type_spell)
                    if bare and bare not in result:
                        result[bare] = rel_include

            for child in cursor.get_children():
                _walk(child, in_boost_ser)

        _walk(tu.cursor)

    return result


def gen_boost_hpp(td, header_file, gen_dotted_names_so_far, illixr_ser_map=None):
    """
    Generate the _ser.hpp for a bridge type.

    Includes the _ser.hpp of every contained bridge-defined type so that
    nested serialization is always available.  Also includes the Boost
    serialization header for any contained ILLIXR system type that has one.
    """
    dotted = td["dotted"]
    fields = td["fields"]
    has_mat = any(is_mat(f["type"]) for f in fields.values())
    full_qn = dotted_to_cpp_ns(dotted)
    gen_set = set(gen_dotted_names_so_far)

    # Collect ser includes for contained types.
    # For bridge-defined types: include their _ser.hpp (same directory).
    # For ILLIXR system types: look up in the dynamically built map.
    _ser_map = illixr_ser_map or {}

    contained_ser_includes = []
    for field_def in fields.values():
        ftype = field_def["type"]
        if ftype in gen_set:
            # Bridge-defined nested type: include its _ser.hpp
            rel = dotted_to_path(ftype)
            contained_ser_includes.append(f'"illixr/bridge/{rel}_ser.hpp"')
        elif ftype in _ser_map:
            contained_ser_includes.append(f'"{_ser_map[ftype]}"')
    # Deduplicate while preserving order
    seen = set()
    deduped = []
    for h in contained_ser_includes:
        if h not in seen:
            seen.add(h)
            deduped.append(h)
    contained_ser_includes = deduped

    header = []
    header.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    header.append("// SPDX-License-Identifier: BSL-1.0")
    header.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    header.append(f"// Bridge type: {dotted_to_python_import(dotted)}")
    header.append("")
    header.append("#pragma once")
    header.append("")
    header.append(f"#include \"{header_file}\"")
    for ser_inc in contained_ser_includes:
        header.append(f"#include {ser_inc}")
    header.append("")
    header.append("#include <boost/serialization/array.hpp>")
    header.append("#include <boost/serialization/export.hpp>")
    header.append("#include <boost/serialization/nvp.hpp>")
    header.append("#include <boost/serialization/split_free.hpp>")
    header.append("#include <boost/serialization/base_object.hpp>")
    header.append("namespace boost {")
    header.append("namespace serialization {")
    header.append("")

    if has_mat:
        # Mat types require split save/load free functions
        header.append("template<class Archive>")
        header.append(f"void save(Archive& ar, {full_qn}& data, const unsigned int) {{")
        header.append("    ar & boost::serialization::base_object<ILLIXR::switchboard::event>(data);")
        for field_name, field_def in fields.items():
            if is_mat(field_def["type"]):
                header += _mat_save_lines(field_name)
            else:
                s = serialize_stmt(field_name, field_def)
                if s:
                    header.append(s)
        header.append("}")
        header.append("")
        header.append("template<class Archive>")
        header.append(f"void load(Archive& ar, {full_qn}& data, const unsigned int) {{")
        header.append("    ar & boost::serialization::base_object<ILLIXR::switchboard::event>(data);")
        for field_name, field_def in fields.items():
            if is_mat(field_def["type"]):
                header += _mat_load_lines(field_name)
            else:
                s = serialize_stmt(field_name, field_def)
                if s:
                    header.append(s)
        header.append("}")
        header.append("")
        header.append(f"}} // BOOST_SERIALIZATION_SPLIT_FREE({full_qn})")
        header.append(f"BOOST_SERIALIZATION_SPLIT_FREE({full_qn})")
    else:
        header.append("template<class Archive>")
        header.append(f"void serialize(Archive& ar, {full_qn}& data, const unsigned int) {{")
        header.append("    ar & boost::serialization::base_object<ILLIXR::switchboard::event>(data);")
        for field_name, field_def in fields.items():
            s = serialize_stmt(field_name, field_def)
            if s:
                header.append(s)
        header.append("}")

    header.append("")
    header.append("} // namespace serialization")
    header.append("} // namespace boost")
    header.append("")
    header.append(f"BOOST_CLASS_EXPORT_KEY({full_qn})")
    header.append("")

    return "\n".join(header)


def gen_boost_cpp(td, gen_dotted_names_so_far, header_file):
    dotted = td["dotted"]
    full_qn = dotted_to_cpp_ns(dotted)
    cpp = []
    cpp.append(f"#include \"{header_file}\"")
    cpp.append("")
    cpp.append(f"BOOST_CLASS_EXPORT_IMPLEMENT({full_qn})")
    return "\n".join(cpp)

# ---------------------------------------------------------------------------
# pybind11 bindings generation
# ---------------------------------------------------------------------------

def _mat_getter_lines(field_name, field_type, qname):
    np_dtype = MAT_TYPES[field_type][1][3:]  # strip "np."
    return [
        f'    .def_property("{field_name}",',
        f'        [](const {qname}& self) -> py::array {{',
        f'            if (self.{field_name}_.empty()) return py::array();',
        f'            py::object guard = py::capsule(',
        f'                new std::shared_ptr<{qname}>(),',
        f'                [](void* p) {{',
        f'                    delete static_cast<std::shared_ptr<{qname}>*>(p); }});',
        f'            std::vector<ssize_t> shp, str;',
        f'            if (self.{field_name}_.channels() == 1) {{',
        f'                shp = {{self.{field_name}_.rows, self.{field_name}_.cols}};',
        f'                str = {{(ssize_t)self.{field_name}_.step[0],',
        f'                        (ssize_t)self.{field_name}_.elemSize()}};',
        f'            }} else {{',
        f'                shp = {{self.{field_name}_.rows, self.{field_name}_.cols,',
        f'                        self.{field_name}_.channels()}};',
        f'                str = {{(ssize_t)self.{field_name}_.step[0],',
        f'                        (ssize_t)self.{field_name}_.step[1],',
        f'                        (ssize_t)self.{field_name}_.elemSize1()}};',
        f'            }}',
        f'            return py::array(py::dtype("{np_dtype}"),',
        f'                shp, str, self.{field_name}_.data, guard);',
        f'        }},',
    ]


def _mat_setter_lines(field_name, field_type, qname):
    cv_base = MAT_TYPES[field_type][0]
    cpp_elem = MAT_TYPES[field_type][2]
    return [
        f'        []({qname}& self, py::array_t<{cpp_elem}> arr) {{',
        f'            auto buf = arr.request();',
        f'            int r = (int)buf.shape[0], c = (int)buf.shape[1];',
        f'            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;',
        f'            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);',
        f'            tmp.copyTo(self.{field_name}_);',
        f'        }})',
    ]


def _fixed_array_getter_lines(field_name, field_def, qname, gen_dotted_names):
    field_type = field_def["type"]
    shape = field_def["shape"]

    if field_type in gen_dotted_names:
        n = shape[0]
        return [
            f'    .def_property("{field_name}",',
            f'        [](const {qname}& self) {{',
            f'            py::list result;',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                result.append(self.{field_name}_[i]);',
            f'            return result;',
            f'        }},',
        ]

    ctype = canonical_type(field_type)
    cpp_t = cpp_scalar(ctype)
    if len(shape) == 1:
        flat   = shape[0]
        data_p = f"self.{field_name}_"
    elif len(shape) == 2:
        flat   = shape[0] * shape[1]
        data_p = f"&self.{field_name}_[0][0]"
    else:
        flat   = shape[0] * shape[1] * shape[2]
        data_p = f"&self.{field_name}_[0][0][0]"
    return [
        f'    .def_property("{field_name}",',
        f'        [](const {qname}& self) -> py::array_t<{cpp_t}> {{',
        f'            return py::array_t<{cpp_t}>(',
        f'                {{{flat}}}, {{sizeof({cpp_t})}},',
        f'                const_cast<{cpp_t}*>({data_p}));',
        f'        }},',
    ]


def _fixed_array_setter_lines(field_name, field_def, qname, gen_dotted_names):
    field_type = field_def["type"]
    shape = field_def["shape"]

    if field_type in gen_dotted_names:
        n = shape[0]
        fqn = dotted_to_cpp_ns(field_type)
        return [
            f'        []({qname}& self, py::list lst) {{',
            f'            if ((int)lst.size() != {n})',
            f'                throw std::runtime_error(',
            f'                    "{field_name}: expected {n} elements");',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                self.{field_name}_[i] = lst[i].cast<{fqn}>();',
            f'        }})',
        ]

    ctype = canonical_type(field_type)
    cpp_t = cpp_scalar(ctype)
    if len(shape) == 1:
        flat = shape[0]
        dest = f"self.{field_name}_"
    elif len(shape) == 2:
        flat = shape[0] * shape[1]
        dest = f"&self.{field_name}_[0][0]"
    else:
        flat = shape[0] * shape[1] * shape[2]
        dest = f"&self.{field_name}_[0][0][0]"
    return [
        f'        []({qname}& self, py::array_t<{cpp_t}> arr) {{',
        f'            if (arr.size() != {flat})',
        f'                throw std::runtime_error(',
        f'                    "{field_name}: expected {flat} elements");',
        f'            std::copy(arr.data(), arr.data() + {flat}, {dest});',
        f'        }})',
    ]


def _kw_param(field_name, field_def, gen_dotted_names):
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if is_mat(field_type) or (shape and not container):
        return f'py::arg("{field_name}") = py::none()'

    if field_type in gen_dotted_names:
        fqn = dotted_to_cpp_ns(field_type)
        if container == "vector":
            return f'py::arg("{field_name}") = std::vector<{fqn}>()'
        return f'py::arg("{field_name}") = {fqn}()'

    ctype = canonical_type(field_type)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f'py::arg("{field_name}") = std::vector<{cpp_t}>()'
    if container == "dict":
        return f'py::arg("{field_name}") = std::unordered_map<std::string, {cpp_t}>()'
    defaults = {"bool": "false", "std::string": '""',
                "float": "0.0f", "double": "0.0"}
    return f'py::arg("{field_name}") = {defaults.get(cpp_t, "0")}'


def _kw_init_body(field_name, field_def, gen_dotted_names):
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if is_mat(field_type):
        cv_base = MAT_TYPES[field_type][0]
        return (
            f"        if (!{field_name}.is_none()) {{\n"
            f"            auto arr = {field_name}.cast<py::array>();\n"
            f"            auto buf = arr.request();\n"
            f"            int r = (int)buf.shape[0], c = (int)buf.shape[1];\n"
            f"            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;\n"
            f"            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);\n"
            f"            tmp.copyTo(obj.{field_name}_);\n"
            f"        }}"
        )

    if shape and not container:
        if field_type in gen_dotted_names:
            fqn = dotted_to_cpp_ns(field_type)
            n = shape[0]
            return (
                f"        if (!{field_name}.is_none()) {{\n"
                f"            auto lst = {field_name}.cast<py::list>();\n"
                f"            if ((int)lst.size() != {n})\n"
                f"                throw std::runtime_error(\n"
                f'                    "{field_name}: expected {n} elements");\n'
                f"            for (int i = 0; i < {n}; ++i)\n"
                f"                obj.{field_name}_[i] = lst[i].cast<{fqn}>();\n"
                f"        }}"
            )
        ctype = canonical_type(field_type)
        cpp_t = cpp_scalar(ctype)
        if len(shape) == 1:
            flat = shape[0]
            dest = f"obj.{field_name}_"
        elif len(shape) == 2:
            flat = shape[0] * shape[1]
            dest = f"&obj.{field_name}_[0][0]"
        else:
            flat = shape[0] * shape[1] * shape[2]
            dest = f"&obj.{field_name}_[0][0][0]"
        return (
            f"        if (!{field_name}.is_none()) {{\n"
            f"            auto arr = {field_name}.cast<py::array_t<{cpp_t}>>();\n"
            f"            if (arr.size() != {flat})\n"
            f"                throw std::runtime_error(\n"
            f'                    "{field_name}: expected {flat} elements");\n'
            f"            std::copy(arr.data(), arr.data() + {flat}, {dest});\n"
            f"        }}"
        )

    return f"        obj.{field_name}_ = {field_name};"


def _lambda_param_type(field_def, gen_dotted_names):
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if is_mat(field_type) or (shape and not container):
        return "py::object"

    if field_type in gen_dotted_names:
        fqn = dotted_to_cpp_ns(field_type)
        if container == "vector":
            return f"std::vector<{fqn}>"
        return fqn

    ctype = canonical_type(field_type)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f"std::vector<{cpp_t}>"
    if container == "dict":
        return f"std::unordered_map<std::string, {cpp_t}>"
    return cpp_t


def gen_bindings_cpp(td, gen_dotted_names_so_far):
    dotted = td["dotted"]
    stem = dotted_stem(dotted)
    fields = td["fields"]
    gen_set = set(gen_dotted_names_so_far)
    has_mat = any(is_mat(f["type"]) for f in fields.values())
    qname = dotted_to_cpp_ns(dotted)
    module = dotted_to_module_name(dotted)
    hdr = dotted_to_header_path(dotted)

    cpp = []
    cpp.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    cpp.append("// SPDX-License-Identifier: BSL-1.0")
    cpp.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    cpp.append(f"// Python module: {dotted_to_python_import(dotted)}")
    cpp.append("")
    cpp.append(f'#include "{hdr}"')
    cpp.append("")
    cpp.append("#include <pybind11/embed.h>")
    cpp.append("#include <pybind11/numpy.h>")
    cpp.append("#include <pybind11/stl.h>")
    if has_mat:
        cpp.append("#include <opencv2/core.hpp>")
    cpp.append("")
    cpp.append("#include <memory>")
    cpp.append("#include <stdexcept>")
    cpp.append("#include <vector>")
    cpp.append("")
    cpp.append("namespace py = pybind11;")
    cpp.append("")
    cpp.append(f"PYBIND11_EMBEDDED_MODULE({module}, m) {{")
    cpp.append(f'    m.doc() = "ILLIXR bridge type: {dotted_to_python_import(dotted)}";')
    cpp.append("")

    # kw-init lambda parameters
    params = [(fn, _lambda_param_type(fd, gen_set)) for fn, fd in fields.items()]
    param_str = ",\n".join(f"            {param_type} {param_name}" for param_name, param_type in params)

    cpp.append(f"    py::class_<{qname}>(m, \"{stem}\")")
    if param_str:
        cpp.append(f"        .def(py::init([]({param_str}) {{")
        cpp.append(f"            {qname} obj;")
        for field_name, field_def in fields.items():
            cpp.append(_kw_init_body(field_name, field_def, gen_set))
        cpp.append("            return obj;")
        cpp.append("        }),")
        args = [_kw_param(fn, fd, gen_set) for fn, fd in fields.items()]
        for i, arg in enumerate(args):
            suffix = "," if i < len(args) - 1 else ""
            cpp.append(f"        {arg}{suffix}")
        cpp.append("        )")
    else:
        cpp.append(f"        .def(py::init<>())")

    # Properties and readwrite
    for field_name, field_def in fields.items():
        field_type = field_def["type"]
        container = field_def.get("container")
        shape = field_def.get("shape")

        if is_mat(field_type):
            cpp += _mat_getter_lines(field_name, field_type, qname)
            cpp += _mat_setter_lines(field_name, field_type, qname)
        elif shape and not container:
            cpp += _fixed_array_getter_lines(field_name, field_def, qname, gen_set)
            cpp += _fixed_array_setter_lines(field_name, field_def, qname, gen_set)
        else:
            cpp.append(f'        .def_readwrite("{field_name}", &{qname}::{field_name}_)')

    cpp.append("        ;")
    cpp.append("}")
    cpp.append("")
    return "\n".join(cpp)


# ---------------------------------------------------------------------------
# Python package shim generation
# ---------------------------------------------------------------------------

def gen_python_package_tree(sorted_types, plugin_dir: Path):
    """
    Generate the illixr/bridge[/<ns>/]__init__.py and <stem>.py shims for
    all bridge types used by a plugin.  Creates directories as needed.

    Returns list of generated .py file paths.
    """
    generated = []

    # Collect all unique package directories needed
    # illixr/bridge/ is always needed
    # illixr/bridge/geometry/ needed for geometry.* types, etc.
    pkg_dirs: set[Path] = set()
    for td in sorted_types:
        dotted = td["dotted"]
        parts = dotted.split(".")
        # All ancestor package dirs including the leaf's parent
        for depth in range(len(parts)):
            rel = Path("illixr", "bridge", *parts[:depth])
            pkg_dirs.add(plugin_dir / rel)

    # Write __init__.py for each package dir
    # The top-level illixr/bridge/__init__.py imports all types in topo order
    # Subpackage __init__.py files import their own types in topo order

    # Group types by their immediate parent package
    # key: tuple of namespace parts above the stem
    # e.g. () for root, ('geometry',) for geometry.*
    ns_to_types: dict[tuple, list] = {}
    for td in sorted_types:
        parts = td["dotted"].split(".")
        ns = tuple(parts[:-1])
        ns_to_types.setdefault(ns, []).append(td)

    # For each package dir, write __init__.py that imports its direct children
    # in topological order
    for pkg_dir in sorted(pkg_dirs):
        pkg_dir.mkdir(parents=True, exist_ok=True)
        rel_parts = pkg_dir.relative_to(plugin_dir / "illixr" / "bridge").parts
        # Types directly in this namespace
        types_here = ns_to_types.get(rel_parts, [])
        init_lines = [
            f"# Auto-generated by generate_python_bridges.py -- do not edit directly.",
            f"# Bridge types in illixr.bridge{'.' + '.'.join(rel_parts) if rel_parts else ''}",
            "",
        ]
        for td in types_here:
            module = dotted_to_module_name(td["dotted"])
            init_lines.append(f"from {module} import *  # noqa: F401,F403")
        init_path = pkg_dir / "__init__.py"
        init_path.write_text("\n".join(init_lines) + "\n")
        generated.append(str(init_path))

    # Also ensure illixr/__init__.py exists
    illixr_init = plugin_dir / "illixr" / "__init__.py"
    illixr_init.parent.mkdir(parents=True, exist_ok=True)
    if not illixr_init.exists():
        illixr_init.write_text(
            "# Auto-generated by generate_python_bridges.py\n")
        generated.append(str(illixr_init))

    return generated


def gen_plugin_cmake(bridge, gen_dotted_names, gen_serializers, plugin_dir,
                     build_include_dir):
    """Generate a CMakeLists.txt matching the ILLIXR SHARED plugin pattern."""
    plugin_name = bridge["name"]
    gen_hdrs = []
    for dotted in sorted(gen_dotted_names):
        hdr = (build_include_dir / "illixr" / "bridge" /
               (str(dotted_to_path(dotted)) + ".hpp"))
        gen_hdrs.append(str(hdr))

    cmake = []
    cmake.append("# Copyright 2020-" + YEAR + ", The Board of Trustees of the University of Illinois.")
    cmake.append("# SPDX-License-Identifier: BSL-1.0")
    cmake.append("# This file was generated by generate_python_bridges.py -- do not edit directly.")
    cmake.append("")
    cmake.append("set(PLUGIN_NAME plugin." + plugin_name + "${ILLIXR_BUILD_SUFFIX})")
    cmake.append("")
    # Do NOT call find_package(pybind11) here. pybind11 is already found
    # in the parent cmake scope, and its targets (pybind11::embed etc.) are
    # globally visible to subdirectories.  Re-calling find_package would
    # trigger FindPythonLibsNew again, conflicting with cached Python3
    # variables and causing PYTHON_LIBRARY-NOTFOUND on re-runs.
    cmake.append("")
    cmake.append("# Glob binding sources generated at configure time")
    cmake.append('file(GLOB _BRIDGE_BINDINGS "' + str(plugin_dir) + '/bindings_*.cpp")')
    cmake.append("")
    cmake.append("add_library(${PLUGIN_NAME} SHARED")
    cmake.append('    "' + str(plugin_dir) + '/plugin.cpp"')
    cmake.append('    "' + str(plugin_dir) + '/plugin.hpp"')
    cmake.append("    ${_BRIDGE_BINDINGS}")
    for hdr in gen_hdrs:
        cmake.append('    "' + hdr + '"')
    for hdr in gen_serializers:
        cmake.append('    "' + hdr + '"')
    cmake.append('    "${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp"')
    cmake.append('    "${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp"')
    cmake.append(")")
    cmake.append("")
    cmake.append("target_include_directories(${PLUGIN_NAME} PRIVATE")
    cmake.append('    "' + str(plugin_dir) + '"')
    cmake.append('    "' + str(build_include_dir) + '"')
    cmake.append('    "${CMAKE_SOURCE_DIR}/include"')
    cmake.append(")")
    cmake.append("")
    cmake.append("target_compile_definitions(${PLUGIN_NAME} PRIVATE")
    cmake.append("    PLUGIN_NAME=" + plugin_name)
    # Serialization is always compiled in - no per-type defines needed.
    cmake.append(")")
    cmake.append("")
    cmake.append("set_target_properties(${PLUGIN_NAME} PROPERTIES")
    cmake.append("    CXX_STANDARD 17")
    cmake.append("    CXX_STANDARD_REQUIRED ON")
    cmake.append(")")
    cmake.append("")
    cmake.append("target_link_libraries(${PLUGIN_NAME} PUBLIC")
    cmake.append("    pybind11::embed")
    cmake.append("    spdlog::spdlog")
    cmake.append("    Boost::serialization")
    cmake.append(")")
    cmake.append("")
    cmake.append("# Link OpenCV if available (needed for mat_* bridge fields)")
    cmake.append("if(TARGET opencv_core)")
    cmake.append("    target_link_libraries(${PLUGIN_NAME} PUBLIC opencv_core)")
    cmake.append("elseif(OpenCV_FOUND)")
    cmake.append("    target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBS})")
    cmake.append("endif()")
    cmake.append("")
    cmake.append("install(TARGETS ${PLUGIN_NAME} DESTINATION lib)")
    cmake.append("")
    return "\n".join(cmake)


# ---------------------------------------------------------------------------
# Plugin source generation
# ---------------------------------------------------------------------------

def _include_for_type(type_name, gen_dotted_names):
    if type_name in gen_dotted_names:
        return f'"illixr/bridge/{dotted_to_path(type_name)}.hpp"'
    return f'"illixr/data_format/{type_name}.hpp"'


def _cpp_type_for_switchboard(type_name, gen_dotted_names):
    """Return the C++ type name to use in switchboard::reader/writer<T>."""
    if type_name in gen_dotted_names:
        return dotted_to_cpp_ns(type_name)
    return type_name  # ILLIXR system type, already in ILLIXR namespace


def gen_plugin_hpp(bridge, gen_dotted_names):
    plugin_name = bridge["name"]
    all_types = (
            {inp["type"] for inp in bridge["inputs"]} |
            {out["type"] for out in bridge["outputs"]}
    )
    type_includes = sorted(_include_for_type(t, gen_dotted_names) for t in all_types)

    hpp = []
    hpp.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    hpp.append("// SPDX-License-Identifier: BSL-1.0")
    hpp.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    hpp.append("")
    hpp.append("#pragma once")
    hpp.append("")
    hpp.append('#include "illixr/plugin.hpp"')
    hpp.append('#include "illixr/switchboard.hpp"')
    for inc in type_includes:
        hpp.append(f"#include {inc}")
    hpp.append("")
    hpp.append("#include <pybind11/embed.h>")
    hpp.append("#include <string>")
    hpp.append("#include <thread>")
    hpp.append("#include <unordered_map>")
    hpp.append("")
    hpp.append("namespace py = pybind11;")
    hpp.append("")
    hpp.append("namespace ILLIXR {")
    hpp.append("")
    hpp.append(f"class {plugin_name} : public plugin {{")
    hpp.append("public:")
    hpp.append(f"    [[maybe_unused]] explicit {plugin_name}(const std::string& name, phonebook* pb);")
    hpp.append(f"    ~{plugin_name}() override;")
    hpp.append("")
    hpp.append("    void start() override;")
    hpp.append("")
    hpp.append("private:")
    hpp.append("    void run_python_thread();")
    hpp.append("    void parse_py_args(const std::string& input);")
    hpp.append("")
    hpp.append("    const std::shared_ptr<switchboard> switchboard_;")
    for inp in bridge["inputs"]:
        cpp_t = _cpp_type_for_switchboard(inp["type"], gen_dotted_names)
        hpp.append(f"    switchboard::reader<{cpp_t}> {inp['alias']}_reader_;")
    for out in bridge["outputs"]:
        cpp_t = _cpp_type_for_switchboard(out["type"], gen_dotted_names)
        hpp.append(
            f"    switchboard::{'network_writer' if out['network'] != 'none' else 'writer'}<{cpp_t}> {out['alias']}_writer_;")
    hpp.append("")
    hpp.append("    pybind11::scoped_interpreter guard_;")
    hpp.append("    pybind11::gil_scoped_release  release_;")
    hpp.append("    std::thread                   py_thread_;")
    hpp.append("")
    # Static topic_config builders - one per transport type used.
    # Sequential field assignment avoids designated initializer syntax
    # which is not supported on MSVC.
    _net_types = set(out["network"] for out in bridge["outputs"]
                     if out["network"] != "none")
    if "tcp" in _net_types or "any" in _net_types:
        hpp.append("    static network::topic_config _make_tcp_config() {")
        hpp.append("        network::topic_config cfg;")
        hpp.append("        cfg.serialization_method = network::topic_config::BOOST;")
        hpp.append("        cfg.transport_method = network::topic_config::TCP;")
        hpp.append("        return cfg;")
        hpp.append("    }")
    if "udp" in _net_types:
        hpp.append("    static network::topic_config _make_udp_config() {")
        hpp.append("        network::topic_config cfg;")
        hpp.append("        cfg.serialization_method = network::topic_config::BOOST;")
        hpp.append("        cfg.transport_method = network::topic_config::UDP;")
        hpp.append("        return cfg;")
        hpp.append("    }")
    hpp.append("    std::unordered_map<std::string, std::string> py_args_;")
    hpp.append("    std::string                   py_exe_;")
    hpp.append("};")
    hpp.append("")
    hpp.append("} // namespace ILLIXR")
    hpp.append("")
    return "\n".join(hpp)


def _gen_reader_dict_body(td, gen_dotted_names, var='val', indent='                ',
                          dict_var='_d', type_defs_map=None):
    """
    Generate C++ lines that build a py::dict from a struct value.
    Returns a list of C++ source line strings.
    """
    gen_set = set(gen_dotted_names)
    result = []
    for fname, fdef in td["fields"].items():
        ftype     = fdef["type"]
        container = fdef.get("container")
        shape     = fdef.get("shape")
        image     = fdef.get("image")

        if is_mat(ftype):
            np_dtype = MAT_TYPES[ftype][1][3:]  # strip "np."
            result.append(indent + "{")
            result.append(indent + f"    const auto& _m = {var}.{fname}_;")
            result.append(indent +  "    if (!_m.empty()) {")
            result.append(indent +  "        std::vector<ssize_t> _shp, _str;")
            result.append(indent +  "        if (_m.channels() == 1) {")
            result.append(indent +  "            _shp = {_m.rows, _m.cols};")
            result.append(indent +  "            _str = {(ssize_t)_m.step[0], (ssize_t)_m.elemSize()};")
            result.append(indent +  "        } else {")
            result.append(indent +  "            _shp = {_m.rows, _m.cols, _m.channels()};")
            result.append(indent +  "            _str = {(ssize_t)_m.step[0], (ssize_t)_m.step[1], (ssize_t)_m.elemSize1()};")
            result.append(indent +  "        }")
            result.append(indent + f"        {dict_var}[\"{fname}\"] = py::array(")
            result.append(indent + f'            py::dtype("{np_dtype}"), _shp, _str, _m.data);')
            result.append(indent +  "    } else {")
            result.append(indent + f"        {dict_var}[\"{fname}\"] = py::none();")
            result.append(indent +  "    }")
            result.append(indent +  "}")

        elif shape and not container:
            ctype = canonical_type(ftype) if ftype not in gen_set else None
            if ctype:
                cpp_t = cpp_scalar(ctype)
                if len(shape) == 1:
                    flat = shape[0]
                    ptr  = f"{var}.{fname}_"
                elif len(shape) == 2:
                    flat = shape[0] * shape[1]
                    ptr  = f"&{var}.{fname}_[0][0]"
                else:
                    flat = shape[0] * shape[1] * shape[2]
                    ptr  = f"&{var}.{fname}_[0][0][0]"
                result.append(indent +
                    f"{dict_var}[\"{fname}\"] = py::array_t<{cpp_t}>"
                    f"({{{flat}}}, {{sizeof({cpp_t})}},"
                    f"reinterpret_cast<const {cpp_t}*>({ptr}));")

        elif image:
            # Image field (type: image): stored as flat row-major (H, W, ch) bytes.
            # numpy output: (H, W, ch) with C-contiguous strides (W*ch, ch, 1),
            # matching entry_to_numpy. YAML shape [width, height, ch] is user-natural
            # but the array axes follow the standard image convention.
            ctype  = canonical_type(ftype)
            cpp_t  = cpp_scalar(ctype)
            w_dim, h_dim, ch = image
            w_expr = str(w_dim) if isinstance(w_dim, int) else f"{var}.{w_dim}_"
            h_expr = str(h_dim) if isinstance(h_dim, int) else f"{var}.{h_dim}_"
            result.append(indent + "{")
            result.append(indent + f"    auto* _vbuf_{fname} = "
                                    f"new std::vector<{cpp_t}>({var}.{fname}_);")
            result.append(indent + f"    py::capsule _cap_{fname}("
                                    f"_vbuf_{fname}, [](void* p) {{"
                                    f"delete static_cast<std::vector<{cpp_t}>*>(p); }});")
            result.append(indent +
                f"    {dict_var}[\"{fname}\"] = py::array_t<{cpp_t}>(")
            result.append(indent +
                f"        {{{h_expr}, {w_expr}, {ch}}},")
            result.append(indent +
                f"        {{(py::ssize_t)({w_expr} * {ch} * sizeof({cpp_t})),"
                f" (py::ssize_t)({ch} * sizeof({cpp_t})),"
                f" (py::ssize_t)sizeof({cpp_t})}},")
            result.append(indent +
                f"        _vbuf_{fname}->data(), _cap_{fname});")
            result.append(indent + "}")

        elif (container == "vector"
              and shape and any(isinstance(e, str) or isinstance(e, int) for e in shape)
              and ftype not in gen_set):
            # Dynamic multi-dimensional numpy array.
            # Flat vector wrapped with runtime shape from sibling fields.
            # Uses a heap-allocated copy owned by a capsule for safe lifetime.
            ctype        = canonical_type(ftype)
            cpp_t        = cpp_scalar(ctype)
            sf           = fdef["shape"]
            ndim         = len(sf)
            # Each entry in sf is either a string (sibling field name) or
            # an int literal. Build shape and C-contiguous stride expressions.
            def _dim_expr(d):
                return str(d) if isinstance(d, int) else f"{var}.{d}_"
            shape_expr = ", ".join(f"(py::ssize_t){_dim_expr(d)}" for d in sf)
            # C-contiguous strides: stride[i] = product(dim[i+1:]) * sizeof(T)
            stride_parts = []
            for i in range(ndim):
                inner = sf[i+1:]
                if inner:
                    prod = " * ".join(_dim_expr(d) for d in inner)
                    stride_parts.append(f"(py::ssize_t)({prod} * sizeof({cpp_t}))")
                else:
                    stride_parts.append(f"(py::ssize_t)sizeof({cpp_t})")
            stride_expr = ", ".join(stride_parts)
            result.append(indent + "{")
            result.append(indent + f"    auto* _vbuf_{fname} = "
                                    f"new std::vector<{cpp_t}>({var}.{fname}_);")
            result.append(indent + f"    py::capsule _cap_{fname}("
                                    f"_vbuf_{fname}, [](void* p) {{"
                                    f"delete static_cast<std::vector<{cpp_t}>*>(p); }});")
            result.append(indent + f"    {dict_var}[\"{fname}\"] = "
                                    f"py::array_t<{cpp_t}>("
                                    f"{{{shape_expr}}}, {{{stride_expr}}},"
                                    f"_vbuf_{fname}->data(), _cap_{fname});")
            result.append(indent + "}")

        elif ftype in gen_set and not container:
            # Nested bridge-defined struct - build a nested py::dict inline.
            # Cannot use py::cast because the binding module is not imported.
            sub_var  = f"_sub_{fname}"
            sub_dict = f"_dict_{fname}"
            sub_td   = (type_defs_map or {}).get(ftype)
            result.append(indent + "{")
            result.append(indent + f"    const auto& {sub_var} = {var}.{fname}_;")
            result.append(indent + f"    py::dict {sub_dict};")
            if sub_td:
                sub_lines = _gen_reader_dict_body(
                    sub_td, gen_dotted_names,
                    var=sub_var, indent=indent + "    ",
                    dict_var=sub_dict, type_defs_map=type_defs_map)
                result.extend(sub_lines)
            result.append(indent + f"    {dict_var}[\"{fname}\"] = {sub_dict};")
            result.append(indent + "}")

        elif container == "vector" and ftype in gen_set:
            # Vector of bridge-defined structs - build a py::list of dicts.
            elem_var  = f"_elem_{fname}"
            elem_dict = f"_edict_{fname}"
            list_var  = f"_list_{fname}"
            elem_td   = (type_defs_map or {}).get(ftype)
            result.append(indent + "{")
            result.append(indent + f"    py::list {list_var};")
            result.append(indent + f"    for (const auto& {elem_var} : {var}.{fname}_) {{")
            result.append(indent + f"        py::dict {elem_dict};")
            if elem_td:
                elem_lines = _gen_reader_dict_body(
                    elem_td, gen_dotted_names,
                    var=elem_var, indent=indent + "        ",
                    dict_var=elem_dict, type_defs_map=type_defs_map)
                result.extend(elem_lines)
            result.append(indent + f"        {list_var}.append({elem_dict});")
            result.append(indent +  "    }")
            result.append(indent + f"    {dict_var}[\"{fname}\"] = {list_var};")
            result.append(indent + "}")

        else:
            # Scalars, vectors of scalars, dicts - pybind11 built-in converters handle these.
            result.append(indent + f"{dict_var}[\"{fname}\"] = py::cast({var}.{fname}_);")

    return result

def _gen_writer_from_dict_body(td, gen_dotted_names, src='d', dest='data',
                               indent='                    ',
                               type_defs_map=None):
    """
    Generate C++ lines that populate a struct from a py::dict.
    Each dict key (without trailing _) is assigned to the matching field.
    Returns a list of C++ source line strings.
    """
    result = []
    # Fields referenced by shape_fields of another field are populated
    # automatically from the numpy array shape - skip them in the dict loop.
    # Fields auto-populated from shape or image array dimensions
    _auto_populated = set()
    for _fd in td["fields"].values():
        # shape dynamic dims
        if _fd.get("container") == "vector":
            for _sf in (_fd.get("shape") or []):
                if isinstance(_sf, str):
                    _auto_populated.add(_sf)
        # image width/height dims
        _img = _fd.get("image")
        if _img:
            w_dim, h_dim, _ = _img
            if isinstance(w_dim, str):
                _auto_populated.add(w_dim)
            if isinstance(h_dim, str):
                _auto_populated.add(h_dim)

    for fname, fdef in td["fields"].items():
        if fname in _auto_populated:
            continue
        ftype     = fdef["type"]
        container = fdef.get("container")
        shape     = fdef.get("shape")
        image     = fdef.get("image")
        key       = fname

        result.append(indent + f'if ({src}.contains("{key}")) {{')

        if is_mat(ftype):
            cpp_elem = MAT_TYPES[ftype][2]
            cv_base  = MAT_TYPES[ftype][0]
            result.append(indent + f'    auto _arr = {src}["{key}"].cast<py::array_t<{cpp_elem}>>();')
            result.append(indent +  "    auto _buf = _arr.request();")
            result.append(indent +  "    int _r = (int)_buf.shape[0], _c = (int)_buf.shape[1];")
            result.append(indent +  "    int _ch = (_buf.ndim == 3) ? (int)_buf.shape[2] : 1;")
            result.append(indent + f"    cv::Mat _tmp(_r, _c, CV_MAKETYPE({cv_base}(_ch), _ch), _buf.ptr);")
            result.append(indent + f"    _tmp.copyTo({dest}.{fname}_);")

        elif shape and not container:
            ctype = canonical_type(ftype) if ftype not in set(gen_dotted_names) else None
            if ctype:
                cpp_t = cpp_scalar(ctype)
                if len(shape) == 1:
                    flat = shape[0]
                    ptr  = f"{dest}.{fname}_"
                elif len(shape) == 2:
                    flat = shape[0] * shape[1]
                    ptr  = f"&{dest}.{fname}_[0][0]"
                else:
                    flat = shape[0] * shape[1] * shape[2]
                    ptr  = f"&{dest}.{fname}_[0][0][0]"
                result.append(indent + f'    auto _a = {src}["{key}"].cast<py::array_t<{cpp_t}>>();')
                result.append(indent + f"    if (_a.size() >= {flat})")
                result.append(indent + f"        std::copy(_a.data(), _a.data()+{flat}, {ptr});")

        elif image:
            # Image field (type: image): Python sends (H, W, ch) array matching
            # the reader output. Since it is C-contiguous, just flatten and copy.
            ctype  = canonical_type(ftype)
            cpp_t  = cpp_scalar(ctype)
            w_dim, h_dim, ch = image
            result.append(indent + f'if ({src}.contains("{key}")) {{')
            result.append(indent +  '    {')
            result.append(indent + f'        auto _np = {src}["{key}"].cast<py::array_t<{cpp_t}>>();')
            result.append(indent +  '        auto _buf = _np.request();')
            result.append(indent + f'        {dest}.{fname}_.assign(')
            result.append(indent + f'            static_cast<const {cpp_t}*>(_buf.ptr),')
            result.append(indent + f'            static_cast<const {cpp_t}*>(_buf.ptr) + _buf.size);')
            if isinstance(w_dim, str):
                result.append(indent + f'        {dest}.{w_dim}_ = (_buf.ndim > 1) ? (int32_t)_buf.shape[1] : 0;')
            if isinstance(h_dim, str):
                result.append(indent + f'        {dest}.{h_dim}_ = (_buf.ndim > 0) ? (int32_t)_buf.shape[0] : 0;')
            result.append(indent +  '    }')
            result.append(indent +  '}')

        elif (container == "vector"
              and shape and any(isinstance(e, str) or isinstance(e, int) for e in shape)
              and ftype not in set(gen_dotted_names)):
            # Dynamic numpy array: accept array, extract flat data and populate
            # sibling dimension fields from the array's shape attribute.
            ctype = canonical_type(ftype)
            cpp_t = cpp_scalar(ctype)
            sf    = fdef["shape"]
            result.append(indent +  "    {")
            result.append(indent + f"        auto _np = {src}[\"{key}\"].cast<py::array_t<{cpp_t}>>();")
            result.append(indent +  "        auto _buf = _np.request();")
            result.append(indent + f"        {dest}.{fname}_.assign(")
            result.append(indent + f"            static_cast<const {cpp_t}*>(_buf.ptr),")
            result.append(indent + f"            static_cast<const {cpp_t}*>(_buf.ptr) + _buf.size);")
            for di, sf_entry in enumerate(sf):
                if isinstance(sf_entry, int):
                    continue  # literal - no field to populate
                result.append(indent + f"        if (_buf.ndim > {di})")
                result.append(indent + f"            {dest}.{sf_entry}_ = (int32_t)_buf.shape[{di}];")
            result.append(indent +  "    }")

        elif container == "vector" and ftype in set(gen_dotted_names):
            # Vector of bridge-defined structs: iterate the Python list,
            # populate each element from its dict field-by-field.
            # Cannot use cast<std::vector<T>>() - T is not registered as pybind11 type.
            cpp_t  = _cpp_type_for_switchboard(ftype, gen_dotted_names)
            result.append(indent + f'    if (py::isinstance<py::list>({src}["{key}"])) {{')
            result.append(indent + f'        auto _lst = {src}["{key}"].cast<py::list>();')
            result.append(indent + f'        {dest}.{fname}_.clear();')
            result.append(indent + f'        {dest}.{fname}_.reserve(_lst.size());')
            result.append(indent + f'        for (auto _item : _lst) {{')
            result.append(indent + f'            if (!py::isinstance<py::dict>(_item)) continue;')
            result.append(indent + f'            {cpp_t} _elem;')
            result.append(indent + f'            auto _ed = _item.cast<py::dict>();')
            # Inline the sub-struct field assignments
            if ftype in (type_defs_map or {}):
                sub_td = type_defs_map[ftype]
                sub_lines = _gen_writer_from_dict_body(
                    sub_td, gen_dotted_names,
                    src='_ed', dest='_elem',
                    indent=indent + '            ',
                    type_defs_map=type_defs_map)
                result.extend(sub_lines)
            result.append(indent + f'            {dest}.{fname}_.push_back(std::move(_elem));')
            result.append(indent + f'        }}')
            result.append(indent + f'    }}')

        elif container == "vector":
            elem = cpp_scalar(canonical_type(ftype))
            result.append(indent + f'    {dest}.{fname}_ = {src}["{key}"].cast<std::vector<{elem}>>();')

        elif container == "dict":
            ct = cpp_scalar(canonical_type(ftype))
            result.append(indent + f'    {dest}.{fname}_ = {src}["{key}"].cast<std::unordered_map<std::string,{ct}>>();')

        else:
            if ftype in ("string", "str") or canonical_type(ftype) == "std::string":
                result.append(indent + f'    {dest}.{fname}_ = {src}["{key}"].cast<std::string>();')
            elif ftype in set(gen_dotted_names):
                cpp_t = _cpp_type_for_switchboard(ftype, gen_dotted_names)
                result.append(indent + f'    {dest}.{fname}_ = {src}["{key}"].cast<{cpp_t}>();')
            else:
                ct = cpp_scalar(canonical_type(ftype))
                result.append(indent + f'    {dest}.{fname}_ = {src}["{key}"].cast<{ct}>();')

        result.append(indent + "}")

    return result

def gen_plugin_cpp(bridge, gen_dotted_names, script_yaml_default,
                   plugin_dir: Path, type_defs_map=None):
    plugin_name = bridge["name"]
    plugin_name_upper = plugin_name.upper()
    # Environment variable names follow ILLIXR convention: ILLIXR_<NAME>_<KEY>
    env_script = f"ILLIXR_{plugin_name_upper}_SCRIPT"
    env_args = f"ILLIXR_{plugin_name_upper}_ARGS"

    cpp = []
    cpp.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    cpp.append("// SPDX-License-Identifier: BSL-1.0")
    cpp.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    cpp.append("")
    cpp.append('#include "plugin.hpp"')
    cpp.append("")
    cpp.append("#include <pybind11/embed.h>")
    cpp.append("#include <pybind11/numpy.h>")
    cpp.append("#include <pybind11/stl.h>")
    cpp.append("#include <spdlog/spdlog.h>")
    cpp.append("")
    cpp.append("#include <filesystem>")
    cpp.append("#include <stdexcept>")
    cpp.append("#include <string>")
    cpp.append("#include <thread>")
    cpp.append("")
    cpp.append("namespace py = pybind11;")
    cpp.append("")
    cpp.append("using namespace ILLIXR;")
    cpp.append("")

    # -----------------------------------------------------------------------
    # Constructor
    # -----------------------------------------------------------------------
    cpp.append(f"[[maybe_unused]] {plugin_name}::{plugin_name}(const std::string& name, phonebook* pb)")
    cpp.append(f"    : plugin{{name, pb}}")
    cpp.append(f"    , switchboard_{{pb->lookup_impl<switchboard>()}}")
    for inp in bridge["inputs"]:
        cpp_t = _cpp_type_for_switchboard(inp["type"], gen_dotted_names)
        cpp.append(f'    , {inp["alias"]}_reader_{{switchboard_->get_reader<{cpp_t}>("{inp["topic"]}")}}')
    for out in bridge["outputs"]:
        cpp_t = _cpp_type_for_switchboard(out["type"], gen_dotted_names)
        net = out["network"]
        alias = out["alias"]
        topic = out["topic"]
        if net == "none":
            cpp.append(
                f'    , {alias}_writer_{{switchboard_->'
                f'get_writer<{cpp_t}>("{topic}")}}'
            )
        else:
            cfg_fn = "_make_udp_config" if net == "udp" else "_make_tcp_config"
            cpp.append(
                f'    , {alias}_writer_{{switchboard_->'
                f'get_network_writer<{cpp_t}>("{topic}", {cfg_fn}())}}'
            )
    cpp.append("    , guard_{}")
    cpp.append("    , release_{} {")
    cpp.append("")
    cpp.append("    // Set up venv site-packages if VIRTUAL_ENV is set.")
    cpp.append("    const char* venv = std::getenv(\"VIRTUAL_ENV\");")
    cpp.append("    if (venv) {")
    cpp.append("        py::gil_scoped_acquire acquire;")
    cpp.append("        py::module_ sys  = py::module_::import(\"sys\");")
    cpp.append("        py::module_ site = py::module_::import(\"site\");")
    cpp.append("        auto vi    = sys.attr(\"version_info\");")
    cpp.append("        int major  = vi.attr(\"major\").cast<int>();")
    cpp.append("        int minor  = vi.attr(\"minor\").cast<int>();")
    cpp.append("        std::string sp = std::string(venv) + \"/lib/python\" +")
    cpp.append("                         std::to_string(major) + \".\" +")
    cpp.append("                         std::to_string(minor) + \"/site-packages\";")
    cpp.append("        py::list path = sys.attr(\"path\");")
    cpp.append("        path.attr(\"insert\")(0, sp);")
    cpp.append("        site.attr(\"addsitedir\")(sp);")
    cpp.append("    }")
    cpp.append("")
    cpp.append(f'    // Script path: env var {env_script} overrides YAML default.')
    cpp.append(f'    const char* env_script = std::getenv("{env_script}");')
    cpp.append(f'    py_exe_ = env_script ? std::string(env_script)')
    cpp.append(f'                         : std::string("{script_yaml_default}");')
    cpp.append(f'    if (py_exe_.empty())')
    cpp.append(
        f'        throw std::runtime_error("[{plugin_name}] No script path: set {env_script} or provide script: in the bridge yaml.");')
    cpp.append("")
    cpp.append(f'    std::string arg_string = switchboard_->get_env("{env_args}", "");')
    cpp.append("    parse_py_args(arg_string);")
    cpp.append(f'    spdlog::get("illixr")->debug("[{plugin_name}] script: {{}}  args: {{}}", py_exe_, arg_string);')
    cpp.append("}")
    cpp.append("")

    # -----------------------------------------------------------------------
    # Destructor
    # -----------------------------------------------------------------------
    cpp.append(f"{plugin_name}::~{plugin_name}() {{")
    cpp.append("    if (py_thread_.joinable())")
    cpp.append("        py_thread_.join();")
    cpp.append("}")
    cpp.append("")

    # -----------------------------------------------------------------------
    # start()
    # -----------------------------------------------------------------------
    cpp.append(f"void {plugin_name}::start() {{")
    cpp.append(f"    py_thread_ = std::thread(&{plugin_name}::run_python_thread, this);")
    cpp.append("}")
    cpp.append("")

    # -----------------------------------------------------------------------
    # parse_py_args()  - identical to semantic_xr implementation
    # -----------------------------------------------------------------------
    cpp.append(f"void {plugin_name}::parse_py_args(const std::string& input) {{")
    cpp.append("    std::string::size_type start = 0;")
    cpp.append("    while (start < input.size()) {")
    cpp.append("        auto comma = input.find(',', start);")
    cpp.append("        if (comma == std::string::npos) comma = input.size();")
    cpp.append("        std::string token = input.substr(start, comma - start);")
    cpp.append("        if (!token.empty()) {")
    cpp.append("            auto eq = token.find('=');")
    cpp.append("            if (eq == std::string::npos) {")
    cpp.append('                py_args_[token] = "";')
    cpp.append("            } else if (eq == 0) {")
    cpp.append(f'                spdlog::get("illixr")->warn("[{plugin_name}] Malformed arg token \'{{}}\'", token);')
    cpp.append("            } else {")
    cpp.append("                py_args_[token.substr(0, eq)] = token.substr(eq + 1);")
    cpp.append("            }")
    cpp.append("        }")
    cpp.append("        start = comma + 1;")
    cpp.append("    }")
    cpp.append("}")
    cpp.append("")

    # -----------------------------------------------------------------------
    # run_python_thread()
    # -----------------------------------------------------------------------
    cpp.append(f"void {plugin_name}::run_python_thread() {{")
    cpp.append("    py::gil_scoped_acquire acquire;")
    cpp.append("    try {")
    cpp.append("        // Build sys.argv from parsed args")
    cpp.append("        py::list argv;")
    cpp.append("        argv.append(py_exe_);")
    cpp.append("        for (const auto& [key, val] : py_args_) {")
    cpp.append('            argv.append((key.size() == 1 ? "-" : "--") + key);')
    cpp.append('            if (!val.empty()) argv.append(val);')
    cpp.append("        }")
    cpp.append('        py::module_::import("sys").attr("argv") = argv;')
    cpp.append("")
    cpp.append(f'        // Add the plugin package dir so illixr.bridge.* resolves.')
    cpp.append(f'        py::module_ sys_mod = py::module_::import("sys");')
    cpp.append(f'        sys_mod.attr("path").attr("insert")(0, std::string("{plugin_dir}"));')
    cpp.append("")
    cpp.append("        // Inject reader/writer handles into globals.")
    cpp.append("        // Scripts access these as bare globals, no import needed.")
    cpp.append("        py::dict globals = py::globals();")
    cpp.append("")

    # Inject readers - each reader.get() returns a py::dict
    for inp in bridge['inputs']:
        alias       = inp['alias']
        itype       = inp['type']
        global_name = f'illixr_{alias}_reader'
        cpp_t       = _cpp_type_for_switchboard(itype, gen_dotted_names)
        td_for_type = (type_defs_map or {}).get(itype)

        cpp.append(f'        // {global_name}.get() -> py::dict or None')
        cpp.append(f'        struct Py_{alias}_reader {{')
        cpp.append(f'            {plugin_name}* self_;')
        cpp.append(f'            py::object get() const {{')
        cpp.append(f'                auto val = self_->{alias}_reader_.get_ro_nullable();')
        cpp.append( '                if (!val) return py::none();')
        cpp.append( '                py::dict _d;')
        if td_for_type:
            # val is a switchboard::ptr<const T> (shared_ptr-like),
            # so fields are accessed via val->field_ not val.field_
            for dict_line in _gen_reader_dict_body(
                    td_for_type, gen_dotted_names, var="(*val)",
                    type_defs_map=type_defs_map):
                cpp.append(dict_line)
        else:
            # ILLIXR system type - fall back to exposing raw cast
            cpp.append(f'                _d["value"] = py::cast(*val);')
        cpp.append( '                return _d;')
        cpp.append(f'            }}')
        cpp.append(f'        }};')
        cpp.append( '        {')
        cpp.append( '        auto _mreg = py::module_::import("__main__");')
        cpp.append(f'        py::class_<Py_{alias}_reader>(_mreg,')
        cpp.append(f'            "_{plugin_name}_{alias}_reader_t")')
        cpp.append(f'            .def("get", &Py_{alias}_reader::get);')
        cpp.append( '        }')
        cpp.append(f'        globals["{global_name}"] = Py_{alias}_reader{{this}};')
        cpp.append('')

    # Inject writers
    for out in bridge['outputs']:
        alias       = out['alias']
        cpp_t       = _cpp_type_for_switchboard(out['type'], gen_dotted_names)
        global_name = f'illixr_{alias}_writer'
        td_out      = (type_defs_map or {}).get(out['type'])

        cpp.append(f"        // {global_name}.put(dict) -> publish to '{out['topic']}'")
        cpp.append(f'        struct Py_{alias}_writer {{')
        cpp.append(f'            {plugin_name}* self_;')
        cpp.append( '            void put(py::object val) const {')
        cpp.append( '                if (!py::isinstance<py::dict>(val)) {')
        cpp.append(f'                    throw py::type_error("illixr_{alias}_writer.put(): expected a dict");')
        cpp.append( '                }')
        cpp.append(f'                {cpp_t} data;')
        cpp.append( '                auto d = val.cast<py::dict>();')
        if td_out:
            for wline in _gen_writer_from_dict_body(td_out, gen_dotted_names,
                                                    type_defs_map=type_defs_map):
                cpp.append(wline)
        cpp.append(f'                auto ev = self_->{alias}_writer_.allocate(std::move(data));')
        cpp.append(f'                self_->{alias}_writer_.put(std::move(ev));')
        cpp.append( '            }')
        cpp.append( '        };')
        cpp.append( '        {')
        cpp.append( '        auto _mreg = py::module_::import("__main__");')
        cpp.append(f'        py::class_<Py_{alias}_writer>(_mreg,')
        cpp.append(f'            "_{plugin_name}_{alias}_writer_t")')
        cpp.append(f'            .def("put", &Py_{alias}_writer::put);')
        cpp.append( '        }')
        cpp.append(f'        globals["{global_name}"] = Py_{alias}_writer{{this}};')
        cpp.append('')

    # Inject subscribe callable - callback receives a py::dict
    cpp.append('        globals["illixr_subscribe"] = py::cpp_function(')
    cpp.append('            [this](const std::string& alias, py::object callback) {')
    for inp in bridge['inputs']:
        inp_alias = inp['alias']
        inp_topic = inp['topic']
        inp_t     = _cpp_type_for_switchboard(inp['type'], gen_dotted_names)
        td_inp    = (type_defs_map or {}).get(inp['type'])
        cpp.append(f'                if (alias == "{inp_alias}") {{')
        cpp.append(f'                    switchboard_->schedule<{inp_t}>(')
        cpp.append(f'                        id_, "{inp_topic}",')
        cpp.append(f'                        [cb = callback](switchboard::ptr<const {inp_t}> val, std::size_t) {{')
        cpp.append( '                            py::gil_scoped_acquire gil;')
        cpp.append( '                            py::dict _d;')
        if td_inp:
            for dline in _gen_reader_dict_body(td_inp, gen_dotted_names,
                    var='(*val)', indent='                            ',
                    dict_var='_d', type_defs_map=type_defs_map):
                cpp.append(dline)
        cpp.append( '                            cb(_d);')
        cpp.append( '                        });')
        cpp.append( '                    return;')
        cpp.append( '                }')
    cpp.append('                throw py::value_error(')
    cpp.append('                    "illixr_subscribe: unknown alias \'" + alias + "\'.");')
    cpp.append('            });')
    cpp.append('')


    # Run the script
    cpp.append(f'        spdlog::get("illixr")->info("[{plugin_name}] Running script: {{}}", py_exe_);')
    cpp.append("        py::eval_file(py_exe_, globals);")
    cpp.append("")
    cpp.append("    } catch (const py::error_already_set& e) {")
    cpp.append(f'        spdlog::get("illixr")->error("[{plugin_name}] Python exception: {{}}", e.what());')
    cpp.append("    } catch (const std::exception& e) {")
    cpp.append(f'        spdlog::get("illixr")->error("[{plugin_name}] Exception: {{}}", e.what());')
    cpp.append("    }")
    cpp.append("}")
    cpp.append("")
    cpp.append(f"PLUGIN_MAIN({plugin_name})")
    cpp.append("")
    return "\n".join(cpp)


# ---------------------------------------------------------------------------
# Tier 1 - profile YAML generation
# ---------------------------------------------------------------------------

def write_profile_yaml_files(master_path, profiles_dir):
    with open(master_path) as f:
        raw = yaml.safe_load(f)

    if not isinstance(raw, dict):
        raise SchemaError(
            f"{master_path}: top level must be a mapping of profile names")

    for profile_name, profile_data in raw.items():
        if not isinstance(profile_data, dict):
            raise SchemaError(
                f"{master_path}: profile '{profile_name}' must be a mapping")
        bridges_val = profile_data.get("bridges", "")
        if not bridges_val:
            raise SchemaError(
                f"{master_path}: profile '{profile_name}' missing 'bridges' key")
        bridges_str = re.sub(r'\s*,\s*', ',', str(bridges_val).strip())
        out_path = Path(profiles_dir) / f"{profile_name}.yaml"
        out_path.write_text(
            "# This file was auto-generated from python_profiles.yaml"
            " -- do not edit directly.\n"
            f"bridges: {bridges_str}\n"
        )


# ---------------------------------------------------------------------------
# Tier 2 - struct headers + plugin sources
# ---------------------------------------------------------------------------

def cmake_list(items):
    return ";".join(str(i) for i in items)


def _file_md5(path: Path) -> str:
    """Return hex MD5 of file content, or empty string if file missing."""
    try:
        return hashlib.md5(path.read_bytes()).hexdigest()
    except OSError:
        return ""


def _load_state(build_dir: Path) -> dict:
    """Load the bridge state JSON file, returning empty dict if absent."""
    state_file = build_dir / ".py_bridge_state.json"
    try:
        return json.loads(state_file.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def _save_state(build_dir: Path, state: dict) -> None:
    """Persist the bridge state JSON file."""
    state_file = build_dir / ".py_bridge_state.json"
    state_file.write_text(json.dumps(state, indent=2, sort_keys=True))


def _bridge_stale(bname: str, bridge_yaml: Path,
                  type_yaml_paths_for_bridge: list[Path],
                  state: dict) -> bool:
    """
    Return True if the bridge needs regeneration.

    Compares MD5 hashes of the bridge yaml and all its type yamls
    against the values stored in the state file from the previous run.
    Always returns True if any file is missing from the state.
    """
    entry = state.get(bname, {})
    if not entry:
        return True

    # Check bridge yaml hash
    current_bridge_hash = _file_md5(bridge_yaml)
    if entry.get("bridge_hash") != current_bridge_hash:
        return True

    # Check type yaml hashes
    stored_type_hashes = entry.get("type_hashes", {})
    for tpath in type_yaml_paths_for_bridge:
        current = _file_md5(tpath)
        if stored_type_hashes.get(str(tpath)) != current:
            return True
    # Also stale if a previously tracked yaml is now gone
    for stored_path in stored_type_hashes:
        if not Path(stored_path).exists():
            return True

    return False



def run_generate(bridge_profile_path, build_dir, source_dir):
    bridges_dir = source_dir / "interfaces" / "python" / "bridges"
    data_dir = source_dir / "interfaces" / "data"

    # Read bridge profile
    try:
        with open(bridge_profile_path) as f:
            bp_raw = yaml.safe_load(f)
    except Exception as e:
        print(f'message(FATAL_ERROR "Cannot read bridge profile '
              f'\'{bridge_profile_path}\': {e}")')
        sys.exit(1)

    bridges_str = bp_raw.get("bridges", "")
    bridge_names = [b.strip() for b in str(bridges_str).split(",") if b.strip()]
    if not bridge_names:
        print(f'message(FATAL_ERROR "Bridge profile \'{bridge_profile_path}\' '
              f'has no bridges listed")')
        sys.exit(1)

    # Read and validate bridge descriptors; check for duplicates
    bridge_raws = []
    seen_names = {}
    for bridge_name in bridge_names:
        # Bridge names are plain snake_case plugin names -- never dotted.
        # A dot here almost always means the user put a type dotted name
        # (e.g., semantic_xr.semantic_data) in the profile bridges: list
        # instead of in the bridge descriptor types: list.
        if "." in bridge_name:
            error_msg = (
                f"Bridge name '{bridge_name}' in profile '{bridge_profile_path}' "
                "contains a dot, which is not allowed. "
                "The bridges: list must contain bridge plugin names "
                "(e.g. semantic_xr), not dotted type names "
                "(e.g. semantic_xr.semantic_data). "
                "Dotted type names belong in the bridge descriptor "
                "types: list, not in the profile."
            )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        if not re.match(r'^[a-z][a-z0-9_]*$', bridge_name):
            error_msg = f"Bridge name '{bridge_name}' must be lowercase snake_case (no dots)."
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        bridge_path = bridges_dir / f"{bridge_name}.yaml"
        if not bridge_path.exists():
            error_msg = (
                f"Bridge descriptor not found: {bridge_path}. "
                "The bridges: list must contain plugin names matching "
                "files in interfaces/python/bridges/. "
                "Dotted type names (e.g. semantic_xr.semantic_data) "
                "belong in the bridge descriptor types: list."
            )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        try:
            with open(bridge_path) as bridge_file:
                bridge_raw = yaml.safe_load(bridge_file)
        except Exception as e:
            error_msg = f"Cannot read bridge descriptor '{bridge_path}': {e}"
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        # Bridge name is the YAML file's stem - no 'name:' key in the file.
        declared = bridge_path.stem
        if declared in seen_names:
            error_msg = (
                f"Duplicate bridge name '{declared}' in profile "
                f"'{bridge_profile_path}': both '{seen_names[declared]}' "
                f"and '{bridge_path}' have the same stem."
            )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        seen_names[declared] = str(bridge_path)
        bridge_raws.append((bridge_path, bridge_raw))

    # Pass 1: collect candidate dotted type names from all bridge topic entries.
    # Types are now discovered from 'type:' fields directly - no 'types:' key.
    candidate_dotted: set[str] = set()
    for bridge_path, bridge_raw in bridge_raws:
        for section in ("inputs", "outputs"):
            for entry in bridge_raw.get(section, []):
                type_name = entry.get("type", "")
                if type_name and type_name not in KNOWN_ILLIXR_TYPES:
                    candidate_dotted.add(type_name)

    # Load and validate all referenced type YAML files.
    # Build the complete name set before validating any individual type
    # so that cross-struct field references within the profile resolve.
    all_dotted_yaml: set[str] = set()
    type_yaml_raw: dict[str, dict] = {}
    type_yaml_paths: dict[str, Path] = {}

    def load_type_yaml(type_name_: str) -> None:
        """Recursively load a type YAML and all bridge types it references."""
        if type_name_ in all_dotted_yaml or type_name_ in KNOWN_ILLIXR_TYPES or type_name_ in IMAGE_TYPES:
            return
        rel = dotted_to_path(type_name_)
        type_path = data_dir / f"{rel}.yaml"
        if not type_path.exists():
            error_msg_ = (
                f"Type yaml not found for '{type_name_}': {type_path}. "
                "Check that the file exists and that the dotted name "
                "matches its path under interfaces/data/."
            )
            print(f'message(FATAL_ERROR "{error_msg_}")')
            sys.exit(1)
        try:
            with open(type_path) as fl:
                type_raw_ = yaml.safe_load(fl)
        except Exception as e_:
            error_msg_ = f"Cannot read type yaml '{type_path}': {e_}"
            print(f'message(FATAL_ERROR "{error_msg_}")')
            sys.exit(1)
        all_dotted_yaml.add(type_name_)
        type_yaml_raw[type_name_] = type_raw_
        type_yaml_paths[type_name_] = type_path
        # Recursively load bridge-defined struct types referenced in fields
        for field_def in type_raw_.get("fields", {}).values():
            field_type = field_def.get("type", "")
            if field_type and not is_scalar(field_type) and not is_mat(field_type):
                load_type_yaml(field_type)

    for type_name in sorted(candidate_dotted):
        load_type_yaml(type_name)

    # Validate all loaded type YAMLs now that the full name set is known
    validated_types = []
    for type_name, type_raw in type_yaml_raw.items():
        try:
            td = validate_type_yaml(type_raw, type_yaml_paths[type_name],
                                    data_dir, all_dotted_yaml)
            validated_types.append(td)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Type schema error: {e}")')
            sys.exit(1)

    sorted_types = topo_sort(validated_types)
    gen_dotted_set = {td["dotted"] for td in sorted_types}
    all_types = gen_dotted_set | set(KNOWN_ILLIXR_TYPES.keys())

    # Pass 2: validate bridge descriptors now that all type names are known
    validated_bridges = []
    for bridge_path, bridge_raw in bridge_raws:
        try:
            bd = validate_bridge_yaml(bridge_raw, bridge_path, all_types)
            validated_bridges.append(bd)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Bridge schema error in \'{bridge_path}\': {e}")')
            sys.exit(1)

    # Determine which bridges need regeneration using the JSON state file.
    # This is fully self-contained in Python with no cmake cache variables.
    def _progress(msg: str) -> None:
        """Print a progress message to stderr for cmake to display."""
        print(msg, file=sys.stderr, flush=True)

    state = _load_state(build_dir)

    # Build dependency walker used by staleness check and emit below
    type_defs_by_dotted_pre = {td["dotted"]: td for td in sorted_types}

    def _collect_deps(dotted, visited=None):
        if visited is None:
            visited = set()
        if dotted in visited or dotted not in type_defs_by_dotted_pre:
            return visited
        visited.add(dotted)
        for fdef in type_defs_by_dotted_pre[dotted]["fields"].values():
            ftype = fdef.get("type", "")
            if ftype in type_defs_by_dotted_pre:
                _collect_deps(ftype, visited)
        return visited

    def _type_yamls_for_bridge(bd_):
        all_deps: set[str] = set()
        for tname in bd_["type_names"]:
            _collect_deps(tname, all_deps)
        return [type_yaml_paths[t] for t in all_deps if t in type_yaml_paths]

    bridges_to_generate = []
    for bd in validated_bridges:
        bname = bd["name"]
        bridge_yaml = bridges_dir / f"{bname}.yaml"
        tyamls = _type_yamls_for_bridge(bd)
        if _bridge_stale(bname, bridge_yaml, tyamls, state):
            _progress(f"Regenerating Python bridge: {bname}")
            bridges_to_generate.append(bd)
        else:
            _progress(f"Python bridge '{bname}' is up-to-date")

    # Emit the type yamls used by each bridge so PythonBridge.cmake can
    # track them for future staleness checks.
    #
    # We emit ALL type yamls loaded for the profile per bridge, including
    # transitively discovered ones (e.g. point_cloud.yaml loaded as a nested
    # field inside semantic_data.yaml).  bd["type_names"] only contains types
    # directly named in inputs/outputs; type_yaml_paths contains every yaml
    # loaded by load_type_yaml() including recursive field dependencies.
    #
    # To correctly attribute transitive dependencies to bridges: a bridge
    # depends on a type yaml if that yaml was needed to fully define any
    # type the bridge directly uses.  We compute this by walking the full
    # dependency closure for each bridge.
    for bd in validated_bridges:
        all_dep_names: set[str] = set()
        for tname in bd["type_names"]:
            _collect_deps(tname, all_dep_names)
        bridge_type_yamls = sorted(
            str(type_yaml_paths[t])
            for t in all_dep_names
            if t in type_yaml_paths
        )
        name_upper = bd["name"].upper().replace(".", "_")
        print(f'set(PY_BRIDGE_TYPE_YAMLS_{name_upper} "{cmake_list(bridge_type_yamls)}")')

    # Generate struct headers (mirroring subdir structure).
    # Only regenerate if at least one bridge is stale - if everything is
    # up-to-date the headers on disk are already correct.
    struct_out_root = build_dir / "include" / "illixr" / "bridge"
    struct_out_root.mkdir(parents=True, exist_ok=True)

    cumulative_dotted: list[str] = []
    generated_hdrs: list[str] = []
    generated_serializers: set[str] = set()
    # The libclang serialization scan is expensive - build it lazily the
    # first time a struct header actually needs to be written.
    illixr_ser_map: dict | None = None

    for td in sorted_types:
        dotted = td["dotted"]
        rel_path = dotted_to_path(dotted)
        out_hdr = struct_out_root / f"{rel_path}.hpp"
        out_hdr.parent.mkdir(parents=True, exist_ok=True)
        if bridges_to_generate:
            if illixr_ser_map is None:
                illixr_ser_map = build_illixr_serialization_map(source_dir)
            _progress(f"  Generating struct header: {dotted}")
            out_hdr.write_text(gen_struct_header(td, cumulative_dotted))
            boost_hdr = struct_out_root / f"{rel_path}_ser.hpp"
            boost_hdr.write_text(gen_boost_hpp(td, out_hdr.name, cumulative_dotted, illixr_ser_map))
            boost_cpp = struct_out_root / f"{rel_path}_ser.cpp"
            boost_cpp.write_text(gen_boost_cpp(td, cumulative_dotted, boost_hdr.name))
        else:
            boost_hdr = struct_out_root / f"{rel_path}_ser.hpp"
            boost_cpp = struct_out_root / f"{rel_path}_ser.cpp"
        generated_hdrs.append(str(out_hdr))
        generated_serializers.add(str(boost_hdr))
        generated_serializers.add(str(boost_cpp))
        cumulative_dotted.append(dotted)

    # Generate per-bridge sources
    all_bridge_names = []
    all_plugin_dirs = []
    all_plugin_cpp_files = []
    all_binding_cpp_files = []

    for bd in bridges_to_generate:
        bridge_name = bd["name"]
        plugin_dir = build_dir / "generated" / "plugins" / bridge_name
        plugin_dir.mkdir(parents=True, exist_ok=True)

        bridge_path = bridges_dir / f"{bridge_name}.yaml"
        # Script path: resolved YAML value used as compile-time default.
        # At runtime ILLIXR_<NAME>_SCRIPT env var overrides this.
        script_yaml_default = str((bridge_path.parent / bd["script"]).resolve())

        _progress(f"  Generating plugin sources: {bridge_name}")
        (plugin_dir / "plugin.hpp").write_text(
            gen_plugin_hpp(bd, gen_dotted_set))
        (plugin_dir / "plugin.cpp").write_text(
            gen_plugin_cpp(bd, gen_dotted_set, script_yaml_default,
                            plugin_dir, type_defs_by_dotted_pre))
        (plugin_dir / "CMakeLists.txt").write_text(
            gen_plugin_cmake(bd, gen_dotted_set, generated_serializers, plugin_dir,
                             build_dir / "include"))

        # Bindings: one file per generated type used by this bridge
        bridge_binding_files = []
        cumul_so_far: list[str] = []
        for td in sorted_types:
            dotted = td["dotted"]
            if dotted not in bd["type_names"]:
                cumul_so_far.append(dotted)
                continue
            bfile = plugin_dir / dotted_to_binding_filename(dotted)
            bfile.write_text(gen_bindings_cpp(td, cumul_so_far))
            bridge_binding_files.append(str(bfile))
            cumul_so_far.append(dotted)

        # Python package tree for bridge types used by this bridge
        used_tds = [td for td in sorted_types if td["dotted"] in bd["type_names"]]
        gen_python_package_tree(used_tds, plugin_dir)

        all_bridge_names.append(bridge_name)
        all_plugin_dirs.append(str(plugin_dir))
        all_plugin_cpp_files.append(str(plugin_dir / "plugin.cpp"))
        all_binding_cpp_files.extend(bridge_binding_files)

    # Save JSON state file with hashes of all processed files.
    new_state = {}
    for bd in validated_bridges:
        bname = bd["name"]
        bridge_yaml = bridges_dir / f"{bname}.yaml"
        tyamls = _type_yamls_for_bridge(bd)
        new_state[bname] = {
            "bridge_hash": _file_md5(bridge_yaml),
            "type_hashes": {str(p): _file_md5(p) for p in tyamls},
        }
    _save_state(build_dir, new_state)
    if bridges_to_generate:
        _progress(f"Python bridge generation complete "                  f"({len(bridges_to_generate)} bridge(s) regenerated)")

    # Emit CMake variables.
    # Always emit ALL bridges (validated_bridges), not just the regenerated
    # subset, so cmake can register every bridge as a build target.
    all_bridge_names_full = [bd["name"] for bd in validated_bridges]
    all_cmake_dirs = [str(build_dir / "generated" / "plugins" / n)
                      for n in all_bridge_names_full]
    all_plugin_dirs_full = [str(build_dir / "generated" / "plugins" / n)
                            for n in all_bridge_names_full]
    all_plugin_cpps_full = [str(build_dir / "generated" / "plugins" / n / "plugin.cpp")
                            for n in all_bridge_names_full]
    print(f'set(PY_BRIDGE_NAMES "{cmake_list(all_bridge_names_full)}")')
    print(f'set(PY_BRIDGE_CMAKE_DIRS "{cmake_list(all_cmake_dirs)}")')
    print(f'set(PY_BRIDGE_PLUGIN_DIRS "{cmake_list(all_plugin_dirs_full)}")')
    print(f'set(PY_BRIDGE_PLUGIN_CPPS "{cmake_list(all_plugin_cpps_full)}")')
    print(f'set(PY_BRIDGE_BINDING_CPPS "{cmake_list(all_binding_cpp_files)}")')
    print(f'set(PY_BRIDGE_STRUCT_HDRS "{cmake_list(generated_hdrs)}")')
    print(f'set(PY_BRIDGE_STRUCT_INCLUDE_DIR "{build_dir / "include"}")')
    print(f'set(PY_BRIDGE_COUNT "{len(all_bridge_names_full)}")')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="ILLIXR Python bridge generator")
    ap.add_argument("--write-profiles", action="store_true",
                    help="Tier 1: write per-profile YAML files")
    ap.add_argument("--generate", action="store_true",
                    help="Tier 2: generate struct headers and plugin sources")
    ap.add_argument("args", nargs="*")
    opts = ap.parse_args()

    if not opts.write_profiles and not opts.generate:
        print('message(FATAL_ERROR "generate_python_bridges.py: '
              'specify --write-profiles and/or --generate")')
        sys.exit(1)

    positional = opts.args

    if opts.write_profiles and opts.generate:
        if len(positional) != 4:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles --generate requires 4 positional args: '
                  'master_profile bridge_profile build_dir source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        bridge_profile = Path(positional[1]).resolve()
        build_dir = Path(positional[2]).resolve()
        source_dir = Path(positional[3]).resolve()
        profiles_dir = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yaml_files(master_profile, profiles_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)
        run_generate(bridge_profile, build_dir, source_dir)

    elif opts.write_profiles:
        if len(positional) != 2:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles requires 2 positional args: '
                  'master_profile source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        source_dir = Path(positional[1]).resolve()
        profiles_dir = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yaml_files(master_profile, profiles_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)

    else:
        if len(positional) != 3:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--generate requires 3 positional args: '
                  'bridge_profile build_dir source_dir")')
            sys.exit(1)
        bridge_profile = Path(positional[0]).resolve()
        build_dir = Path(positional[1]).resolve()
        source_dir = Path(positional[2]).resolve()
        run_generate(bridge_profile, build_dir, source_dir)


if __name__ == "__main__":
    main()
