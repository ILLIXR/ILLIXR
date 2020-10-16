import asyncio
from dataclasses import dataclass
import itertools
import os
import sys
import shlex
import subprocess
import urllib.parse
from pathlib import Path
from typing import Any, Awaitable, Mapping, Optional, Sequence, Tuple, Union, TypeVar, Iterable, List
from tqdm import tqdm


import aiohttp
import aiofiles

# isort util.py
# black --line-length 90 util.py
# mypy --strict --ignore-missing-imports util.py


V = TypeVar("V")
def flatten1(it: Iterable[Iterable[V]]) -> Iterable[V]:
    """Flatten 1 level of iterables"""
    return itertools.chain.from_iterable(it)


def unflatten(it: Iterable[V]) -> Iterable[Iterable[V]]:
    for elem in it:
        yield (elem,)


def replace_all(it: Iterable[V], replacements: Mapping[V, V]) -> Iterable[V]:
    for elem in it:
        yield replacements.get(elem, elem)


def relative_to(path: Path, root: Path) -> Path:
    """Returns a path to `path` relative to `root`

    Postcondition: (root / relative_to(path, root)).resolve() = path.resolve()
    """
    while True:
        try:
            return path.relative_to(root)
        except ValueError:
            return ".." / relative_to(path, (root / "..").resolve())


def escape_string(string: str, escape_seq: str, escapes: Mapping[str, str]) -> str:
    """Escapes each of the escapes in string with escape_seq"""
    string.replace(escape_seq, escape_seq + escape_seq)
    for unsafe_string, safe_string in escapes.items():
        string = string.replace(unsafe_string, escape_seq + safe_string)
    return string


def escape_fname(string: str) -> str:
    """Sanitizes a string so that it can be a filename

    See https://en.wikipedia.org/wiki/Filename#Reserved_characters_and_words
    """

    ret = escape_string(
        string, "%", {"/": "s", "\\": "b", ":": "c"}
    )
    # handle some special cases
    if ret == ".":
        return "%d"
    elif ret == "..":
        return "%d%d"
    elif ret == "~":
        return "%t"
    else:
        return ret

async def pathify(
    url_str: str,
    base: Path,
    cache_path: Path,
    should_exist: bool,
    should_dir: bool,
    session: Optional[aiohttp.ClientSession] = None,
) -> Path:
    """Takes a URL, copies it to disk (if not already there), and returns a local path to it.

Args:
    url_str (str): the URL in question
    base (Path): the base path for resolving relative file paths
    cache_path (Path): the location where this fn will put objects
    should_exist (bool): ensure that the file exists or raise ValueError (applicable to the file: scheme)
    should_dir (bool): expect the url to point to a directory, otherwise a file, and raise ValueError if that expectation is violated
    session (Optional[aiohttp.ClientSession]): aiohttp session to be used if URLs are http/https

Returns:
    Path: a path to the resource on the local disk. Do not modify the file at this path.

Supported schemes:

- `http:` and `https:`
  - Does not support sub-schemes.
  - Caller must pass a aiohttp session.
  - Result is cached
  - Returns files (not directories)

- `zip:`
  - Requires a sub-scheme (e.g. `zip+file:///path/to/archive.zip`)
  - Calls `unzip` on the file returned by the sub-scheme
  - Result is cached
  - Returns directories

- `git:`
  - Supports optional sub-schemes
  - Calls `git clone` on the URL with the sub-scheme
    - Optionally, A branch, tag, or commit hash can be appended to the URL after a `@`.
    - e.g. `git+https://github.com/a/b@v1` calls `git clone -b v1 https://github.com/a/b`
  - Result is cached
  - Inspired by [Pip](https://pip.pypa.io/en/stable/reference/pip_install/#vcs-support)
  - Returns directories

- `file:` (default if no scheme is provided)
  - Supports optional sub-schemes
  - Supports absolute (e.g. file:///path/to/file) and relative (file:path/to/file), where relative paths are resolved relative to `base`
  - Also accepts the path through a `path` fragment argument
    - This way, one can specify a path within a directory retrieved by another scheme (e.g. file+git://github.com/a/b#path=path/within/repo).
  - Returns files or directories

    """

    url = urllib.parse.urlparse(url_str)
    cache_dest = Path(cache_path / escape_fname(url_str))
    first_scheme, _, rest_schemes = url.scheme.partition("+")
    fragment = urllib.parse.parse_qs(url.fragment)

    if first_scheme in set(["http", "https"]) and not rest_schemes:
        if should_dir:
            raise ValueError(
                f"{url} points to a file (because of the http[s] scheme) not a dir"
            )
        elif cache_dest.exists():
            return cache_dest
        elif session is None:
            raise ValueError(f"{url_str} given, so aiohttp.ClientSession expected")
        else:
            async with session.get(url_str) as src:
                async with aiofiles.open(cache_dest, "wb") as dst:
                    await dst.write(await src.read())
            return cache_dest
    elif first_scheme == "file" and rest_schemes and "path" in fragment:
        underlying_fragment = {key: val for key, val in fragment.items() if key != "path"}
        underlying_fragment_str = urllib.parse.urlencode(underlying_fragment, doseq=True)
        dir_url = urllib.parse.urlunparse(
            (rest_schemes, *url[1:5], underlying_fragment_str)
        )
        dir_path = await pathify(dir_url, base, cache_path, True, True, session)
        path_within_dir = Path(fragment["path"][0])
        path = dir_path / path_within_dir
        if should_exist and not path.exists():
            raise ValueError(f"Expected {path_within_dir} to exist within {dir_url}")
        elif path.exists() and (path.is_dir() != should_dir):
            raise ValueError(
                f"{path_within_dir} points to a {'dir' if path.is_dir() else 'file'} "
                f"but a {'dir' if should_dir else 'file'} expected"
            )
        else:
            return path
    elif first_scheme == "zip" and rest_schemes:
        if not should_dir:
            raise ValueError(
                f"{url} points to a dir (because of the zip scheme) not a file"
            )
        elif cache_dest.exists():
            return cache_dest
        else:
            archive_url = urllib.parse.urlunparse((rest_schemes, *url[1:]))
            archive_path = await pathify(
                archive_url, base, cache_path, True, False, session
            )
            await subprocess_run(["unzip", str(archive_path), "-d", str(cache_dest)], check=True, capture_output=True)
            return cache_dest
    elif first_scheme == "git":
        repo_scheme = url.scheme.partition("+")[2]  # could be git+b+c, we want b+c
        repo_url, _, rev = urllib.parse.urlunparse((repo_scheme, *url[1:6])).partition("@")
        if not should_dir:
            raise ValueError(
                f"{url} points to a dir (because of the git scheme) not a file"
            )
        elif cache_dest.exists():
            return cache_dest
        elif not rev:
            await subprocess_run(["git", "clone", repo_url, str(cache_dest), "--recursive"], check=True, capture_output=True)
            return cache_dest
        else:
            repo_path = await pathify(
                f"git+{repo_url}", base, cache_path, True, True, session
            )
            await subprocess_run(["git", "-C", str(repo_path), "fetch"], check=True, capture_output=True)
            await subprocess_run(["git", "-C", str(repo_path), "checkout", rev], check=True, capture_output=True)
            await subprocess_run(["git", "-C", str(repo_path), "submodule", "update", "--recursive"], check=True, capture_output=True)
            return repo_path
    elif first_scheme in set(["file", ""]) and not rest_schemes:
        # no scheme; just a plain path
        path = Path(url.path)
        normed_path = path if path.is_absolute() else Path(base / path)
        if should_exist and not normed_path.exists():
            raise ValueError(f"Expected {normed_path} to exist")
        elif normed_path.exists() and (normed_path.is_dir() != should_dir):
            raise ValueError(
                f"{normed_path} points to a {'dir' if normed_path.is_dir() else 'file'} "
                f"but a {'dir' if should_dir else 'file'} expected"
            )
        else:
            return normed_path
    else:
        raise ValueError(f"Unsupported urlscheme {url.scheme}")


async def gather_aws(*aws: Awaitable[Any], desc: Optional[str] = None, sequential: bool = False) -> Tuple[Any, ...]:
    """Await on all of aws, either sequentially or in parallel"""
    if sequential:
        return tuple([
            await aw
            for aw in tqdm(aws, desc=desc, total=len(aws))
        ])
    else:
        # I have to number all of the aws
        # because asyncio.as_completed returns results out-of-order

        aws_numbered: List[Awaitable[Tuple[int, Any]]] = []
        for i, aw in enumerate(aws):
            # There is no async-lambda :'(
            async def aw2(i=i, aw=aw) -> Tuple[int, Any]:
                return (i, await aw)
            aws_numbered.append(aw2())

        results_numbered = [
            await aw
            for aw in tqdm(
                    asyncio.as_completed(aws_numbered),
                    desc=desc,
                    total=len(aws),
            )
        ]

        results = map(lambda pair: pair[1], sorted(results_numbered, key=lambda pair: pair[0]))
        return tuple(results)



class CalledProcessError(Exception):
    ...


@dataclass
class CompletedProcess:
    args: Sequence[str]
    returncode: int
    stdout: bytes
    stderr: bytes

    def check_returncode(self) -> None:
        if self.returncode != 0:
            print("$ " + shlex.join(self.args))
            sys.stdout.buffer.write(self.stdout)
            sys.stdout.buffer.write(self.stderr)
            print(f"Exit code {self.returncode}")
            raise CalledProcessError(f"{shlex.join(self.args)} returned {self.returncode}")


async def subprocess_run(
    args: Sequence[str],
    cwd: Optional[Union[Path, str]] = None,
    check: bool = False,
    env: Optional[Mapping[str, str]] = None,
    env_override: Optional[Mapping[str, str]] = None,
    capture_output: bool = False,
    silence_output: bool = False,
) -> CompletedProcess:
    """Clone of subprocess.run, but asynchronous.

    This allows concurrency: Launch another process while awaiting this process to complete.

    I will progressively port arguments by need.

    env_override is a mapping used to update (not replace) env.

    """

    stdout_descr: Optional[int] = None
    stderr_descr: Optional[int] = None
    if capture_output:
        stdout_descr = asyncio.subprocess.PIPE
        stderr_descr = asyncio.subprocess.PIPE
    elif silence_output:
        stdout_descr = asyncio.subprocess.DEVNULL
        stderr_descr = asyncio.subprocess.DEVNULL

    cwd = cwd if cwd is not None else Path(".")
    env = dict(env if env is not None else os.environ)
    if env_override:
        env.update(env_override)

    try:
        proc = await asyncio.create_subprocess_exec(
            args[0], *args[1:], env=env, cwd=str(cwd), stdout=stdout_descr, stderr=stderr_descr,
        )
        if capture_output:
            stdout, stderr = await proc.communicate()
            returncode = proc.returncode
        else:
            stdout, stderr = (b"", b"")
            returncode = await proc.wait()
        comp_proc = CompletedProcess(args, returncode or 0, stdout, stderr)
        if check:
            comp_proc.check_returncode()
        return comp_proc
    except asyncio.CancelledError:
        proc.terminate()
        raise
