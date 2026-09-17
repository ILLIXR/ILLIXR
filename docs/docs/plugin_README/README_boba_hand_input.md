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

For bare hands, the sampler also calls the existing `XR_EXT_hand_tracking`
routine at that display time. Each hand must have an active joint tracker and a
valid, tracked wrist. Aim/grip actions alone can retain predicted poses after
tracking is lost. An absent hand therefore produces neutral input even if its
actions still appear active, releasing only its own grab. Failed or unavailable
joint tracking also produces neutral hand input. The pose thread and Boba frame
sampler use separate output snapshots; Boba never reads an older cached hand.

The [companion runtime](https://github.com/ILLIXR/Boba-ILLIXR/tree/hand-interaction)
adds a floating **Game Select** button inset from the upper-right
of the view. Put the hand's index fingertip on it, wait for the teal
highlight and white fingertip dot, then pinch and release. The Rope, Sloth,
Exit Game, and Close panel expands leftward and downward. Close dismisses the
menu; Exit Game uses the existing demo shutdown path and requests native client
shutdown. Exit needs a fresh pinch, so holding the opening pinch cannot quit.
This uses the existing bitmap overlay, texture cache, and native transport.
Pinch recognition comes from the runtime; no new hand tracker is needed.

The native client now renders red and blue animated OpenXR hand meshes. Their
fingertips follow the pointing target in both eyes even before pinch input is
ready. Pinching selects or grabs; releasing keeps the pointer visible while the
hand is tracked. The client draws pointing feedback above the selector panel.
The decorative arrows are removed, and hand mode draws no laser or
controller-origin dots. The aim ray is still used internally for targeting.
The open-hand cursor follows the aim location without snapping to nearby
markers. During a pinched grab it follows the exact contact marker, so large
movements do not separate the hand from the grabbed point. Release restores
free aiming. Menu hits use the displayed fingertip and current-frame panel;
the cursor is placed at that same panel point in each stereo eye. Release an
object before using that hand for the menu. Hand selection uses the adapter's
pinch button state;
weak analog pinch values do not acquire or sustain a grab. Releasing the pinch
or losing pinch readiness ends the grab with the existing two-frame release
confirmation. A new first tutorial slide explains these hand controls.

The runtime supplies bind geometry through
[`XR_FB_hand_tracking_mesh`](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrHandTrackingMeshFB.html).
It is cached once per hand and skinned with the same joint sample used for input.
The effective runtime scale is queried without changing the existing joint output.
Finger articulation and orientation relative to the headset are retained, but
the mesh's index fingertip is anchored to Boba's existing interaction cursor.
This deliberately displays a small animated hand at the target, rather than a
life-size hand at the user's physical position. The existing Vulkan overlay
pipeline draws the shaded triangles above the menu.

Command type 2 extends the existing 14-float overlay stream with the per-eye
cursor anchor, hand side, and fallback group length. No mesh/joint data is sent
across the network. Old clients ignore that command and render the following
phone-demo icon lines. The updated client uses those icons if a runtime mesh is
unavailable, and hides either representation when its current hand is absent
or the displayed cursor frame has been stale for 250 ms.

**Rebuild and reinstall the Quest APK for the mesh renderer**, including when
the APK from `78ce974` is already installed. Restart the matching desktop too.

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
APK before testing hand pointers and pinch controls. Presence checks and mesh
rendering run on the Quest, so installing the updated APK is required; restart the desktop
demo to load the matching pointer rendering changes.

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

The mesh check uses deterministic geometry with the production loader, skinning,
and projection code. It verifies fingertip anchoring, articulation, head rotation,
runtime scale, lost hands, invalid geometry, and cursor metadata. Eigen3 and
OpenXR headers are required; use `-DOPENXR_INCLUDE_DIR=...` for a local SDK.
It does not substitute for headset appearance or gesture checks.

With info logging enabled (`ILLIXR_LOG_LEVEL=info`; the Release default is warn),
`[boba_hand_mesh] left/right: cached ...` confirms actual runtime
geometry was retrieved. `rendering animated cursor mesh` confirms the renderer
received a tracked hand and a matching cursor. `mesh unavailable` means the icon
fallback is active. Check both eyes while curling fingers, pinching/releasing,
grabbing with large movement, hovering menu buttons, and hiding one hand at a time.

`boba_hand_input` compiles the production sampling method against deterministic
action sources to check each hand independently, runtime readiness, inactive
joint trackers, predicted/untracked wrists, tracking loss, failed
queries, and controller fallback. Android compilation checks the actual OpenXR
API. The companion's `test/test_hand_interaction.py` covers the input bridge,
existing grab path, menu interaction, and stale-input handling.

On a physical Quest, put down the controllers and show both open hands:

| Check | Action and expected behavior |
| --- | --- |
| Tutorial | The first page explains hand controls. Pinch and release for each page; weak pinch values must not advance it. Wait for Ready before the final pinch. |
| Open-hand pointer | Before pinching, move each open hand. Its red/blue hand-only icon should follow continuously in both eyes, without arrows or a laser. |
| Marker | Place the hand icon's fingertip on an interaction marker. |
| Grab / move / release | Hold an index–thumb pinch and move the hand, including a large upward motion. The fingertip stays on the grabbed contact in both eyes. Release to let go and return to free aiming. |
| Hover / release | Move an open hand near the rope. The cursor follows the aim location without snapping to a marker or grabbing. After a grab, open the pinch: the attachment must release without pinching again, even if readiness drops. |
| Tracking recovery | Hide the hand while holding; the pointer disappears and the grab releases. Show an open hand before pinching again. |
| One hand missing | Hide only the left hand while both are holding, then repeat for the right. The missing hand must disappear and release immediately; the other must keep working. Returning while still pinching must not resume the lost grab. |
| Menu | Move the fingertip onto upper-right **Game Select**. When the button fills teal and the white fingertip dot appears, pinch and release. Check alignment while turning the head and at each button edge. |
| Close | Pinch **Close**. Holding that pinch must not grab an object behind the panel. |
| Game switching | Select Sloth, wait for loading, then choose Rope. |
| Both hands | Repeat with each hand. Pinching off-panel must not activate the other hand's hovered button. |
| Controllers | Pick up Touch controllers and check trigger grab/release and Y/B + joystick selection. |
| Shutdown | Open Game Select, release the opening pinch, then point at Exit Game and pinch. The desktop demo and native client should stop. Ctrl+C and a 0.75-second controller side-grip hold also exit. Relaunch the app and server to check reconnection. |

Check both eyes for button visibility and pointer alignment while moving the head.
Physical gesture recognition and headset comfort still require Quest testing;
synthetic input checks cannot establish these.
