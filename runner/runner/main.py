#!/usr/bin/env python3
import contextlib
import io
import multiprocessing
import os
import shlex
import subprocess
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
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    make(path, targets, plugin_config["config"], env_override=env_override)
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
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
    make(path, targets, plugin_config["config"], env_override=env_override)

    return path / plugin_so_name


def build_runtime(
    config: Mapping[str, Any],
    suffix: str,
    test: bool = False,
) -> Path:
    profile        : str               = config["profile"]
    name           : str               = "main" if suffix == "exe" else "plugin"
    runtime_name   : str               = f"{name}.{profile}.{suffix}"
    runtime_config : Mapping[str, Any] = config["runtime"]["config"]
    runtime_path   : Path              = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    targets        : List[str]         = [runtime_name] + (["tests/run"] if test else [])
    env_override: Mapping[str, str] = dict(ILLIXR_INTEGRATION="yes")
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
            REALSENSE_CAM=str(realsense_cam_string),
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
    realsense_cam_string = config["realsense_cam"]

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
            ILLIXR_ENABLE_PRE_SLEEP=str(config["enable_pre_sleep"]),
            KIMERA_ROOT=config["action"]["kimera_path"],
            REALSENSE_CAM=str(realsense_cam_string),
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


def find_pkg_deps(path_pkg_deps: Path) -> List[str]:
    with path_pkg_deps.open() as fobj_pkg_deps:
        ## Grab the list of package depencencies by reading 'pkg-deps.txt` for this plugin
        str_pkg_deps: str = fobj_pkg_deps.readline()

    if str_pkg_deps:
        ## For plugins with dependencies, emit include paths from pkg-config with `--cflags`
        cmd_pkg_config: List[str] = ["pkg-config", "--cflags"] + str_pkg_deps.split()
        stdout_pkg_include: str = subprocess.check_output(cmd_pkg_config).decode("utf-8").strip()
        return stdout_pkg_include.split()
    else:
        ## For plugins with no dependencies, emit an empty list of include paths
        return list()


def delint_files(
    paths_file: List[Path],
    paths_filter_out: List[Path],
    exe_linter: str,
    exe_formatter: str,
    enable_fix: bool,
    str_cpp_std: str,
    paths_pkg_include: List[str],
) -> None:
    def path_to_json(path: Path) -> str:
        return '{"name":"%s","lines":[[9999999,9999999]]}' % str(path)

    arg_fix: List[str] = ["--fix", "--fix-errors"] if enable_fix else list()
    objs_filter: List[str] = [path_to_json(path) for path in paths_filter_out] + ['{"name":".hpp"}', '{"name":".cpp"}']
    arg_filter: str = "--line-filter=[" + ",".join(objs_filter) + "]"
    arg_files: List[str] = [str(path_file) for path_file in paths_file]
    arg_sep: str = "--"
    arg_std: str = "-std=" + str_cpp_std
    arg_limit: str = "-ferror-limit=0"
    cmd_delint: List[str] = [exe_linter] + arg_fix + arg_files + [arg_sep, arg_std, arg_limit] + paths_pkg_include

    subprocess_run(cmd_delint, check=True, capture_output=False)

    if enable_fix:
        ## If `enable_fix` is on, also apply in-place formatting
        arg_inplace: str = "-i"
        arg_style: str = "--style=file"
        cmd_format: List[str] = [exe_formatter, arg_inplace, arg_style] + arg_files

        subprocess_run(cmd_format, check=True, capture_output=False)


def delint_one_plugin(
    config: Mapping[str, Any],
    plugin_config: Mapping[str, Any],
    paths_filter_out: List[Path],
    exe_linter: str,
    exe_formatter: str,
    enable_fix: bool,
    str_cpp_std: str,
) -> Path:
    path_plugin: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)

    if not path_plugin.is_dir():
        raise RuntimeError("[delint_one_plugin] Directory '%s' does not exist" % str(path_plugin))

    paths_pkg_include: List[str] ## Forward declaration of type for `paths_pkg_include`
    if str(plugin_config["path"]) != "common/":
        ## Kludge: If the plugin being processed is not `common`, emit `common` to the include paths
        path_common_sym: Path = path_plugin / "common"

        if not path_common_sym.is_symlink():
            raise RuntimeError("[delint_one_plugin] File '%s' does not exist" % str(path_common_sym))

        path_common: Path = path_common_sym.resolve()
        paths_pkg_include = ["-I" + str(path_common)]
    else:
        ## Kludge: If the plugin being processed is `common`, emit an empty list of include paths
        paths_pkg_include = list()

    path_pkg_deps: Path = path_plugin / "pkg-deps.txt"
    paths_pkg_include.extend(find_pkg_deps(path_pkg_deps))

    glob_hpp: str = "**/*.hpp"
    glob_cpp: str = "**/*.cpp"

    paths_file_hpp: List[Path] = [path for path in path_plugin.glob(glob_hpp)]
    paths_file_cpp: List[Path] = [path for path in path_plugin.glob(glob_cpp)]
    paths_file: List[Path] = paths_file_hpp + paths_file_cpp

    delint_files(paths_file, paths_filter_out, exe_linter, exe_formatter, enable_fix, str_cpp_std, paths_pkg_include)

    return path_plugin


def delint_code(config: Mapping[str, Any]) -> None:
    path_lint_file: Path = relative_to(Path(config["action"]["lint_file"]), root_dir)
    path_format_file: Path = relative_to(Path(config["action"]["format_file"]), root_dir)

    if not path_lint_file.is_file():
        raise RuntimeError("[delint_code] Lint file '%s' not found" % str(path_lint_file))
    if not path_format_file.is_file():
        raise RuntimeError("[delint_code] Format file '%s' not found" % str(path_format_file))

    exe_linter: str = config["action"]["linter"]
    exe_formatter: str = config["action"]["formatter"]
    str_filter_out: str = config["action"]["filter_out"]
    enable_fix: bool = bool(config["action"]["enable_fix"])
    str_cpp_std: str = config["action"]["cpp_std"]
    target_file: str = config["action"]["target_file"]
    target_pkg_deps: str = config["action"]["target_pkg_deps"]

    paths_filter_out: List[Path] ## Forward declaration of type for `paths_filter_out`
    if str_filter_out:
        paths_filter_out = [relative_to(Path(str_path), root_dir) for str_path in str_filter_out.split(",")]
        # for path_filter_out in paths_filter_out:
        #    if not path_filter_out.is_file():
        #        raise RuntimeError("[delint_code] Source file '%s' not found" % str(path_filter_out))
    else:
        paths_filter_out = list()

    if target_file and target_pkg_deps:
        ## If a target source file and its `pkg-deps.txt` file are specified, only delint that source file
        path_target_file: Path = relative_to(Path(target_file), root_dir)
        path_target_pkg_deps: Path = relative_to(Path(target_pkg_deps), root_dir)

        if not path_target_file.is_file():
            raise RuntimeError("[delint_code] Target file '%s' not found" % str(path_target_file))
        if not path_target_pkg_deps.is_file():
            raise RuntimeError("[delint_code] Target file '%s' not found" % str(path_target_pkg_deps))

        paths_pkg_include: List[str] = find_pkg_deps(path_target_pkg_deps)
        delint_files(
            [path_target_file], paths_filter_out, exe_linter, exe_formatter, enable_fix, str_cpp_std, paths_pkg_include
        )
    else:
        ## Otherwise, delint all plugins in `delint.yaml`
        plugin_paths = threading_map(
            lambda plugin_config: delint_one_plugin(
                config, plugin_config, paths_filter_out, exe_linter, exe_formatter, enable_fix, str_cpp_std
            ),
            [plugin_config for plugin_group in config["plugin_groups"] for plugin_config in plugin_group["plugin_group"]],
            desc="Delinting plugins",
        )


actions = {
    "native": load_native,
    "monado": load_monado,
    "tests": load_tests,
    "clean": clean_project,
    "docs": make_docs,
    "delint": delint_code,
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
