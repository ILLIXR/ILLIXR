#!/usr/bin/env python3
"""Check CPU loading images between GPU gameplay frames without a headset.

Run with the configured Boba CUDA Python and pass the installed Boba-ILLIXR root.
The bridge uses temporary local IPC files and requires a working NVIDIA GPU.
"""

import argparse
import os
from pathlib import Path
import struct
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("demo_root", type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.demo_root.resolve()))

    import numpy as np
    import torch
    from qqtt.illixr_bridge import ILLIXRImmersiveBridge

    if not torch.cuda.is_available():
        raise RuntimeError("This regression requires CUDA to exercise direct frame copies")

    width, height = 64, 48
    # A noncontiguous pattern also checks the bridge's input normalization.
    pattern = torch.arange(width * height * 4).remainder(251).to(torch.uint8)
    pattern = pattern.reshape(width, height, 4).transpose(0, 1)
    devices = ("cuda", "cpu", "cpu", "cuda") * 3
    with tempfile.TemporaryDirectory(prefix="boba-frames-") as temporary:
        directory = Path(temporary)
        for suffix, filename in (
            ("INPUT_SOCKET", "input.sock"),
            ("FRAME_PATH", "frames.bin"),
            ("OVERLAY_PATH", "overlay.bin"),
            ("MODAL_PATH", "modal.bin"),
        ):
            os.environ[f"BOBA_ILLIXR_{suffix}"] = str(directory / filename)
        os.environ["BOBA_IMMERSIVE_DIRECT_COMMIT_MODE"] = "registered"
        bridge = ILLIXRImmersiveBridge(args.demo_root, width, height)
        try:
            bridge.start()
            initial_stats = bridge.bridge_commit_stats()
            assert initial_stats["direct_commit_enabled"], initial_stats
            for frame_id, device in enumerate(devices, 1):
                left = pattern.to(device)
                right = (255 - pattern).to(device)
                ok, _ = bridge.publish_stereo_frames(left, right)
                assert ok and bridge.wait_for_bridge_idle(timeout=10), frame_id
                payload = (directory / "frames.bin").read_bytes()
                header = bridge.HEADER_STRUCT.unpack_from(payload)
                latest_id, slot = header[7:9]
                assert latest_id == frame_id, (latest_id, frame_id)
                metadata_stride = header[10] // header[6]
                metadata_offset = bridge.HEADER_STRUCT.size + slot * metadata_stride
                assert struct.unpack_from("<Q", payload, metadata_offset)[0] == frame_id
                pixels_offset = (
                    bridge.HEADER_STRUCT.size + header[10] + slot * bridge.frame_bytes
                )
                actual = np.frombuffer(
                    payload, dtype=np.uint8, count=bridge.frame_bytes, offset=pixels_offset
                ).reshape(2, height, width, 4)
                np.testing.assert_array_equal(actual[0], left.cpu().numpy())
                np.testing.assert_array_equal(actual[1], right.cpu().numpy())
            stats = bridge.bridge_commit_stats()
            assert stats["committed_update_count"] == len(devices), stats
            assert stats["direct_commit_count"] == 6, stats
            assert stats["bridge_publish_sample_mismatch_count"] == 0, stats
        finally:
            bridge.stop()
    print("PASS: 12 GPU and CPU stereo frames; exact pixels and generations; GPU direct copies retained")


if __name__ == "__main__":
    main()
