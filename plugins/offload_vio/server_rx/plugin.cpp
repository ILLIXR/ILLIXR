#include "plugin.hpp"

#include "illixr/network/net_config.hpp"
#include "video_decoder.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// #define USE_COMPRESSION

[[maybe_unused]] server_reader::server_reader(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , imu_{switchboard_->get_writer<imu_type>("imu")}
    , cam_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , imu_cam_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("compressed_imu_cam")}
    , log_(spdlogger(switchboard_->get_env_char("OFFLOAD_VIO_LOG_LEVEL"))) {
    log_->info("Camera Time,Uplink Time(ms)");
}

ILLIXR::threadloop::skip_option server_reader::_p_should_skip() {
    return skip_option::run;
}

void server_reader::_p_one_iteration() {
    if (imu_cam_reader_.size() > 0) {
        auto                       buffer_ptr   = imu_cam_reader_.dequeue();
        std::string                buffer_str   = **buffer_ptr;
        std::string::size_type     end_position = buffer_str.find(delimiter_);
        vio_input_proto::IMUCamVec vio_input;
        bool                       success = vio_input.ParseFromString(buffer_str.substr(0, end_position));
        if (!success) {
            log_->error("[offload_vio.server_rx]Error parsing the protobuf, vio input size = {}",
                        buffer_str.size() - delimiter_.size());
        } else {
            receive_vio_input(vio_input);
        }
    }
}

void server_reader::start() {
    threadloop::start();

    decoder_ = std::make_unique<vio_video_decoder>([this](cv::Mat&& img0, cv::Mat&& img1) {
        queue_.consume_one([&](uint64_t& timestamp) {
            (void) timestamp;
            uint64_t curr =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            // std::cout << "=== latency: " << (curr - timestamp) / 1000000.0 << std::endl;
        });
        {
            std::lock_guard<std::mutex> lock{mutex_};
            this->img0_dst_ = std::forward<cv::Mat>(img0);
            this->img1_dst_ = std::forward<cv::Mat>(img1);
            img_ready_      = true;
        }
        condition_variable_.notify_one();
    });
    decoder_->init();
}

void server_reader::receive_vio_input(const vio_input_proto::IMUCamVec& vio_input) {
    // Logging the transmitting time
    unsigned long long curr_time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    double msec_to_trans = (curr_time - vio_input.real_timestamp()) / 1e6;

    // Loop through and publish all IMU values first
    for (int i = 0; i < vio_input.imu_data_size() - 1; i++) {
        vio_input_proto::IMUData curr_data = vio_input.imu_data(i);
        imu_.put(imu_.allocate<imu_type>(imu_type{
            time_point{std::chrono::nanoseconds{curr_data.timestamp()}},
            Eigen::Vector3d{curr_data.angular_vel().x(), curr_data.angular_vel().y(), curr_data.angular_vel().z()},
            Eigen::Vector3d{curr_data.linear_accel().x(), curr_data.linear_accel().y(), curr_data.linear_accel().z()}}));
    }
    // Publish the Cam value then
    vio_input_proto::CamData cam_data = vio_input.cam_data();
    log_->info("{},{}", cam_data.timestamp(), msec_to_trans);

    // Must do a deep copy of the received data (in the form of a string of bytes)
    auto img0_copy = std::string(cam_data.img0_data());
    auto img1_copy = std::string(cam_data.img1_data());

#ifdef USE_COMPRESSION
    time_point start_decomp = clock_->now();
    // With compression
    uint64_t curr =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    queue_.push(curr);
    std::unique_lock<std::mutex> lock{mutex_};
    decoder_->enqueue(img0_copy, img1_copy);
    condition_variable_.wait(lock, [this]() {
        return img_ready_;
    });
    img_ready_ = false;

    cv::Mat img0(img0_dst_.clone());
    cv::Mat img1(img1_dst_.clone());

    lock.unlock();
    log_->warn("{},{}", cam_data.timestamp(), (clock_->now() - start_decomp).count() / 1e6);
    // With compression end
#else
    // Without compression
    cv::Mat img0(cam_data.rows(), cam_data.cols(), CV_8UC1, img0_copy.data());
    cv::Mat img1(cam_data.rows(), cam_data.cols(), CV_8UC1, img1_copy.data());
    // Without compression end
#endif
    cam_.put(cam_.allocate<binocular_cam_type>(binocular_cam_type{
        time_point{std::chrono::nanoseconds{cam_data.timestamp()}},
        img0.clone(),
        img1.clone(),
    }));
    // If we publish all IMU samples before the camera data, the camera data may not be captured in any of the IMU callbacks
    // in the tracking algorithm (e.g. OpenVINS), and has to wait for another camera frame time (until the next packet
    // arrives) to be consumed. Therefore, we publish one (or more) IMU samples after the camera data to make sure that the
    // camera data will be captured.
    vio_input_proto::IMUData last_imu = vio_input.imu_data(vio_input.imu_data_size() - 1);
    imu_.put(imu_.allocate<imu_type>(
        imu_type{time_point{std::chrono::nanoseconds{last_imu.timestamp()}},
                 Eigen::Vector3d{last_imu.angular_vel().x(), last_imu.angular_vel().y(), last_imu.angular_vel().z()},
                 Eigen::Vector3d{last_imu.linear_accel().x(), last_imu.linear_accel().y(), last_imu.linear_accel().z()}}));
}

PLUGIN_MAIN(server_reader)
