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

/**
 * Bridge between ILLIXR's typed switchboard and the existing Boba Python process.
 *
 * The plugin keeps Boba out of the ILLIXR address space. It forwards coherent
 * OpenXR view/controller snapshots over a local datagram socket and publishes
 * Boba's mmap-backed stereo, overlay, and modal rings as lightweight
 * `stereo_frame` descriptors. A downstream display or streaming plugin owns the
 * actual GPU upload and presentation.
 */
class boba_immersive final : public plugin {
public:
    /** Resolve the Boba launcher and bind the switchboard endpoints. */
    boba_immersive(const std::string& name, phonebook* pb);
    /** Ensure worker and child-process resources are stopped before destruction. */
    ~boba_immersive() override;

    /** Start the process/IPC bridge on its dedicated worker thread. */
    void start() override;

    /** Stop the worker, terminate Boba's process group, and remove per-run IPC files. */
    void stop() override;

private:
    using controller_input = data_format::quest_controller_input;
    using view_frame       = data_format::openxr_view_frame;
    using stereo_frame     = data_format::stereo_frame;

    /** Read-only RAII mapping for one producer-owned Boba ring file. */
    struct mapped_file {
        int                 fd{-1};
        const std::uint8_t* data{nullptr};
        std::size_t         size{0};

        mapped_file()                              = default;
        mapped_file(const mapped_file&)            = delete;
        mapped_file& operator=(const mapped_file&) = delete;
        ~mapped_file();

        /** Unmap the current region and close its file descriptor. */
        void reset();
    };

    /** Own the complete child-process and IPC lifecycle until either side stops. */
    void run();

    /** Allocate a unique directory so concurrent or stale runs cannot share IPC state. */
    bool create_runtime_directory();

    /** Create the non-blocking Unix datagram socket used to send input to Boba. */
    bool create_input_socket();

    /** Spawn Boba in a new process group and pass it the four IPC paths. */
    bool launch_boba();

    /** Gracefully stop the complete Boba process group, escalating after a timeout. */
    void terminate_boba();

    /** Close and unlink all per-run socket and shared-memory paths. */
    void cleanup_runtime_directory();

    /** Forward the newest sequence-matched controller/view snapshot exactly once. */
    bool send_latest_input();

    /** Lazily map a ring after Boba has created and sized it. */
    bool map_if_ready(const std::string& path, mapped_file* mapping);

    /** Discover the required frame ring and optional overlay/modal rings. */
    bool map_output_files();

    /** Validate the latest ring slot and publish a zero-copy stereo descriptor. */
    bool publish_latest_frame();

    /** Ask a networked native Quest client to exit when Boba terminates. */
    void notify_native_client_shutdown();

    // ILLIXR services and typed endpoints used by the bridge.
    const std::shared_ptr<switchboard>                                                  switchboard_;
    const std::shared_ptr<const relative_clock>                                         clock_;
    const std::shared_ptr<stoplight>                                                    stoplight_;
    switchboard::reader<controller_input>                                               controller_reader_;
    switchboard::reader<view_frame>                                                     view_reader_;
    switchboard::writer<stereo_frame>                                                   stereo_writer_;
    std::optional<switchboard::network_writer<switchboard::event_wrapper<std::string>>> native_client_control_writer_;

    // Per-run launcher and IPC paths shared with the child process.
    std::string boba_launcher_;
    std::string runtime_directory_;
    std::string input_socket_path_;
    std::string frame_path_;
    std::string overlay_path_;
    std::string modal_path_;

    // Native resources owned by the worker thread.
    int   input_socket_{-1};
    pid_t boba_pid_{-1};

    // Boba owns and writes these rings; ILLIXR maps them read-only.
    mapped_file frame_mapping_;
    mapped_file overlay_mapping_;
    mapped_file modal_mapping_;

    // Worker coordination and monotonic producer sequence tracking.
    std::atomic<bool> stop_requested_{false};
    std::thread       worker_;
    std::uint64_t     last_input_sequence_{0};
    std::uint64_t     last_frame_id_{0};
    bool              have_input_sequence_{false};
    bool              native_client_shutdown_sent_{false};
};

} // namespace ILLIXR
