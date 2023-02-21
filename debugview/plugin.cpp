// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "common/data_format.hpp"
#include "common/error_util.hpp"
#include "common/gl_util/obj.hpp"
#include "common/global_module_defs.hpp"
#include "common/math_util.hpp"
#include "common/pose_prediction.hpp"
#include "common/shader_util.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "shaders/demo_shader.hpp"

#include <chrono>
#include <cmath>
#include <functional>
#include <future>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <string_view>
#include <thread>

using namespace ILLIXR;

constexpr size_t TEST_PATTERN_WIDTH  = 256;
constexpr size_t TEST_PATTERN_HEIGHT = 256;

// Loosely inspired by
// http://spointeau.blogspot.com/2013/12/hello-i-am-looking-at-opengl-3.html

Eigen::Matrix4f lookAt(Eigen::Vector3f eye, Eigen::Vector3f target, Eigen::Vector3f up) {
    using namespace Eigen;
    Vector3f lookDir = (target - eye).normalized();
    Vector3f upDir   = up.normalized();
    Vector3f sideDir = lookDir.cross(upDir).normalized();
    upDir            = sideDir.cross(lookDir);

    Matrix4f result;
    result << sideDir.x(), sideDir.y(), sideDir.z(), -sideDir.dot(eye), upDir.x(), upDir.y(), upDir.z(), -upDir.dot(eye),
        -lookDir.x(), -lookDir.y(), -lookDir.z(), lookDir.dot(eye), 0, 0, 0, 1;

    return result;
}

/**
 * @brief Callback function to handle glfw errors
 */
static void glfw_error_callback(int error, const char* description) {
    std::cerr << "|| glfw error_callback: " << error << std::endl << "|> " << description << std::endl;
    ILLIXR::abort();
}

class debugview : public threadloop {
public:
    // Public constructor, Spindle passes the phonebook to this
    // constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the plugin can read
    // the data whenever it needs to.
    debugview(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , pp{pb->lookup_impl<pose_prediction>()}
        , _m_slow_pose{sb->get_reader<pose_type>("slow_pose")}
        , _m_fast_pose{sb->get_reader<imu_raw_type>("imu_raw")} //, glfw_context{pb->lookup_impl<global_config>()->glfw_context}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , _m_cam_vins{sb->get_buffered_reader<cam_type>("vins")} { }

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
            ImGui::Checkbox("Follow headset position", &follow_headset);

            ImGui::SliderFloat("View distance ", &view_dist, 0.1f, 10.0f);

            ImGui::SliderFloat3("Tracking \"offset\"", tracking_position_offset.data(), -10.0f, 10.0f);

            if (ImGui::Button("Reset")) {
                tracking_position_offset = Eigen::Vector3f{5.0f, 2.0f, -3.0f};
            }
            ImGui::SameLine();
            ImGui::Text("Resets to default tracking universe");

            if (ImGui::Button("Zero")) {
                tracking_position_offset = Eigen::Vector3f{0.0f, 0.0f, 0.0f};
            }
            ImGui::SameLine();
            ImGui::Text("Resets to zero'd out tracking universe");

            if (ImGui::Button("Zero orientation")) {
                const pose_type predicted_pose = pp->get_fast_pose().pose;
                if (pp->fast_pose_reliable()) {
                    // Can only zero if predicted_pose is valid
                    pp->set_offset(predicted_pose.orientation);
                }
            }
            ImGui::SameLine();
            ImGui::Text("Resets to zero'd out tracking universe");
        }
        ImGui::Spacing();
        ImGui::Text("Switchboard connection status:");
        ImGui::Text("Predicted pose topic:");
        ImGui::SameLine();

        if (pp->fast_pose_reliable()) {
            const pose_type predicted_pose = pp->get_fast_pose().pose;
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid predicted pose pointer");
            ImGui::Text("Prediced pose position (XYZ):\n  (%f, %f, %f)", predicted_pose.position.x(),
                        predicted_pose.position.y(), predicted_pose.position.z());
            ImGui::Text("Predicted pose quaternion (XYZW):\n  (%f, %f, %f, %f)", predicted_pose.orientation.x(),
                        predicted_pose.orientation.y(), predicted_pose.orientation.z(), predicted_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid predicted pose pointer");
        }

        ImGui::Text("Fast pose topic:");
        ImGui::SameLine();

        switchboard::ptr<const imu_raw_type> raw_imu = _m_fast_pose.get_ro_nullable();
        if (raw_imu) {
            pose_type raw_pose;
            raw_pose.position      = Eigen::Vector3f{float(raw_imu->pos(0)), float(raw_imu->pos(1)), float(raw_imu->pos(2))};
            raw_pose.orientation   = Eigen::Quaternionf{float(raw_imu->quat.w()), float(raw_imu->quat.x()),
                                                      float(raw_imu->quat.y()), float(raw_imu->quat.z())};
            pose_type swapped_pose = pp->correct_pose(raw_pose);

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

        switchboard::ptr<const pose_type> slow_pose_ptr = _m_slow_pose.get_ro_nullable();
        if (slow_pose_ptr) {
            pose_type swapped_pose = pp->correct_pose(*slow_pose_ptr);
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

        if (pp->true_pose_reliable()) {
            const pose_type true_pose = pp->get_true_pose();
            ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), "Valid ground truth pose pointer");
            ImGui::Text("Ground truth position (XYZ):\n  (%f, %f, %f)", true_pose.position.x(), true_pose.position.y(),
                        true_pose.position.z());
            ImGui::Text("Ground truth quaternion (XYZW):\n  (%f, %f, %f, %f)", true_pose.orientation.x(),
                        true_pose.orientation.y(), true_pose.orientation.z(), true_pose.orientation.w());
        } else {
            ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Invalid ground truth pose pointer");
        }

        ImGui::Text("Debug view eulers:");
        ImGui::Text("	(%f, %f)", view_euler.x(), view_euler.y());

        ImGui::End();

        ImGui::Begin("Camera + IMU");
        ImGui::Text("Camera view buffers: ");
        ImGui::Text("	Camera0: (%d, %d) \n		GL texture handle: %d", camera_texture_sizes[0].x(),
                    camera_texture_sizes[0].y(), camera_textures[0]);
        ImGui::Text("	Camera1: (%d, %d) \n		GL texture handle: %d", camera_texture_sizes[1].x(),
                    camera_texture_sizes[1].y(), camera_textures[1]);
        ImGui::Text("	Vins0: (%d, %d) \n		GL texture handle: %d", camera_texture_sizes_vins[0].x(),
                    camera_texture_sizes_vins[0].y(), camera_textures_vins[0]);
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(700, 350), ImGuiCond_Once);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once,
                                ImVec2(1.0f, 1.0f));
        ImGui::Begin("Onboard Camera Views");
        auto windowSize     = ImGui::GetWindowSize();
        auto verticalOffset = ImGui::GetCursorPos().y;
        ImGui::Image((void*) (intptr_t) camera_textures[0], ImVec2(windowSize.x / 2, windowSize.y - verticalOffset * 2));
        ImGui::SameLine();
        ImGui::Image((void*) (intptr_t) camera_textures[1], ImVec2(windowSize.x / 2, windowSize.y - verticalOffset * 2));
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(700, 350), ImGuiCond_Once);
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - windowSize.y),
                                ImGuiCond_Once, ImVec2(1.0f, 1.0f));
        ImGui::Begin("Visual-Inertial Tracking");
        ImGui::Image((void*) (intptr_t) camera_textures_vins[0], ImVec2(windowSize.x, windowSize.y - verticalOffset * 2));
        ImGui::End();

        ImGui::Render();

        RAC_ERRNO_MSG("debugview after ImGui render");
    }

    bool load_camera_images() {
        RAC_ERRNO_MSG("debugview at start of load_camera_images");

        cam = _m_cam.size() == 0 ? nullptr : _m_cam.dequeue();
        if (cam != nullptr) {
            glBindTexture(GL_TEXTURE_2D, camera_textures[0]);
            cv::Mat img0{cam->img0.clone()};
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img0.cols, img0.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img0.ptr());
            camera_texture_sizes[0] = Eigen::Vector2i(img0.cols, img0.rows);
            GLint swizzleMask[]     = {GL_RED, GL_RED, GL_RED, GL_RED};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

            glBindTexture(GL_TEXTURE_2D, camera_textures[1]);
            cv::Mat img1{cam->img1.clone()}; /// <- Adding this here to simulate the copy
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, img1.cols, img1.rows, 0, GL_RED, GL_UNSIGNED_BYTE, img1.ptr());
            camera_texture_sizes[1] = Eigen::Vector2i(img1.cols, img1.rows);
            GLint swizzleMask1[]    = {GL_RED, GL_RED, GL_RED, GL_RED};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask1);
        }

        cam_vins = _m_cam_vins.size() == 0 ? nullptr : _m_cam_vins.dequeue();
        if (cam_vins != nullptr) {
            glBindTexture(GL_TEXTURE_2D, camera_textures_vins[0]);
            cv::Mat img0{cam_vins->img0.clone()}; /// <- Adding this here to simulate the copy
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img0.cols, img0.rows, 0, GL_BGR, GL_UNSIGNED_BYTE, img0.ptr());
            camera_texture_sizes_vins[0] = Eigen::Vector2i(img0.cols, img0.rows);
        }

        if (cam == nullptr && cam_vins == nullptr)
            return false;
        RAC_ERRNO_MSG("debugview at end of load_camera_images");
        return true;
    }

    Eigen::Matrix4f generateHeadsetTransform(const Eigen::Vector3f& position, const Eigen::Quaternionf& rotation,
                                             const Eigen::Vector3f& positionOffset) {
        Eigen::Matrix4f headsetPosition;
        headsetPosition << 1, 0, 0, position.x() + positionOffset.x(), 0, 1, 0, position.y() + positionOffset.y(), 0, 0, 1,
            position.z() + positionOffset.z(), 0, 0, 0, 1;

        // We need to convert the headset rotation quaternion to a 4x4 homogenous matrix.
        // First of all, we convert to 3x3 matrix, then extend to 4x4 by augmenting.
        Eigen::Matrix3f rotationMatrix              = rotation.toRotationMatrix();
        Eigen::Matrix4f rotationMatrixHomogeneous   = Eigen::Matrix4f::Identity();
        rotationMatrixHomogeneous.block(0, 0, 3, 3) = rotationMatrix;
        // Then we apply the headset rotation.
        return headsetPosition * rotationMatrixHomogeneous;
    }

    void _p_thread_setup() override {
        RAC_ERRNO_MSG("debugview at start of _p_thread_setup");

        // Note: glfwMakeContextCurrent must be called from the thread which will be using it.
        glfwMakeContextCurrent(gui_window);
    }

    void _p_one_iteration() override {
        RAC_ERRNO_MSG("debugview at stat of _p_one_iteration");

        RAC_ERRNO_MSG("debugview before glfwPollEvents");
        glfwPollEvents();
        RAC_ERRNO_MSG("debugview after glfwPollEvents");

        if (glfwGetMouseButton(gui_window, GLFW_MOUSE_BUTTON_LEFT)) {
            double xpos, ypos;

            glfwGetCursorPos(gui_window, &xpos, &ypos);

            Eigen::Vector2d new_pos = Eigen::Vector2d{xpos, ypos};
            if (beingDragged == false) {
                last_mouse_pos = new_pos;
                beingDragged   = true;
            }
            mouse_velocity = new_pos - last_mouse_pos;
            last_mouse_pos = new_pos;
        } else {
            beingDragged = false;
        }

        view_euler.y() += mouse_velocity.x() * 0.002f;
        view_euler.x() += mouse_velocity.y() * 0.002f;

        mouse_velocity = mouse_velocity * 0.95;

        load_camera_images();

        glUseProgram(demoShaderProgram);

        Eigen::Matrix4f headsetPose = Eigen::Matrix4f::Identity();

        const fast_pose_type predicted_pose = pp->get_fast_pose();
        if (pp->fast_pose_reliable()) {
            const pose_type    pose         = predicted_pose.pose;
            Eigen::Quaternionf combinedQuat = pose.orientation;
            headsetPose                     = generateHeadsetTransform(pose.position, combinedQuat, tracking_position_offset);
        }

        Eigen::Matrix4f modelMatrix = Eigen::Matrix4f::Identity();

        // If we are following the headset, and have a valid pose, apply the optional offset.
        Eigen::Vector3f optionalOffset = (follow_headset && pp->fast_pose_reliable())
            ? (predicted_pose.pose.position + tracking_position_offset)
            : Eigen::Vector3f{0.0f, 0.0f, 0.0f};

        Eigen::Matrix4f userView =
            lookAt(Eigen::Vector3f{(float) (view_dist * cos(view_euler.y())), (float) (view_dist * sin(view_euler.x())),
                                   (float) (view_dist * sin(view_euler.y()))} +
                       optionalOffset,
                   optionalOffset, Eigen::Vector3f::UnitY());

        Eigen::Matrix4f modelView = userView * modelMatrix;

        glUseProgram(demoShaderProgram);

        // Size viewport to window size.
        int display_w, display_h;

        glfwGetFramebufferSize(gui_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        float ratio = (float) display_h / (float) display_w;

        // Construct a basic perspective projection
        RAC_ERRNO_MSG("debugview before projection_fov");
        math_util::projection_fov(&basicProjection, 40.0f, 40.0f, 40.0f * ratio, 40.0f * ratio, 0.03f, 20.0f);
        RAC_ERRNO_MSG("debugview after projection_fov");

        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        glClearDepth(1);

        glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*) modelView.data());
        glUniformMatrix4fv(projectionAttr, 1, GL_FALSE, (GLfloat*) (basicProjection.data()));
        glBindVertexArray(demo_vao);

        // Draw things
        glClearColor(0.8f, 0.8f, 0.8f, 1.0f);

        RAC_ERRNO_MSG("debugview before glClear");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RAC_ERRNO_MSG("debugview after glClear");

        demoscene.Draw();

        modelView = userView * headsetPose;

        glUniformMatrix4fv(modelViewAttr, 1, GL_FALSE, (GLfloat*) modelView.data());

        headset.Draw();

        draw_GUI();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        RAC_ERRNO_MSG("debugview before glfwSwapBuffers");
        glfwSwapBuffers(gui_window);
        RAC_ERRNO_MSG("debugview after glfwSwapBuffers");
    }

private:
    // GLFWwindow * const glfw_context;
    const std::shared_ptr<switchboard>     sb;
    const std::shared_ptr<pose_prediction> pp;

    switchboard::reader<pose_type>         _m_slow_pose;
    switchboard::reader<imu_raw_type>      _m_fast_pose;
    switchboard::buffered_reader<cam_type> _m_cam;
    switchboard::buffered_reader<cam_type> _m_cam_vins;
    GLFWwindow*                            gui_window;

    uint8_t test_pattern[TEST_PATTERN_WIDTH][TEST_PATTERN_HEIGHT];

    Eigen::Vector3d view_euler     = Eigen::Vector3d::Zero();
    Eigen::Vector2d last_mouse_pos = Eigen::Vector2d::Zero();
    Eigen::Vector2d mouse_velocity = Eigen::Vector2d::Zero();
    bool            beingDragged   = false;

    float view_dist = 2.0;

    bool follow_headset = true;

    Eigen::Vector3f tracking_position_offset = Eigen::Vector3f{0.0f, 0.0f, 0.0f};

    switchboard::ptr<const cam_type> cam;
    // std::vector<std::optional<cv::Mat>> camera_data = {std::nullopt, std::nullopt};
    GLuint          camera_textures[2];
    Eigen::Vector2i camera_texture_sizes[2] = {Eigen::Vector2i::Zero(), Eigen::Vector2i::Zero()};

    switchboard::ptr<const cam_type> cam_vins;
    GLuint                           camera_textures_vins[1];
    Eigen::Vector2i                  camera_texture_sizes_vins[1] = {Eigen::Vector2i::Zero()};

    GLuint demo_vao;
    GLuint demoShaderProgram;

    GLuint vertexPosAttr;
    GLuint vertexNormalAttr;
    GLuint modelViewAttr;
    GLuint projectionAttr;

    GLuint colorUniform;

    ObjScene demoscene;
    ObjScene headset;

    Eigen::Matrix4f basicProjection;

public:
    /* compatibility interface */

    // Debug view application overrides _p_start to control its own lifecycle/scheduling.
    virtual void start() override {
        RAC_ERRNO_MSG("debugview at the top of start()");

        if (!glfwInit()) {
            ILLIXR::abort("[debugview] Failed to initalize glfw");
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

        gui_window = glfwCreateWindow(1600, 1000, "ILLIXR Debug View", nullptr, nullptr);
        if (gui_window == nullptr) {
            std::cerr << "Debug view couldn't create window " << __FILE__ << ":" << __LINE__ << std::endl;
            ILLIXR::abort();
        }

        glfwSetWindowSize(gui_window, 1600, 1000);

        glfwMakeContextCurrent(gui_window);

        RAC_ERRNO_MSG("debuview before vsync");
        glfwSwapInterval(1); // Enable vsync!
        RAC_ERRNO_MSG("debugview after vysnc");

        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, 0);

        // Init and verify GLEW
        const GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            std::cerr << "[debugview] GLEW Error: " << glewGetErrorString(glew_err) << std::endl;
            glfwDestroyWindow(gui_window);
            ILLIXR::abort("[debugview] Failed to initialize GLEW");
        }
        RAC_ERRNO_MSG("debugview after glewInit");

        // Initialize IMGUI context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Dark theme, of course.
        ImGui::StyleColorsDark();

        // Init IMGUI for OpenGL
        ImGui_ImplGlfw_InitForOpenGL(gui_window, true);
        ImGui_ImplOpenGL3_Init(glsl_version.data());

        // Create and bind global VAO object
        glGenVertexArrays(1, &demo_vao);
        glBindVertexArray(demo_vao);

        demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);
#ifndef NDEBUG
        std::cout << "Demo app shader program is program " << demoShaderProgram << std::endl;
#endif

        vertexPosAttr    = glGetAttribLocation(demoShaderProgram, "vertexPosition");
        vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
        modelViewAttr    = glGetUniformLocation(demoShaderProgram, "u_modelview");
        projectionAttr   = glGetUniformLocation(demoShaderProgram, "u_projection");
        colorUniform     = glGetUniformLocation(demoShaderProgram, "u_color");
        RAC_ERRNO_MSG("debugview after glGetUniformLocation");

        // Load/initialize the demo scene.
        char* obj_dir = std::getenv("ILLIXR_DEMO_DATA");
        if (obj_dir == nullptr) {
            ILLIXR::abort("Please define ILLIXR_DEMO_DATA.");
        }

        demoscene = ObjScene(std::string(obj_dir), "scene.obj");
        headset   = ObjScene(std::string(obj_dir), "headset.obj");

        // Generate fun test pattern for missing camera images.
        for (unsigned x = 0; x < TEST_PATTERN_WIDTH; x++) {
            for (unsigned y = 0; y < TEST_PATTERN_HEIGHT; y++) {
                test_pattern[x][y] = ((x + y) % 6 == 0) ? 255 : 0;
            }
        }

        glGenTextures(2, &(camera_textures[0]));
        glBindTexture(GL_TEXTURE_2D, camera_textures[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, camera_textures[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenTextures(1, &(camera_textures_vins[0]));
        glBindTexture(GL_TEXTURE_2D, camera_textures_vins[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Construct a basic perspective projection
        math_util::projection_fov(&basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f);

        glfwMakeContextCurrent(nullptr);
        threadloop::start();

        RAC_ERRNO_MSG("debuview at bottom of start()");
    }

    virtual ~debugview() override {
        RAC_ERRNO_MSG("debugview at start of destructor");

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(gui_window);

        RAC_ERRNO_MSG("debugview during destructor");

        glfwTerminate();
    }
};

PLUGIN_MAIN(debugview);
