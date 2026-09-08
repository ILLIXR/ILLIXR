# Boba physics-based Gaussian digital twins

`boba_immersive` adds physics-based Gaussian digital twin support to ILLIXR,
using [Boba](https://jianxiapyh.github.io/Boba-project-page/) as the simulation
and rendering backend. The Boba simulation and rendering code stays in the Boba
repository; the ILLIXR plugins launch it, deliver headset/controller poses to
it, and return its rendered stereo images to the headset.

Two Quest 3 paths are available:

- `boba_quest_native_server` uses the native ILLIXR Quest app and ILLIXR's
  existing network backends. Meta OpenXR runs directly on the Quest. It does
  not require ALVR or SteamVR.
- `boba_quest` preserves the original ALVR/SteamVR path as a working baseline.
  The desktop OpenXR plugin receives input and submits images through SteamVR,
  while ALVR transports them between SteamVR and its Quest client.

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
- applies checksum-verified compatibility patches to the setup-managed
  Boba-Demo checkout so its launcher and Python environment guards use
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

## Install the native Quest app

The first installation requires a Quest with developer mode enabled, connected
and authorized once through USB. Activate the ILLIXR Conda environment so the
host-side `protoc` compiler is available, then run from the source checkout:

```bash
conda activate illixr
./scripts/install_quest_app.sh
```

The script builds the APK, installs it, launches `ILLIXRApp`, and prints the
Quest's Wi-Fi address and the corresponding desktop option. Use `--no-build` to
reinstall an existing APK, `--no-launch` to install without opening the app, or
`--serial SERIAL` when multiple Android devices are connected. Run
`./scripts/install_quest_app.sh --help` for Android SDK and JDK overrides.

The installed development APK appears in the Quest's **Unknown Sources** app
list. USB is not used by the runtime and may be disconnected after installation.

## Run over Wi-Fi without ALVR or SteamVR

Put the Quest and desktop on the same local network, open `ILLIXRApp` on an
awake Quest, and pass its Wi-Fi address to the desktop process:

```bash
./main.opt.exe \
  --yaml=profiles/boba_quest_native_server.yaml \
  --quest-ip 192.168.x.x
```

Adjust the executable and profile paths for the selected build or install
directory. The desktop sends a small configuration handshake to the Quest; the
Quest learns the desktop address from that packet and connects back through the
existing ILLIXR transport. No desktop address needs to be entered on the
headset. The desktop waits up to 120 seconds for the app by default; change this
with `--quest-connect-timeout SECONDS`.

Allow UDP ports 9010 and 9003 and TCP port 9001 on the local firewall. The app
must be open because a stopped Android application cannot be awakened over an
ordinary LAN connection. The headset must also be awake for OpenXR to supply
valid tracking and controller data.

## ALVR/SteamVR baseline

For the `boba_quest` profile, Steam, SteamVR, and ALVR are outside the setup
script. Start the paired ALVR Quest client and SteamVR before launching ILLIXR.
This path remains available for comparison and rollback.
