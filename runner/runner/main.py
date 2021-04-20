#!/usr/bin/env python3
import multiprocessing
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from subprocess import PIPE
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


def plugin_to_str(plugin_path: Path, plugin_config: Mapping[str, Any]) -> str:
    path_str: str = str(plugin_path)
    name: str = plugin_config["name"] if "name" in plugin_config else os.path.basename(path_str)
    return name


def flow_to_str(flow: Mapping[str, Any]) -> str:
    plugin_names: List[str] = list()
    for plugin_group in flow:
        for plugin_config in plugin_group["plugin_group"]:
            plugin_path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
            plugin_names.append(plugin_to_str(plugin_path, plugin_config))
    plugins_str: str = ', '.join(plugin_names)
    return f"[ {plugins_str} ]"


def clean_one_plugin(config: Mapping[str, Any], plugin_config: Mapping[str, Any]) -> Path:
    action_name = config["action"]["name"]
    profile = config["profile"]
    plugin_path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    path_str: str = str(plugin_path)
    name: str = plugin_to_str(plugin_path, plugin_config)
    targets: List[str] = ["clean"]
    var_dict: Mapping[str, str] = plugin_config["config"] if "config" in plugin_config else None
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")

    if os.path.isfile(plugin_path / "Makefile"):
        make(plugin_path, targets, var_dict, env_override=env_override)
    elif os.path.isfile(plugin_path / "CMakeLists.txt"):
        print(f" >  (Cleaning cmake builds not supported for plugins yet)")
    else:
        raise RuntimeError("Failed to find build recipe file")

    return plugin_path


def build_one_plugin(
    config: Mapping[str, Any],
    plugin_config: Mapping[str, Any],
    test: bool = False,
) -> Path:
    profile = config["profile"]
    plugin_path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    if not (plugin_path / "common").exists():
        common_path = pathify(config["common"]["path"], root_dir, cache_path, True, True)
        common_path = common_path.resolve()
        os.symlink(common_path, plugin_path / "common")
    plugin_so_name = f"plugin.{profile}.so"
    targets = [plugin_so_name] + (["tests/run"] if test else [])

    ## When building using runner, enable ILLIXR integrated mode (compilation)
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    var_dict: Optional[Mapping[str, str]] = plugin_config["config"] if "config" in plugin_config else None
    make(plugin_path, targets, var_dict, env_override=env_override)

    return plugin_path / plugin_so_name


def build_runtime(
    config: Mapping[str, Any],
    suffix: str,
    test: bool = False,
    is_mainline: bool = False,
) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path: Path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    targets = [runtime_name] + (["tests/run"] if test else [])
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="ON")
    if is_mainline:
        runtime_config.update(ILLIXR_MONADO_MAINLINE="ON")
    make(runtime_path, targets, runtime_config, env_override=env_override)
    return runtime_path / runtime_name


def do_ci(config: Mapping[str, Any]) -> None:
    action_name: str = config["action"]["name"]
    runtime_exe_path: Path = build_runtime(config, "exe", test=True)
    data_path: Path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path: Path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag: str = config["enable_offload"]
    enable_alignment_flag: str = config["enable_alignment"]

    for ci_type in ["no-build", "build-only", "run-solo"]:
        for plugin_config in config["action"][ci_type]["plugin_group"]:
            plugin_path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
            plugin_name: str = plugin_to_str(plugin_path, plugin_config)

            print(f"[{action_name}] {ci_type}: {plugin_name}")

            clean_one_plugin(config, plugin_config)

            if ci_type == "no-build":
                continue

            plugin_so_path: Path = build_one_plugin(config, plugin_config)

            if ci_type == "build-only":
                continue

            plugin_paths: List[Path] = [ plugin_so_path ]
            subprocess_run(
                ["catchsegv", "xvfb-run", str(runtime_exe_path), *map(str, plugin_paths)],
                env_override=dict(
                    ILLIXR_DATA=str(data_path),
                    ILLIXR_DEMO_DATA=str(demo_data_path),
                    ILLIXR_RUN_DURATION=str(config["action"].get("ILLIXR_RUN_DURATION", 10)),
                    ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
                    ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
                    ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
                    ILLIXR_ENABLE_PRE_SLEEP=str(False), ## Hardcode to False for solo runs
                    KIMERA_ROOT=config["action"]["kimera_path"],
                ),
                check=True,
            )


def load_native(config: Mapping[str, Any]) -> None:
    action_name = config["action"]["name"]
    runtime_exe_path = build_runtime(config, "exe")
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    realsense_cam_string = config["realsense_cam"]

    flow_id = 0
    plugins_append = config["append"]["plugin_group"]
    for flow in [flow_obj["flow"] + plugins_append for flow_obj in config["flows"]]:
        flow_str: str = flow_to_str(flow)
        print(f"[{action_name}] Processing flow: {flow_str}")
        plugin_paths = threading_map(
            lambda plugin_config: build_one_plugin(config, plugin_config),
            [plugin_config for plugin_group in flow for plugin_config in plugin_group["plugin_group"]],
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
        if log_stdout_str:
            log_stdout_str += f".{flow_id}"
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
        flow_id += 1


def load_tests(config: Mapping[str, Any]) -> None:
    action_name = config["action"]["name"]
    runtime_exe_path = build_runtime(config, "exe", test=True)
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    enable_offload_flag = config["enable_offload"]
    enable_alignment_flag = config["enable_alignment"]
    realsense_cam_string = config["realsense_cam"]

    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")

    common_path = pathify(config["common"]["path"], root_dir, cache_path, True, True)
    make(common_path, ["clean"], env_override=env_override)
    make(common_path, ["tests/run"], env_override=env_override) # Tests 'common' (don't need 'plugins/common.yaml' for CI)

    enable_ci = bool(config["action"]["enable_ci"]) if "enable_ci" in config["action"] else False

    if enable_ci:
        ## Perform CI solo builds and runs before processing the flows
        do_ci(config)

    plugins_append = config["append"]["plugin_group"]
    for flow in [flow_obj["flow"] + plugins_append for flow_obj in config["flows"]]:
        flow_str: str = flow_to_str(flow)
        print(f"[{action_name}] Processing flow: {flow_str}")
        plugin_paths = threading_map(
            lambda plugin_config: build_one_plugin(config, plugin_config, test=True),
            [plugin_config for plugin_group in flow for plugin_config in plugin_group["plugin_group"]],
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
                ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
                KIMERA_ROOT=config["action"]["kimera_path"],
                AUDIO_ROOT=config["action"]["audio_path"],
                REALSENSE_CAM=str(realsense_cam_string),
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

    is_mainline: bool = bool(config["action"]["is_mainline"])
    build_runtime(config, "so", is_mainline=is_mainline)

    if not "openxr_app" in config["action"]:
        raise RuntimeError(f"Missing 'openxr_app' property for action '{action_name}")

    openxr_app_obj    : Mapping[str, Any] = config["action"]["openxr_app"]
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
        openxr_app_bin_path = pathify(openxr_app_obj["app"], root_dir, cache_path, True, True)

    ## Compile the OpenXR app if we received an 'app' with 'src_path'
    if openxr_app_path:
        cmake(
            openxr_app_path,
            openxr_app_path / "build",
            dict(CMAKE_BUILD_TYPE=cmake_profile, **openxr_app_config),
        )

    if not openxr_app_bin_path.exists():
        raise RuntimeError(f"{action_name} Failed to build openxr_app (mainline={is_mainline}, path={openxr_app_bin_path})")

    def process_plugin(plugin_config: Mapping[str, Any]) -> Path:
        if is_mainline:
            plugin_config.update(ILLIXR_MONADO_MAINLINE="ON")
        return build_one_plugin(config, plugin_config)

    plugins_append = config["append"]["plugin_group"]
    for flow in [flow_obj["flow"] + plugins_append for flow_obj in config["flows"]]:
        flow_str: str = flow_to_str(flow)
        print(f"[{action_name}] Processing flow: {flow_str}")
        plugin_paths: List[Path] = threading_map(
            process_plugin,
            [plugin_config for plugin_group in flow for plugin_config in plugin_group["plugin_group"]],
            desc="Building plugins",
        )
        plugin_paths_comp_arg: str = ':'.join(map(str, plugin_paths))

        env_monado: Mapping[str, str] = dict(
            ILLIXR_DATA=str(data_path),
            ILLIXR_PATH=str(runtime_path / f"plugin.{profile}.so"),
            ILLIXR_COMP=plugin_paths_comp_arg,
            XR_RUNTIME_JSON=str(monado_path / "build" / "openxr_monado-dev.json"),
        )

        ## For CMake
        monado_build_opts: Mapping[str, str] = dict(
            CMAKE_BUILD_TYPE=cmake_profile,
            ILLIXR_PATH=str(runtime_path),
            **monado_config,
        )

        if is_mainline:
            monado_build_opts.update(ILLIXR_MONADO_MAINLINE="ON")

        ## Compile Monado
        cmake(
            monado_path,
            monado_path / "build",
            monado_build_opts,
            env_override=env_monado,
        )

        if is_mainline:
            monado_target_name : str  = "monado-service"
            monado_target_dir  : Path = monado_path / "build" / "src" / "xrt" / "targets" / "service"
            monado_target_path : Path = monado_target_dir / monado_target_name

            if not monado_target_path.exists():
                raise RuntimeError(f"[{action_name}] Failed to build monado (mainline={is_mainline}, path={monado_target_path})")

            env_monado_service: Mapping[str, str] = dict(**os.environ, **env_monado)

            ## Open the Monado service application in the background
            monado_service_proc = subprocess.Popen([str(monado_target_path)], env=env_monado_service, stdin=PIPE, stdout=PIPE, stderr=PIPE)

        ## Give the Monado service some time to boot up and the user some time to initialize VIO
        time.sleep(5)

        subprocess_run(
            [str(openxr_app_bin_path)],
            env_override=dict(
                ILLIXR_DEMO_DATA=str(demo_data_path),
                ILLIXR_OFFLOAD_ENABLE=str(enable_offload_flag),
                ILLIXR_ALIGNMENT_ENABLE=str(enable_alignment_flag),
                ILLIXR_ENABLE_VERBOSE_ERRORS=str(config["enable_verbose_errors"]),
                ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
                KIMERA_ROOT=config["action"]["kimera_path"],
                AUDIO_ROOT=config["action"]["audio_path"],
                REALSENSE_CAM=str(realsense_cam_string),
                **env_monado,
            ),
            check=True,
        )

        if is_mainline:
            ## Close and clean up the Monado service application
            try:
                outs, errs = monado_service_proc.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                monado_service_proc.kill()
                outs, errs = monado_service_proc.communicate()

                ## Clean up leftover socket. It can only either be in $XDG_RUNTIME_DIR or /tmp
                Path(env_monado_service['XDG_RUNTIME_DIR'] + "/monado_comp_ipc").unlink(missing_ok=True)
                Path("/tmp/monado_comp_ipc").unlink(missing_ok=True)

            print("\nstdout:\n")
            sys.stdout.buffer.write(outs)

            print("\nstderr:\n")
            sys.stderr.buffer.write(errs)


def clean_project(config: Mapping[str, Any]) -> None:
    action_name = config["action"]["name"]
    plugins_append = config["append"]["plugin_group"]
    for flow in [flow_obj["flow"] + plugins_append for flow_obj in config["flows"]]:
        flow_str: str = flow_to_str(flow)
        print(f"[{action_name}] Processing flow: {flow_str}")
        plugin_paths = threading_map(
            lambda plugin_config: clean_one_plugin(config, plugin_config),
            [plugin_config for plugin_group in flow for plugin_config in plugin_group["plugin_group"]],
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
