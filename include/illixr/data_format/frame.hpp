#pragma once

#include <array>
#include <GL/gl.h>

#include "illixr/switchboard.hpp"
#include "illixr/data_format/pose.hpp"

namespace ILLIXR::data_format {
// Using arrays as a swapchain
// Array of left eyes, array of right eyes
// This more closely matches the format used by Monado
struct [[maybe_unused]]rendered_frame : public switchboard::event {
    std::array<GLuint, 2> swapchain_indices{}; // Does not change between swaps in swapchain
    std::array<GLuint, 2> swap_indices{};      // Which element of the swapchain
    fast_pose_type        render_pose;         // The pose used when rendering this frame.
    time_point            sample_time{};
    time_point            render_time{};

    rendered_frame() = default;

    rendered_frame(std::array<GLuint, 2>&& swapchain_indices_, std::array<GLuint, 2>&& swap_indices_,
                   fast_pose_type render_pose_, time_point sample_time_, time_point render_time_)
            : swapchain_indices{swapchain_indices_}
            , swap_indices{swap_indices_}
            , render_pose(std::move(render_pose_))
            , sample_time(sample_time_)
            , render_time(render_time_) { }
};

}