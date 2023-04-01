#!/usr/bin/env python3
import multiprocessing
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from subprocess import PIPE, DEVNULL
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

# Environment variables for configuring the GPU
env_gpu : Mapping[str, str] = dict(
    __GL_MaxFramesAllowed="1", # Double buffer framebuffer
    __GL_SYNC_TO_VBLANK="1",   # Block on vsync
)


def clean_one_plugin(config: Mapping[str, Any], plugin_config: Mapping[str, Any]) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    path_str: str = str(path)
    name: str = plugin_config["name"] if plugin_config["name"] else os.path.basename(path_str)
    targets: List[str] = ["clean"]
    print(f"[Clean] Plugin '{name}' @ '{path_str}/'")
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    make(path, targets, plugin_config["config"], env_override=env_override)
    return path


def build_one_plugin(
    config: Mapping[str, Any],
    plugin_config: Mapping[str, Any],
    test: bool = False,
    illixr_monado: bool = False,
) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    if not (path / "common").exists():
        common_path = pathify(config["common"]["path"], root_dir, cache_path, True, True)
        common_path = common_path.resolve()
        os.symlink(common_path, path / "common")
    plugin_so_name = f"plugin.{profile}.so"
    targets = [plugin_so_name] + (["tests/run"] if test else [])
    if illixr_monado:
        plugin_config["config"].update(ILLIXR_MONADO="ON")

    ## When building using runner, enable ILLIXR integrated mode (compilation)
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    make(path, targets, plugin_config["config"], env_override=env_override)

    return path / plugin_so_name


def build_runtime(
    config: Mapping[str, Any],
    suffix: str,
    test: bool = False,
    illixr_monado: bool = False,
) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path: Path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    targets = [runtime_name] + (["tests/run"] if test else [])
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="ON")
    if illixr_monado:
        runtime_config.update(ILLIXR_MONADO="ON")
    make(runtime_path, targets, runtime_config, env_override=env_override)
    return runtime_path / runtime_name


def load_native(config: Mapping[str, Any]) -> None:
    runtime_exe_path = build_runtime(config, "exe")
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    realsense_cam_string = config["realsense_cam"]
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
        ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
        KIMERA_ROOT=config["action"]["kimera_path"],
        AUDIO_ROOT=config["action"]["audio_path"],
        REALSENSE_CAM=str(realsense_cam_string),
        **env_gpu,
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
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    make(Path("common"), ["tests/run"], env_override=env_override)
    realsense_cam_string = config["realsense_cam"]
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config, test=True),
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Building plugins",
    )

    ## If pre-sleep is enabled, the application will pause and wait for a gdb process.
    ## If enabled, disable 'catchsegv' so that gdb can catch segfaults.
    enable_pre_sleep : bool      = config["enable_pre_sleep"]
    cmd_list_tail    : List[str] = ["xvfb-run", str(runtime_exe_path), *map(str, plugin_paths)]
    cmd_list         : List[str] = (["catchsegv"] if not enable_pre_sleep else list()) + cmd_list_tail

    subprocess_run(
        cmd_list,
        env_override=dict(
            ILLIXR_DATA=str(data_path),
            ILLIXR_DEMO_DATA=str(demo_data_path),
            ILLIXR_RUN_DURATION=str(config["action"].get("ILLIXR_RUN_DURATION", 10)),
            ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
            ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
            ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
            ILLIXR_ENABLE_PRE_SLEEP=str(enable_pre_sleep),
            KIMERA_ROOT=config["action"]["kimera_path"],
            AUDIO_ROOT=config["action"]["audio_path"],
            REALSENSE_CAM=str(realsense_cam_string),
            **env_gpu,
        ),
        check=True,
    )


def load_monado(config: Mapping[str, Any]) -> None:
    action_name = config["action"]["name"]

    profile = config["profile"]
    cmake_profile = "Debug" if profile == "dbg" else "RelWithDebInfo"

    runtime_path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    monado_config = config["action"]["monado"].get("config", {})
    monado_path = pathify(config["action"]["monado"]["path"], root_dir, cache_path, True, True)
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    realsense_cam_string = config["realsense_cam"]
    build_runtime(config, "so", illixr_monado = True)

    def process_plugin(plugin_config: Mapping[str, Any]) -> Path:
        plugin_config.update(ILLIXR_MONADO="ON")
        return build_one_plugin(config, plugin_config, illixr_monado=True)

    plugin_paths: List[Path] = threading_map(
        process_plugin,
        [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
        desc="Building plugins",
    )
    plugin_paths_comp_arg: str = ':'.join(map(str, plugin_paths))

    env_monado: Mapping[str, str] = dict(
        ILLIXR_DATA=str(data_path),
        ILLIXR_PATH=str(runtime_path / f"plugin.{profile}.so"),
        ILLIXR_COMP=plugin_paths_comp_arg,
        XR_RUNTIME_JSON=str(monado_path / "build" / "openxr_monado-dev.json"),
        XRT_TRACING="true",
        KIMERA_ROOT=config["action"]["kimera_path"],
        AUDIO_ROOT=config["action"]["audio_path"],
    )

    ## For CMake
    monado_build_opts: Mapping[str, str] = dict(
        CMAKE_BUILD_TYPE=cmake_profile,
        ILLIXR_PATH=str(runtime_path),
        **monado_config,
    )

    monado_build_opts.update(ILLIXR_MONADO="ON")

    ## Compile Monado
    cmake(
        monado_path,
        monado_path / "build",
        monado_build_opts,
        env_override=env_monado,
    )

    ## Compile OpenXR apps
    openxr_app_bin_paths = []
    if (profile != "dbg"):
        for openxr_app_obj in config["action"]["openxr_apps"]:
            openxr_app_config : Mapping[str, str] = openxr_app_obj.get("config", {})

            openxr_app_path     : Optional[Path] # Forward declare type
            openxr_app_bin_path : Path           # Forward declare type

            if "src_path" in openxr_app_obj["app"]:
                ## Pathify 'src_path' for compilation
                openxr_app_path     = pathify(openxr_app_obj["app"]["src_path"], root_dir, cache_path, True , True)
                openxr_app_bin_path = openxr_app_path / openxr_app_obj["app"]["bin_subpath"]
            else:
                ## Get the full path to the 'app' binary
                openxr_app_path     = None
                openxr_app_bin_path = pathify(openxr_app_obj["app"], root_dir, cache_path, True, False)

            ## Compile the OpenXR app if we received an 'app' with 'src_path'
            if openxr_app_path:
                cmake(
                    openxr_app_path,
                    openxr_app_path / "build",
                    dict(CMAKE_BUILD_TYPE=cmake_profile, **openxr_app_config),
                )

            if not openxr_app_bin_path.exists():
                raise RuntimeError(f"{action_name} Failed to build openxr_app, path={openxr_app_bin_path})")
            else:
                openxr_app_bin_paths.append(openxr_app_bin_path);

    monado_target_name : str  = "monado-service"
    monado_target_dir  : Path = monado_path / "build" / "src" / "xrt" / "targets" / "service"
    monado_target_path : Path = monado_target_dir / monado_target_name

    if not monado_target_path.exists():
        raise RuntimeError(f"[{action_name}] Failed to build monado, path={monado_target_path})")

    ## Open the Monado service application
    actual_cmd_str = config["action"].get("command", "$cmd")
    illixr_cmd_list = [str(monado_target_path), *map(str, plugin_paths)]
    env_override=dict(
        ILLIXR_DEMO_DATA=str(demo_data_path),
        ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
        ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
        ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
        ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
        REALSENSE_CAM=str(realsense_cam_string),
        **env_monado,
        **os.environ,
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

    ## Launch the Monado service before any OpenXR apps are opened
    monado_service_proc = subprocess.Popen(actual_cmd_list, env=env_override)

    ## Give the Monado service some time to boot up and the user some time to initialize VIO
    time.sleep(5)

    ## Launch all OpenXR apps after the service is launched
    for openxr_app_bin_path in openxr_app_bin_paths:
        subprocess.Popen(
           [str(openxr_app_bin_path)],
           env=dict(
               ILLIXR_DEMO_DATA=str(demo_data_path),
               ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
               ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
               ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
               ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
               REALSENSE_CAM=str(realsense_cam_string),
               IPC_IGNORE_VERSION="1",
               **env_monado,
               **os.environ,
           )
        )

    ## Continue running the service until it closes
    while (monado_service_proc.poll() == None):
        pass


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