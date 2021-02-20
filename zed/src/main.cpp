// ZED includes
#include <sl/Camera.hpp>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <fstream>

//ILLIXR includes
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/managed_thread.hpp"

using namespace ILLIXR;
using namespace sl;

/**
* Conversion function between sl::Mat and cv::Mat
**/
cv::Mat slMat2cvMat(Mat& input) {
    // Mapping between MAT_TYPE and CV_TYPE
    int cv_type = -1;
    switch (input.getDataType()) {
        case MAT_TYPE::F32_C1: cv_type = CV_32FC1; break;
        case MAT_TYPE::F32_C2: cv_type = CV_32FC2; break;
        case MAT_TYPE::F32_C3: cv_type = CV_32FC3; break;
        case MAT_TYPE::F32_C4: cv_type = CV_32FC4; break;
        case MAT_TYPE::U8_C1: cv_type = CV_8UC1; break;
        case MAT_TYPE::U8_C2: cv_type = CV_8UC2; break;
        case MAT_TYPE::U8_C3: cv_type = CV_8UC3; break;
        case MAT_TYPE::U8_C4: cv_type = CV_8UC4; break;
        default: break;
    }

    // Since cv::Mat data requires a uchar* pointer, we get the uchar1 pointer from sl::Mat (getPtr<T>())
    // cv::Mat and sl::Mat will share a single memory structure
    return cv::Mat(input.getHeight(), input.getWidth(), cv_type, input.getPtr<sl::uchar1>(MEM::CPU));
}

template <typename Scalar, int Rows, int Cols, typename Source>
Eigen::Matrix<Scalar, Rows, Cols> to_matrix(Source source) {
	return Eigen::Matrix<Scalar, Rows, Cols>{source};
}

const record_header __imu_cam_record {"imu_cam", {
    {"iteration_no", typeid(std::size_t)},
    {"has_camera", typeid(bool)},
}};

class ZedPlugin : public plugin {
private:

	class ImuThread : public ManagedThread {
	public:
		ImuThread(ZedPlugin& outer_)
			: outer{outer_}
			, _m_imu_cam{outer.sb->get_writer<imu_cam_type>("imu_cam")}
			, _m_cam{outer.sb->get_reader<imu_cam_type>("cam")}
			, it_log{outer.record_logger_}
		{ }


	virtual void body() override {
		SensorsData sensors_data;
		do {
			outer.camera.getSensorsData(sensors_data, TIME_REFERENCE::CURRENT);
			if (sensors_data.imu.timestamp > last_imu_ts) {
				last_imu_ts = sensors_data.imu.timestamp;
				break;
			}
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
		} while (true);

        std::optional<cv::Mat> img0;
        std::optional<cv::Mat> img1;

        switchboard::ptr<const imu_cam_type> c = _m_cam.get_ro_nullable();
        if (c && c->dataset_time != last_cam_time) {
            last_cam_time = c->dataset_time;
            img0 = c->img0;
            img1 = c->img1;
        }

        it_log.log(record{__imu_cam_record, {
			{get_iterations()},
            {bool(img0)},
        }});

        _m_imu_cam.put(_m_imu_cam.allocate(
			time_type{std::chrono::nanoseconds(sensors_data.imu.timestamp.getNanoseconds())},
			to_matrix<float, 3, 1>(sensors_data.imu.angular_velocity_uncalibrated.ptr()) * (M_PI/180),
			to_matrix<float, 3, 1>(sensors_data.imu.linear_acceleration_uncalibrated.ptr()),
            img0,
            img1,
            static_cast<ullong>(sensors_data.imu.timestamp.getNanoseconds())
		));

		// This limits the rate.
		std::this_thread::sleep_for(std::chrono::milliseconds{2});
	}

	private:
		ZedPlugin& outer;
		switchboard::writer<imu_cam_type> _m_imu_cam;
		switchboard::reader<imu_cam_type> _m_cam;
		SensorsData sensors_data;
		Timestamp last_imu_ts = 0;
		size_t last_cam_time = 0;
		record_coalescer it_log;
	};

	class CamThread : public ManagedThread {
	public:
		CamThread(ZedPlugin& outer_)
			: outer{outer_}
			, _m_cam{outer.sb->get_writer<imu_cam_type>("cam")}
			, _m_rgb_depth{outer.sb->get_writer<rgb_depth_type>("rgb_depth")}
			, image_size{outer.camera.getCameraInformation().camera_configuration.resolution}
		{
			runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
			imageL_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
			imageR_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C1, MEM::CPU);
			rgb_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C4, MEM::CPU);
			depth_zed.alloc(image_size.width, image_size.height, MAT_TYPE::F32_C1, MEM::CPU);

			imageL_ocv = slMat2cvMat(imageL_zed);
			imageR_ocv = slMat2cvMat(imageR_zed);
			rgb_ocv = slMat2cvMat(rgb_zed);
			depth_ocv = slMat2cvMat(depth_zed);
		}

		virtual void body() override {
			while (outer.camera.grab(runtime_parameters) != ERROR_CODE::SUCCESS) {
			}

			// Retrieve images
			outer.camera.retrieveImage(imageL_zed, VIEW::LEFT_GRAY, MEM::CPU, image_size);
			outer.camera.retrieveImage(imageR_zed, VIEW::RIGHT_GRAY, MEM::CPU, image_size);
			outer.camera.retrieveMeasure(depth_zed, MEASURE::DEPTH, MEM::CPU, image_size);
			outer.camera.retrieveImage(rgb_zed, VIEW::LEFT, MEM::CPU, image_size);

			auto cam_time = outer.camera.getTimestamp(sl::TIME_REFERENCE::IMAGE);

			_m_cam.put(_m_cam.allocate(
				// Make a copy, so that we don't have race
				time_type{},
				Eigen::Vector3f{},
				Eigen::Vector3f{},
	            imageL_ocv,
				imageR_ocv,
				cam_time
			));

            _m_rgb_depth.put(_m_rgb_depth.allocate(
                rgb_ocv,
                depth_ocv,
                cam_time
            ));

			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}

	private:
		ZedPlugin& outer;
		switchboard::writer<imu_cam_type> _m_cam;
		switchboard::writer<rgb_depth_type> _m_rgb_depth;
		Resolution image_size;
		RuntimeParameters runtime_parameters;

		Mat imageL_zed;
		Mat imageR_zed;
		Mat depth_zed;
		Mat rgb_zed;

		cv::Mat imageL_ocv;
		cv::Mat imageR_ocv;
		cv::Mat depth_ocv;
		cv::Mat rgb_ocv;
	};

	int start_camera() {
		InitParameters init_params;
	    init_params.camera_resolution = RESOLUTION::VGA;
	    init_params.coordinate_units = UNIT::MILLIMETER; // for kf
	    init_params.coordinate_system = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
	    init_params.camera_fps = 15;
	    init_params.depth_mode = DEPTH_MODE::PERFORMANCE;
	    init_params.depth_stabilization = true;
	    // init_params.depth_minimum_distance = 0.1;

	    // Open the camera
		std::cerr << "===============================================" << std::endl;
		enum sl::ERROR_CODE err = camera.open(init_params);

		std::cerr << (err != sl::ERROR_CODE::SUCCESS) << " // " << err << " != " << sl::ERROR_CODE::SUCCESS << std::endl;
		std::cerr << (err == sl::ERROR_CODE::SUCCESS) << " // " << err << " == " << sl::ERROR_CODE::SUCCESS << std::endl;
		std::cerr << (((int)err) != ((int)sl::ERROR_CODE::SUCCESS)) << " // " << ((int)err) << " != " << ((int)sl::ERROR_CODE::SUCCESS) << std::endl;
		std::cerr << (((int)err) == ((int)sl::ERROR_CODE::SUCCESS)) << " // " << ((int)err) << " == " << ((int)sl::ERROR_CODE::SUCCESS) << std::endl;

		if (err != sl::ERROR_CODE::SUCCESS) {
			std::cout << err << std::endl; // Display the error
			assert(0 && "ZED couldn't open.");
		}
	
	    // This is 4% of camera frame time, not 4 ms
	    camera.setCameraSettings(VIDEO_SETTINGS::EXPOSURE, 4);
	}

public:
	ZedPlugin(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, start_camera_{start_camera()}
		, imu_thread{*this}
		, cam_thread{*this}
	{ }
private:
	std::shared_ptr<switchboard> sb;
    Camera camera;
	int start_camera_;
	ProvidesInitProtocol<ImuThread> imu_thread;
	ProvidesInitProtocol<CamThread> cam_thread;
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(ZedPlugin);
