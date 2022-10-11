#include "common/threadloop.hpp"
#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/stoplight.hpp"
#include "common/switchboard.hpp"
#include "vio_input.pb.h"

#include <opencv/cv.hpp>
#include <opencv2/core/mat.hpp>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>

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
    { 
		if (!filesystem::exists(data_path)) {
			if (!std::filesystem::create_directory(data_path)) {
				std::cerr << "Failed to create data directory.";
			}
		}
		
		enc_latency.open(data_path + "/enc_latency.csv");
		compression_csv.open(data_path + "/compression_info.csv");
		compression_csv << "compression ratio" << "," << "size"<< "," << "average size" << std::endl;

		socket.set_reuseaddr();
		socket.bind(Address(CLIENT_IP, CLIENT_PORT_1));
		initial_timestamp();

		std::srand(std::time(0));
	}

    virtual void start() override {
        threadloop::start();

        encoder = std::make_unique<video_encoder>([this](const GstMapInfo& img0, const GstMapInfo& img1) {
            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                // std::cout << "=== latency: " << (curr - timestamp) / 1000000.0 << std::endl;
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
		while (!_m_stoplight->check_should_stop()) {
        	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
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

			// WITH COMPRESSION
            // get nanoseconds since epoch
            uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto start_cmp = timestamp();
            queue.push(curr);
            std::unique_lock<std::mutex> lock{mutex};
            encoder->enqueue(img0, img1);
            cv.wait(lock, [this]() { return img_ready; });
			enc_latency << frame_id << "," << datum->time.time_since_epoch().count() << "," << timestamp() - start_cmp << std::endl;
            img_ready = false;

			imu_cam_data->set_img0_size(this->img0.size);
			imu_cam_data->set_img1_size(this->img1.size);

            sizes.push_back(this->img0.size);

            // calculate average sizes
            if (sizes.size() > 1) {
                int32_t sum = 0;
                for (auto& s : sizes) {
                    sum += s;
                }
				// ratios.push_back(img0_size / (sum / sizes.size()));
				// sizes_avg.push_back(sum / sizes.size());
				compression_csv << img0_size / (sum / sizes.size()) << "," << this->img0.size << "," << sum / sizes.size() << std::endl;
                // std::cout << sizes.size() << " compression ratio: " << img0_size / (sum / sizes.size()) << " average size " << sum / sizes.size() << std::endl;
            }

			imu_cam_data->set_img0_data((void*) this->img0.data, this->img0.size);
			imu_cam_data->set_img1_data((void*) this->img1.data, this->img1.size);

            lock.unlock();
			// WITH COMPRESSION END

			// NO COMPRESSION
			// imu_cam_data->set_img0_size(img0_size);
			// imu_cam_data->set_img1_size(img0_size);

			// imu_cam_data->set_img0_data((void*) img0.data, img0_size);
			// imu_cam_data->set_img1_data((void*) img1.data, img0_size);

			data_buffer->set_real_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			data_buffer->set_dataset_timestamp(datum->dataset_time.time_since_epoch().count());
			data_buffer->set_frame_id(frame_id);
			data_buffer->set_cam_time(datum->time.time_since_epoch().count());
					
			// Prepare data delivery
			string delimitter = "EEND!";

			string data_to_be_sent = data_buffer->SerializeAsString();
			socket.write(data_to_be_sent + delimitter);
			frame_id++;
			delete data_buffer;
			data_buffer = new vio_input_proto::IMUCamVec();
		}
    }

	// virtual void stop() override{
	// 	for (size_t i = 0; i < ratios.size(); i++) {
	// 		compression_csv << ratios[i] << "," << sizes[i] << "," << sizes_avg[i] << std::endl;
	// 	}
	// }

private:
    std::unique_ptr<video_encoder> encoder = nullptr;
	long previous_timestamp = 0;
	int frame_id = 0;
	vio_input_proto::IMUCamVec* data_buffer = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<RelativeClock> _m_clock;

	TCPSocket socket;
	Address server_addr;

	const std::string data_path = filesystem::current_path().string() + "/recorded_data";
	std::ofstream enc_latency;
	// std::vector<int> ratios;
	// std::vector<int> sizes_avg;
	std::ofstream compression_csv;
};

PLUGIN_MAIN(offload_writer)