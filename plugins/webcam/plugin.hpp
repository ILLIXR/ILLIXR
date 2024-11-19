#pragma once

#include "illixr/opencv_data_types.hpp"
#include "illixr/threadloop.hpp"
#include "include/data_format.hpp"

#include <opencv4/opencv2/videoio.hpp>

namespace ILLIXR {
class webcam : public threadloop {
public:
    [[maybe_unused]] webcam(const std::string& name_, phonebook* pb_);
    void _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard> _switchboard;
    switchboard::writer<binocular_cam_type>    _frame_pub;
    new_map left_data;
    new_map right_data;
    bool written = false;
};

}