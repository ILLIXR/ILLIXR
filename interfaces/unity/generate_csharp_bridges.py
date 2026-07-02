#!/usr/bin/env python3
# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""
generate_csharp_bridges.py

Entry point for the C#/Unity bridge generator, invoked by csharp_bridge.cmake.
Mirrors interfaces/python/generate_python_bridges.py's CLI shape so the two
generators plug into cmake identically:

  --write-profiles <csharp_profiles.yaml> <source_dir>
      Tier 1: expand csharp_profiles.yaml into interfaces/csharp/profiles/<name>.yaml

  --generate <profile_yaml> <build_dir> <source_dir>
      Tier 2: generate shared struct headers (interfaces/data/*.yaml -- the
      exact same headers the Python generator produces, reused verbatim if
      already generated for this build) plus, per bridge:
        <build_dir>/generated/unity_plugins/<bridge>/plugin.hpp
        <build_dir>/generated/unity_plugins/<bridge>/plugin.cpp
        <build_dir>/generated/unity_plugins/<bridge>/unity_wire_<type>.hpp
        <build_dir>/generated/unity_plugins/<bridge>/CMakeLists.txt
      Then prints cmake set() lines for CS_BRIDGE_NAMES / CS_BRIDGE_CMAKE_DIRS
      / CS_BRIDGE_COUNT, which csharp_bridge.cmake evaluates directly.

Staleness/MD5 caching (state.py's role on the Python side) is not yet
ported here - every --generate call currently regenerates everything.
This is correctness-first, not incremental; porting the caching is a
follow-up once the codegen surface stabilizes.
"""
import sys
from pathlib import Path

import yaml

from ..bridge_generator.helpers import dotted_to_path, cmake_list
from ..bridge_generator.load_bridges import load_bridge_descriptors, load_and_validate
from ..bridge_generator.codegen_struct import gen_struct_header, gen_boost_hpp, gen_boost_cpp
from ..bridge_generator.scan_illixr import build_illixr_serialization_map
from ..bridge_generator.codegen_unity_plugin import (
    gen_wire_header, gen_plugin_hpp, gen_plugin_cpp, gen_plugin_cmake,
    )
from ..bridge_generator.codegen_csharp_writer import gen_csharp_bridge_file, pascal
from ..bridge_generator.run import write_profile_yaml_files


def run_generate(bridge_profile_path: Path, build_dir: Path, source_dir: Path):
    """Generate the bridge files"""
    # pylint: disable=too-many-locals,too-many-statements
    bridges_dir = source_dir / "interfaces" / "csharp" / "bridges"
    data_dir = source_dir / "interfaces" / "data"

    with open(bridge_profile_path, encoding="utf-8") as f:
        bp_raw = yaml.safe_load(f)
    bridges_str = bp_raw.get("bridges", "")
    bridge_names = [b.strip() for b in str(bridges_str).split(",") if b.strip()]
    if not bridge_names:
        print(f'message(FATAL_ERROR "C# bridge profile \'{bridge_profile_path}\' has no bridges listed")')
        sys.exit(1)

    bridge_raws = load_bridge_descriptors(bridges_dir, bridge_names)
    validated_bridges, sorted_types, gen_dotted_set, type_defs_by_dotted = \
        load_and_validate(bridge_raws, data_dir, source_dir)

    # Shared struct headers -- identical output to the Python generator for
    # the same type YAML, written to the same location so both generators
    # can reuse each other's output within one build.
    struct_out_root = build_dir / "include" / "illixr" / "bridge"
    struct_out_root.mkdir(parents=True, exist_ok=True)
    illixr_ser_map = build_illixr_serialization_map(source_dir)
    cumulative_dotted: list[str] = []
    generated_serializers: set = set()
    for td in sorted_types:
        dotted = td["dotted"]
        rel_path = dotted_to_path(dotted)
        out_hdr = struct_out_root / f"{rel_path}.hpp"
        out_hdr.parent.mkdir(parents=True, exist_ok=True)
        out_hdr.write_text(gen_struct_header(td, cumulative_dotted))
        boost_hdr = struct_out_root / f"{rel_path}_ser.hpp"
        boost_hdr.write_text(gen_boost_hpp(td, out_hdr.name, cumulative_dotted, illixr_ser_map))
        boost_cpp = struct_out_root / f"{rel_path}_ser.cpp"
        boost_cpp.write_text(gen_boost_cpp(td, boost_hdr.name))
        generated_serializers.add(str(boost_hdr))
        generated_serializers.add(str(boost_cpp))
        cumulative_dotted.append(dotted)

    all_plugin_dirs = []
    all_bridge_names = []
    all_csharp_files = []
    csharp_out_root = build_dir / "generated" / "unity_csharp"
    csharp_out_root.mkdir(parents=True, exist_ok=True)
    for bd in validated_bridges:
        bridge_name = bd["name"]
        plugin_dir = build_dir / "generated" / "unity_plugins" / bridge_name
        plugin_dir.mkdir(parents=True, exist_ok=True)

        used_types = set(bd["type_names"])
        for type_name in used_types:
            td = type_defs_by_dotted.get(type_name)
            if td is None:
                continue
            wire_hdr = plugin_dir / f"unity_wire_{type_name.replace('.', '_')}.hpp"
            wire_hdr.write_text(gen_wire_header(type_name, td, gen_dotted_set))

        (plugin_dir / "plugin.hpp").write_text(
            gen_plugin_hpp(bd, gen_dotted_set, type_defs_by_dotted))
        (plugin_dir / "plugin.cpp").write_text(
            gen_plugin_cpp(bd, gen_dotted_set, type_defs_by_dotted))
        (plugin_dir / "CMakeLists.txt").write_text(
            gen_plugin_cmake(bd, gen_dotted_set, generated_serializers, plugin_dir,
                             build_dir / "include"))

        cs_file = csharp_out_root / f"{pascal(bridge_name)}Bridge.cs"
        cs_file.write_text(gen_csharp_bridge_file(bd, type_defs_by_dotted, gen_dotted_set))
        all_csharp_files.append(str(cs_file))

        all_plugin_dirs.append(str(plugin_dir))
        all_bridge_names.append(bridge_name)

    print(f'set(CS_BRIDGE_NAMES "{cmake_list(all_bridge_names)}")')
    print(f'set(CS_BRIDGE_CMAKE_DIRS "{cmake_list(all_plugin_dirs)}")')
    print(f'set(CS_BRIDGE_COUNT "{len(all_bridge_names)}")')
    print(f'set(CS_BRIDGE_CSHARP_FILES "{cmake_list(all_csharp_files)}")')


def main():
    """Parse arguments and run the generator"""
    if len(sys.argv) < 2:
        print('message(FATAL_ERROR "generate_csharp_bridges.py: no command given")')
        sys.exit(1)

    if sys.argv[1] == "--write-profiles":
        master_path, source_dir = Path(sys.argv[2]), Path(sys.argv[3])
        profiles_dir = source_dir / "interfaces" / "csharp" / "profiles"
        write_profile_yaml_files(master_path, profiles_dir)
    elif sys.argv[1] == "--generate":
        profile_path, build_dir, source_dir = Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
        run_generate(profile_path, build_dir, source_dir)
    else:
        print(f'message(FATAL_ERROR "generate_csharp_bridges.py: unknown command \'{sys.argv[1]}\'")')
        sys.exit(1)


if __name__ == "__main__":
    main()
