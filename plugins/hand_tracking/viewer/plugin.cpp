#include "plugin.hpp"

#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/gl_util/obj.hpp"
#include "illixr/imgui/backends/imgui_impl_glfw.h"
#include "illixr/imgui/backends/imgui_impl_opengl3.h"
#include "illixr/shader_util.hpp"

#include <opencv2/opencv.hpp>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

int                     viewer::requested_unit_ = -1;
units::measurement_unit viewer::base_unit_      = units::UNSET;

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
    switch (it) {
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
    return "";
}

viewer::viewer(const std::string& name_, phonebook* pb_)
    : plugin{name_, pb_}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , switchboard_{pb_->lookup_impl<switchboard>()}
    , pose_{switchboard_->root_coordinates.position(), switchboard_->root_coordinates.orientation()}
    , current_frame_(nullptr) {
}

void viewer::start() {
    plugin::start();
    if (!glfwInit()) {
        ILLIXR::abort("[viewer] Failed to initialize glfw");
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
    viewport_ = glfwCreateWindow(640 * 3 + 20, 1000, "ILLIXR Hand Tracking Viewer", nullptr, nullptr);
    if (viewport_ == nullptr) {
        spdlog::get(name_)->error("couldn't create window {}:{}", __FILE__, __LINE__);
        ILLIXR::abort();
    }

    glfwSetWindowSize(viewport_, 640 * 3 + 20, 480 * 3 + 20);

    glfwMakeContextCurrent(viewport_);

    glfwSwapInterval(1);

    const GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        // spdlog::get(name)->error("GLEW Error: {}", glewGetErrorString(glew_err));
        glfwDestroyWindow(viewport_);
        ILLIXR::abort("[hand_tracking viewer] Failed to initialize GLEW");
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

    // Construct a basic perspective projection
    // math_util::projection_fov(&basicProjection, 40.0f, 40.0f, 40.0f, 40.0f, 0.03f, 20.0f);

    glfwMakeContextCurrent(nullptr);
    switchboard_->schedule<ht::ht_frame>(id_, "ht", [this](const switchboard::ptr<const ht::ht_frame>& ht_frame, std::size_t) {
        this->make_gui(ht_frame);
    });
}

viewer::~viewer() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(viewport_);

    RAC_ERRNO_MSG("debugview during destructor");

    glfwTerminate();
}

void viewer::make_position_table() const {
    ImGui::RadioButton("millimeter", &requested_unit_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("centimeter", &requested_unit_, 1);
    ImGui::SameLine();
    ImGui::RadioButton("meter", &requested_unit_, 2);
    ImGui::SameLine();
    ImGui::RadioButton("foot", &requested_unit_, 3);
    ImGui::SameLine();
    ImGui::RadioButton("inch", &requested_unit_, 4);
    if (ImGui::BeginTable("Poses", 2, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Pose");
        ImGui::TableSetupColumn("x, y, z : w, x, y, z");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Origin");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f, %.2f, %.2f : %.2f, %.2f, %.2f, %.2f", pose_.position.x(), pose_.position.y(), pose_.position.z(),
                    pose_.orientation.w(), pose_.orientation.x(), pose_.orientation.y(), pose_.orientation.z());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Current");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f, %.2f, %.2f : %.2f, %.2f, %.2f, %.2f", current_frame_->offset_pose.position.x(),
                    current_frame_->offset_pose.position.y(), current_frame_->offset_pose.position.z(),
                    current_frame_->offset_pose.orientation.w(), current_frame_->offset_pose.orientation.x(),
                    current_frame_->offset_pose.orientation.y(), current_frame_->offset_pose.orientation.z());
        ImGui::EndTable();
    }
    std::string label;
    if (ImGui::BeginTable("True Points", 2, ImGuiTableFlags_Borders)) {
        ImGui::TableNextRow();
        for (int idx = 0; idx < 2; idx++) {
            if (idx == 0) {
                label = "True Points Left Hand";
            } else {
                label = "True Points Right Hand";
            }

            ImGui::TableSetColumnIndex(idx);
            if (ImGui::BeginTable(label.c_str(), 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Point");
                ImGui::TableSetupColumn("x");
                ImGui::TableSetupColumn("y");
                ImGui::TableSetupColumn("z");
                ImGui::TableSetupColumn("Confidence");
                // ImGui::TableSetupColumn("Wx");
                // ImGui::TableSetupColumn("Wy");
                // ImGui::TableSetupColumn("Wz");
                ImGui::TableHeadersRow();
                auto points = current_frame_->hand_positions.at(static_cast<ht::hand>(idx));
                bool skip = points.points.empty();
                for (int row = ht::WRIST; row < ht::PINKY_TIP; row++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", ht::point_str_map.at(row).c_str());
                    point_with_units pnt;
                    if (!skip) {
                        pnt = points.points.at(row);
                    }
                    for (int i = 0; i < 3; i++) {
                        ImGui::TableSetColumnIndex(i + 1);
                        if (pnt.valid) {
                            ImGui::Text("%.3f", pnt[i] * units::conversion_factor[points.unit][requested_unit_]);
                            // ImGui::TableSetColumnIndex(i + 1 + 4);
                            // ImGui::Text("%.3f", thp.at(row)[i] * convert[points.unit][requested_unit_]);
                        } else {
                            ImGui::Text("");
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                                   ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                            // ImGui::TableSetColumnIndex(i + 1 + 4);
                            // ImGui::Text("");
                            // ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                            //                        ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                        }
                    }
                    ImGui::TableSetColumnIndex(4);
                    if (pnt.valid) {
                        ImGui::Text("%.2f", pnt.confidence);
                    } else {
                        ImGui::Text("");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndTable();
    }
}

void viewer::make_detection_table(units::eyes eye, int idx, const std::string& label) const {
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
                for (auto i : ht::hand_map) {
                    rect current_rect = current_frame_->detections.at(eye).palms.at(i);
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
                for (auto i : ht::hand_map) {
                    rect current_rect = current_frame_->detections.at(eye).hands.at(i);
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
                ImGui::Text("%.2f", current_frame_->detections.at(eye).confidence.at(ht::LEFT_HAND));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", current_frame_->detections.at(eye).confidence.at(ht::RIGHT_HAND));
                for (int row = ht::WRIST; row <= ht::PINKY_TIP; row++) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%s", ht::point_str_map.at(row).c_str());
                    ImGui::TableSetColumnIndex(1);
                    auto pnt = current_frame_->detections.at(eye).points.at(ht::LEFT_HAND).points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x(), pnt.y(), pnt.z());
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                    ImGui::TableSetColumnIndex(2);
                    pnt = current_frame_->detections.at(eye).points.at(ht::RIGHT_HAND).points[row];
                    if (pnt.valid) {
                        ImGui::Text("%.2f, %.2f, %.2f", pnt.x(), pnt.y(), pnt.z());
                    } else {
                        ImGui::Text("%s", "");
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
                    }
                }

                ImGui::EndTable();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Image((void*) (intptr_t) textures_[idx], ImVec2(640, 480));
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
}

void viewer::make_gui(const switchboard::ptr<const ht::ht_frame>& frame) {
    glfwMakeContextCurrent(viewport_);
    glfwPollEvents();
    current_frame_ = frame.get();

    if (base_unit_ == units::UNSET) {
        base_unit_      = current_frame_->unit;
        requested_unit_ = base_unit_;
    }
    std::vector<image::image_type> found_types;
    cv::Mat                        raw_img[2];
    int                            last_idx = 0;

    if (current_frame_->find(image::LEFT_EYE_PROCESSED) != current_frame_->images.end()) {
        found_types.push_back(image::LEFT_EYE_PROCESSED);
        raw_img[0] = current_frame_->at(image::LEFT_EYE).clone();
        last_idx++;
    }

    if (current_frame_->find(image::RIGHT_EYE_PROCESSED) != current_frame_->images.end()) {
        found_types.push_back(image::RIGHT_EYE_PROCESSED);
        raw_img[last_idx] = current_frame_->at(image::RIGHT_EYE).clone();
    }

    if (found_types.empty() && current_frame_->find(image::RGB_PROCESSED) != current_frame_->images.end()) {
        found_types.push_back(image::RGB_PROCESSED);
        raw_img[0] = current_frame_->at(image::RGB).clone();
    }

    cv::Mat processed[2];
    cv::Mat combined[2], flattened[2];
    int     d_width, d_height;
    glfwGetFramebufferSize(viewport_, &d_width, &d_height);
    glViewport(0, 0, d_width, d_height);
    glClearColor(clear_color_.x * clear_color_.w, clear_color_.y * clear_color_.w, clear_color_.z * clear_color_.w, clear_color_.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();

    ImGui_ImplGlfw_NewFrame();

    ImGui::NewFrame();
    float proc_time;
    if (current_frame_->detections.count(units::LEFT_EYE) == 1 && current_frame_->detections.count(units::RIGHT_EYE) == 1) {
        raw_img[0]     = current_frame_->at(image::LEFT_EYE).clone();
        raw_img[1]     = current_frame_->at(image::RIGHT_EYE).clone();
        enabled_right_ = true;
        tab_label_     = "Left Eye Raw";
        single_eye_    = units::LEFT_EYE;
        detections_    = {image::LEFT_EYE_PROCESSED, image::RIGHT_EYE_PROCESSED};
        proc_time      = (float) std::max(current_frame_->detections.at(static_cast<const units::eyes>(image::LEFT_EYE)).proc_time,
                                          current_frame_->detections.at(static_cast<const units::eyes>(image::RIGHT_EYE)).proc_time) /
                         (1000.f * 1000.f);
    } else {
        image::image_type img_typ;
        if (current_frame_->detections.count(units::LEFT_EYE) == 1) {
            single_eye_ = units::LEFT_EYE;
            detections_ = {image::LEFT_EYE_PROCESSED};
            img_typ     = image::LEFT_EYE;
            proc_time   = (float) current_frame_->detections.at(static_cast<const units::eyes>(image::LEFT_EYE)).proc_time /
                          (1000.f * 1000.f);
        } else if (current_frame_->detections.count(units::RIGHT_EYE) == 1) {
            single_eye_ = units::RIGHT_EYE;
            detections_ = {image::RIGHT_EYE_PROCESSED};
            img_typ     = image::RIGHT_EYE;
            proc_time   = (float) current_frame_->detections.at(static_cast<const units::eyes>(image::RIGHT_EYE)).proc_time /
                          (1000.f * 1000.f);
        } else {
            return;
        }
        raw_img[0] = current_frame_->at(img_typ).clone();
        raw_img[1].release();
        enabled_right_ = false;
        tab_label_     = "Raw";
    }

    for (size_t i = 0; i < 2; i++) {
        if (i == 1 && !enabled_right_)
            break;
        processed[i] = current_frame_->at(detections_[i]).clone();
        cv::Mat mask, inv, r;
        cv::cvtColor(processed[i], mask, cv::COLOR_RGBA2GRAY);
        cv::threshold(mask, mask, 10, 255, cv::THRESH_BINARY);
        cv::bitwise_not(mask, mask);
        cv::bitwise_and(raw_img[i], raw_img[i], r, mask);
        cv::cvtColor(processed[i], flattened[i], cv::COLOR_RGBA2RGB);
        combined[i] = flattened[i] + r;
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, combined[i].cols, combined[i].rows, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     combined[i].ptr());
    }

    if (ImGui::Begin("Hand Detections")) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) { }
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
            make_detection_table(single_eye_, 0, tab_label_);
            if (enabled_right_) {
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
    glfwSwapBuffers(viewport_);
}

PLUGIN_MAIN(viewer)
