#include "plugin.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#if __has_include("quest_pose.pb.h")
    #include "quest_pose.pb.h"
#else
    #include "../proto/quest_pose_stub.hpp"
#endif

#ifdef _WIN32
#include <winsock2.h>
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
          network::topic_config{.serialization_method = network::topic_config::SerializationMethod::PROTOBUF})} { }

void quest_head_pose_rx::_p_one_iteration() {
    //if (count > 10)
    //    return;
    //std::this_thread::sleep_for (std::chrono::seconds(1));
    //height += 0.02;
    if (pose_reader_.size() > 0) {
        spdlog::get("illixr")->debug("Have pose");
         auto                   buffer_ptr   = pose_reader_.dequeue();
         std::string            buffer_str   = **buffer_ptr;
         std::string::size_type end_position = buffer_str.find(delimiter_);

        std::string pose_buf = buffer_str.substr(0, end_position);
    //unity_pose_proto::Pose*     upo = new unity_pose_proto::Pose();
    //unity_pose_proto::Position* pos = new unity_pose_proto::Position();
    //pos->set_x(0.);
    //pos->set_y(height);
    //pos->set_z(0.);
    //upo->set_allocated_pos(pos);
    //unity_pose_proto::Quaternion* quat = new unity_pose_proto::Quaternion();
    //quat->set_w(1.0);
    //quat->set_x(0.);
    //quat->set_y(0.);
    //quat->set_z(0.);
    //upo->set_allocated_quat(quat);

    //std::string pose_buf = upo->SerializeAsString();

        pose_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(pose_buf));
        spdlog::get("illixr")->debug("sent pose");
        //spdlog::get("illixr")->debug("Sent Pose: {},{},{}: {},{},{},{}", upo->pos().x(),upo->pos().y(),upo->pos().z(),
        //                             upo->quat().w(),upo->quat().x(),upo->quat().y(),upo->quat().z());
    //delete upo;
    }
}

PLUGIN_MAIN(quest_head_pose_rx)
