#include "common/threadloop.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/switchboard.hpp"
#include "vio_input.pb.h"

#include <ecal/ecal.h>
#include <ecal/msg/protobuf/subscriber.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <opencv2/highgui.hpp>

#include "video_decoder.h"

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
    { 
		eCAL::Initialize(0, NULL, "VIO Server Reader");
		subscriber = eCAL::protobuf::CSubscriber<vio_input_proto::IMUCamVec>("vio_input");
		subscriber.AddReceiveCallback(std::bind(&server_reader::ReceiveVioInput, this, std::placeholders::_2));
	}

    void start() override {
        threadloop::start();

        decoder = std::make_unique<video_decoder>([this](cv::Mat&& img0, cv::Mat&& img1) {
            std::cout << "callback" << std::endl;

            // show img0
            cv::imshow("img0", img0);
            cv::waitKey(1);

            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                std::cout << "=== latency: " << curr - timestamp << std::endl;
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
		unsigned long long curr_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		double sec_to_trans = (curr_time - vio_input.real_timestamp()) / 1e9;
		std::cout << vio_input.frame_id() << ": Seconds to transfer frame + IMU (ms): " << sec_to_trans * 1e3 << std::endl;

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

                uint64_t curr = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                queue.push(curr);
                std::unique_lock<std::mutex> lock{mutex};
                decoder->enqueue(img0_copy, img1_copy);
                cv.wait(lock, [this]() { return img_ready; });
                img_ready = false;

//				cv::Mat img0(curr_data.rows(), curr_data.cols(), CV_8UC1, img0_copy->data());
//				cv::Mat img1(curr_data.rows(), curr_data.cols(), CV_8UC1, img1_copy->data());

                cam0 = std::make_optional<cv::Mat>(std::move(img0));
                cam1 = std::make_optional<cv::Mat>(std::move(img1));

                std::cout << "unlock" << std::endl;
                lock.unlock();
			}

			_m_imu_cam.put(_m_imu_cam.allocate<imu_cam_type_prof>(
				imu_cam_type_prof {
					vio_input.frame_id(),
					time_point{std::chrono::nanoseconds{curr_data.timestamp()}},
					time_point{std::chrono::nanoseconds{vio_input.real_timestamp()}}, // Timestamp of when the device sent the packet
					time_point{std::chrono::nanoseconds{curr_time}}, // Timestamp of receive time of the packet
					Eigen::Vector3f{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
					Eigen::Vector3f{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()},
					cam0,
					cam1
				}
			));	
		}

		unsigned long long after_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		double sec_to_push = (after_time - curr_time) / 1e9;
		std::cout << vio_input.frame_id() << ": Seconds to push data (ms): " << sec_to_push * 1e3 << std::endl;
	}

protected:
    skip_option _p_should_skip() override {
        return ILLIXR::threadloop::skip_option::skip_and_yield;
    }

    void _p_one_iteration() override {

    }

private:

    const std::shared_ptr<switchboard> sb;
	switchboard::writer<imu_cam_type_prof> _m_imu_cam;

    eCAL::protobuf::CSubscriber<vio_input_proto::IMUCamVec> subscriber;
};

PLUGIN_MAIN(server_reader)
