#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "vio_input.pb.h"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/subscriber.h>
#include <memory>
#include <opencv2/core/mat.hpp>
#include <utility>

using namespace ILLIXR;

class server_reader : public plugin {
public:
    server_reader(std::string name_, phonebook* pb_)
        : plugin{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_imu{sb->get_writer<imu_type>("imu")}
        , _m_cam{sb->get_writer<cam_type>("cam")} {
        eCAL::Initialize(0, nullptr, "VIO Server Reader");
        subscriber = eCAL::protobuf::CSubscriber<vio_input_proto::IMUCamVec>("vio_input");
        subscriber.AddReceiveCallback(std::bind(&server_reader::ReceiveVioInput, this, std::placeholders::_2));
    }

private:
    void ReceiveVioInput(const vio_input_proto::IMUCamVec& vio_input) {
        // Loop through all IMU values first then the cam frame
        for (int i = 0; i < vio_input.imu_cam_data_size(); i++) {
            const vio_input_proto::IMUCamData& curr_data = vio_input.imu_cam_data(i);

            if (curr_data.rows() != -1 && curr_data.cols() != -1) {
                cv::Mat img0(curr_data.rows(), curr_data.cols(), CV_8UC1, (void*) (curr_data.img0_data().data()));
                cv::Mat img1(curr_data.rows(), curr_data.cols(), CV_8UC1, (void*) (curr_data.img1_data().data()));

                _m_cam.put(_m_cam.allocate<cam_type>(
                    cam_type{time_point{std::chrono::nanoseconds{curr_data.timestamp()}}, img0.clone(), img1.clone()}));
            }

            _m_imu.put(_m_imu.allocate<imu_type>(imu_type{
                time_point{std::chrono::nanoseconds{curr_data.timestamp()}},
                Eigen::Vector3d{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
                Eigen::Vector3d{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()}}));
        }
    }

    const std::shared_ptr<switchboard> sb;
    switchboard::writer<imu_type>      _m_imu;
    switchboard::writer<cam_type>      _m_cam;

    eCAL::protobuf::CSubscriber<vio_input_proto::IMUCamVec> subscriber;
};

PLUGIN_MAIN(server_reader)
