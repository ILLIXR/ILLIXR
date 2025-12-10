#pragma once

#include "illixr/data_format/pose.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

namespace ILLIXR {

class MY_EXPORT_API quest_head_pose_rx : public threadloop {
public:
    [[maybe_unused]] quest_head_pose_rx(const std::string& name, phonebook* pb);

protected:
    void _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>    switchboard_;
    const std::shared_ptr<relative_clock> clock_;

    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> pose_reader_;
    switchboard::local_writer<switchboard::event_wrapper<std::string>>    pose_writer_;
    // float height = 1.5;
    // int count = 0;
    const std::string delimiter_ = "EEND!";
};
} // namespace ILLIXR
