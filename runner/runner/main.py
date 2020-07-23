#!/usr/bin/env python3
import asyncio
import multiprocessing
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional, cast, Union

import click
import jsonschema  # type: ignore
import yaml
from yamlinclude import YamlIncludeConstructor  # type: ignore

# isort main.py
# black main.py
# mypy --strict main.py


root_dir = Path(__file__).resolve().parent.parent.parent

with (root_dir / "runner/config_schema.yaml").open() as f:
    config_schema = yaml.safe_load(f)


async def subprocess_run(
    args: List[str],
    cwd: Optional[Union[Path, str]] = None,
    check: bool = False,
    env: Optional[Dict[str, str]] = None,
) -> asyncio.subprocess.Process:
    """Clone of subprocess.run, but asynchronous.

    This allows concurrency: Launch another process while awaiting this process to complete.

    I will progressively port arguments by need.

    """

    proc = await asyncio.create_subprocess_exec(args[0], *args[1:], env=env, cwd=str(cwd))

    return_code = await proc.wait()
    if check and return_code != 0:
        raise subprocess.CalledProcessError(return_code, cmd=args)

    return proc


def parse_validate_config(config_file: Path) -> Dict[str, Any]:
    """Parse a YAML config file, returning the validated ILLIXR system config."""
    YamlIncludeConstructor.add_to_loader_class(
        loader_class=yaml.FullLoader, base_dir=config_file.parent,
    )

    with config_file.open() as file_:
        config = yaml.full_load(file_)

    return config


async def make(path: Path, target: str, vars: Optional[Dict[str, str]] = None) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    vars_args = [f"{key}={val}" for key, val in (vars if vars else {}).items()]
    await subprocess_run(
        ["make", "-j", str(parallelism), "-C", str(path), target, *vars_args], check=True,
    )


async def build_one_plugin(
    config: Dict[str, Any], plugin_config: Dict[str, Any]
) -> Path:
    profile = config.get("profile", "dbg")
    path: Optional[Path] = plugin_config.get("path", None)
    if not path:
        raise ValueError("Path not given for plugin")
    vars = plugin_config.get("config", {})
    so_name = f"plugin.{profile}.so"
    await make(path, so_name, vars)
    return path / so_name


async def build_runtime(config: Dict[str, Any], suffix: str) -> Path:
    profile = config.get("profile", "dbg")
    runtime_name = f"main.{profile}.{suffix}"
    runtime_config = config.get("runtime", {}).get("config", {})
    runtime_path: Path = config.get("runtime", {}).get("path", root_dir)
    if not runtime_path.exists():
        raise RuntimeError(
            f"Please change loader.runtime.path ({runtime_path}) to point to a clone of https://github.com/ILLIXR/ILLIXR"
        )
    await make(runtime_path / "runtime", runtime_name, runtime_config)
    return runtime_path / "runtime" / runtime_name


async def load_native(config: Dict[str, Any]) -> None:
    runtime_exe_path, plugin_paths = await asyncio.gather(
        build_runtime(config, "exe"),
        asyncio.gather(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config.get("plugin", [])
            )
        ),
    )
    await subprocess_run(
        [str(runtime_exe_path), *map(str, plugin_paths)], check=True,
    )


async def load_gdb(config: Dict[str, Any]) -> None:
    runtime_exe_path, plugin_paths = await asyncio.gather(
        build_runtime(config, "exe"),
        asyncio.gather(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config.get("plugin", [])
            )
        ),
    )
    await subprocess_run(
        ["gdb", "-q", "--args", str(runtime_exe_path), *map(str, plugin_paths),], check=True,
    )


async def cmake(
    path: Path, build_path: Path, vars: Optional[Dict[str, str]] = None
) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    arg_vars = [f"-D{key}={val}" for key, val in (vars if vars else {}).items()]
    build_path.mkdir(exist_ok=True)
    await subprocess_run(
        [
            "cmake",
            "-S",
            str(path),
            "-B",
            str(build_path),
            "-G",
            "Unix Makefiles",
            *arg_vars,
        ],
        check=True,
    )
    await make(build_path, "all", {})


async def load_monado(config: Dict[str, Any]) -> None:
    profile = config.get("profile", "dbg")
    cmake_profile = "Debug" if profile == "dbg" else "Release"

    runtime_path: Path = config["loader"].get("runtime", {}).get("path", root_dir)

    monado_config = config["loader"].get("monado", {}).get("config", {})
    monado_path = (
        config["loader"]
        .get("monado", {})
        .get("path", root_dir / "../monado_integration")
    )
    if not monado_path.exists():
        raise RuntimeError(
            f"Please change loader.monado.path ({monado_path}) to point to a clone of https://github.com/ILLIXR/monado_integration"
        )

    openxr_app_config = config["loader"].get("openxr_app", {}).get("config", {})
    openxr_app_path = (
        config["loader"]
        .get("openxr_app", {})
        .get("path", root_dir / "../Monado_OpenXR_Simple_Example")
    )
    if not openxr_app_path.exists():
        raise RuntimeError(
            f"Please change loader.openxr.app_path ({openxr_app_path}) to point to an OpenXR app such as https://github.com/ILLIXR/Monado_OpenXR_Simple_Example/"
        )

    _, _, _, plugin_paths = await asyncio.gather(
        cmake(
            monado_path,
            monado_path / "build",
            dict(CMAKE_BUILD_TYPE=cmake_profile, **monado_config),
        ),
        cmake(
            openxr_app_path,
            openxr_app_path / "build",
            dict(CMAKE_BUILD_TYPE=cmake_profile, **openxr_app_config),
        ),
        build_runtime(config, "so"),
        asyncio.gather(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config.get("plugin", [])
            )
        ),
    )
    await subprocess_run(
        [str(openxr_app_path / "build" / "./openxr-example")],
        check=True,
        env=dict(
            XR_RUNTIME_JSON=str(monado_path / "build" / "openxr_monado-dev.json"),
            ILLIXR_PATH=str(runtime_path),
            ILLIXR_COMP=":".join(map(str, plugin_paths)),
        ),
    )


loaders = {
    "native": load_native,
    "gdb": load_gdb,
    "monado": load_monado,
}


async def run_config(config: Dict[str, Any]) -> None:
    """Compile and run a given ILLIXR system config."""
    loader = config.get("loader", {}).get("name", "native")
    if loader not in loaders:
        raise RuntimeError(f"No such loader: {loader}")
    await loaders[loader](config)


if __name__ == '__main__':
    @click.command()
    @click.argument("config", type=click.Path(exists=True))
    def main(config: Path) -> None:
        asyncio.run(run_config(parse_validate_config(Path(config))))
    main()
