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
		cv::Mat* rgb;
		cv::Mat* depth;
    std::size_t serial_no;
} cam_type;

class rs_imu_thread : public threadloop {
public:
	rs_imu_thread(std::string name_, phonebook *pb_)
	: threadloop{name_, pb_}
	, sb{pb->lookup_impl<switchboard>()}
	, _m_imu_cam{sb->publish<imu_cam_type>("imu_cam")}
	{
		pipe = std::make_shared<rs2::pipeline>();
		cfg.disable_all_streams();
		cfg.enable_stream(RS2_STREAM_POSE, RS2_FORMAT_6DOF);
		pipe->start(cfg);
	}

	virtual void start() override {
		threadloop::start();
	}

	virtual void stop() override {
		threadloop::stop();
	}

protected:
	virtual skip_option _p_should_skip() override {
		return skip_option::run;
	}

	virtual void _p_one_iteration() override {
		auto frames = pipe->wait_for_frames();
		auto f = frames.first_or_default(RS2_STREAM_POSE);
		auto pose_data = f.as<rs2::pose_frame>().get_pose_data();

		la = {pose_data.acceleration.x, pose_data.acceleration.y, pose_data.acceleration.z};
		av = {pose_data.angular_velocity.x, pose_data.angular_velocity.y, pose_data.angular_velocity.z};
	}

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<reader_latest<cam_type>> _m_cam_type;

	std::shared_ptr<rs2::pipeline> pipe;
	rs2::config cfg;

	Eigen::Vector3f la;
	Eigen::Vector3f av;
};

PLUGIN_MAIN(rs_imu_thread);

int main(int argc, char **argv) { return 0; }
