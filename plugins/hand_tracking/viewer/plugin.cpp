#include "illixr/hand_tracking_data.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/plugin.hpp"

#include <opencv2/opencv.hpp>

// clang-format off
#include <GL/glew.h>    // GLEW has to be loaded before other GL libraries
#include <GLFW/glfw3.h> // Also loading first, just to be safe
// clang-format on

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "illixr/gl_util/obj.hpp"
#include "illixr/shaders/demo_shader.hpp"
#include "illixr/shader_util.hpp"
#include "illixr/math_util.hpp"

using namespace ILLIXR;

/**
 * @brief Callback function to handle glfw errors
 */
static void glfw_error_callback(int error, const char* description) {
    spdlog::get("illixr")->error("|| glfw error_callback: {}\n|> {}", error, description);
    ILLIXR::abort();
}

constexpr char kWindowName[] = "HandTracking";

class viewer : public plugin {
public:
    viewer(const std::string& name_, phonebook* pb_) :
        plugin{name_, pb_}, _clock{pb->lookup_impl<RelativeClock>()}
        , _switchboard{pb_->lookup_impl<switchboard>()}
        //, _frame_reader{_switchboard->get_reader<ht_frame>("ht")}
        , _raw_frame{_switchboard->get_buffered_reader<frame_type>("webcam")} {}

    void start() override {
        plugin::start();
        if (!glfwInit()) {
            ILLIXR::abort("[viewer] Failed to initalize glfw");
        }
        
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
        _viewport = glfwCreateWindow(640*3 + 20, 1000, "ILLIXR Debug View", nullptr, nullptr);
        if (_viewport == nullptr) {
            spdlog::get(name)->error("couldn't create window {}:{}", __FILE__, __LINE__);
            ILLIXR::abort();
        }

        glfwSetWindowSize(_viewport, 640*3 + 20, 1000);

        glfwMakeContextCurrent(_viewport);

        glfwSwapInterval(1);
        
        const GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            //spdlog::get(name)->error("GLEW Error: {}", glewGetErrorString(glew_err));
            glfwDestroyWindow(_viewport);
            ILLIXR::abort("[debugview] Failed to initialize GLEW");
        }
        
        // Initialize IMGUI context.
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        
        // Dark theme, of course.
        ImGui::StyleColorsDark();

        // Init IMGUI for OpenGL
        ImGui_ImplGlfw_InitForOpenGL(_viewport, true);
        ImGui_ImplOpenGL3_Init(glsl_version.data());

        // Create and bind global VAO object
        glGenVertexArrays(1, &demo_vao);
        glBindVertexArray(demo_vao);

        demoShaderProgram = init_and_link(demo_vertex_shader, demo_fragment_shader);

        vertexPosAttr    = glGetAttribLocation(demoShaderProgram, "vertexPosition");
        vertexNormalAttr = glGetAttribLocation(demoShaderProgram, "vertexNormal");
        modelViewAttr    = glGetUniformLocation(demoShaderProgram, "u_modelview");
        projectionAttr   = glGetUniformLocation(demoShaderProgram, "u_projection");
        colorUniform     = glGetUniformLocation(demoShaderProgram, "u_color");

        glGenTextures(3, &(textures[0]));
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, textures[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, textures[2]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Construct a basic perspective projection
        math_util::projection_fov(&basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f);

        glfwMakeContextCurrent(nullptr);
        //threadloop::start();
        _switchboard->schedule<ht_frame>(id, "ht", [this](const switchboard::ptr<const ht_frame>& ht_frame, std::size_t) {
            this->make_gui(ht_frame);
        });
    }

    ~viewer() override {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(_viewport);

        RAC_ERRNO_MSG("debugview during destructor");

        glfwTerminate();
        delete nullframe;
    }

    //void _p_thread_setup() override {
    //    glfwMakeContextCurrent(_viewport);
    //}

    //void _p_one_iteration() override {
    void make_gui(const switchboard::ptr<const ht_frame>& frame) {
        glfwMakeContextCurrent(_viewport);
        glfwPollEvents();

        cv::Mat raw_img;
        // get raw frame from camera input
        auto raw = _raw_frame.size() == 0 ? nullptr : _raw_frame.dequeue();
        if(raw != nullptr) {
            raw_img = raw->img.clone();
            cv::flip(raw_img, raw_img, /*flipcode=HORIZONTAL*/ 1);
            glBindTexture(GL_TEXTURE_2D, textures[0]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, raw_img.cols, raw_img.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, raw_img.ptr());
        }

        //auto frame = _frame_reader.get_ro_nullable();
        // get processed frame
        cv::Mat processed;
        cv::Mat combined, flattened;
        //std::cout << "Have Frame " << _clock->absolute_ns(frame->time) << " " << std::endl;
        current_frame = frame.get();
        if(!frame->img.empty()) {
            processed = frame->img.clone();
            cv::Mat mask, inv, r;
            cv::cvtColor(processed, mask, cv::COLOR_RGBA2GRAY);
            cv::threshold(mask, mask, 10, 255, cv::THRESH_BINARY);
            cv::bitwise_not(mask, mask);
            cv::bitwise_and(raw_img, raw_img, r, mask);
            cv::imwrite("test1.png", r);
            cv::cvtColor(processed, flattened, cv::COLOR_RGBA2RGB);
            glBindTexture(GL_TEXTURE_2D, textures[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,  flattened.cols, flattened.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, flattened.ptr());

            combined = flattened + r;
            glBindTexture(GL_TEXTURE_2D, textures[2]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, combined.cols, combined.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, combined.ptr());
        }

        glUseProgram(demoShaderProgram);
        int d_width, d_height;
        glfwGetFramebufferSize(_viewport, &d_width, &d_height);
        glViewport(0, 0, d_width, d_height);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();

        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.f, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once, ImVec2(0.f, 1.f));
        {
            ImGui::Begin("Hand Detections");

            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;

            if (ImGui::BeginTable("palms", 6, flags)) {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Unit");
                ImGui::TableSetupColumn("Center");
                ImGui::TableSetupColumn("Width");
                ImGui::TableSetupColumn("Height");
                ImGui::TableSetupColumn("Rotation");
                ImGui::TableHeadersRow();
                rect current_rect;
                for (int i = 0; i < 2; i++) {
                    current_rect = (i == 0) ? current_frame->left_palm : current_frame->right_palm;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", (i == 0) ? "Left palm" : "Right palm");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", (current_rect.normalized) ? "%" : "px");
                    if (current_rect.valid) {
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.2f, %.2f", current_rect.x_center, current_rect.y_center);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.2f", current_rect.width);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%.2f", current_rect.height);
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text("%.2f", current_rect.rotation);
                    }
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("hands", 6, flags)) {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Unit");
                ImGui::TableSetupColumn("Center");
                ImGui::TableSetupColumn("Width");
                ImGui::TableSetupColumn("Height");
                ImGui::TableSetupColumn("Rotation");
                ImGui::TableHeadersRow();
                rect current_rect;
                for (int i = 0; i < 2; i++) {
                    current_rect = (i == 0) ? current_frame->left_hand : current_frame->right_hand;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", (i == 0) ? "Left hand" : "Right hand");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", (current_rect.normalized) ? "%" : "px");
                    if (current_rect.valid) {
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.2f, %.2f", current_rect.x_center, current_rect.y_center);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.2f", current_rect.width);
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%.2f", current_rect.height);
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text("%.2f", current_rect.rotation);
                    }
                }
                ImGui::EndTable();
            }

            if (ImGui::BeginTable("handResults", 3, flags)) {
                ImGui::TableSetupColumn("Point");
                ImGui::TableSetupColumn("Left");
                ImGui::TableSetupColumn("Right");
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", "Confidence");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", current_frame->left_confidence);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", current_frame->right_confidence);
                for (int row = WRIST; row <= PINKY_TIP; row++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", point_map.at(row).c_str());
                    ImGui::TableSetColumnIndex(1);
                    auto pnt = current_frame->left_hand_points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x, pnt.y, pnt.z);
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                    ImGui::TableSetColumnIndex(2);
                    pnt = current_frame->right_hand_points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x, pnt.y, pnt.z);
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                }

                ImGui::EndTable();
            }

            ImGui::End();
        }
        ImGui::SetNextWindowSize(ImVec2(640*3, 480), ImGuiCond_Once);
        //ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Once, ImVec2(1.f, 1.f));

        {
            ImGui::Begin("Images");
            if(raw != nullptr) {
                auto windowsize  = ImGui::GetWindowSize();
                auto vert_offset = ImGui::GetCursorPos().y;
                ImGui::Image((void*) (intptr_t) textures[0], ImVec2(windowsize.x / 3, windowsize.y));
                if (!processed.empty()) {
                    ImGui::SameLine();
                    ImGui::Image((void*) (intptr_t) textures[1], ImVec2(windowsize.x / 3, windowsize.y));
                    ImGui::SameLine();
                    ImGui::Image((void*) (intptr_t) textures[2], ImVec2(windowsize.x / 3, windowsize.y));
                }
            }
            ImGui::End();
        }
        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(_viewport);

        //std::this_thread::sleep_for(std::chrono::nanoseconds(33300000));
        count++;
    }
private:
    std::shared_ptr<RelativeClock> _clock;
    const std::shared_ptr<switchboard> _switchboard;
    //switchboard::reader<ht_frame>      _frame_reader;
    switchboard::buffered_reader<frame_type>  _raw_frame;
    std::shared_ptr<ht_frame>          _ht_frame;
    GLFWwindow*                        _viewport{};
    uint count = 0;
    GLuint demo_vao;
    GLuint demoShaderProgram;
    bool have_raw = false;
    bool have_frame = false;
    uint raw_width = 0;
    uint raw_height = 0;
    GLuint vertexPosAttr;
    GLuint vertexNormalAttr;
    GLuint modelViewAttr;
    GLuint projectionAttr;

    GLuint colorUniform;

    GLuint          textures[3];
    Eigen::Vector2i raw_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i processed_size = Eigen::Vector2i::Zero();
    Eigen::Vector2i combined_size = Eigen::Vector2i::Zero();

    Eigen::Matrix4f basicProjection;
    const ht_frame *nullframe = new ht_frame(time_point(), nullptr, nullptr, nullptr, nullptr, nullptr, 0., 0., nullptr, nullptr);
    const ht_frame *current_frame;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

PLUGIN_MAIN(viewer)
