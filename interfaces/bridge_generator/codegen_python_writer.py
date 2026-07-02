# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Writer dict-body code generation (py::dict -> C++ struct)."""

from . import types as bg_types
from . import codegen_struct as bg_cg_struct


def gen_writer_from_dict_body(td, gen_dotted_names, src='d', dest='data',
                              indent='                    ',
                              type_defs_map=None):
    """Generate C++ lines that populate a C++ struct from a ``py::dict``.

    The generated code checks ``src.contains(key)`` before each assignment,
    so missing keys leave the corresponding struct field at its default value.

    Handles: scalars, fixed C-arrays, dynamic numpy arrays, ``type: image``
    fields, Eigen fields (``Vector``, ``Quaternion``, ``Matrix``), ``cv::Mat``,
    nested bridge structs, vectors of bridge structs, and ILLIXR system types.
    Eigen quaternion: Python ``[x, y, z, w]`` -> Eigen ``(w, x, y, z)``.

    Args:
        td (dict): Normalized type definition dict with ``"dotted"`` and
                   ``"fields"`` keys.
        gen_dotted_names (iterable[str]): All bridge-defined dotted type names.
        src (str): Name of the source ``py::dict`` variable.
        dest (str): Name of the destination struct variable.
        indent (str): Leading whitespace for each emitted line.
        type_defs_map (dict | None): Map of dotted name -> type definition,
                                      used to inline nested struct assignments.

    Returns:
        list[str]: C++ source lines populating the struct from the dict.
    """
    # pylint: disable=too-many-arguments,too-many-locals,too-many-branches,too-many-statements
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

    for field_name, field_def in td["fields"].items():
        if field_name in _auto_populated:
            continue
        field_type = field_def["type"]
        container = field_def.get("container")
        shape = field_def.get("shape")
        image = field_def.get("image")
        key = field_name

        result.append(indent + f'if ({src}.contains("{key}")) {{')

        if bg_types.is_mat(field_type):
            cpp_elem = bg_types.MAT_TYPES[field_type][2]
            cv_base = bg_types.MAT_TYPES[field_type][0]
            result.append(indent + f'    auto _arr = {src}["{key}"].cast<py::array_t<{cpp_elem}>>();')
            result.append(indent + "    auto _buf = _arr.request();")
            result.append(indent + "    int _r = (int)_buf.shape[0], _c = (int)_buf.shape[1];")
            result.append(indent + "    int _ch = (_buf.ndim == 3) ? (int)_buf.shape[2] : 1;")
            result.append(indent + f"    cv::Mat _tmp(_r, _c, CV_MAKETYPE({cv_base}(_ch), _ch), _buf.ptr);")
            result.append(indent + f"    _tmp.copyTo({dest}.{field_name}_);")

        elif shape and not container and field_def.get("cpp_type"):
            # Eigen field from a system type YAML - use Eigen-specific construction.
            # Python convention: Vector as [x,y,z], Quaternion as [x,y,z,w].
            # Eigen Quaternionf constructor: (w, x, y, z).
            # Note: the outer loop already wraps each field in if (src.contains(key)),
            # so we only emit the inner conversion body here.
            cpp_type = field_def["cpp_type"]
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            flat = 1
            for d_ in shape:
                flat *= d_
            result.append(indent + f'    auto _a = {src}["{key}"].cast<py::array_t<{cpp_t}>>();')
            result.append(indent + '    auto _p = _a.unchecked<1>();')
            result.append(indent + f'    if (_a.size() >= {flat}) {{')
            if cpp_type.startswith("Eigen::Quaternion"):
                # Python [x,y,z,w] -> Eigen (w,x,y,z)
                result.append(indent + f'        {dest}.{field_name}_ = {cpp_type}(')
                result.append(indent + f'            _p({flat-1}), _p(0), _p(1), _p(2));  // (w,x,y,z)')
            elif cpp_type.startswith("Eigen::Matrix"):
                # Map flat array into matrix using Eigen::Map (row-major)
                result.append(indent + f'        {dest}.{field_name}_ = Eigen::Map<const {cpp_type}>(_a.data());')
            else:
                # VectorNf/d - construct from individual elements
                args = ", ".join(f"_p({i})" for i in range(flat))
                result.append(indent + f'        {dest}.{field_name}_ = {cpp_type}({args});')
            result.append(indent + '    }')

        elif shape and not container:
            ctype = bg_types.canonical_type(field_type) if field_type not in set(gen_dotted_names) else None
            if ctype:
                cpp_t = bg_types.cpp_scalar(ctype)
                if len(shape) == 1:
                    flat = shape[0]
                    ptr = f"{dest}.{field_name}_"
                elif len(shape) == 2:
                    flat = shape[0] * shape[1]
                    ptr = f"&{dest}.{field_name}_[0][0]"
                else:
                    flat = shape[0] * shape[1] * shape[2]
                    ptr = f"&{dest}.{field_name}_[0][0][0]"
                result.append(indent + f'    auto _a = {src}["{key}"].cast<py::array_t<{cpp_t}>>();')
                result.append(indent + f"    if (_a.size() >= {flat})")
                result.append(indent + f"        std::copy(_a.data(), _a.data()+{flat}, {ptr});")

        elif image:
            # Image field (type: image): Python sends (H, W, ch) array matching
            # the reader output. Since it is C-contiguous, just flatten and copy.
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            w_dim, h_dim, _ = image
            result.append(indent + f'if ({src}.contains("{key}")) {{')
            result.append(indent + '    {')
            result.append(indent + f'        auto _np = {src}["{key}"].cast<py::array_t<{cpp_t}>>();')
            result.append(indent + '        auto _buf = _np.request();')
            result.append(indent + f'        {dest}.{field_name}_.assign(')
            result.append(indent + f'            static_cast<const {cpp_t}*>(_buf.ptr),')
            result.append(indent + f'            static_cast<const {cpp_t}*>(_buf.ptr) + _buf.size);')
            if isinstance(w_dim, str):
                result.append(indent + f'        {dest}.{w_dim}_ = (_buf.ndim > 1) ? (int32_t)_buf.shape[1] : 0;')
            if isinstance(h_dim, str):
                result.append(indent + f'        {dest}.{h_dim}_ = (_buf.ndim > 0) ? (int32_t)_buf.shape[0] : 0;')
            result.append(indent + '    }')
            result.append(indent + '}')

        elif (container == "vector"
              and shape and any(isinstance(e, (int, str)) for e in shape)
              and field_type not in set(gen_dotted_names)):
            # Dynamic numpy array: accept array, extract flat data and populate
            # sibling dimension fields from the array's shape attribute.
            ctype = bg_types.canonical_type(field_type)
            cpp_t = bg_types.cpp_scalar(ctype)
            sf = field_def["shape"]
            result.append(indent + "    {")
            result.append(indent + f"        auto _np = {src}[\"{key}\"].cast<py::array_t<{cpp_t}>>();")
            result.append(indent + "        auto _buf = _np.request();")
            result.append(indent + f"        {dest}.{field_name}_.assign(")
            result.append(indent + f"            static_cast<const {cpp_t}*>(_buf.ptr),")
            result.append(indent + f"            static_cast<const {cpp_t}*>(_buf.ptr) + _buf.size);")
            for di, sf_entry in enumerate(sf):
                if isinstance(sf_entry, int):
                    continue  # literal - no field to populate
                result.append(indent + f"        if (_buf.ndim > {di})")
                result.append(indent + f"            {dest}.{sf_entry}_ = (int32_t)_buf.shape[{di}];")
            result.append(indent + "    }")

        elif container == "vector" and (field_type in set(gen_dotted_names) or field_type in bg_types.KNOWN_ILLIXR_TYPES):
            # Vector of bridge-defined structs: iterate the Python list,
            # populate each element from its dict field-by-field.
            # Cannot use cast<std::vector<T>>() - T is not registered as pybind11 type.
            cpp_t = bg_cg_struct.cpp_type_for_switchboard(field_type, gen_dotted_names)
            result.append(indent + f'    if (py::isinstance<py::list>({src}["{key}"])) {{')
            result.append(indent + f'        auto _lst = {src}["{key}"].cast<py::list>();')
            result.append(indent + f'        {dest}.{field_name}_.clear();')
            result.append(indent + f'        {dest}.{field_name}_.reserve(_lst.size());')
            result.append(indent + '        for (auto _item : _lst) {')
            result.append(indent + '            if (!py::isinstance<py::dict>(_item)) continue;')
            result.append(indent + f'            {cpp_t} _elem;')
            result.append(indent + '            auto _ed = _item.cast<py::dict>();')
            # Inline the sub-struct field assignments
            if field_type in (type_defs_map or {}):
                sub_td = type_defs_map[field_type]
                sub_lines = gen_writer_from_dict_body(
                    sub_td, gen_dotted_names,
                    src='_ed', dest='_elem',
                    indent=indent + '            ',
                    type_defs_map=type_defs_map)
                result.extend(sub_lines)
            result.append(indent + f'            {dest}.{field_name}_.push_back(std::move(_elem));')
            result.append(indent + '        }')
            result.append(indent + '    }')

        elif container == "vector":
            elem = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
            result.append(indent + f'    {dest}.{field_name}_ = {src}["{key}"].cast<std::vector<{elem}>>();')

        elif container == "dict":
            ct = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
            result.append(indent + f'    {dest}.{field_name}_ = {src}["{key}"].cast<std::unordered_map<std::string,{ct}>>();')

        else:
            if field_type in ("string", "str") or bg_types.canonical_type(field_type) == "std::string":
                result.append(indent + f'    {dest}.{field_name}_ = {src}["{key}"].cast<std::string>();')
            elif field_type in set(gen_dotted_names):
                cpp_t = bg_cg_struct.cpp_type_for_switchboard(field_type, gen_dotted_names)
                result.append(indent + f'    {dest}.{field_name}_ = {src}["{key}"].cast<{cpp_t}>();')
            else:
                ct = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
                result.append(indent + f'    {dest}.{field_name}_ = {src}["{key}"].cast<{ct}>();')

        result.append(indent + "}")

    return result
