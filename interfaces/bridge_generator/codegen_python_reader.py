# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Reader dict-body code generation (C++ struct -> py::dict)."""

from . import types as bg_types


def gen_reader_dict_body(td, gen_dotted_names, var='val', indent='                ',
                         dict_var='_d', type_defs_map=None):
    """
    Generate C++ lines that build a py::dict from a struct value.
    Returns a list of C++ source line strings.
    """
    # pylint: disable=too-many-arguments,too-many-locals,too-many-branches,too-many-statements
    gen_set = set(gen_dotted_names)
    result = []
    for field_name, field_def in td["fields"].items():
        field_type = field_def["type"]
        container = field_def.get("container")
        shape = field_def.get("shape")
        image = field_def.get("image")

        if bg_types.is_mat(field_type):
            np_dtype = bg_types.MAT_TYPES[field_type][1][3:]  # strip "np."
            result.append(indent + "{")
            result.append(indent + f"    const auto& _m = {var}.{field_name}_;")
            result.append(indent + "    if (!_m.empty()) {")
            result.append(indent + "        std::vector<ssize_t> _shp, _str;")
            result.append(indent + "        if (_m.channels() == 1) {")
            result.append(indent + "            _shp = {_m.rows, _m.cols};")
            result.append(indent + "            _str = {(ssize_t)_m.step[0], (ssize_t)_m.elemSize()};")
            result.append(indent + "        } else {")
            result.append(indent + "            _shp = {_m.rows, _m.cols, _m.channels()};")
            result.append(
                indent + "            _str = {(ssize_t)_m.step[0], (ssize_t)_m.step[1], (ssize_t)_m.elemSize1()};")
            result.append(indent + "        }")
            result.append(indent + f"        {dict_var}[\"{field_name}\"] = py::array(")
            result.append(indent + f'            py::dtype("{np_dtype}"), _shp, _str, _m.data);')
            result.append(indent + "    } else {")
            result.append(indent + f"        {dict_var}[\"{field_name}\"] = py::none();")
            result.append(indent + "    }")
            result.append(indent + "}")

        elif shape and not container and field_def.get("cpp_type"):
            # Eigen field from a system type YAML.
            # Expose as a numpy array in Python convention:
            #   Vector:     [x, y, z, ...]           (natural order via .data())
            #   Quaternion: [x, y, z, w]              (coeffs() gives xyzw order)
            #   Matrix:     flat row-major via .data()
            cpp_type = field_def["cpp_type"]
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            flat = 1
            for d_ in shape:
                flat *= d_
            if cpp_type.startswith("Eigen::Quaternion"):
                # coeffs() returns [x, y, z, w] - matches Python convention
                data_ptr = f"{var}.{field_name}_.coeffs().data()"
            else:
                # Vector and Matrix: .data() gives row-major elements
                data_ptr = f"{var}.{field_name}_.data()"
            result.append(indent +
                          f"{dict_var}[\"{field_name}\"] = py::array_t<{cpp_t}>"
                          f"({{{flat}}}, {{sizeof({cpp_t})}},"
                          f"reinterpret_cast<const {cpp_t}*>({data_ptr}));")

        elif shape and not container:
            ctype = bg_types.canonical_type(field_type) if field_type not in gen_set else None
            if ctype:
                cpp_t = bg_types.cpp_scalar(ctype)
                if len(shape) == 1:
                    flat = shape[0]
                    ptr = f"{var}.{field_name}_"
                elif len(shape) == 2:
                    flat = shape[0] * shape[1]
                    ptr = f"&{var}.{field_name}_[0][0]"
                else:
                    flat = shape[0] * shape[1] * shape[2]
                    ptr = f"&{var}.{field_name}_[0][0][0]"
                result.append(indent +
                              f"{dict_var}[\"{field_name}\"] = py::array_t<{cpp_t}>"
                              f"({{{flat}}}, {{sizeof({cpp_t})}},"
                              f"reinterpret_cast<const {cpp_t}*>({ptr}));")

        elif image:
            # Image field (type: image): stored as flat row-major (H, W, ch) bytes.
            # numpy output: (H, W, ch) with C-contiguous strides (W*ch, ch, 1),
            # matching entry_to_numpy. YAML shape [width, height, ch] is user-natural,
            # but the array axes follow the standard image convention.
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            w_dim, h_dim, ch = image
            w_expr = str(w_dim) if isinstance(w_dim, int) else f"{var}.{w_dim}_"
            h_expr = str(h_dim) if isinstance(h_dim, int) else f"{var}.{h_dim}_"
            result.append(indent + "{")
            result.append(indent + f"    auto* _vbuf_{field_name} = "
                                   f"new std::vector<{cpp_t}>({var}.{field_name}_);")
            result.append(indent + f"    py::capsule _cap_{field_name}("
                                   f"_vbuf_{field_name}, [](void* p) {{"
                                   f"delete static_cast<std::vector<{cpp_t}>*>(p); }});")
            result.append(indent +
                          f"    {dict_var}[\"{field_name}\"] = py::array_t<{cpp_t}>(")
            result.append(indent +
                          f"        {{{h_expr}, {w_expr}, {ch}}},")
            result.append(indent +
                          f"        {{(py::ssize_t)({w_expr} * {ch} * sizeof({cpp_t})),"
                          f" (py::ssize_t)({ch} * sizeof({cpp_t})),"
                          f" (py::ssize_t)sizeof({cpp_t})}},")
            result.append(indent +
                          f"        _vbuf_{field_name}->data(), _cap_{field_name});")
            result.append(indent + "}")

        elif (container == "vector"
              and shape and any(isinstance(e, (str, int)) for e in shape)
              and field_type not in gen_set):
            # Dynamic multidimensional numpy array.
            # Flat vector wrapped with runtime shape from sibling fields.
            # Uses a heap-allocated copy owned by a capsule for safe lifetime.
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            sf = field_def["shape"]
            ndim = len(sf)

            # Each entry in sf is either a string (sibling field name) or
            # an int literal. Build shape and C-contiguous stride expressions.
            def _dim_expr(d):
                """Return a C++ expression for a single shape dimension.

                Args:
                    d (int | str): Integer literal or field name string.

                Returns:
                    str: The integer as a string literal, or ``f"{var}.{d}_"``
                         for a named field dimension.
                """
                return str(d) if isinstance(d, int) else f"{var}.{d}_"

            shape_expr = ", ".join(f"(py::ssize_t){_dim_expr(d)}" for d in sf)
            # C-contiguous strides: stride[i] = product(dim[i+1:]) * sizeof(T)
            stride_parts = []
            for i in range(ndim):
                inner = sf[i + 1:]
                if inner:
                    prod = " * ".join(_dim_expr(d) for d in inner)
                    stride_parts.append(f"(py::ssize_t)({prod} * sizeof({cpp_t}))")
                else:
                    stride_parts.append(f"(py::ssize_t)sizeof({cpp_t})")
            stride_expr = ", ".join(stride_parts)
            result.append(indent + "{")
            result.append(indent + f"    auto* _vbuf_{field_name} = "
                                   f"new std::vector<{cpp_t}>({var}.{field_name}_);")
            result.append(indent + f"    py::capsule _cap_{field_name}("
                                   f"_vbuf_{field_name}, [](void* p) {{"
                                   f"delete static_cast<std::vector<{cpp_t}>*>(p); }});")
            result.append(indent + f"    {dict_var}[\"{field_name}\"] = "
                                   f"py::array_t<{cpp_t}>("
                                   f"{{{shape_expr}}}, {{{stride_expr}}},"
                                   f"_vbuf_{field_name}->data(), _cap_{field_name});")
            result.append(indent + "}")

        elif (field_type in gen_set or field_type in bg_types.KNOWN_ILLIXR_TYPES) and not container:
            # Nested bridge-defined struct - build a nested py::dict inline.
            # Cannot use py::cast because the binding module is not imported.
            sub_var = f"_sub_{field_name}"
            sub_dict = f"_dict_{field_name}"
            sub_td = (type_defs_map or {}).get(field_type)
            result.append(indent + "{")
            result.append(indent + f"    const auto& {sub_var} = {var}.{field_name}_;")
            result.append(indent + f"    py::dict {sub_dict};")
            if sub_td:
                sub_lines = gen_reader_dict_body(
                    sub_td, gen_dotted_names,
                    var=sub_var, indent=indent + "    ",
                    dict_var=sub_dict, type_defs_map=type_defs_map)
                result.extend(sub_lines)
            result.append(indent + f"    {dict_var}[\"{field_name}\"] = {sub_dict};")
            result.append(indent + "}")

        elif container == "vector" and (field_type in gen_set or field_type in bg_types.KNOWN_ILLIXR_TYPES):
            # Vector of bridge-defined structs - build a py::list of dicts.
            elem_var = f"_elem_{field_name}"
            elem_dict = f"_edict_{field_name}"
            list_var = f"_list_{field_name}"
            elem_td = (type_defs_map or {}).get(field_type)
            result.append(indent + "{")
            result.append(indent + f"    py::list {list_var};")
            result.append(indent + f"    for (const auto& {elem_var} : {var}.{field_name}_) {{")
            result.append(indent + f"        py::dict {elem_dict};")
            if elem_td:
                elem_lines = gen_reader_dict_body(
                    elem_td, gen_dotted_names,
                    var=elem_var, indent=indent + "        ",
                    dict_var=elem_dict, type_defs_map=type_defs_map)
                result.extend(elem_lines)
            result.append(indent + f"        {list_var}.append({elem_dict});")
            result.append(indent + "    }")
            result.append(indent + f"    {dict_var}[\"{field_name}\"] = {list_var};")
            result.append(indent + "}")

        else:
            # Scalars, vectors of scalars, dicts - pybind11 built-in converters handle these.
            result.append(indent + f"{dict_var}[\"{field_name}\"] = py::cast({var}.{field_name}_);")

    return result
