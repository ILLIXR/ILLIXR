#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API
#include <mutex>

// ILLIXR includes
#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

static constexpr int IMAGE_WIDTH_D4XX = 640;
static constexpr int IMAGE_HEIGHT_D4XX = 480;
static constexpr int FPS_D4XX = 30;
static constexpr int GYRO_RATE_D4XX = 400; // 200 or 400
static constexpr int ACCEL_RATE_D4XX = 250; // 63 or 250

static constexpr int IMAGE_WIDTH_T26X = 848;
static constexpr int IMAGE_HEIGHT_T26X = 800;

class realsense : public plugin {
public:
	realsense(std::string name_, phonebook *pb_)
        : plugin{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_cam{sb->get_writer<imu_cam_type>("imu_cam")}
        , _m_rgb_depth{sb->get_writer<rgb_depth_type>("rgb_depth")}
        , realsense_cam{ILLIXR::getenv_or("REALSENSE_CAM", "auto")}
		, _m_clock{pb->lookup_impl<RelativeClock>()}
        {
            cfg.disable_all_streams();
            configure_camera();
        }

	void callback(const rs2::frame& frame)
        {
            std::lock_guard<std::mutex> lock(mutex);
            // This lock guarantees that concurrent invocations of `callback` are serialized.
            // Even if the API does not invoke `callback` in parallel, this is still important for the memory-model.
            // Without this lock, prior invocations of `callback` are not necessarily "happens-before" ordered, so accessing persistent variables constitutes a data-race, which is undefined behavior in the C++ memory model.

            if (cam_select == D4XXI){
                if (auto fs = frame.as<rs2::frameset>()) {
                    rs2::video_frame ir_frame_left = fs.get_infrared_frame(1);
                    rs2::video_frame ir_frame_right = fs.get_infrared_frame(2);
                    rs2::video_frame depth_frame = fs.get_depth_frame();
                    rs2::video_frame rgb_frame = fs.get_color_frame();
                    cv::Mat ir_left = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC1, (void*)ir_frame_left.get_data());
                    cv::Mat ir_right = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC1, (void *)ir_frame_right.get_data());
                    cv::Mat rgb = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_8UC3, (void *)rgb_frame.get_data());
                    cv::Mat depth = cv::Mat(cv::Size(IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX), CV_16UC1, (void *)depth_frame.get_data());
                    cv::Mat converted_depth;
                    float depth_scale = pipe.get_active_profile().get_device().first<rs2::depth_sensor>().get_depth_scale(); // for converting measurements into millimeters
                    depth.convertTo(converted_depth, CV_32FC1, depth_scale * 1000.f);
                    cam_type_ = cam_type {
                        .img0 = cv::Mat{ir_left},
                        .img1 = cv::Mat{ir_right},
                        .rgb = cv::Mat{rgb},
                        .depth = cv::Mat{converted_depth},
                        .iteration = iteration_cam,
                    };
                    iteration_cam++;
                }
            }

            else if (cam_select == T26X){
                if (auto fs = frame.as<rs2::frameset>()) {
                    rs2::video_frame fisheye_frame_left = fs.get_fisheye_frame(1);
                    rs2::video_frame fisheye_frame_right = fs.get_fisheye_frame(2);
                    cv::Mat fisheye_left = cv::Mat(cv::Size(IMAGE_WIDTH_T26X, IMAGE_HEIGHT_T26X), CV_8UC1, (void*)fisheye_frame_left.get_data());
                    cv::Mat fisheye_right = cv::Mat(cv::Size(IMAGE_WIDTH_T26X, IMAGE_HEIGHT_T26X), CV_8UC1, (void *)fisheye_frame_right.get_data());
                    cam_type_ = cam_type {
                        .img0 = cv::Mat{fisheye_left},
                        .img1 = cv::Mat{fisheye_right},
                        .iteration = iteration_cam,
                    };
                    iteration_cam++;
                }
            }

            if (auto mf = frame.as<rs2::motion_frame>()) {
                std::string s = mf.get_profile().stream_name();

                if (s == "Accel")
                {
                    rs2::motion_frame accel = mf;
                    accel_data = accel.get_motion_data();
                    accel_type_.accel_data = &accel_data;
                    accel_type_.iteration = iteration_accel;
                    iteration_accel++;
                }

                if (s == "Gyro")
                {
                    if (last_iteration_accel == accel_type_.iteration) { return; }

                    last_iteration_accel = accel_type_.iteration;
                    rs2_vector accel = *accel_type_.accel_data;
                    rs2::motion_frame gyro = mf;
                    double ts = gyro.get_timestamp();
                    gyro_data = gyro.get_motion_data();

                    // IMU data
                    Eigen::Vector3f la = {accel.x, accel.y, accel.z};
                    Eigen::Vector3f av = {gyro_data.x, gyro_data.y, gyro_data.z};

                    // Time as ullong (nanoseconds)
                    ullong imu_time = static_cast<ullong>(ts * 1000000);
					if (!_m_first_imu_time) {
						_m_first_imu_time = imu_time;
						_m_first_real_time = _m_clock->now();
					}
					time_point imu_time_point{*_m_first_real_time + std::chrono::nanoseconds(imu_time - *_m_first_imu_time)};

                    // Time as time_point
                    time_point imu_time_point{*first_real_time + std::chrono::nanoseconds(imu_time - *first_imu_time)};

                    // Images
                    std::optional<cv::Mat> img0 = std::nullopt;
                    std::optional<cv::Mat> img1 = std::nullopt;
                    std::optional<cv::Mat> rgb = std::nullopt;
                    std::optional<cv::Mat> depth = std::nullopt;

                        
                    if (last_iteration_cam != cam_type_.iteration)
                    {
                        last_iteration_cam = cam_type_.iteration;
                        img0 = cam_type_.img0;
                        img1 = cam_type_.img1;
                        rgb = cam_type_.rgb;
                        depth = cam_type_.depth;
                    }
                    
                    // Submit to switchboard
                    _m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type>(
                        {
                            imu_time_point,
                            av,
                            la,
                            img0,
                            img1,
                            imu_time
                        }
                    ));
                    
                    if (rgb && depth)
                    {
                        _m_rgb_depth.put(_m_rgb_depth.allocate<rgb_depth_type>(
                            {
                                rgb,
                                depth,
                                imu_time
                            }
                        ));
                    }
                }
            }
			
        };

	virtual ~realsense() override { pipe.stop(); }

private:
    typedef struct {
        cv::Mat img0;
        cv::Mat img1;
        cv::Mat rgb;
        cv::Mat depth;
        int iteration;
    } cam_type;

    typedef enum {
        UNSUPPORTED,
        D4XXI,
        T26X
    } cam_enum;

    typedef struct {
        rs2_vector* accel_data;
        int iteration;
    } accel_type;

	const std::shared_ptr<switchboard> sb;
    switchboard::writer<imu_cam_type> _m_imu_cam;
	switchboard::writer<rgb_depth_type> _m_rgb_depth;
    std::mutex mutex;
	rs2::pipeline_profile profiles;
	rs2::pipeline pipe;
	rs2::config cfg;
	rs2_vector gyro_data;
	rs2_vector accel_data;

	cam_type cam_type_;
    cam_enum cam_select{UNSUPPORTED};
    bool D4XXI_found{false};
    bool T26X_found{false}; 
    
	accel_type accel_type_;
	int iteration_cam = 0;
	int iteration_accel = 0;
	int last_iteration_cam;
	int last_iteration_accel;
    std::string realsense_cam;

    void find_supported_devices(rs2::device_list devices){
        bool gyro_found{false};
        bool accel_found{false};    
        for (rs2::device device : devices) {
            if (device.supports(RS2_CAMERA_INFO_PRODUCT_LINE)){
                std::string product_line = device.get_info(RS2_CAMERA_INFO_PRODUCT_LINE); 
                #ifndef NDEBUG
                    std::cout << "Found Product Line: " << product_line << std::endl;
                #endif
                if (product_line == "D400"){
                    #ifndef NDEBUG
                        std::cout << "Checking for supported streams" << std::endl;
                    #endif
                    std::vector<rs2::sensor> sensors = device.query_sensors();
                    for (rs2::sensor sensor : sensors){
                        std::vector<rs2::stream_profile> stream_profiles = sensor.get_stream_profiles();
                        //Currently, all D4XX cameras provide infrared, RGB, and depth, so we only need to check for accel and gyro
                        for (auto&& sp : stream_profiles)
                        {
                            if (sp.stream_type() == RS2_STREAM_GYRO){
                                gyro_found = true;
                            }
                            if (sp.stream_type() == RS2_STREAM_ACCEL){
                                accel_found = true;
                            }
                        }
                    }
                    if (accel_found && gyro_found){
                        D4XXI_found = true;
                        #ifndef NDEBUG
                            std::cout << "Supported D4XX found!" << std::endl;
                        #endif
                    }
                } 
                else if (product_line == "T200"){
                    T26X_found = true;
                    #ifndef NDEBUG
                        std::cout << "T26X found! " << std::endl;
                    #endif
                }
            }
            
        }
        if (!T26X_found && !D4XXI_found){
            #ifndef NDEBUG
                std::cout << "No supported Realsense device detected!" << std::endl;
            #endif
        }
    }

    void configure_camera()
    {
        rs2::context ctx;
        rs2::device_list devices = ctx.query_devices();
        //This plugin assumes only one device should be connected to the system. If multiple supported devices are found the preference is to choose D4XX with IMU over T26X systems. 
        find_supported_devices(devices);
        if (realsense_cam.compare("auto") == 0) {
            if (D4XXI_found){
                cam_select = D4XXI;
                #ifndef NDEBUG
                    std::cout << "Setting cam_select: D4XX" << std::endl;
                #endif
            }
            else if (T26X_found){
                cam_select = T26X;
                #ifndef NDEBUG
                    std::cout << "Setting cam_select: T26X" << std::endl;
                #endif
            }
        }
        else if ((realsense_cam.compare("D4XX") == 0) && D4XXI_found){
            cam_select = D4XXI;
            #ifndef NDEBUG
                std::cout << "Setting cam_select: D4XX" << std::endl;
            #endif
        }
        else if ((realsense_cam.compare("T26X") == 0) && T26X_found){
            cam_select = T26X;
            #ifndef NDEBUG
                std::cout << "Setting cam_select: T26X" << std::endl;
            #endif
        }
        if (cam_select == UNSUPPORTED){
            ILLIXR::abort("Supported Realsense device NOT found!");
        }
        if (cam_select == T26X){
            //T26X series has fixed options for accel rate, gyro rate, fisheye resolution, and FPS
            cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F); // 62 Hz
            cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F); // 200 Hz
            cfg.enable_stream(RS2_STREAM_FISHEYE, 1, RS2_FORMAT_Y8); //848x800, 30 FPS
            cfg.enable_stream(RS2_STREAM_FISHEYE, 2, RS2_FORMAT_Y8); //848x800, 30 FPS
            profiles = pipe.start(cfg, [&](const rs2::frame& frame) { this->callback(frame); });
        }
        else if (cam_select == D4XXI){
            cfg.enable_stream(RS2_STREAM_ACCEL, RS2_FORMAT_MOTION_XYZ32F, ACCEL_RATE_D4XX); // adjustable to 0, 63 (default), 250 hz
            cfg.enable_stream(RS2_STREAM_GYRO, RS2_FORMAT_MOTION_XYZ32F, GYRO_RATE_D4XX); // adjustable set to 0, 200 (default), 400 hz
            cfg.enable_stream(RS2_STREAM_INFRARED, 1, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Y8, FPS_D4XX);
            cfg.enable_stream(RS2_STREAM_INFRARED, 2, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Y8, FPS_D4XX);
            cfg.enable_stream(RS2_STREAM_COLOR, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_BGR8, FPS_D4XX);
            cfg.enable_stream(RS2_STREAM_DEPTH, IMAGE_WIDTH_D4XX, IMAGE_HEIGHT_D4XX, RS2_FORMAT_Z16, FPS_D4XX);
            profiles = pipe.start(cfg, [&](const rs2::frame& frame) { this->callback(frame); });
            profiles.get_device().first<rs2::depth_sensor>().set_option(RS2_OPTION_EMITTER_ENABLED, 0.f); // disables IR emitter to use stereo images for SLAM but degrades depth quality in low texture environments.
        }
    }


	std::optional<ullong> _m_first_imu_time;
	std::optional<time_point> _m_first_real_time;
	const std::shared_ptr<const RelativeClock> _m_clock;
};

PLUGIN_MAIN(realsense);

