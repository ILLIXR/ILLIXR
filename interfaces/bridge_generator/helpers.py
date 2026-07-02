# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Dotted-name helpers, SchemaError, topo_sort, cmake_list."""
from pathlib import Path


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


def topo_sort(type_defs):
    """Sort by dotted name key; dependencies before dependents."""
    name_to_def = {td["dotted"]: td for td in type_defs}
    visited = set()
    order = []

    def visit(dotted):
        """Visit a type node in the dependency graph, appending it in post-order.

        Args:
            dotted (str): Dotted type name to visit.

        Returns:
            None
        """
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

def cmake_list(items):
    """Join a sequence of items into a CMake semicolon-separated list string.

    Args:
        items (Iterable): Items to join; each is converted to ``str``.

    Returns:
        str: Semicolon-separated string suitable for use in CMake ``set()`` calls.
    """
    return ";".join(str(i) for i in items)
