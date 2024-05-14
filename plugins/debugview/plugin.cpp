#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>
#include <iostream>
#include <opencv2/opencv.hpp>

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "illixr/error_util.hpp"
#include "illixr/gl_util/obj.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/math_util.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/shaders/demo_shader.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "third_party/imgui.h"
#include "third_party/imgui_impl_glfw.h"
#include "third_party/imgui_impl_opengl3.h"

using namespace ILLIXR;

constexpr size_t TEST_PATTERN_WIDTH  = 256;
constexpr size_t TEST_PATTERN_HEIGHT = 256;

// Loosely inspired by
// http://spointeau.blogspot.com/2013/12/hello-i-am-looking-at-opengl-3.html

Eigen::Matrix4f look_at(const Eigen::Vector3f& eye, const Eigen::Vector3f& target, const Eigen::Vector3f& up) {
    using namespace Eigen;
    Vector3f look_dir = (target - eye).normalized();
    Vector3f up_dir   = up.normalized();
    Vector3f side_dir = look_dir.cross(up_dir).normalized();
    up_dir            = side_dir.cross(look_dir);

    Matrix4f result;
    result << side_dir.x(), side_dir.y(), side_dir.z(), -side_dir.dot(eye), up_dir.x(), up_dir.y(), up_dir.z(), -up_dir.dot(eye),
        -look_dir.x(), -look_dir.y(), -look_dir.z(), look_dir.dot(eye), 0, 0, 0, 1;

    return result;
}

/**
 * @brief Callback function to handle glfw errors
 */
static void glfw_error_callback(int error, const char* description) {
    spdlog::get("illixr")->error("|| glfw error_callback: {}\n|> {}", error, description);
    ILLIXR::abort();
}

class debugview : public threadloop {
public:
    // Public constructor, Spindle passes the phonebook to this
    // constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the plugin can read
    // the data whenever it needs to.
    [[maybe_unused]] debugview(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , pose_prediction_{phonebook_->lookup_impl<pose_prediction>()}
        , slow_pose_reader_{switchboard_->get_reader<pose_type>("slow_pose")}
        , fast_pose_reader_{switchboard_->get_reader<imu_raw_type>("imu_raw")} //, glfw_context{pb->lookup_impl<global_config>()->glfw_context}
        , rgb_depth_reader_(switchboard_->get_reader<rgb_depth_type>("rgb_depth"))
        , cam_reader_{switchboard_->get_buffered_reader<cam_type>("cam")} {
        spdlogger(std::getenv("DEBUGVIEW_LOG_LEVEL"));
    }

    void draw_GUI() {
        RAC_ERRNO_MSG("debugview at start of draw_GUI");

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();

        // Calls glfw within source code which sets errno
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        // Init the window docked to the bottom left corner.
        ImGui::SetNextWindowPos(ImVec2(0.0f, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once, ImVec2(0.0f, 1.0f));
        ImGui::Begin("ILLIXR Debug View");

        ImGui::Text("Adjust options for the runtime debug view.");
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Headset visualization options", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Follow headset position", &follow_headset_);

            ImGui::SliderFloat("View distance ", &view_distance_, 0.1f, 10.0f);

            ImGui::SliderFloat3("Tracking \"offset\"", tracking_position_offset_.data(), -10.0f, 10.0f);

            if (ImGui::Button("Reset")) {
                tracking_position_offset_ = Eigen::Vector3f{5.0f, 2.0f, -3.0f};
            }
            ImGui::SameLine();
            ImGui::Text("Resets to default tracking universe");

            if (ImGui::Button("Zero")) {
                tracking_position_offset_ = Eigen::Vector3f{0.0f, 0.0f, 0.0f};
            }
            ImGui::SameLine();
            ImGui::Text("Resets to zeroed out tracking universe");

            if (ImGui::Button("Zero orientation")) {
                const pose_type predicted_pose = pose_prediction_->get_fast_pose().pose;
                if (pose_prediction_->fast_pose_reliable()) {
                    // Can only zero if predicted_pose is valid
                    pose_prediction_->set_offset(predicted_pose.orientation);
                }
            }
            ImGui::SameLine();
            ImGui::Text("Resets to zeroed out tracking universe");
        }
        ImGui::Spacing();
        ImGui::Text("Switchboard connection status:");
        ImGui::Text("Predicted pose topic:");
        ImGui::SameLine();

        if (pose_prediction_->fast_pose_reliable()) {
            const pose_type predicted_pose = pose_prediction_->get_fast_pose().pose;
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid predicted pose pointer");
            ImGui::Text("Predicted pose position (XYZ):\n  (%f, %f, %f)", predicted_pose.position.x(),
                        predicted_pose.position.y(), predicted_pose.position.z());
            ImGui::Text("Predicted pose quaternion (XYZW):\n  (%f, %f, %f, %f)", predicted_pose.orientation.x(),
                        predicted_pose.orientation.y(), predicted_pose.orientation.z(), predicted_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid predicted pose pointer");
        }

        ImGui::Text("Fast pose topic:");
        ImGui::SameLine();

        switchboard::ptr<const imu_raw_type> raw_imu = fast_pose_reader_.get_ro_nullable();
        if (raw_imu) {
            pose_type raw_pose;
            raw_pose.position      = Eigen::Vector3f{float(raw_imu->pos(0)), float(raw_imu->pos(1)), float(raw_imu->pos(2))};
            raw_pose.orientation   = Eigen::Quaternionf{float(raw_imu->quat.w()), float(raw_imu->quat.x()),
                                                      float(raw_imu->quat.y()), float(raw_imu->quat.z())};
            pose_type swapped_pose = pose_prediction_->correct_pose(raw_pose);

            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid fast pose pointer");
            ImGui::Text("Fast pose position (XYZ):\n  (%f, %f, %f)", swapped_pose.position.x(), swapped_pose.position.y(),
                        swapped_pose.position.z());
            ImGui::Text("Fast pose quaternion (XYZW):\n  (%f, %f, %f, %f)", swapped_pose.orientation.x(),
                        swapped_pose.orientation.y(), swapped_pose.orientation.z(), swapped_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid fast pose pointer");
        }

        ImGui::Text("Slow pose topic:");
        ImGui::SameLine();

        switchboard::ptr<const pose_type> slow_pose_ptr = slow_pose_reader_.get_ro_nullable();
        if (slow_pose_ptr) {
            pose_type swapped_pose = pose_prediction_->correct_pose(*slow_pose_ptr);
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid slow pose pointer");
            ImGui::Text("Slow pose position (XYZ):\n  (%f, %f, %f)", swapped_pose.position.x(), swapped_pose.position.y(),
                        swapped_pose.position.z());
            ImGui::Text("Slow pose quaternion (XYZW):\n  (%f, %f, %f, %f)", swapped_pose.orientation.x(),
                        swapped_pose.orientation.y(), swapped_pose.orientation.z(), swapped_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid slow pose pointer");
        }

        ImGui::Text("Ground truth pose topic:");
        ImGui::SameLine();

        if (pose_prediction_->true_pose_reliable()) {
            const pose_type true_pose = pose_prediction_->get_true_pose();
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid ground truth pose pointer");
            ImGui::Text("Ground truth position (XYZ):\n  (%f, %f, %f)", true_pose.position.x(), true_pose.position.y(),
                        true_pose.position.z());
            ImGui::Text("Ground truth quaternion (XYZW):\n  (%f, %f, %f, %f)", true_pose.orientation.x(),
                        true_pose.orientation.y(), true_pose.orientation.z(), true_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid ground truth pose pointer");
        }

        ImGui::Text("Debug view Eulers:");
        ImGui::Text("	(%f, %f)", view_euler_.x(), view_euler_.y());

        ImGui::End();

        ImGui::Begin("Camera + IMU");
        ImGui::Text("Camera view buffers: ");
        ImGui::Text("	Camera0: (%d, %d) \n		GL texture handle: %d", camera_texture_size_[0].x(),
                    camera_texture_size_[0].y(), camera_texture_[0]);
        ImGui::Text("	Camera1: (%d, %d) \n		GL texture handle: %d", camera_texture_size_[1].x(),
                    camera_texture_size_[1].y(), camera_texture_[1]);
        ImGui::End();

        if (use_cam_) {
            ImGui::SetNextWindowSize(ImVec2(700, 350), ImGuiCond_Once);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once,
                                    ImVec2(1.0f, 1.0f));
            ImGui::Begin("Onboard camera views");
            auto window_size     = ImGui::GetWindowSize();
            auto vertical_offset = ImGui::GetCursorPos().y;
            ImGui::Image((void*) (intptr_t) camera_texture_[0], ImVec2(window_size.x / 2, window_size.y - vertical_offset * 2));
            ImGui::SameLine();
            ImGui::Image((void*) (intptr_t) camera_texture_[1], ImVec2(window_size.x / 2, window_size.y - vertical_offset * 2));
            ImGui::End();
        }

        if (use_rgb_depth_) {
            ImGui::SetNextWindowSize(ImVec2(700, 350), ImGuiCond_Once);

            // if there are RGBD stream and Stereo images stream, then move the RGBD display window up
            // essentially making the display images of RGBD on top of stereo
            if (use_cam_)
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - 350),
                                        ImGuiCond_Once, ImVec2(1.0f, 1.0f));
            else
                ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once,
                                        ImVec2(1.0f, 1.0f));
            ImGui::Begin("Onboard RGBD views");
            auto window_size     = ImGui::GetWindowSize();
            auto vertical_offset = ImGui::GetCursorPos().y;
            ImGui::Image((void*) (intptr_t) rgb_depth_texture_[0], ImVec2(window_size.x / 2, window_size.y - vertical_offset * 2));
            ImGui::SameLine();
            ImGui::Image((void*) (intptr_t) rgb_depth_texture_[1], ImVec2(window_size.x / 2, window_size.y - vertical_offset * 2));
            ImGui::End();
        }

        ImGui::Render();

        RAC_ERRNO_MSG("debugview after ImGui render");
    }

    bool load_camera_images() {
        RAC_ERRNO_MSG("debugview at start of load_camera_images");

        cam_ = cam_reader_.size() == 0 ? nullptr : cam_reader_.dequeue();
        if (cam_ == nullptr) {
            return false;
        }

        if (!use_cam_)
            use_cam_ = true;

        glBindTexture(GL_TEXTURE_2D, camera_texture_[0]);
        cv::Mat img0{cam_->img0.clone()};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img0.cols, img0.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img0.ptr());
        camera_texture_size_[0] = Eigen::Vector2i(img0.cols, img0.rows);
        GLint swizzle_mask[]     = {GL_RED, GL_RED, GL_RED, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask);

        glBindTexture(GL_TEXTURE_2D, camera_texture_[1]);
        cv::Mat img1{cam_->img1.clone()}; /// <- Adding this here to simulate the copy
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img1.cols, img1.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img1.ptr());
        camera_texture_size_[1] = Eigen::Vector2i(img1.cols, img1.rows);
        GLint swizzle_mask1[]    = {GL_RED, GL_RED, GL_RED, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask1);

        RAC_ERRNO_MSG("debugview at end of load_camera_images");
        return true;
    }

    bool load_rgb_depth() {
        RAC_ERRNO_MSG("debugview at start of load_rgb_depth");

        rgb_depth_ = rgb_depth_reader_.get_ro_nullable();
        if (rgb_depth_ == nullptr) {
            return false;
        }

        if (!use_rgb_depth_)
            use_rgb_depth_ = true;

        glBindTexture(GL_TEXTURE_2D, rgb_depth_texture_[0]);
        cv::Mat rgb{rgb_depth_->rgb.clone()};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgb.cols, rgb.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgb.ptr());
        rgbd_texture_size_[0] = Eigen::Vector2i(rgb.cols, rgb.rows);

        glBindTexture(GL_TEXTURE_2D, rgb_depth_texture_[1]);
        cv::Mat depth{rgb_depth_->depth.clone()};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, depth.cols, depth.rows, 0, GL_DEPTH_COMPONENT, GL_SHORT,
                     depth.ptr());
        rgbd_texture_size_[1] = Eigen::Vector2i(depth.cols, depth.rows);
        GLint swizzle_mask[]   = {GL_RED, GL_RED, GL_RED, 1};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask);

        RAC_ERRNO_MSG("debugview at end of load_rgb_depth");
        return true;
    }

    static Eigen::Matrix4f generate_headset_transform(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation,
                                                    const Eigen::Vector3f& position_offset) {
        Eigen::Matrix4f headset_position;
        headset_position << 1, 0, 0, position.x() + position_offset.x(), 0, 1, 0, position.y() + position_offset.y(), 0, 0, 1,
            position.z() + position_offset.z(), 0, 0, 0, 1;

        // We need to convert the headset rotation quaternion to a 4x4 homogenous matrix.
        // First of all, we convert to 3x3 matrix, then extend to 4x4 by augmenting.
        Eigen::Matrix3f rotation_matrix              = rotation.toRotationMatrix();
        Eigen::Matrix4f rotation_matrix_homogeneous   = Eigen::Matrix4f::Identity();
        rotation_matrix_homogeneous.block(0, 0, 3, 3) = rotation_matrix;
        // Then we apply the headset rotation.
        return headset_position * rotation_matrix_homogeneous;
    }

    void _p_thread_setup() override {
        RAC_ERRNO_MSG("debugview at start of _p_thread_setup");

        // Note: glfwMakeContextCurrent must be called from the thread which will be using it.
        glfwMakeContextCurrent(gui_window_);
    }

    void _p_one_iteration() override {
        RAC_ERRNO_MSG("debugview at stat of _p_one_iteration");

        RAC_ERRNO_MSG("debugview before glfwPollEvents");
        glfwPollEvents();
        RAC_ERRNO_MSG("debugview after glfwPollEvents");

        if (glfwGetMouseButton(gui_window_, GLFW_MOUSE_BUTTON_LEFT)) {
            double xpos, ypos;

            glfwGetCursorPos(gui_window_, &xpos, &ypos);

            Eigen::Vector2d new_pos = Eigen::Vector2d{xpos, ypos};
            if (!being_dragged_) {
                last_mouse_position_ = new_pos;
                being_dragged_   = true;
            }
            mouse_velocity_ = new_pos - last_mouse_position_;
            last_mouse_position_ = new_pos;
        } else {
            being_dragged_ = false;
        }

        view_euler_.y() += mouse_velocity_.x() * 0.002f;
        view_euler_.x() += mouse_velocity_.y() * 0.002f;

        mouse_velocity_ = mouse_velocity_ * 0.95;

        load_camera_images();
        load_rgb_depth();

        glUseProgram(demo_shader_program_);

        Eigen::Matrix4f headset_pose = Eigen::Matrix4f::Identity();

        const fast_pose_type predicted_pose = pose_prediction_->get_fast_pose();
        if (pose_prediction_->fast_pose_reliable()) {
            const pose_type    pose         = predicted_pose.pose;
            Eigen::Quaternionf combined_quat = pose.orientation;
            headset_pose                     = generate_headset_transform(pose.position, combined_quat, tracking_position_offset_);
        }

        Eigen::Matrix4f model_matrix = Eigen::Matrix4f::Identity();

        // If we are following the headset, and have a valid pose, apply the optional offset.
        Eigen::Vector3f optional_offset = (follow_headset_ && pose_prediction_->fast_pose_reliable())
            ? (predicted_pose.pose.position + tracking_position_offset_)
            : Eigen::Vector3f{0.0f, 0.0f, 0.0f};

        Eigen::Matrix4f user_view =
            look_at(Eigen::Vector3f{(float) (view_distance_ * cos(view_euler_.y())), (float) (view_distance_ * sin(view_euler_.x())),
                                   (float) (view_distance_ * sin(view_euler_.y()))} +
                       optional_offset,
                   optional_offset, Eigen::Vector3f::UnitY());

        Eigen::Matrix4f model_view = user_view * model_matrix;

        glUseProgram(demo_shader_program_);

        // Size viewport to window size.
        int display_w, display_h;

        glfwGetFramebufferSize(gui_window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        float ratio = (float) display_h / (float) display_w;

        // Construct a basic perspective projection
        RAC_ERRNO_MSG("debugview before projection_fov");
        math_util::projection_fov(&basic_projection_, 40.0f, 40.0f, 40.0f * ratio, 40.0f * ratio, 0.03f, 20.0f);
        RAC_ERRNO_MSG("debugview after projection_fov");

        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        glClearDepth(1);

        glUniformMatrix4fv(static_cast<GLint>(model_view_attr_), 1, GL_FALSE, (GLfloat*) model_view.data());
        glUniformMatrix4fv(static_cast<GLint>(projection_attr_), 1, GL_FALSE, (GLfloat*) (basic_projection_.data()));
        glBindVertexArray(demo_vao_);

        // Draw things
        glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

        RAC_ERRNO_MSG("debugview before glClear");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RAC_ERRNO_MSG("debugview after glClear");

        demo_scene_.Draw();

        model_view = user_view * headset_pose;

        glUniformMatrix4fv(static_cast<GLint>(model_view_attr_), 1, GL_FALSE, (GLfloat*) model_view.data());

        headset_.Draw();

        draw_GUI();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        RAC_ERRNO_MSG("debugview before glfwSwapBuffers");
        glfwSwapBuffers(gui_window_);
        RAC_ERRNO_MSG("debugview after glfwSwapBuffers");
    }

    /* compatibility interface */

    // Debug view application overrides _p_start to control its own lifecycle/scheduling.
    void start() override {
        RAC_ERRNO_MSG("debugview at the top of start()");

        if (!glfwInit()) {
            ILLIXR::abort("[debugview] Failed to initialize glfw");
        }
        RAC_ERRNO_MSG("debugview after glfwInit");

        /// Registering error callback for additional debug info
        glfwSetErrorCallback(glfw_error_callback);

        /// Enable debug context for glDebugMessageCallback to work
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GL_TRUE);

        constexpr std::string_view glsl_version{"#version 330 core"};

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        gui_window_ = glfwCreateWindow(1600, 1000, "ILLIXR Debug View", nullptr, nullptr);
        if (gui_window_ == nullptr) {
            spdlog::get(name_)->error("couldn't create window {}:{}", __FILE__, __LINE__);
            ILLIXR::abort();
        }

        glfwSetWindowSize(gui_window_, 1600, 1000);

        glfwMakeContextCurrent(gui_window_);

        RAC_ERRNO_MSG("debuview before vsync");
        glfwSwapInterval(1); // Enable vsync!
        RAC_ERRNO_MSG("debugview after vysnc");

        // glEnable(GL_DEBUG_OUTPUT);
        // glDebugMessageCallback(MessageCallback, 0);

        // Init and verify GLEW
        const GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            spdlog::get(name_)->error("GLEW Error: {}", glewGetErrorString(glew_err));
            glfwDestroyWindow(gui_window_);
            ILLIXR::abort("[debugview] Failed to initialize GLEW");
        }
        RAC_ERRNO_MSG("debugview after glewInit");

        // Initialize IMGUI context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Dark theme, of course.
        ImGui::StyleColorsDark();

        // Init IMGUI for OpenGL
        ImGui_ImplGlfw_InitForOpenGL(gui_window_, true);
        ImGui_ImplOpenGL3_Init(glsl_version.data());

        // Create and bind global VAO object
        glGenVertexArrays(1, &demo_vao_);
        glBindVertexArray(demo_vao_);

        demo_shader_program_ = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
        spdlog::get(name_)->debug("Demo app shader program is program {}", demo_shader_program_);
#endif

        vertex_pos_attr    = glGetAttribLocation(demo_shader_program_, "vertexPosition");
        vertex_normal_attr_ = glGetAttribLocation(demo_shader_program_, "vertexNormal");
        model_view_attr_    = glGetUniformLocation(demo_shader_program_, "u_modelview");
        projection_attr_   = glGetUniformLocation(demo_shader_program_, "u_projection");
        color_uniform_     = glGetUniformLocation(demo_shader_program_, "u_color");
        RAC_ERRNO_MSG("debugview after glGetUniformLocation");

        // Load/initialize the demo scene.
        char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
        if (obj_dir == nullptr) {
            ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
        }

        demo_scene_ = ObjScene(std::string(obj_dir), "scene.obj");
        headset_   = ObjScene(std::string(obj_dir), "headset.obj");

        // Generate fun test pattern for missing camera images.
        for (unsigned x = 0; x < TEST_PATTERN_WIDTH; x++) {
            for (unsigned y = 0; y < TEST_PATTERN_HEIGHT; y++) {
                test_pattern_[x][y] = ((x + y) % 6 == 0) ? 255 : 0;
            }
        }

        glGenTextures(2, &(camera_texture_[0]));
        glBindTexture(GL_TEXTURE_2D, camera_texture_[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, camera_texture_[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(2, &(rgb_depth_texture_[0]));
        glBindTexture(GL_TEXTURE_2D, rgb_depth_texture_[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, rgb_depth_texture_[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Construct a basic perspective projection
        math_util::projection_fov(&basic_projection_, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f);

        glfwMakeContextCurrent(nullptr);
        threadloop::start();

        RAC_ERRNO_MSG("debuview at bottom of start()");
    }

    ~debugview() override {
        RAC_ERRNO_MSG("debugview at start of destructor");

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(gui_window_);

        RAC_ERRNO_MSG("debugview during destructor");

        glfwTerminate();
    }

private:
    // GLFWwindow * const glfw_context;
    const std::shared_ptr<switchboard>     switchboard_;
    const std::shared_ptr<pose_prediction> pose_prediction_;

    switchboard::reader<pose_type>         slow_pose_reader_;
    switchboard::reader<imu_raw_type>      fast_pose_reader_;
    switchboard::reader<rgb_depth_type>    rgb_depth_reader_;
    switchboard::buffered_reader<cam_type> cam_reader_;
    GLFWwindow*                            gui_window_{};

    [[maybe_unused]] uint8_t test_pattern_[TEST_PATTERN_WIDTH][TEST_PATTERN_HEIGHT]{};

    Eigen::Vector3d view_euler_     = Eigen::Vector3d::Zero();
    Eigen::Vector2d last_mouse_position_ = Eigen::Vector2d::Zero();
    Eigen::Vector2d mouse_velocity_ = Eigen::Vector2d::Zero();
    bool            being_dragged_   = false;

    float view_distance_ = 2.0;

    bool follow_headset_ = true;

    Eigen::Vector3f tracking_position_offset_ = Eigen::Vector3f{0.0f, 0.0f, 0.0f};

    switchboard::ptr<const cam_type>       cam_;
    switchboard::ptr<const rgb_depth_type> rgb_depth_;
    bool                                   use_cam_  = false;
    bool                                   use_rgb_depth_ = false;
    // std::vector<std::optional<cv::Mat>> camera_data = {std::nullopt, std::nullopt};
    GLuint          camera_texture_[2]{};
    Eigen::Vector2i camera_texture_size_[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};
    GLuint          rgb_depth_texture_[2]{};
    [[maybe_unused]] Eigen::Vector2i rgbd_texture_size_[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};

    GLuint demo_vao_{};
    GLuint demo_shader_program_{};

    [[maybe_unused]] GLuint vertex_pos_attr{};
    [[maybe_unused]] GLuint vertex_normal_attr_{};
    GLuint model_view_attr_{};
    GLuint projection_attr_{};

    [[maybe_unused]] GLuint color_uniform_{};

    ObjScene demo_scene_;
    ObjScene headset_;

    Eigen::Matrix4f basic_projection_;
};

PLUGIN_MAIN(debugview)
