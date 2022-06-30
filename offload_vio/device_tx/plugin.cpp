#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <opencv2/core/mat.hpp>
#include <filesystem>
#include <fstream>

#include "vio_input.pb.h"
#include "common/network/socket.hpp"
#include "common/network/timestamp.hpp"
#include "common/network/net_config.hpp"

using namespace ILLIXR;

class offload_writer : public plugin {
public:
    offload_writer(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, server_addr(SERVER_IP, SERVER_PORT_1)
    { 
		socket.set_reuseaddr();
		socket.bind(Address(CLIENT_IP, CLIENT_PORT_1));
		initial_timestamp();

		if (!filesystem::exists(data_path)) {
			if (!filesystem::create_directory(data_path)) {
				std::cerr << "Failed to create data directory.";
			}
		}
		
		hashed_data.open(data_path + "/hash_device_tx.txt");
	}


    virtual void start() override {
        plugin::start();

		cout << "TEST: Connecting to " << server_addr.str(":") << endl;
		socket.connect(server_addr);
		cout << "Connected to " << server_addr.str(":") << endl;	

        sb->schedule<imu_cam_type_prof>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type_prof> datum, std::size_t) {
			this->send_imu_cam_data(datum);
		});
	}


    void send_imu_cam_data(switchboard::ptr<const imu_cam_type_prof> datum) {
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
			data_buffer->set_dataset_timestamp(datum->dataset_time.time_since_epoch().count());
			data_buffer->set_frame_id(frame_id);
			frame_id++;
			
			// Prepare data delivery
			string data_to_be_sent = data_buffer->SerializeAsString();
			string delimitter = "END!";

			auto start = timestamp();
			socket.write(data_to_be_sent + delimitter);
			auto send_duration = timestamp() - start;
			// cout << "Send time = " << send_duration << endl;

			hash<std::string> hasher;
    		auto hash_result = hasher(data_to_be_sent);
			hashed_data << frame_id << "\t" << hash_result << endl;

			delete data_buffer;
			data_buffer = new vio_input_proto::IMUCamVec();
		}
    }

private:
	long previous_timestamp = 0;
	int frame_id = 0;
	vio_input_proto::IMUCamVec* data_buffer = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard> sb;

	TCPSocket socket;
	Address server_addr;

	const string data_path = filesystem::current_path().string() + "/recorded_data";
	std::ofstream hashed_data;
};

PLUGIN_MAIN(offload_writer)