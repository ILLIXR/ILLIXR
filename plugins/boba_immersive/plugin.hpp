#pragma once

#include "illixr/data_format/openxr_view_frame.hpp"
#include "illixr/data_format/quest_controller.hpp"
#include "illixr/data_format/stereo_frame.hpp"
#include "illixr/plugin.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>
#include <thread>

namespace ILLIXR {

class boba_immersive final : public plugin {
public:
    boba_immersive(const std::string& name, phonebook* pb);
    ~boba_immersive() override;

    void start() override;
    void stop() override;

private:
    using controller_input = data_format::quest_controller_input;
    using view_frame       = data_format::openxr_view_frame;
    using stereo_frame     = data_format::stereo_frame;

    struct mapped_file {
        int                 fd{-1};
        const std::uint8_t* data{nullptr};
        std::size_t         size{0};

        mapped_file()                              = default;
        mapped_file(const mapped_file&)            = delete;
        mapped_file& operator=(const mapped_file&) = delete;
        ~mapped_file();

        void reset();
    };

    void run();
    bool create_runtime_directory();
    bool create_input_socket();
    bool launch_boba();
    void terminate_boba();
    void cleanup_runtime_directory();

    bool send_latest_input();
    bool map_if_ready(const std::string& path, mapped_file* mapping);
    bool map_output_files();
    bool publish_latest_frame();
    void notify_native_client_shutdown();

    const std::shared_ptr<switchboard>                                                  switchboard_;
    const std::shared_ptr<const relative_clock>                                         clock_;
    const std::shared_ptr<stoplight>                                                    stoplight_;
    switchboard::reader<controller_input>                                               controller_reader_;
    switchboard::reader<view_frame>                                                     view_reader_;
    switchboard::writer<stereo_frame>                                                   stereo_writer_;
    std::optional<switchboard::network_writer<switchboard::event_wrapper<std::string>>> native_client_control_writer_;

    std::string boba_launcher_;
    std::string runtime_directory_;
    std::string input_socket_path_;
    std::string frame_path_;
    std::string overlay_path_;
    std::string modal_path_;

    int   input_socket_{-1};
    pid_t boba_pid_{-1};

    mapped_file frame_mapping_;
    mapped_file overlay_mapping_;
    mapped_file modal_mapping_;

    std::atomic<bool> stop_requested_{false};
    std::thread       worker_;
    std::uint64_t     last_input_sequence_{0};
    std::uint64_t     last_frame_id_{0};
    bool              have_input_sequence_{false};
    bool              native_client_shutdown_sent_{false};
};

} // namespace ILLIXR
