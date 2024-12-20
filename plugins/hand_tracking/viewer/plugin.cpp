#include "plugin.hpp"

#include "illixr/opencv_data_types.hpp"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "illixr/gl_util/obj.hpp"
#include "illixr/shader_util.hpp"

#include <opencv2/opencv.hpp>

using namespace ILLIXR;
int viewer::requested_unit_ = -1;
units::measurement_unit viewer::base_unit_ = units::UNSET;

//                                mm,           cm,           m,                   ft,                    in
constexpr float convert[5][5] = {{1.,           0.1,          0.001,               (1. / (12. * 25.4)),   (1. / 25.4)},  //mm
                                 {10.,          1.,           0.01,                (1. / (12 * 2.54)),    (1. / 2.54)},  //cm
                                 {1000.,        100.,         1.,                  (100. / (2.54 * 12.)), (100. / 2.54)}, // m
                                 {(25.4 * 12.), (2.54 * 12.), (2.54 * 12. / 100.), 1.,                    12.},  // ft
                                 { 25.4,        2.54,         0.0254,              (1. / 12.),            1.}  // in
};

/**
* @brief Callback function to handle glfw errors
 */
static void glfw_error_callback(int error, const char* description) {
    spdlog::get("illixr")->error("|| glfw error_callback: {}\n|> {}", error, description);
    ILLIXR::abort();
}

/**
* @brief Callback function to handle glfw errors
*/

std::string image_type_string(const image::image_type it) {
    switch(it) {
    case image::LEFT_EYE:
    case image::LEFT_EYE_PROCESSED:
        return "left";
    case image::RIGHT_EYE:
    case image::RIGHT_EYE_PROCESSED:
        return "right";
    case image::RGB:
    case image::RGB_PROCESSED:
        return "rgb";
    case image::DEPTH:
        return "depth";
    case image::CONFIDENCE:
        return "confidence";
    }
}

viewer::viewer(const std::string& name_, phonebook* pb_) :
    plugin{name_, pb_}, _clock{pb->lookup_impl<RelativeClock>()}
    , _switchboard{pb_->lookup_impl<switchboard>()}
    , current_frame(nullptr) {
}

void viewer::start() {
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

    glfwSetWindowSize(_viewport, 640*3 + 20, 480 * 3 + 20);

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

    glGenTextures(2, &(textures[0]));
    for (unsigned int texture : textures) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    // Construct a basic perspective projection
    //math_util::projection_fov(&basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f);

    glfwMakeContextCurrent(nullptr);
    _switchboard->schedule<HandTracking::ht_frame>(id, "ht", [this](const switchboard::ptr<const HandTracking::ht_frame>& ht_frame, std::size_t) {
        this->make_gui(ht_frame);
    });
}

viewer::~viewer() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(_viewport);

    RAC_ERRNO_MSG("debugview during destructor");

    glfwTerminate();
}

void viewer::make_position_table() {
    ImGui::RadioButton("millimeter", &requested_unit_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("centimeter", &requested_unit_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("meter", &requested_unit_, 2);
    ImGui::SameLine();
    ImGui::RadioButton("foot", &requested_unit_, 3);
    ImGui::SameLine();
    ImGui::RadioButton("inch", &requested_unit_, 4);
    std::string label;
    if (ImGui::BeginTable("True Points", 2, ImGuiTableFlags_Borders)) {
        ImGui::TableNextRow();
        for (int idx = 0; idx < 2; idx++) {
            if (idx == 0){
                label = "True Points Left";
            } else {
                label = "True Points Right";
            }

            ImGui::TableSetColumnIndex(idx);
            if (ImGui::BeginTable(label.c_str(), 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Point");
                ImGui::TableSetupColumn("x");
                ImGui::TableSetupColumn("y");
                ImGui::TableSetupColumn("z");
                ImGui::TableSetupColumn("Confidence");
                ImGui::TableHeadersRow();
                auto points = current_frame->hand_positions.at(static_cast<ILLIXR::HandTracking::hand>(idx));
                bool skip = points.points.empty();
                for (int row = ILLIXR::HandTracking::WRIST; row < ILLIXR::HandTracking::PINKY_TIP; row++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", ILLIXR::HandTracking::point_str_map.at(row).c_str());
                    ILLIXR::point_with_units pnt;
                    if (!skip) {
                        pnt = points.points.at(row);
                    }
                    for (int i = 0; i < 3; i++) {
                        ImGui::TableSetColumnIndex(i + 1);
                        if (points.valid) {
                            ImGui::Text("%.3f", pnt[i] * convert[points.unit][requested_unit_]);
                        } else {
                            ImGui::Text("");
                        }
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (points.valid) {
                        ImGui::Text("%.2f", pnt.confidence);
                    } else {
                        ImGui::Text("");
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndTable();
    }
}

void viewer::make_detection_table(units::eyes eye, int idx, const std::string& label) {
    if (ImGui::BeginTabItem(label.c_str())) {
        if (ImGui::BeginTable("first_det", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("first_det_info");
            ImGui::TableSetupColumn("first_det_img");
            ImGui::TableNextRow();

            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
            ImGui::TableSetColumnIndex(0);

            if (ImGui::BeginTable("Palms", 6, flags)) {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Unit");
                ImGui::TableSetupColumn("Center");
                ImGui::TableSetupColumn("Width");
                ImGui::TableSetupColumn("Height");
                ImGui::TableSetupColumn("Rotation");
                ImGui::TableHeadersRow();
                ILLIXR::rect current_rect;
                for (auto i: ILLIXR::HandTracking::hand_map) {
                    current_rect = current_frame->detections.at(eye).palms.at(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", (i == 0) ? "Left palm" : "Right palm");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", units::unit_str.at(current_rect.unit).c_str());
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
            if (ImGui::BeginTable("Hands", 6, flags)) {
                ImGui::TableSetupColumn("");
                ImGui::TableSetupColumn("Unit");
                ImGui::TableSetupColumn("Center");
                ImGui::TableSetupColumn("Width");
                ImGui::TableSetupColumn("Height");
                ImGui::TableSetupColumn("Rotation");
                ImGui::TableHeadersRow();
                ILLIXR::rect current_rect;
                for (auto i: ILLIXR::HandTracking::hand_map) {
                    current_rect = current_frame->detections.at(eye).hands.at(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", (i == 0) ? "Left hand" : "Right hand");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", units::unit_str.at(current_rect.unit).c_str());
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
            if (ImGui::BeginTable("Hand Points", 3, flags)) {
                ImGui::TableSetupColumn("Point");
                ImGui::TableSetupColumn("Left");
                ImGui::TableSetupColumn("Right");
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", "Confidence");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f", current_frame->detections.at(eye).confidence.at(ILLIXR::HandTracking::LEFT_HAND));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", current_frame->detections.at(eye).confidence.at(ILLIXR::HandTracking::RIGHT_HAND));
                for (int row = ILLIXR::HandTracking::WRIST; row <= ILLIXR::HandTracking::PINKY_TIP; row++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", ILLIXR::HandTracking::point_str_map.at(row).c_str());
                    ImGui::TableSetColumnIndex(1);
                    auto pnt = current_frame->detections.at(eye).points.at(ILLIXR::HandTracking::LEFT_HAND).points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x(), pnt.y(), pnt.z());
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                               ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                    ImGui::TableSetColumnIndex(2);
                    pnt = current_frame->detections.at(eye).points.at(ILLIXR::HandTracking::RIGHT_HAND).points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x(), pnt.y(), pnt.z());
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                               ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                }

                ImGui::EndTable();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Image((void *) (intptr_t) textures[idx], ImVec2(640, 480));
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
}

void viewer::make_gui(const switchboard::ptr<const HandTracking::ht_frame>& frame) {
    glfwMakeContextCurrent(_viewport);
    glfwPollEvents();
    current_frame = frame.get();

    if (base_unit_ == units::UNSET) {
        base_unit_ = current_frame->unit;
        requested_unit_ = base_unit_;
    }
    std::vector<image::image_type> found_types;
    cv::Mat raw_img[2];
    int last_idx = 0;


    if (current_frame->find(image::LEFT_EYE_PROCESSED) != current_frame->images.end()) {
        found_types.push_back(image::LEFT_EYE_PROCESSED);
        raw_img[0] = current_frame->at(image::LEFT_EYE).clone();
        last_idx++;
    }

    if (current_frame->find(image::RIGHT_EYE_PROCESSED) != current_frame->images.end()) {
        found_types.push_back(image::RIGHT_EYE_PROCESSED);
        raw_img[last_idx] = current_frame->at(image::RIGHT_EYE).clone();
    }

    if (found_types.empty() && current_frame->find(image::RGB_PROCESSED) != current_frame->images.end()) {
        found_types.push_back(image::RGB_PROCESSED);
        raw_img[0] = current_frame->at(image::RGB).clone();
    }

    cv::Mat processed[2];
    cv::Mat combined[2], flattened[2];
    int d_width, d_height;
    glfwGetFramebufferSize(_viewport, &d_width, &d_height);
    glViewport(0, 0, d_width, d_height);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();
    float proc_time;
    if (current_frame->detections.count(units::LEFT_EYE) == 1 && current_frame->detections.count(units::RIGHT_EYE) == 1) {
        raw_img[0] = current_frame->at(image::LEFT_EYE).clone();
        raw_img[1] = current_frame->at(image::RIGHT_EYE).clone();
        enabled_right = true;
        tab_label = "Left Eye Raw";
        single_eye = units::LEFT_EYE;
        detections = {image::LEFT_EYE_PROCESSED, image::RIGHT_EYE_PROCESSED};
        proc_time = (float)std::max(current_frame->detections.at(static_cast<const units::eyes>(image::LEFT_EYE)).proc_time,
                                    current_frame->detections.at(static_cast<const units::eyes>(image::RIGHT_EYE)).proc_time) / 1000.f;
    } else {
        image::image_type img_typ;
        if (current_frame->detections.count(units::LEFT_EYE) == 1) {
            single_eye = units::LEFT_EYE;
            detections = {image::LEFT_EYE_PROCESSED};
            img_typ = image::LEFT_EYE;
            proc_time = (float)current_frame->detections.at(static_cast<const units::eyes>(image::LEFT_EYE)).proc_time / 1000.f;
        } else if (current_frame->detections.count(units::RIGHT_EYE) == 1){
            single_eye = units::RIGHT_EYE;
            detections = {image::RIGHT_EYE_PROCESSED};
            img_typ = image::RIGHT_EYE;
            proc_time = (float)current_frame->detections.at(static_cast<const units::eyes>(image::RIGHT_EYE)).proc_time / 1000.f;
        } else {
            return;
        }
        raw_img[0] = current_frame->at(img_typ).clone();
        raw_img[1].release();
        enabled_right = false;
        tab_label = "Raw";
    }

    for (size_t i = 0; i < 2; i++) {
        if (i == 1 && !enabled_right)
            break;
        processed[i] = current_frame->at(detections[i]).clone();
        cv::Mat mask, inv, r;
        cv::cvtColor(processed[i], mask, cv::COLOR_RGBA2GRAY);
        cv::threshold(mask, mask, 10, 255, cv::THRESH_BINARY);
        cv::bitwise_not(mask, mask);
        cv::bitwise_and(raw_img[i], raw_img[i], r, mask);
        cv::cvtColor(processed[i], flattened[i], cv::COLOR_RGBA2RGB);
        combined[i] = flattened[i] + r;
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, combined[i].cols, combined[i].rows, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, combined[i].ptr());
    }

    if (ImGui::Begin("Hand Detections")) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")){
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if (ImGui::BeginTable("times_table", 2, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("title_label");
            ImGui::TableSetupColumn("time_label");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", "Processing time (ms)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%f", proc_time);
            ImGui::EndTable();
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("TabBar")) {
            make_detection_table(single_eye, 0, tab_label);
            if (enabled_right) {
                make_detection_table(units::RIGHT_EYE, 1, "Right Eye Raw");
            }
            if (ImGui::BeginTabItem("True Position")) {
                make_position_table();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(_viewport);
}

PLUGIN_MAIN(viewer)
