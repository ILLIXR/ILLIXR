#!/usr/bin/env python3
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""
generate_system_bindings.py

Parses all headers in include/illixr/data_format/ (excluding the
serialization/ subdirectory) using libclang and generates pybind11
EMBEDDED_MODULE binding files for each.

Outputs (all checked into git under interfaces/python/system_bindings/)
-----------------------------------------------------------------------
  illixr/<stem>.py                  Python shim: from illixr_<stem> import *
  illixr/__init__.py                Package init; imports all modules in
                                    dependency order
  bindings_<stem>.cpp               pybind11 PYBIND11_EMBEDDED_MODULE
  system_types.json                 Machine-readable summary of all structs,
                                    fields, skipped structs, and import order

Usage
-----
  python3 interfaces/python/generate_system_bindings.py \\
      [--build-dir  /path/to/build]         \\  # reads CMakeCache.txt
      [--include-dir /extra/include] ...

Paths derived from the script's own location (interfaces/python/):
  Source root : two directories above this script
  Data headers: <source_root>/include/illixr/data_format/
  Output dir  : <source_root>/interfaces/python/system_bindings/

The script reads CMakeCache.txt from --build-dir to discover include paths
set by cmake (e.g., Eigen, OpenCV, Boost).  --include-dir arguments are
added on top.

All #ifdef blocks evaluate as false (no predefined macros beyond the
compiler's built-ins), which eliminates conditionally-compiled types.

If any field in a struct has an unresolvable type, the entire struct is
skipped and a warning is emitted to stderr.
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional

try:
    import clang.cindex as clang
except ImportError:
    print("ERROR: libclang Python bindings not found.  "
          "Install with: pip install libclang", file=sys.stderr)
    sys.exit(1)

import datetime
YEAR = str(datetime.date.today().year)

# ---------------------------------------------------------------------------
# CMakeCache.txt parsing
# ---------------------------------------------------------------------------

# CMake cache variables that contain include-path-like values
_CMAKE_INCLUDE_VARS = [
    "CMAKE_SOURCE_DIR",
    "CMAKE_BINARY_DIR",
    "Eigen3_INCLUDE_DIR",
    "EIGEN3_INCLUDE_DIR",
    "OpenCV_INCLUDE_DIRS",
    "Boost_INCLUDE_DIR",
    "Boost_INCLUDE_DIRS",
    "SPDLOG_INCLUDE_DIR",
    "pybind11_INCLUDE_DIR",
]


def read_cmake_cache(build_dir: Path) -> list[str]:
    """
    Parse CMakeCache.txt and return a list of include directories inferred
    from well-known cache variables.
    """
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.exists():
        return []

    include_dirs: list[str] = []
    values: dict[str, str] = {}

    with open(cache_file) as f:
        for line in f:
            line = line.strip()
            if line.startswith("#") or line.startswith("//") or "=" not in line:
                continue
            # FORMAT: VAR_NAME:TYPE=VALUE
            try:
                key_type, value = line.split("=", 1)
                key = key_type.split(":")[0].strip()
                values[key] = value.strip()
            except ValueError:
                continue

    source_dir = values.get("CMAKE_SOURCE_DIR", "")
    binary_dir = values.get("CMAKE_BINARY_DIR", "")

    # Always add source include/ and build include/
    if source_dir:
        p = Path(source_dir) / "include"
        if p.exists():
            include_dirs.append(str(p))
    if binary_dir:
        p = Path(binary_dir) / "include"
        if p.exists():
            include_dirs.append(str(p))

    # Add any recognized include variables
    for var in _CMAKE_INCLUDE_VARS:
        if var in values and values[var]:
            # Values may be semicolon-separated lists
            for entry in values[var].split(";"):
                entry = entry.strip()
                if entry and Path(entry).exists():
                    include_dirs.append(entry)

    # Deduplicate while preserving order
    seen: set[str] = set()
    result: list[str] = []
    for d in include_dirs:
        if d not in seen:
            seen.add(d)
            result.append(d)
    return result


# ---------------------------------------------------------------------------
# C++ type -> pybind11 type mapping
# ---------------------------------------------------------------------------

# Maps canonical C++ spelling -> (pybind11 def_readwrite works natively,
# numpy hint for documentation)
_SCALAR_CPP_TYPES = {
    "bool",
    "char", "signed char", "unsigned char",
    "short", "unsigned short",
    "int", "unsigned int",
    "long", "unsigned long",
    "long long", "unsigned long long",
    "float", "double", "long double",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "ptrdiff_t",
    "std::string",
}

# Container templates we know how to bind via pybind11/stl.h
_KNOWN_CONTAINERS = {
    "std::vector",
    "std::array",
    "std::map",
    "std::unordered_map",
    "std::set",
    "std::unordered_set",
    "std::pair",
    "std::optional",
}

# cv::Mat is handled specially
_OPENCV_TYPES = {"cv::Mat", "cv::Mat_"}


def _is_bindable_type(spelling: str, known_struct_names: set[str]) -> bool:
    """
    Return True if a C++ type spelling is one pybind11 can handle natively
    or that we know is a registered struct.
    """
    s = spelling.strip()

    # Remove const/volatile/reference/pointer qualifiers
    s = re.sub(r'\bconst\b', '', s)
    s = re.sub(r'\bvolatile\b', '', s)
    s = s.replace("&", "").replace("*", "").strip()

    if s in _SCALAR_CPP_TYPES:
        return True

    # ILLIXR:: prefix stripped
    bare = s.replace("ILLIXR::", "").strip()
    if bare in known_struct_names:
        return True

    # cv::Mat
    if any(s.startswith(t) for t in _OPENCV_TYPES):
        return True

    # Known container prefix
    if any(s.startswith(t) for t in _KNOWN_CONTAINERS):
        return True

    # Enum types (libclang reports them as their typedef spelling)
    # We allow them through; pybind11 handles plain enums via def_readwrite
    return False


# ---------------------------------------------------------------------------
# libclang AST walking
# ---------------------------------------------------------------------------

class FieldInfo:
    def __init__(self, name: str, cpp_type: str):
        self.name     = name
        self.cpp_type = cpp_type


class StructInfo:
    def __init__(self, name: str, qualified_name: str,
                 bases: list[str], fields: list[FieldInfo],
                 source_file: str):
        self.name           = name           # unqualified
        self.qualified_name = qualified_name # ILLIXR::foo
        self.bases          = bases          # list of qualified base names
        self.fields         = fields
        self.source_file    = source_file


def _qualified_name(cursor) -> str:
    parts = []
    c = cursor
    while c and c.kind != clang.CursorKind.TRANSLATION_UNIT:
        if c.spelling:
            parts.append(c.spelling)
        c = c.semantic_parent
    return "::".join(reversed(parts))


def _base_qualified_name(cursor) -> str:
    """Return the qualified name of a base class cursor."""
    # Walk into the base specifier to find the referenced type
    for child in cursor.get_children():
        if child.kind == clang.CursorKind.TYPE_REF:
            return child.referenced.type.spelling.replace("struct ", "").strip()
    return cursor.type.spelling.replace("struct ", "").strip()


def parse_header(header_path: Path,
                 include_dirs: list[str],
                 target_file: str) -> tuple[list[StructInfo], list[str]]:
    """
    Parse a single header with libclang.  Returns (structs, warnings).
    Only structs whose definition location matches target_file are returned.
    Structs with any unresolvable field are skipped (warning emitted).
    """
    index = clang.Index.create()

    args = ["-std=c++17", "-x", "c++"]
    for d in include_dirs:
        args += ["-I", d]
    # No predefined macros -> all #ifdef blocks evaluate as false
    args += ["-undef"]

    tu = index.parse(
        str(header_path),
        args=args,
        options=(
                clang.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES |
                clang.TranslationUnit.PARSE_INCOMPLETE
        ),
    )

    warnings: list[str] = []
    structs:  list[StructInfo] = []

    # Collect all struct names defined in this file first (for field validation)
    local_struct_names: set[str] = set()

    def collect_local_names(cursor):
        if cursor.location.file and \
                Path(cursor.location.file.name).resolve() == \
                Path(target_file).resolve():
            if cursor.kind in (clang.CursorKind.STRUCT_DECL,
                               clang.CursorKind.CLASS_DECL):
                if cursor.is_definition() and cursor.spelling:
                    local_struct_names.add(cursor.spelling)
        for child in cursor.get_children():
            collect_local_names(child)

    collect_local_names(tu.cursor)

    def walk(cursor):
        if cursor.location.file and \
                Path(cursor.location.file.name).resolve() == \
                Path(target_file).resolve():
            if cursor.kind in (clang.CursorKind.STRUCT_DECL,
                               clang.CursorKind.CLASS_DECL):
                if not cursor.is_definition() or not cursor.spelling:
                    return
                _process_struct(cursor)

        for child in cursor.get_children():
            walk(child)

    # Build a set of all known struct names (local and bases pulled via includes)
    all_known: set[str] = set(local_struct_names)

    def _process_struct(cursor):
        struct_name = cursor.spelling
        qual_name   = _qualified_name(cursor)

        bases:  list[str]      = []
        fields: list[FieldInfo] = []
        skip_reason: Optional[str] = None

        for child in cursor.get_children():

            # Base classes
            if child.kind == clang.CursorKind.CXX_BASE_SPECIFIER:
                base_name = _base_qualified_name(child)
                if base_name not in ("switchboard::event",):
                    bases.append(base_name)
                continue

            # Fields
            if child.kind == clang.CursorKind.FIELD_DECL:
                if child.access_specifier == clang.AccessSpecifier.PRIVATE:
                    continue
                field_type = child.type.spelling
                # Strip elaborated type keywords
                field_type = re.sub(r'\bstruct\b\s*', '', field_type)
                field_type = re.sub(r'\bclass\b\s*',  '', field_type)
                field_type = field_type.strip()

                if not _is_bindable_type(field_type, all_known):
                    skip_reason = (
                        f"struct '{struct_name}' skipped: "
                        f"unresolvable field type '{field_type}' "
                        f"for field '{child.spelling}'"
                    )
                    break

                fields.append(FieldInfo(child.spelling, field_type))

        if skip_reason:
            warnings.append(skip_reason)
            return

        structs.append(StructInfo(
            name           = struct_name,
            qualified_name = qual_name,
            bases          = bases,
            fields         = fields,
            source_file    = str(header_path),
        ))

    walk(tu.cursor)
    return structs, warnings


# ---------------------------------------------------------------------------
# Dependency / topological sort
# ---------------------------------------------------------------------------

def topo_sort_structs(all_structs: dict[str, StructInfo]) -> list[str]:
    """
    Return struct names in topological order (bases before derived).
    all_structs maps qualified_name -> StructInfo.
    """
    visited: set[str] = set()
    order:   list[str] = []

    def visit(name: str):
        if name in visited:
            return
        visited.add(name)
        info = all_structs.get(name)
        if info:
            for base in info.bases:
                visit(base)
        order.append(name)

    for struct in all_structs:
        visit(struct)
    return order


def topo_sort_modules(
        module_structs: dict[str, list[StructInfo]]
) -> list[str]:
    """
    Return module stems in dependency order.
    A module depends on another if any of its structs has a base defined in
    the other module.
    """
    # Build qualified_name -> stem map
    qname_to_stem: dict[str, str] = {}
    for base, structs in module_structs.items():
        for s in structs:
            qname_to_stem[s.qualified_name] = base

    visited: set[str] = set()
    order:   list[str] = []

    def visit(stem: str):
        if stem in visited:
            return
        visited.add(stem)
        for m_struct in module_structs.get(stem, []):
            for base_ in m_struct.bases:
                dep_stem = qname_to_stem.get(base_)
                if dep_stem and dep_stem != stem:
                    visit(dep_stem)
        order.append(stem)

    for struct in module_structs:
        visit(struct)
    return order


# ---------------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------------

def gen_binding_cpp(stem: str,
                    structs: list[StructInfo],
                    sorted_struct_names: list[str],
                    all_struct_map: dict[str, StructInfo],
                    has_serialization: bool,
                    ser_header: Optional[str]) -> str:
    """Generate the bindings_<stem>.cpp content."""

    # Collect all headers needed
    local_headers: set[str] = set()
    for struct in structs:
        local_headers.add(
            f'"illixr/data_format/{Path(struct.source_file).name}"')

    needs_eigen = any(
        any("Eigen" in f.cpp_type for f in s.fields)
        for s in structs)
    needs_opencv = any(
        any("cv::" in f.cpp_type for f in s.fields)
        for s in structs)

    # Sort structs in this module in dependency order
    qname_order = {name: i for i, name in enumerate(sorted_struct_names)}
    sorted_local = sorted(
        structs,
        key=lambda s: qname_order.get(s.qualified_name, 999))

    cpp: list[str] = []
    cpp.append(
        f"// Copyright 2020-{YEAR}, The Board of Trustees of the "
        "University of Illinois.")
    cpp.append("// SPDX-License-Identifier: BSL-1.0")
    cpp.append(
        "// This file was generated by generate_system_bindings.py "
        "-- do not edit directly.")
    cpp.append(f"// Module: illixr.{stem}")
    cpp.append("")

    # Dependency order comment
    deps = []
    for s in sorted_local:
        for base in s.bases:
            bi = all_struct_map.get(base)
            if bi:
                dep_stem = Path(bi.source_file).stem
                if dep_stem != stem and dep_stem not in deps:
                    deps.append(dep_stem)
    if deps:
        cpp.append(
            f"// Python import dependencies (auto-imported via illixr "
            f"package): {', '.join('illixr.' + d for d in deps)}")
        cpp.append("")

    for h in sorted(local_headers):
        cpp.append(f"#include {h}")
    if has_serialization and ser_header:
        cpp.append(f'#include "illixr/data_format/serialization/{ser_header}.hpp"')
    cpp.append("")
    cpp.append("#include <pybind11/embed.h>")
    cpp.append("#include <pybind11/stl.h>")
    if needs_eigen:
        cpp.append("#include <pybind11/eigen.h>")
    if needs_opencv:
        cpp.append("#include <opencv2/core.hpp>")
        cpp.append("#include <pybind11/numpy.h>")
    cpp.append("")
    cpp.append("#include <stdexcept>")
    cpp.append("")
    cpp.append("namespace py = pybind11;")
    cpp.append("")
    # Module name uses illixr_ prefix to distinguish from the Python package name
    cpp.append(f"PYBIND11_EMBEDDED_MODULE(illixr_{stem}, m) {{")
    cpp.append(f'    m.doc() = "ILLIXR system types: {stem}";')
    cpp.append("")

    for si in sorted_local:
        qn = si.qualified_name

        # Base class registration
        if si.bases:
            # Only register bases that are in our all_struct_map
            registered_bases = [
                b for b in si.bases if b in all_struct_map]
            if registered_bases:
                bases_str = ", ".join(registered_bases)
                class_decl = (
                    f"    py::class_<{qn}, {bases_str}>"
                    f"(m, \"{si.name}\")")
            else:
                class_decl = f"    py::class_<{qn}>(m, \"{si.name}\")"
        else:
            class_decl = f"    py::class_<{qn}>(m, \"{si.name}\")"

        cpp.append(class_decl)
        cpp.append(f"        .def(py::init<>())")

        # Keyword-argument constructor
        if si.fields:
            cpp.append(
                f"        .def(py::init([]({_lambda_params(si)}) {{")
            cpp.append(f"            {qn} obj;")
            for f in si.fields:
                cpp.append(f"            obj.{f.name} = {f.name};")
            cpp.append("            return obj;")
            cpp.append("        }),")
            for idx, f in enumerate(si.fields):
                suffix = "," if idx < len(si.fields) - 1 else ""
                cpp.append(
                    f"        py::arg(\"{f.name}\") = "
                    f"{_default_for(f.cpp_type)}{suffix}")
            cpp.append("        )")

        # def_readwrite for each field
        for f in si.fields:
            cpp.append(
                f"        .def_readwrite(\"{f.name}\", "
                f"&{qn}::{f.name})")

        cpp.append("        ;")
        cpp.append("")

    cpp.append("}")
    cpp.append("")
    return "\n".join(cpp)


def _lambda_params(si: StructInfo) -> str:
    return ", ".join(f"{f.cpp_type} {f.name}" for f in si.fields)


def _default_for(cpp_type: str) -> str:
    t = cpp_type.strip()
    if t in ("bool",):
        return "false"
    if t == "std::string":
        return '""'
    if t in ("float",):
        return "0.0f"
    if t in ("double",):
        return "0.0"
    if "std::vector" in t or "std::map" in t or "std::unordered_map" in t:
        return f"{t}()"
    if "std::array" in t:
        return f"{t}{{}}"
    # Struct type or unknown -> default construct
    return f"{t}()"


def gen_python_shim(stem: str) -> str:
    return (
        f"# Copyright 2020-{YEAR}, The Board of Trustees of the "
        "University of Illinois.\n"
        "# SPDX-License-Identifier: BSL-1.0\n"
        "# Auto-generated by generate_system_bindings.py "
        "-- do not edit directly.\n"
        f"from illixr_{stem} import *  # noqa: F401,F403\n"
    )


def gen_init_py(module_order: list[str]) -> str:
    py_out: list[str] = []
    py_out.append(
        f"# Copyright 2020-{YEAR}, The Board of Trustees of the "
        "University of Illinois.")
    py_out.append("# SPDX-License-Identifier: BSL-1.0")
    py_out.append(
        "# Auto-generated by generate_system_bindings.py "
        "-- do not edit directly.")
    py_out.append(
        "# Modules are imported in dependency order so that base classes")
    py_out.append("# are always registered before derived classes.")
    py_out.append("")
    py_out.append("def _load() -> None:")
    for stem in module_order:
        py_out.append(f"    from . import {stem}  # noqa: F401")
    py_out.append("")
    py_out.append("")
    py_out.append("_load()")
    py_out.append("")
    return "\n".join(py_out)


# ---------------------------------------------------------------------------
# PythonBridge.cmake snippet generation
# ---------------------------------------------------------------------------

def gen_cmake_snippet(output_dir: Path,
                      source_dir: Path,
                      serialization_map: dict[str, str]) -> str:
    """
    Generate the cmake fragment that PythonBridge.cmake includes to add
    system binding sources and serialization cpp files to each bridge target.
    This file is included by PythonBridge.cmake via include().
    """
    rel_output = output_dir.relative_to(source_dir)
    cmake: list[str] = []
    cmake.append(
        f"# Copyright 2020-{YEAR}, The Board of Trustees of the "
        "University of Illinois.")
    cmake.append("# SPDX-License-Identifier: BSL-1.0")
    cmake.append(
        "# Auto-generated by generate_system_bindings.py "
        "-- do not edit directly.")
    cmake.append(
        "# Included by PythonBridge.cmake to add system binding sources.")
    cmake.append("")
    cmake.append("# Glob all compiled system binding sources")
    cmake.append(
        f"file(GLOB _ILLIXR_SYSTEM_BINDING_SOURCES"
        f"    \"${{CMAKE_SOURCE_DIR}}/{rel_output}/bindings_*.cpp\")")
    cmake.append("")
    cmake.append("# Serialization cpp files for types that have them")
    cmake.append("set(_ILLIXR_SERIALIZATION_SOURCES")
    for stem, ser_stem in sorted(serialization_map.items()):
        cmake.append(
            f"    \"${{CMAKE_SOURCE_DIR}}/utils/serialization/{ser_stem}.cpp\"")
    cmake.append(")")
    cmake.append("")
    cmake.append(
        "# These are appended to each bridge plugin target in "
        "PythonBridge.cmake")
    return "\n".join(cmake)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Derive fixed paths from the script's own location.
    # Script lives at <source_root>/interfaces/python/generate_system_bindings.py
    # so parents[2] is the source root.
    _script_dir = Path(__file__).resolve().parent
    source_dir  = _script_dir.parents[1]   # <source_root>
    output_dir  = _script_dir / "system_bindings"

    ap = argparse.ArgumentParser(
        description="Generate pybind11 bindings for ILLIXR system types")
    ap.add_argument("--build-dir",  default=None,
                    help="CMake build directory (reads CMakeCache.txt for "
                         "include paths).  Optional.")
    ap.add_argument("--include-dir", action="append", default=[],
                    dest="extra_include_dirs",
                    help="Additional include directories (repeatable)")
    opts = ap.parse_args()

    data_format_dir   = source_dir / "include" / "illixr" / "data_format"
    serialization_dir = data_format_dir / "serialization"
    utils_ser_dir     = source_dir / "utils" / "serialization"

    print(f"Source root : {source_dir}", file=sys.stderr)
    print(f"Output dir  : {output_dir}", file=sys.stderr)

    if not data_format_dir.exists():
        print(f"ERROR: data_format directory not found: {data_format_dir}",
              file=sys.stderr)
        sys.exit(1)

    # -----------------------------------------------------------------------
    # 1. Collect include paths
    # -----------------------------------------------------------------------
    include_dirs: list[str] = []

    if opts.build_dir:
        cmake_includes = read_cmake_cache(Path(opts.build_dir).resolve())
        include_dirs.extend(cmake_includes)

    # Always add source include/
    src_include = str(source_dir / "include")
    if src_include not in include_dirs:
        include_dirs.insert(0, src_include)

    for d in opts.extra_include_dirs:
        if d not in include_dirs:
            include_dirs.append(d)

    print(f"Include paths ({len(include_dirs)}):", file=sys.stderr)
    for d in include_dirs:
        print(f"  {d}", file=sys.stderr)

    # -----------------------------------------------------------------------
    # 2. Discover serialization correspondence
    #    serialization/<stem>.hpp -> utils/serialization/<stem>.cpp exists
    #    Skip serialization/openxr.hpp
    # -----------------------------------------------------------------------
    serialization_stems: set[str] = set()
    if serialization_dir.exists():
        for ser_hdr in sorted(serialization_dir.glob("*.hpp")):
            stem = ser_hdr.stem
            if stem == "openxr":
                continue
            cpp_path = utils_ser_dir / f"{stem}.cpp"
            if cpp_path.exists():
                serialization_stems.add(stem)

    print(f"Serialization stems: {sorted(serialization_stems)}",
          file=sys.stderr)

    # -----------------------------------------------------------------------
    # 3. Parse each data_format header (skip serialization/ subdirectory)
    # -----------------------------------------------------------------------
    headers = sorted(
        h for h in data_format_dir.glob("*.hpp")
        if h.parent == data_format_dir   # exclude subdirectories
    )

    print(f"Found {len(headers)} data_format headers", file=sys.stderr)

    # module stem -> list of StructInfo
    module_structs: dict[str, list[StructInfo]] = {}
    all_warnings:   list[str] = []

    for header in headers:
        stem = header.stem
        structs, warnings = parse_header(header, include_dirs, str(header))
        all_warnings.extend(warnings)
        if structs:
            module_structs[stem] = structs
            print(f"  {header.name}: {len(structs)} struct(s) — "
                  f"{[s.name for s in structs]}", file=sys.stderr)
        else:
            print(f"  {header.name}: no bindable structs", file=sys.stderr)

    if all_warnings:
        print(f"\n{len(all_warnings)} warning(s):", file=sys.stderr)
        for w in all_warnings:
            print(f"  WARNING: {w}", file=sys.stderr)

    # -----------------------------------------------------------------------
    # 4. Build global struct map and topological sort
    # -----------------------------------------------------------------------
    all_struct_map: dict[str, StructInfo] = {}   # qualified_name -> StructInfo
    for structs in module_structs.values():
        for s in structs:
            all_struct_map[s.qualified_name] = s

    # Sort structs globally (bases before derived)
    sorted_struct_names = topo_sort_structs(all_struct_map)

    # Sort modules (dependency order for __init__.py)
    module_order = topo_sort_modules(module_structs)

    # -----------------------------------------------------------------------
    # 5. Build serialization map:
    #    module stem -> serialization stem (if a corresponding ser file exists)
    # -----------------------------------------------------------------------
    # Maps data-format header stem -> serialization stem
    # (they may differ — e.g., misc.hpp serializes time_point only)
    ser_for_module: dict[str, str] = {}
    for stem in module_structs:
        if stem in serialization_stems:
            ser_for_module[stem] = stem

    # -----------------------------------------------------------------------
    # 6. Generate output files
    # -----------------------------------------------------------------------
    output_dir.mkdir(parents=True, exist_ok=True)
    illixr_pkg_dir = output_dir / "illixr"
    illixr_pkg_dir.mkdir(exist_ok=True)

    generated_cpp:  list[str] = []
    generated_shim: list[str] = []

    for stem in module_order:
        structs = module_structs[stem]

        # bindings_<stem>.cpp
        cpp_content = gen_binding_cpp(
            stem            = stem,
            structs         = structs,
            sorted_struct_names = sorted_struct_names,
            all_struct_map  = all_struct_map,
            has_serialization = stem in ser_for_module,
            ser_header      = ser_for_module.get(stem),
        )
        cpp_path = output_dir / f"bindings_{stem}.cpp"
        cpp_path.write_text(cpp_content)
        generated_cpp.append(str(cpp_path))

        # illixr/<stem>.py shim
        shim_path = illixr_pkg_dir / f"{stem}.py"
        shim_path.write_text(gen_python_shim(stem))
        generated_shim.append(str(shim_path))

    # illixr/__init__.py
    init_path = illixr_pkg_dir / "__init__.py"
    init_path.write_text(gen_init_py(module_order))

    # cmake snippet
    cmake_path = output_dir / "SystemBindings.cmake"
    cmake_path.write_text(
        gen_cmake_snippet(output_dir, source_dir, ser_for_module))

    # -----------------------------------------------------------------------
    # 7. Write system_types.json summary
    # -----------------------------------------------------------------------
    summary = {
        "generated_by": "generate_system_bindings.py",
        "module_import_order": module_order,
        "serialization_map": ser_for_module,
        "warnings": all_warnings,
        "modules": {},
    }

    for stem in module_order:
        structs = module_structs[stem]
        summary["modules"][stem] = {
            "source_header":     f"illixr/data_format/{stem}.hpp",
            "binding_file":      f"bindings_{stem}.cpp",
            "python_shim":       f"illixr/{stem}.py",
            "has_serialization": stem in ser_for_module,
            "structs": [
                {
                    "name":           s.name,
                    "qualified_name": s.qualified_name,
                    "bases":          s.bases,
                    "fields": [
                        {"name": f.name, "cpp_type": f.cpp_type}
                        for f in s.fields
                    ],
                }
                for s in sorted(
                    structs,
                    key=lambda x: sorted_struct_names.index(x.qualified_name)
                    if x.qualified_name in sorted_struct_names else 999)
            ],
            "skipped_structs": [
                w for w in all_warnings
                if f"header '{stem}.hpp'" in w or
                   any(s.name in w for s in structs)
            ],
        }

    json_path = output_dir / "system_types.json"
    json_path.write_text(json.dumps(summary, indent=2))

    # -----------------------------------------------------------------------
    # 8. Report
    # -----------------------------------------------------------------------
    total_structs = sum(len(v) for v in module_structs.values())
    print(f"\nGenerated:", file=sys.stderr)
    print(f"  {len(generated_cpp)} binding cpp files", file=sys.stderr)
    print(f"  {len(generated_shim)} Python shim files", file=sys.stderr)
    print(f"  illixr/__init__.py", file=sys.stderr)
    print(f"  SystemBindings.cmake", file=sys.stderr)
    print(f"  system_types.json", file=sys.stderr)
    print(f"  {total_structs} structs across "
          f"{len(module_structs)} modules", file=sys.stderr)
    if all_warnings:
        print(f"  {len(all_warnings)} struct(s) skipped (see warnings above)",
              file=sys.stderr)


if __name__ == "__main__":
    main()
