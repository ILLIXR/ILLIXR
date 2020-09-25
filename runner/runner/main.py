#!/usr/bin/env python3
import asyncio
import multiprocessing
import tempfile
from pathlib import Path
import shlex
from typing import Any, Dict, List, Optional, cast

import aiohttp
import click
import jsonschema  # type: ignore
import yaml
from util import flatten1, gather_aws, pathify, relative_to, subprocess_run, replace_all, unflatten
from yamlinclude import YamlIncludeConstructor  # type: ignore

# isort main.py
# black -l 90 main.py
# mypy --strict main.py

root_dir = relative_to((Path(__file__).parent / "../..").resolve(), Path(".").resolve())


cache_path = Path(tempfile.tempdir if tempfile.tempdir else "/tmp") / "ILLIXR" / "cache"
cache_path.mkdir(parents=True, exist_ok=True)


def fill_defaults(
    thing: Any, thing_schema: Dict[str, Any], path: Optional[List[str]] = None
) -> None:
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


async def make(
    path: Path, targets: List[str], var_dict: Optional[Dict[str, str]] = None
) -> None:
    parallelism = max(1, multiprocessing.cpu_count() // 2)
    var_dict_args = [
        f"{key}={val}" for key, val in (var_dict if var_dict else {}).items()
    ]
    await subprocess_run(
        ["make", "-j", str(parallelism), "-C", str(path), *targets, *var_dict_args],
        check=True,
    )


async def build_one_plugin(
    config: Dict[str, Any],
    plugin_config: Dict[str, Any],
    test: bool = False,
    session: Optional[aiohttp.ClientSession] = None,
) -> Path:
    profile = config["profile"]
    path: Path = await pathify(plugin_config["path"], root_dir, cache_path, True, True, session)
    var_dict = plugin_config["config"]
    so_name = f"plugin.{profile}.so"
    targets = [so_name] + (["tests/run"] if test else [])
    await make(path, targets, var_dict)
    return path / so_name


async def build_runtime(
    config: Dict[str, Any],
    suffix: str,
    test: bool = False,
    session: Optional[aiohttp.ClientSession] = None,
) -> Path:
    profile = config["profile"]
    name = "main" if suffix == "exe" else "plugin"
    runtime_name = f"{name}.{profile}.{suffix}"
    runtime_config = config["runtime"]["config"]
    runtime_path: Path = await pathify(
        config["runtime"]["path"], root_dir, cache_path, True, True, session
    )
    if not runtime_path.exists():
        raise RuntimeError(
            f"Please change loader.runtime.path ({runtime_path}) to point to a clone of https://github.com/ILLIXR/ILLIXR"
        )
    targets = [runtime_name] + (["tests/run"] if test else [])
    await make(runtime_path / "runtime", targets, runtime_config)
    return runtime_path / "runtime" / runtime_name


async def load_native(config: Dict[str, Any]) -> None:
    async with aiohttp.ClientSession() as session:
        runtime_exe_path, plugin_paths = await gather_aws(
            build_runtime(config, "exe", session=session),
            gather_aws(
                *(
                    build_one_plugin(config, plugin_config, session=session)
                    for plugin_config in config["plugins"]
                )
            ),
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
    await subprocess_run(
        command_lst_sbst, check=True, env_override=dict(ILLIXR_DATA=config["data"],),
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

    async with aiohttp.ClientSession() as session:

        runtime_path: Path = await pathify(
            config["runtime"]["path"], root_dir, cache_path, True, True, session
        )

        monado_config = config["loader"]["monado"].get("config", {})
        monado_path = await pathify(
            config["loader"]["monado"]["path"], root_dir, cache_path, True, True, session
        )

        openxr_app_config = config["loader"]["openxr_app"].get("config", {})
        openxr_app_path = await pathify(
            config["loader"]["openxr_app"]["path"],
            root_dir,
            cache_path,
            True,
            True,
            session,
        )

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
        env_override=dict(
            XR_RUNTIME_JSON=str(monado_path / "build" / "openxr_monado-dev.json"),
            ILLIXR_PATH=str(runtime_path / "runtime" / f"plugin.{profile}.so"),
            ILLIXR_COMP=":".join(map(str, plugin_paths)),
            ILLIXR_DATA=config["data"],
        ),
    )


loaders = {
    "native": load_native,
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
