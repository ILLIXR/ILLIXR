#!/usr/bin/env python3
import asyncio
import multiprocessing
import os
import subprocess
from pathlib import Path
import shlex
from typing import Any, Dict, List, Optional, cast, Union
import urllib.parse

import click
import jsonschema  # type: ignore
import yaml
from yamlinclude import YamlIncludeConstructor  # type: ignore

# isort main.py
# black main.py
# mypy --strict main.py


def relative_to(path: Path, root: Path):
    while True:
        try:
            return path.relative_to(root)
        except ValueError:
            return ".." / relative_to(path, (root / "..").resolve())

root_dir = relative_to((Path(__file__).parent / "../..").resolve(), Path(".").resolve())


def fill_defaults(thing: Any, thing_schema: Dict[str, Any], path: Optional[List[str]] = None) -> None:
    if path is None:
        path = []

    if thing_schema["type"] == "object":
        for key in thing_schema.get("properties", []):
            if key not in thing:
                if "default" in thing_schema["properties"][key]:
                    thing[key] = thing_schema["properties"][key]["default"]
                    # print(f'{".".join(path + [key])} is defaulting to {thing_schema["properties"][key]["default"]}')
            # even if key is present, it may be incomplete
            fill_defaults(thing[key], thing_schema["properties"][key], path + [key])
    elif thing_schema["type"] == "array":
        for i, item in enumerate(thing):
            fill_defaults(item, thing_schema["items"], path + [str(i)])


def pathify(path: Union[Path, str], base: Union[Path, str], should_exist=True):
    """If path is relative, resolve it relative to base. Also expands git/zip paths."""

    # TODO(git urls):If path2 looks like a git url
    # Git URLs could be used the config file, handled here, and the rest of this script doesn't care.
    # I suggest: 
    # If it exists in ~/.cache/illixr/github.com/username/repo/rev, 
    #   If it hasn't been pulled in a while, do a git pull (to update)
    #   This is important because it is persistent.
    #    Subsequent uses of the same repo will not have to get re-cloned and likely not re-pulled.
    # Otherwise a git clone to that dir.
    # Return a path within that repo.
    path2 = path if isinstance(path, Path) else Path(path)
    bas2 = base if isinstance(base, Path) else Path(base)
    ret = path2 if path2.is_absolute() else base / path2
    if should_exist and not ret.exists():
        raise ValueError(f"{ret} does not exist")
    return ret


async def gather_aws(*aws, sync: bool = False):
    if sync:
        return [
            await aw
            for aw in aws
        ]
    else:
        return await asyncio.gather(*aws)


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

    try:
        cwd = cwd if cwd is not None else Path(".")
        env = env if env is not None else os.environ
        proc = await asyncio.create_subprocess_exec(
            args[0], *args[1:], env=env, cwd=str(cwd)
        )

        return_code = await proc.wait()
        if check and return_code != 0:
            raise subprocess.CalledProcessError(return_code, cmd=shlex.join(args))
        return proc
    except asyncio.CancelledError:
        proc.terminate()
        raise


async def make(path: Path, targets: List[str], var_dict: Optional[Dict[str, str]] = None) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    var_dict_args = [f"{key}={val}" for key, val in (var_dict if var_dict else {}).items()]
    await subprocess_run(
        ["make", "-j", str(parallelism), "-C", str(path), *targets, *var_dict_args],
        check=True,
    )


async def build_one_plugin(
        config: Dict[str, Any], plugin_config: Dict[str, Any], test: bool = False,
) -> Path:
    profile = config["profile"]
    path: Path = pathify(plugin_config["path"], root_dir)
    var_dict = plugin_config["config"]
    so_name = f"plugin.{profile}.so"
    targets = [so_name] + (["tests/run"] if test else [])
    await make(path, targets, var_dict)
    return path / so_name


async def build_runtime(config: Dict[str, Any], suffix: str, test: bool = False) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path: Path = pathify(config["runtime"]["path"], root_dir)
    if not runtime_path.exists():
        raise RuntimeError(
            f"Please change loader.runtime.path ({runtime_path}) to point to a clone of https://github.com/ILLIXR/ILLIXR"
        )
    targets = [runtime_name] + (["tests/run"] if test else [])
    await make(runtime_path / "runtime", targets, runtime_config)
    return runtime_path / "runtime" / runtime_name


async def load_native(config: Dict[str, Any]) -> None:
    runtime_exe_path, plugin_paths = await gather_aws(
        build_runtime(config, "exe"),
        gather_aws(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config["plugins"]
            )
        ),
    )
    await subprocess_run(
        [str(runtime_exe_path), *map(str, plugin_paths)],
        check=True,
        env=dict(
            ILLIXR_DATA=config["data"],
            **os.environ,
        ),
    )


async def load_tests(config: Dict[str, Any]) -> None:
    runtime_exe_path, _, plugin_paths = await gather_aws(
        build_runtime(config, "exe", test=True),
        make(Path("../common"), ["tests/run"]),
        gather_aws(
            *(
                build_one_plugin(config, plugin_config, test=True)
                for plugin_config in config["plugins"]
            ),
            sync=False,
        ),
        sync=False,
    )


async def load_gdb(config: Dict[str, Any]) -> None:
    runtime_exe_path, plugin_paths = await gather_aws(
        build_runtime(config, "exe"),
        gather_aws(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config["plugins"]
            )
        ),
    )
    await subprocess_run(
        ["gdb", "-q", "--args", str(runtime_exe_path), *map(str, plugin_paths),],
        check=True,
        env=dict(
            ILLIXR_DATA=config["data"],
            **os.environ,
        ),
    )


async def cmake(
    path: Path, build_path: Path, var_dict: Optional[Dict[str, str]] = None
) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    var_args = [f"-D{key}={val}" for key, val in (var_dict if var_dict else {}).items()]
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
            *var_args,
        ],
        check=True,
    )
    await make(build_path, ["all"])


async def load_monado(config: Dict[str, Any]) -> None:
    profile = config["profile"]
    cmake_profile = "Debug" if profile == "dbg" else "Release"

    runtime_path: Path = pathify(config["runtime"]["path"], root_dir)

    monado_config = config["loader"]["monado"].get("config", {})
    monado_path = pathify(config["loader"]["monado"]["path"], root_dir)

    openxr_app_config = config["loader"]["openxr_app"].get("config", {})
    openxr_app_path = pathify(config["loader"]["openxr_app"]["path"], root_dir)

    _, _, _, plugin_paths = await gather_aws(
        cmake(
            monado_path,
            monado_path / "build",
            dict(
                CMAKE_BUILD_TYPE=cmake_profile,
                BUILD_WITH_LIBUDEV=0,
                BUILD_WITH_LIBUVC=0,
                BUILD_WITH_LIBUSB=0,
                BUILD_WITH_NS=0,
                BUILD_WITH_PSMV=0,
                BUILD_WITH_PSVR=0,
                BUILD_WITH_OPENHMD=0,
                BUILD_WITH_VIVE=0,
                **monado_config,
            ),
        ),
        cmake(
            openxr_app_path,
            openxr_app_path / "build",
            dict(CMAKE_BUILD_TYPE=cmake_profile, **openxr_app_config),
        ),
        build_runtime(config, "so"),
        gather_aws(
            *(
                build_one_plugin(config, plugin_config)
                for plugin_config in config.get("plugins", [])
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
            ILLIXR_DATA=config["data"],
            **os.environ,
        ),
    )


loaders = {
    "native": load_native,
    "gdb": load_gdb,
    "monado": load_monado,
    "tests": load_tests,
}


async def run_config(config_path: Path) -> None:
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
    await loaders[loader](config)


if __name__ == "__main__":

    @click.command()
    @click.argument("config_path", type=click.Path(exists=True))
    def main(config_path: str) -> None:
        asyncio.run(run_config(Path(config_path)))

    main()
