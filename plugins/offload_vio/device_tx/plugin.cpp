#include "illixr/data_format.hpp"
#include "illixr/network/net_config.hpp"
#include "illixr/network/socket.hpp"
#include "illixr/network/timestamp.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/stoplight.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#include "video_encoder.h"
#include "vio_input.pb.h"

#include <boost/lockfree/spsc_queue.hpp>
#include <cassert>
#include <opencv2/core/mat.hpp>
#include <utility>

using namespace ILLIXR;

// #define USE_COMPRESSION

class offload_writer : public threadloop {
private:
    boost::lockfree::spsc_queue<uint64_t> queue{1000};
    std::vector<int32_t>                  sizes;
    std::mutex                            mutex;
    std::condition_variable               cv;
    GstMapInfo                            img0;
    GstMapInfo                            img1;
    bool                                  img_ready = false;

public:
    offload_writer(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_stoplight{pb->lookup_impl<Stoplight>()}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , server_addr(SERVER_IP, SERVER_PORT_1) {
        spdlogger(std::getenv("OFFLOAD_VIO_LOG_LEVEL"));
        socket.set_reuseaddr();
        socket.bind(Address(CLIENT_IP, CLIENT_PORT_1));
        socket.enable_no_delay();
        initial_timestamp();

        std::srand(std::time(0));
    }

    void start() override {
        threadloop::start();

        encoder = std::make_unique<video_encoder>([this](const GstMapInfo& img0, const GstMapInfo& img1) {
            queue.consume_one([&](uint64_t& timestamp) {
                uint64_t curr =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                        .count();
            });
            {
                std::lock_guard<std::mutex> lock{mutex};
                this->img0 = img0;
                this->img1 = img1;
                img_ready  = true;
            }
            cv.notify_one();
        });
        encoder->init();

#ifndef NDEBUG
        spdlog::get(name)->debug("[offload_vio.revice_tx] TEST: Connecting to {}", server_addr.str(":"));
#endif
        socket.connect(server_addr);
#ifndef NDEBUG
        spdlog::get(name)->debug("[offload_vio.revice_tx] Connected to {}", server_addr.str(":"));
#endif

        sb->schedule<imu_type>(id, "imu", [this](const switchboard::ptr<const imu_type>& datum, std::size_t) {
            this->prepare_imu_cam_data(datum);
        });
    }

protected:
    void _p_thread_setup() override { }

    // TODO not the best way to use threadloop and stoplight
    void _p_one_iteration() override {
        while (!_m_stoplight->check_should_stop()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }

public:
    void send_imu_cam_data(std::optional<time_point>& cam_time) {
        data_buffer->set_real_timestamp(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        data_buffer->set_frame_id(frame_id);

        std::string data_to_be_sent = data_buffer->SerializeAsString();
        std::string delimitter      = "EEND!";

        socket.write(data_to_be_sent + delimitter);

        frame_id++;
        delete data_buffer;
        data_buffer = new vio_input_proto::IMUCamVec();
        cam_time.reset();
    }

    void prepare_imu_cam_data(switchboard::ptr<const imu_type> datum) {
        // Ensures that slam doesnt start before valid IMU readings come in
        if (datum == nullptr) {
            assert(!latest_imu_time);
            return;
        }

        // Ensure that IMU data is received in the time order
        assert(datum->time > latest_imu_time);
        latest_imu_time = datum->time;

        vio_input_proto::IMUData* imu_data = data_buffer->add_imu_data();
        imu_data->set_timestamp(datum->time.time_since_epoch().count());

        auto* angular_vel = new vio_input_proto::Vec3();
        angular_vel->set_x(datum->angular_v.x());
        angular_vel->set_y(datum->angular_v.y());
        angular_vel->set_z(datum->angular_v.z());
        imu_data->set_allocated_angular_vel(angular_vel);

        auto* linear_accel = new vio_input_proto::Vec3();
        linear_accel->set_x(datum->linear_a.x());
        linear_accel->set_y(datum->linear_a.y());
        linear_accel->set_z(datum->linear_a.z());
        imu_data->set_allocated_linear_accel(linear_accel);

        if (latest_cam_time && latest_imu_time > latest_cam_time) {
            send_imu_cam_data(latest_cam_time);
        }

        switchboard::ptr<const cam_type> cam;

        if (_m_cam.size() != 0 && !latest_cam_time) {
            cam = _m_cam.dequeue();

            cv::Mat cam_img0 = (cam->img0).clone();
            cv::Mat cam_img1 = (cam->img1).clone();

            // size of img0 before compression
            double cam_img0_size = cam_img0.total() * cam_img0.elemSize();

            vio_input_proto::CamData* cam_data = new vio_input_proto::CamData();
            cam_data->set_timestamp(cam->time.time_since_epoch().count());
            cam_data->set_rows(cam_img0.rows);
            cam_data->set_cols(cam_img0.cols);

#ifdef USE_COMPRESSION
            /** WITH COMPRESSION **/
            uint64_t curr =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                    .count();
            queue.push(curr);
            std::unique_lock<std::mutex> lock{mutex};
            encoder->enqueue(cam_img0, cam_img1);
            cv.wait(lock, [this]() {
                return img_ready;
            });
            img_ready = false;

            sizes.push_back(this->img0.size);

            // calculate average sizes
            if (sizes.size() > 100) {
                int32_t sum = 0;
                for (auto& s : sizes) {
                    sum += s;
                }
                // For debugging, prints out average image size after compression and compression ratio
                // std::cout << "compression ratio: " << img0_size / (sum / sizes.size()) << " average size after compression "
                // << sum / sizes.size() << std::endl;
            }

            cam_data->set_img0_data((void*) this->img0.data, this->img0.size);
            cam_data->set_img1_data((void*) this->img1.data, this->img1.size);

            lock.unlock();
            /** WITH COMPRESSION END **/
#else
            /** NO COMPRESSION **/
            cam_data->set_img0_data((void*) cam_img0.data, cam_img0_size);
            cam_data->set_img1_data((void*) cam_img1.data, cam_img0_size);
            /** NO COMPRESSION END **/
#endif
            data_buffer->set_allocated_cam_data(cam_data);
            latest_cam_time = cam->time;
            if (latest_imu_time <= latest_cam_time) {
                return;
            } else {
                send_imu_cam_data(latest_cam_time);
            }
        }
    }

private:
    std::unique_ptr<video_encoder>         encoder = nullptr;
    std::optional<time_point>              latest_imu_time;
    std::optional<time_point>              latest_cam_time;
    int                                    frame_id    = 0;
    vio_input_proto::IMUCamVec*            data_buffer = new vio_input_proto::IMUCamVec();
    const std::shared_ptr<switchboard>     sb;
    const std::shared_ptr<RelativeClock>   _m_clock;
    const std::shared_ptr<Stoplight>       _m_stoplight;
    switchboard::buffered_reader<cam_type> _m_cam;

    TCPSocket socket;
    Address   server_addr;
};

PLUGIN_MAIN(offload_writer)
