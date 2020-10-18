#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <mutex>

// ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

typedef struct {
    cv::Mat* img0;
    cv::Mat* img1;
	// std::size_t serial_no;
		int iteration;
} cam_type;

typedef struct {
	rs2_vector* accel_data;
	// std::size_t serial_no;
	int iteration;
} accel_type;

class rs_imu_thread : public plugin {
public:
	rs_imu_thread(std::string name_, phonebook *pb_)
	: plugin{name_, pb_}
	, sb{pb->lookup_impl<switchboard>()}
	, _m_imu_cam{sb->publish<imu_cam_type>("imu_cam")}
	{
		pipe = std::make_shared<rs2::pipeline>();
		auto callback = [&](const rs2::frame& frame)
    {
        std::lock_guard<std::mutex> lock(mutex);

				if (auto fs = frame.as<rs2::frameset>()) {
					// std::cout << "IMAGE" << std::endl;
					rs2::video_frame ir_frame_left = fs.get_infrared_frame(1);
					rs2::video_frame ir_frame_right = fs.get_infrared_frame(2);
					cv::Mat dMat_left = cv::Mat(cv::Size(640, 480), CV_8UC1, (void*)ir_frame_left.get_data());
					cv::Mat dMat_right = cv::Mat(cv::Size(640, 480), CV_8UC1, (void*)ir_frame_right.get_data());
					cam_type_.img0 = new cv::Mat{dMat_left};
					cam_type_.img1 = new cv::Mat{dMat_right};
					cam_type_.iteration = count;
					count++;

					// cv::namedWindow("Pipeline Output");
					// cv::imshow("Pipeline Output", dMat_left);
					// cv::waitKey(1);
				}

				else if (auto fa = frame.as<rs2::motion_frame>()) {
					std::string s = fa.get_profile().stream_name();

					if (s == "Accel") {
						// std::cout << "ACCEL" << std::endl;
						rs2::motion_frame accel = fa;
						accel_data = accel.get_motion_data();
						accel_type_.accel_data = &accel_data;
						accel_type_.iteration = count2;
						count2++;
					}

					if (s == "Gyro") {
						rs2_vector *accel;
						if (lastCount2 != accel_type_.iteration) {
							lastCount2 = accel_type_.iteration;
							accel = accel_type_.accel_data;
						} else return;
						// std::cout << "GYRO" << std::endl;

						rs2::motion_frame gyro = fa;
						ts = gyro.get_timestamp();
						gyro_data = gyro.get_motion_data();

						la = {accel->x, accel->y, accel->z};
						av = {gyro_data.x, gyro_data.y, gyro_data.z};

						// Time as time_point
						imu_time = static_cast<ullong>(ts / 1000000);
						std::cout << imu_time << std::endl;

						// Time as time_point
						using time_point = std::chrono::system_clock::time_point;
						time_point uptime_timepoint{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(imu_time))};
						std::time_t time2 = std::chrono::system_clock::to_time_t(uptime_timepoint);
						t = std::chrono::system_clock::from_time_t(time2);

						// check for new frames
						std::optional<cv::Mat*> img0 = std::nullopt; // has to have type std::nullopt
						std::optional<cv::Mat*> img1 = std::nullopt;
						// cv::Mat* img0 = nullptr;
						//cv::Mat* img1 = nullptr;
						if (lastCount != cam_type_.iteration) {
							lastCount = cam_type_.iteration;
							img0 = cam_type_.img0;
							img1 = cam_type_.img1;
							std::cout << "New Image" << std::endl;
							// cv::namedWindow("Pipeline Output");
							// cv::imshow("Pipeline Output", *img0);
							// cv::waitKey(1);
						}

						// Submit to switchboard
						_m_imu_cam->put(new imu_cam_type {
								t,
								av,
								la,
								img0,
								img1,
								imu_time,
						});
						// std::cout << img0 << std::endl;
					}
				}
    };

		cfg.disable_all_streams();
		cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F, 250); // able to set to 0, 63 (default), 250
		cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F, 400); // able to set to 0, 200 (default), 400
		cfg.enable_stream(RS2_STREAM_INFRARED, 1, 640, 480, RS2_FORMAT_Y8, 30);
		cfg.enable_stream(RS2_STREAM_INFRARED, 2, 640, 480, RS2_FORMAT_Y8, 30);
		profiles = pipe->start(cfg, callback);
	}

	virtual void start() override {
		plugin::start();
	}

	virtual void stop() override {
		plugin::stop();
	}

	virtual ~rs_imu_thread() override {
		pipe->stop();
	}

protected:
	// virtual skip_option _p_should_skip() override {
	// 	return skip_option::run;
	// }

	// virtual void _p_one_iteration() override { }

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;

	std::shared_ptr<rs2::pipeline> pipe;
	rs2::config cfg;
	rs2_vector gyro_data;
	rs2_vector accel_data;
	double ts;

	Eigen::Vector3f la;
	Eigen::Vector3f av;

	// Timestamps
	time_type t;
	ullong imu_time;

	std::mutex mutex;
	rs2::pipeline_profile profiles;

	cam_type cam_type_;
	std::size_t last_serial_no {0};
	accel_type accel_type_;
	std::size_t last_serial_no2 {0};
	int count = 0;
	int count2 = 0;
	int lastCount;
	int lastCount2;
};

PLUGIN_MAIN(rs_imu_thread);

int main(int argc, char **argv) { return 0; }
