# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Unity/C# P/Invoke reader code generation.

Mirrors codegen_reader.py's role (C++ struct -> consumer-side representation)
but targets a fixed-layout P/Invoke wire struct instead of a py::dict, which
cannot hold unbounded data inline. Each type's fields are split into:

  FIXED    -- scalars, fixed-size 'shape' arrays, and non-container nested
              bridge-defined structs. These flatten directly into a wire
              struct with a 1:1 C++/C# layout.

  VARIABLE -- 'container: vector' fields (scalar or nested-struct element
              type), 'type: image' fields, and 'string' fields. These cannot
              live in a fixed-layout struct; each becomes a separate bulk
              accessor call taking a caller-allocated buffer and max length,
              mirroring the get_query_response_info/get_query_response_points
              split already used by handwritten Unity bridge code.

Scope note: vector-of-struct fields are only unrolled one level deep (i.e.,
a VARIABLE field whose element type itself contains a VARIABLE field is
supported; an element type containing a *nested* vector-of-struct field is
not). This covers every type in the semantic_xr example set; deeper nesting
would need recursive accessor names and is left for a follow-up pass.
"""

from . import types as bg_types


def dotted_to_wire_name(dotted: str) -> str:
    """'semantic_xr.point_cloud' -> 'unity_wire_semantic_xr_point_cloud'"""
    return "unity_wire_" + dotted.replace(".", "_")


def shape_count(shape):
    """Count the number of dimensions"""
    n = 1
    for d in shape:
        n *= d
    return n


def is_variable_field(field_def) -> bool:
    """True if a field cannot be flattened into a fixed-layout wire struct."""
    if field_def.get("type") == "string":
        return True
    if field_def.get("image"):
        return True
    if field_def.get("container") in ("vector", "dict"):
        return True
    return False


def classify_fields(td):
    """Split td['fields'].items() into (fixed, variable) lists of (name, field_def)."""
    fixed, variable = [], []
    for field_name, field_def in td["fields"].items():
        (variable if is_variable_field(field_def) else fixed).append((field_name, field_def))
    return fixed, variable


# ---------------------------------------------------------------------------
# FIXED fields -> wire struct declaration + flatten body
# ---------------------------------------------------------------------------

def gen_wire_struct_fields(td, gen_dotted_names):
    """
    C++ field declarations for the wire/info struct: one entry per FIXED
    field, plus a '<name>_count_' int32_t for every VARIABLE field (except
    'image', whose size is derivable from sibling width/height fields already
    present as FIXED fields -- no separate count is needed).
    """
    lines = []
    fixed, variable = classify_fields(td)

    for field_name, field_def in fixed:
        field_type = field_def["type"]
        shape = field_def.get("shape")
        if shape:
            cpp_t = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
            n = shape_count(shape)
            for i in range(n):
                lines.append(f"    {cpp_t} {field_name}_{i};")
        elif field_type in gen_dotted_names or field_type in bg_types.KNOWN_ILLIXR_TYPES:
            lines.append(f"    {dotted_to_wire_name(field_type)} {field_name}_;")
        else:
            cpp_t = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
            lines.append(f"    {cpp_t} {field_name}_;")

    for field_name, field_def in variable:
        if field_def.get("image"):
            continue  # size derivable from sibling width/height fields
        lines.append(f"    int32_t {field_name}_count_;")

    return lines


def gen_flatten_body(td, gen_dotted_names, src="src", dst="out", indent="    "):
    """
    C++ statements populating a wire struct (dst) from a native struct (src).
    FIXED fields are copied/flattened directly; VARIABLE fields only get
    their '_count_' populated here -- actual data movement happens in the
    bulk accessor functions from gen_bulk_accessors().
    """
    # pylint: disable=too-many-locals,too-many-branches
    lines = []
    fixed, variable = classify_fields(td)

    for field_name, field_def in fixed:
        field_type = field_def["type"]
        shape = field_def.get("shape")
        if shape:
            n = shape_count(shape)
            if len(shape) == 1:
                ptr = f"{src}.{field_name}_"
            elif len(shape) == 2:
                ptr = f"&{src}.{field_name}_[0][0]"
            else:
                ptr = f"&{src}.{field_name}_[0][0][0]"
            for i in range(n):
                lines.append(f"{indent}{dst}.{field_name}_{i} = ({ptr})[{i}];")
        elif field_type in gen_dotted_names or field_type in bg_types.KNOWN_ILLIXR_TYPES:
            flat = dotted_to_wire_name(field_type).replace("unity_wire_", "flatten_")
            lines.append(f"{indent}{dst}.{field_name}_ = {flat}({src}.{field_name}_);")
        else:
            lines.append(f"{indent}{dst}.{field_name}_ = {src}.{field_name}_;")

    for field_name, field_def in variable:
        if field_def.get("image"):
            continue
        if field_def.get("container") == "vector" and (
                field_def["type"] in gen_dotted_names or field_def["type"] in bg_types.KNOWN_ILLIXR_TYPES):
            lines.append(f"{indent}{dst}.{field_name}_count_ = "
                         f"static_cast<int32_t>({src}.{field_name}_.size());")
        elif field_def["type"] == "string":
            lines.append(f"{indent}{dst}.{field_name}_count_ = "
                         f"static_cast<int32_t>({src}.{field_name}_.size());")
        else:
            lines.append(f"{indent}{dst}.{field_name}_count_ = "
                         f"static_cast<int32_t>({src}.{field_name}_.size());")

    return lines


# ---------------------------------------------------------------------------
# VARIABLE fields -> bulk accessor function bodies
#
# Each accessor is returned as a dict:
#   name   -- suffix used to build illixr_unity_get_<alias>_<name>
#   params -- C++ parameter list (buffer + max_len, and/or index)
#   body   -- C++ statement lines for the function body (return count
#             actually copied)
# ---------------------------------------------------------------------------

def gen_bulk_accessors(td, gen_dotted_names, type_defs_map, src="src"):
    """Generate the accessors for Unity interfaces"""
    # pylint: disable=too-many-locals
    accessors = []
    _, variable = classify_fields(td)

    for field_name, field_def in variable:
        field_type = field_def["type"]

        if field_def.get("image"):
            w_dim, h_dim, ch = field_def["image"]
            w_expr = str(w_dim) if isinstance(w_dim, int) else f"{src}.{w_dim}_"
            h_expr = str(h_dim) if isinstance(h_dim, int) else f"{src}.{h_dim}_"
            accessors.append({
                "name": field_name,
                "params": "uint8_t* out_buffer, int32_t max_len",
                "body": [
                    f"    int32_t total = static_cast<int32_t>({w_expr} * {h_expr} * {ch});",
                    "    int32_t n = std::min(total, max_len);",
                    f"    std::memcpy(out_buffer, {src}.{field_name}_.data(), n);",
                    "    return n;",
                    ],
                })
            continue

        if field_type == "string":
            accessors.append({
                "name": field_name,
                "params": "char* out_buffer, int32_t max_len",
                "body": [
                    f"    int32_t n = static_cast<int32_t>("
                    f"std::min({src}.{field_name}_.size(), static_cast<size_t>(max_len - 1)));",
                    f"    std::memcpy(out_buffer, {src}.{field_name}_.data(), n);",
                    "    out_buffer[n] = '\\0';",
                    "    return n;",
                    ],
                })
            continue

        if field_def.get("container") == "vector" and (
                field_type in gen_dotted_names or field_type in bg_types.KNOWN_ILLIXR_TYPES):
            # Vector of nested bridge-defined struct -- one level of unrolling.
            elem_td = (type_defs_map or {}).get(field_type)
            if elem_td is None:
                continue
            elem_fixed, elem_variable = classify_fields(elem_td)

            # One bulk accessor per FIXED subfield: one value per outer element.
            for sub_name, sub_field_def in elem_fixed:
                sub_field_type = sub_field_def["type"]
                if sub_field_def.get("shape") or sub_field_type in gen_dotted_names or sub_field_type in bg_types.KNOWN_ILLIXR_TYPES:
                    continue  # not supported one level deep yet -- flag for follow-up
                cpp_t = bg_types.cpp_scalar(bg_types.canonical_type(sub_field_type))
                accessors.append({
                    "name": f"{field_name}_{sub_name}",
                    "params": f"{cpp_t}* out_buffer, int32_t max_len",
                    "body": [
                        f"    int32_t n = std::min(static_cast<int32_t>({src}.{field_name}_.size()), max_len);",
                        "    for (int32_t i = 0; i < n; ++i)",
                        f"        out_buffer[i] = {src}.{field_name}_[i].{sub_name}_;",
                        "    return n;",
                        ],
                    })

            # Per-element counts and concatenated data for each VARIABLE subfield.
            for sub_name, sub_field_def in elem_variable:
                sub_field_type = sub_field_def["type"]
                if sub_field_type == "string" or sub_field_def.get("image"):
                    continue  # not supported one level deep yet -- flag for follow-up
                cpp_t = bg_types.cpp_scalar(bg_types.canonical_type(sub_field_type))

                accessors.append({
                    "name": f"{field_name}_{sub_name}_counts",
                    "params": "int32_t* out_buffer, int32_t max_len",
                    "body": [
                        f"    int32_t n = std::min(static_cast<int32_t>({src}.{field_name}_.size()), max_len);",
                        "    for (int32_t i = 0; i < n; ++i)",
                        f"        out_buffer[i] = static_cast<int32_t>({src}.{field_name}_[i].{sub_name}_.size());",
                        "    return n;",
                        ],
                    })
                accessors.append({
                    "name": f"{field_name}_{sub_name}",
                    "params": f"{cpp_t}* out_buffer, int32_t max_len",
                    "body": [
                        "    int32_t written = 0;",
                        f"    for (const auto& _elem : {src}.{field_name}_) {{",
                        f"        int32_t n = std::min(static_cast<int32_t>(_elem.{sub_name}_.size()),",
                        "                             max_len - written);",
                        "        if (n <= 0) break;",
                        f"        std::memcpy(out_buffer + written, _elem.{sub_name}_.data(), n * sizeof({cpp_t}));",
                        "        written += n;",
                        "    }}",
                        "    return written;",
                        ],
                    })
            continue

        # Top-level vector of scalars.
        cpp_t = bg_types.cpp_scalar(bg_types.canonical_type(field_type))
        accessors.append({
            "name": field_name,
            "params": f"{cpp_t}* out_buffer, int32_t max_len",
            "body": [
                f"    int32_t n = std::min(static_cast<int32_t>({src}.{field_name}_.size()), max_len);",
                f"    std::memcpy(out_buffer, {src}.{field_name}_.data(), n * sizeof({cpp_t}));",
                "    return n;",
                ],
            })

    return accessors
