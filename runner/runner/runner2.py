from pathlib import Path
import shutil
import random
import subprocess
import sys
import click
import json
@click.command()
@click.argument("metrics_dir", type=Path)
def main(metrics_dir: Path) -> None:
    metrics_dir.mkdir(exist_ok=True, parents=True)
    for it in range(2):
        # for cpu_freq in [1.3, 1.8, 2.6, 5.3]:
        for cpu_freq in [2.6, 5.3]:
            # for scheduler, swap in [("static", True), ("static", False)]:
            # for scheduler in ["default", "static", "priority", "manual"]:
            for scheduler in ["static", "manual"]:
                swap = False
                for cpus in [1, 10] if scheduler == "manual" else [1]:
                    label = f"{random.randint(0, 2**32 - 1):08x}"
                    print(scheduler, cpus, cpu_freq, label)
                    swap_str = ['n', 'y'][swap]
                    subprocess.run([
                        sys.executable,
                        "./runner/runner/main.py",
                        "./configs/native.yaml",
                        *["--overrides", f'conditions.scheduler="{scheduler}"'],
                        *["--overrides", f'conditions.swap={json.dumps(swap)}'],
                        *["--overrides", f'loader.env.ILLIXR_SWAP_ORDER="{swap_str}"'],
                        *["--overrides", f'conditions.cpus={cpus}'],
                        *["--overrides", f"conditions.cpu_freq={cpu_freq}"],
                        *["--overrides", f'loader.log_stdout="metrics/log"'],
                    ], check=True)
                    shutil.move("metrics", metrics_dir / label)
main()
