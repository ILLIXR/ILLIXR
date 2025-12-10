#include "plugin.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#if __has_include("quest_pose.pb.h")
    #include "quest_pose.pb.h"
#else
    #include "../proto/quest_pose_stub.hpp"
#endif

#ifdef _WIN32
    //#include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif
#include <cstring>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] quest_head_pose_rx::quest_head_pose_rx(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , pose_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("tx_xr_pose")}
    , pose_writer_{switchboard_->get_local_network_writer<switchboard::event_wrapper<std::string>>(
          "pose_to_unity",
          network::topic_config{network::topic_config::priority_type::MEDIUM,
                                false,
                                false,
                                network::topic_config::packetization_type::DEFAULT,
                                {},
                                network::topic_config::SerializationMethod::PROTOBUF})} { }

void quest_head_pose_rx::_p_one_iteration() {
    if (pose_reader_.size() > 0) {
        // spdlog::get("illixr")->debug("Have pose");
        auto                   buffer_ptr   = pose_reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter_);
        // spdlog::get("illixr")->debug("Pose size: {}", end_position);
        unity_pose_proto::Pose upo;
        if (upo.ParseFromString(buffer_str.substr(0, end_position))) {

            std::string pose_buf = upo.SerializeAsString();

            pose_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(pose_buf));
        } else {
            spdlog::get("illixr")->debug("Failed to parse pose");
        }
    }
}

PLUGIN_MAIN(quest_head_pose_rx)
