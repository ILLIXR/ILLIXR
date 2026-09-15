#pragma once

#include "illixr/data_format/frame.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace ILLIXR {

// Keep the base event type (and its wire format) unchanged. Only queued server
// frames own these packet references; received frames retain their existing
// client-side ownership convention.
template<typename... Args>
std::shared_ptr<data_format::compressed_frame> make_ffmpeg_queued_frame(Args&&... args) {
    auto                           frame  = std::make_unique<data_format::compressed_frame>(std::forward<Args>(args)...);
    const std::array<AVPacket*, 6> source = {frame->left_color,  frame->right_color,     frame->left_depth,
                                             frame->right_depth, frame->left_motion_vec, frame->right_motion_vec};
    frame->left_color = frame->right_color = frame->left_depth = frame->right_depth = nullptr;
    frame->left_motion_vec = frame->right_motion_vec = nullptr;

    std::shared_ptr<data_format::compressed_frame> owned(frame.release(), [](data_format::compressed_frame* value) {
        av_packet_free(&value->left_color);
        av_packet_free(&value->right_color);
        av_packet_free(&value->left_depth);
        av_packet_free(&value->right_depth);
        av_packet_free(&value->left_motion_vec);
        av_packet_free(&value->right_motion_vec);
        delete value;
    });
    const std::array<AVPacket**, 6> destination = {&owned->left_color,  &owned->right_color,     &owned->left_depth,
                                                   &owned->right_depth, &owned->left_motion_vec, &owned->right_motion_vec};
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] != nullptr) {
            *destination[i] = av_packet_clone(source[i]);
            if (*destination[i] == nullptr) {
                throw std::runtime_error{"Failed to retain encoded packet for the send queue"};
            }
        }
    }
    return owned;
}

} // namespace ILLIXR
