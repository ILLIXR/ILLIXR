from pathlib import Path
import random
import subprocess
import sys
import click
@click.command()
def main() -> None:
    all_metrics = Path("metrics-all")
    for it in range(5):
        for scheduler in [True, False]:
            for cpu_list in ["0"] if scheduler else ["0", "0-10"]:
                for cpu_freq in [1.3, 1.8, 2.6, 5.3]:
                    label = f"{random.randint(0, 2**32 - 1):x}"
                    subprocess.run([
                        sys.executable,
                        "./runner/runner/main.py",
                        "./configs/native.yaml",
                        *["--overrides", f"conditions.scheduler={'true' if scheduler else 'false'}"],
                        *["--overrides", f'conditions.cpu_list="{cpu_list}"'],
                        *["--overrides", f"conditions.cpu_freq={cpu_freq}"],
                        *["--overrides", f'loader.log_stdout="metrics/log"'],
                        *["--overrides", f'loader.metrics="{str(all_metrics / label)}"'],
                    ])
main()
