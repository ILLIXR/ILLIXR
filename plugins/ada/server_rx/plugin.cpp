#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] server_rx::server_rx(std::string name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , scannet_{switchboard_->get_writer<scene_recon_type>("ScanNet_Data")}
    , ada_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("ada_data")} {
    if (!std::filesystem::exists(data_path)) {
        if (!std::filesystem::create_directory(data_path)) {
            spdlog::get("illixr")->error("Failed to create data directory.");
        }
    }

    receive_time.open(data_path + "/server_receive_time.csv");
    receive_timestamp.open(data_path + "/server_receiving_timestamp.csv");
    receive_size.open(data_path + "/server_receive_size.csv");

    if (!receive_time || !receive_timestamp || !receive_size) {
        spdlog::get("illixr")->error("[server_rx] Failed to open one or more log files.");
    }

    rx_buf_.reserve(1u << 20);
    const int W = 640, H = 480;
    msb_buf_.create(H, W, CV_8UC1);
    lsb_buf_.create(H, W, CV_8UC1);
    hi16_.create(H, W, CV_16U);
    lo16_.create(H, W, CV_16U);
    depth16_.create(H, W, CV_16U);

    cur_frame = 0;

    const char* env_var_name   = "frame_count_";
    const char* env_value      = std::getenv(env_var_name);
    const char* env_var_name_1 = "FPS";
    const char* env_value_1    = std::getenv(env_var_name_1);

    if (env_value != nullptr && env_value_1 != nullptr) {
        try {
            frame_count_ = std::stoul(env_value);
            fps_         = std::stoul(env_value_1);
            std::cout << "device_tx: Frame Count " << frame_count_ << " FPS " << fps_ << std::endl;
        } catch (const std::invalid_argument& e) {
            spdlog::get("illixr")->error("Invalid argument: the environment variable is not a valid unsigned integer.");
        } catch (const std::out_of_range& e) {
            spdlog::get("illixr")->error(
                "Out of range: the value of the environment variable is too large for an unsigned integer.");
        }
    } else {
        spdlog::get("illixr")->error("Environment variable not found.");
    }
}

void server_rx::_p_one_iteration() {
    if (ada_reader_.size() > 0) {
        auto                   buffer_ptr   = ada_reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter);

        sr_input_proto::SRSendData sr_input;
        bool                       success = sr_input.ParseFromString(buffer_str.substr(0, end_position));
        if (!success) {
            spdlog::get("illixr")->error("Error parsing the protobuf, vio input size = {}",
                                         buffer_str.size() - delimiter.size());
        } else {
            receive_sr_input(sr_input);
        }
    }
}

void server_rx::start() {
    decoder_ = std::make_unique<ada_video_decoder>([this](cv::Mat&& img0, cv::Mat&& img1) {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            img0_dst_  = std::move(img0);
            img1_dst_  = std::move(img1);
            img_ready_ = true;
        }
        cond_var_.notify_one();
    });
    decoder_->init();
    threadloop::start();
}

void server_rx::receive_sr_input(const sr_input_proto::SRSendData& sr_input) {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("Received SR input frame {}", frame_count_);
#endif
    auto start = std::chrono::high_resolution_clock::now();
    if (cur_frame % fps_ == 0 && cur_frame > 0) {
        auto sinceEpoch = start.time_since_epoch();
        auto millis     = std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count();
        receive_timestamp << cur_frame << " " << millis << "\n";
    }
    Eigen::Vector3f    incoming_position{static_cast<float>(sr_input.input_pose().p_x()),
                                      static_cast<float>(sr_input.input_pose().p_y()),
                                      static_cast<float>(sr_input.input_pose().p_z())};
    Eigen::Quaternionf incoming_orientation{
        static_cast<float>(sr_input.input_pose().o_w()), static_cast<float>(sr_input.input_pose().o_x()),
        static_cast<float>(sr_input.input_pose().o_y()), static_cast<float>(sr_input.input_pose().o_z())};

    pose_type pose = {time_point{}, incoming_position, incoming_orientation};

    // Must do a deep copy of the received data (in the form of a string of bytes)
    std::string& msb = const_cast<std::string&>(sr_input.depth_img_msb_data().img_data());
    std::string& lsb = const_cast<std::string&>(sr_input.depth_img_lsb_data().img_data());

    receive_size << "MSB " << cur_frame << " " << sr_input.depth_img_msb_data().size() << "\n";
    receive_size << "LSB " << cur_frame << " " << sr_input.depth_img_lsb_data().size() << "\n";

    auto                         depth_decoding_start = std::chrono::high_resolution_clock::now();
    std::unique_lock<std::mutex> lock{mutex_};
    decoder_->enqueue(msb, lsb);
    cond_var_.wait(lock, [this]() {
        return img_ready_;
    });

    img0_dst_.copyTo(msb_buf_);
    img1_dst_.copyTo(lsb_buf_);
    img_ready_ = false;
    lock.unlock();

    auto depth_decoding_end = std::chrono::high_resolution_clock::now();
    auto duration_depth_decoding =
        std::chrono::duration_cast<std::chrono::microseconds>(depth_decoding_end - depth_decoding_start).count();
    double duration_depth_decoding_ms = duration_depth_decoding / 1000.0;
    // printf("DepthDecode %.3f\n", duration_depth_decoding_ms);
    receive_time << "DepthDecode " << cur_frame << " " << duration_depth_decoding_ms << "\n";

    auto combine_start = std::chrono::high_resolution_clock::now();
    msb_buf_.convertTo(hi16_, CV_16U, 256.0);
    lsb_buf_.convertTo(lo16_, CV_16U);
    cv::bitwise_or(hi16_, lo16_, depth16_);
    auto   combine_end         = std::chrono::high_resolution_clock::now();
    auto   duration_combine    = std::chrono::duration_cast<std::chrono::microseconds>(combine_end - combine_start).count();
    double duration_combine_ms = duration_combine / 1000.0;
    receive_time << "Combine " << cur_frame << " " << duration_combine_ms << "\n";

    // pyh unillixr to explicit save the decoded message (for depth image distoration evaluation)
    // std::string depth_str = std::to_string(cur_frame) + ".pgm";
    // std::string depth_str_MSB = std::to_string(cur_frame) + "_depth_MSB.png";
    // std::string depth_str_LSB = std::to_string(cur_frame) + "_depth_LSB.png";
    // cv::imwrite(depth_str_LSB, test_depth_LSB);
    // cv::imwrite(depth_str_MSB, test_depth_MSB);
    // write16BitPGM(test_depth, depth_str);

    cv::Mat rgb; // pyh dummy here
    scannet_.put(scannet_.allocate<scene_recon_type>(scene_recon_type{time_point{}, pose, depth16_.clone(), rgb, false}));

    auto end         = std::chrono::high_resolution_clock::now();
    auto duration    = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    auto duration_ms = duration / 1000.0;
    receive_time << "Full " << cur_frame << " " << duration_ms << "\n";
    cur_frame++;
    if (cur_frame == frame_count_) {
        receive_time.flush();
        receive_size.flush();
        receive_timestamp.flush();
    }
}

PLUGIN_MAIN(server_rx)
