#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/network/net_config.hpp"
#include "common/network/socket.hpp"
#include "common/network/timestamp.hpp"
#include "common/phonebook.hpp"
#include "common/stoplight.hpp"
#include "common/switchboard.hpp"
#include "common/threadloop.hpp"
#include "video_encoder.h"
#include "vio_input.pb.h"
#include "ssim_calculator.h"

#include <boost/lockfree/spsc_queue.hpp>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <opencv/cv.hpp>
#include <opencv2/core/mat.hpp>

using namespace ILLIXR;
#define USE_COMPRESSION

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
    offload_writer(std::string name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_stoplight{pb->lookup_impl<Stoplight>()}
        , _m_cam{sb->get_buffered_reader<cam_type>("cam")}
        , server_addr(SERVER_IP, SERVER_PORT_1) {
        if (!filesystem::exists(data_path)) {
            if (!filesystem::create_directory(data_path)) {
                std::cerr << "Failed to create data directory.";
            }
        }
        pub_to_send_csv.open(data_path + "/pub_to_send.csv");
        frame_info_csv.open(data_path + "/frame_info.csv");
        frame_info_csv << "frame no." << "," << "IMU (0) or Cam (1)" << "," << "Timestamp" << std::endl;

        socket.set_reuseaddr();
        socket.bind(Address(CLIENT_IP, CLIENT_PORT_1));
        socket.enable_no_delay();
        initial_timestamp();

        std::srand(std::time(0));
    }

    virtual void start() override {
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

        cout << "TEST: Connecting to " << server_addr.str(":") << endl;
        socket.connect(server_addr);
        cout << "Connected to " << server_addr.str(":") << endl;

        sb->schedule<imu_type>(id, "imu", [this](switchboard::ptr<const imu_type> datum, std::size_t) {
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
    void send_imu_cam_data(std::optional<time_point> &cam_time) {
        data_buffer->set_real_timestamp(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        data_buffer->set_frame_id(frame_id);

        string data_to_be_sent = data_buffer->SerializeAsString();
        string delimitter      = "EEND!";

        socket.write(data_to_be_sent + delimitter);
        pub_to_send_csv << (_m_clock->now() - cam_time.value()).count() << std::endl;

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
        last_imu = datum->linear_a;

        // Ensure that IMU data is received in the time order
        assert(datum->time > latest_imu_time);
        latest_imu_time = datum->time;

        vio_input_proto::IMUData* imu_data = data_buffer->add_imu_data();
        imu_data->set_timestamp(datum->time.time_since_epoch().count());

        vio_input_proto::Vec3* angular_vel = new vio_input_proto::Vec3();
        angular_vel->set_x(datum->angular_v.x());
        angular_vel->set_y(datum->angular_v.y());
        angular_vel->set_z(datum->angular_v.z());
        imu_data->set_allocated_angular_vel(angular_vel);

        vio_input_proto::Vec3* linear_accel = new vio_input_proto::Vec3();
        linear_accel->set_x(datum->linear_a.x());
        linear_accel->set_y(datum->linear_a.y());
        linear_accel->set_z(datum->linear_a.z());
        imu_data->set_allocated_linear_accel(linear_accel);

        frame_info_csv << frame_id << "," << "0" << "," << datum->time.time_since_epoch().count() << std::endl;

        if (latest_cam_time && latest_imu_time > latest_cam_time) {
            send_imu_cam_data(latest_cam_time);
        }

        switchboard::ptr<const cam_type> cam;

        if (_m_cam.size() != 0 && !latest_cam_time) {
            absaggcnt++;

            // for euroc data, x axis has gravity. for recorded zed camera data, z axis has gravity.   
            // double abs_lina = std::abs(datum->linear_a.x()-9.8) + std::abs(datum->linear_a.y()) + std::abs(datum->linear_a.z() );
            double abs_lina = std::abs(datum->linear_a.x()) + std::abs(datum->linear_a.y()) + std::abs(datum->linear_a.z() - 9.8);
            double abs_lina_diff = std::abs(datum->linear_a.x() - last_imu.x()) + std::abs(datum->linear_a.y() - last_imu.y()) + std::abs(datum->linear_a.z() - - last_imu.z());
            // double abs_angv = std::abs(datum->angular_v.x()) + std::abs(datum->angular_v.y()) + std::abs(datum->angular_v.z());
            accl_history[accl_count++ % 6] = abs_lina;
            std::cout << "abs_lina is " << abs_lina << std::endl;
            double sum_accl = 0.0;
            for (int i = 0; i < 6; i++) {
                sum_accl += accl_history[i];
                std::cout << "sum_accl is " << sum_accl << std::endl;
            }
            
            // If reaching the threshold, the image has to be sent
            // if (sum_accl > linacc_thresh) {
            //     std::cout << " setting future sendnow because metric " << sum_accl << " crossed linacc threshold:  " << linacc_thresh << std::endl; 
            //     sendnow = true;
            // }
            if (abs_lina > linacc_thresh) {
                std::cout << " setting future sendnow because metric " << abs_lina << " crossed linacc threshold:  " << linacc_thresh << std::endl; 
                sendnow = true;
            }
            if ()
            // if (std::abs(datum->angular_v.x() + datum->angular_v.y() + datum->angular_v.z()) > ang_thresh) {
            //     std::cout << " setting future sendnow because crossed ang_thresh threshold "<<std::endl;
            //     sendnow = true;
            // }

            // double ssim = SSIMCalculator::calculateSSIM(last_cam, img2);

            // If not reaching the threshold, but has skipped a great amount, also send the image
            if (aggrcnt_toswtch > time_thresh) {
                sendnow = true;
                aggrcnt_toswtch = 0;
                std::cout << " setting future sendnow because of long time no send" << std::endl;
            }

            if (sendnow) {
                cam = _m_cam.dequeue();

                cv::Mat cam_img0 = (cam->img0).clone();
                cv::Mat cam_img1 = (cam->img1).clone();
                // last_cam = (cam->img0).clone();

                // size of img0 before compression
                double cam_img0_size = cam_img0.total() * cam_img0.elemSize();

                vio_input_proto::CamData* cam_data = new vio_input_proto::CamData();
                cam_data->set_timestamp(cam->time.time_since_epoch().count());
                cam_data->set_rows(cam_img0.rows);
                cam_data->set_cols(cam_img0.cols);
                frame_info_csv << frame_id << "," << "1" << "," << cam->time.time_since_epoch().count() << std::endl;

        #ifdef USE_COMPRESSION
                /** WITH COMPRESSION **/
                uint64_t curr =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
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
            } else {
                aggrcnt_toswtch++;
                dropped_count++;
                cam = _m_cam.dequeue();
                std::cout << "aggrcnt_toswtch is " << aggrcnt_toswtch << " and " << dropped_count << " frames dropped out of " << absaggcnt << "!\n";
            }
        }
        sendnow = false;
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

    const string data_path = filesystem::current_path().string() + "/recorded_data";
    std::ofstream pub_to_send_csv;
    std::ofstream frame_info_csv;

    // For frame dropping
	bool sendnow = false;
	int dropped_count = 0;
	int aggrcnt_toswtch = 0;
	int absaggcnt = 0;
    double ang_thresh = 2; //1.5;
    double linacc_thresh = 2;
  	double time_thresh = 2;
    // Keep a history of accelerations
    int accl_count = 0;
    double accl_history[6] = {0, 0, 0, 0, 0, 0};
    // cv::Mat last_cam;
    Eigen::Vector3d last_imu = {0.0, 0.0, 0.0};
};

PLUGIN_MAIN(offload_writer)
