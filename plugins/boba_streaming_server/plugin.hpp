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

/**
 * Encode Boba's mmap-backed RGBA output and stream it to the native Quest client.
 *
 * Pixel frames and small vector overlays travel together over the low-latency
 * `compressed_frames` UDP topic. Larger modal textures are content-addressed,
 * cached on the Quest, and sent separately over TCP so packet loss cannot leave
 * a partially updated UI texture.
 */
class boba_streaming_server final : public threadloop {
public:
    /** Bind the local stereo input and the UDP/TCP network outputs. */
    boba_streaming_server(const std::string& name, phonebook* pb);
    /** Release the encoder and any mapped producer rings. */
    ~boba_streaming_server() override;

protected:
    /** Sleep until Boba publishes a source frame newer than the last handled ID. */
    skip_option _p_should_skip() override;

    /** Snapshot, validate, encode, and publish one Boba frame. */
    void _p_one_iteration() override;

private:
    /** Read-only RAII mapping for a producer-owned shared-memory ring. */
    struct mapped_file {
        int                 fd{-1};
        const std::uint8_t* data{nullptr};
        std::size_t         size{0};
        std::string         path;

        mapped_file()                              = default;
        mapped_file(const mapped_file&)            = delete;
        mapped_file& operator=(const mapped_file&) = delete;
        ~mapped_file();
        /** Unmap the current ring, close it, and forget its source path. */
        void reset();
    };

    /** Reuse an existing mapping or replace it when the producer path changes. */
    bool map_file(const std::string& path, mapped_file* mapping);

    /** Test the ring-slot generation marker without unaligned integer access. */
    bool generation_matches(const mapped_file& mapping, std::uint64_t generation_offset,
                            std::uint64_t expected_generation) const;

    /** Bounds-check a shared RGBA image against the currently mapped frame ring. */
    bool image_range_valid(const data_format::stereo_shared_image& image) const;

    /** Validate count, stride, and byte range for one eye's vector overlays. */
    bool overlay_range_valid(const data_format::stereo_overlay_command_range& range) const;

    /** Validate dimensions, row pitch, and byte range for an optional modal bitmap. */
    bool modal_range_valid(const data_format::stereo_modal_overlay& modal) const;

    /** Copy overlay commands out of the recyclable producer ring. */
    std::vector<float> copy_overlay_commands(const data_format::stereo_overlay_command_range& range) const;

    /** Repack a potentially padded modal bitmap into tightly packed RGBA rows. */
    std::vector<std::uint8_t> copy_modal_pixels(const data_format::stereo_modal_overlay& modal) const;

    /** Produce a stable content identity from modal dimensions and RGBA bytes. */
    std::uint64_t modal_texture_id(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height) const;

    /** Reliably publish a changed modal texture and periodically refresh the cache. */
    void publish_modal_texture_if_needed(const data_format::boba_modal_overlay& modal, const std::vector<std::uint8_t>& rgba);

    /** Lazily create the side-by-side AV1 NVENC encoder. */
    void initialize_encoder();

    /** Attach timing, pose, and overlay metadata and send the encoded frame over UDP. */
    void publish_encoded(const data_format::stereo_frame& frame, std::vector<std::uint8_t>&& encoded,
                         data_format::boba_frame_overlay&& overlay, const data_format::boba_modal_overlay& modal,
                         double encode_time_us);

    // Local input and network transports.
    const std::shared_ptr<switchboard>                           switchboard_;
    switchboard::reader<data_format::stereo_frame>               stereo_reader_;
    switchboard::network_writer<data_format::compressed_frame>   frames_writer_;
    switchboard::network_writer<data_format::boba_modal_texture> modal_writer_;

    // Shared rings and the lazily initialized hardware encoder.
    mapped_file                    frame_mapping_;
    mapped_file                    overlay_mapping_;
    mapped_file                    modal_mapping_;
    std::unique_ptr<nvenc_encoder> encoder_;
    // Frame/modal state used for duplicate suppression and reliable texture refresh.
    std::uint64_t last_frame_id_{0};
    std::uint64_t last_modal_texture_id_{0};
    std::uint64_t modal_visible_frame_count_{0};
    bool          last_modal_visible_{false};
    // Runtime encoding configuration and one-second rolling metrics.
    std::int64_t                          bitrate_{30'000'000};
    int                                   framerate_{72};
    std::chrono::steady_clock::time_point metrics_start_{std::chrono::steady_clock::now()};
    std::uint64_t                         metrics_frames_{0};
    std::uint64_t                         metrics_bytes_{0};
    double                                metrics_encode_us_{0.0};
};

} // namespace ILLIXR
