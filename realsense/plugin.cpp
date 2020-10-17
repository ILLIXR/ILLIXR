#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API

// ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace cv;
using namespace ILLIXR;

typedef struct {
    cv::Mat* img0;
    cv::Mat* img1;
    std::size_t serial_no;
} cam_type;

class rs_camera_thread : public threadloop {
public:
	rs_camera_thread(std::string name_, phonebook *pb_, std::shared_ptr<rs2::pipeline> pipe_)
	: threadloop{name_, pb_}
	, sb{pb->lookup_impl<switchboard>()}
	, _m_cam_type{sb->publish<cam_type>("cam_type")}
	, pipe{pipe_}
	{}

protected:
	virtual skip_option _p_should_skip() override {
		return skip_option::run;
	}

	virtual void _p_one_iteration() override {
// 		std::cout << "Entered _p_one_iteration_CAM" << std::endl;
		if (!pipe) return;
		rs2::frameset frameset = pipe->wait_for_frames();
		rs2::video_frame ir_frame_left = frameset.get_infrared_frame(1);
		rs2::video_frame ir_frame_right = frameset.get_infrared_frame(2);
		cv::Mat dMat_left = cv::Mat(cv::Size(640, 480), CV_8UC1, (void*)ir_frame_left.get_data());
		cv::Mat dMat_right = cv::Mat(cv::Size(640, 480), CV_8UC1, (void*)ir_frame_right.get_data());

		_m_cam_type->put(new cam_type{
				new cv::Mat{dMat_left},
				new cv::Mat{dMat_right},
				iteration_no,
		});
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<cam_type>> _m_cam_type;

	std::shared_ptr<rs2::pipeline> pipe;
};

class rs_imu_thread : public threadloop {
public:
	rs_imu_thread(std::string name_, phonebook *pb_)
	: threadloop{name_, pb_}
	, sb{pb->lookup_impl<switchboard>()}
	, _m_imu_cam{sb->publish<imu_cam_type>("imu_cam")}
	, camera_thread_{"rs_camera_thread", pb_, pipe}
	, _m_cam_type{sb->subscribe_latest<cam_type>("cam_type")}
	{
		pipe = std::make_shared<rs2::pipeline>();
		cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F);
		cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F);
		// cfg.enable_stream(RS2_STREAM_INFRARED, 1, 640, 480, RS2_FORMAT_Y8, 30);
		// cfg.enable_stream(RS2_STREAM_INFRARED, 2, 640, 480, RS2_FORMAT_Y8, 30);
		pipe->start(cfg);
		camera_thread_.start();
	}

	virtual void start() override {
		threadloop::start();
	}

	virtual void stop() override {
		camera_thread_.stop();
		threadloop::stop();
	}

	virtual ~rs_imu_thread() override {
		pipe->stop();
	}

protected:
	virtual skip_option _p_should_skip() override {
		return skip_option::run;
	}

	virtual void _p_one_iteration() override {
		// std::cout << "Entered _p_one_iteration_IMU" << std::endl;
		rs2::frameset frames;
		if(pipe->poll_for_frames(&frames)) {
			auto fa = frames.first(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F);
			rs2::motion_frame accel = fa.as<rs2::motion_frame>();
			auto fg = frames.first(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F);
			rs2::motion_frame gyro = fg.as<rs2::motion_frame>();

			if (gyro && accel) {
				std::cout << "gyro and accel exists" << std::endl;
				// Get the timestamp of the current frame
				ts = gyro.get_timestamp();
				imu_time = static_cast<ullong>(ts / 1000000);

				// Time as time_point
        using time_point = std::chrono::system_clock::time_point;
        time_point uptime_timepoint{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(imu_time))};
        std::time_t time2 = std::chrono::system_clock::to_time_t(uptime_timepoint);
        t = std::chrono::system_clock::from_time_t(time2);

				// Get gyro & accel measures
				gyro_data = gyro.get_motion_data();
				accel_data = accel.get_motion_data();

				la = {accel_data.x, accel_data.y, accel_data.z};
				av = {gyro_data.x, gyro_data.y, gyro_data.z};
				std::cout << accel_data.x << std::endl;

				std::optional<cv::Mat*> img0 = std::nullopt;
				std::optional<cv::Mat*> img1 = std::nullopt;
				const cam_type* c = _m_cam_type->get_latest_ro();
        if (c && c->serial_no != last_serial_no) {
            last_serial_no = c->serial_no;
            img0 = c->img0;
            img1 = c->img1;
        }

				_m_imu_cam->put(new imu_cam_type {
            t,
            av,
            la,
            img0,
            img1,
            imu_time,
        });
			}
		}
	}

private:
	rs_camera_thread camera_thread_;
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<reader_latest<cam_type>> _m_cam_type;

	std::shared_ptr<rs2::pipeline> pipe;
	// rs2::pipeline pipe;
	// auto pipe;
	rs2::config cfg;
	rs2::frame frame;
	rs2_vector gyro_data;
	rs2_vector accel_data;
	double ts;

	Eigen::Vector3f la;
	Eigen::Vector3f av;

	// Timestamps
	time_type t;
	ullong imu_time;

	std::size_t last_serial_no {0};
};

PLUGIN_MAIN(rs_imu_thread);

int main(int argc, char **argv) { return 0; }
