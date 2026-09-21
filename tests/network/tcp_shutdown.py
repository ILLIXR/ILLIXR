#!/usr/bin/env python3
"""Exercise TCP worker cleanup through the built ILLIXR runtime on loopback."""

import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile


def check_case(runtime, plugin_dir, case):
    with tempfile.TemporaryDirectory(prefix="illixr-tcp-shutdown-") as directory:
        root = Path(directory)
        (root / "logs").mkdir()
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(10)
            profile = root / "tcp.yaml"
            profile.write_text(
                "plugins: tcp_network_backend\n"
                "env_vars:\n"
                "  ILLIXR_IS_CLIENT: 1\n"
                "  ILLIXR_SERVER_IP: 127.0.0.1\n"
                f"  ILLIXR_TCP_SERVER_PORT: {listener.getsockname()[1]}\n"
            )
            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = str(plugin_dir) + os.pathsep + env.get("LD_LIBRARY_PATH", "")
            process = subprocess.Popen(
                [str(runtime), f"--yaml={profile}", "--duration=1"],
                cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            try:
                peer, _ = listener.accept()
                with peer:
                    if case == "peer EOF":
                        peer.shutdown(socket.SHUT_RDWR)
                        peer.close()
                    elif case == "peer reset":
                        linger_format = "hh" if os.name == "nt" else "ii"
                        peer.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack(linger_format, 1, 0))
                        peer.close()
                    elif case == "malformed header":
                        # A total packet length shorter than the eight-byte header.
                        peer.sendall(struct.pack("=II", 4, 0))
                    # "connected stop" keeps the peer open: stop() must wake recv().
                    output, _ = process.communicate(timeout=10)
                if process.returncode != 0:
                    raise RuntimeError(f"runtime exited with {process.returncode}\n{output}")
                if case in ("peer EOF", "peer reset") and "TCP connection closed or read failed" not in output:
                    raise RuntimeError(f"disconnect path was not exercised\n{output}")
                if case == "malformed header" and "malformed packet header" not in output:
                    raise RuntimeError(f"invalid-header path was not exercised\n{output}")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--plugin-dir", type=Path, required=True)
    args = parser.parse_args()
    failures = 0
    for case in ("connected stop", "peer EOF", "peer reset", "malformed header"):
        try:
            check_case(args.runtime.resolve(), args.plugin_dir.resolve(), case)
            print(f"PASS {case}")
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            failures += 1
            print(f"FAIL {case}: {error}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
