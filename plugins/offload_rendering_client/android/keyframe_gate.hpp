#pragma once

namespace ILLIXR {

/** Resume an inter-frame stream only at a keyframe after dropping an input. */
class keyframe_gate {
public:
    bool accept(bool is_keyframe, bool queue_full) {
        if (is_keyframe) {
            waiting_for_keyframe_ = false;
        } else if (queue_full) {
            waiting_for_keyframe_ = true;
        }
        return !waiting_for_keyframe_;
    }

    void reset() {
        waiting_for_keyframe_ = true;
    }

private:
    bool waiting_for_keyframe_{true};
};

} // namespace ILLIXR
