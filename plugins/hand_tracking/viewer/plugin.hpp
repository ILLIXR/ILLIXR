#pragma once
#include <fstream>
// clang-format off
//#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
//#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/plugin.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/hand_tracking_data.hpp"
#include "imgui/imgui.h"

#include <eigen3/Eigen/Core>

namespace ILLIXR {
class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_);
    void start() override;
    ~viewer() override;
    void make_gui(const switchboard::ptr<const HandTracking::ht_frame>& frame);

private:
    std::ofstream ht_out;
    std::shared_ptr<RelativeClock>     _clock;
    const std::shared_ptr<switchboard> _switchboard;
    std::shared_ptr<HandTracking::ht_frame>          _ht_frame;
    bool _zed = false;  // zed images have to be turned into RGB
    bool _wc = false;   // webcam images need to be flipped
    const HandTracking::ht_frame *current_frame;
};

}
