#pragma once

#include "illixr/data_format/draco.hpp"
#include "illixr/data_format/mesh.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "ordered_scene_updates.hpp"
#include "spatial_hash.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>

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
    void process_ready_scenes();

    const std::shared_ptr<switchboard> switchboard_;
    const unsigned                     frame_count_;
    const unsigned                     fps_;
    const unsigned                     thread_count_;

    ordered_scene_updates<switchboard::ptr<const data_format::draco_type>, switchboard::ptr<const data_format::vb_type>>
               pending_;
    std::mutex scene_mutex_;

    spatial_hash grid_;

    const std::string data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     mesh_management_latency_;
};

} // namespace ILLIXR
