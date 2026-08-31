#pragma once

#include "illixr/data_format/frame.hpp"
#include "illixr/data_format/serialization/frame.hpp"
#include "illixr/data_format/stereo_frame.hpp"
#include "illixr/quest3_params.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "offload_rendering_server/nvenc/nvenc_encoder.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILLIXR {

/** Encode Boba's mmap-backed RGBA stereo_frame and stream it to the native Quest client. */
class boba_streaming_server final : public threadloop {
public:
    boba_streaming_server(const std::string& name, phonebook* pb);
    ~boba_streaming_server() override;

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    struct mapped_file {
        int                 fd{-1};
        const std::uint8_t* data{nullptr};
        std::size_t         size{0};
        std::string         path;

        mapped_file()                              = default;
        mapped_file(const mapped_file&)            = delete;
        mapped_file& operator=(const mapped_file&) = delete;
        ~mapped_file();
        void reset();
    };

    bool                      map_file(const std::string& path, mapped_file* mapping);
    bool                      generation_matches(const mapped_file& mapping, std::uint64_t generation_offset,
                                                 std::uint64_t expected_generation) const;
    bool                      image_range_valid(const data_format::stereo_shared_image& image) const;
    bool                      overlay_range_valid(const data_format::stereo_overlay_command_range& range) const;
    bool                      modal_range_valid(const data_format::stereo_modal_overlay& modal) const;
    std::vector<float>        copy_overlay_commands(const data_format::stereo_overlay_command_range& range) const;
    std::vector<std::uint8_t> copy_modal_pixels(const data_format::stereo_modal_overlay& modal) const;
    std::uint64_t modal_texture_id(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height) const;
    void publish_modal_texture_if_needed(const data_format::boba_modal_overlay& modal, const std::vector<std::uint8_t>& rgba);
    void initialize_encoder();
    void publish_encoded(const data_format::stereo_frame& frame, std::vector<std::uint8_t>&& encoded,
                         data_format::boba_frame_overlay&& overlay, const data_format::boba_modal_overlay& modal,
                         double encode_time_us);

    const std::shared_ptr<switchboard>                           switchboard_;
    switchboard::reader<data_format::stereo_frame>               stereo_reader_;
    switchboard::network_writer<data_format::compressed_frame>   frames_writer_;
    switchboard::network_writer<data_format::boba_modal_texture> modal_writer_;

    mapped_file                           frame_mapping_;
    mapped_file                           overlay_mapping_;
    mapped_file                           modal_mapping_;
    std::unique_ptr<nvenc_encoder>        encoder_;
    std::uint64_t                         last_frame_id_{0};
    std::uint64_t                         last_modal_texture_id_{0};
    std::uint64_t                         modal_visible_frame_count_{0};
    bool                                  last_modal_visible_{false};
    std::int64_t                          bitrate_{30'000'000};
    int                                   framerate_{72};
    std::chrono::steady_clock::time_point metrics_start_{std::chrono::steady_clock::now()};
    std::uint64_t                         metrics_frames_{0};
    std::uint64_t                         metrics_bytes_{0};
    double                                metrics_encode_us_{0.0};
};

} // namespace ILLIXR
