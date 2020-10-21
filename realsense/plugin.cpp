#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <mutex>

// ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

static constexpr int IMAGE_WIDTH = 640;
static constexpr int IMAGE_HEIGHT = 480;
static constexpr int FPS = 30;
static constexpr int GYRO_RATE = 400; // 200 or 400
static constexpr int ACCEL_RATE = 250; // 63 or 250

typedef struct {
	cv::Mat* img0;
	cv::Mat* img1;
	cv::Mat* rgb;
	cv::Mat* depth;
	int iteration;
} cam_type;

typedef struct {
	rs2_vector* accel_data;
	int iteration;
} accel_type;

class realsense : public plugin {
public:
	realsense(std::string name_, phonebook *pb_)
	: plugin{name_, pb_}
	, sb{pb->lookup_impl<switchboard>()}
	, _m_imu_cam{sb->publish<imu_cam_type>("imu_cam")}
	, _m_rgb_depth{sb->publish<rgb_depth_type>("rgb_depth")}
	{
		auto callback = [&](const rs2::frame& frame)
    {
				std::lock_guard<std::mutex> lock(mutex);

				if (auto fs = frame.as<rs2::frameset>()) {
					rs2::video_frame ir_frame_left = fs.get_infrared_frame(1);
					rs2::video_frame ir_frame_right = fs.get_infrared_frame(2);
					rs2::video_frame depth_frame = fs.get_depth_frame();
					rs2::video_frame rgb_frame = fs.get_color_frame();
					cv::Mat ir_left = cv::Mat(cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT), CV_8UC1, (void*)ir_frame_left.get_data());
					cv::Mat ir_right = cv::Mat(cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT), CV_8UC1, (void*)ir_frame_right.get_data());
					cv::Mat rgb = cv::Mat(cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT), CV_8UC3, (void*)rgb_frame.get_data());
					cv::Mat depth = cv::Mat(cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT), CV_16UC1, (void*)depth_frame.get_data());
					cv::Mat converted_depth;
					depth.convertTo(converted_depth, CV_32FC1, depth_scale * 1000.f);

					cam_type_.img0 = new cv::Mat{ir_left};
					cam_type_.img1 = new cv::Mat{ir_right};
					cam_type_.rgb = new cv::Mat{rgb};
					cam_type_.depth = new cv::Mat{depth};
					cam_type_.iteration = iteration_cam;
					iteration_cam++;
				}

				if (auto mf = frame.as<rs2::motion_frame>()) {
					std::string s = mf.get_profile().stream_name();

					if (s == "Accel") {
						rs2::motion_frame accel = mf;
						accel_data = accel.get_motion_data();
						accel_type_.accel_data = &accel_data;
						accel_type_.iteration = iteration_accel;
						iteration_accel++;
					}

					if (s == "Gyro") {
						if (last_iteration_accel == accel_type_.iteration) { return; }
						last_iteration_accel = accel_type_.iteration;
						rs2_vector accel = *accel_type_.accel_data;
						rs2::motion_frame gyro = mf;
						ts = gyro.get_timestamp();
						gyro_data = gyro.get_motion_data();

						// IMU data
						la = {accel.x, accel.y, accel.z};
						av = {gyro_data.x, gyro_data.y, gyro_data.z};

						// Time as ullong (nanoseconds)
						imu_time = static_cast<ullong>(ts * 1000000);

						// Time as time_point
						using time_point = std::chrono::system_clock::time_point;
						time_point uptime_timepoint{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(imu_time))};
						std::time_t time2 = std::chrono::system_clock::to_time_t(uptime_timepoint);
						t = std::chrono::system_clock::from_time_t(time2);

						// Images
						std::optional<cv::Mat*> img0 = std::nullopt;
						std::optional<cv::Mat*> img1 = std::nullopt;
						std::optional<cv::Mat*> rgb = std::nullopt;
						std::optional<cv::Mat*> depth = std::nullopt;
						if (last_iteration_cam != cam_type_.iteration) {
							last_iteration_cam = cam_type_.iteration;
							img0 = cam_type_.img0;
							img1 = cam_type_.img1;
							rgb = cam_type_.rgb;
							depth = cam_type_.depth;
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

						if (rgb && depth) {
							_m_rgb_depth->put(new rgb_depth_type{
								rgb,
								depth
							});
						}
					}
				}
    };

		cfg.disable_all_streams();
		cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F, ACCEL_RATE); // adjustable to 0, 63 (default), 250 hz
		cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F, GYRO_RATE); // adjustable set to 0, 200 (default), 400 hz
		cfg.enable_stream(RS2_STREAM_INFRARED, 1, IMAGE_WIDTH, IMAGE_HEIGHT, RS2_FORMAT_Y8, FPS);
		cfg.enable_stream(RS2_STREAM_INFRARED, 2, IMAGE_WIDTH, IMAGE_HEIGHT, RS2_FORMAT_Y8, FPS);
		cfg.enable_stream(RS2_STREAM_COLOR, IMAGE_WIDTH, IMAGE_HEIGHT, RS2_FORMAT_BGR8, FPS);
		cfg.enable_stream(RS2_STREAM_DEPTH, IMAGE_WIDTH, IMAGE_HEIGHT, RS2_FORMAT_Z16, FPS);
		profiles = pipe.start(cfg, callback);

		// Get depth scale which is used to convert the measurements into millimeters
    depth_scale = pipe.get_active_profile().get_device().first<rs2::depth_sensor>().get_depth_scale();
	}

	virtual void start() override { plugin::start(); }

	virtual void stop() override { plugin::stop(); }

	virtual ~realsense() override { pipe.stop(); }

private:
	const std::shared_ptr<switchboard> sb;
	std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;
	std::unique_ptr<writer<rgb_depth_type>> _m_rgb_depth;

	std::mutex mutex;
	rs2::pipeline_profile profiles;
	rs2::pipeline pipe;
	rs2::config cfg;
	float depth_scale;
	rs2_vector gyro_data;
	rs2_vector accel_data;
	double ts;

	Eigen::Vector3f la;
	Eigen::Vector3f av;

	// Timestamps
	time_type t;
	ullong imu_time;

	cam_type cam_type_;
	accel_type accel_type_;
	int iteration_cam = 0;
	int iteration_accel = 0;
	int last_iteration_cam;
	int last_iteration_accel;
	std::size_t last_serial_no {0};
	std::size_t last_serial_no2 {0};
};

PLUGIN_MAIN(realsense);

