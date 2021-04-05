#!/usr/bin/env python3
import multiprocessing
import os
import shlex
import tempfile
from pathlib import Path
from typing import Any, BinaryIO, ContextManager, List, Mapping, Optional, cast

import click
import jsonschema
import yaml
from util import (
    cmake,
    fill_defaults,
    flatten1,
    make,
    noop_context,
    pathify,
    relative_to,
    replace_all,
    subprocess_run,
    threading_map,
    unflatten,
)
from yamlinclude import YamlIncludeConstructor

# isort main.py
# black -l 90 main.py
# mypy --strict --ignore-missing-imports main.py

root_dir = relative_to((Path(__file__).parent / "../..").resolve(), Path(".").resolve())

cache_path = root_dir / ".cache" / "paths"
cache_path.mkdir(parents=True, exist_ok=True)


def clean_one_plugin(config: Mapping[str, Any], plugin_config: Mapping[str, Any]) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    path_str: str = str(path)
    name: str = plugin_config["name"] if plugin_config["name"] else os.path.basename(path_str)
    targets: List[str] = ["clean"]
    print(f"[Clean] Plugin '{name}' @ '{path_str}/'")
    make(path, targets, plugin_config["config"])
    return path


def build_one_plugin(
    config: Mapping[str, Any],
    plugin_config: Mapping[str, Any],
    test: bool = False,
) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    if not (path / "common").exists():
        common_path = pathify(config["common"]["path"], root_dir, cache_path, True, True)
        common_path = common_path.resolve()
        os.symlink(common_path, path / "common")
    plugin_so_name = f"plugin.{profile}.so"
    targets = [plugin_so_name] + (["tests/run"] if test else [])

    ## When building using runner, enable ILLIXR integrated mode (compilation)
    env_override: Mapping[str, str] = { "ILLIXR_INTEGRATION" : "yes" }
    make(path, targets, plugin_config["config"], env_override=env_override)

    return path / plugin_so_name


def build_runtime(
    config: Mapping[str, Any],
    suffix: str,
    test: bool = False,
) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path: Path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    targets = [runtime_name] + (["tests/run"] if test else [])
    make(runtime_path, targets, runtime_config)
    return runtime_path / runtime_name


def load_native(config: Mapping[str, Any]) -> None:
    runtime_exe_path = build_runtime(config, "exe")
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config),
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Building plugins",
    )
    actual_cmd_str = config["action"].get("command", "$cmd")
    illixr_cmd_list = [str(runtime_exe_path), *map(str, plugin_paths)]
    env_override = dict(
        ILLIXR_DATA=str(data_path),
        ILLIXR_DEMO_DATA=str(demo_data_path),
        ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
        ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
        ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
        ILLIXR_RUN_DURATION=str(config["action"].get("ILLIXR_RUN_DURATION", 60)),
        KIMERA_ROOT=config["action"]["kimera_path"],
    )
    env_list = [f"{shlex.quote(var)}={shlex.quote(val)}" for var, val in env_override.items()]
    actual_cmd_list = list(
        flatten1(
            replace_all(
                unflatten(shlex.split(actual_cmd_str)),
                {
                    ("$env_cmd",): [
                        "env",
                        "-C",
                        Path(".").resolve(),
                        *env_list,
                        *illixr_cmd_list,
                    ],
                    ("$cmd",): illixr_cmd_list,
                    ("$quoted_cmd",): [shlex.quote(shlex.join(illixr_cmd_list))],
                    ("$env",): env_list,
                },
            )
        )
    )
    log_stdout_str = config["action"].get("log_stdout", None)
    log_stdout_ctx = cast(
        ContextManager[Optional[BinaryIO]],
        (open(log_stdout_str, "wb") if (log_stdout_str is not None) else noop_context(None)),
    )
    with log_stdout_ctx as log_stdout:
        subprocess_run(
            actual_cmd_list,
            env_override=env_override,
            stdout=log_stdout,
            check=True,
        )


def load_tests(config: Mapping[str, Any]) -> None:
    runtime_exe_path = build_runtime(config, "exe", test=True)
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    make(Path("common"), ["tests/run"])
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config, test=True),
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Building plugins",
    )
    subprocess_run(
        ["catchsegv", "xvfb-run", str(runtime_exe_path), *map(str, plugin_paths)],
        env_override=dict(
            ILLIXR_DATA=str(data_path),
            ILLIXR_DEMO_DATA=str(demo_data_path),
            ILLIXR_RUN_DURATION=str(config["action"].get("ILLIXR_RUN_DURATION", 10)),
            ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
            ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
            ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
            KIMERA_ROOT=config["action"]["kimera_path"],
        ),
        check=True,
    )


def load_monado(config: Mapping[str, Any]) -> None:
    profile = config["profile"]
    cmake_profile = "Debug" if profile == "dbg" else "Release"
    openxr_app_config = config["action"]["openxr_app"].get("config", {})
    monado_config = config["action"]["monado"].get("config", {})

    runtime_path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    monado_path = pathify(config["action"]["monado"]["path"], root_dir, cache_path, True, True)
    openxr_app_path = pathify(config["action"]["openxr_app"]["path"], root_dir, cache_path, True, True)
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]

    cmake(
        monado_path,
        monado_path / "build",
        dict(
            CMAKE_BUILD_TYPE=cmake_profile,
            BUILD_WITH_LIBUDEV="0",
            BUILD_WITH_LIBUVC="0",
            BUILD_WITH_LIBUSB="0",
            BUILD_WITH_NS="0",
            BUILD_WITH_PSMV="0",
            BUILD_WITH_PSVR="0",
            BUILD_WITH_OPENHMD="0",
            BUILD_WITH_VIVE="0",
            ILLIXR_PATH=str(runtime_path),
            **monado_config,
        ),
    )
    cmake(
        openxr_app_path,
        openxr_app_path / "build",
        dict(CMAKE_BUILD_TYPE=cmake_profile, **openxr_app_config),
    )
    build_runtime(config, "so")
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config),
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Building plugins",
    )

    subprocess_run(
        [str(openxr_app_path / "build" / "./openxr-example")],
        env_override=dict(
            XR_RUNTIME_JSON=str(monado_path / "build" / "openxr_monado-dev.json"),
            ILLIXR_PATH=str(runtime_path / f"plugin.{profile}.so"),
            ILLIXR_COMP=":".join(map(str, plugin_paths)),
            ILLIXR_DATA=str(data_path),
            ILLIXR_DEMO_DATA=str(demo_data_path),
            ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
            ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
            ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
            KIMERA_ROOT=config["action"]["kimera_path"],
        ),
        check=True,
    )


def clean_project(config: Mapping[str, Any]) -> None:
    plugin_paths = threading_map(
        lambda plugin_config: clean_one_plugin(config, plugin_config),
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Cleaning plugins",
    )


def make_docs(config: Mapping[str, Any]) -> None:
    dir_api = "site/api"
    dir_docs = "site/docs"
    cmd_doxygen = ["doxygen", "doxygen.conf"]
    cmd_mkdocs = ["python3", "-m", "mkdocs", "build"]
    if not os.path.exists(dir_api):
        os.makedirs(dir_api)
    if not os.path.exists(dir_docs):
        os.makedirs(dir_docs)
    subprocess_run(
        cmd_doxygen,
        check=True,
        capture_output=False,
    )
    subprocess_run(
        cmd_mkdocs,
        check=True,
        capture_output=False,
    )


actions = {
    "native": load_native,
    "monado": load_monado,
    "tests": load_tests,
    "clean": clean_project,
    "docs": make_docs,
}


def run_config(config_path: Path) -> None:
    """Parse a YAML config file, returning the validated ILLIXR system config."""
    YamlIncludeConstructor.add_to_loader_class(
        loader_class=yaml.FullLoader,
        base_dir=config_path.parent,
    )

    with config_path.open() as f:
        config = yaml.full_load(f)

    with (root_dir / "runner/config_schema.yaml").open() as f:
        config_schema = yaml.safe_load(f)

    jsonschema.validate(instance=config, schema=config_schema)
    fill_defaults(config, config_schema)

    action = config["action"]["name"]

    if action not in actions:
        raise RuntimeError(f"No such action: {action}")
    actions[action](config)


if __name__ == "__main__":

    @click.command()
    @click.argument("config_path", type=click.Path(exists=True))
    def main(config_path: str) -> None:
        run_config(Path(config_path))

    main()
