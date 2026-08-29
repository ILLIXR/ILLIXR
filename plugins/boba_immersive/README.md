# Boba immersive plugin

`boba_immersive` launches the existing Boba Quest demo as a child process,
forwards Quest controller/view input from the ILLIXR switchboard, and publishes
the demo's rendered output as `stereo_frame`. The
`openxr_quest_controller` plugin consumes that frame and submits it through the
SteamVR OpenXR runtime to the Quest over ALVR.

## One-time Boba setup

From an ILLIXR source checkout, run:

```bash
./scripts/setup_boba_immersive.sh
```

The script installs Boba below
`${XDG_DATA_HOME:-$HOME/.local/share}/illixr/boba_immersive` by default. It:

- checks out pinned, compatible revisions of Boba-Public and Boba-Demo;
- creates or validates the dedicated `boba-cu132` Conda environment using
  Boba-Public's pinned CUDA 13.2 environment specification;
- applies a checksum-verified compatibility patch to the setup-managed
  Boba-Demo checkout so its launcher and Python environment guard use
  `boba-cu132`;
- builds Boba's CUDA extensions;
- downloads and extracts all five archives listed under Boba-Public's
  **Required Assets** section;
- downloads and verifies Boba-Demo's Git LFS Sloth payload;
- installs the demo-only Python requirement and validates the Rope, Sloth, and
  Lab assets.

The optional Garden scene is a separate large download. Include it with:

```bash
./scripts/setup_boba_immersive.sh --garden
```

Downloads are resumable. Successfully extracted Boba-Public archives are
removed by default to save disk space; pass `--keep-downloads` to retain them.

For a non-default location, use `--install-root` during setup and export the
same root when launching ILLIXR:

```bash
./scripts/setup_boba_immersive.sh --install-root /path/to/boba_immersive
export BOBA_IMMERSIVE_ROOT=/path/to/boba_immersive
```

`BOBA_DEMO_LAUNCHER` remains available as a direct launcher override. No
machine-specific path is compiled into the plugin.

Steam, SteamVR, and ALVR are outside this setup script. The current MVP assumes
the desktop streamer/runtime and matching Quest client are already installed,
paired, and working before ILLIXR starts.
