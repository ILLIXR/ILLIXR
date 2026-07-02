# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Shared bridge-descriptor and type-yaml loading, used by both the Python and
C# bridge generators, so this logic exists in exactly one place.

Mirrors Pass 1 / Pass 2 of run.py's run_generate(), parameterized on
bridges_dir so it works for interfaces/python/bridges/ or
interfaces/csharp/bridges/ identically -- both reference the same
interfaces/data/ type YAMLs.
"""
import re
import sys
from pathlib import Path

import yaml

from . import types as bg_types
from . import helpers as bg_helpers
from . import schema as bg_schema
from . import scan_illixr as bg_scan_illixr


def _fatal(msg: str):
    print(f'message(FATAL_ERROR "{msg}")')
    sys.exit(1)


def load_bridge_descriptors(bridges_dir: Path, bridge_names: list[str]):
    """Read and sanity-check the raw YAML for each named bridge descriptor."""
    bridge_raws = []
    seen_names = {}
    for bridge_name in bridge_names:
        if "." in bridge_name:
            _fatal(
                f"Bridge name '{bridge_name}' contains a dot, which is not allowed. "
                "The bridges: list must contain bridge plugin names, not dotted type names.")
        if not re.match(r'^[a-z][a-z0-9_]*$', bridge_name):
            _fatal(f"Bridge name '{bridge_name}' must be lowercase snake_case (no dots).")
        bridge_path = bridges_dir / f"{bridge_name}.yaml"
        if not bridge_path.exists():
            _fatal(f"Bridge descriptor not found: {bridge_path}.")
        try:
            with open(bridge_path, encoding="utf-8") as bf:
                bridge_raw = yaml.safe_load(bf)
        except Exception as e:
            _fatal(f"Cannot read bridge descriptor '{bridge_path}': {e}")
        declared = bridge_path.stem
        if declared in seen_names:
            _fatal(f"Duplicate bridge name '{declared}': both '{seen_names[declared]}' "
                   f"and '{bridge_path}' have the same stem.")
        seen_names[declared] = str(bridge_path)
        bridge_raws.append((bridge_path, bridge_raw))
    return bridge_raws


def load_and_validate(bridge_raws, data_dir: Path, source_dir: Path):
    """
    Runs Pass 1 (type discovery + loading + validation + topo sort) and
    Pass 2 (bridge validation) against an already-loaded set of bridge YAMLs.

    Returns:
      validated_bridges   -- list of bridge dicts (name/inputs/outputs/type_names)
      sorted_types        -- topologically sorted list of type_defs
      gen_dotted_set      -- set of dotted names for bridge-defined types
      type_defs_by_dotted -- dotted/bare name -> type_def, including merged
                               ILLIXR system types from interfaces/system/
    """
    # pylint: disable=too-many-locals
    bg_types.ensure_known_illixr_types(source_dir)

    system_yaml_dir = source_dir / "interfaces" / "system" / "illixr" / "data_format"
    system_tds = bg_scan_illixr.load_system_yamls(system_yaml_dir)
    system_bare_names = {dotted.split(".")[-1] for dotted in system_tds}

    candidate_dotted: set = set()
    for _, bridge_raw in bridge_raws:
        for section in ("inputs", "outputs"):
            for entry in bridge_raw.get(section, []):
                t = entry.get("type", "")
                if t and t not in bg_types.KNOWN_ILLIXR_TYPES:
                    candidate_dotted.add(t)

    all_dotted_yaml: set = set()
    type_yaml_raw: dict = {}
    type_yaml_paths: dict = {}

    def load_type_yaml(type_name_: str) -> None:
        if (type_name_ in all_dotted_yaml
                or type_name_ in bg_types.KNOWN_ILLIXR_TYPES
                or type_name_ in bg_types.IMAGE_TYPES
                or type_name_ in system_bare_names):
            return
        rel = bg_helpers.dotted_to_path(type_name_)
        type_path = data_dir / f"{rel}.yaml"
        if not type_path.exists():
            _fatal(f"Type yaml not found for '{type_name_}': {type_path}.")
        try:
            with open(type_path, encoding='utf-8') as fl:
                type_raw_ = yaml.safe_load(fl)
        except Exception as e_:
            _fatal(f"Cannot read type yaml '{type_path}': {e_}")
        all_dotted_yaml.add(type_name_)
        type_yaml_raw[type_name_] = type_raw_
        type_yaml_paths[type_name_] = type_path
        for field_def in type_raw_.get("fields", {}).values():
            field_type = field_def.get("type", "")
            if field_type and not bg_types.is_scalar(field_type) and not bg_types.is_mat(field_type):
                load_type_yaml(field_type)

    for type_name in sorted(candidate_dotted):
        load_type_yaml(type_name)

    validated_types = []
    for type_name, type_raw in type_yaml_raw.items():
        try:
            td = bg_schema.validate_type_yaml(type_raw, type_yaml_paths[type_name], data_dir, all_dotted_yaml)
            validated_types.append(td)
        except bg_helpers.SchemaError as e:
            _fatal(f"Type schema error: {e}")

    sorted_types = bg_helpers. topo_sort(validated_types)
    gen_dotted_set = {td["dotted"] for td in sorted_types}
    all_types = gen_dotted_set | set(bg_types.KNOWN_ILLIXR_TYPES.keys()) | system_bare_names

    validated_bridges = []
    for bridge_path, bridge_raw in bridge_raws:
        try:
            bd = bg_schema.validate_bridge_yaml(bridge_raw, bridge_path, all_types)
            validated_bridges.append(bd)
        except bg_helpers.SchemaError as e:
            _fatal(f"Bridge schema error in '{bridge_path}': {e}")

    type_defs_by_dotted: dict = {td["dotted"]: td for td in sorted_types}
    for bare_name, td in {d.split(".")[-1]: t for d, t in system_tds.items()}.items():
        type_defs_by_dotted.setdefault(bare_name, td)
    for dotted, td in system_tds.items():
        type_defs_by_dotted.setdefault(dotted, td)

    return validated_bridges, sorted_types, gen_dotted_set, type_defs_by_dotted
