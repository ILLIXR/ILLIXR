# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""YAML schema validation for type and bridge descriptors."""
import re
from pathlib import Path

from . import types as bg_types
from . import helpers as bg_helpers


def validate_field(field_name, field_def, known_dotted_names):
    """
    known_dotted_names: set of dotted type names known in this context
    (e.g. {'camera_intrinsics', 'geometry.point'})
    """
    # pylint: disable=too-many-branches,too-many-statements
    if not isinstance(field_def, dict):
        raise bg_helpers.SchemaError(f"Field '{field_name}': definition must be a mapping")
    field_type = field_def.get("type")
    if field_type is None:
        raise bg_helpers.SchemaError(f"Field '{field_name}': missing required 'type' key")

    container = bg_types.CONTAINER_ALIASES.get(field_def.get("container", ""), None)
    if "container" in field_def and container is None:
        raise bg_helpers.SchemaError(
            f"Field '{field_name}': unknown container '{field_def['container']}'. "
            "Use: vector, list, dict, map")

    shape = field_def.get("shape",    None)
    channels = field_def.get("channels", None)

    if shape is not None:
        if container not in ["vector", "list", None]:
            raise bg_helpers.SchemaError(f"Field '{field_name}': 'shape' and 'container' are mutually exclusive for non-vector/list types.")

        # If shape contains string entries, this is a dynamic numpy array.
        # Infer container=vector so all downstream code works without change.
        if any(isinstance(e, str) for e in (shape or [])):
            container = "vector"

    if field_type in bg_types.IMAGE_TYPES:
        # Stored as std::vector<uint8_t> — type: image field.
        # Requires shape: [width_dim, height_dim, channels].
        # numpy output: (height, width, channels) matching entry_to_numpy.
        # Note: container may have been inferred as "vector" by the shape
        # inference above - check field_def directly for user-specified container.
        if field_def.get("container") is not None:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': type 'image' implies vector storage, "
                "do not specify container")
        if channels is not None:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': use shape: [w, h, ch] for type 'image', not 'channels'")
        if shape is None or not isinstance(shape, list) or len(shape) != 3:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': type 'image' requires shape: [width, height, channels]")
        w_dim, h_dim, ch = shape
        for dim_name, dim_val in [("width", w_dim), ("height", h_dim)]:
            if not isinstance(dim_val, (str, int)):
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': image {dim_name} must be a field name or positive integer")
            if isinstance(dim_val, int) and dim_val < 1:
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': image {dim_name} literal must be positive")
            if isinstance(dim_val, str) and not re.match(r'^[a-z][a-z0-9_]*$', dim_val):
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': image {dim_name} '{dim_val}' must be lowercase snake_case")
        if not isinstance(ch, int) or ch not in (1, 2, 3):
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': image channels must be 1, 2, or 3, got '{ch}'")
        # Store as vector<uint8_t>; image=shape tells generators to use
        # (H, W, ch) stride layout rather than plain vector handling.
        return dict(field_def, type="uint8", container="vector",
                    shape=shape, image=shape)

    if bg_types.is_mat(field_type):
        if shape is not None:
            raise bg_helpers.SchemaError(f"Field '{field_name}': 'shape' is not valid with mat_* types")
        if container is not None:
            raise bg_helpers.SchemaError(f"Field '{field_name}': 'container' is not valid with mat_* types")
        if channels is None:
            raise bg_helpers.SchemaError(f"Field '{field_name}': mat_* types require a 'channels' key (1-4)")
        if not isinstance(channels, int) or not 1 <= channels <= 4:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': 'channels' must be an integer between 1 and 4")
        return dict(field_def, type=field_type, container=None, shape=None)

    # Bridge-defined struct (dotted name)
    if field_type in known_dotted_names:
        if channels is not None:
            raise bg_helpers.SchemaError(f"Field '{field_name}': 'channels' is only valid for mat_* types")
        if container is not None and container != "vector":
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': bridge-defined struct types only support "
                "container: vector")
        if shape is not None:
            if not isinstance(shape, list) or len(shape) != 1:
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': bridge-defined struct types only support 1D shape")
            if not isinstance(shape[0], int) or shape[0] < 1:
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': shape dimensions must be positive integers")
        return dict(field_def, container=container, shape=shape)

    if not bg_types.is_scalar(field_type):
        raise bg_helpers.SchemaError(
            f"Field '{field_name}': unknown type '{field_type}'. Must be a scalar type, "
            "a mat_* type, or a bridge-defined struct dotted name "
            "(e.g. 'camera_intrinsics' or 'geometry.camera_intrinsics')")

    if channels is not None:
        raise bg_helpers.SchemaError(f"Field '{field_name}': 'channels' is only valid for mat_* types")

    ctype = bg_types.canonical_type(field_type)

    if shape is not None:
        # 'shape' on a vector field may contain field name strings and/or int
        # literals, defining a dynamic multidimensional numpy array.
        # 'shape' on a plain (non-vector) scalar field may only contain ints
        # and defines a fixed compile-time array (validated above).
        # Both are stored under the same "shape" key; the vector+string case
        # is distinguished at code-generation time by checking the container.
        if container == "vector":
            if ctype in bg_types.SHAPE_FORBIDDEN_SCALAR or ctype == "bool":
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': dynamic 'shape' is not valid with type '{field_type}'")
            if not isinstance(shape, list) or not 1 <= len(shape) <= 3:
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': 'shape' must be a list of 1-3 entries")
            for sf in shape:
                if isinstance(sf, int):
                    if sf < 1:
                        raise bg_helpers.SchemaError(
                            f"Field '{field_name}': shape literal '{sf}' "
                            "must be a positive integer")
                elif not isinstance(sf, str) or not re.match(r"^[a-zA-Z][a-zA-Z0-9_]*$", sf):
                    raise bg_helpers.SchemaError(f"Field '{field_name}': shape entry '{sf}' must be "
                                                 "a field name or a positive integer literal")

        else:
            # Fixed compile-time array: shape must be a list of 1 or 2 positive ints.
            # Dynamic numpy shape (container: vector) is validated separately below.
            if ctype in bg_types.SHAPE_FORBIDDEN_SCALAR:
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': type '{field_type}' cannot be used with 'shape'")
            if not isinstance(shape, list) or len(shape) not in (1, 2, 3):
                raise bg_helpers.SchemaError(
                    f"Field '{field_name}': 'shape' must be a list of 1, 2, or 3 positive integers")
            for dim in shape:
                if not isinstance(dim, int) or dim < 1:
                    raise bg_helpers.SchemaError(
                        f"Field '{field_name}': shape dimensions must be positive integers")

    elif container is not None:
        if container == "vector" and ctype in bg_types.VECTOR_FORBIDDEN:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': vector<bool> is not allowed; "
                "use vector<uint8> instead")
        if container == "dict" and ctype in bg_types.DICT_FORBIDDEN_VALUE_TYPES:
            raise bg_helpers.SchemaError(
                f"Field '{field_name}': dict with value type '{field_type}' is not allowed")

    return dict(field_def, type=ctype, container=container, shape=shape, image=None)


def validate_type_yaml(data, path, data_dir, all_dotted_names):
    """
    path: absolute path to the YAML file
    data_dir: absolute path to interfaces/data/
    all_dotted_names: set of all known dotted type names in this profile

    Derives the dotted name from the file's path relative to data_dir.
    """
    # pylint: disable=too-many-branches,too-many-locals
    rel = Path(path).relative_to(data_dir)
    # rel = geometry/camera_intrinsics.yaml -> dotted = geometry.camera_intrinsics
    parts = list(rel.with_suffix("").parts)
    dotted = ".".join(parts)

    # Validate each segment is lowercase snake_case
    for part in parts:
        if not re.match(r'^[a-z][a-z0-9_]*$', part):
            raise bg_helpers.SchemaError(
                f"{path}: path segment '{part}' must be lowercase snake_case")

    if "name" in data:
        raise bg_helpers.SchemaError(
            f"{path}: type YAML files must not contain a 'name' field; "
            f"the struct name is derived from the filename ('{dotted}')")

    fields_raw = data.get("fields")
    if not fields_raw or not isinstance(fields_raw, dict):
        raise bg_helpers.SchemaError(f"{path}: 'fields' must be a non-empty mapping")

    peers = set(all_dotted_names) - {dotted}
    fields = {}
    for field_name, field_def in fields_raw.items():
        if not re.match(r'^[a-z][a-z0-9_]*$', field_name):
            raise bg_helpers.SchemaError(
                f"{path}: field name '{field_name}' must be lowercase snake_case")
        fields[field_name] = validate_field(field_name, field_def, peers)

    # Process 'image' entries: synthesize missing width/height dimension fields.
    for field_name, field_def in list(fields.items()):
        image = field_def.get("image")
        if not image:
            continue
        w_dim, h_dim, _ = image
        for dim_val in (w_dim, h_dim):
            if isinstance(dim_val, str) and dim_val not in fields:
                fields[dim_val] = {"type": "int", "container": None,
                                   "shape": None, "image": None}
            elif isinstance(dim_val, str):
                sf_type = fields[dim_val].get("type", "")
                sf_cpp = bg_types.cpp_scalar(bg_types.canonical_type(sf_type))
                _int_cpp_types = {"int8_t", "int16_t", "int32_t", "int64_t",
                                  "uint8_t", "uint16_t", "uint32_t", "uint64_t"}
                if sf_cpp not in _int_cpp_types:
                    raise bg_helpers.SchemaError(
                        f"{path}: field '{field_name}' image width/height field '{dim_val}' "
                        f"must be an integer field, got '{sf_type}'")

    # Process dynamic shape entries (shape on a vector field containing strings):
    # synthesize any missing dimension fields as int32_t, or validate that
    # explicitly declared dimension fields are integers.
    _int_cpp_types = {"int8_t", "int16_t", "int32_t", "int64_t",
                      "uint8_t", "uint16_t", "uint32_t", "uint64_t"}
    for field_name, field_def in list(fields.items()):
        if field_def.get("container") != "vector":
            continue
        sf_names = [e for e in (field_def.get("shape") or []) if isinstance(e, str)]
        for sf in sf_names:
            if sf not in fields:
                # Synthesize the dimension field as int32_t
                fields[sf] = {"type": "int", "container": None, "shape": None}
            else:
                sf_type = fields[sf].get("type", "")
                sf_cpp = bg_types.cpp_scalar(bg_types.canonical_type(sf_type))
                if sf_cpp not in _int_cpp_types:
                    raise bg_helpers.SchemaError(
                        f"{path}: field '{field_name}' shape['{sf}'] must be "
                        f"an integer field, got '{sf_type}' (C++ type '{sf_cpp}')")

    return {"dotted": dotted, "fields": fields}


def normalize_network(val):
    """Normalize a ``network:`` YAML value to a canonical lower-case string.

    Accepts ``"tcp"``, ``"TCP"``, ``"udp"``, ``"UDP"``, ``True``, ``False``,
    ``"true"``, ``"false"``.  ``True`` / ``"true"`` maps to ``"tcp"``;
    ``False`` / ``"false"`` maps to ``"none"``.

    Args:
        val: Raw value from the bridge YAML ``network:`` key.

    Returns:
        str: One of ``"tcp"``, ``"udp"``, or ``"none"``.

    Raises:
        bg_helpers.SchemaError: If ``val`` is not a recognized network transport value.
    """
    if val is None or val is False or str(val).lower() == "false":
        return "none"
    v = str(val).lower()
    if v == "tcp":
        return "tcp"
    if v == "udp":
        return "udp"
    if v is True or v == "true":
        return "any"
    raise bg_helpers.SchemaError(
        f"Invalid network value '{val}'. Use: tcp, TCP, udp, UDP, true, false")


def validate_dotted_name(name: str, context: str):
    """Validate that a dotted type name has only lowercase snake_case segments."""
    for part in name.split("."):
        if not re.match(r'^[a-z][a-z0-9_]*$', part):
            raise bg_helpers.SchemaError(
                f"{context}: '{name}' contains invalid segment '{part}'; "
                "each segment must be lowercase snake_case")


def validate_bridge_yaml(data, path, all_type_names):
    """
    Validate a bridge descriptor YAML file.

    Bridge name is derived from the filename stem - no 'name:' key needed.
    Types are discovered automatically from topic 'type:' fields - no
    'types:' key needed. 'script:' is optional; the runtime env var
    ILLIXR_<NAME>_SCRIPT overrides whatever default is compiled in.
    'alias:' on each topic entry is optional; defaults to the topic name.

    all_type_names: set of all valid dotted type names discoverable in
    interfaces/data/ for this profile, plus _bg_types.KNOWN_ILLIXR_TYPES keys.
    """
    # pylint: disable=too-many-branches,too-many-locals
    name = Path(path).stem
    if not re.match(r"^[a-z][a-z0-9_]*$", name):
        raise bg_helpers.SchemaError(
            f"{path}: filename stem '{name}' must be lowercase snake_case")

    if "name" in data:
        raise bg_helpers.SchemaError(
            f"{path}: bridge YAML files must not contain a 'name' key; "
            f"the bridge name is derived from the filename ('{name}')")

    if "types" in data:
        raise bg_helpers.SchemaError(
            f"{path}: bridge YAML files must not contain a 'types' key; "
            "required types are discovered automatically from the 'type:' "
            "fields of each input and output entry")

    # script: is optional - ILLIXR_<NAME>_SCRIPT env var overrides at runtime
    script = data.get("script", "")

    inputs = data.get("inputs", [])
    outputs = data.get("outputs", [])
    if not isinstance(inputs, list):
        raise bg_helpers.SchemaError(f"{path}: 'inputs' must be a list")
    if not isinstance(outputs, list):
        raise bg_helpers.SchemaError(f"{path}: 'outputs' must be a list")

    validated_inputs = []
    for i, inp in enumerate(inputs):
        topic = inp.get("topic")
        input_type = inp.get("type")
        alias = inp.get("alias") or topic  # default alias to topic name
        if not topic:
            raise bg_helpers.SchemaError(f"{path}: input[{i}] missing 'topic'")
        if not input_type:
            raise bg_helpers.SchemaError(f"{path}: input[{i}] missing 'type'")
        if not re.match(r"^[a-z][a-z0-9_]*$", alias):
            raise bg_helpers.SchemaError(
                f"{path}: input[{i}] alias '{alias}' must be lowercase snake_case")
        if input_type not in all_type_names:
            raise bg_helpers.SchemaError(
                f"{path}: input[{i}] type '{input_type}' was not found. "
                f"Check that interfaces/data/{bg_helpers.dotted_to_path(input_type)}.yaml exists. "
                "Use dotted notation for types in subdirectories "
                "(e.g. 'semantic_xr.semantic_data')")
        validated_inputs.append({"topic": topic, "type": input_type, "alias": alias})

    validated_outputs = []
    for i, out in enumerate(outputs):
        topic = out.get("topic")
        out_type = out.get("type")
        alias = out.get("alias") or topic  # default alias to topic name
        network = normalize_network(out.get("network", False))
        if not topic:
            raise bg_helpers.SchemaError(f"{path}: output[{i}] missing 'topic'")
        if not out_type:
            raise bg_helpers.SchemaError(f"{path}: output[{i}] missing 'type'")
        if not re.match(r"^[a-z][a-z0-9_]*$", alias):
            raise bg_helpers.SchemaError(
                f"{path}: output[{i}] alias '{alias}' must be lowercase snake_case")
        if out_type not in all_type_names:
            raise bg_helpers.SchemaError(
                f"{path}: output[{i}] type '{out_type}' was not found. "
                f"Check that interfaces/data/{bg_helpers.dotted_to_path(out_type)}.yaml exists. "
                "Use dotted notation for types in subdirectories "
                "(e.g. 'semantic_xr.query_response')")
        validated_outputs.append(
            {"topic": topic, "type": out_type, "alias": alias, "network": network})

    # Collect the bridge-defined dotted type names used by this bridge
    used_gen_types = set()
    for entry in validated_inputs + validated_outputs:
        if entry["type"] not in bg_types.KNOWN_ILLIXR_TYPES:
            used_gen_types.add(entry["type"])

    return {
        "name": name,
        "script": script,
        "type_names": sorted(used_gen_types),
        "inputs": validated_inputs,
        "outputs": validated_outputs,
        }


# ---------------------------------------------------------------------------
# Topological sort
# ---------------------------------------------------------------------------
