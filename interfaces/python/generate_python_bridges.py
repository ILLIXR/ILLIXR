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
      <master_profile> <source_dir>

  # Tier 2 only (headers + sources for selected profile)
  python3 generate_python_bridges.py --generate
      <bridge_profile> <build_dir> <source_dir>

  # Both tiers in one call (used when python_bridges.yaml has changed)
  python3 generate_python_bridges.py --write-profiles --generate
      <master_profile> <bridge_profile> <build_dir> <source_dir>

Outputs (Tier 2)
----------------
  <build_dir>/include/illixr/bridge/<name>.hpp - generated struct headers
  <build_dir>/generated/plugins/<bridge>/plugin.hpp
  <build_dir>/generated/plugins/<bridge>/plugin.cpp
  <build_dir>/generated/plugins/<bridge>/bindings_<type>.cpp

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
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print('message(FATAL_ERROR "generate_python_bridges.py requires PyYAML: '
          'pip install pyyaml")')
    sys.exit(1)

import datetime
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

# Known existing ILLIXR types → header path (relative to the ILLIXR include/)
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


def validate_field(fname, fdef, known_struct_names):
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

    if ftype in known_struct_names:
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
            "a mat_* type, or a bridge-defined struct name")

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


def validate_type_yaml(data, path, all_struct_names):
    # Struct name is derived from the yaml filename stem, not from a 'name'
    # field.  This guarantees uniqueness across profiles: two bridges
    # referencing the same yaml always produce the same generated header.
    name = Path(path).stem
    if not re.match(r'^[a-z][a-z0-9_]*$', name):
        raise SchemaError(
            f"{path}: filename stem '{name}' must be lowercase snake_case")

    if "name" in data:
        raise SchemaError(
            f"{path}: type yaml files must not contain a 'name' field; "
            f"the struct name is derived from the filename ('{name}')")

    fields_raw = data.get("fields")
    if not fields_raw or not isinstance(fields_raw, dict):
        raise SchemaError(f"{path}: 'fields' must be a non-empty mapping")

    peers  = set(all_struct_names) - {name}
    fields = {}
    for fname, fdef in fields_raw.items():
        if not re.match(r'^[a-z][a-z0-9_]*$', fname):
            raise SchemaError(
                f"{path}: field name '{fname}' must be lowercase snake_case")
        fields[fname] = validate_field(fname, fdef, peers)

    return {"name": name, "fields": fields}


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


def validate_bridge_yaml(data, path, all_type_names):
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
        raise SchemaError(f"{path}: 'types' must be a list of type names")

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
                "or listed in 'types:'")
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
                "or listed in 'types:'")
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
    name_to_def = {td["name"]: td for td in type_defs}
    visited     = set()
    order       = []

    def visit(name):
        if name in visited or name not in name_to_def:
            return
        visited.add(name)
        for fdef in name_to_def[name]["fields"].values():
            visit(fdef.get("type", ""))
        order.append(name_to_def[name])

    for td in type_defs:
        visit(td["name"])
    return order


# ---------------------------------------------------------------------------
# C++ code generation helpers
# ---------------------------------------------------------------------------

def field_decl(fname, fdef, gen_names):
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if shape and not container:
        t = ftype if ftype in gen_names else cpp_scalar(ftype)
        if len(shape) == 1:
            return f"    {t} {fname}_[{shape[0]}];"
        return f"    {t} {fname}_[{shape[0]}][{shape[1]}];"

    if is_mat(ftype):
        return f"    cv::Mat {fname}_;"

    if ftype in gen_names:
        if container == "vector":
            return f"    std::vector<{ftype}> {fname}_;"
        return f"    {ftype} {fname}_;"

    t = cpp_scalar(canonical_type(ftype))
    if container == "vector":
        return f"    std::vector<{t}> {fname}_;"
    if container == "dict":
        return f"    std::unordered_map<std::string, {t}> {fname}_;"
    return f"    {t} {fname}_;"


def required_includes(td, gen_names):
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

        if ftype in gen_names:
            # cross-include uses the new bridge path
            illixr.add(f'"illixr/bridge/{ftype}.hpp"')
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


def serialize_stmt(fname, fdef, gen_names):
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
        f"                std::size_t sz = "
        f"{fname}_.total() * {fname}_.elemSize();",
        f"                ar_ & boost::serialization::make_array(",
        f"                    reinterpret_cast<const uint8_t*>"
        f"({fname}_.data), sz);",
        "            }",
        "        }",
    ]


def _mat_load_lines(fname):
    return [
        "        {",
        "            int rows, cols, typ;",
        f"            ar_ & rows; ar_ & cols; ar_ & typ;",
        f"            {fname}_.create(rows, cols, typ);",
        f"            std::size_t sz = "
        f"{fname}_.total() * {fname}_.elemSize();",
        f"            ar_ & boost::serialization::make_array(",
        f"                reinterpret_cast<uint8_t*>({fname}_.data), sz);",
        "        }",
    ]


# ---------------------------------------------------------------------------
# Struct header generation
# ---------------------------------------------------------------------------

def gen_struct_header(td, gen_names_so_far):
    name    = td["name"]
    fields  = td["fields"]
    guard   = name.upper()
    gen_set = set(gen_names_so_far)
    has_mat = any(is_mat(f["type"]) for f in fields.values())

    illixr_incs, system_incs = required_includes(td, gen_set)

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the "
             "University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py "
             "-- do not edit directly.")
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
    L.append(f"#ifdef ILLIXR_SERIALIZE_{guard}")
    L.append("#include <boost/serialization/access.hpp>")
    L.append("#include <boost/serialization/array.hpp>")
    L.append("#include <boost/serialization/nvp.hpp>")
    L.append("#endif")
    L.append("")
    L.append("namespace ILLIXR {")
    L.append("")
    L.append(f"struct {name} : switchboard::event {{")

    for fname, fdef in fields.items():
        L.append(field_decl(fname, fdef, gen_set))

    L.append("")
    L.append(f"    {name}() = default;")
    L.append("")
    L.append(f"#ifdef ILLIXR_SERIALIZE_{guard}")

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
    L.append(f"#endif // ILLIXR_SERIALIZE_{guard}")
    L.append("};")
    L.append("")
    L.append("} // namespace ILLIXR")
    L.append("")
    L.append(f"#ifdef ILLIXR_SERIALIZE_{guard}")
    L.append(f"BOOST_CLASS_EXPORT_KEY(ILLIXR::{name})")
    L.append(f"BOOST_CLASS_EXPORT_IMPLEMENT(ILLIXR::{name})")
    L.append(f"#endif // ILLIXR_SERIALIZE_{guard}")
    L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------------------
# pybind11 bindings generation
# ---------------------------------------------------------------------------

def _mat_getter_lines(fname, ftype, struct_name):
    np_dtype = MAT_TYPES[ftype][1][3:]   # strip "np."
    cpp_elem = MAT_TYPES[ftype][2]
    return [
        f'    .def_property("{fname}",',
        f'        [](const {struct_name}& self) -> py::array {{',
        f'            if (self.{fname}_.empty()) return py::array();',
        f'            py::object guard = py::capsule(',
        f'                new std::shared_ptr<{struct_name}>(),',
        f'                [](void* p) {{',
        f'                    delete static_cast<'
        f'std::shared_ptr<{struct_name}>*>(p); }});',
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


def _mat_setter_lines(fname, ftype, struct_name):
    cv_base  = MAT_TYPES[ftype][0]
    cpp_elem = MAT_TYPES[ftype][2]
    return [
        f'        []({struct_name}& self, py::array_t<{cpp_elem}> arr) {{',
        f'            auto buf = arr.request();',
        f'            int r = (int)buf.shape[0], c = (int)buf.shape[1];',
        f'            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;',
        f'            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), '
        f'buf.ptr);',
        f'            tmp.copyTo(self.{fname}_);',
        f'        }})',
    ]


def _fixed_array_getter_lines(fname, fdef, struct_name, gen_names):
    """Zero-copy getter for fixed scalar arrays; list getter for struct arrays."""
    ftype = fdef["type"]
    shape = fdef["shape"]

    if ftype in gen_names:
        n = shape[0]
        return [
            f'    .def_property("{fname}",',
            f'        [](const {struct_name}& self) {{',
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
        f'        [](const {struct_name}& self) -> py::array_t<{cpp_t}> {{',
        f'            return py::array_t<{cpp_t}>(',
        f'                {{{flat}}}, {{sizeof({cpp_t})}},',
        f'                const_cast<{cpp_t}*>({data_p}));',
        f'        }},',
    ]


def _fixed_array_setter_lines(fname, fdef, struct_name, gen_names):
    ftype = fdef["type"]
    shape = fdef["shape"]

    if ftype in gen_names:
        n = shape[0]
        return [
            f'        []({struct_name}& self, py::list lst) {{',
            f'            if ((int)lst.size() != {n})',
            f'                throw std::runtime_error(',
            f'                    "{fname}: expected {n} elements");',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                self.{fname}_[i] = '
            f'lst[i].cast<ILLIXR::{ftype}>();',
            f'        }})',
        ]

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    flat  = shape[0] if len(shape) == 1 else shape[0] * shape[1]
    dest  = (f"self.{fname}_"
             if len(shape) == 1 else f"&self.{fname}_[0][0]")
    return [
        f'        []({struct_name}& self, py::array_t<{cpp_t}> arr) {{',
        f'            if (arr.size() != {flat})',
        f'                throw std::runtime_error(',
        f'                    "{fname}: expected {flat} elements");',
        f'            std::copy(arr.data(), arr.data() + {flat}, {dest});',
        f'        }})',
    ]


def _kw_param(fname, fdef, gen_names):
    """pybind11 py::arg default for one field."""
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype) or (shape and not container):
        return f'py::arg("{fname}") = py::none()'

    if ftype in gen_names:
        if container == "vector":
            return f'py::arg("{fname}") = std::vector<ILLIXR::{ftype}>()'
        return f'py::arg("{fname}") = ILLIXR::{ftype}()'

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f'py::arg("{fname}") = std::vector<{cpp_t}>()'
    if container == "dict":
        return (f'py::arg("{fname}") = '
                f'std::unordered_map<std::string, {cpp_t}>()')
    defaults = {"bool": "false", "std::string": '""',
                "float": "0.0f", "double": "0.0"}
    return f'py::arg("{fname}") = {defaults.get(cpp_t, "0")}'


def _kw_init_body(fname, fdef, gen_names):
    """Constructor body assignment for one field."""
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype):
        cv_base  = MAT_TYPES[ftype][0]
        cpp_elem = MAT_TYPES[ftype][2]
        return (
            f"        if (!{fname}.is_none()) {{\n"
            f"            auto arr = {fname}.cast<py::array>();\n"
            f"            auto buf = arr.request();\n"
            f"            int r = (int)buf.shape[0], "
            f"c = (int)buf.shape[1];\n"
            f"            int ch = (buf.ndim == 3) ? "
            f"(int)buf.shape[2] : 1;\n"
            f"            cv::Mat tmp(r, c, "
            f"CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);\n"
            f"            tmp.copyTo(obj.{fname}_);\n"
            f"        }}"
        )

    if shape and not container:
        if ftype in gen_names:
            n = shape[0]
            return (
                f"        if (!{fname}.is_none()) {{\n"
                f"            auto lst = {fname}.cast<py::list>();\n"
                f"            if ((int)lst.size() != {n})\n"
                f"                throw std::runtime_error(\n"
                f'                    "{fname}: expected {n} elements");\n'
                f"            for (int i = 0; i < {n}; ++i)\n"
                f"                obj.{fname}_[i] = "
                f"lst[i].cast<ILLIXR::{ftype}>();\n"
                f"        }}"
            )
        ctype = canonical_type(ftype)
        cpp_t = cpp_scalar(ctype)
        flat  = shape[0] if len(shape) == 1 else shape[0] * shape[1]
        dest  = (f"obj.{fname}_"
                 if len(shape) == 1 else f"&obj.{fname}_[0][0]")
        return (
            f"        if (!{fname}.is_none()) {{\n"
            f"            auto arr = {fname}.cast<py::array_t<{cpp_t}>>();\n"
            f"            if (arr.size() != {flat})\n"
            f"                throw std::runtime_error(\n"
            f'                    "{fname}: expected {flat} elements");\n'
            f"            std::copy(arr.data(), arr.data() + {flat}, "
            f"{dest});\n"
            f"        }}"
        )

    return f"        obj.{fname}_ = {fname};"


def _lambda_param_type(fname, fdef, gen_names):
    """C++ parameter type for the kw-init lambda."""
    ftype     = fdef["type"]
    container = fdef.get("container")
    shape     = fdef.get("shape")

    if is_mat(ftype) or (shape and not container):
        return "py::object"

    if ftype in gen_names:
        if container == "vector":
            return f"std::vector<ILLIXR::{ftype}>"
        return f"ILLIXR::{ftype}"

    ctype = canonical_type(ftype)
    cpp_t = cpp_scalar(ctype)
    if container == "vector":
        return f"std::vector<{cpp_t}>"
    if container == "dict":
        return f"std::unordered_map<std::string, {cpp_t}>"
    return cpp_t


def gen_bindings_cpp(td, gen_names_so_far):
    name    = td["name"]
    fields  = td["fields"]
    gen_set = set(gen_names_so_far)
    has_mat = any(is_mat(f["type"]) for f in fields.values())
    qname   = f"ILLIXR::{name}"

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the "
             "University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py "
             "-- do not edit directly.")
    L.append("")
    L.append(f'#include "illixr/bridge/{name}.hpp"')
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
    L.append(f"PYBIND11_EMBEDDED_MODULE({name}, m) {{")
    L.append(f'    m.doc() = "ILLIXR bridge type: {name}";')
    L.append("")

    # Build kw-init lambda parameter list
    params = [(fn, _lambda_param_type(fn, fd, gen_set))
              for fn, fd in fields.items()]
    param_str = ",\n".join(
        f"            {ptype} {pname}" for pname, ptype in params)

    L.append(f"    py::class_<{qname}>(m, \"{name}\")")
    L.append(f"        .def(py::init([]({param_str}) {{")
    L.append(f"            {qname} obj;")
    for fname, fdef in fields.items():
        L.append(_kw_init_body(fname, fdef, gen_set))
    L.append("            return obj;")
    L.append("        }),")

    # py::arg defaults — last one has no trailing comma
    args = [_kw_param(fn, fd, gen_set) for fn, fd in fields.items()]
    for i, arg in enumerate(args):
        suffix = "," if i < len(args) - 1 else ""
        L.append(f"        {arg}{suffix}")
    L.append("        )")

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
            L.append(f'        .def_readwrite("{fname}", '
                     f'&{qname}::{fname}_)')

    L.append("        ;")
    L.append("}")
    L.append("")
    return "\n".join(L)


# ---------------------------------------------------------------------------
# Plugin source generation
# ---------------------------------------------------------------------------

def _include_for_type(tname, gen_names):
    if tname in gen_names:
        return f'"illixr/bridge/{tname}.hpp"'
    return f'"illixr/data_format/{tname}.hpp"'


def gen_plugin_hpp(bridge, gen_names):
    pname     = bridge["name"]
    all_types = (
            {inp["type"] for inp in bridge["inputs"]} |
            {out["type"] for out in bridge["outputs"]}
    )
    type_incs = sorted(_include_for_type(t, gen_names) for t in all_types)

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the "
             "University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py "
             "-- do not edit directly.")
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
        L.append(
            f"    switchboard::reader<{inp['type']}> {inp['alias']}_reader_;")
    for out in bridge["outputs"]:
        L.append(
            f"    switchboard::writer<{out['type']}> {out['alias']}_writer_;")
    L.append("")
    L.append("    pybind11::scoped_interpreter guard_;")
    L.append("    std::thread                  py_thread_;")
    L.append("};")
    L.append("")
    L.append("} // namespace ILLIXR")
    L.append("")
    return "\n".join(L)


def gen_plugin_cpp(bridge, gen_names, script_abs):
    pname = bridge["name"]

    L = []
    L.append(f"// Copyright 2020-{YEAR}, The Board of Trustees of the "
             "University of Illinois.")
    L.append("// SPDX-License-Identifier: BSL-1.0")
    L.append("// This file was generated by generate_python_bridges.py "
             "-- do not edit directly.")
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

    # Constructor — initializer list
    L.append(f"{pname}::{pname}(const std::string& name, phonebook* pb)")
    L.append(f"    : plugin{{name, pb}}")
    L.append(f"    , switchboard_{{pb->lookup_impl<switchboard>()}}")
    for inp in bridge["inputs"]:
        L.append(
            f"    , {inp['alias']}_reader_{{switchboard_->"
            f"get_reader<{inp['type']}>("
            f"\"{inp['topic']}\")}} ")
    for out in bridge["outputs"]:
        L.append(
            f"    , {out['alias']}_writer_{{switchboard_->"
            f"get_writer<{out['type']}>("
            f"\"{out['topic']}\")}} ")
    L.append("    , guard_{}")
    L.append("{ }")
    L.append("")

    # Destructor
    L.append(f"{pname}::~{pname}() {{")
    L.append("    if (py_thread_.joinable())")
    L.append("        py_thread_.join();")
    L.append("}")
    L.append("")

    # start()
    L.append(f"void {pname}::start() {{")
    L.append(f"    py_thread_ = std::thread(&{pname}::run_python_thread, "
             "this);")
    L.append("}")
    L.append("")

    # run_python_thread()
    L.append(f"void {pname}::run_python_thread() {{")
    L.append(f'    const std::filesystem::path script_path{{"{script_abs}"}};')
    L.append("    if (!std::filesystem::exists(script_path)) {")
    L.append(f'        spdlog::get("illixr")->error(')
    L.append(f'            "[{pname}] Script not found: {{}}",')
    L.append(f'            script_path.string());')
    L.append("        return;")
    L.append("    }")
    L.append("")
    L.append("    py::dict inputs, outputs;")
    L.append("")

    for inp in bridge["inputs"]:
        alias = inp["alias"]
        itype = inp["type"]
        L.append(f'    inputs["{alias}"] = py::cpp_function(')
        L.append(f'        [this]() -> py::object {{')
        L.append(f'            auto val = {alias}_reader_.get_latest_ro();')
        L.append(f'            if (!val) return py::none();')
        L.append(f'            return py::cast(*val);')
        L.append(f'        }});')
        L.append("")

    for out in bridge["outputs"]:
        alias = out["alias"]
        otype = out["type"]
        L.append(f'    outputs["{alias}"] = py::cpp_function(')
        L.append(f'        [this](const {otype}& val) {{')
        L.append(f'            {alias}_writer_.put('
                 f'{alias}_writer_.allocate(val));')
        L.append(f'        }});')
        L.append("")

    L.append("    try {")
    L.append("        py::module_ sys = py::module_::import(\"sys\");")
    L.append("        sys.attr(\"path\").attr(\"insert\")(")
    L.append("            0, script_path.parent_path().string());")
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
# Tier 1 — profile yaml generation
# ---------------------------------------------------------------------------

def write_profile_yamls(master_path, bridges_dir):
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
                f"{master_path}: profile '{profile_name}' "
                "missing 'bridges' key")
        bridges_str = re.sub(r'\s*,\s*', ',', str(bridges_val).strip())
        out_path    = Path(bridges_dir) / f"{profile_name}.yaml"
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
    bridge_names = [b.strip()
                    for b in str(bridges_str).split(",") if b.strip()]
    if not bridge_names:
        print(f'message(FATAL_ERROR "Bridge profile '
              f'\'{bridge_profile_path}\' has no bridges listed")')
        sys.exit(1)

    # Read and validate bridge descriptors; check for duplicates
    bridge_raws = []
    seen_names  = {}
    for bname in bridge_names:
        bpath = bridges_dir / f"{bname}.yaml"
        if not bpath.exists():
            print(f'message(FATAL_ERROR "Bridge descriptor not found: '
                  f'{bpath}")')
            sys.exit(1)
        try:
            with open(bpath) as f:
                braw = yaml.safe_load(f)
        except Exception as e:
            print(f'message(FATAL_ERROR "Cannot read bridge descriptor '
                  f'\'{bpath}\': {e}")')
            sys.exit(1)
        declared = braw.get("name", "")
        if declared in seen_names:
            print(f'message(FATAL_ERROR "Duplicate plugin name '
                  f'\'{declared}\' in bridge profiles: '
                  f'\'{seen_names[declared]}\' and \'{bpath}\'")')
            sys.exit(1)
        seen_names[declared] = str(bpath)
        bridge_raws.append((bpath, braw))

    # Collect all type names referenced across selected bridges
    all_type_yaml_names = set()
    type_yaml_raw       = {}
    type_yaml_paths     = {}

    for bpath, braw in bridge_raws:
        for tname in braw.get("types", []):
            if tname in all_type_yaml_names:
                continue
            tpath = data_dir / f"{tname}.yaml"
            if not tpath.exists():
                print(f'message(FATAL_ERROR "Type yaml not found: {tpath}")')
                sys.exit(1)
            try:
                with open(tpath) as f:
                    traw = yaml.safe_load(f)
            except Exception as e:
                print(f'message(FATAL_ERROR "Cannot read type yaml '
                      f'\'{tpath}\': {e}")')
                sys.exit(1)
            all_type_yaml_names.add(tname)
            type_yaml_raw[tname]   = traw
            type_yaml_paths[tname] = tpath

    # Validate type yamls
    validated_types = []
    for tname, traw in type_yaml_raw.items():
        try:
            td = validate_type_yaml(traw, type_yaml_paths[tname],
                                    all_type_yaml_names)
            validated_types.append(td)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Type schema error: {e}")')
            sys.exit(1)

    sorted_types = topo_sort(validated_types)
    gen_names    = {td["name"] for td in sorted_types}
    all_types    = gen_names | set(KNOWN_ILLIXR_TYPES.keys())

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

    # Compute serialization defines per bridge
    type_defs_by_name = {td["name"]: td for td in sorted_types}

    def collect_ser_deps(tname, out):
        if tname not in gen_names or tname in out:
            return
        out.add(tname)
        for fdef in type_defs_by_name[tname]["fields"].values():
            collect_ser_deps(fdef.get("type", ""), out)

    bridge_serialize = {}
    for bd in validated_bridges:
        deps = set()
        for out in bd["outputs"]:
            if out["network"] != "none":
                collect_ser_deps(out["type"], deps)
        bridge_serialize[bd["name"]] = deps

    # Output directories — new path convention
    struct_out_dir = build_dir / "include" / "illixr" / "bridge"
    struct_out_dir.mkdir(parents=True, exist_ok=True)

    # Generate struct headers
    cumulative     = []
    generated_hdrs = []
    for td in sorted_types:
        content  = gen_struct_header(td, cumulative)
        out_path = struct_out_dir / f"{td['name']}.hpp"
        out_path.write_text(content)
        generated_hdrs.append(str(out_path))
        cumulative.append(td["name"])

    # Generate per-bridge plugin sources + bindings
    all_bridge_names  = []
    all_plugin_dirs   = []
    all_plugin_cpps   = []
    all_binding_cpps  = []
    all_ser_defs      = []
    all_has_network   = []

    cumul_names = list(cumulative)

    for bd in validated_bridges:
        bname      = bd["name"]
        plugin_dir = build_dir / "generated" / "plugins" / bname
        plugin_dir.mkdir(parents=True, exist_ok=True)

        # Script path relative to the bridge descriptor file
        bpath      = bridges_dir / f"{bname}.yaml"
        script_abs = (bpath.parent / bd["script"]).resolve()

        (plugin_dir / "plugin.hpp").write_text(
            gen_plugin_hpp(bd, gen_names))
        (plugin_dir / "plugin.cpp").write_text(
            gen_plugin_cpp(bd, gen_names, str(script_abs)))

        # One bindings file per generated type used by this bridge
        bridge_binding_files = []
        cumul_so_far         = []
        for td in sorted_types:
            tname = td["name"]
            if tname not in bd["type_names"]:
                cumul_so_far.append(tname)
                continue
            bfile = plugin_dir / f"bindings_{tname}.cpp"
            bfile.write_text(gen_bindings_cpp(td, cumul_so_far))
            bridge_binding_files.append(str(bfile))
            cumul_so_far.append(tname)

        ser_defines = [f"ILLIXR_SERIALIZE_{t.upper()}"
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
    struct_include_root = build_dir / "include"
    print(f'set(PY_BRIDGE_STRUCT_INCLUDE_DIR "{struct_include_root}")')
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
                    help="Tier 1: write per-profile yaml files")
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
        # Both: master_profile bridge_profile build_dir source_dir
        if len(positional) != 4:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles --generate requires 4 positional args: '
                  'master_profile bridge_profile build_dir source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        bridge_profile = Path(positional[1]).resolve()
        build_dir      = Path(positional[2]).resolve()
        source_dir     = Path(positional[3]).resolve()
        bridges_dir    = source_dir / "interfaces" / "python" / "bridges"
        try:
            write_profile_yamls(master_profile, bridges_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)
        run_generate(bridge_profile, build_dir, source_dir)

    elif opts.write_profiles:
        # Tier 1 only: master_profile source_dir
        if len(positional) != 2:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles requires 2 positional args: '
                  'master_profile source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        source_dir     = Path(positional[1]).resolve()
        bridges_dir    = source_dir / "interfaces" / "python" / "bridges"
        try:
            write_profile_yamls(master_profile, bridges_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)

    else:
        # Tier 2 only: bridge_profile build_dir source_dir
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
