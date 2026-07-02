# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""C# struct, DllImport, and bridge-class generation for the Unity P/Invoke
boundary. Consumes the same classify_fields()/gen_bulk_accessors() output
codegen_unity_plugin.py uses for the C++ side, so the two stay in lockstep:
every extern "C" symbol gen_plugin_cpp emits has exactly one matching
[DllImport] here, with the same name and parameter shape.
"""

from . import types as bg_types
from . import codegen_unity_reader as bg_cg_unity_reader

# C++ scalar canonical name -> C# type
_CS_SCALAR = {
    "int8": "sbyte", "int16": "short", "int": "int", "int64": "long",
    "uint8": "byte", "uint16": "ushort", "uint32": "uint", "uint64": "ulong",
    "float": "float", "double": "double", "bool": "bool",
    }


def pascal(name: str) -> str:
    return "".join(p.capitalize() for p in name.split("_"))


def _struct_name(dotted: str) -> str:
    return pascal(dotted.split(".")[-1])


# ---------------------------------------------------------------------------
# Struct emission
# ---------------------------------------------------------------------------

def gen_csharp_struct(td, dotted, gen_dotted_names):
    """
    Emits a [StructLayout(LayoutKind.Sequential)] struct matching the C++
    wire struct field-for-field. Flat scalar fields only (no ByValArray) --
    fixed-size 'shape' arrays are unrolled into named Field0..FieldN, matching
    the C++ side, so no marshaling attributes are required anywhere.
    """
    fixed, variable = bg_cg_unity_reader.classify_fields(td)
    lines = ["[StructLayout(LayoutKind.Sequential)]",
             f"public struct {_struct_name(dotted)} {{"]

    for field_name, field_def in fixed:
        field_type = field_def["type"]
        shape = field_def.get("shape")
        pascal_name = pascal(field_name)
        if shape:
            cs_t = _CS_SCALAR[bg_types.canonical_type(field_type)]
            for i in range(bg_cg_unity_reader.shape_count(shape)):
                lines.append(f"    public {cs_t} {pascal_name}{i};")
        elif field_type in gen_dotted_names or field_type in bg_types.KNOWN_ILLIXR_TYPES:
            lines.append(f"    public {_struct_name(field_type)} {pascal_name};")
        else:
            cs_t = _CS_SCALAR[bg_types.canonical_type(field_type)]
            lines.append(f"    public {cs_t} {pascal_name};")

    for field_name, field_def in variable:
        if field_def.get("image"):
            continue  # size derived from sibling width/height fields
        lines.append(f"    public int {pascal(field_name)}Count;")

    lines.append("}")
    return "\n".join(lines)


def _types_needed_in_order(bridge, type_defs_map, gen_dotted_names):
    """
    Collects every type whose struct needs emitting: each input/output type,
    plus (recursively) any FIXED non-container nested struct field type they
    reference. Returned in dependency order (nested types before the types
    that embed them), matching how C# requires a struct's field types to
    already be declared.
    """
    ordered: list[str] = []
    seen: set = set()

    def visit(dotted: str):
        if dotted in seen or dotted not in type_defs_map:
            return
        seen.add(dotted)
        td = type_defs_map[dotted]
        fixed, _ = bg_cg_unity_reader.classify_fields(td)
        for _, field_def in fixed:
            field_type = field_def["type"]
            if not field_def.get("shape") and (field_type in gen_dotted_names or field_type in bg_types.KNOWN_ILLIXR_TYPES):
                visit(field_type)
        ordered.append(dotted)

    for inp in bridge["inputs"]:
        visit(inp["type"])
    for out in bridge["outputs"]:
        visit(out["type"])
    return ordered


# ---------------------------------------------------------------------------
# Parameter translation: C++ accessor param list -> C# P/Invoke param list
# ---------------------------------------------------------------------------

def _cs_params_for(cpp_params: str):
    """Translate a C++ 'T* out_buffer, int32_t max_len' param list into a
    matching C# P/Invoke parameter list. Buffers are pre-allocated arrays
    the caller sizes based on the corresponding *_count field / accessor
    from the info call -- no marshaling attributes needed since arrays
    decay to pointers by default for [DllImport] blittable element types."""
    if cpp_params.startswith("char*"):
        return "byte[] outBuffer, int maxLen"
    if cpp_params.startswith("uint8_t*"):
        return "byte[] outBuffer, int maxLen"
    if cpp_params.startswith("int32_t*"):
        return "int[] outBuffer, int maxLen"
    if cpp_params.startswith("float*"):
        return "float[] outBuffer, int maxLen"
    raise NotImplementedError(f"no C# marshalling rule for accessor params: {cpp_params!r}")


# ---------------------------------------------------------------------------
# Full bridge file
# ---------------------------------------------------------------------------


def gen_csharp_bridge_file(bridge, type_defs_map, gen_dotted_names):
    """
    One .cs file per bridge: every struct it needs, plus a static class with
    one [DllImport] + typed wrapper pair per extern "C" symbol gen_plugin_cpp
    emits for this bridge (get_<alias>_info, each bulk accessor, and
    send_<alias> for FIXED-only outputs).
    """
    # pylint: disable=too-many-locals,too-many-statements
    plugin_name = bridge["name"]
    class_name = pascal(plugin_name) + "Bridge"
    dll_name = f"plugin.{plugin_name}"

    lines = [
        f"// Copyright 2020-{bg_types.YEAR}, The Board of Trustees of the University of Illinois.",
        "// SPDX-License-Identifier: BSL-1.0",
        "// This file was generated by the Unity bridge generator -- do not edit directly.",
        "",
        "using System;",
        "using System.Runtime.InteropServices;",
        "",
        "namespace Illixr {",
        "",
        ]

    for dotted in _types_needed_in_order(bridge, type_defs_map, gen_dotted_names):
        lines.append(gen_csharp_struct(type_defs_map[dotted], dotted, gen_dotted_names))
        lines.append("")

    lines.append(f"public static class {class_name} {{")
    lines.append(f'    private const string DllName = "{dll_name}";')
    lines.append("")
    lines.append("    // NOTE: DllName above must match the plugin's built library base name")
    lines.append("    // (minus 'lib' prefix / extension / ILLIXR_BUILD_SUFFIX). If a build")
    lines.append("    // suffix is in play, override the resolved file name in Unity's Plugin")
    lines.append("    // Inspector per-platform rather than changing this string.")
    lines.append("")

    for inp in bridge["inputs"]:
        alias, input_type = inp["alias"], inp["type"]
        struct_name = _struct_name(input_type)
        td = type_defs_map.get(input_type)

        fn = f"illixr_unity_get_{alias}_info"
        lines.append('    [DllImport(DllName)]')
        lines.append(f"    private static extern int {fn}(out {struct_name} outInfo);")
        lines.append("")
        wrapper = f"TryGet{pascal(alias)}"
        lines.append(f"    /// Polls the latest '{alias}' sample. Returns false if none is available yet.")
        lines.append(f"    public static bool {wrapper}(out {struct_name} value) {{")
        lines.append(f"        return {fn}(out value) == 1;")
        lines.append("    }")
        lines.append("")

        if td is not None:
            for acc in bg_cg_unity_reader.gen_bulk_accessors(td, gen_dotted_names, type_defs_map):
                fn = f"illixr_unity_get_{alias}_{acc['name']}"
                try:
                    cs_params = _cs_params_for(acc["params"])
                except NotImplementedError as e:
                    lines.append(f"    // SKIPPED {fn}: {e}")
                    lines.append("")
                    continue
                lines.append('    [DllImport(DllName)]')
                lines.append(f"    private static extern int {fn}({cs_params});")
                lines.append("")
                wrapper = f"Get{pascal(alias)}{pascal(acc['name'])}"
                lines.append(f"    /// Call after {'TryGet' + pascal(alias)} in the same poll; outBuffer")
                lines.append("    /// must be pre-sized by the caller (e.g., from a *Count field).")
                lines.append(f"    public static int {wrapper}({cs_params}) {{")
                lines.append(f"        return {fn}(outBuffer, maxLen);")
                lines.append("    }")
                lines.append("")

    for out in bridge["outputs"]:
        alias, out_type = out["alias"], out["type"]
        struct_name = _struct_name(out_type)
        td = type_defs_map.get(out_type)
        _, variable = bg_cg_unity_reader.classify_fields(td) if td else ([], [])

        fn = f"illixr_unity_send_{alias}"
        lines.append('    [DllImport(DllName)]')
        lines.append(f"    private static extern void {fn}(ref {struct_name} info);")
        lines.append("")
        wrapper = f"Send{pascal(alias)}"
        if variable:
            lines.append(f"    // NOTE: '{out_type}' has variable-length fields "
                         f"({', '.join(n for n, _ in variable)}) that are not yet")
            lines.append("    // writable from Unity -- the corresponding C++ send_* only")
            lines.append("    // populates fixed fields for now. Values in those fields will")
            lines.append("    // be ignored until write-direction support lands.")
        lines.append(f"    public static void {wrapper}({struct_name} info) {{")
        lines.append(f"        {fn}(ref info);")
        lines.append("    }")
        lines.append("")

    lines.append("}")
    lines.append("")
    lines.append("} // namespace Illixr")
    lines.append("")
    return "\n".join(lines)
