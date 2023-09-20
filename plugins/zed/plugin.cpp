#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// ILLIXR includes
#include "illixr/data_format.hpp"
#include "illixr/error_util.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

// ZED includes
#include "zed_opencv.hpp"

using namespace ILLIXR;

// Set exposure to 8% of camera frame time. This is an empirically determined number
static constexpr unsigned EXPOSURE_TIME_PERCENT = 8;

const record_header __imu_cam_record{"imu_cam",
                                     {
                                         {"iteration_no", typeid(std::size_t)},
                                         {"has_camera", typeid(bool)},
                                     }};

struct cam_type_zed : public switchboard::event {
    cam_type_zed(cv::Mat _img0, cv::Mat _img1, cv::Mat _rgb, cv::Mat _depth, std::size_t _serial_no)
        : img0{std::move(_img0)}
        , img1{std::move(_img1)}
        , rgb{std::move(_rgb)}
        , depth{std::move(_depth)}
        , serial_no{_serial_no} { }

    cv::Mat     img0;
    cv::Mat     img1;
    cv::Mat     rgb;
    cv::Mat     depth;
    std::size_t serial_no;
};

std::shared_ptr<Camera> start_camera() {
    std::shared_ptr<Camera> zedm = std::make_shared<Camera>();

    assert(zedm != nullptr && "Zed camera should be initialized");

    // Cam setup
    InitParameters init_params;
    init_params.camera_resolution      = RESOLUTION::VGA;
    init_params.coordinate_units       = UNIT::MILLIMETER;                           // For scene reconstruction
    init_params.coordinate_system      = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
    init_params.camera_fps             = 30;                                         // gives best user experience
    init_params.depth_mode             = DEPTH_MODE::PERFORMANCE;
    init_params.depth_stabilization    = true;
    init_params.depth_minimum_distance = 0.3;

    // Open the camera
    ERROR_CODE err = zedm->open(init_params);
    if (err != ERROR_CODE::SUCCESS) {
        spdlog::get("illixr")->info("[zed] {}", toString(err).c_str());
        zedm->close();
    }

    zedm->setCameraSettings(VIDEO_SETTINGS::EXPOSURE, EXPOSURE_TIME_PERCENT);

    return zedm;
}

class zed_camera_thread : public threadloop {
public:
    zed_camera_thread(const std::string& name_, phonebook* pb_, std::shared_ptr<Camera> zedm_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_cam{sb->get_writer<cam_type_zed>("cam_zed")}
        , zedm{std::move(zedm_)}
        , image_size{zedm->getCameraInformation().camera_configuration.resolution} {
        // runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
        // Image setup
        imageL_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
        imageR_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
        rgb_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C4, MEM::CPU);
        depth_zed.alloc(image_size.width, image_size.height, MAT_TYPE::F32_C1, MEM::CPU);

        imageL_ocv = slMat2cvMat(imageL_zed);
        imageR_ocv = slMat2cvMat(imageR_zed);
        rgb_ocv    = slMat2cvMat(rgb_zed);
        depth_ocv  = slMat2cvMat(depth_zed);
    }

private:
    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<cam_type_zed>          _m_cam;
    std::shared_ptr<Camera>                    zedm;
    Resolution                                 image_size;
    RuntimeParameters                          runtime_parameters;
    std::size_t                                serial_no{0};

    Mat imageL_zed;
    Mat imageR_zed;
    Mat depth_zed;
    Mat rgb_zed;

    cv::Mat imageL_ocv;
    cv::Mat imageR_ocv;
    cv::Mat depth_ocv;
    cv::Mat rgb_ocv;

    std::optional<ullong> _m_first_imu_time;

protected:
    skip_option _p_should_skip() override {
        if (zedm->grab(runtime_parameters) == ERROR_CODE::SUCCESS) {
            return skip_option::run;
        } else {
            return skip_option::skip_and_spin;
        }
    }

    void _p_one_iteration() override {
        RAC_ERRNO_MSG("zed at start of _p_one_iteration");

        // Time as ullong (nanoseconds)
        // ullong cam_time = static_cast<ullong>(zedm->getTimestamp(TIME_REFERENCE::IMAGE).getNanoseconds());

        // Retrieve images
        zedm->retrieveImage(imageL_zed, VIEW::LEFT_GRAY, MEM::CPU, image_size);
        zedm->retrieveImage(imageR_zed, VIEW::RIGHT_GRAY, MEM::CPU, image_size);
        zedm->retrieveMeasure(depth_zed, MEASURE::DEPTH, MEM::CPU, image_size);
        zedm->retrieveImage(rgb_zed, VIEW::LEFT, MEM::CPU, image_size);

        _m_cam.put(_m_cam.allocate<cam_type_zed>({cv::Mat{imageL_ocv.clone()}, cv::Mat{imageR_ocv.clone()},
                                                  cv::Mat{rgb_ocv.clone()}, cv::Mat{depth_ocv.clone()}, ++serial_no}));

        RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
    }
};

class zed_imu_thread : public threadloop {
public:
    void stop() override {
        camera_thread_.stop();
        threadloop::stop();
    }

    zed_imu_thread(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , zedm{start_camera()}
        , camera_thread_{"zed_camera_thread", pb_, zedm}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_imu{sb->get_writer<imu_type>("imu")}
        , _m_cam_reader{sb->get_reader<cam_type_zed>("cam_zed")}
        , _m_cam_publisher{sb->get_writer<cam_type>("cam")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
        , it_log{record_logger_} {
        camera_thread_.start();
    }

    // destructor
    ~zed_imu_thread() override {
        zedm->close();
    }

protected:
    skip_option _p_should_skip() override {
        zedm->getSensorsData(sensors_data, TIME_REFERENCE::CURRENT);
        if (sensors_data.imu.timestamp > last_imu_ts) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            return skip_option::run;
        } else {
            return skip_option::skip_and_yield;
        }
    }

    void _p_one_iteration() override {
        RAC_ERRNO_MSG("zed at start of _p_one_iteration");

        // std::cout << "IMU Rate: " << sensors_data.imu.effective_rate << "\n" << std::endl;

        // Time as ullong (nanoseconds)
        auto imu_time = static_cast<ullong>(sensors_data.imu.timestamp.getNanoseconds());

        // Time as time_point
        if (!_m_first_imu_time) {
            _m_first_imu_time  = imu_time;
            _m_first_real_time = _m_clock->now();
        }
        // _m_first_real_time is the time point when the system receives the first IMU sample
        // Timestamp for later IMU samples is its dataset time difference from the first sample added to _m_first_real_time
        time_point imu_time_point{*_m_first_real_time + std::chrono::nanoseconds(imu_time - *_m_first_imu_time)};

        // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
        Eigen::Vector3f la = {sensors_data.imu.linear_acceleration_uncalibrated.x,
                              sensors_data.imu.linear_acceleration_uncalibrated.y,
                              sensors_data.imu.linear_acceleration_uncalibrated.z};
        Eigen::Vector3f av = {static_cast<float>(sensors_data.imu.angular_velocity_uncalibrated.x * (M_PI / 180)),
                              static_cast<float>(sensors_data.imu.angular_velocity_uncalibrated.y * (M_PI / 180)),
                              static_cast<float>(sensors_data.imu.angular_velocity_uncalibrated.z * (M_PI / 180))};

        _m_imu.put(_m_imu.allocate<imu_type>({imu_time_point, av.cast<double>(), la.cast<double>()}));

        switchboard::ptr<const cam_type_zed> c = _m_cam_reader.get_ro_nullable();
        if (c && c->serial_no != last_serial_no) {
            _m_cam_publisher.put(_m_cam_publisher.allocate<cam_type>({imu_time_point, cv::Mat{c->img0}, cv::Mat{c->img1}}));
            _m_rgb_depth.put(_m_rgb_depth.allocate<rgb_depth_type>({imu_time_point, cv::Mat{c->rgb}, cv::Mat{c->depth}}));
            last_serial_no = c->serial_no;
        }

        last_imu_ts = sensors_data.imu.timestamp;

        RAC_ERRNO_MSG("zed_imu at end of _p_one_iteration");
    }

private:
    std::shared_ptr<Camera> zedm;
    zed_camera_thread       camera_thread_;

    const std::shared_ptr<switchboard>         sb;
    const std::shared_ptr<const RelativeClock> _m_clock;
    switchboard::writer<imu_type>              _m_imu;
    switchboard::reader<cam_type_zed>          _m_cam_reader;
    switchboard::writer<cam_type>              _m_cam_publisher;
    switchboard::writer<rgb_depth_type>        _m_rgb_depth;

    // IMU
    SensorsData sensors_data;
    Timestamp   last_imu_ts    = 0;
    std::size_t last_serial_no = 0;

    // Logger
    record_coalescer it_log;

    std::optional<ullong>     _m_first_imu_time;
    std::optional<time_point> _m_first_real_time;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread)
