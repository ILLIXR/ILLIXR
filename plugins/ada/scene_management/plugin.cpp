// This version measures scene updates without an Open3D renderer.
#include "plugin.hpp"

#include "illixr/data_format/formatted_mesh.hpp"

#include <spdlog/spdlog.h>
#include <stdexcept>

#define VERIFY

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] scene_management::scene_management(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , frame_count_{static_cast<unsigned>(switchboard_->get_env_ulong("FRAME_COUNT", 1158))}
    , fps_{static_cast<unsigned>(switchboard_->get_env_ulong("FPS", 15))}
    , thread_count_{static_cast<unsigned>(switchboard_->get_env_ulong("PARTIAL_MESH_COUNT", 8))}
    , pending_{thread_count_} {
    if (fps_ == 0) {
        throw std::invalid_argument("FPS extraction interval must be positive");
    }
    std::filesystem::create_directories(data_path_);
    mesh_management_latency_.open(data_path_ + "/mesh_management_latency.csv");
    std::cout << "SM: FRAME_COUNT is: " << frame_count_ << " FPS is " << fps_ << " CHUNK_COUNT is " << thread_count_
              << std::endl;

    // Register callbacks only after all shared state is initialized.
    switchboard_->schedule<draco_type>(id_, "decoded_inactive_scene",
                                       [&](switchboard::ptr<const draco_type> datum, std::size_t) {
                                           this->process_inactive_frame(datum);
                                       });
    switchboard_->schedule<vb_type>(id_, "VB_update_lists", [&](switchboard::ptr<const vb_type> datum, std::size_t) {
        this->process_vb_lists(datum);
    });
}

void scene_management::process_vb_lists(switchboard::ptr<const vb_type>& datum) {
    std::lock_guard<std::mutex> lock(scene_mutex_);
    if (!pending_.add_cleanup(datum->scene_id, datum)) {
        spdlog::get("illixr")->warn("Ignoring duplicate or stale VB update for scene {}", datum->scene_id);
        return;
    }
    process_ready_scenes();
}

void scene_management::process_inactive_frame(switchboard::ptr<const draco_type>& datum) {
    std::lock_guard<std::mutex> lock(scene_mutex_);
    if (!pending_.add_chunk(datum->frame_id, datum->chunk_id, datum)) {
        spdlog::get("illixr")->warn("Ignoring invalid, duplicate or stale chunk {} for scene {}", datum->chunk_id,
                                    datum->frame_id);
        return;
    }
    process_ready_scenes();
}

// Both subscriptions hold scene_mutex_ through cleanup and integration. Future
// VB updates must not erase geometry belonging to an unfinished earlier scene.
void scene_management::process_ready_scenes() {
    while (auto update = pending_.pop_ready()) {
        const auto scene_id    = update->scene_id;
        auto       clean_start = std::chrono::high_resolution_clock::now();
        grid_.clean_mesh_vb_redesign_with_list(update->cleanup->unique_VB_lists);
        grid_.deleted_ranges_processing();
        auto clean_end = std::chrono::high_resolution_clock::now();
        auto clean_ms =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(clean_end - clean_start).count()) /
            1000.0;
        mesh_management_latency_ << "Clean " << scene_id << " " << clean_ms << "\n";
        printf("===Device Mesh Manager: Finished Processing VB List for Scene %u===\n", scene_id);
        printf("===Device Mesh Manager: Processing Scene %u with %zu pending chunks===\n", scene_id, update->chunks.size());
        auto const_start = std::chrono::high_resolution_clock::now();

        auto start = std::chrono::high_resolution_clock::now();
        for (uint i = 0; i < thread_count_; ++i) {
            // pyh this is Partial VB-Aligned Vertex Merging (S4.4)
            // Alias the immutable map while retaining ownership of its event.
            const auto& chunk = update->chunks[i];
            if (auto formatted = std::dynamic_pointer_cast<const formatted_mesh_type>(chunk)) {
                grid_.append_mesh_allocate(
                    std::shared_ptr<const spatial_hash::SceneUpdateRanges>(formatted->data, &formatted->data->block_ranges));
            } else {
                grid_.append_mesh_allocate(
                    std::shared_ptr<const spatial_hash::SceneUpdateMap>(chunk, &chunk->scene_update_mapping));
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "Merge " << scene_id << " " << duration << "\n";

        start = std::chrono::high_resolution_clock::now();

        unsigned current_gap = 0;
        // this is Live Mesh Integration & Mesh Nullification (Sec4.3 Stage 3 and Stage 4)
        current_gap = grid_.append_mesh_match_and_insert(false);

        end      = std::chrono::high_resolution_clock::now();
        duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "Map " << scene_id << " " << duration << "\n";

        // At this point the Scene Mesh is up-to-date
        duration =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - const_start).count()) / 1000.0;

        size_t vertices_size_in_bytes = grid_.vertices_.size() * sizeof(data_format::scene_vertex);
        size_t faces_size_in_bytes    = grid_.faces_.size() * sizeof(int);
        size_t total_size_in_bytes    = vertices_size_in_bytes + faces_size_in_bytes;

        mesh_management_latency_ << "Display " << scene_id << " " << duration << " " << vertices_size_in_bytes << " "
                                 << faces_size_in_bytes << " " << total_size_in_bytes << " " << current_gap << "\n";

        auto since_epoch = end.time_since_epoch();
        auto millis      = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
        // record timestamp on when mesh is available
        mesh_management_latency_ << "Ready " << scene_id << " " << millis << "\n";

        start = std::chrono::high_resolution_clock::now();
        update->chunks.clear();
        end      = std::chrono::high_resolution_clock::now();
        duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "PP " << scene_id << " " << duration << "\n";

#if defined VERIFY
        if (frame_count_ >= fps_ && scene_id == (frame_count_ / fps_) - 1) {
            const auto output = switchboard_->get_env("ADA_FINAL_MESH_PATH");
            if (!output.empty()) {
                const auto parent = std::filesystem::path(output).parent_path();
                if (!parent.empty())
                    std::filesystem::create_directories(parent);
            }
            grid_.print_mesh_as_obj(scene_id, 1, output);
        }
#endif
        printf("===Device Mesh Manager: Finished Scene %u===\n", scene_id);
        std::cout.flush();
        mesh_management_latency_.flush();
    }
}

threadloop::skip_option scene_management::_p_should_skip() {
    return skip_option::run;
}

PLUGIN_MAIN(scene_management)
