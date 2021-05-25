import multiprocessing
import contextlib
from pathlib import Path
import sys
import click
import subprocess
from typing import Callable, List, Iterable

from util import identity, T

@contextlib.contextmanager
def set_cpu_freq(freq_ghz: float) -> None:
    num_cpus = multiprocessing.cpu_count()

    def all_cpu_get(path: str, parser: Callable[[str], T] = identity) -> List[T]:
        return [
            parser(Path(f"/sys/devices/system/cpu/cpu{cpu}/{path}").read_text().strip())
            for cpu in range(num_cpus)
        ]

    def all_cpu_set(path: str, values: Iterable[T], stringer: Callable[[T], str] = str) -> None:
        for cpu, value in zip(range(num_cpus), values):
            subprocess.run(["sudo", "tee", f"/sys/devices/system/cpu/cpu{cpu}/{path}"], check=True, input=stringer(value), text=True, capture_output=True)

    limit_min_freqs = all_cpu_get("cpufreq/cpuinfo_min_freq", int)
    limit_max_freqs = all_cpu_get("cpufreq/cpuinfo_max_freq", int)

    freq = int(freq_ghz * 1e6)
    if not (max(limit_min_freqs) <= freq <= min(limit_max_freqs)):
        raise ValueError(f"{freq} out of range ({max(limit_min_freqs)} to {min(limit_max_freqs)})")

    old_min_freqs = all_cpu_get("cpufreq/scaling_min_freq", int)
    old_max_freqs = all_cpu_get("cpufreq/scaling_max_freq", int)

    all_cpu_set("cpufreq/scaling_min_freq", [freq] * num_cpus)
    all_cpu_set("cpufreq/scaling_max_freq", [freq] * num_cpus)

    try:
        yield
    finally:
        all_cpu_set("cpufreq/scaling_min_freq", old_min_freqs)
        all_cpu_set("cpufreq/scaling_max_freq", old_max_freqs)

if __name__ == "__main__":
    @click.command()
    @click.argument("freq", type=float)
    @click.argument("cmd", type=str, nargs=-1)
    def main(
            freq: float,
            cmd: List[str],
    ) -> None:
        """Run `cmd` at the specified `freq_ghz`"""
        with set_cpu_freq(freq):
            proc = subprocess.run(cmd, check=False)
        sys.exit(proc.returncode)
    main()
