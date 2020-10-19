from __future__ import annotations

from dataclasses import dataclass
import itertools
import multiprocessing
import operator
import os
import sys
import shlex
import subprocess
import urllib.parse
from pathlib import Path
import queue
import threading
from typing import Any, Awaitable, Mapping, Optional, Sequence, Tuple, Union, TypeVar, Iterable, List, Callable


import aiohttp
import aiofiles
from tqdm import tqdm

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
            

@dataclass
class CalledProcessError:
    args: List[str]
    cwd: Union[Path, str]
    env: Mapping[str, str]
    stdout: bytes
    stderr: bytes
    returncode: int

    def __str__(self):
        env_str = shlex.join("f{key}={val}" for key, val in env.items())
        return """'{shlex.join(self.args)}' returned non-zero exit status {self.returncode}.
Full command:
  env -C {shlex.quote(cwd)} - {env_str} {shlex.join(self.args)}
stdout:
{textwrap.indent(self.stdout.decode(), "  ")}
stderr:
{textwrap.indent(self.stderr.decode(), "  ")}
"""
        

def subprocess_run(
    args: Sequence[str],
    cwd: Optional[Union[Path, str]] = None,
    check: bool = False,
    env: Optional[Mapping[str, str]] = None,
    env_override: Optional[Mapping[str, str]] = None,
    capture_output: bool = False,
) -> subprocess.CompletedProcess:
    """Wrapper around of subprocess.run.

    I will progressively port arguments by need.

    env_override is a mapping used to update (not replace) env.

    If the subprocess's returns non-zero and the return code is
    checked (`subprocess_run(..., check=True)`), all captured output
    is dumped.

    """

    env = dict(env if env is not None else os.environ)
    cwd = cwd if cwd is not None else Path()
    if env_override:
        env.update(env_override)

    proc = subprocess.run(args, env=env, cwd=cwd, capture_output=capture_output)

    if check:
        if proc.returncode != 0:
            raise CalledProcessError(args, cwd, env, proc.stdout, proc.stderr, proc.returncode)
    return proc


T = TypeVar("T")
def chunker(it: Iterable[T], size: int) -> Iterable[List[T]]:
    '''chunk input into size or less chunks
shamelessly swiped from Lib/multiprocessing.py:Pool._get_task'''
    it = iter(it)
    while True:
        x = list(itertools.islice(it, size))
        if not x:
            return
        yield x


def threading_imap_unordered(func: Callable[[T], V], iterable: Iterable[T], chunksize: int = 1, desc: Optional[str] = None, length_hint: Optional[int] = 0) -> Iterable[V]:
    """Clone of multiprocessing.imap_unordered for threads with tqdm for progress

    `desc` is an optional label for the progress bar.

    If the length cannot be determined by operator.length_hint, `length_hint` will be used. If it is None, we fallback to tqdm without a `total`.
    """
    results: queue.Queue[V] = queue.Queue(maxsize=operator.length_hint(iterable))

    def worker(chunk: List[T], results: queue.Queue[V]) -> None:
        for elem in chunk:
            results.put(func(elem))

    threads = [
        threading.Thread(target=worker, args=(chunk, results))
        for chunk in chunker(iterable, chunksize)
    ]

    for thread in threads:
        thread.start()

    def get_results(results: queue.Queue[V]) -> Iterable[V]:
        while any(thread.is_alive() for thread in threads):
            try:
                result = results.get(timeout=0.5)
            except queue.Empty:
                pass
            else:
                tqdm.write(str(result))
                yield result

    results_list = list(tqdm(
        get_results(results),
        total=operator.length_hint(iterable, length_hint),
        desc=desc,
    ))

    for thread in threads:
        thread.join()

    return results_list


def threading_map(func: Callable[[T], V], iterable: Sequence[T], chunksize: int = 1, desc: Optional[str] = None, length_hint: Optional[int] = 0) -> Sequence[V]:
    """Clone of multiprocessing.map for threads with tqdm for progress

    `desc` is an optional label for the progress bar.

    If the length cannot be determined by operator.length_hint, `length_hint` will be used. If it is None, we fallback to tqdm without a `total`.
    """

    return map(
        lambda tupl: tupl[1],
        sorted(
            threading_imap_unordered(
                lambda tupl: (tupl[0], func(tupl[1])),
                list(enumerate(iterable)),
                chunksize=chunksize,
                length_hint=operator.length_hint(iterable, length_hint),
            ),
        )
    )


def fill_defaults(
    thing: Any, thing_schema: Mapping[str, Any], path: Optional[List[str]] = None
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

def pathify(
    url_str: str,
    base: Path,
    cache_path: Path,
    should_exist: bool,
    should_dir: bool,
) -> Path:
    """Takes a URL, copies it to disk (if not already there), and returns a local path to it.

Args:
    url_str (str): the URL in question
    base (Path): the base path for resolving relative file paths
    cache_path (Path): the location where this fn will put objects
    should_exist (bool): ensure that the file exists or raise ValueError (applicable to the file: scheme)
    should_dir (bool): expect the url to point to a directory, otherwise a file, and raise ValueError if that expectation is violated

Returns:
    Path: a path to the resource on the local disk. Do not modify the file at this path.

Supported schemes:

- `http:` and `https:`
  - Does not support sub-schemes.
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
        else:
            with urllib.requests.get(url_str) as src:
                with open(cache_dest, "wb") as dst:
                    shutil.copyfileobj(src, dst)
            return cache_dest
    elif first_scheme == "file" and rest_schemes and "path" in fragment:
        underlying_fragment = {key: val for key, val in fragment.items() if key != "path"}
        underlying_fragment_str = urllib.parse.urlencode(underlying_fragment, doseq=True)
        dir_url = urllib.parse.urlunparse(
            (rest_schemes, *url[1:5], underlying_fragment_str)
        )
        dir_path = pathify(dir_url, base, cache_path, True, True)
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
            archive_path = pathify(archive_url, base, cache_path, True, False)
            subprocess_run(["unzip", str(archive_path), "-d", str(cache_dest)], check=True, capture_output=True)
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
            subprocess_run(["git", "clone", repo_url, str(cache_dest), "--recursive"], check=True, capture_output=True)
            return cache_dest
        else:
            repo_path = pathify(f"git+{repo_url}", base, cache_path, True, True)
            subprocess_run(["git", "-C", str(repo_path), "fetch"], check=True, capture_output=True)
            subprocess_run(["git", "-C", str(repo_path), "checkout", rev], check=True, capture_output=True)
            subprocess_run(["git", "-C", str(repo_path), "submodule", "update", "--recursive"], check=True, capture_output=True)
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
