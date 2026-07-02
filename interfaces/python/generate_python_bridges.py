#!/usr/bin/env python3
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""
generate_python_bridges.py

Entry point for the ILLIXR Python bridge generator.
All generation logic lives in bridge_generator/.
"""
import argparse
import sys
from pathlib import Path

from ..bridge_generator.helpers import SchemaError
from ..bridge_generator.run import run_generate, write_profile_yaml_files


def main():
    """Parse command-line arguments and dispatch to the appropriate generation tier.

    Supports two modes of operation, typically called in sequence from CMake:

    **Tier 1 - write profile YAML files** (``--write-profiles``):

        generate_python_bridges.py --write-profiles \
            <master_profile.yaml> <profiles_output_dir> <source_dir>

    Reads the main profile YAML and writes one per-profile YAML file into
    ``profiles_output_dir``.  Called once during initial CMake configuration.

    **Tier 2 - generate bridge sources** (``--generate``):

        generate_python_bridges.py --generate \
            <profile.yaml> <build_dir> <source_dir>

    Runs the full generation pipeline for one profile: validates type and
    bridge YAML files, determines which bridges are stale, regenerates C++
    struct headers, serialization headers, pybind11 bindings, and plugin
    CMakeLists/hpp/cpp files, then prints CMake variables listing all
    generated artifacts.

    Raises:
        SystemExit: On argument errors or generation failures.
    """
    ap = argparse.ArgumentParser(
        description="ILLIXR Python bridge generator")
    ap.add_argument("--write-profiles", action="store_true",
                    help="Tier 1: write per-profile YAML files")
    ap.add_argument("--generate", action="store_true",
                    help="Tier 2: generate struct headers and plugin sources")
    ap.add_argument("args", nargs="*")
    opts = ap.parse_args()

    if not opts.write_profiles and not opts.generate:
        print('message(FATAL_ERROR "generate_python_bridges.py: '
              'specify --write-profiles and/or --generate")')
        sys.exit(1)

    positional = opts.args

    if opts.write_profiles and opts.generate:
        if len(positional) != 4:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles --generate requires 4 positional args: '
                  'master_profile bridge_profile build_dir source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        bridge_profile = Path(positional[1]).resolve()
        build_dir = Path(positional[2]).resolve()
        source_dir = Path(positional[3]).resolve()
        profiles_dir = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yaml_files(master_profile, profiles_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)
        run_generate(bridge_profile, build_dir, source_dir)

    elif opts.write_profiles:
        if len(positional) != 2:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--write-profiles requires 2 positional args: '
                  'master_profile source_dir")')
            sys.exit(1)
        master_profile = Path(positional[0]).resolve()
        source_dir = Path(positional[1]).resolve()
        profiles_dir = source_dir / "interfaces" / "python" / "profiles"
        profiles_dir.mkdir(parents=True, exist_ok=True)
        try:
            write_profile_yaml_files(master_profile, profiles_dir)
        except SchemaError as e:
            print(f'message(FATAL_ERROR "Master profile error: {e}")')
            sys.exit(1)

    else:
        if len(positional) != 3:
            print('message(FATAL_ERROR "generate_python_bridges.py '
                  '--generate requires 3 positional args: '
                  'bridge_profile build_dir source_dir")')
            sys.exit(1)
        bridge_profile = Path(positional[0]).resolve()
        build_dir = Path(positional[1]).resolve()
        source_dir = Path(positional[2]).resolve()
        run_generate(bridge_profile, build_dir, source_dir)


if __name__ == "__main__":
    main()
