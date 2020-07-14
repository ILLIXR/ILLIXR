// ZED includes
#include <sl/Camera.hpp>
#include <opencv2/opencv.hpp>
#include <cmath>

//ILLIXR includes
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"
#include "common/threadloop.hpp"

using namespace sl;
using namespace ILLIXR;

cv::Mat slMat2cvMat(Mat& input);

std::shared_ptr<Camera> start_camera() {
    std::shared_ptr<Camera> zedm = std::make_shared<Camera>();

    // Cam setup
    InitParameters init_params;
    init_params.camera_resolution = RESOLUTION::VGA;
    init_params.coordinate_units = UNIT::METER;
    init_params.coordinate_system = COORDINATE_SYSTEM::RIGHT_HANDED_Z_UP_X_FWD; // Coordinate system used in ROS
    init_params.camera_fps = 60;
    init_params.depth_mode = DEPTH_MODE::PERFORMANCE;
    init_params.depth_minimum_distance = 0.1;

    // Open the camera
    ERROR_CODE err = zedm->open(init_params);
    if (err != ERROR_CODE::SUCCESS) {
        printf("%s\n", toString(err).c_str());
        zedm->close();
    }

    return zedm;
}

typedef struct {
	cv::Mat* img0;
	cv::Mat* img1;
	cv::Mat* rgb;
	cv::Mat* depth;
	std::size_t serial_no;
} cam_type;

class zed_camera_thread : public threadloop {
public:
  zed_camera_thread(std::string name_, phonebook* pb_, std::shared_ptr<Camera> zedm_)
  : threadloop{name_, pb_}
  , sb{pb->lookup_impl<switchboard>()}
  , _m_cam_type{sb->publish<cam_type>("cam_type")}
  , zedm{zedm_}
  , image_size{zedm->getCameraInformation().camera_resolution}
  {
    runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
    // Image setup
    imageL_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C4, MEM::CPU);
    imageR_zed.alloc(image_size.width, image_size.height, MAT_TYPE::U8_C4, MEM::CPU);
    depth_zed.alloc(image_size.width, image_size.height, MAT_TYPE::F32_C1);

    imageL_ocv = slMat2cvMat(imageL_zed);
    imageR_ocv = slMat2cvMat(imageR_zed);
    depth_ocv = slMat2cvMat(depth_zed);
  }

private:
  const std::shared_ptr<switchboard> sb;
  std::unique_ptr<writer<cam_type>> _m_cam_type;
  std::shared_ptr<Camera> zedm;
  Resolution image_size;
  RuntimeParameters runtime_parameters;
  std::size_t serial_no {0};

  Mat imageL_zed;
  Mat imageR_zed;
  Mat depth_zed;

  cv::Mat imageL_ocv;
  cv::Mat imageR_ocv;
  cv::Mat depth_ocv;

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
      zedm->retrieveImage(imageL_zed, VIEW::LEFT, MEM::CPU, image_size);
      zedm->retrieveImage(imageR_zed, VIEW::RIGHT, MEM::CPU, image_size);
      zedm->retrieveMeasure(depth_zed, MEASURE::DEPTH, MEM::CPU, image_size);


      cv::Mat* grayL_ocv_out = new cv::Mat{};
	  cv::Mat* grayR_ocv_out = new cv::Mat{};
	  cv::Mat* image_rgb_ocv_out = new cv::Mat{};
	  cv::Mat* depth_ocv_out = new cv::Mat{cv::Size{image_size.width, image_size.height}, CV_16UC1};

      // Convert to Grayscale
      cv::cvtColor(imageL_ocv, *grayL_ocv_out, CV_BGR2GRAY);
      cv::cvtColor(imageR_ocv, *grayR_ocv_out, CV_BGR2GRAY);
      cv::cvtColor(imageL_ocv, *image_rgb_ocv_out, CV_BGR2RGB);

      depth_ocv *= 1000.0f;
      depth_ocv.convertTo(*depth_ocv_out, CV_16UC1); // in mm, rounded

	  _m_cam_type->put(new cam_type{
			  grayL_ocv_out,
			  grayR_ocv_out,
			  image_rgb_ocv_out,
			  depth_ocv_out,
			  ++serial_no,
	  });
	  ++serial_no;
  }
};

class zed_imu_thread : public threadloop {
public:
    zed_imu_thread(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu_cam{sb->publish<imu_cam_type>("imu_cam")}
        , _m_rgb_depth{sb->publish<rgb_depth_type>("rgb_depth")}
        , zedm{start_camera()}
        , camera_thread_{"zed_camera_thread", pb_, zedm}
        , _m_cam_type{sb->subscribe_latest<cam_type>("cam_type")}
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
			return skip_option::run;
		} else {
			return skip_option::skip_and_yield;
		}
	}

    virtual void _p_one_iteration() override {

      // Time as ullong (nanoseconds)
      imu_time = static_cast<ullong>(sensors_data.imu.timestamp.getNanoseconds());

      // Time as time_point
      using time_point = std::chrono::system_clock::time_point;
      time_point uptime_timepoint{std::chrono::duration_cast<time_point::duration>(std::chrono::nanoseconds(sensors_data.imu.timestamp.getNanoseconds()))};
      std::time_t time2 = std::chrono::system_clock::to_time_t(uptime_timepoint);
      t = std::chrono::system_clock::from_time_t(time2);

      // Linear Acceleration and Angular Velocity (av converted from deg/s to rad/s)
      la = {sensors_data.imu.linear_acceleration_uncalibrated.x , sensors_data.imu.linear_acceleration_uncalibrated.y, sensors_data.imu.linear_acceleration_uncalibrated.z };
      av = {sensors_data.imu.angular_velocity_uncalibrated.x  * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.y * (M_PI/180), sensors_data.imu.angular_velocity_uncalibrated.z * (M_PI/180)};

	  std::optional<cv::Mat*> img0 = std::nullopt;
	  std::optional<cv::Mat*> img1 = std::nullopt;
	  std::optional<cv::Mat*> rgb = std::nullopt;
	  std::optional<cv::Mat*> depth = std::nullopt;

	  const cam_type* c = _m_cam_type->get_latest_ro();
	  if (c && c->serial_no != last_serial_no) {
		  last_serial_no = c->serial_no;
		  *img0 = c->img0;
		  *img1 = c->img1;
		  *rgb = c->rgb;
		  *depth = c->depth;
	  }

      _m_imu_cam->put(new imu_cam_type{
          t,
          av,
          la,
          img0,
          img1,
          imu_time,
      });

      // Passed to scene reconstruction
      if (rgb && depth) {
        _m_rgb_depth->put(new rgb_depth_type{
          static_cast<int64_t>(zedm->getTimestamp(TIME_REFERENCE::IMAGE)),
          rgb->data,
          depth->data,
        });
      }

      last_imu_ts = sensors_data.imu.timestamp;
    }

private:
    std::shared_ptr<Camera> zedm;
    zed_camera_thread camera_thread_;

    const std::shared_ptr<switchboard> sb;
    std::unique_ptr<writer<imu_cam_type>> _m_imu_cam;
    std::unique_ptr<writer<rgb_depth_type>> _m_rgb_depth;
    std::unique_ptr<reader_latest<cam_type>> _m_cam_type;

    // IMU
    SensorsData sensors_data;
    Timestamp last_imu_ts = 0;
    Eigen::Vector3f la;
    Eigen::Vector3f av;

    // Timestamps
    time_type t;
    ullong imu_time;

	std::size_t last_serial_no {0};
};

// This line makes the plugin importable by Spindle
PLUGIN_MAIN(zed_imu_thread);

int main(int argc, char **argv) { return 0; }

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
