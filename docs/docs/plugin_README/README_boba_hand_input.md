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

Use `scripts/setup_boba_immersive.sh` to install this branch's pinned companion,
or point `BOBA_IMMERSIVE_ROOT` at the matching companion checkout. Rebuild the
desktop Boba plugins and the native client with Boba enabled:

```bash
./scripts/install_quest_app.sh --boba --release
```

Enable hand tracking on the Quest and put down the controllers. Open the hand
before the first pinch; pinch advances the tutorial and starts the demo when
Ready. Hold a pinch over an interaction marker to grab, move the hand, then
release. Controller input remains available when controllers are active.

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

On a physical Quest, verify hands-only startup, marker selection, sustained grab
and movement, release, hand occlusion/reacquisition, Game Select/Close, Rope →
Sloth → Rope, and a transition back to Touch controllers. Physical gesture
recognition and visual targeting must be checked on the headset.
