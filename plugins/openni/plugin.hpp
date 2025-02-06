#pragma once

#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <openni2/OpenNI.h>

namespace ILLIXR {
class openni_plugin : public threadloop {
public:
    [[maybe_unused]] openni_plugin(const std::string& name, phonebook* pb);

    ~openni_plugin() override;

protected:
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;
    bool        camera_initialize();

private:
    // ILLIXR
    const std::shared_ptr<switchboard>          switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    switchboard::writer<rgb_depth_type>         rgb_depth_;

    // OpenNI
    openni::Status        device_status_ = openni::STATUS_OK;
    openni::Device        device_;
    openni::VideoStream   depth_, color_;
    openni::VideoFrameRef depth_frame_, color_frame_;

    // timestamp
    uint64_t   cam_time_{};
    uint64_t   last_timestamp_ = 0;
    uint64_t   first_time_{};
    time_point first_real_time_{};
    uint64_t   time_sleep_{};
};
} // namespace ILLIXR