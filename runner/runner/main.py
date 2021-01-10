#!/usr/bin/env python3
import multiprocessing
import os
import shlex
import tempfile
from pathlib import Path
from typing import Any, List, Mapping, Optional

import click
import jsonschema
import yaml
from util import (
    fill_defaults,
    flatten1,
    pathify,
    relative_to,
    replace_all,
    subprocess_run,
    threading_map,
    unflatten,
    make,
    cmake,
)
from yamlinclude import YamlIncludeConstructor

# isort main.py
# black -l 90 main.py
# mypy --strict --ignore-missing-imports main.py

root_dir = relative_to((Path(__file__).parent / "../..").resolve(), Path(".").resolve())

cache_path = root_dir / ".cache" / "paths"
cache_path.mkdir(parents=True, exist_ok=True)


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


def load_native(config: Mapping[str, Any]) -> None:
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
    )
    command_str = config["loader"].get("command", "%a")
    main_cmd_lst = [str(runtime_exe_path), *map(str, plugin_paths)]
    command_lst_sbst = list(
        flatten1(
            replace_all(
                unflatten(shlex.split(command_str)),
                {("%a",): main_cmd_lst, ("%b",): [shlex.quote(shlex.join(main_cmd_lst))]},
            )
        )
    )
    subprocess_run(
        command_lst_sbst,
        env_override=dict(
            ILLIXR_DATA=str(data_path),
            ILLIXR_DEMO_DATA=str(demo_data_path),
            ILLIXR_RUN_DURATION=str(config["loader"].get("ILLIXR_RUN_DURATION", 60)),
            KIMERA_ROOT=config["loader"]["kimera_path"],
        ),
        check=True,
    )


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
        env_override=dict(
            ILLIXR_DATA=str(data_path),
            ILLIXR_DEMO_DATA=str(demo_data_path),
            ILLIXR_RUN_DURATION=str(config["loader"].get("ILLIXR_RUN_DURATION", 10)),
            KIMERA_ROOT=config["loader"]["kimera_path"],
        ),
        check=True,
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
        check=True,
    )


loaders = {
    "native": load_native,
    "monado": load_monado,
    "tests": load_tests,
}


def run_config(config_path: Path) -> None:
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

    loader = config["loader"]["name"]

    if loader not in loaders:
        raise RuntimeError(f"No such loader: {loader}")
    loaders[loader](config)


if __name__ == "__main__":

    @click.command()
    @click.argument("config_path", type=click.Path(exists=True))
    def main(config_path: str) -> None:
        run_config(Path(config_path))

    main()
