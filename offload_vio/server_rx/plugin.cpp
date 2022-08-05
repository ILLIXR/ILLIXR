#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"

#include <boost/lockfree/spsc_queue.hpp>
#include <opencv2/highgui.hpp>
#include <filesystem>
#include <fstream>

#include "vio_input.pb.h"
#include "video_decoder.h"

#include "common/network/socket.hpp"
#include "common/network/net_config.hpp"
#include "common/network/timestamp.hpp"

using namespace ILLIXR;

class server_reader : public threadloop {
private:
    std::unique_ptr<video_decoder> decoder;

    boost::lockfree::spsc_queue<uint64_t> queue {1000};
    std::mutex mutex;
    std::condition_variable cv;
    cv::Mat img0;
    cv::Mat img1;
    bool img_ready = false;
public:
	server_reader(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_imu_cam{sb->get_writer<imu_cam_type_prof>("imu_cam")}
		, _conn_signal{sb->get_writer<connection_signal>("connection_signal")}
		, server_addr(SERVER_IP, SERVER_PORT_1)
		, buffer_str("")
    { 
		if (!filesystem::exists(data_path)) {
			if (!std::filesystem::create_directory(data_path)) {
				std::cerr << "Failed to create data directory.";
			}
		}
		
		receive_time.open(data_path + "/receive_time.csv");
		hashed_data.open(data_path + "/hash_server_rx.txt");
		dec_latency.open(data_path + "/dec.csv");
		socket.set_reuseaddr();
		socket.bind(server_addr);
	}

	virtual skip_option _p_should_skip() override {
		return skip_option::run;
    }

	void _p_one_iteration() override {
		if (read_socket == NULL) {
			_conn_signal.put(_conn_signal.allocate<connection_signal>(
				connection_signal{true}
			));
			socket.listen();
			cout << "server_rx: Waiting for connection!" << endl;
			read_socket = new TCPSocket( FileDescriptor( SystemCall( "accept", ::accept( socket.fd_num(), nullptr, nullptr) ) ) ); /* Blocking operation, waiting for client to connect */
			cout << "server_rx: Connection is established with " << read_socket->peer_address().str(":") << endl;
		} else {
			auto now = timestamp();
			string delimitter = "END!";
			string recv_data = read_socket->read(); /* Blocking operation, wait for the data to come */
			buffer_str = buffer_str + recv_data;
			if (recv_data.size() > 0) {
				string::size_type end_position = buffer_str.find(delimitter);
				while (end_position != string::npos) {
					string before = buffer_str.substr(0, end_position);
					buffer_str = buffer_str.substr(end_position + delimitter.size());
					// cout << "Complete response = " << before.size() << endl;
					// process the data
					vio_input_proto::IMUCamVec vio_input;
					bool success = vio_input.ParseFromString(before);
					if (!success) {
						cout << "Error parsing the protobuf, vio input size = " << before.size() << endl;
					} else {
						// cout << "Received the protobuf data!" << endl;
						// hash<std::string> hasher;
						// auto hash_result = hasher(before);
						// hashed_data << vio_input.frame_id() << "\t" << hash_result << endl;
						cout << "Receive frame id = " << vio_input.frame_id() << endl;
						ReceiveVioInput(vio_input);
					}
					end_position = buffer_str.find(delimitter);
				}
				// cout << "Recv time = " << timestamp() - now << ", size = " << recv_data.size() << endl;
			}
		}
	}

	~server_reader() {
		delete read_socket;
	}

    void start() override {
        threadloop::start();

        decoder = std::make_unique<video_decoder>([this](cv::Mat&& img0, cv::Mat&& img1) {
            std::cout << "callback" << std::endl;

            // show img0
            // cv::imshow("img0", img0);
            // cv::waitKey(1);

            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << "=== latency: " << (curr - timestamp) / 1000000.0 << std::endl;
            });
            {
                std::lock_guard<std::mutex> lock{mutex};
                this->img0 = std::forward<cv::Mat>(img0);
                this->img1 = std::forward<cv::Mat>(img1);
                img_ready = true;
            }
            std::cout << "notify" << std::endl;
            cv.notify_one();
        });
        decoder->init();
    }

private:
	void ReceiveVioInput(const vio_input_proto::IMUCamVec& vio_input) {
        std::cout << "Received VIO input" << std::endl;	

		// Logging
		unsigned long long curr_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		double sec_to_trans = (curr_time - vio_input.real_timestamp()) / 1e9;
		// receive_time << vio_input.frame_id() << "," << vio_input.real_timestamp() << "," << sec_to_trans * 1e3 << std::endl;
		receive_time << vio_input.frame_id() << "," << vio_input.cam_time() << "," << sec_to_trans * 1e3 << std::endl;

		// Loop through all IMU values first then the cam frame	
		for (int i = 0; i < vio_input.imu_cam_data_size(); i++) {
			vio_input_proto::IMUCamData curr_data = vio_input.imu_cam_data(i);

			std::optional<cv::Mat> cam0 = std::nullopt;
			std::optional<cv::Mat> cam1 = std::nullopt;

			if (curr_data.img0_size() != -1 && curr_data.img1_size() != -1) {

				// Must do a deep copy of the received data (in the form of a string of bytes)
				auto img0_copy = std::string(curr_data.img0_data());
				auto img1_copy = std::string(curr_data.img1_data());

                std::cout << "img0 size: " << curr_data.img0_size() << std::endl;

				auto start_dcmp = timestamp();
                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                queue.push(curr);
                std::unique_lock<std::mutex> lock{mutex};
                decoder->enqueue(img0_copy, img1_copy);
                cv.wait(lock, [this]() { return img_ready; });
                img_ready = false;
				dec_latency << vio_input.frame_id() << "," << vio_input.cam_time() << "," << timestamp() - start_dcmp << std::endl;

//				cv::Mat img0(curr_data.rows(), curr_data.cols(), CV_8UC1, img0_copy->data());
//				cv::Mat img1(curr_data.rows(), curr_data.cols(), CV_8UC1, img1_copy->data());

                cam0 = std::make_optional<cv::Mat>(img0.clone());
                cam1 = std::make_optional<cv::Mat>(img1.clone());

                std::cout << "unlock" << std::endl;
                lock.unlock();
			}

			_m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type_prof>(
				imu_cam_type_prof {
					vio_input.frame_id(),
					time_point{std::chrono::nanoseconds{curr_data.timestamp()}},
					time_point{std::chrono::nanoseconds{vio_input.real_timestamp()}}, // Timestamp of when the device sent the packet
					time_point{std::chrono::nanoseconds{curr_time}}, // Timestamp of receive time of the packet
					time_point{std::chrono::nanoseconds{vio_input.dataset_timestamp()}}, // Timestamp of the sensor data
					0,
					Eigen::Vector3f{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
					Eigen::Vector3f{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()},
					cam0,
					cam1
				}
			));	
		}

		// unsigned long long after_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		// double sec_to_push = (after_time - curr_time) / 1e9;
		// std::cout << vio_input.frame_id() << ": Seconds to push data (ms): " << sec_to_push * 1e3 << std::endl;
	}

private:

    const std::shared_ptr<switchboard> sb;
	switchboard::writer<imu_cam_type_prof> _m_imu_cam;
	switchboard::writer<connection_signal> _conn_signal;

	TCPSocket socket;
	TCPSocket * read_socket = NULL;
	Address server_addr;
	string buffer_str;

	const std::string data_path = filesystem::current_path().string() + "/recorded_data";
	std::ofstream receive_time;
	std::ofstream hashed_data;
	std::ofstream dec_latency;
};

PLUGIN_MAIN(server_reader)