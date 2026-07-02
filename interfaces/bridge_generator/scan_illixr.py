# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""libclang scan of ILLIXR data_format/ headers for serialization info."""
import re
import sys
from pathlib import Path

import yaml

try:
    import clang.cindex as _clang
except ImportError:
    print("generate_system_types.py requires libclang: pip install clang",
          file=sys.stderr)
    sys.exit(1)


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
                    and cursor.kind in (_clang.CursorKind.FUNCTION_TEMPLATE,
                                        _clang.CursorKind.FUNCTION_DECL,)
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


def load_system_yamls(system_yaml_dir: Path) -> dict:
    """
    Load all auto-generated system type YAMLs from system_yaml_dir and
    return a map dotted_name -> td compatible with _gen_reader_dict_body.

    Inherited fields from base classes are inlined (base fields first, then
    derived) so that the Python dict contains all fields in the correct order.

    system_yaml_dir is expected to contain one .yaml file per data_format
    header, as written by generate_system_types.py.
    """
    if not system_yaml_dir.exists():
        return {}

    raw: dict = {}  # dotted -> {dotted, fields, base}

    for yf in sorted(yf for yf in system_yaml_dir.rglob("*.yaml") if not yf.name.startswith(".")):
        try:
            data = yaml.safe_load(yf.read_text())
        except Exception:
            continue
        if not data or "structs" not in data:
            continue
        for _, struct_def in data["structs"].items():
            dotted = struct_def.get("dotted", "")
            if not dotted:
                continue
            fields: dict = {}
            for field_name, field_def in (struct_def.get("fields") or {}).items():
                if isinstance(field_def, dict):
                    fields[field_name] = {
                        "type":      field_def.get("type", "int"),
                        "container": field_def.get("container", None),
                        "shape":     field_def.get("shape",     None),
                        "image":     None,
                        }
                else:
                    fields[field_name] = {
                        "type": str(field_def), "container": None,
                        "shape": None, "image": None,
                        }
            raw[dotted] = {
                "dotted": dotted,
                "fields": fields,
                "base":   struct_def.get("base"),
                }

    # Flatten inheritance: base fields first, then derived override
    def _flatten(dotted: str, visited: set) -> dict:
        """Recursively inline base class fields before derived fields (DFS).

        Args:
            dotted (str): Dotted type name to flatten.
            visited (set): Guard against inheritance cycles.

        Returns:
            dict: Merged fields dict with base fields first, derived fields overriding.
        """
        if dotted in visited:
            return {}  # cycle guard
        visited.add(dotted)
        if dotted not in raw:
            return {}
        entry = raw[dotted]
        base_fields: dict = {}
        if entry.get("base"):
            base_fields = _flatten(entry["base"], visited)
        return {**base_fields, **entry["fields"]}

    result: dict = {}
    for dotted in raw:
        result[dotted] = {
            "dotted": dotted,
            "fields": _flatten(dotted, set()),
            }
    return result
