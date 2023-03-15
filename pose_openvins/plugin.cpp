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
#include "state/Propagator.h"
#include "state/State.h"
#include "utils/quat_ops.h"

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
#if ENABLE_OPENVINS_PREDICT
        , _m_pose_fast{sb->get_writer<imu_raw_type>("imu_raw")}
#endif
        , _m_imu_integrator_input{sb->get_writer<imu_integrator_input>("imu_integrator_input")}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , _m_cam_pub{sb->get_writer<cam_type>("vins")}
        , ov_update_running(false)
        , new_state_valid(false)
        , root_path{getenv("OPENVINS_ROOT")}
        , sensor_name{getenv("OPENVINS_SENSOR")} {
        // Create subscriber for our IMU
        // NOTE: ILLIXR plugins can only have a single schedule / subscriber
        // NOTE: Thus here we will subscribe directly to the IMU and use the buffer_reader interface to get camera
        sb->schedule<imu_type>(id, "imu", [&](switchboard::ptr<const imu_type> datum, std::size_t iteration_no) {
            feed_imu_cam(datum, iteration_no);
        });

        // Load our parser and the user's verbosity
        boost::filesystem::path config_path = root_path / "config" / sensor_name / "estimator_config.yaml";
        auto                    parser      = std::make_shared<ov_core::YamlParser>(config_path.string());
        std::string             verbosity   = "INFO";
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
     * The IMU should always append to OpenVINS feed function and not be blocked by update.
     * Additionally, we publish a "fast pose" on the "imu_raw" topic for the headset.
     * If we are not processing any images currently we will create an async thread and call update.
     * After update we publish the updated pose along with the visualization image.
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
        PRINT_ALL(BLUE "imu = %.8f (%.1f HZ)\n" RESET, imu_datum.timestamp, 1.0 / (time_imu - last_imu_time));
        last_imu_time = time_imu;

#if ENABLE_OPENVINS_PREDICT
        // Predict the state to this time (fast..)!!
        if (ov_system->initialized()) {
            std::shared_ptr<State>        state      = ov_system->get_state();
            Eigen::Matrix<double, 13, 1>  state_plus = Eigen::Matrix<double, 13, 1>::Zero();
            Eigen::Matrix<double, 12, 12> cov_plus   = Eigen::Matrix<double, 12, 12>::Zero();
            if (ov_system->get_propagator()->fast_state_propagate(state, time_imu, state_plus, cov_plus)) {
                // Velocity from fast predict are in the body frame
                // This re-rotate it into the global frame of reference v_IiinG
                Eigen::Matrix<double, 4, 1> curr_quat = state_plus.block(0, 0, 4, 1);
                Eigen::Matrix<double, 3, 1> curr_pos  = state_plus.block(4, 0, 3, 1);
                Eigen::Matrix<double, 3, 1> curr_vel =
                    ov_core::quat_2_Rot(curr_quat).transpose() * state_plus.block(7, 0, 3, 1);

                // TODO: can we get a better second IMU by storing? is this important?
                Eigen::Matrix<double, 3, 1> w_hat  = imu_datum.wm;
                Eigen::Matrix<double, 3, 1> a_hat  = imu_datum.am;
                Eigen::Matrix<double, 3, 1> w_hat2 = imu_datum.wm;
                Eigen::Matrix<double, 3, 1> a_hat2 = imu_datum.am;

                // Publish
                _m_pose_fast.put(_m_pose_fast.allocate(
                    w_hat, a_hat, w_hat2, a_hat2, curr_pos, curr_vel,
                    Eigen::Quaterniond{curr_quat(3), curr_quat(0), curr_quat(1), curr_quat(2)}, datum->time));
                // PRINT_ALL(BLUE "fast pub = %.8f\n" RESET, time_imu);
            }
        }
#endif

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

        // If we have initialized then we should report the current pose
        // NOTE: This needs to be in our main thread as it seems the switchboard has mutex problems
        if (ov_system->initialized() && new_state_valid) {
            // Get the pose returned from SLAM's last update
            std::lock_guard<std::mutex> lck(new_state_mtx);
            time_point                  time_of_update(from_seconds(new_state_time));
            Eigen::Vector4d             quat   = new_state.block(0, 0, 4, 1);
            Eigen::Vector3d             pose   = new_state.block(4, 0, 3, 1);
            Eigen::Vector3d             vel    = new_state.block(7, 0, 3, 1);
            Eigen::Vector3d             bias_g = new_state.block(10, 0, 3, 1);
            Eigen::Vector3d             bias_a = new_state.block(13, 0, 3, 1);

            // OpenVINS has the rotation R_GtoIi in its state in JPL quaterion
            // We thus switch it to hamilton quaternions by simply flipping the order of xyzw
            // This results in the rotation R_IitoG being reported along with the p_IiinG (e.g. SE(3) pose)
            Eigen::Vector3f    swapped_pos = Eigen::Vector3f{float(pose(0)), float(pose(1)), float(pose(2))};
            Eigen::Quaternionf swapped_rot = Eigen::Quaternionf{float(quat(3)), float(quat(0)), float(quat(1)), float(quat(2))};
            Eigen::Quaterniond swapped_rot2 = Eigen::Quaterniond{(quat(3)), (quat(0)), (quat(1)), (quat(2))};
            assert(isfinite(swapped_rot.w()));
            assert(isfinite(swapped_rot.x()));
            assert(isfinite(swapped_rot.y()));
            assert(isfinite(swapped_rot.z()));
            assert(isfinite(swapped_pos[0]));
            assert(isfinite(swapped_pos[1]));
            assert(isfinite(swapped_pos[2]));

            // Push back this pose to our "slow" pose topic
            _m_pose.put(_m_pose.allocate(time_of_update, swapped_pos, swapped_rot));

            // Also send this information with biases and velocity to the fast IMU integrator
            imu_params params = {
                .gyro_noise            = ov_system->get_params().imu_noises.sigma_w,
                .acc_noise             = ov_system->get_params().imu_noises.sigma_a,
                .gyro_walk             = ov_system->get_params().imu_noises.sigma_wb,
                .acc_walk              = ov_system->get_params().imu_noises.sigma_ab,
                .n_gravity             = Eigen::Matrix<double, 3, 1>(0.0, 0.0, -ov_system->get_params().gravity_mag),
                .imu_integration_sigma = 1.0,
                .nominal_rate          = 200.0,
            };
            _m_imu_integrator_input.put(_m_imu_integrator_input.allocate(time_of_update, from_seconds(new_state_camdt), params,
                                                                         bias_a, bias_g, pose, vel, swapped_rot2));
            new_state_valid = false;
        }

        // Check if we should drop this image
        double time_cam   = duration2double(cam->time.time_since_epoch());
        double time_delta = 1.0 / ov_system->get_params().track_frequency;
        if (time_cam < ov_update_last_timestamp + time_delta)
            return;
        ov_update_last_timestamp = time_cam;

        // Create the camera data type from switchboard data
        ov_core::CameraData cam_datum;
        cam_datum.timestamp = time_cam;
        cam_datum.sensor_ids.push_back(0);
        cam_datum.images.push_back(cam->img0.clone());
        if (ov_system->get_params().use_mask) {
            assert(ov_system->get_params().masks.at(0).rows == cam->img0.rows);
            assert(ov_system->get_params().masks.at(0).cols == cam->img0.cols);
            cam_datum.masks.push_back(ov_system->get_params().masks.at(0));
        } else {
            cam_datum.masks.push_back(cv::Mat::zeros(cam->img0.rows, cam->img0.cols, CV_8UC1));
        }

        // If we have a stereo image we should append it too...
        if (!cam->img1.empty() && ov_system->get_params().state_options.num_cameras > 1) {
            cam_datum.sensor_ids.push_back(1);
            cam_datum.images.push_back(cam->img1.clone());
            if (ov_system->get_params().use_mask) {
                assert(ov_system->get_params().masks.at(1).rows == cam->img1.rows);
                assert(ov_system->get_params().masks.at(1).cols == cam->img1.cols);
                cam_datum.masks.push_back(ov_system->get_params().masks.at(1));
            } else {
                cam_datum.masks.push_back(cv::Mat::zeros(cam->img1.rows, cam->img1.cols, CV_8UC1));
            }
        }
        ov_update_camera_queue.push_back(cam_datum);
        std::sort(ov_update_camera_queue.begin(), ov_update_camera_queue.end());
        PRINT_ALL(BLUE "camera = %.8f (%zu in queue)\n" RESET, cam_datum.timestamp, _m_cam.size());

        // Lets multi-thread it!
        ov_update_running = true;
        std::thread thread([&] {
            // Loop through our queue and see if we are able to process any of our camera measurements
            // We are able to process if we have at least one IMU measurement greater than the camera time
            double timestamp_imu_inC = time_imu - ov_system->get_state()->_calib_dt_CAMtoIMU->value()(0);
            while (!ov_update_camera_queue.empty() && ov_update_camera_queue.at(0).timestamp < timestamp_imu_inC) {
                auto   cam_datum_new = ov_update_camera_queue.at(0);
                auto   rT0_1         = boost::posix_time::microsec_clock::local_time();
                double update_dt     = 100.0 * (timestamp_imu_inC - cam_datum_new.timestamp);

                // Actually do our update!
                ov_system->feed_measurement_camera(cam_datum_new);
                PRINT_ALL(BLUE "update = %.8f (%zu in queue)\n" RESET, cam_datum_new.timestamp, ov_update_camera_queue.size());

                // Save the state if we have initialized the system
                std::lock_guard<std::mutex> lck(new_state_mtx);
                if (ov_system->initialized()) {
                    new_state       = ov_system->get_state()->_imu->value();
                    new_state_time  = ov_system->get_state()->_timestamp;
                    new_state_camdt = ov_system->get_state()->_calib_dt_CAMtoIMU->value()(0);
                    new_state_valid = true;
                }

                // Debug testing for what is the update is very very slow...
                // usleep(0.1 * 1e6);

                // Debug display of tracks
                // TODO: can this be clean up to look nicer?
                cv::Mat imgout = ov_system->get_historical_viz_image();
                if (!imgout.empty()) {
                    auto              rT0_2          = boost::posix_time::microsec_clock::local_time();
                    double            viz_track_rate = 1.0 / ((rT0_2 - rT0_1).total_microseconds() * 1e-6);
                    std::stringstream stream;
                    stream << std::fixed << std::setprecision(1) << viz_track_rate;
                    std::string rate = stream.str() + "hz";
                    cv::putText(imgout, rate, cv::Point(60, imgout.rows - 60), cv::FONT_HERSHEY_COMPLEX_SMALL, 1.5,
                                cv::Scalar(0, 255, 0), 2);
                    _m_cam_pub.put(
                        _m_cam_pub.allocate<cam_type>({time_point(from_seconds(cam_datum_new.timestamp)), imgout, cv::Mat()}));
                }

                // We are done with this image!
                ov_update_camera_queue.pop_front();
                auto   rT0_2      = boost::posix_time::microsec_clock::local_time();
                double time_total = (rT0_2 - rT0_1).total_microseconds() * 1e-6;
                PRINT_INFO(BLUE "[TIME]: %.4f seconds total (%.1f hz, %.2f ms behind)\n" RESET, time_total, 1.0 / time_total,
                           update_dt);
            }

            // Mark that we have finished our async processing of the image queue
            ov_update_running = false;
        });

        // We detach this thread so it runs in the background!
        // It is important that we do not lose any IMU messages...
        thread.detach();
    }

private:
    // ILLIXR glue and publishers
    const std::shared_ptr<switchboard> sb;
    std::shared_ptr<RelativeClock>     _m_rtc;
    switchboard::writer<pose_type>     _m_pose;
#if ENABLE_OPENVINS_PREDICT
    switchboard::writer<imu_raw_type> _m_pose_fast;
#endif
    switchboard::writer<imu_integrator_input> _m_imu_integrator_input;

    // Camera subscriber and tracking image publisher
    switchboard::buffered_reader<cam_type> _m_cam;
    switchboard::writer<cam_type>          _m_cam_pub;

    // OpenVINS related
    std::shared_ptr<VioManager> ov_system;
    std::atomic<bool>           ov_update_running;

    // Queue up camera measurements sorted by time and trigger once we have
    // exactly one IMU measurement with timestamp newer than the camera measurement
    // This also handles out-of-order camera measurements, which is rare, but
    // a nice feature to have for general robustness to bad camera drivers.
    double                          ov_update_last_timestamp = -1.0;
    std::deque<ov_core::CameraData> ov_update_camera_queue;
    double                          last_imu_time = -1.0;

    // If we have a new state this will be stored here and published by the next IMU
    // We need to do this as switchboard seems to not like async threads..
    std::atomic<bool> new_state_valid;
    std::mutex        new_state_mtx;
    Eigen::VectorXd   new_state;
    double            new_state_time;
    double            new_state_camdt;

    // How to get the configuration information
    boost::filesystem::path root_path;
    std::string             sensor_name;
};

PLUGIN_MAIN(slam2)
