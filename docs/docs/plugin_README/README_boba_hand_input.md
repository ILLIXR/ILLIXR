# Boba hand input on the native Quest client

This change is stacked on [PR #498](https://github.com/ILLIXR/ILLIXR/pull/498).
It reuses the hand actions already created by `oxr_relay::init_hand_interaction`:

| Existing action | Boba input |
| --- | --- |
| `/input/aim/pose` | Marker and menu ray |
| `/input/grip/pose` | Translation of a held object |
| `/input/pinch_ext/value` | Select / hold / release |
| `/input/pinch_ext/ready_ext` | Whether pinch input is usable |

The native sampler reads these actions under the existing action mutex at the
same predicted display time as the stereo views. It uses the existing
`quest_controller` event and Boba packet layout, tagging the interaction profile
as `hand_interaction` (value 7). Controller sampling retains the existing Touch
bindings. The Quest manifest already declares hand tracking and the OpenXR
interface already enables `XR_EXT_hand_interaction` when available.

The [companion runtime](https://github.com/ILLIXR/Boba-ILLIXR/tree/hand-interaction)
adds a floating **Game Select** button at the lower-right
of the view. Aim and pinch opens a panel with Rope, Sloth, and Close buttons.
This uses the existing bitmap overlay, texture cache, and native transport.
Pinch recognition comes from the runtime; no new hand tracker is needed.

The companion also reuses the phone demo's red and blue hand icons. Their
fingertips follow the pointing target in both eyes even before pinch input is
ready. Pinching selects or grabs; releasing keeps the pointer visible while the
hand is tracked. The client draws pointing feedback above the selector panel.

## Build and install

Use `scripts/setup_boba_immersive.sh` to install this branch's pinned companion,
or point `BOBA_IMMERSIVE_ROOT` at the matching companion checkout. The companion
[operator guide](https://github.com/ILLIXR/Boba-ILLIXR/blob/hand-interaction/IMMERSIVE_DEMO_OPERATOR_GUIDE.md)
covers desktop and Android dependencies. Build the desktop Release profile
`profiles/boba_quest_native_server.yaml` from this checkout.

Connect the Quest by USB, wake it, and enable hand tracking in its settings.
From this ILLIXR checkout, rebuild and install the native client with Boba enabled:

```bash
adb devices
export QUEST_SERIAL=YOUR_QUEST_SERIAL  # Use the identifier shown as "device".
./scripts/install_quest_app.sh --boba --release --no-launch --serial "$QUEST_SERIAL"
```

Wait for `Success` and note the Wi-Fi address printed by the installer. Add
`--no-build` only when reinstalling an APK already built from this checkout.
Installing the older PR #498 APK replaces this app; reinstall the hand-input
APK before testing hand rays and pinch controls.

If ADB reports `device not found`, run `adb devices` again and use the current
identifier with status `device`. Reconnect the USB data cable if the list is
empty, or allow USB debugging in the headset if it says `unauthorized`.
The USB serial and Wi-Fi address are separate.

## Start the demo

Open **ILLIXRApp** from **Unknown Sources** inside the Quest and keep it open.
With USB connected, the equivalent launch command is:

```bash
adb -s "$QUEST_SERIAL" shell am force-stop com.example.native_activity
adb -s "$QUEST_SERIAL" shell am start \
  -n com.example.native_activity/com.example.ILLIXR.ILLIXRNativeActivity
```

USB is only needed for installation and ADB commands. Once the hand-input APK
is installed, start the app manually when USB is disconnected; the demo uses
Wi-Fi. Run the desktop from this ILLIXR checkout with its build environment
active:

```bash
conda activate illixr
export BOBA_IMMERSIVE_ROOT="$(realpath ../Boba-ILLIXR)"  # Matching companion checkout.
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QUEST_IP=YOUR_QUEST_WIFI_ADDRESS
unset BOBA_DEMO_LAUNCHER BOBA_DESKTOP_PREVIEW
set -o pipefail
./scripts/run_boba_quest.sh \
  --yaml=profiles/boba_quest_native_server.yaml \
  --duration=600 --quest-ip "$QUEST_IP" 2>&1 | tee boba-hand-desktop.log
```

The launcher uses `build/main.opt.exe` by default. Set `ILLIXR_BUILD_DIR` to the
desktop build directory if it is elsewhere. It finds plugin directories with
Bash globbing, so `rg` is not required, and preserves dependency directories
already in `LD_LIBRARY_PATH`. This avoids a missing `tcp_network_backend`
library caused by empty plugin discovery. A missing build or empty plugin
directory produces an error before ILLIXR starts.

For headset logs, run this in a second terminal while USB is connected, setting
`QUEST_SERIAL` there to the same identifier:

```bash
adb -s "$QUEST_SERIAL" logcat -T 1 -v threadtime >boba-hand-quest.log
```

Stop logging with Ctrl+C after the demo. Reopen ILLIXRApp before restarting the
desktop, including after a failed launch.

## Validation

```bash
cmake -S tests/boba -B build/boba-checks
cmake --build build/boba-checks
ctest --test-dir build/boba-checks --output-on-failure
```

`boba_hand_input` compiles the production sampling method against deterministic
action sources to check both hands, runtime readiness, tracking loss, failed
queries, and controller fallback. Android compilation checks the actual OpenXR
API. The companion's `test/test_hand_interaction.py` covers the input bridge,
existing grab path, menu interaction, and stale-input handling.

On a physical Quest, put down the controllers and show both open hands:

| Check | Action and expected behavior |
| --- | --- |
| Tutorial | Pinch and release for each page; wait for Ready before the final pinch. |
| Open-hand pointer | Before pinching, move each open hand. Its ray and red/blue hand icon should follow continuously in both eyes. |
| Marker | Aim the hand ray at an interaction marker. |
| Grab / move / release | Hold an index–thumb pinch, move the hand, then release. |
| Tracking recovery | Hide the hand while holding; the pointer disappears and the grab releases. Show an open hand before pinching again. |
| Menu | Aim at the lower-right **Game Select** button and pinch. The pointer should stay visible above the panel. |
| Close | Pinch **Close**. Holding that pinch must not grab an object behind the panel. |
| Game switching | Select Sloth, wait for loading, then choose Rope. |
| Both hands | Repeat with each hand. Pinching off-panel must not activate the other hand's hovered button. |
| Controllers | Pick up Touch controllers and check trigger grab/release and Y/B + joystick selection. |
| Shutdown | Press Ctrl+C or let the run end. Relaunch the app and server to check reconnection. |

Check both eyes for button visibility and ray alignment while moving the head.
Physical gesture recognition and headset comfort still require Quest testing;
synthetic input checks cannot establish these.
