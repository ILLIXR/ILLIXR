#include "plugin.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ILLIXR {

namespace {

    // NVENC requires aligned surfaces. Only the visible source rectangle is sent
    // to the Quest renderer; padding remains an encoder implementation detail.
    constexpr std::uint32_t kPerEyeVisibleWidth         = NATIVE_STREAM_EYE_WIDTH;
    constexpr std::uint32_t kPerEyeVisibleHeight        = NATIVE_STREAM_EYE_HEIGHT;
    constexpr std::uint32_t kPerEyeEncodeWidth          = (kPerEyeVisibleWidth + 31U) & ~31U;
    constexpr std::uint32_t kEncodeHeight               = (kPerEyeVisibleHeight + 31U) & ~31U;
    constexpr std::uint32_t kOverlayCommandStrideFloats = data_format::boba_frame_overlay::command_stride_floats;
    constexpr std::uint64_t kModalTextureResendFrames   = 300;

    XrPosef to_xr_pose(const data_format::stereo_render_view& view) {
        XrPosef pose{};
        pose.orientation.w = 1.0F;
        if (view.valid) {
            pose.position    = {view.position.x(), view.position.y(), view.position.z()};
            pose.orientation = {view.orientation.x(), view.orientation.y(), view.orientation.z(), view.orientation.w()};
        }
        return pose;
    }

    std::uint64_t system_time_ns() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

} // namespace

// ---- Shared-ring mapping and validation -----------------------------------

boba_streaming_server::mapped_file::~mapped_file() {
    reset();
}

void boba_streaming_server::mapped_file::reset() {
    if (data != nullptr) {
        munmap(const_cast<std::uint8_t*>(data), size);
    }
    if (fd >= 0) {
        close(fd);
    }
    fd   = -1;
    data = nullptr;
    size = 0;
    path.clear();
}

boba_streaming_server::boba_streaming_server(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , stereo_reader_{switchboard_->get_reader<data_format::stereo_frame>("stereo_frame")}
    , frames_writer_{switchboard_->get_network_writer<data_format::compressed_frame>(
          "compressed_frames", network::topic_config{network::topic_config::BOOST, network::topic_config::UDP})}
    , modal_writer_{switchboard_->get_network_writer<data_format::boba_modal_texture>(
          "boba_modal_texture", network::topic_config{network::topic_config::BOOST, network::topic_config::TCP})} {
    spdlogger(switchboard_->get_env_char("BOBA_STREAMING_SERVER_LOG_LEVEL", "info"));
    bitrate_   = std::max<std::int64_t>(1, switchboard_->get_env_int("BOBA_STREAM_BITRATE", 30'000'000));
    framerate_ = std::max(1, switchboard_->get_env_int("BOBA_STREAM_FRAMERATE", 72));
    plugin_logger_->info("Boba native Quest stream configured for AV1 {}x{} at {} fps / {:.1f} Mbps", kPerEyeEncodeWidth * 2,
                         kEncodeHeight, framerate_, static_cast<double>(bitrate_) / 1'000'000.0);
}

boba_streaming_server::~boba_streaming_server() = default;

// The threadloop polls switchboard state but yields when no new producer
// generation is available, avoiding a busy spin at the desktop frame rate.
threadloop::skip_option boba_streaming_server::_p_should_skip() {
    const auto frame = stereo_reader_.get_ro_nullable();
    if (frame == nullptr || frame->source_frame_id == 0 || frame->source_frame_id <= last_frame_id_) {
        return skip_option::skip_and_yield;
    }
    return skip_option::run;
}

bool boba_streaming_server::map_file(const std::string& path, mapped_file* mapping) {
    if (path.empty()) {
        return false;
    }
    if (mapping->data != nullptr && mapping->path == path) {
        return true;
    }
    mapping->reset();
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        plugin_logger_->warn("Could not open Boba frame mapping {}", path);
        return false;
    }
    struct stat status{};
    if (fstat(fd, &status) != 0 || status.st_size <= 0) {
        close(fd);
        return false;
    }
    void* mapped = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return false;
    }
    mapping->fd   = fd;
    mapping->data = static_cast<const std::uint8_t*>(mapped);
    mapping->size = static_cast<std::size_t>(status.st_size);
    mapping->path = path;
    plugin_logger_->info("Mapped Boba ring {} ({} bytes)", path, mapping->size);
    return true;
}

bool boba_streaming_server::generation_matches(const mapped_file& mapping, std::uint64_t generation_offset,
                                               std::uint64_t expected_generation) const {
    if (mapping.data == nullptr || generation_offset > mapping.size ||
        sizeof(std::uint64_t) > mapping.size - generation_offset) {
        return false;
    }
    std::uint64_t generation = 0;
    std::memcpy(&generation, mapping.data + generation_offset, sizeof(generation));
    return generation == expected_generation;
}

bool boba_streaming_server::image_range_valid(const data_format::stereo_shared_image& image) const {
    const std::uint64_t minimum_stride = static_cast<std::uint64_t>(image.width) * 4U;
    const std::uint64_t required =
        image.height == 0 ? 0 : static_cast<std::uint64_t>(image.row_stride_bytes) * (image.height - 1U) + minimum_stride;
    return image.width > 0 && image.height > 0 && image.row_stride_bytes >= minimum_stride && image.byte_count >= required &&
        image.byte_offset <= frame_mapping_.size && required <= frame_mapping_.size - image.byte_offset;
}

bool boba_streaming_server::overlay_range_valid(const data_format::stereo_overlay_command_range& range) const {
    if (range.command_count == 0) {
        return true;
    }
    if (range.command_stride_floats != kOverlayCommandStrideFloats ||
        range.command_count > data_format::boba_frame_overlay::max_commands_per_eye) {
        return false;
    }
    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(range.command_count) * range.command_stride_floats * sizeof(float);
    return range.byte_offset <= overlay_mapping_.size && byte_count <= overlay_mapping_.size - range.byte_offset;
}

bool boba_streaming_server::modal_range_valid(const data_format::stereo_modal_overlay& modal) const {
    if (!modal.visible) {
        return true;
    }
    const std::uint64_t tight_row_bytes = static_cast<std::uint64_t>(modal.width) * 4U;
    const std::uint64_t required_bytes  = modal.height == 0
         ? 0
         : static_cast<std::uint64_t>(modal.source_row_stride_bytes) * (modal.height - 1U) + tight_row_bytes;
    return modal.width > 0 && modal.height > 0 && modal.width <= 8192 && modal.height <= 8192 &&
        modal.source_row_stride_bytes >= tight_row_bytes && modal.byte_offset <= modal_mapping_.size &&
        required_bytes <= modal_mapping_.size - modal.byte_offset;
}

std::vector<float> boba_streaming_server::copy_overlay_commands(const data_format::stereo_overlay_command_range& range) const {
    const std::size_t  float_count = static_cast<std::size_t>(range.command_count) * range.command_stride_floats;
    std::vector<float> commands(float_count);
    if (float_count != 0) {
        std::memcpy(commands.data(), overlay_mapping_.data + range.byte_offset, float_count * sizeof(float));
    }
    return commands;
}

std::vector<std::uint8_t> boba_streaming_server::copy_modal_pixels(const data_format::stereo_modal_overlay& modal) const {
    const std::size_t         tight_row_bytes = static_cast<std::size_t>(modal.width) * 4U;
    std::vector<std::uint8_t> rgba(tight_row_bytes * modal.height);
    for (std::uint32_t row = 0; row < modal.height; ++row) {
        std::memcpy(rgba.data() + static_cast<std::size_t>(row) * tight_row_bytes,
                    modal_mapping_.data + modal.byte_offset + static_cast<std::uint64_t>(row) * modal.source_row_stride_bytes,
                    tight_row_bytes);
    }
    return rgba;
}

// ---- Modal-texture reliability path ---------------------------------------

std::uint64_t boba_streaming_server::modal_texture_id(const std::vector<std::uint8_t>& rgba, std::uint32_t width,
                                                      std::uint32_t height) const {
    // FNV-1a gives the modal a stable content identity across reconnects. A
    // repeated ID therefore always refers to the same dimensions and bytes.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto    mix  = [&hash](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (std::size_t shift = 0; shift < sizeof(width); ++shift) {
        mix(static_cast<std::uint8_t>((width >> (shift * 8U)) & 0xFFU));
        mix(static_cast<std::uint8_t>((height >> (shift * 8U)) & 0xFFU));
    }
    for (std::uint8_t value : rgba) {
        mix(value);
    }
    return hash == 0 ? 1 : hash;
}

void boba_streaming_server::publish_modal_texture_if_needed(const data_format::boba_modal_overlay& modal,
                                                            const std::vector<std::uint8_t>&       rgba) {
    if (!modal.visible) {
        last_modal_visible_        = false;
        modal_visible_frame_count_ = 0;
        return;
    }

    ++modal_visible_frame_count_;
    const bool changed = modal.texture_id != last_modal_texture_id_;
    const bool resend  = changed || !last_modal_visible_ || modal_visible_frame_count_ % kModalTextureResendFrames == 0;
    if (resend) {
        auto update        = modal_writer_.allocate();
        update->texture_id = modal.texture_id;
        update->width      = modal.width;
        update->height     = modal.height;
        update->rgba       = rgba;
        modal_writer_.put(std::move(update));
        plugin_logger_->info("Published Boba modal texture id={} size={}x{} bytes={}", modal.texture_id, modal.width,
                             modal.height, rgba.size());
    }
    last_modal_texture_id_ = modal.texture_id;
    last_modal_visible_    = true;
}

void boba_streaming_server::initialize_encoder() {
    if (encoder_) {
        return;
    }
    encoder_ = std::make_unique<nvenc_encoder>(kPerEyeEncodeWidth * 2, kEncodeHeight, bitrate_, framerate_, encoder_mode::color,
                                               encoder_codec::av1);
    if (!encoder_->initialize(vulkan_context{})) {
        encoder_.reset();
        throw std::runtime_error("Could not initialize the Boba NVENC AV1 encoder");
    }
}

// ---- Encoded-frame publication --------------------------------------------

void boba_streaming_server::publish_encoded(const data_format::stereo_frame& frame, std::vector<std::uint8_t>&& encoded,
                                            data_format::boba_frame_overlay&&      overlay,
                                            const data_format::boba_modal_overlay& modal, double encode_time_us) {
    auto output                  = std::make_shared<data_format::compressed_frame>();
    output->left_color           = std::move(encoded);
    output->right_color          = {};
    output->nalu_only            = false;
    output->use_depth            = false;
    output->use_motion_vectors   = false;
    output->presentation_mode    = frame.presentation_mode;
    output->content_aspect_ratio = static_cast<float>(frame.left.width) / static_cast<float>(frame.left.height);
    output->boba_overlay         = std::move(overlay);
    output->boba_modal           = modal;
    output->pose[0]              = to_xr_pose(frame.left_render_view);
    output->pose[1]              = to_xr_pose(frame.right_render_view);
    output->fov_left             = {frame.left_render_view.angle_left, frame.right_render_view.angle_left};
    output->fov_right            = {frame.left_render_view.angle_right, frame.right_render_view.angle_right};
    output->fov_up               = {frame.left_render_view.angle_up, frame.right_render_view.angle_up};
    output->fov_down             = {frame.left_render_view.angle_down, frame.right_render_view.angle_down};
    output->sent_time            = system_time_ns();
    output->frame_number         = frame.source_frame_id;
    output->pose_id              = 0;
    output->encode_time          = encode_time_us;
    output->is_keyframe          = encoder_->last_frame_was_keyframe();
    output->magic                = 0xdeadbeef;

    metrics_bytes_ += output->left_color.size();
    frames_writer_.put(std::move(output));
}

void boba_streaming_server::_p_one_iteration() {
    const auto frame = stereo_reader_.get_ro_nullable();
    if (frame == nullptr || frame->source_frame_id <= last_frame_id_) {
        return;
    }
    // Mark this ID as handled even when invalid so a malformed ring slot cannot
    // spin the thread indefinitely; the next Boba frame remains eligible.
    last_frame_id_ = frame->source_frame_id;
    if (frame->format != data_format::stereo_pixel_format::rgba8_unorm ||
        !map_file(frame->pixel_buffer_path, &frame_mapping_) || !image_range_valid(frame->left) ||
        !image_range_valid(frame->right) || frame->left.width != frame->right.width ||
        frame->left.height != frame->right.height ||
        !generation_matches(frame_mapping_, frame->pixel_generation_offset, frame->source_frame_id)) {
        plugin_logger_->warn("Dropping unsupported or stale Boba stereo frame {}", frame->source_frame_id);
        return;
    }

    // Copy small overlay commands before encoding so the network packet never
    // references memory that Boba can recycle after this iteration.
    bool overlay_generation_required = false;
    if (!frame->overlay_buffer_path.empty()) {
        if (!map_file(frame->overlay_buffer_path, &overlay_mapping_) ||
            !generation_matches(overlay_mapping_, frame->overlay_generation_offset, frame->source_frame_id) ||
            !overlay_range_valid(frame->left_overlay_commands) || !overlay_range_valid(frame->right_overlay_commands)) {
            plugin_logger_->debug("Waiting for matching Boba overlay generation {}", frame->source_frame_id);
            return;
        }
        overlay_generation_required = true;
    } else {
        overlay_mapping_.reset();
    }

    data_format::boba_frame_overlay overlay{};
    overlay.source_width  = frame->left.width;
    overlay.source_height = frame->left.height;
    if (overlay_generation_required) {
        overlay.left_commands  = copy_overlay_commands(frame->left_overlay_commands);
        overlay.right_commands = copy_overlay_commands(frame->right_overlay_commands);
    }

    // Modal placement belongs to every frame, while its potentially large RGBA
    // texture is content-addressed and transmitted separately over TCP.
    bool                            modal_generation_required = false;
    data_format::boba_modal_overlay modal{};
    std::vector<std::uint8_t>       modal_pixels;
    if (!frame->modal_buffer_path.empty()) {
        if (!map_file(frame->modal_buffer_path, &modal_mapping_) ||
            !generation_matches(modal_mapping_, frame->modal_generation_offset, frame->source_frame_id) ||
            !modal_range_valid(frame->modal)) {
            plugin_logger_->debug("Waiting for matching Boba modal generation {}", frame->source_frame_id);
            return;
        }
        modal_generation_required = true;
        modal.visible             = frame->modal.visible;
        modal.left_valid          = frame->modal.left_valid;
        modal.right_valid         = frame->modal.right_valid;
        modal.width               = frame->modal.width;
        modal.height              = frame->modal.height;
        modal.width_m             = frame->modal.width_m;
        modal.height_m            = frame->modal.height_m;
        for (std::size_t index = 0; index < 4; ++index) {
            modal.left_quad_pixels[index * 2]      = frame->modal.left_quad_pixels[index].x();
            modal.left_quad_pixels[index * 2 + 1]  = frame->modal.left_quad_pixels[index].y();
            modal.right_quad_pixels[index * 2]     = frame->modal.right_quad_pixels[index].x();
            modal.right_quad_pixels[index * 2 + 1] = frame->modal.right_quad_pixels[index].y();
        }
        if (modal.visible) {
            modal_pixels     = copy_modal_pixels(frame->modal);
            modal.texture_id = modal_texture_id(modal_pixels, modal.width, modal.height);
        }
    } else {
        modal_mapping_.reset();
        if (frame->modal.visible) {
            plugin_logger_->warn("Boba frame {} declares a modal without a modal ring", frame->source_frame_id);
            return;
        }
    }

    // Encode directly from the mmap rows. Generation markers are checked again
    // afterward, giving the ring a seqlock-like stale-read guard without copying
    // both high-resolution eye images on the CPU.
    initialize_encoder();
    const auto*               left        = frame_mapping_.data + frame->left.byte_offset;
    const auto*               right       = frame_mapping_.data + frame->right.byte_offset;
    const std::size_t         left_pitch  = frame->left.row_stride_bytes;
    const std::size_t         right_pitch = frame->right.row_stride_bytes;
    const auto                start       = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> encoded =
        encoder_->encode_rgba_stereo(left, left_pitch, right, right_pitch, frame->left.width, frame->left.height,
                                     frame->origin == data_format::stereo_image_origin::upper_left);
    const double encode_us = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    if (!generation_matches(frame_mapping_, frame->pixel_generation_offset, frame->source_frame_id) ||
        (overlay_generation_required &&
         !generation_matches(overlay_mapping_, frame->overlay_generation_offset, frame->source_frame_id)) ||
        (modal_generation_required &&
         !generation_matches(modal_mapping_, frame->modal_generation_offset, frame->source_frame_id))) {
        plugin_logger_->debug("Boba ring recycled frame {} during encode; dropping it", frame->source_frame_id);
        return;
    }
    if (encoded.empty()) {
        plugin_logger_->warn("NVENC returned an empty frame for Boba frame {}", frame->source_frame_id);
        return;
    }
    publish_modal_texture_if_needed(modal, modal_pixels);
    publish_encoded(*frame, std::move(encoded), std::move(overlay), modal, encode_us);

    // Report transport-facing throughput rather than logging every frame.
    ++metrics_frames_;
    metrics_encode_us_ += encode_us;
    const auto   now      = std::chrono::steady_clock::now();
    const double interval = std::chrono::duration<double>(now - metrics_start_).count();
    if (interval >= 1.0) {
        plugin_logger_->info("Boba native stream: {:.1f} fps, {:.2f} ms encode, {:.1f} Mbps",
                             static_cast<double>(metrics_frames_) / interval,
                             metrics_encode_us_ / static_cast<double>(metrics_frames_) / 1000.0,
                             static_cast<double>(metrics_bytes_) * 8.0 / interval / 1'000'000.0);
        metrics_start_     = now;
        metrics_frames_    = 0;
        metrics_bytes_     = 0;
        metrics_encode_us_ = 0.0;
    }
}

} // namespace ILLIXR

using namespace ILLIXR;
PLUGIN_MAIN(boba_streaming_server)
