#pragma once

#include "illixr/data_format/draco.hpp"
#include "illixr/data_format/mesh.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "spatial_hash.hpp"

#include <filesystem>
#include <fstream>

namespace ILLIXR {
class scene_management : public threadloop {
public:
    [[maybe_unused]] scene_management(const std::string& name_, phonebook* pb_);

    void process_vb_lists(switchboard::ptr<const data_format::vb_type>& datum);

    void process_inactive_frame(switchboard::ptr<const data_format::draco_type>& datum);

protected:
    skip_option _p_should_skip() override;

    void _p_one_iteration() override { }

private:
    // ILLIXR related variables
    const std::shared_ptr<switchboard> switchboard_;
    // switchboard::buffered_reader<data_format::draco_type> input_inactive_mesh_;
    // switchboard::buffered_reader<data_format::vb_type>    input_vb_lists_;

    std::vector<switchboard::ptr<const data_format::draco_type>> pending_chunks_;
    std::vector<switchboard::ptr<const data_format::vb_type>>    pending_clean_reqs_;

    spatial_hash grid_;

    int      current_id_;
    int      last_processed_;
    unsigned chunk_counter_;
    bool     mesh_processing_;
    bool     clean_waiting_;
    int      last_cleaned_;
    unsigned frame_count_;
    unsigned fps_;
    unsigned thread_count_;

    const std::string data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     mesh_management_latency_;
};

} // namespace ILLIXR
