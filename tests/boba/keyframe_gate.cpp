#include "plugins/offload_rendering_client/android/keyframe_gate.hpp"

#include <stdexcept>

int main() {
    ILLIXR::keyframe_gate gate;
    const auto            require = [](bool condition) {
        if (!condition) {
            throw std::runtime_error("keyframe recovery check failed");
        }
    };

    // Connecting midway through a GOP must not feed missing-reference frames.
    require(!gate.accept(false, false));
    require(gate.accept(true, false));
    require(gate.accept(false, false));

    // A queue overrun invalidates the chain even after space becomes available.
    require(!gate.accept(false, true));
    require(!gate.accept(false, false));
    require(!gate.accept(false, false));
    require(gate.accept(true, false));
    require(gate.accept(false, false));

    // A new keyframe can replace queued pictures without using their references.
    require(gate.accept(true, true));
    require(gate.accept(false, false));

    gate.reset();
    require(!gate.accept(false, false));
    require(gate.accept(true, false));
}
