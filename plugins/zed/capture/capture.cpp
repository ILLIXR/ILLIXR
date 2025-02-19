#include "capture.hpp"

#include "files.hpp"
#include "illixr/data_format/unit.hpp"
#include "illixr/imgui/backends/imgui_impl_glfw.h"
#include "illixr/imgui/backends/imgui_impl_opengl3.h"
#include "illixr/shader_util.hpp"
#include "zed_opencv.hpp"

using namespace ILLIXR::zed_capture;

static void glfw_error_callback(int error, const char* description) {
    spdlog::get("illixr")->error("|| glfw error_callback: {}\n|> {}", error, description);
    ILLIXR::abort();
}

void capture::get_camera(const data_format::pose_data& wcf) {
    sl::InitParameters params;
    params.camera_resolution      = sl::RESOLUTION::HD720;
    params.coordinate_units       = sl::UNIT::MILLIMETER;
    params.coordinate_system      = sl::COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD;
    params.camera_fps             = fps_;
    params.depth_mode             = sl::DEPTH_MODE::PERFORMANCE;
    params.depth_stabilization    = true;
    params.depth_minimum_distance = 100.;

    wcs_xform_.setTranslation(sl::Translation(wcf.position.x(), wcf.position.y(), wcf.position.z()));
    wcs_xform_.setOrientation(
        sl::Orientation({wcf.orientation.x(), wcf.orientation.y(), wcf.orientation.z(), wcf.orientation.w()}));
    sl::PositionalTrackingParameters t_params(wcs_xform_);

    if (camera_->open(params) != sl::ERROR_CODE::SUCCESS)
        throw std::runtime_error("Open failed");

    if (camera_->enablePositionalTracking(t_params) != sl::ERROR_CODE::SUCCESS)
        throw std::runtime_error("tracking failed");
    camera_->setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, -1);

    camera_->updateSelfCalibration();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    camera_->grab(runtime_params_);
}

void capture::get_config() {
    auto                     cam_conf  = camera_->getCameraInformation().camera_configuration;
    sl::CameraParameters     left_cam  = cam_conf.calibration_parameters.left_cam;
    sl::CameraParameters     right_cam = cam_conf.calibration_parameters.right_cam;
    data_format::camera_data cc{
        cam_conf.resolution.width,
        cam_conf.resolution.height,
        cam_conf.fps,
        cam_conf.calibration_parameters.getCameraBaseline(),
        data_format::units::MILLIMETER,
        {{data_format::units::eyes::LEFT_EYE,
          {left_cam.cx, left_cam.cy, left_cam.v_fov * (M_PI / 180.), left_cam.h_fov * (M_PI / 180.)}},
         {data_format::units::eyes::RIGHT_EYE,
          {right_cam.cx, right_cam.cy, right_cam.v_fov * (M_PI / 180.), right_cam.h_fov * (M_PI / 180.)}}}};
    img_size_ = cam_conf.resolution;
    std::ofstream cam_of;
    cam_of.open(files::cam_file_, std::ofstream::out);
    cam_of << "#width,height,fps_,baseline,Lcenter_x,Lcenter_y,Lvfov,Lhfox,Rcenter_x,Rcenter_y,Rvfov,Lhfov" << std::endl;
    cam_of << cc << std::endl;
    cam_of.close();
}

void capture::make_gui() {
    glfwMakeContextCurrent(viewport_);
    glfwPollEvents();
    int d_width, d_height;
    glfwGetFramebufferSize(viewport_, &d_width, &d_height);
    glViewport(0, 0, d_width, d_height);
    glClearColor(clear_color_.x * clear_color_.w, clear_color_.y * clear_color_.w, clear_color_.z * clear_color_.w,
                 clear_color_.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();
    raw_img_[0] = imageL_ocv_.clone();
    raw_img_[1] = imageR_ocv_.clone();
    for (int i = 0; i < 2; i++) {
        cv::cvtColor(raw_img_[i], raw_img_[i], cv::COLOR_BGRA2RGB);
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, raw_img_[i].cols, raw_img_[i].rows, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     raw_img_[i].ptr());
    }

    if (ImGui::Begin("images")) {
        if (ImGui::BeginTable("img_table", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Image((void*) (intptr_t) textures_[0], ImVec2((float) img_size_.width, (float) img_size_.height));
            ImGui::TableSetColumnIndex(1);
            ImGui::Image((void*) (intptr_t) textures_[1], ImVec2((float) img_size_.width, (float) img_size_.height));
            ImGui::EndTable();
        }
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(viewport_);
}

int capture::get_data() {
    if (camera_->grab(runtime_params_) == sl::ERROR_CODE::SUCCESS) {
        sl::Pose                      pose;
        sl::POSITIONAL_TRACKING_STATE state = camera_->getPosition(pose, sl::REFERENCE_FRAME::WORLD);
        if (state == sl::POSITIONAL_TRACKING_STATE::OK) {
            timepoint_ = pose.timestamp.getNanoseconds();
            data_format::pose_type poseData(
                time_point(clock_duration_{timepoint_}),
                {pose.getTranslation().tx, pose.getTranslation().ty, pose.getTranslation().tz},
                {pose.getOrientation().w, pose.getOrientation().x, pose.getOrientation().y, pose.getOrientation().z});
            data_of_ << poseData << std::endl;

            camera_->retrieveImage(imageL_zed_, sl::VIEW::LEFT, sl::MEM::CPU, img_size_);
            camera_->retrieveImage(imageR_zed_, sl::VIEW::RIGHT, sl::MEM::CPU, img_size_);
            // camera_->retrieveMeasure(depth_zed, sl::MEASURE::DEPTH, sl::MEM::CPU, img_size_);
            //  camera_->retrieveMeasure(conf_zed, sl::MEASURE::CONFIDENCE);

            std::string c_imgL = files::camL_path_.string() + "/" + std::to_string(timepoint_) + ".png";
            camL_of_ << timepoint_ << "," << timepoint_ << ".png" << std::endl;
            cv::imwrite(c_imgL, imageL_ocv_);
            std::string c_imgR = files::camR_path_.string() + "/" + std::to_string(timepoint_) + ".png";
            camR_of_ << timepoint_ << "," << timepoint_ << ".png" << std::endl;
            cv::imwrite(c_imgR, imageR_ocv_);
            // std::string c_depth = files::depth_path.string() + "/" + std::to_string(timepoint_) + ".png";
            // depth_of << timepoint_ << "," << timepoint_ << ".png" << std::endl;
            // writeFloatImage(c_depth, depth_ocv);
            // std::string c_conf = files::conf_path.string() + "/" + std::to_string(timepoint_) + ".png";
            // conf_of << timepoint_ << "," << timepoint_ << ".png" << std::endl;
            // writeFloatImage(c_conf, conf_ocv);

            make_gui();
            return 1;
        } else {
            std::cout << "Dropping frame" << std::endl;
        }
    }
    return 0;
}

capture::~capture() {
    camera_->close();
    delete camera_;
    data_of_.close();
    camL_of_.close();
    camR_of_.close();
    // depth_of.close();
    // conf_of.close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(viewport_);

    glfwTerminate();
}

capture::capture(const int fp, const data_format::pose_data& wcf)
    : timepoint_{0}
    , fps_{fp} {
    // runtime_params_ = sl::RuntimeParameters(true,   // enable depth
    //                                         false,  //enable fill mode
    //                                         95,     // confidence limit cutoff
    //                                         100,    // texture confidence cutoff
    //                                         sl::REFERENCE_FRAME::WORLD,  // reference frame
    //                                         true);  // remove saturated
    data_of_.open(files::data_file_, std::ofstream::out);
    data_of_ << "#timestamp[ns],tx,ty,tx,w,x,y,z" << std::endl;
    camL_of_.open(files::camL_file_, std::ofstream ::out);
    camL_of_ << "#timestamp[ns],filename" << std::endl;
    camR_of_.open(files::camR_file_, std::ofstream ::out);
    camR_of_ << "#timestamp[ns],filename" << std::endl;
    // depth_of.open(files::depth_file, std::ofstream ::out);
    // depth_of << "#timestamp[ns],filename" << std::endl;
    // conf_of.open(files::conf_file, std::ofstream ::out);
    // conf_of << "#timestamp[ns],filename" << std::endl;
    camera_ = new sl::Camera();
    get_camera(wcf);

    get_config();

    imageL_zed_.alloc(img_size_.width, img_size_.height, sl::MAT_TYPE::U8_C4, sl::MEM::CPU);
    imageR_zed_.alloc(img_size_.width, img_size_.height, sl::MAT_TYPE::U8_C4, sl::MEM::CPU);
    // depth_zed.alloc(img_size_.width, img_size_.height, sl::MAT_TYPE::F32_C1, sl::MEM::CPU);
    // conf_zed.alloc(img_size_.width, img_size_.height, sl::MAT_TYPE::F32_C1, sl::MEM::CPU);

    imageL_ocv_ = slMat_to_cvMat(imageL_zed_);
    imageR_ocv_ = slMat_to_cvMat(imageR_zed_);
    // depth_ocv = slMat2cvMat(depth_zed);
    // conf_ocv = slMat2cvMat(conf_zed);

    if (!glfwInit()) {
        throw std::runtime_error("gl init failed");
    }

    glfwSetErrorCallback(glfw_error_callback);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GL_TRUE);

    constexpr std::string_view glsl_version{"#version 330 core"};
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    viewport_ = glfwCreateWindow(img_size_.width * 2 + 20, img_size_.height + 20, "ILLIXR Debug View", nullptr, nullptr);

    glfwSetWindowSize(viewport_, img_size_.width * 2 + 20, img_size_.height + 20);

    glfwMakeContextCurrent(viewport_);

    glfwSwapInterval(1);

    const GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        glfwDestroyWindow(viewport_);
        throw std::runtime_error("[debugview] Failed to initialize GLEW");
    }

    // Initialize IMGUI context.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Dark theme, of course.
    ImGui::StyleColorsDark();

    // Init IMGUI for OpenGL
    ImGui_ImplGlfw_InitForOpenGL(viewport_, true);
    ImGui_ImplOpenGL3_Init(glsl_version.data());

    glGenTextures(2, &(textures_[0]));
    for (unsigned int texture : textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glfwMakeContextCurrent(nullptr);
}
