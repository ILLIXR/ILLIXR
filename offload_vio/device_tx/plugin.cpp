#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/publisher.h>
#include <opencv2/core/mat.hpp>

#include "vio_input.pb.h"

using namespace ILLIXR;

class offload_writer : public plugin {
public:
    offload_writer(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
    { 
		eCAL::Initialize(0, NULL, "VIO Device Transmitter");
		publisher = eCAL::protobuf::CPublisher<vio_input_proto::IMUCamVec>("vio_input");
		publisher.SetLayerMode(eCAL::TLayer::tlayer_udp_mc, eCAL::TLayer::smode_off);
		publisher.SetLayerMode(eCAL::TLayer::tlayer_tcp, eCAL::TLayer::smode_auto);
	}


    virtual void start() override {
        plugin::start();

        sb->schedule<imu_cam_type>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type> datum, std::size_t) {
			this->send_imu_cam_data(datum);
		});
	}


    void send_imu_cam_data(switchboard::ptr<const imu_cam_type> datum) {
		// Ensures that slam doesnt start before valid IMU readings come in
        if (datum == nullptr) {
            assert(previous_timestamp == 0);
            return;
        }

		assert(datum->time.time_since_epoch().count() > previous_timestamp);
		previous_timestamp = datum->time.time_since_epoch().count();

		vio_input_proto::IMUCamData* imu_cam_data = data_buffer->add_imu_cam_data();
		imu_cam_data->set_timestamp(datum->time.time_since_epoch().count());

		vio_input_proto::Vec3* angular_vel = new vio_input_proto::Vec3();
		angular_vel->set_x(datum->angular_v.x());
		angular_vel->set_y(datum->angular_v.y());
		angular_vel->set_z(datum->angular_v.z());
		imu_cam_data->set_allocated_angular_vel(angular_vel);

		vio_input_proto::Vec3* linear_accel = new vio_input_proto::Vec3();
		linear_accel->set_x(datum->linear_a.x());
		linear_accel->set_y(datum->linear_a.y());
		linear_accel->set_z(datum->linear_a.z());
		imu_cam_data->set_allocated_linear_accel(linear_accel);

      	if (!datum->img0.has_value() && !datum->img1.has_value()) {
			imu_cam_data->set_rows(-1);
			imu_cam_data->set_cols(-1);

		} else {
			cv::Mat img0{(datum->img0.value()).clone()};
        	cv::Mat img1{(datum->img1.value()).clone()};

			imu_cam_data->set_rows(img0.rows);
			imu_cam_data->set_cols(img0.cols);

			imu_cam_data->set_img0_data((void*) img0.data, img0.rows * img0.cols);
			imu_cam_data->set_img1_data((void*) img1.data, img1.rows * img1.cols);

			data_buffer->set_real_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			publisher.Send(*data_buffer);
			delete data_buffer;
			data_buffer = new vio_input_proto::IMUCamVec();
		}
    }

private:
	long previous_timestamp = 0;
	vio_input_proto::IMUCamVec* data_buffer = new vio_input_proto::IMUCamVec();

    const std::shared_ptr<switchboard> sb;
	eCAL::protobuf::CPublisher<vio_input_proto::IMUCamVec> publisher;
};

PLUGIN_MAIN(offload_writer)