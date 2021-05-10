from pathlib import Path
import shutil
from tqdm import tqdm
import random
import shlex
import subprocess
import sys
import click
import json
@click.command()
@click.argument("metrics_dir", type=Path)
@click.option("--iters", default=4, type=int)
@click.option("--schedulers", default="default,static,priority,manual", type=str)
@click.option("--cpu-freqs", default="1.3,1.8,2.6,5.3", type=str)
@click.option("--swaps", default="0,1,2", type=str)
@click.option("--multicore-manual/--no-multicore-manual", default=True, type=bool)
@click.option("--dry-run/--no-dry-run", default=False, type=bool)
def main(metrics_dir: Path, iters: int, swaps: str, schedulers: str, cpu_freqs: str, multicore_manual: bool, dry_run: bool) -> None:
    metrics_dir.mkdir(exist_ok=True, parents=True)

    def gen_options():
        for it in range(iters):
            for cpu_freq in map(float, cpu_freqs.split(",")):
                for scheduler in schedulers.split(","):
                    swap_list = swaps.split(",") if scheduler == "dynamic" else [None]
                    for swap in swap_list:
                        cpus_list = [1, 10] if scheduler == "manual" and multicore_manual else [1]
                        for cpus in cpus_list:
                            yield scheduler, cpu_freq, it, cpus, swap

    options = list(gen_options())

    for scheduler, cpu_freq, it, cpus, swap in tqdm(options, disable=dry_run):
        label = f"{random.randint(0, 2**32 - 1):08x}"
        cmd = [
            sys.executable,
            "./runner/runner/main.py",
            "./configs/native.yaml",
            "--quiet",
            *["--overrides", f'conditions.scheduler="{scheduler}"'],
            *(["--overrides", f'conditions.swap={swap}'] if swap is not None else []),
            *["--overrides", f'conditions.cpus={cpus}'],
            *["--overrides", f"conditions.cpu_freq={cpu_freq}"],
            *["--overrides", f'loader.log_stdout="metrics/log"'],
        ]
        if dry_run:
            print(" && ".join([
                shlex.join(cmd),
                shlex.join(["mv", "metrics", str(metrics_dir / label)])
            ]), end="\n\n")
        else:
            subprocess.run(cmd, check=True)
            shutil.move("metrics", metrics_dir / label)
main()
