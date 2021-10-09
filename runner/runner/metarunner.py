from pathlib import Path
import shutil
from tqdm import tqdm
import random
import shlex
import subprocess
import sys
import click
import warnings
import json

class ILLIXRWarning(Warning):
    pass

warnings.simplefilter("always", category=ILLIXRWarning)

@click.command()
@click.argument("metrics_dir", type=Path)
@click.option(
    "--iters",
    default=4,
    type=int,
    help="Iterate over the grid multiple times; this is the outer-most loop, so if `iters == 2`, then runner would run [A, B, C, A, B, C]",
    show_default=True,
)
@click.option(
    "--schedulers",
    default="default,priority,manual,static,dynamic",
    type=str,
    show_default=True,
)
@click.option(
    "--cpu-freqs",
    default="1.3,1.8,2.6,5.3",
    type=str,
    show_default=True,
)
@click.option(
    "--swaps",
    default="0,1,2",
    type=str,
    help="These 'swap-strategies' affect the order of plugins in the scheduler. See `robotics_project/README.md`. The non-scheduled cases will not iterate the swaps.",
    show_default=True,
)
@click.option(
    "--timewarp-cushions",
    default="0.95",
    type=str,
    help="Comma-separated list of ms to add to the dynamic scheduler's estimate of the compute-time of timewarp.",
    show_default=True,
)
@click.option(
    "--multicore-manual/--no-multicore-manual",
    default=True,
    type=bool,
    help="Whether or not to tack on a manual-scheduler, multiple-cores. This run is comparable to the mainline ILLIXR.",
    show_default=True,
)
@click.option(
    "--dry-run/--no-dry-run",
    default=False,
    type=bool,
    help="This prints the config instead of runnign ILLIXR. Use this to make sure you like the configuration-grid.",
    show_default=True,
)
@click.option(
    "--duration",
    default=75,
    type=int,
    help="The duration of the ILLIXR run.",
    show_default=True,
)
def main(
        metrics_dir: Path,
        iters: int,
        swaps: str,
        schedulers: str,
        cpu_freqs: str,
        multicore_manual: bool,
        timewarp_cushions: float,
        dry_run: bool,
        duration: int,
) -> None:
    """This is a metarunner (it runs the runner on a configuration-grid)

    """

    metrics_dir.mkdir(exist_ok=True, parents=True)

    def gen_options():
        for it in range(iters):
            for cpu_freq in map(float, cpu_freqs.split(",")):
                for scheduler in schedulers.split(","):
                    swap_list = swaps.split(",") if scheduler == "dynamic" else [None]
                    for swap in swap_list:
                        cpus_list = [1, 10] if scheduler == "manual" and multicore_manual else [1]
                        for cpus in cpus_list:
                            for timewarp_cushion in timewarp_cushions.split(","):
                                yield scheduler, cpu_freq, it, cpus, swap, float(timewarp_cushion)

    options = list(gen_options())

    subprocess.run(["sudo", "rm", "-rf", "metrics"], check=True)

    for scheduler, cpu_freq, it, cpus, swap, timewarp_cushion in tqdm(options, disable=dry_run):
        label = f"{random.randint(0, 2**32 - 1):08x}"
        cmd = [
            sys.executable,
            "runner/runner/main.py",
            "configs/native.yaml",
            "--quiet",
            *["--overrides", f'conditions.scheduler="{scheduler}"'],
            *(["--overrides", f'conditions.swap={swap}'] if swap is not None else []),
            *["--overrides", f'conditions.cpus={cpus}'],
            *["--overrides", f"conditions.cpu_freq={cpu_freq}"],
            *["--overrides", f'loader.log_stdout="metrics/log"'],
            *["--overrides", f'conditions.duration={duration}'],
            *["--overrides", f'conditions.timewarp_cushion={timewarp_cushion}'],
        ]
        if dry_run:
            swap_str = f", swap: {json.dumps(swap):5s}"
            print(f"{{scheduler: {json.dumps(scheduler):10s}, cpu_freq: {json.dumps(cpu_freq):3s}, cpus: {cpus:2d}{swap_str}}}")
        else:
            while True:
                subprocess.run(cmd, check=True)
                metrics = Path("metrics")
                if is_good_run(metrics):
                    shutil.move(metrics, metrics_dir / label)
                    break
                else:
                    warnings.warn(ILLIXRWarning("ILLIRX did not really run. I'm not sure why. Re-trying"))

def is_good_run(metrics: Path) -> bool:
    log = (metrics / "log").read_text()
    return all([
        log.count("waiting for enough clone states") >= 4,
        log.count("Stack frame logger on ") >= 5,
        log.count("NOT ENOUGH POINTS -- Tracking failed") >= 30,
    ])

main()
