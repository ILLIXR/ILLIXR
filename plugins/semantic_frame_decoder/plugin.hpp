#pragma once

#include "illixr/bridge/semantic_xr/semantic_data.hpp"
#include "illixr/data_format/semantics.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "nvdec_decoder.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ILLIXR {
class semantic_frame_decoder : public plugin {
public:
    [[maybe_unused]] explicit semantic_frame_decoder(const std::string& name, phonebook* pb);

    void stop() override;

private:
    // Fired by switchboard whenever a new semantic_data frame arrives.
    // Decodes image -> RGB uint8 and stores in decoded_frames_ cache.
    void on_semantic_data(const switchboard::ptr<const data_format::semantic_frame>& frame, std::size_t idx);

    const std::shared_ptr<switchboard>                      switchboard_;
    switchboard::writer<bridge::semantic_xr::semantic_data> decoded_writer_;

    // NVDEC decoder — constructed lazily on the first decode callback
    // invocation, so its CUDA context is bound to that thread.
    std::unique_ptr<nvdec_decoder> decoder_;
    std::mutex                     decoder_init_mutex_;
    bool                           decoder_initialized_ = false;
};

} // namespace ILLIXR
