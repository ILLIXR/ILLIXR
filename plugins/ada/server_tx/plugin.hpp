#pragma once

#include "illixr/data_format/mesh.hpp"
#include "illixr/data_format/scene_reconstruction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"

#if __has_include("sr_output.pb.h")
    #include "sr_output.pb.h"
#else
    #include "../proto/output_stub.hpp"
#endif

#include <filesystem>
#include <fstream>

using namespace ILLIXR;

const std::string delimiter = "EEND!";

class server_tx : public plugin {
public:
    [[maybe_unused]] server_tx(const std::string& name_, phonebook* pb_);

    void start() override;

    void send_vb_list(switchboard::ptr<const data_format::vb_type> datum);

    void send_sr_output(switchboard::ptr<const data_format::mesh_type> datum);

private:
    const std::shared_ptr<switchboard>                                   switchboard_;
    sr_output_proto::CompressMeshData*                                   server_outgoing_payload;
    sr_output_proto::CompressMeshData*                                   server_outgoing_vb_payload;
    switchboard::network_writer<switchboard::event_wrapper<std::string>> ada_writer_;

    const std::string data_path = std::filesystem::current_path().string() + "/recorded_data";
    std::ofstream     sender_time;
    std::ofstream     sender_timestamp;

    unsigned chunk_count;
};
