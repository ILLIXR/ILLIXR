#include "common/threadloop.hpp"
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <opencv2/core/mat.hpp>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>

#include "vio_input.pb.h"
#include "video_encoder.h"
#include <boost/lockfree/spsc_queue.hpp>
#include "common/network/socket.hpp"
#include "common/network/timestamp.hpp"
#include "common/network/net_config.hpp"

using namespace ILLIXR;

class offload_writer : public threadloop {
private:
    boost::lockfree::spsc_queue<uint64_t> queue {1000};
    std::vector<int32_t> sizes;
    std::mutex mutex;
    std::condition_variable cv;
    GstMapInfo img0;
    GstMapInfo img1;
    bool img_ready = false;

public:
    offload_writer(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_clock{pb->lookup_impl<RelativeClock>()}
		, server_addr(SERVER_IP, SERVER_PORT_1)
		, drop_count{0}
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
		frame_info.open(data_path + "/frame_info.csv");
		enc_latency.open(data_path + "/enc.csv");

		frame_info << "frame_id,created_to_sent_time_ms,duration_to_send_ms,is_dropped" << endl;
		std::srand(std::time(0));
	}

    virtual void start() override {
        threadloop::start();

        encoder = std::make_unique<video_encoder>([this](const GstMapInfo& img0, const GstMapInfo& img1) {
            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << "=== latency: " << (curr - timestamp) / 1000000.0 << std::endl;
				enc_latency << (curr - timestamp) / 1000000.0 << std::endl;
            });
            {
                std::lock_guard<std::mutex> lock{mutex};
                this->img0 = img0;
                this->img1 = img1;
                img_ready = true;
            }
            cv.notify_one();
        });
        encoder->init();

		cout << "TEST: Connecting to " << server_addr.str(":") << endl;
		socket.connect(server_addr);
		cout << "Connected to " << server_addr.str(":") << endl;	

        sb->schedule<imu_cam_type_prof>(id, "imu_cam", [this](switchboard::ptr<const imu_cam_type_prof> datum, std::size_t) {
			this->send_imu_cam_data(datum);
		});
	}

protected:
    void _p_thread_setup() override {

    }

    void _p_one_iteration() override {
        std::this_thread::sleep_for(std::chrono::hours(1));
//        _encoder->start();
    }

public:

    void send_imu_cam_data(switchboard::ptr<const imu_cam_type_prof> datum) {
		// Ensures that slam doesnt start before valid IMU readings come in
        if (datum == nullptr) {
            assert(previous_timestamp == 0);
            return;
        }

//		assert(datum->time.time_since_epoch().count() > previous_timestamp);
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
			imu_cam_data->set_img0_size(-1);
			imu_cam_data->set_img1_size(-1);

		} else {
			cv::Mat img0 = (datum->img0.value()).clone();
			cv::Mat img1 = (datum->img1.value()).clone();

            // size of img0
            double img0_size = img0.total() * img0.elemSize();

            // get nanoseconds since epoch
            uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            queue.push(curr);
            std::unique_lock<std::mutex> lock{mutex};
            encoder->enqueue(img0, img1);
            cv.wait(lock, [this]() { return img_ready; });
            img_ready = false;

			imu_cam_data->set_img0_size(this->img0.size);
			imu_cam_data->set_img1_size(this->img1.size);

            sizes.push_back(this->img0.size);

            // calculate average sizes
            if (sizes.size() > 100) {
                int32_t sum = 0;
                for (auto& s : sizes) {
                    sum += s;
                }
                std::cout << sizes.size() << " average size: " << img0_size / (sum / sizes.size()) << " " << sum / sizes.size() << std::endl;
            }

			imu_cam_data->set_img0_data((void*) this->img0.data, this->img0.size);
			imu_cam_data->set_img1_data((void*) this->img1.data, this->img1.size);

            lock.unlock();

			data_buffer->set_real_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			data_buffer->set_dataset_timestamp(datum->dataset_time.time_since_epoch().count());
			data_buffer->set_frame_id(frame_id);
			
			float created_to_sent = (_m_clock->now().time_since_epoch().count() - datum->created_time) / 1e6;
			std::cout << "Created to send: " << created_to_sent << "\n";
			if (false) {
				std::cout << "dropping " << drop_count++ << "\n";
			} else {
				// if ((float)rand()/RAND_MAX < 0.2) {std::cout << "dropping " << drop_count++ << "\n"; return;} // drop packets
				
				// Prepare data delivery
				string data_to_be_sent = data_buffer->SerializeAsString();
				// std::cout << "Packet size: " << data_to_be_sent.size() << "\n";
				string delimitter = "END!";

				// float created_to_sent = (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - datum->created_time) / 1e6;
				// cout << "before write\n";
				auto start = timestamp();
				socket.write(data_to_be_sent + delimitter);
				// cout << "after write\n";
				auto send_duration = timestamp() - start;
				frame_info << frame_id << "," << created_to_sent << "," << send_duration << ",0" << endl;

				hash<std::string> hasher;
				auto hash_result = hasher(data_to_be_sent);
				hashed_data << frame_id << "\t" << hash_result << "\t" << data_buffer->dataset_timestamp() << endl;
			}
			frame_id++;
			delete data_buffer;
			data_buffer = new vio_input_proto::IMUCamVec();
		}
		
    }

private:
    std::unique_ptr<video_encoder> encoder = nullptr;
	long previous_timestamp = 0;
	int frame_id = 0;
	vio_input_proto::IMUCamVec* data_buffer = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<RelativeClock> _m_clock;

	TCPSocket socket;
	Address server_addr;

	const string data_path = filesystem::current_path().string() + "/recorded_data";
	std::ofstream hashed_data;
	std::ofstream frame_info;
	std::ofstream enc_latency;

	int drop_count;
};

PLUGIN_MAIN(offload_writer)