#pragma once

#include "decoder_cache.hpp"

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
#include <pybind11/embed.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ILLIXR {
class semantic_python : public plugin {
public:
    [[maybe_unused]] explicit semantic_python(const std::string& name, phonebook* pb);
    ~semantic_python() override;

    void start() override;

private:
    void run_python_thread();

    /* Parses a comma-delimited string of key=value and bare-key arguments.
      "config=configs/myconfig.yaml,stride=3,max_frames=100,save_objects"
      produces:
        { "config" -> "configs/myconfig.yaml",
          "stride" -> "3",
          "max_frames" -> "100",
          "save_objects" -> "" }
     */
    void parse_py_args(const std::string& input);

    // Fired by switchboard whenever a new semantic_data frame arrives.
    // Decodes image -> RGB uint8 and stores in decoded_frames_ cache.
    void on_semantic_data(const switchboard::ptr<const data_format::semantic_data>& frame, std::size_t idx);

    const std::shared_ptr<switchboard>                       switchboard_;
    switchboard::reader<data_format::semantic_data>          semantic_reader_;
    switchboard::reader<data_format::voice_query>            voice_query_reader_;
    switchboard::network_writer<data_format::query_response> response_writer_;

    pybind11::scoped_interpreter                 guard_;
    pybind11::gil_scoped_release                 release_;
    std::thread                                  py_thread_;
    std::unordered_map<std::string, std::string> py_args_;
    std::string                                  py_exe_;

    // NVDEC decoder — constructed lazily on the first decode callback
    // invocation, so its CUDA context is bound to that thread.
    std::unique_ptr<nvdec_decoder> decoder_;
    std::mutex                     decoder_init_mutex_;
    bool                           decoder_initialized_ = false;

    // Decoded RGB frame cache — written by decode callback, read by get().
    decode::decoded_frame_cache decoded_frames_;
};

} // namespace ILLIXR
