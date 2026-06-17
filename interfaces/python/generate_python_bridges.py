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
       PythonBridge.cmake handles the timestamp check for python_bridges.yaml
       and passes --write-profiles when generation is needed.

Tier 2 Struct headers + plugin sources - (always runs when called)
       Scoped to the bridges listed in the selected PYTHON_BRIDGE_PROFILE.

Invocation
----------
  # Tier 1 only (profile YAMLs)
  python3 generate_python_bridges.py --write-profiles
      <master_profile>  <source_dir>

  # Tier 2 only (headers + sources for selected profile)
  python3 generate_python_bridges.py --generate
      <bridge_profile>  <build_dir>  <source_dir>

  # Both tiers in one call (used when python_bridges.yaml has changed)
  python3 generate_python_bridges.py --write-profiles --generate
      <master_profile>  <bridge_profile>  <build_dir>  <source_dir>

Type naming and namespacing
---------------------------
Types are referenced in YAML using dotted notation that mirrors the directory
structure under interfaces/data/:

  camera_intrinsics          → interfaces/data/camera_intrinsics.yaml
                               C++: ILLIXR::bridge::camera_intrinsics
                               Python: illixr.bridge.camera_intrinsics

  geometry.camera_intrinsics → interfaces/data/geometry/camera_intrinsics.yaml
                               C++: ILLIXR::bridge::geometry::camera_intrinsics
                               Python: illixr.bridge.geometry.camera_intrinsics

Each directory level adds one nested C++ namespace inside ILLIXR::bridge and
one Python sub-package level inside illixr.bridge.

Outputs (Tier 2)
----------------
  <build>/include/illixr/bridge[/<ns>]/<name>.hpp   generated struct headers
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
import re
import sys
from pathlib import Path

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
    "int8":    ("int8_t",      "np.int8",    []),
    "int16":   ("int16_t",     "np.int16",   []),
    "int":     ("int32_t",     "np.int32",   []),
    "int32":   ("int32_t",     "np.int32",   []),
    "int64":   ("int64_t",     "np.int64",   []),
    "uint8":   ("uint8_t",     "np.uint8",   []),
    "uint16":  ("uint16_t",    "np.uint16",  []),
    "uint32":  ("uint32_t",    "np.uint32",  []),
    "uint64":  ("uint64_t",    "np.uint64",  []),
    "byte":    ("uint8_t",     "np.uint8",   []),
    "char":    ("uint8_t",     "np.uint8",   []),
    "float":   ("float",       "np.float32", []),
    "float32": ("float",       "np.float32", []),
    "double":  ("double",      "np.float64", []),
    "float64": ("double",      "np.float64", []),
    "bool":    ("bool",        "bool",       []),
    "string":  ("std::string", "str",        ["<boost/serialization/string.hpp>"]),
    "str":     ("std::string", "str",        ["<boost/serialization/string.hpp>"]),
}

SCALAR_ALIASES = {
    "int32":   "int",
    "byte":    "uint8",
    "char":    "uint8",
    "float32": "float",
    "float64": "double",
    "str":     "string",
}

MAT_TYPES = {
    "mat_8u":  ("CV_8UC",  "np.uint8",   "uint8_t"),
    "mat_8s":  ("CV_8SC",  "np.int8",    "int8_t"),
    "mat_16u": ("CV_16UC", "np.uint16",  "uint16_t"),
    "mat_16s": ("CV_16SC", "np.int16",   "int16_t"),
    "mat_32s": ("CV_32SC", "np.int32",   "int32_t"),
    "mat_32f": ("CV_32FC", "np.float32", "float"),
    "mat_64f": ("CV_64FC", "np.float64", "double"),
}

CONTAINER_ALIASES = {
    "list":   "vector",
    "vector": "vector",
    "dict":   "dict",
    "map":    "dict",
}

DICT_FORBIDDEN_VALUE_TYPES = {"bool", "uint8", "uint16", "uint32", "uint64"}
SHAPE_FORBIDDEN_SCALAR     = {"string", "str", "bool"}
VECTOR_FORBIDDEN           = {"bool"}

# Known existing ILLIXR system types → header path (relative to include/)
KNOWN_ILLIXR_TYPES = {
    "semantic_data":    "illixr/data_format/semantic_data.hpp",
    "voice_query":      "illixr/data_format/voice_query.hpp",
    "query_response":   "illixr/data_format/query_response.hpp",
    "compressed_frame": "illixr/data_format/compressed_frame.hpp",
    "dual_frames":      "illixr/data_format/dual_frames.hpp",
    "combined_pose":    "illixr/data_format/combined_pose.hpp",
    "audio_data":       "illixr/data_format/audio_data.hpp",
    "frame_meta":       "illixr/data_format/frame_meta.hpp",
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
    """'geometry.camera_intrinsics' → Path('geometry/camera_intrinsics')"""
    return Path(*dotted.split("."))


def dotted_to_cpp_ns(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' → 'ILLIXR::bridge::geometry::camera_intrinsics'
    'camera_intrinsics'          → 'ILLIXR::bridge::camera_intrinsics'
    """
    parts = dotted.split(".")
    return "::".join(["ILLIXR", "bridge"] + parts)


def dotted_to_open_namespaces(dotted: str) -> list[str]:
    """
    Returns the sequence of 'namespace X {' lines needed to open the
    namespace for this type, outermost first.
    'geometry.camera_intrinsics' → ['namespace ILLIXR {',
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
    """'geometry.camera_intrinsics' → 'camera_intrinsics'"""
    return dotted.split(".")[-1]


def dotted_to_header_path(dotted: str) -> str:
    """
    Include path relative to the build include root.
    'geometry.camera_intrinsics' → 'illixr/bridge/geometry/camera_intrinsics.hpp'
    """
    rel = dotted_to_path(dotted)
    return f"illixr/bridge/{rel}.hpp"


def dotted_to_module_name(dotted: str) -> str:
    """
    pybind11 embedded module identifier (no dots/slashes allowed).
    'geometry.camera_intrinsics' → 'illixr_bridge_geometry_camera_intrinsics'
    """
    flat = dotted.replace(".", "_")
    return f"illixr_bridge_{flat}"


def dotted_to_python_import(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' → 'illixr.bridge.geometry.camera_intrinsics'
    """
    return f"illixr.bridge.{dotted}"


def dotted_to_serialize_define(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' → 'ILLIXR_SERIALIZE_GEOMETRY_CAMERA_INTRINSICS'
    """
    return "ILLIXR_SERIALIZE_" + dotted.upper().replace(".", "_")


def dotted_to_binding_filename(dotted: str) -> str:
    """
    'geometry.camera_intrinsics' → 'bindings_geometry_camera_intrinsics.cpp'
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


def validate_field(fname, fdef, known_dotted_names):
    """
    known_dotted_names: set of dotted type names known in this context
    (e.g. {'camera_intrinsics', 'geometry.point'})
    """
    if not isinstance(fdef, dict):
        raise SchemaError(f"Field '{fname}': definition must be a mapping")
    ftype = fdef.get("type")
    if ftype is None:
        raise SchemaError(f"Field '{fname}': missing required 'type' key")

    container = CONTAINER_ALIASES.get(fdef.get("container", ""), None)
    if "container" in fdef and container is None:
        raise SchemaError(
            f"Field '{fname}': unknown container '{fdef['container']}'. "
            "Use: vector, list, dict, map")

    shape    = fdef.get("shape",    None)
    channels = fdef.get("channels", None)

    if shape is not None and container is not None:
        raise SchemaError(
            f"Field '{fname}': 'shape' and 'container' are mutually exclusive")

    if is_mat(ftype):
        if shape is not None:
            raise SchemaError(f"Field '{fname}': 'shape' is not valid with mat_* types")
        if container is not None:
            raise SchemaError(f"Field '{fname}': 'container' is not valid with mat_* types")
        if channels is None:
            raise SchemaError(f"Field '{fname}': mat_* types require a 'channels' key (1-4)")
        if not isinstance(channels, int) or not (1 <= channels <= 4):
            raise SchemaError(
                f"Field '{fname}': 'channels' must be an integer between 1 and 4")
        return dict(fdef, type=ftype, container=None, shape=None)

    # Bridge-defined struct (dotted name)
    if ftype in known_dotted_names:
        if channels is not None:
            raise SchemaError(f"Field '{fname}': 'channels' is only valid for mat_* types")
        if container is not None and container != "vector":
            raise SchemaError(
                f"Field '{fname}': bridge-defined struct types only support "
                "container: vector")
        if shape is not None:
            if not isinstance(shape, list) or len(shape) != 1:
                raise SchemaError(
                    f"Field '{fname}': bridge-defined struct types only support 1D shape")
            if not isinstance(shape[0], int) or shape[0] < 1:
                raise SchemaError(
                    f"Field '{fname}': shape dimensions must be positive integers")
        return dict(fdef, container=container, shape=shape)

    if not is_scalar(ftype):
        raise SchemaError(
            f"Field '{fname}': unknown type '{ftype}'. Must be a scalar type, "
            "a mat_* type, or a bridge-defined struct dotted name "
            "(e.g. 'camera_intrinsics' or 'geometry.camera_intrinsics')")

    if channels is not None:
        raise SchemaError(f"Field '{fname}': 'channels' is only valid for mat_* types")

    ctype = canonical_type(ftype)

    if shape is not None:
        if ctype in SHAPE_FORBIDDEN_SCALAR:
            raise SchemaError(
                f"Field '{fname}': type '{ftype}' cannot be used with 'shape'")
        if not isinstance(shape, list) or len(shape) not in (1, 2):
            raise SchemaError(
                f"Field '{fname}': 'shape' must be a list of 1 or 2 positive integers")
        for dim in shape:
            if not isinstance(dim, int) or dim < 1:
                raise SchemaError(
                    f"Field '{fname}': shape dimensions must be positive integers")

    if container is not None:
        if container == "vector" and ctype in VECTOR_FORBIDDEN:
            raise SchemaError(
                f"Field '{fname}': vector<bool> is not allowed; "
                "use vector<uint8> instead")
        if container == "dict" and ctype in DICT_FORBIDDEN_VALUE_TYPES:
            raise SchemaError(
                f"Field '{fname}': dict with value type '{ftype}' is not allowed")

    return dict(fdef, type=ctype, container=container, shape=shape)


def validate_type_yaml(data, path, data_dir, all_dotted_names):
    """
    path: absolute path to the YAML file
    data_dir: absolute path to interfaces/data/
    all_dotted_names: set of all known dotted type names in this profile

    Derives the dotted name from the file's path relative to data_dir.
    """
    rel   = Path(path).relative_to(data_dir)
    # rel = geometry/camera_intrinsics.yaml → dotted = geometry.camera_intrinsics
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

    peers  = set(all_dotted_names) - {dotted}
    fields = {}
    for fname, fdef in fields_raw.items():
        if not re.match(r'^[a-z][a-z0-9_]*$', fname):
            raise SchemaError(
                f"{path}: field name '{fname}' must be lowercase snake_case")
        fields[fname] = validate_field(fname, fdef, peers)

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
    """all_type_names: set of all valid dotted bridge names + KNOWN_ILLIXR_TYPES keys"""
    name = data.get("name")
    if not name:
        raise SchemaError(f"{path}: missing required 'name' key")
    if not re.match(r'^[a-z][a-z0-9_]*$', name):
        raise SchemaError(
            f"{path}: 'name' must be lowercase snake_case, got '{name}'")

    script = data.get("script")
    if not script:
        raise SchemaError(f"{path}: missing required 'script' key")

    type_names = data.get("types", [])
    if not isinstance(type_names, list):
        raise SchemaError(f"{path}: 'types' must be a list of dotted type names")

    for tname in type_names:
        validate_dotted_name(tname, f"{path} types")

    inputs  = data.get("inputs",  [])
    outputs = data.get("outputs", [])
    if not isinstance(inputs,  list):
        raise SchemaError(f"{path}: 'inputs' must be a list")
    if not isinstance(outputs, list):
        raise SchemaError(f"{path}: 'outputs' must be a list")

    validated_inputs = []
    for i, inp in enumerate(inputs):
        topic = inp.get("topic")
        itype = inp.get("type")
        alias = inp.get("alias")
        if not topic:
            raise SchemaError(f"{path}: input[{i}] missing 'topic'")
        if not itype:
            raise SchemaError(f"{path}: input[{i}] missing 'type'")
        if not alias:
            raise SchemaError(f"{path}: input[{i}] missing 'alias'")
        if itype not in all_type_names:
            raise SchemaError(
                f"{path}: input[{i}] type '{itype}' is not a known ILLIXR type "
                "or listed in 'types:' — remember to use dotted notation "
                "for types in subdirectories (e.g. 'geometry.camera_intrinsics')")
        validated_inputs.append({"topic": topic, "type": itype, "alias": alias})

    validated_outputs = []
    for i, out in enumerate(outputs):
        topic   = out.get("topic")
        otype   = out.get("type")
        alias   = out.get("alias")
        network = normalize_network(out.get("network", False))
        if not topic:
            raise SchemaError(f"{path}: output[{i}] missing 'topic'")
        if not otype:
            raise SchemaError(f"{path}: output[{i}] missing 'type'")
        if not alias:
            raise SchemaError(f"{path}: output[{i}] missing 'alias'")
        if otype not in all_type_names:
            raise SchemaError(
                f"{path}: output[{i}] type '{otype}' is not a known ILLIXR type "
                "or listed in 'types:' — remember to use dotted notation "
                "for types in subdirectories (e.g. 'geometry.camera_intrinsics')")
        validated_outputs.append(
            {"topic": topic, "type": otype, "alias": alias, "network": network})

    return {
        "name":       name,
        "script":     script,
        "type_names": type_names,
        "inputs":     validated_inputs,
        "outputs":    validated_outputs,
    }


# ---------------------------------------------------------------------------
# Topological sort
# ---------------------------------------------------------------------------

def topo_sort(type_defs):
    """Sort by dotted name key; dependencies before dependents."""
    name_to_def = {td["dotted"]: td for td in type_defs}
    visited     = set()
    order       = []

    def visit(dotted):
        if dotted in visited or dotted not in name_to_def:
            return
        visited.add(dotted)
        for fdef in name_to_def[dotted]["fields"].values():
            visit(fdef.get("type", ""))
        order.append(name_to_def[dotted])

    for td in type_defs:
        visit(td["dotted"])
    return order


# ---------------------------------------------------------------------------
# C++ code generation helpers
# ---------------------------------------------------------------------------

def _cpp_type_ref(ftype, gen_dotted_names):
    """
    Return the C++ type spelling to use in a field declaration.
    For bridge types, emits the fully-qualified ILLIXR::bridge::... name.
    """
    if ftype in gen_dotted_names:
        return dotted_to_cpp_ns(ftype)
    return ftype   # scalar, already a C++ type name


def field_decl(fname, fdef, gen_dotted_names):
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    cpp_t = _cpp_type_ref(ftype, gen_dotted_names)

    if shape and not container:
        if len(shape) == 1:
            return f"    {cpp_t} {fname}_[{shape[0]}];"
        return f"    {cpp_t} {fname}_[{shape[0]}][{shape[1]}];"

    if is_mat(ftype):
        return f"    cv::Mat {fname}_;"

    if ftype in gen_dotted_names:
        if container == "vector":
            return f"    std::vector<{cpp_t}> {fname}_;"
        return f"    {cpp_t} {fname}_;"

    t = cpp_scalar(canonical_type(ftype))
    if container == "vector":
        return f"    std::vector<{t}> {fname}_;"
    if container == "dict":
        return f"    std::unordered_map<std::string, {t}> {fname}_;"
    return f"    {t} {fname}_;"


def required_includes(td, gen_dotted_names):
    illixr = set()
    system = set()
    has_vec = has_map = has_str = has_cstdint = has_mat = False

    for fdef in td["fields"].values():
        ftype     = fdef["type"]
        container = fdef.get("container")

        if is_mat(ftype):
            has_mat = has_cstdint = True
            system.add("<boost/serialization/split_member.hpp>")
            continue

        if ftype in gen_dotted_names:
            # Cross-include: path mirrors the dotted namespace
            illixr.add(f'"illixr/bridge/{dotted_to_path(ftype)}.hpp"')
            if container == "vector":
                has_vec = True
            continue

        ctype = canonical_type(ftype)
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


def serialize_stmt(fname, fdef, gen_dotted_names):
    ftype     = fdef["type"]
    shape     = fdef.get("shape")
    container = fdef.get("container")

    if is_mat(ftype):
        return None

    if shape and not container:
        if len(shape) == 1:
            return (f"        ar_ & boost::serialization::"
                    f"make_array({fname}_, {shape[0]});")
        n = shape[0] * shape[1]
        return (f"        ar_ & boost::serialization::"
                f"make_array(&{fname}_[0][0], {n});")

    return f"        ar_ & {fname}_;"


def _mat_save_lines(fname):
    return [
        "        {",
        f"            int rows = {fname}_.rows, cols = {fname}_.cols, "
        f"typ = {fname}_.type();",
        f"            ar_ & rows; ar_ & cols; ar_ & typ;",
        f"            if ({fname}_.isContinuous()) {{",
        f"                std::size_t sz = {fname}_.total() * {fname}_.elemSize();",
        f"                ar_ & boost::serialization::make_array(",
        f"                    reinterpret_cast<const uint8_t*>({fname}_.data), sz);",
        "            }",
        "        }",
    ]


def _mat_load_lines(fname):
    return [
        "        {",
        "            int rows, cols, typ;",
        f"            ar_ & rows; ar_ & cols; ar_ & typ;",
        f"            {fname}_.create(rows, cols, typ);",
        f"            std::size_t sz = {fname}_.total() * {fname}_.elemSize();",
        f"            ar_ & boost::serialization::make_array(",
        f"                reinterpret_cast<uint8_t*>({fname}_.data), sz);",
        "        }",
    ]


# ---------------------------------------------------------------------------
# Struct header generation
# ---------------------------------------------------------------------------

def gen_struct_header(td, gen_dotted_names_so_far):
    dotted  = td["dotted"]
    stem    = dotted_stem(dotted)
    fields  = td["fields"]
    guard   = dotted_to_serialize_define(dotted)   # e.g. ILLIXR_SERIALIZE_GEOMETRY_CAM...
    gen_set = set(gen_dotted_names_so_far)
    has_mat = any(is_mat(f["type"]) for f in fields.values())
    full_qn = dotted_to_cpp_ns(dotted)

    illixr_incs, system_incs = required_includes(td, gen_set)

    open_ns  = dotted_to_open_namespaces(dotted)
    close_ns = dotted_to_close_namespaces(dotted)

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    L.append(f"// Bridge type: {dotted_to_python_import(dotted)}")
    L.append("")
    L.append("#pragma once")
    L.append("")
    L.append('#include "illixr/switchboard.hpp"')
    if illixr_incs:
        L.append("")
        for h in illixr_incs:
            L.append(f"#include {h}")
    if system_incs:
        L.append("")
        for h in system_incs:
            L.append(f"#include {h}")
    L.append("")
    L.append(f"#ifdef {guard}")
    L.append("#include <boost/serialization/access.hpp>")
    L.append("#include <boost/serialization/array.hpp>")
    L.append("#include <boost/serialization/nvp.hpp>")
    L.append("#endif")
    L.append("")

    # Open nested namespaces
    for ns_line in open_ns:
        L.append(ns_line)
    L.append("")

    L.append(f"struct {stem} : switchboard::event {{")
    for fname, fdef in fields.items():
        L.append(field_decl(fname, fdef, gen_set))
    L.append("")
    L.append(f"    {stem}() = default;")
    L.append("")
    L.append(f"#ifdef {guard}")

    if has_mat:
        L.append("    BOOST_SERIALIZATION_SPLIT_MEMBER()")
        L.append("")
        L.append("    template<typename Archive>")
        L.append("    void save(Archive& ar_, const unsigned int) const {")
        for fname, fdef in fields.items():
            if is_mat(fdef["type"]):
                L += _mat_save_lines(fname)
            else:
                s = serialize_stmt(fname, fdef, gen_set)
                if s:
                    L.append(s)
        L.append("    }")
        L.append("")
        L.append("    template<typename Archive>")
        L.append("    void load(Archive& ar_, const unsigned int) {")
        for fname, fdef in fields.items():
            if is_mat(fdef["type"]):
                L += _mat_load_lines(fname)
            else:
                s = serialize_stmt(fname, fdef, gen_set)
                if s:
                    L.append(s)
        L.append("    }")
    else:
        L.append("    template<typename Archive>")
        L.append("    void serialize(Archive& ar_, const unsigned int) {")
        for fname, fdef in fields.items():
            s = serialize_stmt(fname, fdef, gen_set)
            if s:
                L.append(s)
        L.append("    }")

    L.append("")
    L.append("private:")
    L.append("    friend class boost::serialization::access;")
    L.append(f"#endif // {guard}")
    L.append("};")
    L.append("")

    # Close nested namespaces
    for ns_line in close_ns:
        L.append(ns_line)
    L.append("")

    L.append(f"#ifdef {guard}")
    L.append(f"BOOST_CLASS_EXPORT_KEY({full_qn})")
    L.append(f"BOOST_CLASS_EXPORT_IMPLEMENT({full_qn})")
    L.append(f"#endif // {guard}")
    L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------------------
# pybind11 bindings generation
# ---------------------------------------------------------------------------

def _mat_getter_lines(fname, ftype, qname):
    np_dtype = MAT_TYPES[ftype][1][3:]   # strip "np."
    return [
        f'    .def_property("{fname}",',
        f'        [](const {qname}& self) -> py::array {{',
        f'            if (self.{fname}_.empty()) return py::array();',
        f'            py::object guard = py::capsule(',
        f'                new std::shared_ptr<{qname}>(),',
        f'                [](void* p) {{',
        f'                    delete static_cast<std::shared_ptr<{qname}>*>(p); }});',
        f'            std::vector<ssize_t> shp, str;',
        f'            if (self.{fname}_.channels() == 1) {{',
        f'                shp = {{self.{fname}_.rows, self.{fname}_.cols}};',
        f'                str = {{(ssize_t)self.{fname}_.step[0],',
        f'                        (ssize_t)self.{fname}_.elemSize()}};',
        f'            }} else {{',
        f'                shp = {{self.{fname}_.rows, self.{fname}_.cols,',
        f'                        self.{fname}_.channels()}};',
        f'                str = {{(ssize_t)self.{fname}_.step[0],',
        f'                        (ssize_t)self.{fname}_.step[1],',
        f'                        (ssize_t)self.{fname}_.elemSize1()}};',
        f'            }}',
        f'            return py::array(py::dtype("{np_dtype}"),',
        f'                shp, str, self.{fname}_.data, guard);',
        f'        }},',
    ]


def _mat_setter_lines(fname, ftype, qname):
    cv_base  = MAT_TYPES[ftype][0]
    cpp_elem = MAT_TYPES[ftype][2]
    return [
        f'        []({qname}& self, py::array_t<{cpp_elem}> arr) {{',
        f'            auto buf = arr.request();',
        f'            int r = (int)buf.shape[0], c = (int)buf.shape[1];',
        f'            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;',
        f'            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);',
        f'            tmp.copyTo(self.{fname}_);',
        f'        }})',
    ]


def _fixed_array_getter_lines(fname, fdef, qname, gen_dotted_names):
    ftype = fdef["type"]
    shape = fdef["shape"]

    if ftype in gen_dotted_names:
        n = shape[0]
        return [
            f'    .def_property("{fname}",',
            f'        [](const {qname}& self) {{',
            f'            py::list result;',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                result.append(self.{fname}_[i]);',
            f'            return result;',
            f'        }},',
        ]

    ctype  = canonical_type(ftype)
    cpp_t  = cpp_scalar(ctype)
    flat   = shape[0] if len(shape) == 1 else shape[0] * shape[1]
    data_p = f"self.{fname}_" if len(shape) == 1 else f"&self.{fname}_[0][0]"
    return [
        f'    .def_property("{fname}",',
        f'        [](const {qname}& self) -> py::array_t<{cpp_t}> {{',
        f'            return py::array_t<{cpp_t}>(',
        f'                {{{flat}}}, {{sizeof({cpp_t})}},',
        f'                const_cast<{cpp_t}*>({data_p}));',
        f'        }},',
    ]


def _fixed_array_setter_lines(fname, fdef, qname, gen_dotted_names):
    ftype = fdef["type"]
    shape = fdef["shape"]

    if ftype in gen_dotted_names:
        n    = shape[0]
        fqn  = dotted_to_cpp_ns(ftype)
        return [
            f'        []({qname}& self, py::list lst) {{',
            f'            if ((int)lst.size() != {n})',
            f'                throw std::runtime_error(',
            f'                    "{fname}: expected {n} elements");',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                self.{fname}_[i] = lst[i].cast<{fqn}>();',
            f'        }})',
        ]

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    flat  = shape[0] if len(shape) == 1 else shape[0] * shape[1]
    dest  = (f"self.{fname}_" if len(shape) == 1 else f"&self.{fname}_[0][0]")
    return [
        f'        []({qname}& self, py::array_t<{cpp_t}> arr) {{',
        f'            if (arr.size() != {flat})',
        f'                throw std::runtime_error(',
        f'                    "{fname}: expected {flat} elements");',
        f'            std::copy(arr.data(), arr.data() + {flat}, {dest});',
        f'        }})',
    ]


def _kw_param(fname, fdef, gen_dotted_names):
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype) or (shape and not container):
        return f'py::arg("{fname}") = py::none()'

    if ftype in gen_dotted_names:
        fqn = dotted_to_cpp_ns(ftype)
        if container == "vector":
            return f'py::arg("{fname}") = std::vector<{fqn}>()'
        return f'py::arg("{fname}") = {fqn}()'

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f'py::arg("{fname}") = std::vector<{cpp_t}>()'
    if container == "dict":
        return f'py::arg("{fname}") = std::unordered_map<std::string, {cpp_t}>()'
    defaults = {"bool": "false", "std::string": '""',
                "float": "0.0f", "double": "0.0"}
    return f'py::arg("{fname}") = {defaults.get(cpp_t, "0")}'


def _kw_init_body(fname, fdef, gen_dotted_names):
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype):
        cv_base = MAT_TYPES[ftype][0]
        return (
            f"        if (!{fname}.is_none()) {{\n"
            f"            auto arr = {fname}.cast<py::array>();\n"
            f"            auto buf = arr.request();\n"
            f"            int r = (int)buf.shape[0], c = (int)buf.shape[1];\n"
            f"            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;\n"
            f"            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);\n"
            f"            tmp.copyTo(obj.{fname}_);\n"
            f"        }}"
        )

    if shape and not container:
        if ftype in gen_dotted_names:
            fqn = dotted_to_cpp_ns(ftype)
            n   = shape[0]
            return (
                f"        if (!{fname}.is_none()) {{\n"
                f"            auto lst = {fname}.cast<py::list>();\n"
                f"            if ((int)lst.size() != {n})\n"
                f"                throw std::runtime_error(\n"
                f'                    "{fname}: expected {n} elements");\n'
                f"            for (int i = 0; i < {n}; ++i)\n"
                f"                obj.{fname}_[i] = lst[i].cast<{fqn}>();\n"
                f"        }}"
            )
        ctype = canonical_type(ftype)
        cpp_t = cpp_scalar(ctype)
        flat  = shape[0] if len(shape) == 1 else shape[0] * shape[1]
        dest  = (f"obj.{fname}_" if len(shape) == 1 else f"&obj.{fname}_[0][0]")
        return (
            f"        if (!{fname}.is_none()) {{\n"
            f"            auto arr = {fname}.cast<py::array_t<{cpp_t}>>();\n"
            f"            if (arr.size() != {flat})\n"
            f"                throw std::runtime_error(\n"
            f'                    "{fname}: expected {flat} elements");\n'
            f"            std::copy(arr.data(), arr.data() + {flat}, {dest});\n"
            f"        }}"
        )

    return f"        obj.{fname}_ = {fname};"


def _lambda_param_type(fname, fdef, gen_dotted_names):
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype) or (shape and not container):
        return "py::object"

    if ftype in gen_dotted_names:
        fqn = dotted_to_cpp_ns(ftype)
        if container == "vector":
            return f"std::vector<{fqn}>"
        return fqn

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f"std::vector<{cpp_t}>"
    if container == "dict":
        return f"std::unordered_map<std::string, {cpp_t}>"
    return cpp_t


def gen_bindings_cpp(td, gen_dotted_names_so_far):
    dotted  = td["dotted"]
    stem    = dotted_stem(dotted)
    fields  = td["fields"]
    gen_set = set(gen_dotted_names_so_far)
    has_mat = any(is_mat(f["type"]) for f in fields.values())
    qname   = dotted_to_cpp_ns(dotted)
    module  = dotted_to_module_name(dotted)
    hdr     = dotted_to_header_path(dotted)

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    L.append(f"// Python module: {dotted_to_python_import(dotted)}")
    L.append("")
    L.append(f'#include "{hdr}"')
    L.append("")
    L.append("#include <pybind11/embed.h>")
    L.append("#include <pybind11/numpy.h>")
    L.append("#include <pybind11/stl.h>")
    if has_mat:
        L.append("#include <opencv2/core.hpp>")
    L.append("")
    L.append("#include <memory>")
    L.append("#include <stdexcept>")
    L.append("#include <vector>")
    L.append("")
    L.append("namespace py = pybind11;")
    L.append("")
    L.append(f"PYBIND11_EMBEDDED_MODULE({module}, m) {{")
    L.append(f'    m.doc() = "ILLIXR bridge type: {dotted_to_python_import(dotted)}";')
    L.append("")

    # kw-init lambda parameters
    params    = [(fn, _lambda_param_type(fn, fd, gen_set)) for fn, fd in fields.items()]
    param_str = ",\n".join(f"            {ptype} {pname}" for pname, ptype in params)

    L.append(f"    py::class_<{qname}>(m, \"{stem}\")")
    if param_str:
        L.append(f"        .def(py::init([]({param_str}) {{")
        L.append(f"            {qname} obj;")
        for fname, fdef in fields.items():
            L.append(_kw_init_body(fname, fdef, gen_set))
        L.append("            return obj;")
        L.append("        }),")
        args = [_kw_param(fn, fd, gen_set) for fn, fd in fields.items()]
        for i, arg in enumerate(args):
            suffix = "," if i < len(args) - 1 else ""
            L.append(f"        {arg}{suffix}")
        L.append("        )")
    else:
        L.append(f"        .def(py::init<>())")

    # Properties and readwrite
    for fname, fdef in fields.items():
        ftype     = fdef["type"]
        container = fdef.get("container")
        shape     = fdef.get("shape")

        if is_mat(ftype):
            L += _mat_getter_lines(fname, ftype, qname)
            L += _mat_setter_lines(fname, ftype, qname)
        elif shape and not container:
            L += _fixed_array_getter_lines(fname, fdef, qname, gen_set)
            L += _fixed_array_setter_lines(fname, fdef, qname, gen_set)
        else:
            L.append(f'        .def_readwrite("{fname}", &{qname}::{fname}_)')

    L.append("        ;")
    L.append("}")
    L.append("")
    return "\n".join(L)


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
        parts  = dotted.split(".")
        # All ancestor package dirs including the leaf's parent
        for depth in range(len(parts)):
            rel = Path("illixr", "bridge", *parts[:depth])
            pkg_dirs.add(plugin_dir / rel)

    # Write __init__.py for each package dir
    # The top-level illixr/bridge/__init__.py imports all types in topo order
    # Sub-package __init__.py files import their own types in topo order

    # Group types by their immediate parent package
    # key: tuple of namespace parts above the stem
    # e.g. () for root, ('geometry',) for geometry.*
    ns_to_types: dict[tuple, list] = {}
    for td in sorted_types:
        parts = td["dotted"].split(".")
        ns    = tuple(parts[:-1])
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
            stem   = dotted_stem(td["dotted"])
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


# ---------------------------------------------------------------------------
# Plugin source generation
# ---------------------------------------------------------------------------

def _include_for_type(tname, gen_dotted_names):
    if tname in gen_dotted_names:
        return f'"illixr/bridge/{dotted_to_path(tname)}.hpp"'
    return f'"illixr/data_format/{tname}.hpp"'


def _cpp_type_for_switchboard(tname, gen_dotted_names):
    """Return the C++ type name to use in switchboard::reader/writer<T>."""
    if tname in gen_dotted_names:
        return dotted_to_cpp_ns(tname)
    return tname   # ILLIXR system type, already in ILLIXR namespace


def gen_plugin_hpp(bridge, gen_dotted_names):
    pname     = bridge["name"]
    all_types = (
            {inp["type"] for inp in bridge["inputs"]} |
            {out["type"] for out in bridge["outputs"]}
    )
    type_incs = sorted(_include_for_type(t, gen_dotted_names) for t in all_types)

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    L.append("")
    L.append("#pragma once")
    L.append("")
    L.append('#include "illixr/plugin.hpp"')
    L.append('#include "illixr/switchboard.hpp"')
    for inc in type_incs:
        L.append(f"#include {inc}")
    L.append("")
    L.append("#include <pybind11/embed.h>")
    L.append("#include <thread>")
    L.append("")
    L.append("namespace py = pybind11;")
    L.append("")
    L.append("namespace ILLIXR {")
    L.append("")
    L.append(f"class {pname} : public plugin {{")
    L.append("public:")
    L.append(f"    explicit {pname}(const std::string& name, phonebook* pb);")
    L.append(f"    ~{pname}() override;")
    L.append("")
    L.append("    void start() override;")
    L.append("")
    L.append("private:")
    L.append("    void run_python_thread();")
    L.append("")
    L.append("    const std::shared_ptr<switchboard> switchboard_;")
    for inp in bridge["inputs"]:
        cpp_t = _cpp_type_for_switchboard(inp["type"], gen_dotted_names)
        L.append(f"    switchboard::reader<{cpp_t}> {inp['alias']}_reader_;")
    for out in bridge["outputs"]:
        cpp_t = _cpp_type_for_switchboard(out["type"], gen_dotted_names)
        L.append(f"    switchboard::writer<{cpp_t}> {out['alias']}_writer_;")
    L.append("")
    L.append("    pybind11::scoped_interpreter guard_;")
    L.append("    std::thread                  py_thread_;")
    L.append("};")
    L.append("")
    L.append("} // namespace ILLIXR")
    L.append("")
    return "\n".join(L)


def gen_plugin_cpp(bridge, gen_dotted_names, script_abs, plugin_dir: Path):
    pname = bridge["name"]

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py -- do not edit directly.")
    L.append("")
    L.append('#include "plugin.hpp"')
    L.append("")
    L.append("#include <pybind11/embed.h>")
    L.append("#include <pybind11/numpy.h>")
    L.append("#include <pybind11/stl.h>")
    L.append("#include <spdlog/spdlog.h>")
    L.append("")
    L.append("#include <filesystem>")
    L.append("#include <stdexcept>")
    L.append("#include <string>")
    L.append("#include <thread>")
    L.append("")
    L.append("namespace py = pybind11;")
    L.append("")
    L.append("namespace ILLIXR {")
    L.append("")

    L.append(f"{pname}::{pname}(const std::string& name, phonebook* pb)")
    L.append(f"    : plugin{{name, pb}}")
    L.append(f"    , switchboard_{{pb->lookup_impl<switchboard>()}}")
    for inp in bridge["inputs"]:
        cpp_t = _cpp_type_for_switchboard(inp["type"], gen_dotted_names)
        L.append(f"    , {inp['alias']}_reader_{{switchboard_->"
                 f"get_reader<{cpp_t}>(\"{inp['topic']}\")}} ")
    for out in bridge["outputs"]:
        cpp_t = _cpp_type_for_switchboard(out["type"], gen_dotted_names)
        L.append(f"    , {out['alias']}_writer_{{switchboard_->"
                 f"get_writer<{cpp_t}>(\"{out['topic']}\")}} ")
    L.append("    , guard_{}")
    L.append("{ }")
    L.append("")

    L.append(f"{pname}::~{pname}() {{")
    L.append("    if (py_thread_.joinable())")
    L.append("        py_thread_.join();")
    L.append("}")
    L.append("")

    L.append(f"void {pname}::start() {{")
    L.append(f"    py_thread_ = std::thread(&{pname}::run_python_thread, this);")
    L.append("}")
    L.append("")

    L.append(f"void {pname}::run_python_thread() {{")
    L.append(f'    const std::filesystem::path script_path{{"{script_abs}"}};')
    L.append("    if (!std::filesystem::exists(script_path)) {")
    L.append(f'        spdlog::get("illixr")->error(')
    L.append(f'            "[{pname}] Script not found: {{}}", script_path.string());')
    L.append("        return;")
    L.append("    }")
    L.append("")
    # Add the plugin dir to sys.path so illixr.bridge.* package resolves
    L.append(f'    const std::string plugin_pkg_dir = "{plugin_dir}";')
    L.append("    py::dict inputs, outputs;")
    L.append("")

    for inp in bridge["inputs"]:
        alias = inp["alias"]
        L.append(f'    inputs["{alias}"] = py::cpp_function(')
        L.append(f'        [this]() -> py::object {{')
        L.append(f'            auto val = {alias}_reader_.get_latest_ro();')
        L.append(f'            if (!val) return py::none();')
        L.append(f'            return py::cast(*val);')
        L.append(f'        }});')
        L.append("")

    for out in bridge["outputs"]:
        alias = out["alias"]
        cpp_t = _cpp_type_for_switchboard(out["type"], gen_dotted_names)
        L.append(f'    outputs["{alias}"] = py::cpp_function(')
        L.append(f'        [this](const {cpp_t}& val) {{')
        L.append(f'            {alias}_writer_.put({alias}_writer_.allocate(val));')
        L.append(f'        }});')
        L.append("")

    L.append("    try {")
    L.append("        py::module_ sys = py::module_::import(\"sys\");")
    L.append("        // Add plugin package dir first so illixr.bridge.* resolves.")
    L.append("        sys.attr(\"path\").attr(\"insert\")(0, plugin_pkg_dir);")
    L.append("        // Add script dir so the user script can do relative imports.")
    L.append("        sys.attr(\"path\").attr(\"insert\")(0, script_path.parent_path().string());")
    L.append("        py::module_ script = py::module_::import(")
    L.append("            script_path.stem().string().c_str());")
    L.append("        script.attr(\"run\")(inputs, outputs);")
    L.append("    } catch (const py::error_already_set& e) {")
    L.append(f'        spdlog::get("illixr")->error(')
    L.append(f'            "[{pname}] Python error: {{}}", e.what());')
    L.append("    }")
    L.append("}")
    L.append("")
    L.append("} // namespace ILLIXR")
    L.append("")
    L.append(f"ILLIXR_PLUGIN_MAIN(ILLIXR::{pname})")
    L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------------------
# Tier 1 — profile YAML generation
# ---------------------------------------------------------------------------

def write_profile_yamls(master_path, profiles_dir):
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
        out_path    = Path(profiles_dir) / f"{profile_name}.yaml"
        out_path.write_text(
            "# This file was auto-generated from python_bridges.yaml"
            " -- do not edit directly.\n"
            f"bridges: {bridges_str}\n"
        )


# ---------------------------------------------------------------------------
# Tier 2 — struct headers + plugin sources
# ---------------------------------------------------------------------------

def cmake_list(items):
    return ";".join(str(i) for i in items)


def run_generate(bridge_profile_path, build_dir, source_dir):
    bridges_dir = source_dir / "interfaces" / "python" / "bridges"
    data_dir    = source_dir / "interfaces" / "data"

    # Read bridge profile
    try:
        with open(bridge_profile_path) as f:
            bp_raw = yaml.safe_load(f)
    except Exception as e:
        print(f'message(FATAL_ERROR "Cannot read bridge profile '
              f'\'{bridge_profile_path}\': {e}")')
        sys.exit(1)

    bridges_str  = bp_raw.get("bridges", "")
    bridge_names = [b.strip() for b in str(bridges_str).split(",") if b.strip()]
    if not bridge_names:
        print(f'message(FATAL_ERROR "Bridge profile \'{bridge_profile_path}\' '
              f'has no bridges listed")')
        sys.exit(1)

    # Read and validate bridge descriptors; check for duplicates
    bridge_raws = []
    seen_names  = {}
    for bname in bridge_names:
        # Bridge names are plain snake_case plugin names -- never dotted.
        # A dot here almost always means the user put a type dotted name
        # (e.g. semantic_xr.semantic_data) in the profile bridges: list
        # instead of in the bridge descriptor types: list.
        if "." in bname:
            errmsg = (
                f"Bridge name '{bname}' in profile '{bridge_profile_path}' "
                "contains a dot, which is not allowed. "
                "The bridges: list must contain bridge plugin names "
                "(e.g. semantic_xr), not dotted type names "
                "(e.g. semantic_xr.semantic_data). "
                "Dotted type names belong in the bridge descriptor "
                "types: list, not in the profile."
            )
            print(f'message(FATAL_ERROR "{errmsg}")')
            sys.exit(1)
        if not re.match(r'^[a-z][a-z0-9_]*$', bname):
            errmsg = f"Bridge name '{bname}' must be lowercase snake_case (no dots)."
            print(f'message(FATAL_ERROR "{errmsg}")')
            sys.exit(1)
        bpath = bridges_dir / f"{bname}.yaml"
        if not bpath.exists():
            errmsg = (
                f"Bridge descriptor not found: {bpath}. "
                "The bridges: list must contain plugin names matching "
                "files in interfaces/python/bridges/. "
                "Dotted type names (e.g. semantic_xr.semantic_data) "
                "belong in the bridge descriptor types: list."
            )
            print(f'message(FATAL_ERROR "{errmsg}")')
            sys.exit(1)
        try:
            with open(bpath) as f:
                braw = yaml.safe_load(f)
        except Exception as e:
            errmsg = f"Cannot read bridge descriptor '{bpath}': {e}"
            print(f'message(FATAL_ERROR "{errmsg}")')
            sys.exit(1)
        declared = braw.get("name", "")
        if declared in seen_names:
            errmsg = (
                f"Duplicate plugin name '{declared}' in bridge profiles: "
                f"'{seen_names[declared]}' and '{bpath}'"
            )
            print(f'message(FATAL_ERROR "{errmsg}")')
            sys.exit(1)
        seen_names[declared] = str(bpath)
        bridge_raws.append((bpath, braw))

    # Collect all type names referenced; dotted names map to YAML files
    all_dotted_yaml: set[str] = set()
    type_yaml_raw:   dict[str, dict]  = {}
    type_yaml_paths: dict[str, Path]  = {}

    for bpath, braw in bridge_raws:
        for tname in braw.get("types", []):
            if tname in all_dotted_yaml:
                continue
            # Dotted name → file path under data_dir
            rel   = dotted_to_path(tname)
            tpath = data_dir / f"{rel}.yaml"
            if not tpath.exists():
                print(f'message(FATAL_ERROR "Type YAML not found: {tpath}\\n'
                      f'  Dotted name \'{tname}\' resolves to {tpath}")')
                sys.exit(1)
            try:
                with open(tpath) as f:
                    traw = yaml.safe_load(f)
            except Exception as e:
                print(f'message(FATAL_ERROR "Cannot read type YAML '
                      f'\'{tpath}\': {e}")')
                sys.exit(1)
            all_dotted_yaml.add(tname)
            type_yaml_raw[tname]   = traw
            type_yaml_paths[tname] = tpath

    # Validate type YAMLs
    validated_types = []
    for tname, traw in type_yaml_raw.items():
        try:
            td = validate_type_yaml(traw, type_yaml_paths[tname],
                                    data_dir, all_dotted_yaml)
            validated_types.append(td)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Type schema error: {e}")')
            sys.exit(1)

    sorted_types    = topo_sort(validated_types)
    gen_dotted_set  = {td["dotted"] for td in sorted_types}
    all_types       = gen_dotted_set | set(KNOWN_ILLIXR_TYPES.keys())

    # Validate bridge descriptors
    validated_bridges = []
    for bpath, braw in bridge_raws:
        try:
            bd = validate_bridge_yaml(braw, bpath, all_types)
            validated_bridges.append(bd)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Bridge schema error in '
                  f'\'{bpath}\': {e}")')
            sys.exit(1)

    # Serialization defines per bridge
    type_defs_by_dotted = {td["dotted"]: td for td in sorted_types}

    def collect_ser_deps(tname, out):
        if tname not in gen_dotted_set or tname in out:
            return
        out.add(tname)
        for fdef in type_defs_by_dotted[tname]["fields"].values():
            collect_ser_deps(fdef.get("type", ""), out)

    bridge_serialize = {}
    for bd in validated_bridges:
        deps = set()
        for out in bd["outputs"]:
            if out["network"] != "none":
                collect_ser_deps(out["type"], deps)
        bridge_serialize[bd["name"]] = deps

    # Generate struct headers (mirroring subdir structure)
    struct_out_root = build_dir / "include" / "illixr" / "bridge"
    struct_out_root.mkdir(parents=True, exist_ok=True)

    cumulative_dotted: list[str] = []
    generated_hdrs: list[str]   = []

    for td in sorted_types:
        dotted   = td["dotted"]
        rel_path = dotted_to_path(dotted)
        out_path = struct_out_root / f"{rel_path}.hpp"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(gen_struct_header(td, cumulative_dotted))
        generated_hdrs.append(str(out_path))
        cumulative_dotted.append(dotted)

    # Generate per-bridge sources
    all_bridge_names  = []
    all_plugin_dirs   = []
    all_plugin_cpps   = []
    all_binding_cpps  = []
    all_ser_defs      = []
    all_has_network   = []

    for bd in validated_bridges:
        bname      = bd["name"]
        plugin_dir = build_dir / "generated" / "plugins" / bname
        plugin_dir.mkdir(parents=True, exist_ok=True)

        bpath      = bridges_dir / f"{bname}.yaml"
        script_abs = (bpath.parent / bd["script"]).resolve()

        (plugin_dir / "plugin.hpp").write_text(
            gen_plugin_hpp(bd, gen_dotted_set))
        (plugin_dir / "plugin.cpp").write_text(
            gen_plugin_cpp(bd, gen_dotted_set, str(script_abs), plugin_dir))

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

        ser_defines = [dotted_to_serialize_define(t)
                       for t in sorted(bridge_serialize[bname])]
        has_net     = bool(bridge_serialize[bname])

        all_bridge_names.append(bname)
        all_plugin_dirs.append(str(plugin_dir))
        all_plugin_cpps.append(str(plugin_dir / "plugin.cpp"))
        all_binding_cpps.extend(bridge_binding_files)
        all_ser_defs.append(
            f"{bname}:{cmake_list(ser_defines)}" if ser_defines
            else f"{bname}:")
        all_has_network.append("TRUE" if has_net else "FALSE")

    # Emit CMake variables
    print(f'set(PY_BRIDGE_NAMES "{cmake_list(all_bridge_names)}")')
    print(f'set(PY_BRIDGE_PLUGIN_DIRS "{cmake_list(all_plugin_dirs)}")')
    print(f'set(PY_BRIDGE_PLUGIN_CPPS "{cmake_list(all_plugin_cpps)}")')
    print(f'set(PY_BRIDGE_BINDING_CPPS "{cmake_list(all_binding_cpps)}")')
    print(f'set(PY_BRIDGE_STRUCT_HDRS "{cmake_list(generated_hdrs)}")')
    print(f'set(PY_BRIDGE_STRUCT_INCLUDE_DIR "{build_dir / "include"}")')
    print(f'set(PY_BRIDGE_SERIALIZE_DEFS "{cmake_list(all_ser_defs)}")')
    print(f'set(PY_BRIDGE_HAS_NETWORK "{cmake_list(all_has_network)}")')
    print(f'set(PY_BRIDGE_COUNT "{len(all_bridge_names)}")')


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
        build_dir      = Path(positional[2]).resolve()
        source_dir     = Path(positional[3]).resolve()
        profiles_dir   = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yamls(master_profile, profiles_dir)
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
        source_dir     = Path(positional[1]).resolve()
        profiles_dir   = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yamls(master_profile, profiles_dir)
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
        build_dir      = Path(positional[1]).resolve()
        source_dir     = Path(positional[2]).resolve()
        run_generate(bridge_profile, build_dir, source_dir)


if __name__ == "__main__":
    main()
