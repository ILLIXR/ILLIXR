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
        , _m_imu_raw{sb->get_reader<imu_raw_type>("imu_raw")}
        , server_addr(SERVER_IP, SERVER_PORT_1) {
        if (!filesystem::exists(data_path)) {
            if (!filesystem::create_directory(data_path)) {
                std::cerr << "Failed to create data directory.";
            }
        }
        pub_to_send_csv.open(data_path + "/pub_to_send.csv");
        frame_info_csv.open(data_path + "/frame_info.csv");
        rotation_diff_csv.open(data_path + "/rotation_diff.csv");
        compression_time_csv.open(data_path + "/comp_time.csv");
        request_time_csv.open(data_path + "/request_time.csv");
        frame_info_csv << "frame no." << "," << "IMU (0) or Cam (1)" << "," << "Timestamp" << std::endl;
        request_time_csv << "Request Timestamp (ns)" << std::endl;
        // rotation_diff_csv << "timestamp," << "x," << "y," << "z," << "roll," << "pitch," << "yaw" << std::endl;

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
        std::cout << "Sending camera with timestamp " << cam_time.value().time_since_epoch().count() << std::endl;
        data_buffer->set_real_timestamp(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        data_buffer->set_frame_id(frame_id);

        string data_to_be_sent = data_buffer->SerializeAsString();
        string delimitter      = "EEND!";

        request_time_csv << _m_clock->now().time_since_epoch().count() << std::endl;
        socket.write(data_to_be_sent + delimitter);
        std::cout << "Sent one imu and cam\n";
        pub_to_send_csv << cam_time.value().time_since_epoch().count() << "," << (_m_clock->now() - cam_time.value()).count() / 1e6 << std::endl;

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
        lina_vec.push_back(datum->linear_a);
        angv_vec.push_back(datum->angular_v.norm());
        // std::cout << "Angular speed is " << datum->angular_v.norm() << std::endl;

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
/*
// save velocity
        while (_m_imu_raw.size() != 0) {
            switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.dequeue();
            double speed = (imu_raw->vel).norm();
            speed_vec.push_back(speed);
            // std::cout << "Speed is " << speed << std::endl;
            current_pose = imu_raw->pos;
            current_quat = imu_raw->quat;

        }
*/
        switchboard::ptr<const cam_type> cam;

        if (_m_cam.size() != 0 && !latest_cam_time) {
            absaggcnt++;
            // sendnow = true;

            // For VIO to initialize
            // if (keep_initial-- > 0) sendnow = true;
// Dropping based on velocity
/*
            // for initially there's no IMU integration results
            if (speed_vec.size() == 0) sendnow = true;
            else {
                largest_speed = *(std::max_element(speed_vec.begin(), speed_vec.end()));
            }
            if (largest_speed > speed_thresh) {
                sendnow = true;
                speed_vec.clear();
                lina_vec.clear();
                angv_vec.clear();
                std::cout << " setting future sendnow because of large linear speeds" << std::endl;
            }
            double largest_angv = *(std::max_element(angv_vec.begin(), angv_vec.end()));
            if (largest_angv > angv_thres) {
                sendnow = true;
                std::cout << " setting future sendnow because of large angular velocity" << std::endl;
            }
*/


// Dropping based on pose distance
/*
            switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get_ro_nullable();
            double x, y, z, roll, pitch, yaw;
            if (imu_raw) {
                current_pose = imu_raw->pos;
                current_quat = imu_raw->quat;

                Eigen::Vector3d pose_diff = current_pose - last_pose;
                x = pose_diff[0];
                y = pose_diff[1];
                z = pose_diff[2];

                double distance = pose_diff.norm();
                // std::cout << "DISTANCE: " << distance << std::endl;
                // Based on absolute distance
                // if (distance > distance_thresh) {
                //     sendnow = true
                //     std::cout << " setting future sendnow because of LONG DISTANCE MOVED" << std::endl;
                //     last_pose = current_pose;
                // }
                Eigen::Quaterniond quat_diff = current_quat * last_quat.inverse();
                Eigen::Matrix3d diffMatrix = quat_diff.toRotationMatrix();
                roll = diffMatrix.eulerAngles(0, 1, 2).x();
                pitch = diffMatrix.eulerAngles(0, 1, 2).y();
                yaw = diffMatrix.eulerAngles(0, 1, 2).z();

                // Get the rotation angles (in radians) from the resulting quaternion
                // double roll_last, pitch_last, yaw_last;
                // Eigen::Matrix3d lastMatrix = last_quat.toRotationMatrix();
                // roll_last = lastMatrix.eulerAngles(0, 1, 2).x();
                // pitch_last = lastMatrix.eulerAngles(0, 1, 2).y();
                // yaw_last = lastMatrix.eulerAngles(0, 1, 2).z();

                // double roll_curr, pitch_curr, yaw_curr;
                // Eigen::Matrix3d currMatrix = current_quat.toRotationMatrix();
                // roll_curr = currMatrix.eulerAngles(0, 1, 2).x();
                // pitch_curr = currMatrix.eulerAngles(0, 1, 2).y();
                // yaw_curr = currMatrix.eulerAngles(0, 1, 2).z();

                // FIXME Does taking absolute value always make sense? e.g. -180 and 180 are the same but will give a 360 diff
                // double roll_diff = abs(roll_curr - roll_last);
                // double pitch_diff = abs(pitch_curr - pitch_last);
                // double yaw_diff = abs(yaw_curr - yaw_last);
                // Eigen::Matrix3d::EulerAnglesXYZd euler = rotationMatrix.eulerAngles(0, 1, 2); // XYZ order
                // // Extract the roll, pitch, and yaw angles
                // roll = euler[0];
                // pitch = euler[1];
                // yaw = euler[2];
                // Use the non-directional distance
                // if (distance > distance_thresh || roll > rot_thresh || pitch > rot_thresh || yaw > rot_thresh) {
                // // Based on distance on x y z separately
                if (abs(x) > distance_thresh || abs(y) > distance_thresh || abs(z) > distance_thresh || abs(roll) > rot_thresh || abs(pitch) > rot_thresh || abs(yaw) > rot_thresh) {
                    sendnow = true;
                    std::cout << " setting future sendnow because of LARGE TRANSLATION or ROTATION" << std::endl;
                    // FIXME Does it make sense to only update last_pose when the threshold is surpassed?
                    last_pose = current_pose;
                    last_quat = current_quat;
                }
            } else sendnow = true;
*/
/* Dropping based on linear accelerations and angular velocity
            // for euroc data, x axis has gravity. for recorded zed camera data, z axis has gravity.   
            // double abs_lina = std::abs(datum->linear_a.x()-9.8) + std::abs(datum->linear_a.y()) + std::abs(datum->linear_a.z() );
            double abs_lina = std::abs(datum->linear_a.x()) + std::abs(datum->linear_a.y()) + std::abs(datum->linear_a.z() - 9.8);
            double abs_lina_diff = std::abs(datum->linear_a.x() - last_imu_lina.x()) + std::abs(datum->linear_a.y() - last_imu_lina.y()) + std::abs(datum->linear_a.z() - last_imu_lina.z());
            double abs_angv = std::abs(datum->angular_v.x()) + std::abs(datum->angular_v.y()) + std::abs(datum->angular_v.z());
            double abs_angv_diff = std::abs(datum->angular_v.x() - last_imu_angv.x()) + std::abs(datum->angular_v.y() - last_imu_angv.y()) + std::abs(datum->angular_v.z() - last_imu_angv.z());
            accl_history[accl_count++ % 6] = abs_lina;
            std::cout << "abs_lina is " << abs_lina << std::endl;
            std::cout << "abs_lina_diff is " << abs_lina_diff << std::endl;
            std::cout << "abs_angv is " << abs_angv << std::endl;
            std::cout << "abs_angv_diff is " << abs_angv_diff << std::endl;
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
                std::cout << " SENDNOW abs_lina " << abs_lina << " crossed threshold: " << linacc_thresh << std::endl;
                sendnow = true;
            }
            if (abs_lina_diff > linacc_diff_thresh) {
                std::cout << " SENDNOW abs_lina_diff " << abs_lina_diff << " crossed threshold: " << linacc_diff_thresh << std::endl;
                sendnow = true;
            }
            if (abs_angv > ang_thresh) {
                std::cout << " SENDNOW abs_angv " << abs_angv << " crossed threshold: " << ang_thresh << std::endl;
                sendnow = true;
            }
            if (abs_angv_diff > ang_diff_thresh) {
                std::cout << " SENDNOW abs_angv_diff " << abs_angv_diff << " crossed threshold: " << ang_diff_thresh << std::endl;
                sendnow = true;
            }
            // if (std::abs(datum->angular_v.x() + datum->angular_v.y() + datum->angular_v.z()) > ang_thresh) {
            //     std::cout << " setting future sendnow because crossed ang_thresh threshold "<<std::endl;
            //     sendnow = true;
            // }

            // double ssim = SSIMCalculator::calculateSSIM(last_cam, img2);
*/

            // If not reaching the threshold, but has skipped a great amount, also send the image
            // if (aggrcnt_toswtch == time_thresh) {
            //     sendnow = true;
            //     aggrcnt_toswtch = 0;
            //     std::cout << " setting future sendnow because of long time no send" << std::endl;
            // }
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
            time_point start_compression = _m_clock->now();
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
            int avg_size; 
            if (sizes.size() > 0) {
                int32_t sum = 0;
                for (auto& s : sizes) {
                    sum += s;
                }
                // For debugging, prints out average image size after compression and compression ratio
                // std::cout << "compression ratio: " << cam_img0_size / (sum / sizes.size()) << " average size after compression "
                // << sum / sizes.size() << " single image size: " << this->img0.size << std::endl;
                avg_size = sum / sizes.size();
            }

            cam_data->set_img0_data((void*) this->img0.data, this->img0.size);
            cam_data->set_img1_data((void*) this->img1.data, this->img1.size);

            lock.unlock();
            compression_time_csv << cam->time.time_since_epoch().count() << "," << (_m_clock->now() - start_compression).count() / 1e6 << "," << avg_size << std::endl;
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
                std::cout << "Return because latest_imu_time <= latest_cam_time\n";
                return;
            } else {
                send_imu_cam_data(latest_cam_time);
            }

/* For frame dropping
            if (sendnow) {
                // std::cout << "aggrcnt_toswtch is " << aggrcnt_toswtch << " and " << dropped_count << " frames dropped out of " << absaggcnt << "!\n";
                cam = _m_cam.dequeue();
                // last_imu_lina = datum->linear_a;
                // last_imu_angv = datum->angular_v;
                // rotation_diff_csv << cam->time.time_since_epoch().count() << "," << (double)abs(x) << "," << (double)abs(y) << "," << (double)abs(z) << "," << (double)abs(roll) << "," << (double)abs(pitch) << "," << (double)abs(yaw)  << "," << sendnow << std::endl;

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
*/
/*
        #ifdef USE_COMPRESSION
                // WITH COMPRESSION 
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
                // WITH COMPRESSION END
        #else
                // NO COMPRESSION
                cam_data->set_img0_data((void*) cam_img0.data, cam_img0_size);
                cam_data->set_img1_data((void*) cam_img1.data, cam_img0_size);
                // NO COMPRESSION END 
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
*/
        }
        // sendnow = false;
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
    switchboard::reader<imu_raw_type>      _m_imu_raw;

    TCPSocket socket;
    Address   server_addr;

    const string data_path = filesystem::current_path().string() + "/recorded_data";
    std::ofstream pub_to_send_csv;
    std::ofstream frame_info_csv;
    std::ofstream rotation_diff_csv;
    std::ofstream compression_time_csv;
    std::ofstream request_time_csv;

    // For frame dropping
	bool sendnow = true;
	int dropped_count = 0;
	int aggrcnt_toswtch = 0;
	int absaggcnt = 0;
    double ang_thresh = 2; //1.5;
    double ang_diff_thresh = 2; //1.5;
    double linacc_thresh = 3;
    double linacc_diff_thresh = 5;
  	double time_thresh = -1;
    // Keep a history of accelerations
    int accl_count = 0;
    double accl_history[6] = {0, 0, 0, 0, 0, 0};
    // cv::Mat last_cam;
    Eigen::Vector3d last_imu_lina;
    Eigen::Vector3d last_imu_angv;

    std::vector<double> speed_vec;
    std::vector<Eigen::Vector3d> lina_vec;
    std::vector<double> angv_vec;
    double largest_speed;
    // V1_01
    // double speed_thresh = 0.30;
    // double angv_thres = 0.3;
    // fast2
    double speed_thresh = 0.4;
    double angv_thres = 0.9;

    int keep_initial = 300;

    // Pose based
    Eigen::Vector3d current_pose;
    Eigen::Quaterniond current_quat;
    Eigen::Vector3d last_pose;
    Eigen::Quaterniond last_quat;
    // double distance_thresh = 0.01;
    // double rot_thresh = 3;
    double distance_thresh = -1;
    double rot_thresh = -1;
};

PLUGIN_MAIN(offload_writer)
