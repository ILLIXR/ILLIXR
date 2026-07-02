# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""pybind11 binding and Python package tree generation."""
from pathlib import Path

from . import types as bg_types
from . import helpers as bg_helpers


def _mat_getter_lines(field_name, field_type, qname):
    """Generate pybind11 property getter lines for a ``cv::Mat`` field.

    Returns a ``py::array`` view into the Mat data without copying.
    Empty Mats are exposed as ``None``.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_type (str): Mat type key, e.g. ``"mat_32f"``.
        qname (str): Fully qualified C++ struct name for the lambda parameter.

    Returns:
        list[str]: C++ source lines implementing the getter lambda.
    """
    np_dtype = bg_types.MAT_TYPES[field_type][1][3:]  # strip "np."
    return [
        f'    .def_property("{field_name}",',
        f'        [](const {qname}& self) -> py::array {{',
        f'            if (self.{field_name}_.empty()) return py::array();',
        '            py::object guard = py::capsule(',
        f'                new std::shared_ptr<{qname}>(),',
        '                [](void* p) {',
        f'                    delete static_cast<std::shared_ptr<{qname}>*>(p); }});',
        '            std::vector<ssize_t> shp, str;',
        f'            if (self.{field_name}_.channels() == 1) {{',
        f'                shp = {{self.{field_name}_.rows, self.{field_name}_.cols}};',
        f'                str = {{(ssize_t)self.{field_name}_.step[0],',
        f'                        (ssize_t)self.{field_name}_.elemSize()}};',
        '            } else {',
        f'                shp = {{self.{field_name}_.rows, self.{field_name}_.cols,',
        f'                        self.{field_name}_.channels()}};',
        f'                str = {{(ssize_t)self.{field_name}_.step[0],',
        f'                        (ssize_t)self.{field_name}_.step[1],',
        f'                        (ssize_t)self.{field_name}_.elemSize1()}};',
        '            }',
        f'            return py::array(py::dtype("{np_dtype}"),',
        f'                shp, str, self.{field_name}_.data, guard);',
        '        },',
        ]


def _mat_setter_lines(field_name, field_type, qname):
    """Generate pybind11 property setter lines for a ``cv::Mat`` field.

    Accepts a numpy array from Python and copies it into a ``cv::Mat``.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_type (str): Mat type key, e.g. ``"mat_32f"``.
        qname (str): Fully qualified C++ struct name for the lambda parameter.

    Returns:
        list[str]: C++ source lines implementing the setter lambda.
    """
    cv_base = bg_types.MAT_TYPES[field_type][0]
    cpp_elem = bg_types.MAT_TYPES[field_type][2]
    return [
        f'        []({qname}& self, py::array_t<{cpp_elem}> arr) {{',
        '            auto buf = arr.request();',
        '            int r = (int)buf.shape[0], c = (int)buf.shape[1];',
        '            int ch = (buf.ndim == 3) ? (int)buf.shape[2] : 1;',
        f'            cv::Mat tmp(r, c, CV_MAKETYPE({cv_base}(ch), ch), buf.ptr);',
        f'            tmp.copyTo(self.{field_name}_);',
        '        })',
        ]


def _fixed_array_getter_lines(field_name, field_def, qname, gen_dotted_names):
    """Generate pybind11 property getter lines for a fixed-size C-array field.

    Returns a numpy array view over the raw array data.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition (must have ``"shape"`` key).
        qname (str): Fully qualified C++ struct name for the lambda parameter.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        list[str]: C++ source lines implementing the getter lambda.
    """
    field_type = field_def["type"]
    shape = field_def["shape"]

    if field_type in gen_dotted_names:
        n = shape[0]
        return [
            f'    .def_property("{field_name}",',
            f'        [](const {qname}& self) {{',
            '            py::list result;',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                result.append(self.{field_name}_[i]);',
            '            return result;',
            '        },',
            ]

    ctype = bg_types.canonical_type(field_type)
    cpp_t = bg_types.cpp_scalar(ctype)
    if len(shape) == 1:
        flat = shape[0]
        data_p = f"self.{field_name}_"
    elif len(shape) == 2:
        flat = shape[0] * shape[1]
        data_p = f"&self.{field_name}_[0][0]"
    else:
        flat = shape[0] * shape[1] * shape[2]
        data_p = f"&self.{field_name}_[0][0][0]"
    return [
        f'    .def_property("{field_name}",',
        f'        [](const {qname}& self) -> py::array_t<{cpp_t}> {{',
        f'            return py::array_t<{cpp_t}>(',
        f'                {{{flat}}}, {{sizeof({cpp_t})}},',
        f'                const_cast<{cpp_t}*>({data_p}));',
        '        },',
        ]


def _fixed_array_setter_lines(field_name, field_def, qname, gen_dotted_names):
    """Generate pybind11 property setter lines for a fixed-size C-array field.

    Accepts a numpy array and copies elements into the fixed C-array.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition (must have ``"shape"`` key).
        qname (str): Fully qualified C++ struct name for the lambda parameter.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        list[str]: C++ source lines implementing the setter lambda.
    """
    field_type = field_def["type"]
    shape = field_def["shape"]

    if field_type in gen_dotted_names:
        n = shape[0]
        fqn = bg_helpers.dotted_to_cpp_ns(field_type)
        return [
            f'        []({qname}& self, py::list lst) {{',
            f'            if ((int)lst.size() != {n})',
            '                throw std::runtime_error(',
            f'                    "{field_name}: expected {n} elements");',
            f'            for (int i = 0; i < {n}; ++i)',
            f'                self.{field_name}_[i] = lst[i].cast<{fqn}>();',
            '        })',
            ]

    ctype = bg_types.canonical_type(field_type)
    cpp_t = bg_types.cpp_scalar(ctype)
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
        '                throw std::runtime_error(',
        f'                    "{field_name}: expected {flat} elements");',
        f'            std::copy(arr.data(), arr.data() + {flat}, {dest});',
        '        })',
        ]


def _kw_param(field_name, field_def, gen_dotted_names):
    """Return the pybind11 keyword argument parameter declaration for a field.

    Used in the constructor binding to support named-argument construction
    from Python: ``MyStruct(field_name=value, ...)``.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: A ``py::arg("field_name")`` expression, or ``None`` for fields
             that cannot be passed by keyword (e.g., image fields).
    """
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if bg_types.is_mat(field_type) or (shape and not container):
        return f'py::arg("{field_name}") = py::none()'

    if field_type in gen_dotted_names:
        fqn = bg_helpers.dotted_to_cpp_ns(field_type)
        if container == "vector":
            return f'py::arg("{field_name}") = std::vector<{fqn}>()'
        return f'py::arg("{field_name}") = {fqn}()'

    ctype = bg_types.canonical_type(field_type)
    cpp_t = bg_types.cpp_scalar(ctype)
    if container == "vector":
        return f'py::arg("{field_name}") = std::vector<{cpp_t}>()'
    if container == "dict":
        return f'py::arg("{field_name}") = std::unordered_map<std::string, {cpp_t}>()'
    defaults = {"bool": "false", "std::string": '""',
                "float": "0.0f", "double": "0.0"}
    return f'py::arg("{field_name}") = {defaults.get(cpp_t, "0")}'


def _kw_init_body(field_name, field_def, gen_dotted_names):
    """Return the constructor body statement that assigns a keyword argument to a field.

    Args:
        field_name (str): Field name (without a trailing underscore).
        field_def (dict): Normalized field definition.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: A C++ assignment statement, e.g. ``"obj.x_ = x;"``
    """
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if bg_types.is_mat(field_type):
        cv_base = bg_types.MAT_TYPES[field_type][0]
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
            fqn = bg_helpers.dotted_to_cpp_ns(field_type)
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
        ctype = bg_types.canonical_type(field_type)
        cpp_t = bg_types.cpp_scalar(ctype)
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
    """Return the C++ type to use as the lambda parameter for a pybind11 setter.

    Args:
        field_def (dict): Normalized field definition.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.

    Returns:
        str: C++ type name, e.g. ``"float"`` or ``"py::array_t<float>"``.
    """
    field_type = field_def["type"]
    container = field_def.get("container")
    shape = field_def.get("shape")

    if bg_types.is_mat(field_type) or (shape and not container):
        return "py::object"

    if field_type in gen_dotted_names:
        fqn = bg_helpers.dotted_to_cpp_ns(field_type)
        if container == "vector":
            return f"std::vector<{fqn}>"
        return fqn

    ctype = bg_types. canonical_type(field_type)
    cpp_t = bg_types.cpp_scalar(ctype)
    if container == "vector":
        return f"std::vector<{cpp_t}>"
    if container == "dict":
        return f"std::unordered_map<std::string, {cpp_t}>"
    return cpp_t


def gen_bindings_cpp(td, gen_dotted_names_so_far):
    """Generate the pybind11 bindings source file for a bridge type.

    Produces a ``.cpp`` file that defines a pybind11 embedded module exposing
    the struct as a Python class with property getters/setters and a keyword
    argument constructor.

    Args:
        td (dict): Normalized type definition dict.
        gen_dotted_names_so_far (iterable[str]): Dotted names of all bridge types
                                                  generated before this one.

    Returns:
        str: Complete C++ bindings source file content.
    """
    # pylint: disable=too-many-statements,too-many-locals
    dotted = td["dotted"]
    stem = bg_helpers.dotted_stem(dotted)
    fields = td["fields"]
    gen_set = set(gen_dotted_names_so_far)
    has_mat = any(bg_types.is_mat(f["type"]) for f in fields.values())
    qname = bg_helpers.dotted_to_cpp_ns(dotted)
    module = bg_helpers.dotted_to_module_name(dotted)
    hdr = bg_helpers.dotted_to_header_path(dotted)

    cpp = [f"// Copyright 2020-{bg_types.YEAR}, The Board of Trustees of the University of Illinois.",
           "// SPDX-License-Identifier: BSL-1.0",
           "// This file was generated by generate_python_bridges.py -- do not edit directly.",
           f"// Python module: {bg_helpers.dotted_to_python_import(dotted)}",
           "",
           f'#include "{hdr}"',
           "",
           "#include <pybind11/embed.h>",
           "#include <pybind11/numpy.h>",
           "#include <pybind11/stl.h>"]
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
    cpp.append(f'    m.doc() = "ILLIXR bridge type: {bg_helpers.dotted_to_python_import(dotted)}";')
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
        cpp.append("        .def(py::init<>())")

    # Properties and readwrite
    for field_name, field_def in fields.items():
        field_type = field_def["type"]
        container = field_def.get("container")
        shape = field_def.get("shape")

        if bg_types.is_mat(field_type):
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
    """Generate the Python package ``__init__.py`` files for all bridge types.

    Creates one ``__init__.py`` per namespace directory under the plugin
    directory so that bridge types can be imported as
    ``from illixr.bridge.namespace import TypeName``.

    Args:
        sorted_types (list[dict]): Type definition dicts in topological order.
        plugin_dir (Path): Root directory of the generated plugin sources.

    Returns:
        None: Files are written directly to ``plugin_dir``.
    """
    # pylint: disable=too-many-locals
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
            "# Auto-generated by generate_python_bridges.py -- do not edit directly.",
            f"# Bridge types in illixr.bridge{'.' + '.'.join(rel_parts) if rel_parts else ''}",
            "",
            ]
        for td in types_here:
            module = bg_helpers.dotted_to_module_name(td["dotted"])
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
