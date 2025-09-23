#include "plugin.hpp"

#include <arpa/inet.h> // htonl
#include <chrono>      // std::chrono::...
#include <cstring>     // std::memcpy
#include <spdlog/spdlog.h>
using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] server_tx::server_tx(const std::string& name_, phonebook* pb_)
    : plugin{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , ada_writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
          "ada_server_data",
          network::topic_config{.latency              = std::chrono::milliseconds(0),
                                .serialization_method = network::topic_config::SerializationMethod::PROTOBUF})} {
    if (!std::filesystem::exists(data_path)) {
        if (!std::filesystem::create_directory(data_path)) {
            spdlog::get("illixr")->error("Failed to create data directory.");
        }
    }

    sender_time.open(data_path + "/server_package_mesh.csv");
    sender_timestamp.open(data_path + "/server_send_mesh_timestamp.csv");
    chunk_count = 0;
}

void server_tx::start() {
    plugin::start();
    switchboard_->schedule<vb_type>(id_, "unique_VB_list", [this](switchboard::ptr<const vb_type> datum, std::size_t) {
        this->send_vb_list(datum);
    });

    switchboard_->schedule<mesh_type>(id_, "compressed_scene", [this](switchboard::ptr<const mesh_type> datum, std::size_t) {
        this->send_sr_output(datum);
    });
}

void server_tx::send_vb_list(switchboard::ptr<const vb_type> datum) {
    auto start                 = std::chrono::high_resolution_clock::now();
    server_outgoing_vb_payload = new sr_output_proto::CompressMeshData();

    // 3 indicate vb_lists
    server_outgoing_vb_payload->set_active(3);
    server_outgoing_vb_payload->set_request_id(datum->scene_id);
    for (const auto& each_vb : datum->unique_VB_lists) {
        sr_output_proto::VB* new_vb = server_outgoing_vb_payload->add_vbs();
        new_vb->set_x(static_cast<int32_t>(std::get<0>(each_vb)));
        new_vb->set_y(static_cast<int32_t>(std::get<1>(each_vb)));
        new_vb->set_z(static_cast<int32_t>(std::get<2>(each_vb)));
    }

    const size_t payload_size = server_outgoing_vb_payload->ByteSizeLong();

    std::string buffer = std::to_string(htonl(static_cast<uint32_t>(payload_size)));

    buffer += server_outgoing_vb_payload->SerializeAsString() + delimiter;
    ada_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(buffer));

    delete server_outgoing_vb_payload;

    auto end         = std::chrono::high_resolution_clock::now();
    auto duration    = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    auto duration_ms = static_cast<double>(duration) / 1000.0;
    sender_time << "VB " << datum->scene_id << " " << duration_ms << " " << payload_size << "\n";
    sender_time.flush();
}

void server_tx::send_sr_output(switchboard::ptr<const mesh_type> datum) {
    auto start = std::chrono::high_resolution_clock::now();

    server_outgoing_payload = new sr_output_proto::CompressMeshData();

    auto* draco_owned = new std::string(reinterpret_cast<const char*>(datum->mesh.data()), datum->mesh.size());
    server_outgoing_payload->set_allocated_draco_data(draco_owned);

    server_outgoing_payload->set_active(2);
    server_outgoing_payload->set_request_id(datum->id);
    server_outgoing_payload->set_chunk_id(datum->chunk_id);
    server_outgoing_payload->set_max_chunk(datum->max_chunk);
    chunk_count++;

    // Prepare data delivery
    const size_t payload_size = server_outgoing_payload->ByteSizeLong();
    uint32_t     len_net      = htonl(static_cast<uint32_t>(payload_size));

    std::string buffer;
    buffer.resize(4 + payload_size);
    std::memcpy(buffer.data(), &len_net, 4);
    server_outgoing_payload->SerializeToArray(buffer.data() + 4, static_cast<int>(payload_size));

    ada_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(buffer + delimiter));

    auto end         = std::chrono::high_resolution_clock::now();
    auto duration    = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    auto duration_ms = static_cast<double>(duration) / 1000.0;
    sender_time << datum->id << " " << datum->chunk_id << " " << payload_size << " " << duration_ms << "\n";

    if (chunk_count == datum->max_chunk) {
        chunk_count = 0;
    }
    if (chunk_count == 1) {
        auto since_epoch = end.time_since_epoch();
        auto millis      = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
        sender_timestamp << datum->id << " " << millis << "\n";
        sender_timestamp.flush();
    }
    sender_time.flush();
    delete server_outgoing_payload;
}

PLUGIN_MAIN(server_tx)
