#!/usr/bin/env python3
"""Compile the production hand sampler against deterministic action sources.

Checks transport adaptation, not Quest gesture recognition. The APK build checks
the actual OpenXR interfaces; a headset check is still needed for runtime behavior.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / "plugins/openxr_interface/oxr_relay.cpp").read_text()
    start = source.index("bool oxr_relay::query_controller_hand(")
    sampler = source[start:source.index("\n}\n", start) + 3]
    header = (root / "include/illixr/data_format/quest_controller.hpp").read_text()
    start = header.index("enum class quest_controller_profile")
    profile_enum = header[start:header.index("\n};", start) + 3]
    harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <algorithm>
using XrTime = std::int64_t;
namespace pose { enum { AIM, GRIP, PINCH, POKE }; }
constexpr float kControllerTriggerThreshold = 0.75F;
constexpr float kControllerSqueezeThreshold = 0.85F;
PROFILE_ENUM
struct quest_controller_pose {
    bool active = false, position_valid = false, orientation_valid = false;
    bool position_tracked = false, orientation_tracked = false;
    float x = 0;
    bool valid() const { return active && position_valid && orientation_valid; }
    bool tracked() const { return active && position_tracked && orientation_tracked; }
};
struct quest_controller_button {
    bool active = false, pressed = false;
    float value = 0;
};
struct quest_controller_axis2d { bool active = false; };
struct quest_hand_controller {
    quest_controller_profile interaction_profile = quest_controller_profile::none;
    bool available = false;
    quest_controller_pose grip_pose, aim_pose;
    quest_controller_button trigger, squeeze, primary, secondary, thumbstick_click;
    quest_controller_axis2d thumbstick;
};
struct oxr_relay {
    std::array<quest_controller_profile, 2> controller_profiles_{
        quest_controller_profile::hand_interaction, quest_controller_profile::hand_interaction};
    int hand_subaction_paths_[2] = {0, 1};
    int interaction_pose_actions_[4] = {10, 11, 12, 13};
    int interaction_pose_spaces_[2][4] = {{20, 21, 22, 23}, {30, 31, 32, 33}};
    int interaction_value_actions_[3] = {40, 41, 42};
    int interaction_ready_actions_[3] = {50, 51, 52};
    int controller_trigger_click_action_ = 60, controller_trigger_value_action_ = 61;
    int controller_squeeze_value_action_ = 62, controller_primary_click_action_ = 63;
    int controller_secondary_click_action_ = 64, controller_thumbstick_click_action_ = 65;
    int controller_thumbstick_axis_action_ = 66;
    bool ready = true, action_active = true, tracked = true, failed = false;
    float pinch[2] = {1.0F, 0.0F};
    int physical_queries = 0;
    bool query_controller_pose(int action, int space, int hand, XrTime t, quest_controller_pose* out) {
        assert(t == 123);
        assert(action == 10 || action == 11);
        assert(space == (hand ? 30 : 20) + action - 10);
        *out = {true, true, true, tracked, tracked, float(action + hand)};
        return true;
    }
    bool query_controller_boolean(int action, int, quest_controller_button* out) {
        if (action >= 60) ++physical_queries;
        *out = {action_active, action == 52 ? ready : true, 1.0F};
        return !failed;
    }
    bool query_controller_float(int action, int hand, float threshold, quest_controller_button* out) {
        if (action >= 60) ++physical_queries;
        float value = action == 42 ? pinch[hand] : 1.0F;
        *out = {action_active, value >= threshold, value};
        return !failed;
    }
    bool query_controller_axis(int, int, quest_controller_axis2d* out) {
        ++physical_queries;
        out->active = true;
        return true;
    }
    static void merge_controller_button(quest_controller_button* out, const quest_controller_button& value) {
        out->active |= value.active;
        out->pressed |= value.pressed;
        out->value = std::max(out->value, value.value);
    }
    bool query_controller_hand(std::size_t, XrTime, quest_hand_controller*);
};
SAMPLER
int main() {
    static_assert(static_cast<int>(quest_controller_profile::hand_interaction) == 7);
    oxr_relay relay;
    quest_hand_controller hand;
    assert(relay.query_controller_hand(0, 123, &hand));
    assert(hand.available && hand.trigger.active && hand.trigger.pressed);
    assert(hand.grip_pose.x == 11 && hand.aim_pose.x == 10);
    assert(!hand.primary.active && !hand.secondary.active && !hand.squeeze.active);
    assert(relay.physical_queries == 0);
    assert(relay.query_controller_hand(1, 123, &hand));
    assert(hand.available && hand.trigger.active && !hand.trigger.pressed && hand.trigger.value == 0);
    relay.ready = false;
    assert(relay.query_controller_hand(0, 123, &hand));
    assert(!hand.trigger.active && !hand.trigger.pressed && hand.trigger.value == 0);
    relay.ready = true;
    relay.tracked = false;
    assert(relay.query_controller_hand(0, 123, &hand));
    assert(!hand.trigger.pressed && hand.trigger.value == 0);
    relay.tracked = true;
    relay.action_active = false;
    assert(relay.query_controller_hand(0, 123, &hand));
    assert(!hand.trigger.active && !hand.trigger.pressed && hand.trigger.value == 0);
    relay.failed = true;
    assert(!relay.query_controller_hand(0, 123, &hand));
    assert(!hand.available && !hand.trigger.active && !hand.grip_pose.active);
    relay.failed = false;
    relay.action_active = true;
    relay.controller_profiles_[0] = quest_controller_profile::oculus_touch;
    assert(relay.query_controller_hand(0, 123, &hand));
    assert(hand.trigger.pressed && hand.primary.pressed && hand.thumbstick.active);
    assert(relay.physical_queries == 7);
}
'''.replace("PROFILE_ENUM", profile_enum).replace("SAMPLER", sampler)
    with tempfile.TemporaryDirectory(prefix="boba-hand-input-") as directory:
        path = Path(directory)
        (path / "sampler.cpp").write_text(harness)
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", str(path / "sampler.cpp"),
                        "-o", str(path / "sampler")], check=True)
        subprocess.run([str(path / "sampler")], check=True)
    print("PASS: existing hand actions, release, readiness, loss, failure, both hands, controller fallback")


if __name__ == "__main__":
    main()
