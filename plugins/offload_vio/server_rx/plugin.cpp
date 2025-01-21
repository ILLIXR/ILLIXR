#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/network/timestamp.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "video_decoder.h"
#include "vio_input.pb.h"

#include <boost/lockfree/spsc_queue.hpp>

using namespace ILLIXR;

#define USE_COMPRESSION

class server_reader : public threadloop {
private:
    std::unique_ptr<video_decoder> decoder;

    boost::lockfree::spsc_queue<uint64_t> queue{1000};
    std::mutex                            mutex;
    std::condition_variable               cv;
    cv::Mat                               img0_dst;
    cv::Mat                               img1_dst;
    bool                                  img_ready = false;
    std::shared_ptr<spdlog::logger>       log;

public:
    server_reader(std::string name_, phonebook* pb_)
        : threadloop{std::move(name_), pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_imu{sb->get_writer<imu_type>("imu")}
        , _m_cam{sb->get_writer<cam_type>("cam")}
        , _m_imu_cam_reader{sb->get_buffered_reader<switchboard::event_wrapper<std::string>>("compressed_imu_cam")}
        , buffer_str("")
        , log(spdlogger(std::getenv("OFFLOAD_VIO_LOG_LEVEL"))) {
        log->info("Camera Time,Uplink Time(ms)");
    }

    virtual skip_option _p_should_skip() override {
        return skip_option::run;
    }

    void _p_one_iteration() override {
        if (_m_imu_cam_reader.size() > 0) {
            auto                       buffer_ptr   = _m_imu_cam_reader.dequeue();
            std::string                buffer_str   = **buffer_ptr;
            std::string::size_type     end_position = buffer_str.find(delimitter);
            vio_input_proto::IMUCamVec vio_input;
            bool                       success = vio_input.ParseFromString(buffer_str.substr(0, end_position));
            if (!success) {
                log->error("[offload_vio.server_rx]Error parsing the protobuf, vio input size = {}",
                           buffer_str.size() - delimitter.size());
            } else {
                ReceiveVioInput(vio_input);
            }
        }
    }

    ~server_reader() {
        delete read_socket;
    }

    void start() override {
        threadloop::start();

        decoder = std::make_unique<video_decoder>([this](cv::Mat&& img0, cv::Mat&& img1) {
            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                        .count();
                // std::cout << "=== latency: " << (curr - timestamp) / 1000000.0 << std::endl;
            });
            {
                std::lock_guard<std::mutex> lock{mutex};
                this->img0_dst = std::forward<cv::Mat>(img0);
                this->img1_dst = std::forward<cv::Mat>(img1);
                img_ready      = true;
            }
            cv.notify_one();
        });
        decoder->init();
    }

private:
    void ReceiveVioInput(const vio_input_proto::IMUCamVec& vio_input) {
        // Logging the transmitting time
        unsigned long long curr_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        double msec_to_trans = (curr_time - vio_input.real_timestamp()) / 1e6;

        // Loop through and publish all IMU values first
        for (int i = 0; i < vio_input.imu_data_size() - 1; i++) {
            vio_input_proto::IMUData curr_data = vio_input.imu_data(i);
            _m_imu.put(_m_imu.allocate<imu_type>(imu_type{
                time_point{std::chrono::nanoseconds{curr_data.timestamp()}},
                Eigen::Vector3d{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
                Eigen::Vector3d{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()}}));
        }
        // Publish the Cam value then
        vio_input_proto::CamData cam_data = vio_input.cam_data();
        log->info("{},{}", cam_data.timestamp(), msec_to_trans);

        // Must do a deep copy of the received data (in the form of a string of bytes)
        auto img0_copy = std::string(cam_data.img0_data());
        auto img1_copy = std::string(cam_data.img1_data());

#ifdef USE_COMPRESSION
        time_point start_decomp = _m_clock->now();
        // With compression
        uint64_t curr =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        queue.push(curr);
        std::unique_lock<std::mutex> lock{mutex};
        decoder->enqueue(img0_copy, img1_copy);
        cv.wait(lock, [this]() {
            return img_ready;
        });
        img_ready = false;

        cv::Mat img0(img0_dst.clone());
        cv::Mat img1(img1_dst.clone());

        lock.unlock();
        log->warn("{},{}", cam_data.timestamp(), (_m_clock->now() - start_decomp).count() / 1e6);
        // With compression end
#else
        // Without compression
        cv::Mat img0(cam_data.rows(), cam_data.cols(), CV_8UC1, img0_copy.data());
        cv::Mat img1(cam_data.rows(), cam_data.cols(), CV_8UC1, img1_copy.data());
        // Without compression end
#endif
        _m_cam.put(_m_cam.allocate<cam_type>(cam_type{
            time_point{std::chrono::nanoseconds{cam_data.timestamp()}},
            img0.clone(),
            img1.clone(),
        }));
        // If we publish all IMU samples before the camera data, the camera data may not be captured in any of the IMU callbacks
        // in the tracking algorithm (e.g. OpenVINS), and has to wait for another camera frame time (until the next packet
        // arrives) to be consumed. Therefore, we publish one (or more) IMU samples after the camera data to make sure that the
        // camera data will be captured.
        vio_input_proto::IMUData last_imu = vio_input.imu_data(vio_input.imu_data_size() - 1);
        _m_imu.put(_m_imu.allocate<imu_type>(
            imu_type{time_point{std::chrono::nanoseconds{last_imu.timestamp()}},
                     Eigen::Vector3d{last_imu.angular_vel().x(), last_imu.angular_vel().y(), last_imu.angular_vel().z()},
                     Eigen::Vector3d{last_imu.linear_accel().x(), last_imu.linear_accel().y(), last_imu.linear_accel().z()}}));
    }

private:
    const std::shared_ptr<switchboard>                                    sb;
    const std::shared_ptr<RelativeClock>                                  _m_clock;
    switchboard::writer<imu_type>                                         _m_imu;
    switchboard::writer<cam_type>                                         _m_cam;
    switchboard::buffered_reader<switchboard::event_wrapper<std::string>> _m_imu_cam_reader;

    TCPSocket   socket;
    TCPSocket*  read_socket = NULL;
    Address     server_addr;
    std::string buffer_str;
    std::string delimitter = "EEND!";
};

PLUGIN_MAIN(server_reader)
