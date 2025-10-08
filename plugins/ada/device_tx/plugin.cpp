#include "plugin.hpp"

#include <netinet/in.h>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] device_tx::device_tx(const std::string& name_, phonebook* pb_)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()} // make sure you have the right IP address
    , stoplight_{phonebook_->lookup_impl<stoplight>()}
    , ada_writer_{switchboard_->get_network_writer<switchboard::event_wrapper<std::string>>(
        "ada_data",
        network::topic_config{.latency              = std::chrono::milliseconds(0),
            .serialization_method = network::topic_config::SerializationMethod::PROTOBUF})} {
    if (!std::filesystem::exists(data_path_)) {
        if (!std::filesystem::create_directory(data_path_)) {
            spdlog::get("illixr")->error("Failed to create data directory.");
        }
    }

    starting_timestamp_.open(data_path_ + "/starting_timestamp_.csv");
    sending_timestamp_.open(data_path_ + "/sending_timestamp_.csv");
    frame_send_timing_.open(data_path_ + "/frame_send_latency.csv");

    if (!starting_timestamp_ || !sending_timestamp_ || !frame_send_timing_) {
        spdlog::get("illixr")->error("[device_tx] Failed to open one or more log files.");
    }

    const char* env_value   = std::getenv("FRAME_COUNT");
    const char* env_value_1 = std::getenv("FPS");

    if (env_value != nullptr && env_value_1 != nullptr) {
        try {
            frame_count_ = std::stoul(env_value);
            fps_         = std::stoul(env_value_1);
        } catch (...) {
            spdlog::get("illixr")->error("device_tx: invalid FRAME_COUNT/FPS; falling back to {} / {}", frame_count_, fps_);
        }
    } else {
        spdlog::get("illixr")->error("device_tx: FRAME_COUNT or FPS not set; using defaults {} / {}", frame_count_, fps_);
    }
    spdlog::get("illixr")->info("device_tx: FRAME_COUNT {} /FPS {}", frame_count_, fps_);
}

void device_tx::start() {
    threadloop::start();

#ifdef USE_NVIDIA_CODEC
    encoder_ = std::make_unique<ada_video_encoder>(
        // First callback for img0 and img1
        [this](const GstMapInfo& img0, const GstMapInfo& img1) {
            std::lock_guard<std::mutex> lock{mutex_};
            msb_bytes_.assign(reinterpret_cast<const char*>(img0.data), img0.size);
            lsb_bytes_.assign(reinterpret_cast<const char*>(img1.data), img1.size);
            img_ready_ = true;
            cond_var_.notify_one();
        });
    encoder_->init();
#else
    encoder_ = std::make_unique<encoding::rgb_encoder>();
#endif
    switchboard_->schedule<scene_recon_type>(id_, "ScanNet_Data",
                                             [this](switchboard::ptr<const scene_recon_type> datum, std::size_t) {
                                                 this->send_scene_recon_data(datum);
                                             });
}

void device_tx::_p_one_iteration() {
    while (!stoplight_->check_should_stop()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void device_tx::send_scene_recon_data(switchboard::ptr<const scene_recon_type> datum) {
    auto t0          = std::chrono::high_resolution_clock::now();
    auto since_epoch = t0.time_since_epoch();
    auto millis      = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
    starting_timestamp_ << frame_id_ << " " << millis << "\n";

    if (frame_id_ == 0) {
        spdlog::get("illixr")->info("[device_tx] Start sending encoded packets");
    }

    // Build Payload
    pose = outgoing_payload->mutable_input_pose();

    // pyh assign outgoing pose & image information
    pose->set_p_x(datum->pose.position.x());
    pose->set_p_y(datum->pose.position.y());
    pose->set_p_z(datum->pose.position.z());

    pose->set_o_x(datum->pose.orientation.x());
    pose->set_o_y(datum->pose.orientation.y());
    pose->set_o_z(datum->pose.orientation.z());
    pose->set_o_w(datum->pose.orientation.w());

    // Depth
    cv::Mat cur_depth = datum->depth;

    sr_input_proto::ImgData* depth_img_msb = outgoing_payload->mutable_depth_img_msb_data();
#ifdef USE_NVIDIA_CODEC
    sr_input_proto::ImgData* depth_img_lsb = outgoing_payload->mutable_depth_img_lsb_data();

    depth_img_msb->set_rows(cur_depth.rows);
    depth_img_msb->set_columns(cur_depth.cols);
    depth_img_lsb->set_rows(cur_depth.rows);
    depth_img_lsb->set_columns(cur_depth.cols);

    cv::Mat depth_msb, depth_lsb;

    auto decompose_start = std::chrono::high_resolution_clock::now();

    decompose_16bit_to_8bit(cur_depth, depth_msb, depth_lsb);

    auto decompose_end      = std::chrono::high_resolution_clock::now();
    auto duration_decompose = std::chrono::duration_cast<std::chrono::microseconds>(decompose_end - decompose_start).count();
    frame_send_timing_ << "Decompose " << (static_cast<double>(duration_decompose) / 1000.0) << "\n";

    auto depth_encoding_start = std::chrono::high_resolution_clock::now();
    {
        std::lock_guard<std::mutex> lk{mutex_};
        img_ready_ = false;
    }

    encoder_->enqueue(depth_msb, depth_lsb);

    {
        std::unique_lock<std::mutex> lock{mutex_};
        cond_var_.wait(lock, [this]() {
            return img_ready_;
        });

        depth_img_msb->mutable_img_data()->swap(msb_bytes_);
        depth_img_lsb->mutable_img_data()->swap(lsb_bytes_);
        depth_img_msb->set_size(static_cast<int64_t>(depth_img_msb->img_data().size()));
        depth_img_lsb->set_size(static_cast<int64_t>(depth_img_lsb->img_data().size()));
    }

    auto depth_encoding_end = std::chrono::high_resolution_clock::now();
    auto duration_depth_encoding =
        std::chrono::duration_cast<std::chrono::microseconds>(depth_encoding_end - depth_encoding_start).count();
    frame_send_timing_ << "Encode " << (static_cast<double>(duration_depth_encoding) / 1000.0) << " " << depth_img_msb->size()
                       << " " << depth_img_lsb->size() << "\n";

#else
    depth_img_msb->set_rows(cur_depth.rows);
    depth_img_msb->set_columns(cur_depth.cols);
    cv::Mat rgb;
    float zmin, zmax;
    encoder_->depth2rgb(cur_depth, rgb, zmin, zmax);
    msb_bytes_.assign(reinterpret_cast<const char*>(rgb.data), cur_depth.rows * cur_depth.cols);
    depth_img_msb->mutable_img_data()->swap(msb_bytes_);
    depth_img_msb->set_size(static_cast<int64_t>(depth_img_msb->img_data().size()));
    outgoing_payload->set_zmin(zmin);
    outgoing_payload->set_zmax(zmax);
#endif

    outgoing_payload->set_id(static_cast<int>(frame_id_));

    // serialize protobuf directly into place (no temporary std::string)
    send_buf_ = outgoing_payload->SerializeAsString();

    // send
    ada_writer_.put(std::make_shared<switchboard::event_wrapper<std::string>>(send_buf_ + delimiter));
    spdlog::get("illixr")->debug("Pose of frame {}: {}, {}, {}; {}, {}, {}, {}", frame_id_, outgoing_payload->input_pose().p_x(),
                                 outgoing_payload->input_pose().p_y(), outgoing_payload->input_pose().p_z(),
                                 outgoing_payload->input_pose().o_w(), outgoing_payload->input_pose().o_x(),
                                 outgoing_payload->input_pose().o_y(), outgoing_payload->input_pose().o_z());

    {
        std::lock_guard<std::mutex> lk{mutex_};
        depth_img_msb->mutable_img_data()->swap(msb_bytes_);
#ifdef USE_NVIDIA_CODEC
        depth_img_lsb->mutable_img_data()->swap(lsb_bytes_);
#endif
    }

    auto fullframe          = std::chrono::high_resolution_clock::now();
    auto fullframe_duration = std::chrono::duration_cast<std::chrono::microseconds>(fullframe - t0).count();
    frame_send_timing_ << "FullFrame " << (static_cast<double>(fullframe_duration) / 1000.0) << " " << send_buf_.size() << "\n";

    if (frame_id_ % fps_ == 0 && frame_id_ > 0) {
        since_epoch = fullframe.time_since_epoch();
        millis      = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count();
        sending_timestamp_ << frame_id_ << " " << millis << "\n";
    }

    if (frame_id_ == frame_count_ - 1) {
        printf("finish sending\n");
        frame_send_timing_.flush();
        sending_timestamp_.flush();
        starting_timestamp_.flush();
    }
    frame_id_++;
    delete outgoing_payload;
    outgoing_payload = new sr_input_proto::SRSendData();
}

void device_tx::decompose_16bit_to_8bit(const cv::Mat& depth16, cv::Mat& out_msb, cv::Mat& out_lsb) {
    CV_Assert(depth16.type() == CV_16U);
    out_msb.create(depth16.size(), CV_8UC1);
    out_lsb.create(depth16.size(), CV_8UC1);

    const int rows = depth16.rows;
    const int cols = depth16.cols;

    for (int y = 0; y < rows; ++y) {
        const auto src = depth16.ptr<uint16_t>(y);
        auto       msb = out_msb.ptr<uint8_t>(y);
        auto       lsb = out_lsb.ptr<uint8_t>(y);
        for (int x = 0; x < cols; ++x) {
            const uint16_t v = src[x];
            msb[x]           = static_cast<uint8_t>(v >> 8);
            lsb[x]           = static_cast<uint8_t>(v & 0xFF);
        }
    }
}

device_tx::~device_tx() {
    delete outgoing_payload;
}

PLUGIN_MAIN(device_tx)
