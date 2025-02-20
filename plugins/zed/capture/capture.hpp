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
    ~capture();
    void get_camera(const ILLIXR::data_format::pose_data& wcf);
    void get_config();
    int  get_data();
    void make_gui();

private:
    std::ofstream data_of_;
    std::ofstream camL_of_;
    std::ofstream camR_of_;
    // std::ofstream depth_of_;
    // std::ofstream conf_of_;

    sl::Camera* camera_ = nullptr;

    sl::Mat imageL_zed_;
    sl::Mat imageR_zed_;
    // sl::Mat depth_zed_;
    // sl::Mat conf_zed_;

    cv::Mat imageL_ocv_;
    cv::Mat imageR_ocv_;
    // cv::Mat  depth_ocv_;
    // cv::Mat  conf_ocv_;
    uint64_t timepoint_;

    sl::Resolution img_size_;

    sl::RuntimeParameters runtime_params_;
    sl::Transform         wcs_xform_;

    GLFWwindow* viewport_{};
    GLuint      textures_[2];
    ImVec4      clear_color_ = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    cv::Mat     raw_img_[2];
    const int   fps_;
};

} // namespace ILLIXR::zed_capture
