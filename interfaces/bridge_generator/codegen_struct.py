# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""C++ struct header and Boost serialization code generation."""
import re
import sys
from pathlib import Path

from . import types as bg_types
from . import helpers as bg_helpers

try:
    import clang.cindex as _clang
except ImportError:
    print("generate_system_types.py requires libclang: pip install clang",
          file=sys.stderr)
    sys.exit(1)


def _cpp_type_ref(field_type, gen_dotted_names):
    """Return the C++ type spelling to use in a struct field declaration.

    For bridge-defined types, returns the fully qualified ``ILLIXR::bridge::...``
    name.  For all other types, returns the type unchanged.

    Args:
        field_type (str): YAML type name or dotted bridge type name.
        gen_dotted_names (iterable[str]): All dotted names of bridge-defined types
                                          generated in this profile.

    Returns:
        str: C++ type spelling suitable for use in a struct field declaration.
    """
    if field_type in gen_dotted_names:
        return bg_helpers.dotted_to_cpp_ns(field_type)
    return field_type  # scalar, already a C++ type name


def field_decl(field_name, field_def, gen_dotted_names):
    """Generate a single C++ struct field declaration line.

    Handles all field types: scalars, fixed arrays (1-D, 2-D, 3-D),
    ``std::vector<T>``, ``std::unordered_map<string, T>``, ``cv::Mat``,
    nested bridge structs, and ILLIXR system types.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition from ``validate_field``.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: A C++ declaration line, e.g. ``"    float position_[3];"`` or
             ``"    std::vector<uint8_t> image_;"`` (including leading spaces).
    """
    # pylint: disable=too-many-return-statements
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

    if bg_types.is_mat(field_type):
        return f"    cv::Mat {field_name}_;"

    if field_type in gen_dotted_names:
        if container == "vector":
            return f"    std::vector<{cpp_t}> {field_name}_;"
        return f"    {cpp_t} {field_name}_;"

    t = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
    if container == "vector":
        return f"    std::vector<{t}> {field_name}_;"
    if container == "dict":
        return f"    std::unordered_map<std::string, {t}> {field_name}_;"
    return f"    {t} {field_name}_;"


def required_includes(td, gen_dotted_names):
    """Return the ``#include`` directives required by a struct type definition.

    Splits includes into two groups: ILLIXR project headers (angle-bracket paths
    starting with ``illixr/``) and C++ system headers (e.g. ``<vector>``).

    Args:
        td (dict): Type definition dict with ``"fields"`` key.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names
                                          (used to resolve nested struct headers).

    Returns:
        tuple[list[str], list[str]]: A pair ``(illixr_includes, system_includes)``
                                     where each element is a list of ``#include``
                                     argument strings (with angle brackets or quotes).
    """
    # pylint: disable=too-many-branches
    illixr = set()
    system = set()
    has_vec = has_map = has_str = has_cstdint = has_mat = False

    for field_def in td["fields"].values():
        field_type = field_def["type"]
        container = field_def.get("container")

        if bg_types.is_mat(field_type):
            has_mat = has_cstdint = True
            system.add("<boost/serialization/split_member.hpp>")
            continue

        if field_type in gen_dotted_names:
            # Cross-include: the path mirrors the dotted namespace
            illixr.add(f'"illixr/bridge/{bg_helpers.dotted_to_path(field_type)}.hpp"')
            if container == "vector":
                has_vec = True
            continue

        ctype = bg_types.canonical_type(field_type)
        for h in bg_types.SCALAR_TYPES[ctype][2]:
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
    """Return the Boost serialization statement for a single struct field.

    Generates the ``ar & data.field_`` (or ``make_array``) statement used
    inside a ``boost::serialization::serialize`` / ``save`` / ``load`` function.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition from ``validate_field``.

    Returns:
        str | None: A single C++ statement string, or ``None`` for ``cv::Mat``
                    fields (which require split ``save``/``load`` functions).
    """
    field_type = field_def["type"]
    shape = field_def.get("shape")
    container = field_def.get("container")

    if bg_types.is_mat(field_type):
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
    """Generate Boost serialization ``save`` lines for a ``cv::Mat`` field.

    Serializes rows, cols, type, and raw pixel bytes for a single Mat field.

    Args:
        field_name (str): Field name (without a trailing underscore).

    Returns:
        list[str]: C++ source lines for the save path of the ``cv::Mat``.
    """
    return [
        "    {",
        f"        int rows = data.{field_name}_.rows, cols = data.{field_name}_.cols, "
        f"typ = data.{field_name}_.type();",
        "        ar & rows; ar & cols; ar & typ;",
        f"        if (data.{field_name}_.isContinuous()) {{",
        f"            std::size_t sz = data.{field_name}_.total() * data.{field_name}_.elemSize();",
        "            ar & boost::serialization::make_array(",
        f"                reinterpret_cast<const uint8_t*>(data.{field_name}_.data), sz);",
        "        }",
        "    }",
        ]


def _mat_load_lines(field_name):
    """Generate Boost serialization ``load`` lines for a ``cv::Mat`` field.

    Reconstructs a Mat by deserializing rows, cols, type, and raw pixel bytes.

    Args:
        field_name (str): Field name (without a trailing underscore).

    Returns:
        list[str]: C++ source lines for the load path of the ``cv::Mat``.
    """
    return [
        "    {",
        "        int rows, cols, typ;",
        "        ar & rows; ar & cols; ar & typ;",
        f"        data.{field_name}_.create(rows, cols, typ);",
        f"        std::size_t sz = data.{field_name}_.total() * data.{field_name}_.elemSize();",
        "        ar & boost::serialization::make_array(",
        f"            reinterpret_cast<uint8_t*>(data.{field_name}_.data), sz);",
        "    }",
        ]


# ---------------------------------------------------------------------------
# Struct header generation
# ---------------------------------------------------------------------------

def gen_struct_header(td, gen_dotted_names_so_far):
    """Generate the complete C++ struct header (``.hpp``) for a bridge type.

    Produces a ``#pragma once`` header defining the struct inside the correct
    ``ILLIXR::bridge::...`` namespace hierarchy, with all required ``#include``
    directives.  The struct inherits from ``switchboard::event``.

    Args:
        td (dict): Normalized type definition dict with ``"dotted"`` and ``"fields"`` keys.
        gen_dotted_names_so_far (iterable[str]): Dotted names of all bridge types
                                                  generated before this one (used to
                                                  resolve nested struct includes).

    Returns:
        str: Complete C++ header file content as a single string.
    """
    dotted = td["dotted"]
    stem = bg_helpers.dotted_stem(dotted)
    fields = td["fields"]
    gen_set = set(gen_dotted_names_so_far)

    illixr_includes, system_includes = required_includes(td, gen_set)

    open_ns = bg_helpers.dotted_to_open_namespaces(dotted)
    close_ns = bg_helpers.dotted_to_close_namespaces(dotted)

    header = [f"// Copyright 2020-{bg_types.YEAR}, The Board of Trustees of the University of Illinois.",
              "// SPDX-License-Identifier: BSL-1.0",
              "// This file was generated by generate_python_bridges.py -- do not edit directly.",
              f"// Bridge type: {bg_helpers.dotted_to_python_import(dotted)}",
              "",
              "#pragma once",
              "",
              '#include "illixr/switchboard.hpp"']
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
            options=(_clang.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES |
                     _clang.TranslationUnit.PARSE_INCOMPLETE),
            )

        # Include path is relative to the include root, e.g.
        # "illixr/data_format/serialization/pose.hpp"
        rel_include = str(hdr.relative_to(include_root)).replace("\\", "/")

        def _walk(cursor: _clang.Cursor,
                  in_boost_ser: bool = False) -> None:
            """Recursively walk the AST collecting Boost serialization function declarations.

            Args:
                cursor (_clang.Cursor): Current AST node.
                in_boost_ser (bool): Whether we are inside ``namespace boost::serialization``.

            Returns:
                None: Results are accumulated in the enclosing ``result`` dict.
            """
            # pylint: disable=cell-var-from-loop
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
    """Generate the Boost serialization header (``_ser.hpp``) for a bridge type.

    The serialization header declares a ``boost::serialization::serialize``
    (or split ``save``/``load``) free function for the struct.  It ``#include``s
    the ``_ser.hpp`` of every contained bridge-defined type (for nested
    serialization) and the Boost serialization header for any contained ILLIXR
    system type that has one.

    Args:
        td (dict): Normalized type definition dict.
        header_file (str): Filename of the struct header (used in the ``#include``).
        gen_dotted_names_so_far (iterable[str]): Dotted names of all bridge types
                                                  generated so far.
        illixr_ser_map (dict | None): Map of ILLIXR type name -> serialization
                                       header path, from ``build_illixr_serialization_map``.
                                       ``None`` disables system-type serialization includes.

    Returns:
        str: Complete C++ serialization header content as a single string.
    """
    # pylint: disable=too-many-locals,too-many-branches,too-many-statements
    dotted = td["dotted"]
    fields = td["fields"]
    has_mat = any(bg_types.is_mat(f["type"]) for f in fields.values())
    full_qn = bg_helpers.dotted_to_cpp_ns(dotted)
    gen_set = set(gen_dotted_names_so_far)

    # Collect ser includes for contained types.
    # For bridge-defined types: include their _ser.hpp (same directory).
    # For ILLIXR system types: look up in the dynamically built map.
    _ser_map = illixr_ser_map or {}

    contained_ser_includes = []
    for field_def in fields.values():
        field_type = field_def["type"]
        if field_type in gen_set:
            # Bridge-defined nested type: include its _ser.hpp
            rel = bg_helpers.dotted_to_path(field_type)
            contained_ser_includes.append(f'"illixr/bridge/{rel}_ser.hpp"')
        elif field_type in _ser_map:
            contained_ser_includes.append(f'"{_ser_map[field_type]}"')
    # Deduplicate while preserving order
    seen = set()
    deduped = []
    for h in contained_ser_includes:
        if h not in seen:
            seen.add(h)
            deduped.append(h)
    contained_ser_includes = deduped

    header = [f"// Copyright 2020-{bg_types.YEAR}, The Board of Trustees of the University of Illinois.",
              "// SPDX-License-Identifier: BSL-1.0",
              "// This file was generated by generate_python_bridges.py -- do not edit directly.",
              f"// Bridge type: {bg_helpers.dotted_to_python_import(dotted)}",
              "",
              "#pragma once",
              "",
              f"#include \"{header_file}\""]
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
            if bg_types.is_mat(field_def["type"]):
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
            if bg_types.is_mat(field_def["type"]):
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


def gen_boost_cpp(td, header_file):
    """Generate the Boost serialization implementation file (``_ser.cpp``) for a bridge type.

    Provides ``BOOST_CLASS_EXPORT_IMPLEMENT`` for the struct and ``#include``s
    the corresponding ``_ser.hpp``.

    Args:
        td (dict): Normalized type definition dict.
        header_file (str): Filename of the ``_ser.hpp`` to include.

    Returns:
        str: Complete C++ serialization implementation content as a single string.
    """
    dotted = td["dotted"]
    full_qn = bg_helpers.dotted_to_cpp_ns(dotted)
    cpp = [f"#include \"{header_file}\"", "", f"BOOST_CLASS_EXPORT_IMPLEMENT({full_qn})"]
    return "\n".join(cpp)

# ---------------------------------------------------------------------------
# pybind11 bindings generation
# ---------------------------------------------------------------------------


def include_for_type(type_name, gen_dotted_names):
    """Return the ``#include`` path string for a type used in a plugin.

    Bridge-defined types include from the generated ``illixr/bridge/...`` tree;
    ILLIXR system types include from ``illixr/data_format/...``.

    Args:
        type_name (str): Dotted bridge type name or bare ILLIXR system type name.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: A quoted include path string, e.g.
             ``'"illixr/bridge/semantic_xr/semantic_data.hpp"'``.
    """
    if type_name in gen_dotted_names:
        return f'"illixr/bridge/{bg_helpers.dotted_to_path(type_name)}.hpp"'
    return f'"illixr/data_format/{type_name}.hpp"'


def cpp_type_for_switchboard(type_name, gen_dotted_names):
    """Return the C++ type name to use as the template argument of ``switchboard::reader/writer<T>``.

    Bridge-defined types need the fully qualified ``ILLIXR::bridge::...`` name;
    ILLIXR system types are already in the ``ILLIXR`` namespace and use their
    bare name.

    Args:
        type_name (str): Dotted bridge type name or bare ILLIXR system type name.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: C++ type name, e.g. ``"ILLIXR::bridge::semantic_xr::semantic_data"``
             or ``"combined_pose"``.
    """
    if type_name in gen_dotted_names:
        return bg_helpers.dotted_to_cpp_ns(type_name)
    return type_name  # ILLIXR system type, already in ILLIXR namespace
