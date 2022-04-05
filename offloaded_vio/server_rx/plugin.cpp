#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/subscriber.h>
#include <google/protobuf/util/time_util.h>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include <thread>
#include <string>

#include "vio_input.pb.h"

using namespace ILLIXR;

class server_reader : public plugin {
public:
	server_reader(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->get_writer<imu_cam_type>("imu_cam")}
    { 
		eCAL::Initialize(0, NULL, "VIO Offloading Sensor Data Reader");
		subscriber = eCAL::protobuf::CSubscriber
		<vio_input_proto::IMUCamVec>("vio_input");
		subscriber.AddReceiveCallback(
		std::bind(&server_reader::ReceiveVioInput, this, std::placeholders::_2));
	}

	virtual void _p_one_iteration() {
	}


private:
	void ReceiveVioInput(const vio_input_proto::IMUCamVec& vio_input) {	
		std::cout << "Num imu vals: " << vio_input.imu_cam_data_size() << std::endl;

		// Loop through all IMU values first then the cam frame	
		for (int i = 0; i < vio_input.imu_cam_data_size(); i++) {
			vio_input_proto::IMUCamData curr_data = vio_input.imu_cam_data(i);

			if (curr_data.rows() == -1 && curr_data.cols() == -1) {
				std::cout << "IMU: " << i << std::endl;
				std::optional<cv::Mat> cam0 = std::nullopt;
				std::optional<cv::Mat> cam1 = std::nullopt;

				_m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type>(
					imu_cam_type {
						std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()),
						Eigen::Vector3f{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
						Eigen::Vector3f{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()},
						cam0,
						cam1,
						curr_data.timestamp()
					}
				));

			} else {
				std::cout << "IMU: " << i << std::endl;
				std::cout << "Cam Frame" << std::endl;

				unsigned char* img0_data = (unsigned char*) curr_data.img0_data().c_str();
				unsigned char* img1_data = (unsigned char*) curr_data.img1_data().c_str();

				cv::Mat img0(curr_data.rows(), curr_data.cols(), 0, img0_data);
				cv::Mat img1(curr_data.rows(), curr_data.cols(), 0, img1_data);	

				// std::vector<uint8_t> img0_vec(curr_data.img0_data().begin(),curr_data.img0_data().end());
				// std::vector<uint8_t> img1_vec(curr_data.img1_data().begin(),curr_data.img1_data().end());

				// // Verified that these dont have to be pointers
				// cv::Mat img0(curr_data.rows(), curr_data.cols(), CV_8UC1, img0_vec.data());
				// cv::Mat img1(curr_data.rows(), curr_data.cols(), CV_8UC1, img1_vec.data());

				// std::cout << "TEST " << curr_data.img0_data().size() << std::endl;

				// const std::string::size_type img0_size = curr_data.img0_data().size();
				// char *img0_buffer = new char[img0_size + 1];
				// memcpy(img0_buffer, curr_data.img0_data().c_str(), img0_size + 1);
				// img0_buffer[img0_size] = '\0';

				// const std::string::size_type img1_size = curr_data.img1_data().size();
				// char *img1_buffer = new char[img1_size + 1];
				// memcpy(img1_buffer, curr_data.img1_data().c_str(), img1_size + 1);
				// img1_buffer[img1_size] = '\0';

				// cv::Mat img0(curr_data.rows(), curr_data.cols(), CV_8UC1, img0_buffer);
				// cv::Mat img1(curr_data.rows(), curr_data.cols(), CV_8UC1, img1_buffer);

				cv::imshow("test1", img0);
				cv::waitKey(0);
				cv::imshow("test2", img1);
				cv::waitKey(0);

				std::optional<cv::Mat> cam0 = std::make_optional<cv::Mat>(img0);
				std::optional<cv::Mat> cam1 = std::make_optional<cv::Mat>(img1);

				_m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type>(
					imu_cam_type {
						std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()),
						Eigen::Vector3f{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
						Eigen::Vector3f{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()},
						cam0,
						cam1,
						curr_data.timestamp()
					}
				));		
			}
		}
	}

    const std::shared_ptr<switchboard> sb;
	switchboard::writer<imu_cam_type> _m_imu_cam;

	eCAL::protobuf::CSubscriber<vio_input_proto::IMUCamVec> subscriber;
};

PLUGIN_MAIN(server_reader)