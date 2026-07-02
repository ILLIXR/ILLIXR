# Copyright 2020-2026, The Board of Trustees of the University of Illinois.
# SPDX-License-Identifier: BSL-1.0
"""JSON state file for per-bridge staleness tracking."""
import hashlib
import json
from pathlib import Path


def file_md5(path: Path) -> str:
    """Return the hex-encoded MD5 digest of a file's contents.

    Args:
        path (Path): File to hash.

    Returns:
        str: Hex MD5 digest, or ``""`` if the file cannot be read.
    """
    try:
        return hashlib.md5(path.read_bytes()).hexdigest()
    except OSError:
        return ""


def load_state(build_dir: Path) -> dict:
    """Load the bridge staleness state from ``<build_dir>/.py_bridge_state.json``.

    Args:
        build_dir (Path): CMake build directory.

    Returns:
        dict: Previously saved state keyed by bridge name, or ``{}`` if the
              file does not exist or cannot be parsed.
    """
    state_file = build_dir / ".py_bridge_state.json"
    try:
        return json.loads(state_file.read_text())
    except (OSError, json.JSONDecodeError):
        return {}


def save_state(build_dir: Path, state: dict) -> None:
    """Persist the bridge staleness state to ``<build_dir>/.py_bridge_state.json``.

    Args:
        build_dir (Path): CMake build directory.
        state (dict): State dict mapping bridge name to hashes, as returned by
                      ``_load_state`` and updated during generation.

    Returns:
        None
    """
    state_file = build_dir / ".py_bridge_state.json"
    state_file.write_text(json.dumps(state, indent=2, sort_keys=True))


def bridge_stale(bridge_name: str, bridge_yaml: Path,
                  type_yaml_paths_for_bridge: list[Path],
                  state: dict) -> bool:
    """
    Return True if the bridge needs regeneration.

    Compares MD5 hashes of the bridge YAML and all its type YAMLs
    against the values stored in the state file from the previous run.
    Always returns True if any file is missing from the state.
    """
    entry = state.get(bridge_name, {})
    if not entry:
        return True

    # Check bridge YAML hash
    current_bridge_hash = file_md5(bridge_yaml)
    if entry.get("bridge_hash") != current_bridge_hash:
        return True

    # Check type YAML hashes
    stored_type_hashes = entry.get("type_hashes", {})
    for tpath in type_yaml_paths_for_bridge:
        current = file_md5(tpath)
        if stored_type_hashes.get(str(tpath)) != current:
            return True
    # Also stale if a previously tracked YAML is now gone
    for stored_path in stored_type_hashes:
        if not Path(stored_path).exists():
            return True

    return False
