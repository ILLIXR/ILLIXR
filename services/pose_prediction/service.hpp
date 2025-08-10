#pragma once

#include <cstdlib>                
#include <spdlog/spdlog.h>       

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/pose.hpp"
#include "illixr/data_format/pose_prediction.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"

#include <chrono>
#include <eigen3/Eigen/Dense>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>

static std::vector<float> parse_csv_floats(const std::string& s) {
    std::vector<float> out;
    std::istringstream ss(s);
    std::string field;
    while (std::getline(ss, field, ',')) {
        try {
            out.push_back(std::stof(field));
        } catch (...) { /* ignore bad entries */ }
    }
    return out;
}

namespace ILLIXR {
class pose_prediction_impl : public data_format::pose_prediction {
public:
    explicit pose_prediction_impl(const phonebook* const pb);
    data_format::fast_pose_type get_fast_pose() const override;
    data_format::pose_type      get_true_pose() const override;
    data_format::fast_pose_type get_fast_pose(time_point future_timestamp) const override;
    void                        set_offset(const Eigen::Quaternionf& raw_o_times_offset) override;
    Eigen::Quaternionf          apply_offset(const Eigen::Quaternionf& orientation) const;
    bool                        fast_pose_reliable() const override;
    bool                        true_pose_reliable() const override;
    Eigen::Quaternionf          get_offset() override;
    data_format::pose_type      correct_pose(const data_format::pose_type& pose) const override;

    data_format::fast_pose_type get_fake_render_pose() override;
    data_format::fast_pose_type get_fake_warp_pose() override;

private:
    mutable std::atomic<bool>                                        first_time_{true};
    const std::shared_ptr<switchboard>                               switchboard_;
    const std::shared_ptr<const relative_clock>                      clock_;
    switchboard::reader<data_format::pose_type>                      slow_pose_;
    switchboard::reader<data_format::imu_raw_type>                   imu_raw_;
    switchboard::reader<data_format::pose_type>                      true_pose_;
    switchboard::reader<switchboard::event_wrapper<Eigen::Vector3f>> ground_truth_offset_;
    switchboard::reader<switchboard::event_wrapper<time_point>>      vsync_estimate_;
    mutable Eigen::Quaternionf                                       offset_{Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex                                        offset_mutex_;
    const bool                                                       using_lighthouse_;

    std::vector<data_format::fast_pose_type> render_poses_;
    std::vector<data_format::fast_pose_type> warp_poses_;

    data_format::fast_pose_type fake_render_pose_{}, fake_warp_pose_{};

    inline std::string next_token(std::istringstream& ss) {
        std::string tok;
        std::getline(ss, tok, ',');
        if (tok.empty()) {
            spdlog::get("illixr")->warn("Empty token found in pose file, please check the format.");
            ILLIXR::abort("Empty token in pose file");
        }
        return tok;
    }

    void setup_fake_poses() {
        // Helper lambdas
        auto read_vec3 = [&](const char* name, Eigen::Vector3f& v) {
            auto s = switchboard_->get_env(name);
            auto vals = parse_csv_floats(s);
            if (vals.size() == 3) {
                v = Eigen::Vector3f(vals[0], vals[1], vals[2]);
            }
        };
        auto read_quat = [&](const char* name, Eigen::Quaternionf& q) {
            auto s = switchboard_->get_env(name);
            auto vals = parse_csv_floats(s);
            if (vals.size() == 4) {
                q = Eigen::Quaternionf(vals[0], vals[1], vals[2], vals[3]);
            }
        };

        // defaults (you can comment these out if you want pure-env)--------------------------------------------------------------
        // fake_render_pose_.pose.position    = Eigen::Vector3f(1.1695, 1.52429, -1.47801);
        // fake_render_pose_.pose.orientation = Eigen::Quaternionf(0.989931, 0.0466069, -0.133557, 0.00526199);

        // fake_warp_pose_.pose.position      = Eigen::Vector3f(1.16922, 1.52431, -1.47808);
        // fake_warp_pose_.pose.orientation   = Eigen::Quaternionf(0.990067, 0.0466857, -0.132518, 0.00521687);

        // override from ENV if set
        read_vec3("ILLIXR_FAKE_RENDER_POSE_POS", fake_render_pose_.pose.position);
        read_quat("ILLIXR_FAKE_RENDER_POSE_ORI", fake_render_pose_.pose.orientation);
        read_vec3("ILLIXR_FAKE_WARP_POSE_POS", fake_warp_pose_.pose.position);
        read_quat("ILLIXR_FAKE_WARP_POSE_ORI", fake_warp_pose_.pose.orientation);
    }

    void setup_pose_reader() {
        // use get_env (not get_env_char)
        std::string pose_path_ = switchboard_->get_env("ILLIXR_POSE_PATH");
        if (pose_path_.empty()) {
            spdlog::get("illixr")->error("Please set ILLIXR_POSE_PATH environment variable");
            return;
        }
        std::ifstream pose_file_(pose_path_);
        if (!pose_file_.is_open()) {
            spdlog::get("illixr")->error("Could not open pose file at {}", pose_path_);
            ILLIXR::abort("Could not open pose file");
        }
        std::string line;
        while (std::getline(pose_file_, line)) {
            std::istringstream iss(line);

            data_format::fast_pose_type render_pose{}, warp_pose{};
            (void)std::stoull(next_token(iss)); // skip 'now'

            render_pose.pose.cam_time       = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            render_pose.pose.imu_time       = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            render_pose.predict_target_time = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};

            warp_pose.pose.cam_time         = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            warp_pose.pose.imu_time         = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};
            warp_pose.predict_target_time   = time_point{std::chrono::nanoseconds(std::stoull(next_token(iss)))};

            render_pose.pose.position.x()   = std::stof(next_token(iss));
            render_pose.pose.position.y()   = std::stof(next_token(iss));
            render_pose.pose.position.z()   = std::stof(next_token(iss));

            render_pose.pose.orientation.w()= std::stof(next_token(iss));
            render_pose.pose.orientation.x()= std::stof(next_token(iss));
            render_pose.pose.orientation.y()= std::stof(next_token(iss));
            render_pose.pose.orientation.z()= std::stof(next_token(iss));

            warp_pose.pose.position.x()     = std::stof(next_token(iss));
            warp_pose.pose.position.y()     = std::stof(next_token(iss));
            warp_pose.pose.position.z()     = std::stof(next_token(iss));

            warp_pose.pose.orientation.w()  = std::stof(next_token(iss));
            warp_pose.pose.orientation.x()  = std::stof(next_token(iss));
            warp_pose.pose.orientation.y()  = std::stof(next_token(iss));
            warp_pose.pose.orientation.z()  = std::stof(next_token(iss));

            render_poses_.emplace_back(render_pose);
            warp_poses_.emplace_back(warp_pose);
        }
        spdlog::get("illixr")->info("Done reading poses");
    }
};
} // namespace ILLIXR
