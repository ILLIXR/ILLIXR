# pylint: disable=line-too-long
# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""Top-level generation orchestration."""
from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print('message(FATAL_ERROR "bridge_generator requires PyYAML: '
          'pip install pyyaml")')
    yaml = None
    sys.exit(1)

from . import types as bg_types
from . import helpers as bg_helpers
from . import schema as bg_schema
from . import scan_illixr as bg_scan_illixr
from . import codegen_struct as bg_cg_struct
from . import codegen_python_bindings as bg_cg_bindings
from . import codegen_python_plugin as bg_cg_plugin
from . import state as bg_state


def write_profile_yaml_files(master_path, profiles_dir):
    """Write per-profile YAML files from the main profile descriptor.

    Reads the main profile YAML (which lists profiles and the bridges each
    activates) and writes one ``<profile_name>.yaml`` file into ``profiles_dir``
    for each profile.  These per-profile files are the entry point for
    ``run_generate``.

    Args:
        master_path (Path): Path to the master ``python_profiles.yaml`` file.
        profiles_dir (Path): Directory where per-profile YAML files are written.

    Returns:
        None
    """
    with open(master_path, encoding='utf-8') as f:
        raw = yaml.safe_load(f)

    if not isinstance(raw, dict):
        raise bg_helpers.SchemaError(
            f"{master_path}: top level must be a mapping of profile names")

    for profile_name, profile_data in raw.items():
        if not isinstance(profile_data, dict):
            raise bg_helpers.SchemaError(
                f"{master_path}: profile '{profile_name}' must be a mapping")
        bridges_val = profile_data.get("bridges", "")
        if not bridges_val:
            raise bg_helpers.SchemaError(
                f"{master_path}: profile '{profile_name}' missing 'bridges' key")
        bridges_str = re.sub(r'\s*,\s*', ',', str(bridges_val).strip())
        out_path = Path(profiles_dir) / f"{profile_name}.yaml"
        out_path.write_text(
            "# This file was auto-generated from python_profiles.yaml"
            " -- do not edit directly.\n"
            f"bridges: {bridges_str}\n"
            )


# ---------------------------------------------------------------------------
# Tier 2 - struct headers + plugin sources
# ---------------------------------------------------------------------------

def run_generate(bridge_profile_path, build_dir, source_dir):
    """Run the full bridge generation pipeline for one profile.

    Orchestrates all generation passes:

    1. Populate ``KNOWN_ILLIXR_TYPES`` via libclang scan.
    2. Load system type YAMLs from ``interfaces/system/``.
    3. Load and validate all type YAML files referenced by the bridge descriptors.
    4. Validate all bridge descriptors.
    5. Check serialization headers for ILLIXR types on network outputs.
    6. Determine which bridges are stale (via MD5 state file).
    7. Regenerate stale bridges: struct headers, serialization headers,
       pybind11 bindings, plugin CMakeLists / hpp / cpp.
    8. Emit CMake variables listing all generated files.
    9. Update the staleness state file.

    Args:
        bridge_profile_path (Path): Path to a per-profile YAML file listing
                                     active bridge names.
        build_dir (Path): CMake build directory; generated files are written
                           into ``build_dir/generated/`` and the state file
                           into ``build_dir/.py_bridge_state.json``.
        source_dir (Path): ILLIXR repository root (contains ``include/`` and
                            ``interfaces/``).

    Returns:
        None: All output is written to disk and CMake variables are printed
              to stdout for ``execute_process`` to capture.
    """
    # pylint: disable=too-many-locals,too-many-branches,too-many-statements
    bridges_dir = source_dir / "interfaces" / "python" / "bridges"
    data_dir = source_dir / "interfaces" / "data"

    # Read bridge profile
    try:
        with open(bridge_profile_path, encoding='utf-8') as f:
            bp_raw = yaml.safe_load(f)
    except (FileNotFoundError, PermissionError, ValueError, OSError) as e:
        print(f'message(FATAL_ERROR "Cannot read bridge profile '
              f'\'{bridge_profile_path}\': {e}")')
        sys.exit(1)

    bridges_str = bp_raw.get("bridges", "")
    bridge_names = [b.strip() for b in str(bridges_str).split(",") if b.strip()]
    if not bridge_names:
        print(f'message(FATAL_ERROR "Bridge profile \'{bridge_profile_path}\' '
              f'has no bridges listed")')
        sys.exit(1)

    # Read and validate bridge descriptors; check for duplicates
    bridge_raws = []
    seen_names = {}
    for bridge_name in bridge_names:
        # Bridge names are plain snake_case plugin names -- never dotted.
        # A dot here almost always means the user put a type dotted name
        # (e.g.,, semantic_xr.semantic_data) in the profile bridges: list
        # instead of in the bridge descriptor types: list.
        if "." in bridge_name:
            error_msg = (
                f"Bridge name '{bridge_name}' in profile '{bridge_profile_path}' "
                "contains a dot, which is not allowed. "
                "The bridges: list must contain bridge plugin names "
                "(e.g., semantic_xr), not dotted type names "
                "(e.g., semantic_xr.semantic_data). "
                "Dotted type names belong in the bridge descriptor "
                "types: list, not in the profile."
                )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        if not re.match(r'^[a-z][a-z0-9_]*$', bridge_name):
            error_msg = f"Bridge name '{bridge_name}' must be lowercase snake_case (no dots)."
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        bridge_path = bridges_dir / f"{bridge_name}.yaml"
        if not bridge_path.exists():
            error_msg = (
                f"Bridge descriptor not found: {bridge_path}. "
                "The bridges: list must contain plugin names matching "
                "files in interfaces/python/bridges/. "
                "Dotted type names (e.g., semantic_xr.semantic_data) "
                "belong in the bridge descriptor types: list."
                )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        try:
            with open(bridge_path, encoding='utf-8') as bridge_file:
                bridge_raw = yaml.safe_load(bridge_file)
        except (FileNotFoundError, PermissionError, ValueError, OSError) as e:
            error_msg = f"Cannot read bridge descriptor '{bridge_path}': {e}"
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        # Bridge name is the YAML file's stem - no 'name:' key in the file.
        declared = bridge_path.stem
        if declared in seen_names:
            error_msg = (
                f"Duplicate bridge name '{declared}' in profile "
                f"'{bridge_profile_path}': both '{seen_names[declared]}' "
                f"and '{bridge_path}' have the same stem."
                )
            print(f'message(FATAL_ERROR "{error_msg}")')
            sys.exit(1)
        seen_names[declared] = str(bridge_path)
        bridge_raws.append((bridge_path, bridge_raw))

    # Populate _bg_types.KNOWN_ILLIXR_TYPES before Pass 1 so that load_type_yaml
    # correctly skips YAML loading for ILLIXR system types.
    bg_types.ensure_known_illixr_types(source_dir)

    # Load system type YAMLs early so load_type_yaml can skip bare
    # ILLIXR type names (e.g., "combined_pose") without needing a
    # YAML file under interfaces/data/.
    _early_system_yaml_dir = (source_dir / "interfaces" / "system"
                              / "illixr" / "data_format")
    _early_system_tds = bg_scan_illixr.load_system_yamls(_early_system_yaml_dir)
    _system_bare_names = {
        dotted.split(".")[-1] for dotted in _early_system_tds
        }

    # Pass 1: collect candidate dotted type names from all bridge topic entries.
    # Types are now discovered from 'type:' fields directly - no 'types:' key.
    candidate_dotted: set[str] = set()
    for bridge_path, bridge_raw in bridge_raws:
        for section in ("inputs", "outputs"):
            for entry in bridge_raw.get(section, []):
                type_name = entry.get("type", "")
                if type_name and type_name not in bg_types.KNOWN_ILLIXR_TYPES:
                    candidate_dotted.add(type_name)

    # Load and validate all referenced type YAML files.
    # Build the complete name set before validating any individual type
    # so that cross-struct field references within the profile resolve.
    all_dotted_yaml: set[str] = set()
    type_yaml_raw: dict[str, dict] = {}
    type_yaml_paths: dict[str, Path] = {}

    def load_type_yaml(type_name_: str) -> None:
        """Recursively load a type YAML and all bridge types it references.

        Resolves a dotted type name to a YAML file under ``data_dir``, parses
        and validates it, then recursively loads any nested bridge types found
        in its fields.  Skips types that are already loaded, ILLIXR system types,
        image built-ins, and bare names from the system YAMLs.

        Args:
            type_name_ (str): Dotted type name to load, e.g.,
                               ``"semantic_xr.semantic_data"``.

        Returns:
            None: Loaded type definitions are accumulated in the enclosing
                  ``all_dotted_yaml`` dict.

        Raises:
            SystemExit: If the YAML file is not found or fails validation
                        (prints a CMake ``FATAL_ERROR`` message first).
        """
        if (type_name_ in all_dotted_yaml or type_name_ in bg_types.KNOWN_ILLIXR_TYPES
            or type_name_ in bg_types.IMAGE_TYPES or type_name_ in _system_bare_names):
            return
        rel = bg_helpers.dotted_to_path(type_name_)
        type_path = data_dir / f"{rel}.yaml"
        if not type_path.exists():
            error_msg_ = (
                f"Type YAML not found for '{type_name_}': {type_path}. "
                "Check that the file exists and that the dotted name "
                "matches its path under interfaces/data/."
                )
            print(f'message(FATAL_ERROR "{error_msg_}")')
            sys.exit(1)
        try:
            with open(type_path, encoding='utf-8') as fl:
                type_raw_ = yaml.safe_load(fl)
        except (FileNotFoundError, PermissionError, ValueError, OSError) as e_:
            error_msg_ = f"Cannot read type YAML '{type_path}': {e_}"
            print(f'message(FATAL_ERROR "{error_msg_}")')
            sys.exit(1)
        all_dotted_yaml.add(type_name_)
        type_yaml_raw[type_name_] = type_raw_
        type_yaml_paths[type_name_] = type_path
        # Recursively load bridge-defined struct types referenced in fields
        for field_def in type_raw_.get("fields", {}).values():
            field_type = field_def.get("type", "")
            if field_type and not bg_types.is_scalar(field_type) and not bg_types.is_mat(field_type):
                load_type_yaml(field_type)

    for type_name in sorted(candidate_dotted):
        load_type_yaml(type_name)

    # Validate all loaded type YAMLs now that the full name set is known
    validated_types = []
    for type_name, type_raw in type_yaml_raw.items():
        try:
            td = bg_schema.validate_type_yaml(type_raw, type_yaml_paths[type_name],
                                              data_dir, all_dotted_yaml)
            validated_types.append(td)
        except bg_helpers.SchemaError as e:
            print(f'message(FATAL_ERROR "Type schema error: {e}")')
            sys.exit(1)

    sorted_types = bg_helpers.topo_sort(validated_types)
    gen_dotted_set = {td["dotted"] for td in sorted_types}
    all_types = (gen_dotted_set
                 | set(bg_types.KNOWN_ILLIXR_TYPES.keys())
                 | _system_bare_names)

    # Pass 2: validate bridge descriptors now that all type names are known
    validated_bridges = []
    for bridge_path, bridge_raw in bridge_raws:
        try:
            bd = bg_schema.validate_bridge_yaml(bridge_raw, bridge_path, all_types)
            validated_bridges.append(bd)
        except bg_helpers.SchemaError as e:
            print(f'message(FATAL_ERROR "Bridge schema error in \'{bridge_path}\': {e}")')
            sys.exit(1)

    # Pass 3: for any bridge output that uses network transport, verify that
    # every ILLIXR system type referenced (directly or transitively) has a
    # serialization header in data_format/serialization/.  Bridge-defined types
    # always have generated serialization, so only ILLIXR types need checking.
    # Build the ser map once if any network output uses an ILLIXR system type.
    _ser_map_for_check: dict | None = None
    for bd in validated_bridges:
        for out in bd["outputs"]:
            if out["network"] == "none":
                continue
            out_type = out["type"]
            if out_type not in bg_types.KNOWN_ILLIXR_TYPES:
                continue  # bridge-defined type - serialization is generated
            # ILLIXR system type on a network topic - check ser header exists
            if _ser_map_for_check is None:
                _ser_map_for_check = bg_scan_illixr.build_illixr_serialization_map(source_dir)
            if out_type not in _ser_map_for_check:
                print(
                    f'message(FATAL_ERROR "Bridge \'{bd["name"]}\' output topic \'{out["topic"]}\' '

                    f'uses ILLIXR type \'{out_type}\' over network transport, but no serialization '

                    f'header was found for it in data_format/serialization/. '

                    f'Add a boost::serialization::serialize() specialisation for '

                    f'ILLIXR::{out_type} to a header in that directory.")'

                    )
                sys.exit(1)

    # Determine which bridges need regeneration using the JSON state file.
    # This is fully self-contained in Python with no cmake cache variables.
    def _progress(msg: str) -> None:
        """Print a progress message to stderr (visible in CMake output).

        Args:
            msg (str): Message text.

        Returns:
            None
        """
        print(msg, file=sys.stderr, flush=True)

    state = bg_state.load_state(build_dir)

    # Build dependency walker used by staleness check and emit below
    type_defs_by_dotted_pre = {td["dotted"]: td for td in sorted_types}

    # Load auto-generated system type YAMLs (interfaces/system/) and
    # merge into type_defs_by_dotted_pre so that reader/writer code generation
    # can emit field-by-field dict conversion for ILLIXR system types.
    # Bridge-defined types take precedence if there is a name collision.
    system_yaml_dir = (source_dir / "interfaces" / "system"
                       / "illixr" / "data_format")
    system_tds = bg_scan_illixr.load_system_yamls(system_yaml_dir)
    # Map bare struct name -> dotted for KNOWN_ILLIXR_TYPES lookup
    _system_by_bare = {dotted.split(".")[-1]: td
                       for dotted, td in system_tds.items()}
    # Merge: bare ILLIXR type names map to their system td
    for bare_name, td in _system_by_bare.items():
        if bare_name not in type_defs_by_dotted_pre:
            type_defs_by_dotted_pre[bare_name] = td
    # Also add by full dotted name for field-level nested struct lookups
    for dotted, td in system_tds.items():
        if dotted not in type_defs_by_dotted_pre:
            type_defs_by_dotted_pre[dotted] = td

    def _collect_deps(dotted, visited=None):
        """Recursively collect all bridge type dotted names that a type depends on.

        Performs a depth-first traversal of field types to find all bridge-defined
        nested types.

        Args:
            dotted (str): Starting dotted type name.
            visited (set | None): Guard against cycles; initialized to an empty set
                                   on the first call.

        Returns:
            set[str]: All dotted type names reachable from ``dotted`` (including
                      ``dotted`` itself if it is bridge-defined).
        """
        if visited is None:
            visited = set()
        if dotted in visited or dotted not in type_defs_by_dotted_pre:
            return visited
        visited.add(dotted)
        for field_def in type_defs_by_dotted_pre[dotted]["fields"].values():
            field_type = field_def.get("type", "")
            if field_type in type_defs_by_dotted_pre:
                _collect_deps(field_type, visited)
        return visited

    def _type_yamls_for_bridge(bd_):
        """Return all type YAML paths referenced by a bridge descriptor.

        Collects the YAML file paths for every type name in the bridge's
        ``"type_names"`` list that has a corresponding file under ``data_dir``.

        Args:
            bd_ (dict): Normalized bridge definition from ``validate_bridge_yaml``.

        Returns:
            list[Path]: Absolute paths to all type YAML files for the bridge.
        """
        all_deps: set[str] = set()
        for type_name in bd_["type_names"]:
            _collect_deps(type_name, all_deps)
        return [type_yaml_paths[t] for t in all_deps if t in type_yaml_paths]

    bridges_to_generate = []
    for bd in validated_bridges:
        bridge_name = bd["name"]
        bridge_yaml = bridges_dir / f"{bridge_name}.yaml"
        type_yamls = _type_yamls_for_bridge(bd)
        if bg_state.bridge_stale(bridge_name, bridge_yaml, type_yamls, state):
            _progress(f"Regenerating Python bridge: {bridge_name}")
            bridges_to_generate.append(bd)
        else:
            _progress(f"Python bridge '{bridge_name}' is up-to-date")

    # Emit the type YAMLs used by each bridge so PythonBridge.cmake can
    # track them for future staleness checks.
    #
    # We emit ALL type YAMLs loaded for the profile per bridge, including
    # transitively discovered ones (e.g., point_cloud.yaml loaded as a nested
    # field inside semantic_data.yaml).  bd["type_names"] only contains types
    # directly named in inputs/outputs; type_yaml_paths contains every YAML
    # loaded by load_type_yaml() including recursive field dependencies.
    #
    # To correctly attribute transitive dependencies to bridges: a bridge
    # depends on a type YAML if that YAML was needed to fully define any
    # type the bridge directly uses.  We compute this by walking the full
    # dependency closure for each bridge.
    for bd in validated_bridges:
        all_dep_names: set[str] = set()
        for type_name in bd["type_names"]:
            _collect_deps(type_name, all_dep_names)
        bridge_type_yamls = sorted(
            str(type_yaml_paths[t])
            for t in all_dep_names
            if t in type_yaml_paths
            )
        name_upper = bd["name"].upper().replace(".", "_")
        print(f'set(PY_BRIDGE_TYPE_YAMLS_{name_upper} "{bg_helpers.cmake_list(bridge_type_yamls)}")')

    # Generate struct headers (mirroring subdir structure).
    # Only regenerate if at least one bridge is stale - if everything is
    # up to date, the headers on disk are already correct.
    struct_out_root = build_dir / "include" / "illixr" / "bridge"
    struct_out_root.mkdir(parents=True, exist_ok=True)

    cumulative_dotted: list[str] = []
    generated_headers: list[str] = []
    generated_serializers: set[str] = set()
    # The libclang serialization scan is expensive - build it lazily the
    # first time a struct header actually needs to be written.
    illixr_ser_map: dict | None = None

    for td in sorted_types:
        dotted = td["dotted"]
        rel_path = bg_helpers.dotted_to_path(dotted)
        out_hdr = struct_out_root / f"{rel_path}.hpp"
        out_hdr.parent.mkdir(parents=True, exist_ok=True)
        if bridges_to_generate:
            if illixr_ser_map is None:
                illixr_ser_map = bg_scan_illixr.build_illixr_serialization_map(source_dir)
            _progress(f"  Generating struct header: {dotted}")
            out_hdr.write_text(bg_cg_struct.gen_struct_header(td, cumulative_dotted))
            boost_hdr = struct_out_root / f"{rel_path}_ser.hpp"
            boost_hdr.write_text(bg_cg_struct.gen_boost_hpp(td, out_hdr.name, cumulative_dotted, illixr_ser_map))
            boost_cpp = struct_out_root / f"{rel_path}_ser.cpp"
            boost_cpp.write_text(bg_cg_struct.gen_boost_cpp(td, boost_hdr.name))
        else:
            boost_hdr = struct_out_root / f"{rel_path}_ser.hpp"
            boost_cpp = struct_out_root / f"{rel_path}_ser.cpp"
        generated_headers.append(str(out_hdr))
        generated_serializers.add(str(boost_hdr))
        generated_serializers.add(str(boost_cpp))
        cumulative_dotted.append(dotted)

    # Generate per-bridge sources
    all_bridge_names = []
    all_plugin_dirs = []
    all_plugin_cpp_files = []
    all_binding_cpp_files = []

    for bd in bridges_to_generate:
        bridge_name = bd["name"]
        plugin_dir = build_dir / "generated" / "plugins" / bridge_name
        plugin_dir.mkdir(parents=True, exist_ok=True)

        bridge_path = bridges_dir / f"{bridge_name}.yaml"
        # Script path: resolved YAML value used as compile-time default.
        # At runtime ILLIXR_<NAME>_SCRIPT env var overrides this.
        script_yaml_default = str((bridge_path.parent / bd["script"]).resolve())

        _progress(f"  Generating plugin sources: {bridge_name}")
        (plugin_dir / "plugin.hpp").write_text(
            bg_cg_plugin.gen_plugin_hpp(bd, gen_dotted_set))
        (plugin_dir / "plugin.cpp").write_text(
            bg_cg_plugin.gen_plugin_cpp(bd, gen_dotted_set, script_yaml_default,
                                        plugin_dir, type_defs_by_dotted_pre))
        (plugin_dir / "CMakeLists.txt").write_text(
            bg_cg_plugin.gen_plugin_cmake(bd, gen_dotted_set, generated_serializers,
                                          plugin_dir, build_dir / "include"))

        # Bindings: one file per generated type used by this bridge
        bridge_binding_files = []
        cumulative_so_far: list[str] = []
        for td in sorted_types:
            dotted = td["dotted"]
            if dotted not in bd["type_names"]:
                cumulative_so_far.append(dotted)
                continue
            bfile = plugin_dir / bg_helpers.dotted_to_binding_filename(dotted)
            bfile.write_text(bg_cg_bindings.gen_bindings_cpp(td, cumulative_so_far))
            bridge_binding_files.append(str(bfile))
            cumulative_so_far.append(dotted)

        # Python package tree for bridge types used by this bridge
        used_tds = [td for td in sorted_types if td["dotted"] in bd["type_names"]]
        bg_cg_bindings.gen_python_package_tree(used_tds, plugin_dir)

        all_bridge_names.append(bridge_name)
        all_plugin_dirs.append(str(plugin_dir))
        all_plugin_cpp_files.append(str(plugin_dir / "plugin.cpp"))
        all_binding_cpp_files.extend(bridge_binding_files)

    # Save the JSON state file with hashes of all processed files.
    new_state = {}
    for bd in validated_bridges:
        bridge_name = bd["name"]
        bridge_yaml = bridges_dir / f"{bridge_name}.yaml"
        type_yamls = _type_yamls_for_bridge(bd)
        new_state[bridge_name] = {
            "bridge_hash": bg_state.file_md5(bridge_yaml),
            "type_hashes": {str(p): bg_state.file_md5(p) for p in type_yamls},
            }
    bg_state.save_state(build_dir, new_state)
    if bridges_to_generate:
        _progress("Python bridge generation complete "
                  f"({len(bridges_to_generate)} bridge(s) regenerated)")

    # Emit CMake variables.
    # Always emit ALL bridges (validated_bridges), not just the regenerated
    # subset, so cmake can register every bridge as a build target.
    all_bridge_names_full = [bd["name"] for bd in validated_bridges]
    all_cmake_dirs = [str(build_dir / "generated" / "plugins" / n)
                      for n in all_bridge_names_full]
    all_plugin_dirs_full = [str(build_dir / "generated" / "plugins" / n)
                            for n in all_bridge_names_full]
    all_plugin_cpp_files_full = [str(build_dir / "generated" / "plugins" / n / "plugin.cpp")
                                 for n in all_bridge_names_full]
    print(f'set(PY_BRIDGE_NAMES "{bg_helpers.cmake_list(all_bridge_names_full)}")')
    print(f'set(PY_BRIDGE_CMAKE_DIRS "{bg_helpers.cmake_list(all_cmake_dirs)}")')
    print(f'set(PY_BRIDGE_PLUGIN_DIRS "{bg_helpers.cmake_list(all_plugin_dirs_full)}")')
    print(f'set(PY_BRIDGE_PLUGIN_CPPS "{bg_helpers.cmake_list(all_plugin_cpp_files_full)}")')
    print(f'set(PY_BRIDGE_BINDING_CPPS "{bg_helpers.cmake_list(all_binding_cpp_files)}")')
    print(f'set(PY_BRIDGE_STRUCT_HDRS "{bg_helpers.cmake_list(generated_headers)}")')
    print(f'set(PY_BRIDGE_STRUCT_INCLUDE_DIR "{build_dir / "include"}")')
    print(f'set(PY_BRIDGE_COUNT "{len(all_bridge_names_full)}")')

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
