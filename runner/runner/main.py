#!/usr/bin/env python3
import multiprocessing
import os
import shlex
import json
import tempfile
import getpass
import shutil
from pathlib import Path
import subprocess
import functools
import operator
import zlib
import datetime
from typing import Any, List, Mapping, Optional, ContextManager, BinaryIO, Tuple, cast

import click
import jsonschema
import yaml
from util import (
    fill_defaults,
    flatten1,
    pathify,
    relative_to,
    replace_all,
    set_cpu_freq,
    subprocess_run,
    threading_map,
    unflatten,
    noop_context,
    recursive_setitem,
    invert,
)
from yamlinclude import YamlIncludeConstructor

# isort main.py
# black -l 90 main.py
# mypy --strict --ignore-missing-imports main.py

root_dir = relative_to((Path(__file__).parent / "../..").resolve(), Path(".").resolve())

cache_path = root_dir / ".cache" / "paths"
cache_path.mkdir(parents=True, exist_ok=True)


def make(
    path: Path,
    targets: List[str],
    var_dict: Optional[Mapping[str, str]] = None,
    parallelism: Optional[int] = None,
) -> None:

    if parallelism is None:
        parallelism = max(1, multiprocessing.cpu_count() // 2)

    var_dict_args = shlex.join(
        f"{key}={val}" for key, val in (var_dict if var_dict else {}).items()
    )

    subprocess_run(
        ["make", "-j", str(parallelism), "-C", str(path), *targets, *var_dict_args],
        check=True,
        capture_output=True,
    )


def cmake(
    path: Path, build_path: Path, var_dict: Optional[Mapping[str, str]] = None
) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    var_args = [f"-D{key}={val}" for key, val in (var_dict if var_dict else {}).items()]
    build_path.mkdir(exist_ok=True)
    subprocess_run(
        [
            "cmake",
            "-S",
            str(path),
            "-B",
            str(build_path),
            "-G",
            "Unix Makefiles",
            *var_args,
        ],
        check=True,
        capture_output=True,
    )
    make(build_path, ["all"])


def build_one_plugin(
    config: Mapping[str, Any], plugin_config: Mapping[str, Any], test: bool = False,
) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir, cache_path, True, True)
    if not (path / "common").exists():
        common_path = pathify(
            config["common"]["path"], root_dir, cache_path, True, True
        )
        common_path = common_path.resolve()
        os.symlink(common_path, path / "common")
    plugin_so_name = f"plugin.{profile}.so"
    targets = [plugin_so_name] + (["tests/run"] if test else [])
    make(path, targets, plugin_config["config"])
    return path / plugin_so_name


def build_runtime(config: Mapping[str, Any], suffix: str, test: bool = False,) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    targets = [runtime_name] + (["tests/run"] if test else [])
    make(runtime_path, targets, runtime_config)
    return runtime_path / runtime_name


def write_scheduler_config(config: Mapping[str, Any]) -> Path:
    dag = config["loader"]["scheduler"]
    scheduler = config["conditions"]["scheduler"]
    assert scheduler in {"static", "dynamic"}
    freq = config["conditions"].get("cpu_freq", 5.3)
    expected_ov = (8.33/1.05 - dag["nodes"]["timewarp_gl"][0][freq] - dag["nodes"]["gtsam_integrator"][0][freq] - dag["nodes"]["offline_imu"][0][freq] - dag["nodes"]["gldemo"][0][freq]) * dag["fc"][freq] - dag["nodes"]["offline_cam"][0][freq]
    # assert expected_ov - 0.1 <= dag["nodes"]["open_vins"][0][freq] <= expected_ov + 0.1, expected_ov

    name2id = invert(dict(enumerate([None] + [
        Path(plugin["path"]).name
        for plugin_group in config["plugin_groups"]
        for plugin in plugin_group["plugin_group"]
    ])))

    path = Path(tempfile.gettempdir()) / "illixr_dag.txt"

    lines: str = []
    for node, (ctmap, b, c, d, _) in dag["nodes"].items():
        compute_time = ctmap[freq] if scheduler == "static" else 5.0
        lines.append(f"N {name2id[node]} {compute_time} {b} {c} {d} N")
    for node, (_, _, _, _, neighbors) in dag["nodes"].items():
        if neighbors:
            lines.append(f"E {name2id[node]} {' '.join(str(name2id[neighbor]) for neighbor in neighbors)} X")
    for weight, chain in dag["chains"]:
        if chain:
            lines.append(f"C {weight} {' '.join(str(name2id[node]) for node in chain)} X")
    lines.append({
        0: "",
        1: f"WF {name2id['offline_cam']} {name2id['gldemo']} X",
        2: "",
    }[config["conditions"]["swap"]])

    lines.append("weights")

    if "appendix" in dag:
        lines.extend(dag["appendix"])
    path.write_text("\n".join(lines))
    return path

def load_native(config: Mapping[str, Any], quiet: bool) -> None:
    runtime_exe_path = build_runtime(config, "exe")
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config),
        [
            plugin_config
            for plugin_group in config["plugin_groups"]
            for plugin_config in plugin_group["plugin_group"]
        ],
        desc="Building plugins",
        quiet=quiet,
    )

    actual_cmd_str = config["loader"].get("command", "$full_cmd")

    scheduler = config["conditions"]["scheduler"]
    scheduler_config_path = str(write_scheduler_config(config)) if scheduler in {"static", "dynamic"} else ""

    env_override = dict(
        ILLIXR_DATA=str(data_path),
        ILLIXR_DEMO_DATA=str(demo_data_path),
        KIMERA_ROOT=config["loader"]["kimera_path"],
        ILLIXR_RUN_DURATION=str(config["conditions"]["duration"]),
        ILLIXR_SCHEDULER=scheduler,
        ILLIXR_SCHEDULER_CONFIG=scheduler_config_path,
        ILLIXR_SCHEDULER_FC=config["loader"]["scheduler"]["fc"][config["conditions"]["cpu_freq"]] if scheduler == "static" else 1,
        ILLIXR_TIMEWARP_DELAY=str(int(config["loader"]["scheduler"]["nodes"]["timewarp_gl"][0][config["conditions"]["cpu_freq"]] * 1e6)),
        ILLIXR_SWAP_ORDER=str(config["conditions"].get("swap", "N/A")),
        **config["loader"].get("env", {}),
    )

    echo_prefix = ["echo"] if config["loader"].get("echo", False) else []
    sudo_prefix = ["sudo"] if config["loader"].get("sudo", False) else []
    cpu_freq_prefix = ["python3", "/home/grayson5/.local/bin/cpu_freq", str(config["conditions"]["cpu_freq"])]
    gdb_prefix = ["gdb", "--quiet", "--args"] if config["loader"].get("gdb", False) else []
    cpus = config["conditions"]["cpus"]
    cpus = multiprocessing.cpu_count() if cpus == 0 else cpus
    taskset_prefix = ["taskset", "--all-tasks", "--cpu-list", "0-{cpus-1}"] if "cpu_list" in config["loader"] else []
    illixr_cmd = [str(runtime_exe_path), *map(str, plugin_paths)]
    env_prefix = ["env", "-C", str(Path(".").resolve())] + [f"{var}={val}" for var, val in env_override.items()]

    hash_ = functools.reduce(operator.xor, [
        zlib.crc32(path.read_bytes())
        for path in [runtime_exe_path, *plugin_paths]
    ])

    actual_cmd_list = list(
        flatten1(
            replace_all(
                unflatten(shlex.split(actual_cmd_str)),
                {
                    ("$echo",): echo_prefix,
                    ("$sudo",): sudo_prefix,
                    ("$cpu_freq",): sudo_prefix,
                    ("$gdb",): sudo_prefix,
                    ("$taskset",): taskset_prefix,
                    ("$env",): env_prefix,
                    ("$cmd",): illixr_cmd,
                    ("$quoted_cmd",): [shlex.quote(shlex.join(illixr_cmd))],
                    ("$full_cmd",): echo_prefix + sudo_prefix + cpu_freq_prefix + taskset_prefix + env_prefix + gdb_prefix + illixr_cmd,
                },
            )
        )
    )
    config["conditions"]["hash"] = hash_
    config["info"] = {
        "date": datetime.datetime.now().isoformat(),
        "command":  actual_cmd_list,
        "git-commit": subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, check=True).stdout,
        "git-dirty": subprocess.run(["git", "status", "--porcelain=2"], capture_output=True, check=True).stdout,
    }


    log_stdout_str = config["loader"].get("log_stdout", None)
    log_stdout_ctx: ContextManager[Optional[BinaryIO]] = cast(ContextManager[Optional[BinaryIO]],
        open(log_stdout_str, "wb")
        if log_stdout_str is not None
        else noop_context(None)
    )

    (Path("metrics") / "config.yaml").write_text(yaml.dump(config))

    with log_stdout_ctx as log_stdout:
        subprocess_run(
            actual_cmd_list,
            stdout=log_stdout,
            stderr=subprocess.STDOUT,
        )

    if sudo_prefix:
        subprocess_run([*sudo_prefix, "chown", getpass.getuser(), "-R", "metrics"])


def load_tests(config: Mapping[str, Any]) -> None:
    runtime_exe_path = build_runtime(config, "exe", test=True)
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)
    make(Path("common"), ["tests/run"])
    plugin_paths = threading_map(
        lambda plugin_config: build_one_plugin(config, plugin_config, test=True),
        [
            plugin_config
            for plugin_group in config["plugin_groups"]
            for plugin_config in plugin_group["plugin_group"]
        ],
        desc="Building plugins",
    )
    subprocess_run(
        ["xvfb-run", str(runtime_exe_path), *map(str, plugin_paths)],
        env_override=dict(ILLIXR_DATA=str(data_path), ILLIXR_DEMO_DATA=str(demo_data_path), ILLIXR_RUN_DURATION="10"),
    )


def load_monado(config: Mapping[str, Any]) -> None:
    profile = config["profile"]
    cmake_profile = "Debug" if profile == "dbg" else "Release"
    openxr_app_config = config["loader"]["openxr_app"].get("config", {})
    monado_config = config["loader"]["monado"].get("config", {})

    runtime_path = pathify(config["runtime"]["path"], root_dir, cache_path, True, True)
    monado_path = pathify(
        config["loader"]["monado"]["path"], root_dir, cache_path, True, True
    )
    openxr_app_path = pathify(
        config["loader"]["openxr_app"]["path"], root_dir, cache_path, True, True
    )
    data_path = pathify(config["data"], root_dir, cache_path, True, True)
    demo_data_path = pathify(config["demo_data"], root_dir, cache_path, True, True)

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
        [
            plugin_config
            for plugin_group in config["plugin_groups"]
            for plugin_config in plugin_group["plugin_group"]
        ],
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
        ),
    )


loaders = {
    "native": load_native,
    "monado": load_monado,
    "tests": load_tests,
}


def run_config(config_path: Path, overrides: List[Tuple[Tuple[str], str]], quiet: bool) -> None:
    """Parse a YAML config file, returning the validated ILLIXR system config."""
    YamlIncludeConstructor.add_to_loader_class(
        loader_class=yaml.FullLoader, base_dir=config_path.parent,
    )

    with config_path.open() as f:
        config = yaml.full_load(f)

    with (root_dir / "runner/config_schema.yaml").open() as f:
        config_schema = yaml.safe_load(f)

    jsonschema.validate(instance=config, schema=config_schema)
    fill_defaults(config, config_schema)

    for key, val in overrides:
        recursive_setitem(config, key, json.loads(val))

    loader = config["loader"]["name"]

    metrics = Path("metrics")
    if metrics.exists():
        shutil.rmtree(metrics)
    metrics.mkdir()

    if loader not in loaders:
        raise RuntimeError(f"No such loader: {loader}")

    loaders[loader](config, quiet)

if __name__ == "__main__":

    @click.command()
    @click.argument("config_path", type=click.Path(exists=True))
    @click.option("--overrides", multiple=True, type=str)
    @click.option("--quiet/--verbose", default=False, type=bool)
    def main(config_path: str, overrides: List[str], quiet: bool) -> None:
        overrides = [
            (tuple(key.split(".")), val)
            for key, val in [override.split("=") for override in overrides]
        ]
        run_config(Path(config_path), overrides, quiet)

    main()
