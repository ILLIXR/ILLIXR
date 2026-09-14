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
  Its `quest3.controller` plugin runs on Linux/X11, receives input and submits
  images through SteamVR, while ALVR transports them between SteamVR and its
  Quest client. The native Quest app obtains input through `openxr_interface`.

!!! note "Boba CUDA environment"
    The pinned Boba simulation/renderer and its CUDA extensions require CUDA 13.2
    in the `boba-cu132` environment. This requirement belongs to Boba; the general
    ILLIXR offload encoder retains its CUDA 12.8+ and CUDA 13 support. Boba's host
    RGBA conversion is built separately from the general offload conversion code.

## One-time Boba setup

From an ILLIXR source checkout, run:

```bash
./scripts/setup_boba_immersive.sh
```

The script installs Boba below
`${XDG_DATA_HOME:-$HOME/.local/share}/illixr/boba_immersive` by default. It:

- checks out Boba-Public at [`612d22a`](https://github.com/jianxiapyh/Boba-Public/commit/612d22a74c2d54e3f20d2c197182090a22a20494)
  and Boba-Demo at [`684d3c2`](https://github.com/jianxiapyh/Boba-Demo/commit/684d3c2d7fc0cd4c5ba1202748cadf6584d2db01),
  including the dedicated cuSOLVER rotation solver;
- creates or validates the dedicated `boba-cu132` Conda environment using
  Boba-Public's pinned CUDA 13.2 environment specification;
- applies checksum-verified compatibility patches so both repositories use
  `boba-cu132`, and shared frame slots are invalidated before their pixels are
  overwritten; CPU loading images use staged copies while GPU gameplay images
  retain direct CUDA copies;
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
When upgrading the previous pinned runtime, known installer patches are backed
up under `.state/checkout-backups` before updating. Custom or staged edits are
preserved and cause setup to stop; use a separate `--install-root` to test an
update alongside a modified checkout. The native Lab demo can be installed with
`--skip-public-assets` because it uses Boba-Demo's packaged assets.

For a non-default location, use `--install-root` during setup and export the
same root when launching ILLIXR:

```bash
./scripts/setup_boba_immersive.sh --install-root /path/to/boba_immersive
export BOBA_IMMERSIVE_ROOT=/path/to/boba_immersive
```

`BOBA_DEMO_LAUNCHER` remains available as a direct launcher override. No
machine-specific path is compiled into the plugin.

The default launcher starts Rope. If a previous session used a Sloth launcher
override, run `unset BOBA_DEMO_LAUNCHER` before launching to restore that default.
Hold Y or B to open the object selector and switch between Rope and Sloth.

## Select Boba frame transport

Video frames, their presentation/overlay metadata, modal textures, and runtime
shutdown messages use ILLIXR's TCP backend. Headset/controller tracking updates
and the startup address handshake use UDP. Controller/view samples carry sequence
numbers so the Boba bridge can reject updates older than the last forwarded sample.

Selecting `boba_quest_native_server` as the CMake build profile, or enabling
`USE_BOBA_STREAMING_SERVER`, enables Boba's presentation and overlay metadata in
the frame protocol for that desktop build. Build the Quest client with `--boba`
as shown below so it reads the same format.

Quest Touch controller actions are guarded by `ILLIXR_ENABLE_QUEST_CONTROLLERS`.
This option defaults to OFF for device-agnostic builds; enabling Boba also enables
it. It can be enabled independently with `-DILLIXR_ENABLE_QUEST_CONTROLLERS=ON`.
`boba_immersive` and `boba_streaming_server` run on Linux, while the native app
runs `openxr_interface` on Android.

For a manual Android build, use
`./gradlew -PILLIXR_ENABLE_BOBA=ON -PILLIXR_LOCAL_SIDELOAD=ON :app:assembleRelease`.
Other offload clients can
enable the same support with CMake's `-DILLIXR_ENABLE_BOBA=ON` option. To turn it
off, use `-PILLIXR_ENABLE_BOBA=OFF` with Gradle or `-DILLIXR_ENABLE_BOBA=OFF`
with CMake and select a desktop profile without `boba_streaming_server`. Ordinary
builds default to the original frame format without this metadata. The Boba-only
fields are also omitted from compressed and decoded frames when support is disabled.

Both endpoints must use matching settings. Rebuild the desktop and Quest client
when switching between the two formats; selecting a runtime profile alone does
not change the compiled frame protocol. With `--no-build`, reuse an APK built
with the matching setting.

When updating from the earlier UDP-video version of this integration, rebuild
both the desktop and Quest app: tracking now uses the original ILLIXR UDP packet
format, and video uses TCP.

## Install the native Quest app

The first installation requires a Quest with developer mode enabled, connected
and authorized once through USB. Before installing the APK, check that ADB can
see the headset. With the Android SDK's `platform-tools` directory on your
`PATH`, run:

```bash
adb devices
```

An authorized headset appears as:

```text
List of devices attached
QUEST_SERIAL    device
```

The first column is the headset's serial number; `QUEST_SERIAL` is a placeholder
for your device's value. The `device` status means USB debugging is authorized.
If it says `unauthorized`, accept the USB debugging prompt inside the headset
and run `adb devices` again.

Activate the ILLIXR Conda environment so the host-side `protoc` compiler is
available, then run from the source checkout:

```bash
conda activate illixr
./scripts/install_quest_app.sh --boba
```

With `--boba`, the script defaults to an optimized Release APK. Use `--debug`
only when debugging the native application; an unoptimized Debug build is not a
performance baseline. The signing keystore remains at `$HOME/illixr.keystore`.
The helper's local sideload flag excludes only the Google Play target-API lint
rule; all other release checks still run.

The script builds the APK, installs it, launches `ILLIXRApp`, and prints the
Quest's Wi-Fi address and the corresponding desktop option. Use `--no-build` to
reinstall an existing APK, `--no-launch` to install without opening the app, or
`--serial SERIAL` when multiple Android devices are connected. Run
`./scripts/install_quest_app.sh --help` for Android SDK and JDK overrides.

To select a specific headset, replace `QUEST_SERIAL` with the serial number
shown by `adb devices`:

```bash
./scripts/install_quest_app.sh --boba --serial QUEST_SERIAL
```

The installed development APK appears in the Quest's **Unknown Sources** app
list. USB is not used by the runtime and may be disconnected after installation.

For performance diagnosis, leave USB connected to capture logs while video
continues to use Wi-Fi:

```bash
adb -s QUEST_SERIAL logcat -v threadtime > quest-boba.log
```

Capture the desktop output in a separate terminal. Compare `Boba native stream`
FPS, encode time, send time, rejected inputs, and skipped source frames with
`Boba receiver`, `Selected decoder`, `Decoder FPS`, and decode latency in the
Quest log. Source frame skips are expected when Boba renders faster than the
configured stream rate. A slow synchronous TCP send is included in `ms send`.

The native stream is paced at 72 FPS by default, with 30 Mbps AV1 and unchanged
4288 × 2240 combined output. The 1344 × 1344 source eyes retain the upstream
rendering settings. Native mode hides the separate desktop spectator view by
default; enable it with `BOBA_DESKTOP_PREVIEW=true` when launching ILLIXR. That
extra camera consumes rendering time without changing the headset image.
Recycled input is rejected before NVENC advances its
reference state. If the client drops an encoded input, it waits for a keyframe
before accepting dependent frames again.

## Run over Wi-Fi without ALVR or SteamVR

Put the Quest and desktop on the same local network, open `ILLIXRApp` on an
awake Quest, and pass its Wi-Fi address to the desktop process:

```bash
./main.opt.exe \
  --yaml=profiles/boba_quest_native_server.yaml \
  --duration=600 \
  --quest-ip 192.168.x.x
```

`--duration=600` allows 10 minutes of testing; use Ctrl+C to stop earlier.
Without a duration override, ILLIXR stops after 60 seconds and closes the native
Quest app through the shutdown channel.

Adjust the executable and profile paths for the selected build or install
directory. The desktop sends a small configuration handshake to the Quest; the
Quest learns the desktop address from that packet and connects back through the
existing ILLIXR transport. No desktop address needs to be entered on the
headset. The desktop waits up to 120 seconds for the app by default; change this
with `--quest-connect-timeout SECONDS`.

The Java listener is an optional UDP address bootstrap, chosen because Java owns
NativeActivity startup and teardown. It learns the desktop's reachable IP from
the packet source; native C++ backends carry ongoing tracking and video. Existing
`ILLIXR_SERVER_IP`, `ILLIXR_TCP_SERVER_IP`, `ILLIXR_TCP_CLIENT_IP`, and TCP/UDP
port environment settings are retained. A preconfigured Boba client starts without
waiting for discovery; generic offload builds start directly with their configured
addresses. The optional `illixr_server_ip` Android intent remains available in Boba.

Allow UDP ports 9010 and 9003 and TCP port 9001 on the local firewall. The app
must be open because a stopped Android application cannot be awakened over an
ordinary LAN connection. The headset must also be awake for OpenXR to supply
valid tracking and controller data.

## ALVR/SteamVR baseline

For the `boba_quest` profile, Steam, SteamVR, and ALVR are outside the setup
script. Start the paired ALVR Quest client and SteamVR before launching ILLIXR.
This path remains available for comparison and rollback.

## Frame delivery regression checks

The keyframe recovery checks run without a headset or GPU:

```bash
cmake -S tests/boba -B build/boba-checks
cmake --build build/boba-checks
ctest --test-dir build/boba-checks --output-on-failure
```

To include the NVENC test, configure with
`-DBOBA_NVENC_LIBRARY=/path/to/libplugin.boba_streaming_server.opt.so` and
`-DVIDEO_CODEC_SDK_PATH=/path/to/nv-codec-headers`, using the same CUDA toolkit,
C++ runtime, and dependency prefix as the desktop build. It rejects recycled
inputs after upload, verifies that a requested keyframe survives rejection, and
writes 12 accepted frames to `build/boba-checks/stereo.obu` for decoder checks.

To check CPU loading images between GPU gameplay frames, run this from the
ILLIXR checkout with the configured Boba CUDA environment:

```bash
conda run --no-capture-output -n boba-cu132 \
  python tests/boba/cpu_loading_frames.py "$BOBA_IMMERSIVE_ROOT/Boba-Demo"
```

This GPU test uses temporary local IPC without a headset. It checks exact pixel
contents and frame generations for alternating GPU and CPU stereo inputs, and
verifies that GPU frames continue using direct copies after CPU loading images.
