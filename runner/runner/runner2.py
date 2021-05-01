from pathlib import Path
import random
import subprocess
import sys
import click
@click.command()
def main() -> None:
    all_metrics = Path("metrics-all")
    for it in range(1):
        for cpu_freq in [2.6]:
        # for cpu_freq in [1.3, 1.8, 2.6, 5.3]:
            for scheduler in ["default", "static", "priority", "manual"]:
                for cpus in [1]:
                # for cpu_list in ["0"] if scheduler == "default" else ["0", "0-10"]:
                    label = f"{random.randint(0, 2**32 - 1):x}"
                    print(scheduler, cpus, cpu_freq, label)
                    subprocess.run([
                        sys.executable,
                        "./runner/runner/main.py",
                        "./configs/native.yaml",
                        *["--overrides", f'conditions.scheduler="{scheduler}"'],
                        *["--overrides", f'conditions.cpus={cpus}'],
                        *["--overrides", f"conditions.cpu_freq={cpu_freq}"],
                        *["--overrides", f'loader.log_stdout="metrics/log"'],
                        *["--overrides", f'loader.metrics="{str(all_metrics / label)}"'],
                    ], check=True)
main()
