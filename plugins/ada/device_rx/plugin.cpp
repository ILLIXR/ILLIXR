#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] device_rx::device_rx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , sr_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("ada_processed")}
    // pyh handling compressed partial meshes
    , mesh_{switchboard_->get_writer<mesh_type>("compressed_scene")}
    // comment out the appropriate amount based on the parallel compression you are using

    // pyh handling Unique Voxel Block List (UVBL)
    , vb_{switchboard_->get_writer<vb_type>("VB_update_lists")} {
    chunck_number_ = switchboard_->get_env_ulong("MESH_DECOMPRESS_PARALLELISM", 8);

    std::filesystem::create_directories(data_path_);

    receiving_latency_.open(data_path_ + "/receiving_timestamp.csv");
    vb_timestamp_.open(data_path_ + "/vb_timestamp_.csv");
    device_unpackage_time_.open(data_path_ + "/device_unpackage_time_.csv");

    // pyh: need to set number of partial chunks to be expected from the serverenv; default to 8 if not set/invalid
    // e.g. export PARTIAL_MESH_COUNT=8
    const char* env_value = std::getenv("PARTIAL_MESH_COUNT");

    if (env_value != nullptr) {
        try {
            chunck_number_ = std::stoul(env_value);
        } catch (...) {
            spdlog::get("illixr")->error("device_rx: invalid PARTIAL_MESH_COUNT; falling back to {}", chunck_number_);
        }
    } else {
        spdlog::get("illixr")->error("device_rx: PARTIAL_MESH_COUNT not set; using default {}", chunck_number_);
    }
    if (chunck_number_ == 0) {
        chunck_number_ = 1; // pyh: avoid modulo-by-zero
        spdlog::get("illixr")->error("device_rx: PARTIAL_MESH_COUNT=0 is invalid; forcing 1");
    }
}

threadloop::skip_option device_rx::_p_should_skip() {
    return skip_option::run;
}

void device_rx::_p_one_iteration() {
    if (sr_reader_.size() > 0) {
        spdlog::get("illixr")->debug("[device_rx] Received packet");
        auto                   buffer_ptr = sr_reader_.dequeue();
        std::string            buffer_str = **buffer_ptr;
        std::string::size_type end        = buffer_str.find(delimiter_);
        spdlog::get("illixr")->debug("   Buffer size: {},   end: {}", buffer_str.size(), end);
        spdlog::get("illixr")->debug("   Buffer: {}", buffer_str);
        sr_output_proto::CompressMeshData sr_output;
        if (sr_output.ParseFromString(buffer_str.substr(0, end))) {
            receive_sr_output(sr_output);
        } else {
            spdlog::get("illixr")->error("client_rx: Cannot parse SR output!");
        }
    }
}

void device_rx::receive_sr_output(const sr_output_proto::CompressMeshData& sr_output) {
    const auto t0             = std::chrono::high_resolution_clock::now();
    const auto ms_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(t0.time_since_epoch()).count();
    unsigned   active         = sr_output.active();
    if (active < 3) {
        const std::string& dataString = sr_output.draco_data();
        unsigned           scene_id   = sr_output.request_id();
        std::vector<char>  payload(dataString.begin(), dataString.end());

        mesh_.put(mesh_.allocate<mesh_type>(mesh_type{sr_output.chunk_id() % chunck_number_, payload, false, scene_id,
                                                      sr_output.chunk_id(), sr_output.max_chunk()}));

        auto t1          = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        // 5/16 printed out packet size
        device_unpackage_time_ << sr_output.request_id() << " " << sr_output.chunk_id() << " " << duration_ms << " "
                               << sr_output.ByteSizeLong() << "\n";
        device_unpackage_time_.flush();

        if (static_cast<int>(scene_id) > last_id_) {
            receiving_latency_ << scene_id << " " << ms_since_epoch << "\n";
            receiving_latency_.flush();
            last_id_ = static_cast<int>(scene_id);
        }
    } else {
        if (active != 3) {
            spdlog::get("illixr")->debug("received unknown package type {}", active);
        }
        // pyh handling the Unique Voxel Block List (UVBL)
        std::set<std::tuple<int, int, int>> vb_lists;
        for (const auto& vb : sr_output.vbs()) {
            vb_lists.emplace(vb.x(), vb.y(), vb.z());
        }
        vb_.put(vb_.allocate<vb_type>(vb_type{std::move(vb_lists), sr_output.request_id()}));
        spdlog::get("illixr")->debug("recontructed VB update of {}", sr_output.request_id());

        auto t1          = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        vb_timestamp_ << sr_output.request_id() << " " << ms_since_epoch << "\n";
        vb_timestamp_.flush();
        device_unpackage_time_ << "VB " << sr_output.request_id() << " " << duration_ms << " " << sr_output.ByteSizeLong()
                               << "\n";
        device_unpackage_time_.flush();
    }
}

PLUGIN_MAIN(device_rx)
