#include "plugin.hpp"

#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

namespace {
[[maybe_unused]] bool write_16_bit_pgm(const cv::Mat& image, const std::string& filename) {
    // Check if the input image is 16-bit single-channel
    if (image.empty() || image.type() != CV_16U) {
        std::cerr << "Input image must be non-empty and 16-bit single-channel." << std::endl;
        return false;
    }

    std::ofstream file(filename, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    // PGM header for a binary P5 type, 16-bit depth
    file << "P5\n";
    file << image.cols << " " << image.rows << "\n";
    file << "65535\n"; // Maximum value for 16-bit depth

    // Writing the image data
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            uint16_t pixelValue = image.at<uint16_t>(y, x);

            // Write the pixel value as big endian
            char highByte = pixelValue >> 8;
            char lowByte  = pixelValue & 0xFF;
            file.write(&highByte, 1);
            file.write(&lowByte, 1);
        }
    }

    file.close();
    return true;
}
} // namespace

[[maybe_unused]] server_rx::server_rx(const std::string& name_, phonebook* pb_)
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

    /*const int width = 640;
    const int height = 480;
    msb_buf_.create(height, width, CV_8UC1);
    lsb_buf_.create(height, width, CV_8UC1);
    hi16_.create(height, width, CV_16U);
    lo16_.create(height, width, CV_16U);
    depth16_.create(height, width, CV_16U);*/

    cur_frame = 0;

    const char* env_var_name   = "FRAME_COUNT";
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
        auto                       buffer_ptr   = ada_reader_.dequeue();
        std::string                buffer_str   = **buffer_ptr;
        std::string::size_type     end_position = buffer_str.find(delimiter);
        sr_input_proto::SRSendData sr_input;
        bool                       success = sr_input.ParseFromString(buffer_str.substr(0, end_position));
        if (!success) {
            spdlog::get("illixr")->error("Error parsing the protobuf, vio input size = {}",
                                         buffer_str.size() - delimiter.size());
        } else {
            spdlog::get("illixr")->debug("Pose of frame {}: {}, {}, {}; {}, {}, {}, {}", current_frame_no_,
                                         sr_input.input_pose().p_x(), sr_input.input_pose().p_y(), sr_input.input_pose().p_z(),
                                         sr_input.input_pose().o_w(), sr_input.input_pose().o_x(), sr_input.input_pose().o_y(),
                                         sr_input.input_pose().o_z());
            receive_sr_input(sr_input);
        }
    }
}

void server_rx::start() {
#ifdef USE_NVIDIA_CODEC
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
#else
    decoder_ = std::make_unique<encoding::rgb_decoder>();
#endif
    threadloop::start();
}

void server_rx::receive_sr_input(const sr_input_proto::SRSendData& sr_input) {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("Received SR input frame {}/{}", current_frame_no_, frame_count_ - 1);
#endif
    auto start = std::chrono::high_resolution_clock::now();
    if (cur_frame % fps_ == 0 && cur_frame > 0) {
        auto since_epoch = start.time_since_epoch();
        auto millis      = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
        receive_timestamp << cur_frame << " " << millis << "\n";
    }
    Eigen::Vector3f    incoming_position{static_cast<float>(sr_input.input_pose().p_x()),
                                      static_cast<float>(sr_input.input_pose().p_y()),
                                      static_cast<float>(sr_input.input_pose().p_z())};
    Eigen::Quaternionf incoming_orientation{
        static_cast<float>(sr_input.input_pose().o_w()), static_cast<float>(sr_input.input_pose().o_x()),
        static_cast<float>(sr_input.input_pose().o_y()), static_cast<float>(sr_input.input_pose().o_z())};

    pose_type pose = {time_point{}, incoming_position, incoming_orientation};

#ifdef USE_NVIDIA_CODEC
    auto msb = const_cast<std::string&>(sr_input.depth_img_msb_data().img_data());
    auto lsb = const_cast<std::string&>(sr_input.depth_img_lsb_data().img_data());

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
    double duration_depth_decoding_ms = static_cast<double>(duration_depth_decoding) / 1000.0;
    // printf("DepthDecode %.3f\n", duration_depth_decoding_ms);
    receive_time << "DepthDecode " << cur_frame << " " << duration_depth_decoding_ms << "\n";

    auto combine_start = std::chrono::high_resolution_clock::now();

    msb_buf_.convertTo(hi16_, CV_16U, 256.0);
    lsb_buf_.convertTo(lo16_, CV_16U);

    cv::bitwise_or(hi16_, lo16_, depth16_);
    auto   combine_end         = std::chrono::high_resolution_clock::now();
    auto   duration_combine    = std::chrono::duration_cast<std::chrono::microseconds>(combine_end - combine_start).count();
    double duration_combine_ms = static_cast<double>(duration_combine) / 1000.0;
    receive_time << "Combine " << cur_frame << " " << duration_combine_ms << "\n";
#else
    auto    msb = const_cast<std::string&>(sr_input.depth_img_msb_data().img_data());
    cv::Mat encoded_rgb(sr_input.depth_img_msb_data().rows(), sr_input.depth_img_msb_data().columns(), CV_8UC3, msb.data());
    float   zmin = sr_input.zmax();
    float   zmax = sr_input.zmax();
    cv::Mat temp_depth;
    decoder_->rgb2depth(encoded_rgb, temp_depth, zmin, zmax);
    temp_depth.convertTo(depth16_, CV_16U);
#endif
    std::string depth_str = std::to_string(cur_frame) + ".pgm";
    write_16_bit_pgm(depth16_, depth_str);
    cv::Mat rgb; // pyh dummy here
    scannet_.put(scannet_.allocate<scene_recon_type>(scene_recon_type{time_point{}, pose, depth16_.clone(), rgb, false}));

    auto end         = std::chrono::high_resolution_clock::now();
    auto duration    = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    auto duration_ms = static_cast<double>(duration) / 1000.0;
    receive_time << "Full " << cur_frame << " " << duration_ms << "\n";
    cur_frame++;
    if (cur_frame == frame_count_) {
        receive_time.flush();
        receive_size.flush();
        receive_timestamp.flush();
    }
    current_frame_no_++;
}

PLUGIN_MAIN(server_rx)
