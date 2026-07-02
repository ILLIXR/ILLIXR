# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Type-system constants, auto-discovery, and primitive helpers."""
from __future__ import annotations

import datetime
import sys
from pathlib import Path

try:
    import clang.cindex as _clang
except ImportError:
    print("generate_system_types.py requires libclang: pip install clang",
          file=sys.stderr)
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

# Image types: the underlying storage is std::vector<uint8_t>.
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
# KNOWN_ILLIXR_TYPES is built lazily by scanning include/illixr/data_format/*.hpp
# via libclang. Maps bare struct name -> include path relative to source include root.
# Falls back to an empty dict if libclang is unavailable.
_illixr_types_cache: dict | None = None


def _build_known_illixr_types(source_dir: Path) -> dict:
    """Scan ILLIXR data_format headers and build a map of known struct names to header paths.

    Walks every ``.hpp`` in ``include/illixr/data_format/`` (non-recursive, skipping
    ``serialization/``) via libclang and collects every struct definition found directly
    inside ``namespace ILLIXR``.  Results are cached for the lifetime of the process.

    Args:
        source_dir (Path): Root of the ILLIXR repository (contains ``include/``).

    Returns:
        dict[str, str]: Map of bare struct name to include path relative to
                        ``source_dir/include``, e.g.
                        ``{"combined_pose": "illixr/data_format/combined_pose.hpp"}``.
                        Returns an empty dict if libclang is unavailable or
                        ``data_format/`` does not exist.
    """
    global _illixr_types_cache
    if _illixr_types_cache is not None:
        return _illixr_types_cache

    data_format_dir = source_dir / "include" / "illixr" / "data_format"
    if not data_format_dir.exists():
        _illixr_types_cache = {}
        return _illixr_types_cache

    include_root = source_dir / "include"
    result: dict = {}
    index = _clang.Index.create()

    for hdr in sorted(data_format_dir.glob("*.hpp")):
        tu = index.parse(
            str(hdr),
            args=["-std=c++17", "-x", "c++",
                  "-I", str(include_root), "-undef"],
            options=(
                _clang.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES |
                _clang.TranslationUnit.PARSE_INCOMPLETE
                ),
            )
        rel = str(hdr.relative_to(include_root)).replace("\\\\", "/")

        def _walk_types(cursor, in_illixr=False):
            """Recursively walk the AST collecting struct definitions inside namespace ILLIXR.

            Args:
                cursor (_clang.Cursor): Current AST node.
                in_illixr (bool): Whether we are currently inside ``namespace ILLIXR``.
            """
            # pylint: disable=cell-var-from-loop
            if cursor.kind == _clang.CursorKind.NAMESPACE:
                enter = in_illixr or cursor.spelling == "ILLIXR"
                for child in cursor.get_children():
                    _walk_types(child, enter)
                return
            if (in_illixr and
                    cursor.kind == _clang.CursorKind.STRUCT_DECL and
                    cursor.spelling and
                    cursor.is_definition() and
                    cursor.spelling not in result):
                result[cursor.spelling] = rel
            for child in cursor.get_children():
                _walk_types(child, in_illixr)

        _walk_types(tu.cursor)

    _illixr_types_cache = result
    return result


# Module-level alias - populated on first call to run_generate().
# Code that checks 'field_type in KNOWN_ILLIXR_TYPES' will use this.
KNOWN_ILLIXR_TYPES: dict = {}


def ensure_known_illixr_types(source_dir: Path) -> None:
    """Populate ``KNOWN_ILLIXR_TYPES`` by scanning data_format headers if not already done.

    This is the only place where ``KNOWN_ILLIXR_TYPES`` is assigned.
    All other modules read it via ``_bg_types.KNOWN_ILLIXR_TYPES`` to
    ensure they always see the populated value.

    Args:
        source_dir (Path): ILLIXR repository root (contains ``include/``).

    Returns:
        None
    """
    global KNOWN_ILLIXR_TYPES
    if not KNOWN_ILLIXR_TYPES:
        KNOWN_ILLIXR_TYPES = _build_known_illixr_types(source_dir)



# ---------------------------------------------------------------------------
# Dotted-name helpers
#
# A dotted type name like "geometry.camera_intrinsics" encodes both the
# directory path relative to interfaces/data/ and the C++ namespace nesting
# inside ILLIXR::bridge.
#
# All internal representations keep the dotted form as the canonical key.
# ---------------------------------------------------------------------------

def canonical_type(t):
    """Return the canonical (normalized) form of a YAML scalar type name.

    Resolves aliases such as ``"int32"`` -> ``"int"``, ``"str"`` -> ``"string"``.
    Unknown names are returned unchanged.

    Args:
        t (str): YAML type name, e.g. ``"int32"`` or ``"float64"``.

    Returns:
        str: Canonical type name, e.g. ``"int"`` or ``"double"``.
    """
    return SCALAR_ALIASES.get(t, t)


def is_scalar(t):
    """Return True if ``t`` is a known YAML scalar type (after alias resolution).

    Args:
        t (str): YAML type name.

    Returns:
        bool: ``True`` if ``canonical_type(t)`` is in ``SCALAR_TYPES``.
    """
    return canonical_type(t) in SCALAR_TYPES


def is_mat(t):
    """Return True if ``t`` is an OpenCV Mat type (``mat_8u``, ``mat_32f``, etc.).

    Args:
        t (str): YAML type name.

    Returns:
        bool: ``True`` if ``t`` is a key in ``MAT_TYPES``.
    """
    return t in MAT_TYPES


def cpp_scalar(ctype):
    """Return the C++ primitive type name for a canonical scalar type.

    Args:
        ctype (str): Canonical scalar type name, e.g. ``"int"``, ``"float"``, ``"uint64"``.
                     Must be a key in ``SCALAR_TYPES`` (use ``canonical_type`` first).

    Returns:
        str: C++ type name, e.g. ``"int32_t"``, ``"float"``, ``"uint64_t"``.

    Raises:
        KeyError: If ``ctype`` is not in ``SCALAR_TYPES``.
    """
    return SCALAR_TYPES[ctype][0]
