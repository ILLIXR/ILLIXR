#pragma once
// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on
#include "illixr/data_format/pose.hpp"
#include "illixr/imgui/imgui.h"

#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <sl/Camera.hpp>

namespace ILLIXR::zed_capture {
class capture {
public:
    capture() = delete;
    capture(int fp, const ILLIXR::data_format::pose_data& wcf);
    void get_camera(const ILLIXR::data_format::pose_data& wcf);
    void get_config();
    int  get_data();
    void make_gui();
    ~capture();

private:
    std::ofstream data_of;
    std::ofstream camL_of;
    std::ofstream camR_of;
    // std::ofstream depth_of;
    // std::ofstream conf_of;

    sl::Camera* camera = nullptr;

    sl::Mat imageL_zed;
    sl::Mat imageR_zed;
    // sl::Mat depth_zed;
    // sl::Mat conf_zed;

    cv::Mat imageL_ocv;
    cv::Mat imageR_ocv;
    // cv::Mat  depth_ocv;
    // cv::Mat  conf_ocv;
    uint64_t timepoint;

    sl::Resolution img_size;

    sl::RuntimeParameters runtime_params_;
    sl::Transform         wcs_xform;

    GLFWwindow* _viewport{};
    GLuint      textures[2];
    ImVec4      clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    cv::Mat     raw_img[2];
    const int   fps;
};

} // namespace ILLIXR::zed_capture
