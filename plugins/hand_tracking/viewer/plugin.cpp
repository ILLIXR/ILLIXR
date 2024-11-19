#include "plugin.hpp"

//#include "illixr/gl_util/obj.hpp"
#include "illixr/opencv_data_types.hpp"
//#include "illixr/shader_util.hpp"
//#include "imgui/backends/imgui_impl_glfw.h"
//#include "imgui/backends/imgui_impl_opengl3.h"
#include "include/data_format.hpp"

#include <opencv2/opencv.hpp>

using namespace ILLIXR;
using namespace io;
/**
* @brief Callback function to handle glfw errors
*/

std::string image_type_string(const image::image_type it) {
    switch(it) {
    case image::LEFT:
    case image::LEFT_PROCESSED:
        return "left";
    case image::RIGHT:
    case image::RIGHT_PROCESSED:
        return "right";
    case image::RGB:
    case image::RGB_PROCESSED:
        return "rgb";
    case image::DEPTH:
    case image::DEPTH_PROCESSED:
        return "depth";
    }
}

viewer::viewer(const std::string& name_, phonebook* pb_) :
    plugin{name_, pb_}, _clock{pb->lookup_impl<RelativeClock>()}
    , _switchboard{pb_->lookup_impl<switchboard>()}
    , current_frame(nullptr) {
    const char* input = std::getenv("HT_INPUT");
    if (input) {
        if (strcmp(input, "webcam") == 0) {
            _zed = true;
        } else if (strcmp(input, "cam") == 0) {

        } else if (strcmp(input, "zed") == 0) {
            _zed = true;
        }
    } else {

    }
}

void viewer::start() {
    plugin::start();
    std::string sub_path = "fps30_dur3_CCF";
    files* fls = files::getInstance(sub_path);
    ht_out.open(files::ht_file, std::ofstream::out);
    //threadloop::start();
    _switchboard->schedule<HandTracking::ht_frame>(id, "ht", [this](const switchboard::ptr<const HandTracking::ht_frame>& ht_frame, std::size_t) {
        this->make_gui(ht_frame);
    });
}

viewer::~viewer() {
}

void viewer::make_detection_table(const ht_detection& det, const image::image_type it) {
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
    std::string title = image_type_string(it);

    std::string palm_title = title + "_palms";
    if (ImGui::BeginTable(palm_title.c_str(), 6, flags)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Unit");
        ImGui::TableSetupColumn("Center");
        ImGui::TableSetupColumn("Width");
        ImGui::TableSetupColumn("Height");
        ImGui::TableSetupColumn("Rotation");
        ImGui::TableHeadersRow();
        rect current_rect;
        for (auto i : hand_map) {
            current_rect = det.palms.at(i);
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
    std::string hand_table_title = title + "_hands";
    if (ImGui::BeginTable(hand_table_title.c_str(), 6, flags)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Unit");
        ImGui::TableSetupColumn("Center");
        ImGui::TableSetupColumn("Width");
        ImGui::TableSetupColumn("Height");
        ImGui::TableSetupColumn("Rotation");
        ImGui::TableHeadersRow();
        rect current_rect;
        for (auto i : hand_map) {
            current_rect = det.hands.at(i);
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
    std::string hand_results_title = title + "_results";
    if (ImGui::BeginTable(hand_results_title.c_str(), 3, flags)) {
        ImGui::TableSetupColumn("Point");
        ImGui::TableSetupColumn("Left");
        ImGui::TableSetupColumn("Right");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", "Confidence");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f", det.confidence.at(LEFT_HAND));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.2f", det.confidence.at(RIGHT_HAND));
        for (int row = WRIST; row <= PINKY_TIP; row++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", point_map.at(row).c_str());
            ImGui::TableSetColumnIndex(1);
            auto pnt = det.points.at(LEFT_HAND)[row];
            if (pnt.valid) {
                ImGui::Text("%.2f, %.2f, %.2f", pnt.x, pnt.y, pnt.z);
            } else {
                ImGui::Text("%s", "");
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
            }
            ImGui::TableSetColumnIndex(2);
            pnt = det.points.at(RIGHT_HAND)[row];
            if (pnt.valid) {
                ImGui::Text("%.2f, %.2f, %.2f", pnt.x, pnt.y, pnt.z);
            } else {
                ImGui::Text("%s", "");
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.7f, 0.3f, 0.3f, 0.65f)));
            }
        }

        ImGui::EndTable();
    }
}

void viewer::make_gui(const switchboard::ptr<const ht_frame>& frame) {
    glfwMakeContextCurrent(_viewport);
    glfwPollEvents();
void viewer::make_gui(const switchboard::ptr<const HandTracking::ht_frame>& frame) {
    std::vector<image::image_type> found_types;
    std::cout << "      Writing frame " << frame->time.time_since_epoch().count() << std::endl;
    cv::Mat raw_img[2];
    int last_idx = 0;

    current_frame = frame.get();
    ht_out << *current_frame << std::endl;
/*
    if (frame->find(image::LEFT_PROCESSED) != frame->images.end()) {
        found_types.push_back(image::LEFT_PROCESSED);
        raw_img[0] = frame->at(image::LEFT).clone();
        last_idx++;

    }
    if (frame->find(image::RIGHT_PROCESSED) != frame->images.end()) {
        found_types.push_back(image::RIGHT_PROCESSED);
        raw_img[last_idx] = frame->at(image::RIGHT).clone();


    }
    if (found_types.empty() && frame->find(image::RGB_PROCESSED) != frame->images.end()) {
        found_types.push_back(image::RGB_PROCESSED);
        raw_img[0] = frame->at(image::RGB).clone();
        //if (_wc)
        //    cv::flip(raw_img[0], raw_img[0], 1);
    }



    /*
    cv::Mat processed[2];
    cv::Mat combined[2], flattened[2];




    for(size_t i = 0; i < found_types.size(); i++) {
        // get raw frame from camera input
        glBindTexture(GL_TEXTURE_2D, textures[i * 3]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, raw_img[i].cols, raw_img[i].rows, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, raw_img[i].ptr());
        processed[i] = frame->at(found_types[i]).clone();
        cv::Mat mask, inv, r;
        cv::cvtColor(processed[i], mask, cv::COLOR_RGBA2GRAY);
        cv::threshold(mask, mask, 10, 255, cv::THRESH_BINARY);
        cv::bitwise_not(mask, mask);
        cv::bitwise_and(raw_img[i], raw_img[i], r, mask);
        //cv::imwrite("test1.png", r);
        cv::cvtColor(processed[i], flattened[i], cv::COLOR_RGBA2RGB);
        glBindTexture(GL_TEXTURE_2D, textures[(i * 3) + 1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8,  flattened[i].cols, flattened[i].rows, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, flattened[i].ptr());

        combined[i] = flattened[i] + r;
        glBindTexture(GL_TEXTURE_2D, textures[(i * 3) + 2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, combined[i].cols, combined[i].rows, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, combined[i].ptr());

        ImGui::SetNextWindowPos(ImVec2(i * ImGui::GetIO().DisplaySize.x / 2.,
                                       ImGui::GetIO().DisplaySize.y),
                                ImGuiCond_Once, ImVec2(0.f, 1.f));
        {
            std::string title = image_type_string(found_types[i]);
            ImGui::Begin((title  + " Camera Hand Detections").c_str());
            if (ImGui::BeginTable((title + "_time").c_str(), 2, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn((title + "d_label").c_str());
                ImGui::TableSetupColumn((title + "d_time").c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", "Processing time (ms)");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%ld", frame->detections.at(found_types[i]).proc_time / 1000000);
                ImGui::EndTable();
            }
            if (ImGui::BeginTable((title + "_results_table").c_str(), 2, ImGuiTableFlags_Borders)) {
                ImGui::TableSetupColumn((title + "_det").c_str());
                ImGui::TableSetupColumn((title + "_img").c_str());
                ImGui::TableNextRow();
                int det_col = 0;
                int im_col = 1;
                if (i == 1) {
                    det_col = 1;
                    im_col = 0;
                }
                ImGui::TableSetColumnIndex(det_col);
                make_detection_table(frame->detections.at(found_types[i]), found_types[i]);
                ImGui::TableSetColumnIndex(im_col);
                ImGui::Image((void*) (intptr_t) textures[i * 3], ImVec2(640, 480));
                ImGui::Image((void*) (intptr_t) textures[(i * 3) + 1], ImVec2(640, 480));
                ImGui::Image((void*) (intptr_t) textures[(i * 3) + 2], ImVec2(640, 480));
                ImGui::EndTable();
            }
            ImGui::End();
        }

    }

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(_viewport);

    count++;*/
}

PLUGIN_MAIN(viewer)
