from __future__ import annotations

import contextlib
from dataclasses import dataclass
import itertools
import multiprocessing
import operator
import os
import queue
import shlex
import subprocess
import sys
import threading
import urllib.parse
import urllib.request
from pathlib import Path
import shutil
import textwrap
from types import TracebackType
from typing import (
    cast,
    Any,
    AnyStr,
    Awaitable,
    BinaryIO,
    Callable,
    Iterable,
    Iterator,
    Generic,
    IO,
    List,
    Mapping,
    Optional,
    Sequence,
    Sized,
    Tuple,
    Type,
    TypeVar,
    Union,
)
import zipfile

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
    string = string.replace(escape_seq, escape_seq + escape_seq)
    for unsafe_string, safe_string in escapes.items():
        string = string.replace(unsafe_string, escape_seq + safe_string)
    return string


def escape_fname(string: str) -> str:
    """Sanitizes a string so that it can be a filename

    See https://en.wikipedia.org/wiki/Filename#Reserved_characters_and_words
    """

    ret = escape_string(string, "%", {"/": "s", "\\": "b", ":": "c"})
    # handle some special cases
    if ret == ".":
        return "%d"
    elif ret == "..":
        return "%d%d"
    elif ret == "~":
        return "%t"
    else:
        return ret


class CalledProcessError(Exception):
    # unfortunately, Exception is not compatible with @dataclass
    def __init__(
            self,
            args: Sequence[str],
            cwd: Path,
            env: Mapping[str, str],
            stdout: bytes,
            stderr: bytes,
            returncode: int,
    ) -> None:
        self.args2 = args
        self.cwd = cwd
        self.env = env
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode

    def __str__(self) -> str:
        env_var_str = shlex.join(
            f"{key}={val}"
            for key, val in self.env.items()
            if key not in os.environ or os.environ[key] != val
        )
        if env_var_str:
            env_var_str += " "
        cwd_str = f"-C {shlex.quote(str(self.cwd))} " if self.cwd.resolve() != Path().resolve() else ""
        env_cmd_str = f"env {cwd_str}{env_var_str}" if env_var_str or cwd_str else ""
        cmd_str = f"{env_cmd_str}{shlex.join(self.args2)}"
        stdout = ("\nstdout:\n" + textwrap.indent(self.stdout.decode(), "  ")) if self.stdout is not None else ""
        stderr = ("\nstderr:\n" + textwrap.indent(self.stderr.decode(), "  ")) if self.stderr is not None else ""

        return f"""Command returned non-zero exit status {self.returncode}\ncommand: {cmd_str}{stdout}{stderr}"""


def subprocess_run(
    args: Sequence[str],
    cwd: Optional[Union[Path, str]] = None,
    check: bool = False,
    env: Optional[Mapping[str, str]] = None,
    env_override: Optional[Mapping[str, str]] = None,
    capture_output: bool = False,
    stdout: Optional[BinaryIO] = None,
) -> subprocess.CompletedProcess[bytes]:
    """Wrapper around of subprocess.run.

    I will progressively port arguments by need.

    env_override is a mapping used to update (not replace) env.

    If the subprocess's returns non-zero and the return code is
    checked (`subprocess_run(..., check=True)`), all captured output
    is dumped.

    """

    env = dict(env if env is not None else os.environ)
    cwd = (cwd if isinstance(cwd, Path) else Path(cwd)) if cwd is not None else Path()
    if env_override:
        env.update(env_override)

    proc = subprocess.run(args, env=env, cwd=cwd, capture_output=capture_output, stdout=stdout)

    if check:
        if proc.returncode != 0:
            raise CalledProcessError(
                args, cwd, env, proc.stdout, proc.stderr, proc.returncode
            )
    return proc


T = TypeVar("T")


def chunker(it: Iterable[T], size: int) -> Iterable[List[T]]:
    """chunk input into size or less chunks
shamelessly swiped from Lib/multiprocessing.py:Pool._get_task"""
    it = iter(it)
    while True:
        x = list(itertools.islice(it, size))
        if not x:
            return
        yield x


def my_length_hint(iterable: Iterable[T], default: V) -> Union[int, V]:
    if hasattr(iterable, "__len__"):
        return len(cast(Sized, iterable))
    elif hasattr(iterable, "__length_hint__"):
        return operator.length_hint(iterable)
    else:
        return default


def threading_imap_unordered(
    func: Callable[[T], V],
    iterable: Iterable[T],
    chunksize: int = 1,
    desc: Optional[str] = None,
    length_hint: Optional[int] = 0,
) -> Iterable[V]:
    """Clone of multiprocessing.imap_unordered for threads with tqdm for progress

    `desc` is an optional label for the progress bar.

    If the length cannot be determined by operator.length_hint, `length_hint` will be used. If it is None, we fallback to tqdm without a `total`.
    """
    results: queue.Queue[Tuple[str, Union[V, BaseException]]] = queue.Queue(maxsize=operator.length_hint(iterable))

    def worker(chunk: List[T], results: queue.Queue[Tuple[str, Union[V, BaseException]]]) -> None:
        try:
            for elem in chunk:
                results.put(("elem", func(elem)))
        except BaseException as e:
            results.put(("exception", e))

    threads = [
        threading.Thread(target=worker, args=(chunk, results))
        for chunk in chunker(iterable, chunksize)
    ]

    for thread in threads:
        thread.start()

    def get_results(results: queue.Queue[Tuple[str, Union[V, BaseException]]]) -> Iterable[V]:
        while any(thread.is_alive() for thread in threads):
            try:
                result = results.get(timeout=0.5)
            except queue.Empty:
                pass
            else:
                if result[0] == "elem":
                    tqdm.write(str(result[1]))
                    yield cast(V, result[1])
                elif result[0] == "exception":
                    raise cast(BaseException, result[1])
                else:
                    raise ValueError(f"Unknown signal from thread worker: {result[0]}")

    results_list = list(
        tqdm(
            get_results(results),
            total=my_length_hint(iterable, length_hint),
            desc=desc,
            unit="plugins",
        )
    )

    for thread in threads:
        thread.join()

    return results_list


def threading_map(
    func: Callable[[T], V],
    iterable: Sequence[T],
    chunksize: int = 1,
    desc: Optional[str] = None,
    length_hint: Optional[int] = 0,
) -> Iterable[V]:
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
                length_hint=my_length_hint(iterable, length_hint),
            ),
        ),
    )


def fill_defaults(
    thing: Any, thing_schema: Mapping[str, Any], path: Optional[List[str]] = None
) -> None:
    if path is None:
        path = []

    if "type" in thing_schema and thing_schema["type"] == "object":
        for key in thing_schema.get("properties", []):
            if key not in thing:
                if "default" in thing_schema["properties"][key]:
                    thing[key] = thing_schema["properties"][key]["default"]
                    # print(f'{".".join(path + [key])} is defaulting to {thing_schema["properties"][key]["default"]}')
            # even if key is present, it may be incomplete
            fill_defaults(thing[key], thing_schema["properties"][key], path + [key])
    elif "type" in thing_schema and thing_schema["type"] == "array":
        for i, item in enumerate(thing):
            fill_defaults(item, thing_schema["items"], path + [str(i)])


BLOCKSIZE = 4096

@dataclass
class TqdmOutputFile(Generic[AnyStr]):
    fileobj: IO[AnyStr]
    length: Optional[int]
    desc: Optional[str] = None

    def __post_init__(self) -> None:
        self.t = tqdm(total=self.length, desc=self.desc, unit="bytes", unit_scale=True)

    def read(self, block_size: int = BLOCKSIZE) -> AnyStr:
        buf = self.fileobj.read(block_size)
        self.t.update(len(buf))
        return buf

    def __enter__(self) -> TqdmOutputFile[AnyStr]:
        return self

    def __exit__(self, exctype: Optional[Type[BaseException]], excinst: Optional[BaseException], exctb: Optional[TracebackType]) -> None:
        self.close()

    def close(self) -> None:
        self.t.close()
        self.fileobj.close()

    @staticmethod
    def from_url(url: str, desc: Optional[str] = None) -> TqdmOutputFile[bytes]:
        resp = urllib.request.urlopen(url)
        length = int(resp.getheader('content-length'))
        return TqdmOutputFile(cast(IO[bytes], resp), length, desc)


def truncate(string: str, length: int) -> str:
    if len(string) <= length:
        return string
    else:
        left_portion = int(2/3*length)
        right_portion = length - left_portion - 3
        return string[:left_portion] + "..." + string[-right_portion:]


def unzip_with_progress(zip_path: Path, output_dir: Path, desc: Optional[str] = None) -> None:
    try:
        with zipfile.PyZipFile(str(zip_path)) as zf:
            total = sum(zi.file_size for zi in zf.infolist())
            progress = tqdm(desc=desc, total=total, unit="bytes", unit_scale=True)
            for zi in zf.infolist():
                output_path = output_dir / zi.filename
                if not zi.is_dir():
                    output_path.parent.mkdir(parents=True, exist_ok=True)
                    with output_path.open("wb") as output_fileobj:
                        with zf.open(zi, "r") as input_fileobj:
                            while True:
                                buf = input_fileobj.read(BLOCKSIZE)
                                if not buf:
                                    break
                                progress.update(len(buf))
                                output_fileobj.write(buf)
    except BaseException as e:
        print("Exception is caught")
        if output_dir.exists():
            shutil.rmtree(output_dir)
        raise e


# Only Python 3.9 has Path.is_relative_to :'(
def is_relative_to(a: Path, b: Path) -> bool:
    try:
        a.relative_to(b)
    except ValueError:
        return False
    else:
        return True


DISPLAY_PATH_LENGTH = 60


def pathify(
    path_descr: Union[str, Mapping[str, Any]],
    base: Path,
    cache_path: Path,
    should_exist: bool,
    should_dir: bool,
) -> Path:
    """Takes a URL, copies it to disk (if not already there), and returns a local path to it.

Args:
    path_descr
    base (Path): the base path for resolving relative file paths
    cache_path (Path): the location where this fn will put objects
    should_exist (bool): ensure that the file exists or raise ValueError (applicable to the file: scheme)
    should_dir (bool): expect the url to point to a directory, otherwise a file, and raise ValueError if that expectation is violated

Returns:
    Path: a path to the resource on the local disk. Do not modify the file at this path.

    """

    if isinstance(path_descr, str):
        path = Path(path_descr)
        normed_path = path if path.is_absolute() else Path(base / path)
        if should_exist and not normed_path.exists():
            raise ValueError(f"Expected {path_descr} (=> {normed_path}) to exist")
        elif normed_path.exists() and (normed_path.is_dir() != should_dir):
            raise ValueError(
                f"{path_descr} points to a {'dir' if normed_path.is_dir() else 'file'} "
                f"but a {'dir' if should_dir else 'file'} expected"
            )
        else:
            return normed_path
        return Path(path_descr)
    elif "download_url" in path_descr:
        url = path_descr["download_url"]
        cache_dest = cache_path / escape_fname(url)
        if should_dir:
            raise ValueError(f"{path_descr} points to a file (because of the download_url scheme) not a dir")
        elif cache_dest.exists():
            return cache_dest
        else:
            with TqdmOutputFile.from_url(url, desc=f"Downloading {truncate(url, DISPLAY_PATH_LENGTH)}") as infileobj:
                with cache_dest.open('wb') as outfileobj:
                    shutil.copyfileobj(infileobj, cast(IO[bytes], outfileobj))
            return cache_dest
    elif "download_url_gdrive" in path_descr:
        id = path_descr["download_url_gdrive"]
        cache_dest = cache_path / escape_fname(id + ".zip")
        if should_dir:
            raise ValueError(f"{path_descr} points to a file (because of the download_url_gdrive scheme) not a dir")
        elif cache_dest.exists():
            return cache_dest
        else:
            import gdown #pip install gdown
            gdown.download(id=id, output=str(cache_dest), quiet=False, fuzzy=True)
            # unzip_with_progress(cache_dest, cache_dest_unzip, f"Unzipping {truncate(str(cache_dest), DISPLAY_PATH_LENGTH)}")
            return cache_dest
    elif "subpath" in path_descr:
        ret = cast(Path,
            pathify(path_descr["relative_to"], base, cache_path, True, True)
            / path_descr["subpath"]
        )
        if should_exist and not ret.exists():
            raise ValueError(f"Expected {path_descr['subpath']} to exist within {path_descr['relative_to']}")
        elif ret.exists() and (ret.is_dir() != should_dir):
            raise ValueError(
                f"{path_descr['subpath']} relative to {path_descr['relative_to']} points to a {'dir' if path.is_dir() else 'file'} "
                f"but a {'dir' if should_dir else 'file'} expected"
            )
        else:
            return ret
    elif "archive_path" in path_descr:
        archive_path = pathify(
            path_descr["archive_path"], base, cache_path, True, False
        )
        cache_key = archive_path.relative_to(cache_path) if is_relative_to(archive_path, cache_path) else str(archive_path)
        cache_dest = cache_path / escape_fname("EXTRACTED_" + str(cache_key))
        if not should_dir:
            raise ValueError(
                f"{path_descr} points to a dir (because of the archive_path scheme) not a file"
            )
        elif cache_dest.exists():
            return cache_dest
        else:
            unzip_with_progress(archive_path, cache_dest, f"Unzipping {truncate(str(cache_key), DISPLAY_PATH_LENGTH)}")
            return cache_dest
    elif "git_repo" in path_descr:
        cache_dest = cache_path / escape_fname(path_descr["git_repo"])
        if not should_dir:
            raise ValueError(
                f"{path_descr} points to a dir (because of the git_repo scheme) not a file"
            )
        elif "version" in path_descr:
            repo_path = pathify(dict(git_repo=path_descr["git_repo"]), base, cache_path, True, True)
            subprocess_run(
                ["git", "-C", str(repo_path), "fetch"], check=True, capture_output=True
            )
            subprocess_run(
                ["git", "-C", str(repo_path), "checkout", "--force", path_descr["version"]],
                check=True,
                capture_output=True,
            )
            subprocess_run(
                ["git", "-C", str(repo_path), "submodule", "update", "--recursive"],
                check=True,
                capture_output=True,
            )
            return repo_path
        elif cache_dest.exists():
            return cache_dest
        else:
            subprocess_run(
                [
                    "git",
                    "clone",
                    path_descr["git_repo"],
                    str(cache_dest),
                    "--recursive",
                ],
                check=True,
                capture_output=False,
            )
            return cache_dest
    else:
        raise ValueError(f"Unsupported path description {path_descr}")


@contextlib.contextmanager
def noop_context(x: Any) -> Iterator[Any]:
    yield x


def make(
    path: Path,
    targets: List[str],
    var_dict: Optional[Mapping[str, str]] = None,
    parallelism: Optional[int] = None,
    env_override: Optional[Mapping[str, str]] = None
) -> None:

    if parallelism is None:
        parallelism = max(1, multiprocessing.cpu_count() // 2)

    var_dict_args: List[str] = list() if not var_dict else \
                               [f"{key}={val}" for key, val in var_dict.items()]

    subprocess_run(
        ["make", "-j", str(parallelism), "-C", str(path), *targets, *var_dict_args],
        env_override=env_override,
        check=True,
        capture_output=True,
    )


def cmake(
    path: Path,
    build_path: Path,
    var_dict: Optional[Mapping[str, str]] = None,
    parallelism: Optional[int] = None,
    env_override: Optional[Mapping[str, str]] = None
) -> None:

    if parallelism is None:
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
        env_override=env_override,
    )
    make(build_path, ["all"], parallelism=parallelism, env_override=env_override)
