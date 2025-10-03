// This version only test for speed since we don't have Open3D working on Orin yet
// #include <open3d/Open3D.h>
#include "plugin.hpp"

#include <ctime>
#include <spdlog/spdlog.h>

// for printing out mesh
#define VERIFY

// new slim scene management
using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] scene_management::scene_management(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()} {
    //, input_inactive_mesh_{switchboard_->get_buffered_reader<draco_type>("decoded_inactive_scene")}
    //, input_vb_lists_{switchboard_->get_buffered_reader<vb_type>("VB_update_lists")} {
    switchboard_->schedule<draco_type>(id_, "decoded_inactive_scene",
                                       [&](switchboard::ptr<const draco_type> datum, std::size_t) {
                                           this->process_inactive_frame(datum);
                                       });
    switchboard_->schedule<vb_type>(id_, "VB_update_lists", [&](switchboard::ptr<const vb_type> datum, std::size_t) {
        this->process_vb_lists(datum);
    });

    current_id_      = -1;
    last_processed_  = -1;
    chunk_counter_   = 0;
    last_cleaned_    = -1;
    mesh_processing_ = false;
    clean_waiting_   = false;

    pending_chunks_.reserve(thread_count_ * 5);
    pending_clean_reqs_.reserve(thread_count_ * 5);
    mesh_management_latency_.open(data_path_ + "/mesh_management_latency.csv");

    const char* env_var_name   = "FRAME_COUNT";
    const char* env_value      = std::getenv(env_var_name);
    const char* env_var_name_1 = "FPS";
    const char* env_value_1    = std::getenv(env_var_name_1);
    const char* env_var_name_2 = "PARTIAL_MESH_COUNT";
    const char* env_value_2    = std::getenv(env_var_name_2);

    if (env_value != nullptr && env_value_1 != nullptr && env_value_2 != nullptr) {
        try {
            frame_count_  = std::stoul(env_value);
            fps_          = std::stoul(env_value_1);
            thread_count_ = std::stoul(env_value_2);
            std::cout << "SM: FRAME_COUNT is: " << frame_count_ << " FPS is " << fps_ << " CHUNK_COUNT is " << thread_count_
                      << std::endl;
        } catch (const std::invalid_argument& e) {
            std::cerr << "SM: Invalid argument: the environment variable is not a valid unsigned integer." << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "SM: Out of range: the value of the environment variable is too large for an unsigned integer."
                      << std::endl;
        }
    } else {
        std::cerr << "SM: Environment variable not found." << std::endl;
    }
    std::cout.flush();
}

void scene_management::process_vb_lists(switchboard::ptr<const vb_type>& datum) {
    // printf("================================Device Mesh Manager: Started Processing VB List for Scene
    // %u=========================\n", datum->scene_id);
    if (mesh_processing_) {
        // if the old frame is still processing we will wait
        pending_clean_reqs_.push_back(datum);
        clean_waiting_ = true;
    } else {
        auto start = std::chrono::high_resolution_clock::now();

        // stage 1 outdated region processing
        grid_.clean_mesh_vb_redesign_with_list(datum->unique_VB_lists);
        grid_.deleted_ranges_processing();
        last_cleaned_ = static_cast<int>(datum->scene_id);

        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "Clean " << datum->scene_id << " " << duration << "\n";
        printf("===Device Mesh Manager: Finished Processing VB List for Scene %u===\n", datum->scene_id);
    }
}

void scene_management::process_inactive_frame(switchboard::ptr<const draco_type>& datum) {
    // printf("================================Device Mesh Manager: Started Scene ID: %u=========================\n",
    // datum->frame_id); printf("receiving a new scene with id %d, last processed %d,  current_id %d, chunk_counter %u, last
    // cleaned %d, pending chunk size %zu\n", datum->frame_id, last_processed, current_id, chunk_counter, last_cleaned,
    // pending_chunks.size());
    if (static_cast<int>(datum->frame_id) > last_processed_ && static_cast<int>(datum->frame_id) > current_id_) {
        current_id_    = static_cast<int>(datum->frame_id);
        chunk_counter_ = 1;
    } else if (static_cast<int>(datum->frame_id) == current_id_) {
        chunk_counter_++;
    }
    if (chunk_counter_ <= thread_count_) {
        pending_chunks_.push_back(datum);
    }
    if (chunk_counter_ == thread_count_) {
        printf("===Device Mesh Manager: Processing Scene %u with %zu pending chunks===\n", datum->frame_id,
               pending_chunks_.size());
        mesh_processing_ = true;
        // pyh omitting the code for merging multiple clean requests (not used for the paper)
        auto const_start = std::chrono::high_resolution_clock::now();

        // pyh step1 restore unused nullified faces
        grid_.restore_deleted_faces();

        auto start = std::chrono::high_resolution_clock::now();
        for (uint i = 0; i < thread_count_; ++i) {
            // pyh this is Partial VB-Aligned Vertex Merging (S4.4)
            grid_.append_mesh_allocate(pending_chunks_[i]->scene_update_mapping);
        }
        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "Merge " << datum->frame_id << " " << duration << "\n";

        start = std::chrono::high_resolution_clock::now();

        unsigned current_gap = 0;
        // this is Live Mesh Integration & Mesh Nullification (Sec4.3 Stage 3 and Stage 4)
        current_gap = grid_.append_mesh_match_and_insert(false);

        end      = std::chrono::high_resolution_clock::now();
        duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        mesh_management_latency_ << "Map " << datum->frame_id << " " << duration << "\n";

        // At this point the Scene Mesh is up-to-date
        duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - const_start).count()) / 1000.0;

        size_t vertices_size_in_bytes = grid_.vertices_.size() * sizeof(Eigen::Vector3d);
        size_t faces_size_in_bytes    = grid_.faces_.size() * sizeof(int);
        size_t total_size_in_bytes    = vertices_size_in_bytes + faces_size_in_bytes;

        mesh_management_latency_ << "Display " << datum->frame_id << " " << duration << " " << vertices_size_in_bytes << " "
                                 << faces_size_in_bytes << " " << total_size_in_bytes << " " << current_gap << "\n";

        auto since_epoch = end.time_since_epoch();
        auto millis     = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
        // record timestamp on when mesh is available
        mesh_management_latency_ << "Ready " << datum->frame_id << " " << millis << "\n";

        // Note: This release does not ship a test application (Our Jetson Jetpack version has some Open3D dependency
        // issues) and for evaluation we used scene update time which does not need an App.
        //   To enable Scene Request Handling, Integrate by:
        //   1) Publishing a SceneRequest to switchboard from your app/plugin.
        //   2) Let Scene Management subscribes to listen for it and when ready give the update
        //       2.1 For cached scene, one can simply publish to the switchboard current grid.vertices and grid.faces
        //       2.2 For latest scene, need to create a send a request signal (like how depth encoding and mesh compression)
        //       to InfiniTAM and let it start GetMesh() immediately, then after the use the active_id to specify
        //		it as a latest scene request then pretty much following the same code logic and wait until Ready before
        // publish
        // TODO: minimal example later.

        start = std::chrono::high_resolution_clock::now();

        pending_chunks_.erase(pending_chunks_.begin(), pending_chunks_.begin() + thread_count_);

        end      = std::chrono::high_resolution_clock::now();
        duration = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
        // PP: post-processing (remove partial mesh chunks) very minimal but recorded if people want to test it out
        mesh_management_latency_ << "PP " << datum->frame_id << " " << duration << "\n";

#if defined VERIFY
        if (datum->frame_id == ((frame_count_ / fps_) - 1)) {
            grid_.print_mesh_as_obj(datum->frame_id, 1, "");
        }
#endif
        // if a clean request received while the previous frame is processing need to clean it
        if (clean_waiting_) {
            // printf("before finish processing frame %u, we already received cleaning request\n", datum->frame_id);
            start           = std::chrono::high_resolution_clock::now();
            auto pending_request = pending_clean_reqs_.back();
            grid_.clean_mesh_vb_redesign_with_list(pending_request->unique_VB_lists);
            grid_.deleted_ranges_processing();
            last_cleaned_  = static_cast<int>(pending_request->scene_id);
            clean_waiting_ = false;
            end       = std::chrono::high_resolution_clock::now();
            duration  = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
            mesh_management_latency_ << "Clean " << pending_request->scene_id << " " << duration << "\n";
            printf("===Device Mesh Manager: Finished Processing VB List for Scene %u===\n", pending_request->scene_id);
            pending_clean_reqs_.pop_back();
        }

        last_processed_ = static_cast<int>(datum->frame_id);

        printf("===Device Mesh Manager: Finished Scene %u===\n", datum->frame_id);
        mesh_processing_ = false;

        std::cout.flush();
        if (datum->frame_id == (frame_count_ / fps_) - 1) {
            printf("Scene Management processed all frames, shutting down...\n");
            mesh_management_latency_.flush();
        }
    }
}

threadloop::skip_option scene_management::_p_should_skip() {
    return skip_option::run;
}

PLUGIN_MAIN(scene_management)
