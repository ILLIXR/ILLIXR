#pragma once

#include "illixr/data_format/imu.hpp"
#include "illixr/data_format/opencv_data_types.hpp"
#include "illixr/data_format/misc.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#include <media/NdkImageReader.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <android/sensor.h>

namespace ILLIXR {

struct android_imu_struct {
    ASensorManager *sensor_manager;
    const ASensor *accelerometer;
    const ASensor *gyroscope;
    ASensorEventQueue *event_queue;
};


class android_data : public threadloop {
public:
    [[maybe_unused]] android_data(const std::string& name_, phonebook *pb_);
    ~android_data() override;
    [[maybe_unused]] static int android_sensor_callback(int fd, int events, void *data);
    static void *android_run_thread(void *ptr);
    //Reference: https://github.com/sixo/native-camera/tree/93b05aec6d05604a314dc822b6b09a4cbc3d5104
    static void image_callback(void *context, AImageReader *reader);
    AImageReader *create_jpeg_reader();
    ANativeWindow *create_surface(AImageReader *reader);
    //Camera callbacks
    static void on_disconnected(void *context, ACameraDevice *device);
    static void on_error(void *context, ACameraDevice *device, int error);
    static void on_session_active(void *context, ACameraCaptureSession *session);
    static void on_session_ready(void *context, ACameraCaptureSession *session);
    static void on_session_closed(void *context, ACameraCaptureSession *session);
    static void on_capture_failed(void *context, ACameraCaptureSession *session,
                                  ACaptureRequest *request, ACameraCaptureFailure *failure);
    static void on_capture_sequence_completed(void *context, ACameraCaptureSession *session,
                                              int sequenceId, int64_t frameNumber) {
        (void)context;
        (void)session;
        (void)sequenceId;
        (void)frameNumber;
    }

    static void on_capture_sequence_aborted(void *context, ACameraCaptureSession *session,
                                            int sequenceId) {
        (void)context;
        (void)session;
        (void)sequenceId;
    }

    static void on_capture_completed(void *context, ACameraCaptureSession *session,
            ACaptureRequest *request, const ACameraMetadata *result) {
        (void)context;
        (void)session;
        (void)request;
        (void)result;
    }

    std::string get_back_facing_cam_id();

    void init_cam();

    /// For `threadloop` style plugins, do not override the start() method unless you know what you're doing!
    /// _p_one_iteration() is called in a thread created by threadloop::start()
    void _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard> switchboard_;
    const std::shared_ptr<const relative_clock> clock_;
    switchboard::writer<data_format::imu_type> imu_;
    switchboard::writer<data_format::binocular_cam_type> cam_;

    std::optional<ullong> first_imu_time_;
    std::optional<time_point> first_real_time_imu_;

    ACameraManager *camera_manager_ = nullptr;
    ACameraDevice *camera_device_ = nullptr;
    ANativeWindow *image_window_ = nullptr;
    ACameraOutputTarget *image_target_ = nullptr;
    AImageReader *image_reader_ = nullptr;
    ACaptureSessionOutput *image_output_ = nullptr;
    ACaptureRequest *request_ = nullptr;
    ACaptureSessionOutputContainer *outputs_ = nullptr;
    ACameraCaptureSession *capture_session_ = nullptr;
    static const int IMAGE_WIDTH = 752;
    static const int IMAGE_HEIGHT = 480;
    android_imu_struct *a_imu_;

    ACameraDevice_stateCallbacks camera_device_callbacks_ = {
            .context = nullptr,
            .onDisconnected = on_disconnected,
            .onError = on_error,
    };

    ACameraCaptureSession_stateCallbacks session_state_callbacks_{
            .context = nullptr,
            .onClosed = on_session_closed,
            .onReady = on_session_ready,
            .onActive = on_session_active,
    };

    ACameraCaptureSession_captureCallbacks capture_callbacks_{
            .context = nullptr,
            .onCaptureStarted = nullptr,
            .onCaptureProgressed = nullptr,
            .onCaptureCompleted = on_capture_completed,
            .onCaptureFailed = on_capture_failed,
            .onCaptureSequenceCompleted = on_capture_sequence_completed,
            .onCaptureSequenceAborted = on_capture_sequence_aborted,
            .onCaptureBufferLost = nullptr,
    };

    static std::ofstream my_file_;
    static std::ofstream cam_file_;

    static std::mutex lock_;
    static Eigen::Vector3d gyro_;
    static Eigen::Vector3d accel_;
    static cv::Mat last_image_;
    static bool img_ready_;
    static std::mutex mtx_;
    static ullong imu_ts_;
    static double cam_proc_time_;
    static double imu_time_;
    static int counter_;
};
}