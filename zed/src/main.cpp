// ZED includes
#include <sl/Camera.hpp>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <zed_opencv.hpp>

//ILLIXR includes
#include "common/threadloop.hpp"
#include "common/realtime_clock.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

cv::Mat slMat2cvMat(Mat& input);

std::shared_ptr<Camera> start_camera() {
    std::shared_ptr<Camera> zedm = std::make_shared<Camera>();

    // Cam setup
    InitParameters init_params;
    init_params.camera_resolution = RESOLUTION::VGA;
    init_params.coordinate_units = UNIT::MILLIMETER; // for kf
    init_params.coordinate_system = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
    init_params.camera_fps = 15;
    init_params.depth_mode = DEPTH_MODE::PERFORMANCE;
    init_params.depth_stabilization = true;
    // init_params.depth_minimum_distance = 0.1;
    // Open the camera
    ERROR_CODE err = zedm->open(init_params);
    if (err != ERROR_CODE::SUCCESS) {
        printf("%s\n", toString(err).c_str());
        zedm->close();
		abort();
    }

    // This is 4% of camera frame time, not 4 ms
    zedm->setCameraSettings(VIDEO_SETTINGS::EXPOSURE, 4);

    return zedm;
}

class zed_camera_thread : public threadloop {
public:
    zed_camera_thread(std::string name_, phonebook* pb_, std::shared_ptr<Camera> zedm_)
	: threadloop{name_, pb_, false}
    , sb{pb->lookup_impl<switchboard>()}
    , _m_cam_type{sb->get_writer<cam_type>("cam")}
    , zedm{zedm_}
    , image_size{zedm->getCameraInformation().camera_configuration.resolution}
    {
        runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
        // Image setup
        imageL_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
        imageR_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
        rgb_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C4, MEM::CPU);
        depth_zed.alloc(image_size.width, image_size.height, MAT_TYPE::F32_C1, MEM::CPU);

        imageL_ocv = slMat2cvMat(imageL_zed);
        imageR_ocv = slMat2cvMat(imageR_zed);
        rgb_ocv = slMat2cvMat(rgb_zed);
        depth_ocv = slMat2cvMat(depth_zed);
    }

private:
    const std::shared_ptr<switchboard> sb;
	switchboard::writer<cam_type> _m_cam_type;
    std::shared_ptr<Camera> zedm;
    Resolution image_size;
    RuntimeParameters runtime_parameters;
    std::size_t serial_no {0};

    Mat imageL_zed;
    Mat imageR_zed;
    Mat depth_zed;
    Mat rgb_zed;

    cv::Mat imageL_ocv;
    cv::Mat imageR_ocv;
    cv::Mat depth_ocv;
    cv::Mat rgb_ocv;

protected:
    virtual skip_option _p_should_skip() override {
        if (zedm->grab(runtime_parameters) == ERROR_CODE::SUCCESS) {
            return skip_option::run;
        } else {
            return skip_option::skip_and_spin;
        }
    }

    virtual void _p_one_iteration() override {
        // Retrieve images
        zedm->retrieveImage(imageL_zed, VIEW::LEFT_GRAY, MEM::CPU, image_size);
        zedm->retrieveImage(imageR_zed, VIEW::RIGHT_GRAY, MEM::CPU, image_size);
        zedm->retrieveMeasure(depth_zed, MEASURE::DEPTH, MEM::CPU, image_size);
        zedm->retrieveImage(rgb_zed, VIEW::LEFT, MEM::CPU, image_size);

		ullong time = zedm->getTimestamp(sl::TIME_REFERENCE::IMAGE);

        _m_cam_type.put(new (_m_cam_type.allocate()) cam_type{
            // Make a copy, so that we don't have race
			time_point{std::chrono::nanoseconds{time}},
            cv::Mat{imageL_ocv},
            cv::Mat{imageR_ocv},
            time,
        });
    }
};

class zed_imu_thread : public threadloop {
public:

    zed_imu_thread(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_, false}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu{sb->get_writer<imu_type>("imu")}
		, _m_rtc{pb->lookup_impl<realtime_clock>()}
        , zedm{start_camera()}
        , camera_thread_{"zed_camera_thread", pb_, zedm}
        , it_log{record_logger_}
    { }

	virtual void start() override {
        camera_thread_.start();
		threadloop::start();
	}

	virtual void start2() override {
        camera_thread_.start2();
		threadloop::start2();
	}

	virtual void stop() override {
        camera_thread_.stop();
		threadloop::stop();
	}

    // destructor
    virtual ~zed_imu_thread() override {
        zedm->close();
    }

protected:
    virtual skip_option _p_should_skip() override {
        zedm->getSensorsData(sensors_data, TIME_REFERENCE::CURRENT);
        if (sensors_data.imu.timestamp > last_imu_ts) {
            return skip_option::run;
        } else {
            return skip_option::skip_and_yield;
        }
    }

    virtual void _p_one_iteration() override {
		if (iteration_no % 100 == 1) {
			// std::cout << "IMU Rate: " << iteration_no << " " << iteration_no / max(1, std::chrono::duration_cast<std::chrono::seconds>(_m_rtc->time_since_start()).count()) << "\n" << std::endl;
		}

        // Time as ullong (nanoseconds)
        imu_time = static_cast<ullong>(sensors_data.imu.timestamp.getNanoseconds());

        // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
        la = {sensors_data.imu.linear_acceleration_uncalibrated.x , sensors_data.imu.linear_acceleration_uncalibrated.y, sensors_data.imu.linear_acceleration_uncalibrated.z };
        av = {sensors_data.imu.angular_velocity_uncalibrated.x  * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.y * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.z * (M_PI/180)};

        _m_imu.put(new (_m_imu.allocate()) imu_type {
			time_point{std::chrono::nanoseconds{imu_time}},
            av,
            la,
            imu_time,
        });

        last_imu_ts = sensors_data.imu.timestamp;
		std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

private:
    std::shared_ptr<Camera> zedm;
    zed_camera_thread camera_thread_;

    const std::shared_ptr<switchboard> sb;
	switchboard::writer<imu_type> _m_imu;
	const std::shared_ptr<realtime_clock> _m_rtc;

    // IMU
    SensorsData sensors_data;
    Timestamp last_imu_ts = 0;
    Eigen::Vector3f la;
    Eigen::Vector3f av;

    // Timestamps
    ullong imu_time;

    std::size_t last_serial_no {0};

    // Logger
    record_coalescer it_log;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread);

int main(int argc, char **argv) { return 0; }
