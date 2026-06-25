#include "plugin.hpp"

#include "nvdec_decoder.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;
using namespace ILLIXR::bridge;

[[maybe_unused]] semantic_frame_decoder::semantic_frame_decoder(const std::string& name, ILLIXR::phonebook* pb)
    : plugin{name, pb}
    , switchboard_{pb->lookup_impl<switchboard>()}
    , decoded_writer_{switchboard_->get_network_writer<semantic_xr::semantic_data>("semantic_data", {})} {
    // Register decode callback — fires on the switchboard thread whenever a
    // new semantic_frame frame arrives, decoding it immediately into the cache.
    switchboard_->schedule<data_format::semantic_frame>(
        id_, "semantic_frame", [this](const switchboard::ptr<const data_format::semantic_frame>& frame, std::size_t idx) {
            on_semantic_data(frame, idx);
        });
}

void semantic_frame_decoder::stop() { }

void semantic_frame_decoder::on_semantic_data(const switchboard::ptr<const data_format::semantic_frame>& frame,
                                              std::size_t /*idx*/) {
    spdlog::get("illixr")->debug("Callback called");
    if (!frame) {
        spdlog::get("illixr")->warn("[semantic_frame_decoder] on_semantic_data: null frame");
        return;
    }
    if (frame->image.empty()) {
        spdlog::get("illixr")->warn("[semantic_frame_decoder] on_semantic_data: frame={} has empty image", frame->frame_number);
        return;
    }

    spdlog::get("illixr")->debug("[semantic_frame_decoder] on_semantic_data: frame={} image={}B depth={}B", frame->frame_number,
                                 frame->image.size(), frame->depth.size());

    // Lazy decoder init — constructed on the first callback invocation so
    // its CUDA context is bound to the switchboard decode thread.
    {
        std::lock_guard<std::mutex> lock(decoder_init_mutex_);
        if (!decoder_initialized_) {
            spdlog::get("illixr")->info("[semantic_frame_decoder] Initializing NVDEC HEVC decoder...");
            try {
                decoder_             = std::make_unique<nvdec_decoder>(cudaVideoCodec_HEVC);
                decoder_initialized_ = true;
                spdlog::get("illixr")->info("[semantic_frame_decoder] NVDEC HEVC decoder initialized");
            } catch (const std::exception& e) {
                spdlog::get("illixr")->error("[semantic_frame_decoder] NVDEC init failed: {}", e.what());
                return;
            }
        }
    }

    if (!decoder_)
        return;

    try {
        const uint8_t* rgb = decoder_->decode(frame->image.data(), frame->image.size(), frame->intrinsics.width, frame->intrinsics.height);

        if (rgb != nullptr) {
            spdlog::get("illixr")->info(
                "[decoder] intrinsics={}x{} decoder={}x{} rgb_ptr={}",
                frame->intrinsics.width, frame->intrinsics.height,
                decoder_->width(), decoder_->height(),
                (void*)rgb);
            semantic_xr::semantic_data data;
            data.image_.assign(rgb, rgb + (frame->intrinsics.width * frame->intrinsics.height * 3));
            data.image_width_  = frame->intrinsics.width;
            data.image_height_ = frame->intrinsics.height;
            float intr[4]      = {frame->intrinsics.fx, frame->intrinsics.fx, frame->intrinsics.cx, frame->intrinsics.cy};
            std::copy(std::begin(intr), std::end(intr), std::begin(data.intrinsics_));
            data.depth_        = frame->depth;
            data.depth_width_  = frame->depth_intrinsics.width;
            data.depth_height_ = frame->depth_intrinsics.height;
            data.depth_near_z_ = frame->depth_near_z;
            float dintr[4]     = {frame->depth_intrinsics.fx, frame->depth_intrinsics.fx, frame->depth_intrinsics.cx,
                                  frame->depth_intrinsics.cy};
            std::copy(std::begin(dintr), std::end(dintr), std::begin(data.depth_intrinsics_));
            std::copy(std::begin(frame->rgb_camera_pose), std::end(frame->rgb_camera_pose), std::begin(data.rgb_camera_pose_));
            std::copy(std::begin(frame->depth_pose), std::end(frame->depth_pose), std::begin(data.depth_pose_));
            data.max_depth_m_  = frame->max_depth;
            data.frame_number_ = frame->frame_number;
            data.width_        = data.image_width_;
            data.height_       = data.image_height_;

            spdlog::get("illixr")->info(
                "[decoder] publishing: image_.size()={} width_={} height_={} "
                "first_pixel=({},{},{}) mid_pixel=({},{},{})",
                data.image_.size(),
                data.image_width_, data.image_height_,
                data.image_[0], data.image_[1], data.image_[2],
                data.image_[data.image_.size()/2],
                data.image_[data.image_.size()/2 + 1],
                data.image_[data.image_.size()/2 + 2]);

            decoded_writer_.put(decoded_writer_.allocate<semantic_xr::semantic_data>(std::move(data)));

            spdlog::get("illixr")->debug("[semantic_frame_decoder] frame={} decoded -> {}x{} RGB pushed to switchboard",
                                         frame->frame_number, decoder_->width(), decoder_->height());
        } else {
            // nullptr is normal for SPS/PPS-only packets during decoder init —
            // log at debug level so the first few frames don't look like errors.
            spdlog::get("illixr")->debug("[semantic_frame_decoder] frame={} decode returned no output "
                                         "(normal during decoder init with SPS/PPS packets)",
                                         frame->frame_number);
        }
    } catch (const std::exception& e) {
        spdlog::get("illixr")->warn("[semantic_frame_decoder] NVDEC decode failed frame={}: {}", frame->frame_number, e.what());
    }
}

PLUGIN_MAIN(semantic_frame_decoder)
