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


#include <opencv/cv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <math.h>
#include <eigen3/Eigen/Dense>

#include "core/VioManager.h"
#include "state/State.h"
#include "utils/dataset_reader.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <memory>
#include <boost/filesystem.hpp>

#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/relative_clock.hpp"

using namespace ov_msckf;
using namespace ILLIXR;

duration from_seconds(double seconds) {
	return duration{long(seconds * 1e9L)};
}

class slam2 : public plugin {
public:
	/* Provide handles to slam2 */
	slam2(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_rtc{pb->lookup_impl<RelativeClock>()}
		, _m_pose{sb->get_writer<pose_type>("slow_pose")}
		, _m_imu_integrator_input{sb->get_writer<imu_integrator_input>("imu_integrator_input")}
		, _m_cam{sb->get_buffered_reader<cam_type>("cam")}
		, root_path{getenv("OPENVINS_ROOT")}
	{
		/*
        Disabling OpenCV threading is faster on x86 desktop but slower on
        jetson. Keeping this here for manual disabling.
		*/

        // cv::setNumThreads(0);

        VioManagerOptions manager_params;
		boost::filesystem::path config_path = root_path / "config" / "euroc_mav" / "estimator_config.yaml";
		auto parser = std::make_shared<ov_core::YamlParser>(config_path.string());
		manager_params.print_and_load(parser);

#ifndef NDEBUG
    	manager_params.record_timing_information = true;
#endif /// NDEBUG

        open_vins_estimator = std::make_shared<VioManager>(manager_params);

#ifdef CV_HAS_METRICS
		cv::metrics::setAccount(new std::string{"-1"});
#endif
	}

	virtual void start() override {
		plugin::start();
		sb->schedule<imu_type>(id, "imu", [&](switchboard::ptr<const imu_type> datum, std::size_t iteration_no) {
			this->feed_imu_cam(datum, iteration_no);
		});
	}

	void feed_imu_cam(switchboard::ptr<const imu_type> datum, std::size_t iteration_no) {
		// Ensures that slam doesnt start before valid IMU readings come in
		if (datum == nullptr) {
			return;
		}

		// Feed the IMU measurement. There should always be IMU data in each call to feed_imu_cam
		ov_core::ImuData imu_datum = {duration2double(datum->time.time_since_epoch()), datum->angular_v, datum->linear_a};
		open_vins_estimator->feed_measurement_imu(imu_datum);

		switchboard::ptr<const cam_type> cam;
		// Buffered Async:
		cam = _m_cam.size() == 0 ? nullptr : _m_cam.dequeue();

		// If there is not cam data this func call, break early
		if (!cam) {
			return;
		}
		if (!cam_buffer) {
			cam_buffer = cam;
			return;
		}

#ifdef CV_HAS_METRICS
		cv::metrics::setAccount(new std::string{std::to_string(iteration_no)});
		if (iteration_no % 20 == 0) {
			cv::metrics::dump();
		}
#else
#warning "No OpenCV metrics available. Please recompile OpenCV from git clone --branch 3.4.6-instrumented https://github.com/ILLIXR/opencv/. (see install_deps.sh)"
#endif

        // feed the camera images
		cv::Mat img0{cam_buffer->img0};
		cv::Mat img1{cam_buffer->img1};
		cv::Mat white_mask = cv::Mat::zeros(img0.rows, img0.cols, CV_8UC1);

        ov_core::CameraData cam_datum;
		cam_datum.timestamp = duration2double(cam_buffer->time.time_since_epoch());
		cam_datum.sensor_ids.push_back(0);
		cam_datum.sensor_ids.push_back(1);
		cam_datum.images.push_back(img0);
		cam_datum.images.push_back(img1);
		cam_datum.masks.push_back(white_mask);
		cam_datum.masks.push_back(white_mask);
		open_vins_estimator->feed_measurement_camera(cam_datum);

		// Get the pose returned from SLAM
		state = open_vins_estimator->get_state();
		Eigen::Vector4d quat = state->_imu->quat();
		Eigen::Vector3d vel = state->_imu->vel();
		Eigen::Vector3d pose = state->_imu->pos();

		Eigen::Vector3f swapped_pos = Eigen::Vector3f{float(pose(0)), float(pose(1)), float(pose(2))};
		Eigen::Quaternionf swapped_rot = Eigen::Quaternionf{float(quat(3)), float(quat(0)), float(quat(1)), float(quat(2))};
		Eigen::Quaterniond swapped_rot2 = Eigen::Quaterniond{(quat(3)), (quat(0)), (quat(1)), (quat(2))};

       	assert(isfinite(swapped_rot.w()));
        assert(isfinite(swapped_rot.x()));
        assert(isfinite(swapped_rot.y()));
        assert(isfinite(swapped_rot.z()));
        assert(isfinite(swapped_pos[0]));
        assert(isfinite(swapped_pos[1]));
        assert(isfinite(swapped_pos[2]));

		if (open_vins_estimator->initialized()) {

	                cv::Mat imgout = open_vins_estimator->get_historical_viz_image();
	                cv::imshow("Active Tracks", imgout);
	                cv::waitKey(1);

			_m_pose.put(_m_pose.allocate(
				cam_buffer->time,
				swapped_pos,
				swapped_rot
			));

			_m_imu_integrator_input.put(_m_imu_integrator_input.allocate(
				datum->time,
				from_seconds(state->_calib_dt_CAMtoIMU->value()(0)),
				imu_params{
					.gyro_noise = 0.00016968,
					.acc_noise = 0.002,
					.gyro_walk = 1.9393e-05,
					.acc_walk = 0.003,
					.n_gravity = Eigen::Matrix<double,3,1>(0.0, 0.0, -9.81),
					.imu_integration_sigma = 1.0,
					.nominal_rate = 200.0,
				},
				state->_imu->bias_a(),
				state->_imu->bias_g(),
				pose,
				vel,
				swapped_rot2
			));
		}
        cam_buffer = cam;
	}

	virtual ~slam2() override {}

private:
    const std::shared_ptr<switchboard> sb;
	std::shared_ptr<RelativeClock> _m_rtc; 
	switchboard::writer<pose_type> _m_pose;
	switchboard::writer<imu_integrator_input> _m_imu_integrator_input;

	switchboard::ptr<const cam_type> cam_buffer;
	switchboard::buffered_reader<cam_type> _m_cam;

	std::shared_ptr<State> state;
	std::shared_ptr<VioManager> open_vins_estimator;

	boost::filesystem::path root_path;
};

PLUGIN_MAIN(slam2)

