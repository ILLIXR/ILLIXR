#pragma once

#include "illixr/data_format/mesh.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#if __has_include("sr_output.pb.h")
    #include "sr_output.pb.h"
#else
    #include "../proto/output_stub.hpp"
#endif
#include <filesystem>
#include <fstream>

namespace ILLIXR {

class device_rx : public threadloop {
public:
    [[maybe_unused]] device_rx(const std::string& name_, phonebook* pb_);

    skip_option _p_should_skip() override;

    void _p_one_iteration() override;

private:
    // pyh: handleer for single-chunk messages
    void receive_sr_output(const sr_output_proto::CompressMeshData& sr_output);

    const std::shared_ptr<switchboard>                                    switchboard_;
    const std::shared_ptr<relative_clock>                                 clock_;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> sr_reader_;
    switchboard::writer<data_format::mesh_type>                           mesh_;

    switchboard::writer<data_format::vb_type> vb_;

    int      last_id_ = -1;
    unsigned chunck_number_;

    std::string buffer_str_;

    const std::string data_path_ = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     receiving_latency_;
    std::ofstream     vb_timestamp_;
    std::ofstream     device_unpackage_time_;
    const std::string delimiter_ = "EEND!";
};

} // namespace ILLIXR
