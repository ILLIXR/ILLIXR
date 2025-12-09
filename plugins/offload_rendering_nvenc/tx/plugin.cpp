#include "plugin.hpp"

#include "illixr/quest3_params.hpp"

#include <opencv2/core/mat.hpp>


using namespace ILLIXR;

[[maybe_unused]] rendered_frame_tx::rendered_frame_tx(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , frame_reader_{switchboard_->get_buffered_reader<switchboard::event_wrapper<std::string>>("rendered_frames")}
    , encoded_writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
          "compressed_frame",
          network::topic_config{.serialization_method = network::topic_config::SerializationMethod::PROTOBUF})}
#ifdef USE_COMPRESSION
    , left_encoder_(std::make_unique<nvenc_encoder>(HEADSET_WIDTH, HEADSET_HEIGHT, 15000000))
    , right_encoder_(std::make_unique<nvenc_encoder>(HEADSET_WIDTH, HEADSET_HEIGHT, 15000000))
#else
    , left_encoder_(nullptr)
    , right_encoder_(nullptr)
#endif
{
}

void rendered_frame_tx::_p_one_iteration() {
    if (frame_reader_.size() > 0) {
        auto                   buffer_ptr   = frame_reader_.dequeue();
        std::string            buffer_str   = **buffer_ptr;
        std::string::size_type end_position = buffer_str.find(delimiter_);

        rendered_frame_proto::Frame rendered_frame;
        if (rendered_frame.ParseFromString(buffer_str.substr(0, end_position))) {
            spdlog::get("illixr")->debug("Got Frame {}x{}", rendered_frame.columns(), rendered_frame.rows());
            compress_frame(rendered_frame);
        } else {
            spdlog::get("illixr")->error("Cannot parse rendered frame data");
        }
    }
}

void rendered_frame_tx::compress_frame(const rendered_frame_proto::Frame& frame) {
    auto left_eye_raw  = std::string(frame.left_eye());
    auto right_eye_raw = std::string(frame.right_eye());

    cv::Mat left_eye(frame.rows(), frame.columns(), CV_8UC3, left_eye_raw.data());
    cv::Mat right_eye(frame.rows(), frame.columns(), CV_8UC3, left_eye_raw.data());

    //cv::Mat left_eye;
    //cv::Mat right_eye;
    //cv::resize(left_eye_temp, left_eye, cv::Size(), 0.5, 0.5);
    //cv::resize(right_eye_temp, right_eye, cv::Size(), 0.5, 0.5);
    //cv::cvtColor(left_eye, left_eye, cv::COLOR_RGB2BGR);
    //cv::cvtColor(right_eye, right_eye, cv::COLOR_RGB2BGR);
#ifdef USE_COMPRESSION
    std::unique_lock<std::mutex> lock{mutex_};

    auto future_left  = left_encoder_->encode_async(left_eye);
    auto future_right = right_encoder_->encode_async(right_eye);

    auto left_encoded  = future_left.get();
    auto right_encoded = future_right.get();
    lock.unlock();
    spdlog::get("illixr")->debug("done compress");
#else

#endif
    rendered_frame_proto::CompressedFrame compressed_frame;
    compressed_frame.set_timestamp(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
#ifdef USE_COMPRESSION
    compressed_frame.set_left_eye((void*) left_encoded.data(), left_encoded.size());
    compressed_frame.set_left_eye_size(left_encoded.size());
    compressed_frame.set_right_eye((void*) right_encoded.data(), right_encoded.size());
    compressed_frame.set_right_eye_size(right_encoded.size());
#else
    compressed_frame.set_left_eye((void*) left_eye.data, left_eye.rows * left_eye.cols);
    compressed_frame.set_right_eye((void*) right_eye.data, right_eye.rows * right_eye.cols);
#endif
    compressed_frame.set_rows(left_eye.rows);
    compressed_frame.set_columns(left_eye.cols);
    std::string data_buffer = compressed_frame.SerializeAsString();
    count++;
    encoded_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(data_buffer + delimiter_));
}

ILLIXR::threadloop::skip_option rendered_frame_tx::_p_should_skip() {
    return skip_option::run;
}

PLUGIN_MAIN(rendered_frame_tx)
