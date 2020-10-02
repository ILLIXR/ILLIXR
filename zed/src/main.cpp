// ZED includes
#include <sl/Camera.hpp>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <zed_opencv.hpp>
#include <cerrno>
#include <cassert>

//ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/error_util.hpp"

using namespace ILLIXR;

cv::Mat slMat2cvMat(Mat& input);

const record_header __imu_cam_record {"imu_cam", {
    {"iteration_no", typeid(std::size_t)},
    {"has_camera", typeid(bool)},
}};

struct cam_type : switchboard::event {
    cv::Mat img0;
    cv::Mat img1;
    std::size_t serial_no;
};

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
    }

    // This is 4% of camera frame time, not 4 ms
    zedm->setCameraSettings(VIDEO_SETTINGS::EXPOSURE, 4);

    return zedm;
}

class zed_camera_thread : public threadloop {
public:
    zed_camera_thread(std::string name_, phonebook* pb_, std::shared_ptr<Camera> zedm_)
    : threadloop{name_, pb_}
    , sb{pb->lookup_impl<switchboard>()}
    , _m_cam_type{sb->get_writer<cam_type>("cam_type")}
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
        assert(errno == 0 && "Errno should not be set at start of _p_one_iteration");

        // Retrieve images
        zedm->retrieveImage(imageL_zed, VIEW::LEFT_GRAY, MEM::CPU, image_size);
        zedm->retrieveImage(imageR_zed, VIEW::RIGHT_GRAY, MEM::CPU, image_size);
        zedm->retrieveMeasure(depth_zed, MEASURE::DEPTH, MEM::CPU, image_size);
        zedm->retrieveImage(rgb_zed, VIEW::LEFT, MEM::CPU, image_size);

        auto start_cpu_time  = thread_cpu_time();
        auto start_wall_time = std::chrono::high_resolution_clock::now();

        _m_cam_type.put(new (_m_cam_type.allocate()) cam_type{
            // Make a copy, so that we don't have race
            cv::Mat{imageL_ocv},
            cv::Mat{imageR_ocv},
            iteration_no,
        });

        RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
    }
};

class zed_imu_thread : public threadloop {
public:

    virtual void stop() override {
        camera_thread_.stop();
        threadloop::stop();
    }

    zed_imu_thread(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_cam{sb->get_writer<imu_cam_type>("imu_cam")}
        , _m_cam_type{sb->get_reader<cam_type>("cam_type")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
        , _m_imu_integrator{sb->get_writer<imu_integrator_seq>("imu_integrator_seq")}
        , zedm{start_camera()}
        , camera_thread_{"zed_camera_thread", pb_, zedm}
        , it_log{record_logger_}
    {
        camera_thread_.start();
    }

    // destructor
    virtual ~zed_imu_thread() override {
        zedm->close();
    }

protected:
    virtual skip_option _p_should_skip() override {
        zedm->getSensorsData(sensors_data, TIME_REFERENCE::CURRENT);
        if (sensors_data.imu.timestamp > last_imu_ts) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            return skip_option::run;
        } else {
            return skip_option::skip_and_yield;
        }
    }

    virtual void _p_one_iteration() override {
        assert(errno == 0 && "Errno should not be set at start of _p_one_iteration");

        // std::cout << "IMU Rate: " << sensors_data.imu.effective_rate << "\n" << std::endl;

        // Time as ullong (nanoseconds)
        imu_time = static_cast<ullong>(sensors_data.imu.timestamp.getNanoseconds());

        // Time as time_point
        using time_point = std::chrono::system_clock::time_point;
        time_type imu_time_point{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(sensors_data.imu.timestamp.getNanoseconds()))};

        // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
        la = {sensors_data.imu.linear_acceleration_uncalibrated.x , sensors_data.imu.linear_acceleration_uncalibrated.y, sensors_data.imu.linear_acceleration_uncalibrated.z };
        av = {sensors_data.imu.angular_velocity_uncalibrated.x  * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.y * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.z * (M_PI/180)};

        std::optional<cv::Mat> img0 = std::nullopt;
        std::optional<cv::Mat> img1 = std::nullopt;

        const switchboard::ptr<cam_type> c = _m_cam_type.get_nullable();
        if (c && c->serial_no != last_serial_no) {
            last_serial_no = c->serial_no;
            img0 = c->img0;
            img1 = c->img1;
            depth = c->depth;
            rgb = c->rgb;
        }

        it_log.log(record{__imu_cam_record, {
            {iteration_no},
            {bool(img0)},
        }});

        _m_imu_cam.put(new (_m_imu_cam.allocate()) imu_cam_type {
            imu_time_point,
            av,
            la,
            img0,
            img1,
            imu_time,
        });

        if (rgb && depth) {
            _m_rgb_depth->put(new rgb_depth_type{
                    rgb,
                    depth,
                    imu_time
                });
        }
        auto imu_integrator_params = new imu_integrator_seq{
			.seq = static_cast<int>(++_imu_integrator_seq),
		};
		_m_imu_integrator->put(imu_integrator_params);

        last_imu_ts = sensors_data.imu.timestamp;

        RAC_ERRNO_MSG("zed_imu at end of _p_one_iteration");
    }

private:
    std::shared_ptr<Camera> zedm;
    zed_camera_thread camera_thread_;

    const std::shared_ptr<switchboard> sb;
    switchboard::writer<imu_cam_type> _m_imu_cam;
    switchboard::reader<cam_type> _m_cam_type;
    switchboard::writer<rgb_depth_type> _m_rgb_depth;
    switchboard::writer<imu_integrator_seq> _m_imu_integrator;

    // IMU
    SensorsData sensors_data;
    Timestamp last_imu_ts = 0;
    Eigen::Vector3f la;
    Eigen::Vector3f av;

    // Timestamps
    time_type t;
    ullong imu_time;

    std::size_t last_serial_no {0};
    int64_t _imu_integrator_seq{0};

    // Logger
    record_coalescer it_log;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread);

int main(int argc, char **argv) { return 0; }
