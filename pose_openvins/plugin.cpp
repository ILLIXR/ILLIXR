/*
 * OpenVINS: An Open Platform for Visual-Inertial Research
 * Copyright (C) 2019 Patrick Geneva
 * Copyright (C) 2019 Kevin Eckenhoff
 * Copyright (C) 2019 Guoquan Huang
 * Copyright (C) 2019 OpenVINS Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/relative_clock.hpp"
#include "common/switchboard.hpp"
#include "core/VioManager.h"
#include "state/State.h"
#include "utils/dataset_reader.h"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <eigen3/Eigen/Dense>
#include <iostream>
#include <memory>
#include <opencv/cv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace ov_msckf;
using namespace ILLIXR;

duration from_seconds(double seconds) {
    return duration{long(seconds * 1e9L)};
}

class slam2 : public plugin {
public:
    /**
     * @brief Constructor of the plugin that should instantiate OpenVINS
     * @param name_ Pretty name of this plugin
     * @param pb_ Phonebook that is used to connect our plugins
     */
    slam2(std::string name_, phonebook* pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_rtc{pb->lookup_impl<RelativeClock>()}
        , _m_pose{sb->get_writer<pose_type>("slow_pose")}
        , _m_imu_integrator_input{sb->get_writer<imu_integrator_input>("imu_integrator_input")}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , ov_update_running(false)
        , root_path{getenv("OPENVINS_ROOT")}
        , sensor_name{getenv("OPENVINS_SENSOR")} {
        // Create subscriber for our IMU
        // NOTE: ILLIXR plugins can only have a single schedule / subscriber
        // NOTE: Thus here we will subscribe directly to the IMU and use the buffer_reader interface to get camera
        sb->schedule<imu_type>(id, "imu", [&](switchboard::ptr<const imu_type> datum, std::size_t iteration_no) {
            feed_imu_cam(datum, iteration_no);
        });

        // TODO: we should load the whole config from the user
        boost::filesystem::path config_path = root_path / "config" / sensor_name / "estimator_config.yaml";

        // Verbosity
        auto        parser    = std::make_shared<ov_core::YamlParser>(config_path.string());
        std::string verbosity = "INFO";
        parser->parse_config("verbosity", verbosity);
        ov_core::Printer::setPrintLevel(verbosity);

        // Load the yaml config parameters (estimator and sensors)
        VioManagerOptions manager_params;
        manager_params.print_and_load(parser);
#ifndef NDEBUG
        manager_params.record_timing_information = true;
#endif /// NDEBUG
        ov_system = std::make_shared<VioManager>(manager_params);

#ifdef CV_HAS_METRICS
        cv::metrics::setAccount(new std::string{"-1"});
#endif
    }

    /**
     * @brief Callback when we have a new IMU message
     *
     *
     * @param datum IMU data packet from the switchboard
     * @param iteration_no TODO: what is this? not sure...
     */
    void feed_imu_cam(switchboard::ptr<const imu_type> datum, std::size_t iteration_no) {
        // Ensures that slam doesn't start before valid IMU readings come in
        if (datum == nullptr)
            return;

        // Feed the IMU measurement. There should always be IMU data in each call to feed_imu_cam
        double           time_imu  = duration2double(datum->time.time_since_epoch());
        ov_core::ImuData imu_datum = {time_imu, datum->angular_v, datum->linear_a};
        ov_system->feed_measurement_imu(imu_datum);
        // PRINT_WARNING(BLUE "imu = %.15f\n" RESET, imu_datum.timestamp);

        // Get the async buffer next camera
        if (_m_cam.size() == 0)
            return;
        switchboard::ptr<const cam_type> cam = _m_cam.dequeue();

#ifdef CV_HAS_METRICS
        cv::metrics::setAccount(new std::string{std::to_string(iteration_no)});
        if (iteration_no % 20 == 0) {
            cv::metrics::dump();
        }
#else
    #warning \
        "No OpenCV metrics available. Please recompile OpenCV from git clone --branch 3.4.6-instrumented https://github.com/ILLIXR/opencv/. (see install_deps.sh)"
#endif

        // If the processing queue is currently active / running just return so we can keep getting measurements
        // Otherwise create a second thread to do our update in an async manor
        // The visualization of the state, images, and features will be synchronous with the update!
        if (ov_update_running)
            return;

        // Check if we should drop this image
        double time_cam   = duration2double(cam->time.time_since_epoch());
        double time_delta = 1.0 / ov_system->get_params().track_frequency;
        if (time_cam < camera_last_timestamp + time_delta)
            return;
        camera_last_timestamp = time_cam;

        // Create the camera data type from switchboard data
        ov_core::CameraData cam_datum;
        cam_datum.timestamp = time_cam;
        cam_datum.sensor_ids.push_back(0);
        cam_datum.sensor_ids.push_back(1);
        cam_datum.images.push_back(cam->img0.clone());
        cam_datum.images.push_back(cam->img1.clone());
        cam_datum.masks.push_back(cv::Mat::zeros(cam->img0.rows, cam->img0.cols, CV_8UC1));
        cam_datum.masks.push_back(cv::Mat::zeros(cam->img1.rows, cam->img1.cols, CV_8UC1));
        // PRINT_WARNING(BLUE "camera = %.15f (%zu in queue)\n" RESET, cam_datum.timestamp, _m_cam.size());

        // Append it to our queue of images (this is blocking...)
        {
            std::lock_guard<std::mutex> lck(camera_queue_mtx);
            camera_queue.push_back(cam_datum);
            std::sort(camera_queue.begin(), camera_queue.end());
        };

        // Lets multi thread it!
        ov_update_running = true;
        std::thread thread([&] {
            // Lock on the queue (prevents new images from appending)
            std::lock_guard<std::mutex> lck(camera_queue_mtx);

            // Loop through our queue and see if we are able to process any of our camera measurements
            // We are able to process if we have at least one IMU measurement greater than the camera time
            double timestamp_imu_inC = time_imu - ov_system->get_state()->_calib_dt_CAMtoIMU->value()(0);
            while (!camera_queue.empty() && camera_queue.at(0).timestamp < timestamp_imu_inC) {
                auto   rT0_1     = boost::posix_time::microsec_clock::local_time();
                double update_dt = 100.0 * (timestamp_imu_inC - camera_queue.at(0).timestamp);

                // Actually do our update!
                ov_system->feed_measurement_camera(camera_queue.at(0));

                // Debug testing for what is the update is very very slow...
                // usleep(0.1 * 1e6);

                // If we have initialized then we should report the current pose
                if (ov_system->initialized()) {
                    // Get the pose returned from SLAM
                    std::shared_ptr<State> ov_state = ov_system->get_state();
                    Eigen::Vector4d        quat     = ov_state->_imu->quat();
                    Eigen::Vector3d        vel      = ov_state->_imu->vel();
                    Eigen::Vector3d        pose     = ov_state->_imu->pos();
                    Eigen::Vector3d        bias_g   = ov_state->_imu->bias_g();
                    Eigen::Vector3d        bias_a   = ov_state->_imu->bias_a();

                    // OpenVINS has the rotation R_GtoIi in its state in JPL quaterion
                    // We thus switch it to hamilton quaternions by simply flipping the order of xyzw
                    // This results in the rotation R_IitoG being reported along with the p_IiinG (e.g. SE(3) pose)
                    Eigen::Vector3f    swapped_pos = Eigen::Vector3f{float(pose(0)), float(pose(1)), float(pose(2))};
                    Eigen::Quaternionf swapped_rot =
                        Eigen::Quaternionf{float(quat(3)), float(quat(0)), float(quat(1)), float(quat(2))};
                    Eigen::Quaterniond swapped_rot2 = Eigen::Quaterniond{(quat(3)), (quat(0)), (quat(1)), (quat(2))};
                    assert(isfinite(swapped_rot.w()));
                    assert(isfinite(swapped_rot.x()));
                    assert(isfinite(swapped_rot.y()));
                    assert(isfinite(swapped_rot.z()));
                    assert(isfinite(swapped_pos[0]));
                    assert(isfinite(swapped_pos[1]));
                    assert(isfinite(swapped_pos[2]));

                    cv::Mat imgout = ov_system->get_historical_viz_image();
                    cv::imshow("Active Tracks", imgout);
                    cv::waitKey(1);

                    // Push back this pose to our "slow" pose topic
                    time_point time_of_update(from_seconds(camera_queue.at(0).timestamp));
                    _m_pose.put(_m_pose.allocate(time_of_update, swapped_pos, swapped_rot));

                    // Also send this information with biases and velocity to the fast IMU integrator
                    // TODO: these IMU noises should be coming from the configuration yaml of OpenVINS...
                    imu_params params = {
                        .gyro_noise            = 0.00016968,
                        .acc_noise             = 0.002,
                        .gyro_walk             = 1.9393e-05,
                        .acc_walk              = 0.003,
                        .n_gravity             = Eigen::Matrix<double, 3, 1>(0.0, 0.0, -9.81),
                        .imu_integration_sigma = 1.0,
                        .nominal_rate          = 200.0,
                    };
                    auto dt = from_seconds(ov_state->_calib_dt_CAMtoIMU->value()(0));
                    _m_imu_integrator_input.put(
                        _m_imu_integrator_input.allocate(datum->time, dt, params, bias_a, bias_g, pose, vel, swapped_rot2));
                }

                // We are done with this image!
                camera_queue.pop_front();
                auto   rT0_2      = boost::posix_time::microsec_clock::local_time();
                double time_total = (rT0_2 - rT0_1).total_microseconds() * 1e-6;
                PRINT_INFO(BLUE "[TIME]: %.4f seconds total (%.1f hz, %.2f ms behind)\n" RESET, time_total, 1.0 / time_total,
                           update_dt);
            }

            // Mark that we have finished our async processing of the image queue
            ov_update_running = false;
        });

        // If we are single threaded, then run single threaded
        // Otherwise detach this thread so it runs in the background!
        thread.join();
        // thread.detach();
    }

private:
    // ILLIXR glue and publishers
    const std::shared_ptr<switchboard>        sb;
    std::shared_ptr<RelativeClock>            _m_rtc;
    switchboard::writer<pose_type>            _m_pose;
    switchboard::writer<imu_integrator_input> _m_imu_integrator_input;

    // Subscriber / sensor information
    // switchboard::ptr<const cam_type>       cam_buffer;
    switchboard::buffered_reader<cam_type> _m_cam;

    // OpenVINS related
    std::shared_ptr<VioManager> ov_system;
    std::atomic<bool>           ov_update_running;

    // Queue up camera measurements sorted by time and trigger once we have
    // exactly one IMU measurement with timestamp newer than the camera measurement
    // This also handles out-of-order camera measurements, which is rare, but
    // a nice feature to have for general robustness to bad camera drivers.
    std::deque<ov_core::CameraData> camera_queue;
    std::mutex                      camera_queue_mtx;
    double                          camera_last_timestamp = -1.0;

    // How to get the configration information
    boost::filesystem::path root_path;
    std::string             sensor_name;
};

PLUGIN_MAIN(slam2)
