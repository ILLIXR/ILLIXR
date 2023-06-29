#include "common/threadloop.hpp"
#include "common/stoplight.hpp"
#include "common/plugin.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/switchboard.hpp"

#include <opencv/cv.hpp>
#include <opencv2/core/mat.hpp>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cstdlib>

#include "video_encoder.h"
#include <boost/lockfree/spsc_queue.hpp>

#include "vio_input.pb.h"
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
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
		, server_addr(SERVER_IP, SERVER_PORT_1)
    {
		if (!filesystem::exists(data_path)) {
			if (!filesystem::create_directory(data_path)) {
				std::cerr << "Failed to create data directory.";
			}
		}

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

        sb->schedule<imu_type>(id, "imu", [this](switchboard::ptr<const imu_type> datum, std::size_t) {
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
    }

public:

    void send_imu_cam_data(switchboard::ptr<const imu_type> datum) {
		// Ensures that slam doesnt start before valid IMU readings come in
        if (datum == nullptr) {
            assert(previous_timestamp == 0);
            return;
        }

		assert(datum->time.time_since_epoch().count() > previous_timestamp);
		previous_timestamp = datum->time.time_since_epoch().count();
        latest_imu_time = datum->time;

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

        switchboard::ptr<const cam_type> cam;

        if (_m_cam.size() != 0 && !has_cam) {
            cam = _m_cam.dequeue();

			cv::Mat img0 = (cam->img0.value()).clone();
			cv::Mat img1 = (cam->img1.value()).clone();

            // size of img0
            double img0_size = img0.total() * img0.elemSize();

			/** WITH COMPRESSION **/
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
                // For debugging, prints out average image size after compression and compression ratio
                // std::cout << "compression ratio: " << img0_size / (sum / sizes.size()) << " average size after compression " << sum / sizes.size() << std::endl;
            }

			imu_cam_data->set_rows(img0.rows);
			imu_cam_data->set_cols(img0.cols);

            lock.unlock();
			/** WITH COMPRESSION END **/

			/** NO COMPRESSION **/
			// imu_cam_data->set_img0_size(img0_size);
			// imu_cam_data->set_img1_size(img0_size);

			// imu_cam_data->set_img0_data((void*) img0.data, img0_size);
			// imu_cam_data->set_img1_data((void*) img1.data, img0_size);
			/** No compression END **/

			data_buffer->set_real_timestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			data_buffer->set_dataset_timestamp(datum->dataset_time.time_since_epoch().count());
			data_buffer->set_frame_id(frame_id);
			
			if (false) {
				std::cout << "dropping " << drop_count++ << "\n";
			} else {				
				// Prepare data delivery
				string data_to_be_sent = data_buffer->SerializeAsString();
				string delimitter = "END!";

				socket.write(data_to_be_sent + delimitter);
			}
			frame_id++;
			delete data_buffer;
			data_buffer = new vio_input_proto::IMUCamVec();
		} else {
            imu_cam_data->set_rows(-1);
			imu_cam_data->set_cols(-1);
        }
		
    }

private:
    std::unique_ptr<video_encoder> encoder = nullptr;
	long previous_timestamp = 0;
    time_point latest_imu_time;
	int frame_id = 0;
	vio_input_proto::IMUCamVec* data_buffer = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard> sb;
	const std::shared_ptr<RelativeClock> _m_clock;
    switchboard::buffered_reader<cam_type> _m_cam;

	TCPSocket socket;
	Address server_addr;

	const string data_path = filesystem::current_path().string() + "/recorded_data";
};

PLUGIN_MAIN(offload_writer)
