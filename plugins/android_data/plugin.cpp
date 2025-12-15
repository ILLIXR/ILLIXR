#include "plugin.hpp"

#include <opencv2/opencv.hpp> // Include OpenCV API
#include <opencv2/core/mat.hpp>

// ILLIXR includes
#include <chrono>
#include <thread>
#include <fstream>
#include <camera/NdkCameraMetadata.h>
#include <android/native_window_jni.h>
#include <Eigen/Core>
#include <filesystem>
#include <spdlog/spdlog.h>
#define POLL_RATE_USEC 1000

using namespace ILLIXR;

double android_data::cam_proc_time_ = 0.;
double android_data::imu_time_ = 0.;
std::ofstream android_data::my_file_ = {};
std::ofstream android_data::cam_file_ = {};
std::mutex android_data::lock_ = {};
Eigen::Vector3d android_data::gyro_ = {};
Eigen::Vector3d android_data::accel_ = {};
ullong android_data::imu_ts_ = 0;
std::mutex android_data::mtx_ = {};
cv::Mat android_data::last_image_ = {};
bool android_data::img_ready_ = false;
int android_data::counter_ = 0;

[[maybe_unused]] android_data::android_data(const std::string &name_, phonebook *pb_)
        : threadloop{name_, pb_}
        , switchboard_{pb_->lookup_impl<switchboard>()}
        , clock_{pb_->lookup_impl<relative_clock>()}
        , imu_{switchboard_->get_writer<data_format::imu_type>("imu")}
        , cam_{switchboard_->get_writer<data_format::binocular_cam_type>("cam")}{
#ifndef NDEBUG
    spdlog::get("illixr")->debug("IMU CAM CONSTRUCTOR");
#endif
    init_cam();
    //std::filesystem::create_directories("/sdcard/Android/data/com.example.native_activity/cam0/");
//        remove("/sdcard/Android/data/com.example.native_activity/cam0/*.png");
    my_file_.open("/sdcard/Android/data/com.example.native_activity/imu0.csv");
    cam_file_.open("/sdcard/Android/data/com.example.native_activity/data.csv");
    a_imu_ = new android_imu_struct();
    std::thread(android_run_thread, a_imu_).detach();
}

android_data::~android_data() {
    std::cout << "Deconstructing android_cam" << std::endl;
    ACameraCaptureSession_stopRepeating(capture_session_);
    ACameraCaptureSession_close(capture_session_);
    ACaptureSessionOutputContainer_free(outputs_);

    ACameraDevice_close(camera_device_);
    ACameraManager_delete(camera_manager_);
    camera_manager_ = nullptr;
    AImageReader_delete(image_reader_);
    image_reader_ = nullptr;
    ACaptureRequest_free(request_);
    my_file_.close();
    cam_file_.close();
}

[[maybe_unused]] int android_data::android_sensor_callback(int fd, int events, void *data) {
    (void)fd;
    (void)events;
    android_imu_struct *d = static_cast<android_imu_struct*>(data);
    //ANDROID_LOG("Android sensor callback");
    if (d->accelerometer == NULL || d->gyroscope == NULL)
        return 1;

    ASensorEvent event;
    while (ASensorEventQueue_getEvents(d->event_queue, &event, 1) > 0) {
        auto start = std::chrono::high_resolution_clock::now();
        lock_.lock();
        switch (event.type) {
//                case ASENSOR_TYPE_GRAVITY: {
//                    grav[0] = -event.acceleration.z;
//                    grav[1] = -event.acceleration.x;
//                    grav[2] = event.acceleration.y;
//                    ANDROID_LOG("Gravity sensor values %f %f %f", grav[0], grav[1], grav[2]);
//                    break;
//                }

            case ASENSOR_TYPE_ACCELEROMETER: {
//                    accel[0] = event.acceleration.y;
//                    accel[1] = -event.acceleration.x;
//                    accel[2] = event.acceleration.z;

//matches zed
//                    accel[0] = -event.acceleration.z;//  - grav[0];;
//                    accel[1] = -event.acceleration.x;// - grav[1];
//                    accel[2] = event.acceleration.y;// - grav[2] - 9.8;

                accel_[0] = event.acceleration.x;
                accel_[1] = event.acceleration.y;
                accel_[2] = event.acceleration.z;

//                    accel[0] = 1;
//                    accel[1] = 0;
//                    accel[2] = 0;
                break;
            }
            case ASENSOR_TYPE_GYROSCOPE: {
//                    gyro[0] = -event.data[1];
//                    gyro[1] = event.data[0];
//                    gyro[2] = event.data[2];

//matches zed
//                    gyro[0] = -event.data[2];
//                    gyro[1] = -event.data[0];
//                    gyro[2] = event.data[1];

                gyro_[0] = event.data[0];
                gyro_[1] = event.data[1];
                gyro_[2] = event.data[2];
//                    gyro[0] = 0;
//                    gyro[1] = 0;
//                    gyro[2] = 0;
//                    timestamp = event.timestamp;
//                    avg_filter.push_back({accel,gyro});
//                    std::pair<Eigen::Vector3f, Eigen::Vector3f> avg_imu = moving_average();
//                    accel = avg_imu.first;
//                    gyro = avg_imu.second;
//                    if(avg_filter.size() >= 10) {
//                        avg_filter.pop_front();
//                    }
                //uint64_t time_s = std::chrono::system_clock::now().time_since_epoch().count();
#ifndef NDEBUG
                spdlog::get("illixr")->debug("IMU Values : accel %f %f %f %f %f %f", accel_[0], accel_[1], accel_[2],
                            gyro_[0], gyro_[1], gyro_[2]);
#endif
            }
            default:;
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
#ifndef NDEBUG
        spdlog::get("illixr")->debug("durationX: %f", duration_to_double(duration));
#endif
        imu_time_ = imu_time_ + duration_to_double(duration);
        lock_.unlock();
    }

    return 1;
}

void *android_data::android_run_thread(void *ptr) {
//        myfile.open ("/sdcard/Android/data/com.example.native_activity/imu0.csv");
    struct android_imu_struct *d = (struct android_imu_struct *) ptr;
    const int32_t poll_rate_usec = POLL_RATE_USEC;

#if __ANDROID_API__ >= 26
    d->sensor_manager = ASensorManager_getInstanceForPackage("ILLIXR_ANDROID_IMU");
#else
    d->sensor_manager = ASensorManager_getInstance();
#endif

    d->accelerometer = ASensorManager_getDefaultSensor(d->sensor_manager,
                                                       ASENSOR_TYPE_ACCELEROMETER);
    d->gyroscope = ASensorManager_getDefaultSensor(d->sensor_manager, ASENSOR_TYPE_GYROSCOPE);
//        d->gravity = ASensorManager_getDefaultSensor(d->sensor_manager, ASENSOR_TYPE_GRAVITY);


    ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);

    d->event_queue = ASensorManager_createEventQueue(d->sensor_manager, looper, 3, NULL, NULL);

    // Start sensors in case this was not done already.
    if (d->accelerometer != NULL) {
        ASensorEventQueue_enableSensor(d->event_queue, d->accelerometer);
        ASensorEventQueue_setEventRate(d->event_queue, d->accelerometer, poll_rate_usec);
    }
    if (d->gyroscope != NULL) {
        ASensorEventQueue_enableSensor(d->event_queue, d->gyroscope);
        ASensorEventQueue_setEventRate(d->event_queue, d->gyroscope, poll_rate_usec);
    }

    int ret = 0;
    while (ret != ALOOPER_POLL_ERROR) {
        ret = ALooper_pollOnce(0, NULL, NULL, NULL);
        ASensorEvent event;
        while (ASensorEventQueue_getEvents(d->event_queue, &event, 2) >
               0) {
            lock_.lock();
            switch (event.type) {
                case ASENSOR_TYPE_ACCELEROMETER:
                    accel_[0] = event.acceleration.x;
                    accel_[1] = event.acceleration.y;
                    accel_[2] = event.acceleration.z;
                    //ANDROID_LOG("ACCEL %f, %f, %f", accel[0], accel[1], accel[2]);
                    break;
                case ASENSOR_TYPE_GYROSCOPE:
                    gyro_[0] = event.data[0];
                    gyro_[1] = event.data[1];
                    gyro_[2] = event.data[2];
                    //ANDROID_LOG("GYRO %f, %f, %f", gyro[0], gyro[1], gyro[2]);
                    break;
            }
            imu_ts_ = event.timestamp;
            lock_.unlock();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

//        int ret = 0;
//        while (ret != ALOOPER_POLL_ERROR) {
//            ret = ALooper_pollAll(0, NULL, NULL, NULL);
//        }
//        myfile.close();

    return nullptr;
}

// Reference: https://github.com/sixo/native-camera/tree/93b05aec6d05604a314dc822b6b09a4cbc3d5104
void android_data::image_callback(void *context, AImageReader *reader) {
    (void)context;
    AImage *image = nullptr;
    AImageReader_acquireNextImage(reader, &image);
    //ANDROID_LOG("Image callback");
    if (image == nullptr) {
        spdlog::get("illixr")->warn("IMage is null!");
        return;
    }
    auto start = std::chrono::high_resolution_clock::now();


    // Try to process data without blocking the callback
    //std::thread processor([=](){
    uint8_t *rPixel;
    int32_t rLen;
    int32_t yPixelStride, yRowStride;
    AImage_getPlanePixelStride(image, 0, &yPixelStride);
    AImage_getPlaneRowStride(image, 0, &yRowStride);
    AImage_getPlaneData(image, 0, &rPixel, &rLen);
    uint8_t *data = new uint8_t[rLen];

    if (yPixelStride == 1) {
        for (int y = 0; y < IMAGE_HEIGHT; y++)
            memcpy(data + y * IMAGE_WIDTH, rPixel + y * yRowStride, IMAGE_WIDTH);
    }
    cv::Mat rawData(IMAGE_HEIGHT, IMAGE_WIDTH, CV_8UC1, (uint8_t *) data);
    mtx_.lock();
    last_image_ = rawData.clone();
    mtx_.unlock();
    img_ready_ = true;
    AImage_delete(image);
    delete[] data;
//        });
//        processor.detach();
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
#ifndef NDEBUG
    spdlog::get("illixr")->debug("duration: %f", duration_to_double(duration));
#endif
    cam_proc_time_ += duration_to_double(duration);
}


AImageReader *android_data::create_jpeg_reader() {
    AImageReader *reader = nullptr;
    media_status_t status =
            AImageReader_new(IMAGE_WIDTH, IMAGE_HEIGHT, AIMAGE_FORMAT_YUV_420_888, 10, &reader);

    if (status != AMEDIA_OK) {
        spdlog::get("illixr")->error("Error with image reader");
        return nullptr;
    }

    AImageReader_ImageListener listener{
            .context = this,
            .onImageAvailable = image_callback,
    };

    AImageReader_setImageListener(reader, &listener);

    return reader;
}

ANativeWindow *android_data::create_surface(AImageReader *reader) {
    ANativeWindow *nativeWindow;
    AImageReader_getWindow(reader, &nativeWindow);
    return nativeWindow;
}

//Camera callbacks
void android_data::on_disconnected(void *context, ACameraDevice *device) {
    (void)context;
    (void)device;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("on_disconnected");
#endif
}

void android_data::on_error(void *context, ACameraDevice *device, int error) {
    (void)context;
    (void)device;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("error %d", error);
#endif
}

void android_data::on_session_active(void *context, ACameraCaptureSession *session) {
    (void)context;
    (void)session;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("on_session_active()");
#endif
}

void android_data::on_session_ready(void *context, ACameraCaptureSession *session) {
    (void)context;
    (void)session;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("on_session_ready()");
#endif
}

void android_data::on_session_closed(void *context, ACameraCaptureSession *session) {
    (void)context;
    (void)session;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("on_session_closed()");
#endif
}

void android_data::on_capture_failed(void *context, ACameraCaptureSession *session,
                                     ACaptureRequest *request, ACameraCaptureFailure *failure) {
    (void)context;
    (void)session;
    (void)request;
    (void)failure;
#ifndef NDEBUG
    spdlog::get("illixr")->debug("on_capture_failed ");
#endif
}

std::string android_data::get_back_facing_cam_id() {
    ACameraIdList *cameraIds = nullptr;
    ACameraManager_getCameraIdList(camera_manager_, &cameraIds);

    std::string backId;

#ifndef NDEBUG
    spdlog::get("illixr")->debug("found camera count %d", cameraIds->numCameras);
#endif
    bool camera_found = false;
    for (int i = 0; i < cameraIds->numCameras; ++i) {
        const char *id = cameraIds->cameraIds[i];

        ACameraMetadata *metadataObj;
        ACameraManager_getCameraCharacteristics(camera_manager_, id, &metadataObj);
#ifndef NDEBUG
        spdlog::get("illixr")->debug("get camera characteristics %d", i);
#endif

        ACameraMetadata_const_entry lensInfo = {0};
        ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_FACING, &lensInfo);
#ifndef NDEBUG
        spdlog::get("illixr")->debug("lens info %d", i);
#endif

        auto facing = static_cast<acamera_metadata_enum_android_lens_facing_t>(
                lensInfo.data.u8[0]);

        ACameraMetadata_const_entry pose_reference = {0};
        auto res = ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_POSE_REFERENCE,
                                                 &pose_reference);

        if (res != ACAMERA_OK)
            continue;
#ifndef NDEBUG
        auto pose_ref = static_cast<acamera_metadata_enum_acamera_lens_pose_reference>(
                pose_reference.data.u8[0]);

        spdlog::get("illixr")->debug("Pose reference is = %d and facing = %d", static_cast<int>(pose_ref), static_cast<int>(facing));
#endif
        // Found a back-facing camera?
        if (facing == ACAMERA_LENS_FACING_BACK) {
            backId = id;
            camera_found = true;
        }

        if (camera_found) {
            ACameraMetadata_const_entry intrinsics;
            ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_INTRINSIC_CALIBRATION,
                                          &intrinsics);
#ifndef NDEBUG
            for (int x = 0; x < intrinsics.count; ++x) {
                spdlog::get("illixr")->debug("Intrinsics total = %d : %f : index %d", intrinsics.count,
                            intrinsics.data.f[x], x);
            }
#endif

            ACameraMetadata_const_entry distortion;
            ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_DISTORTION, &distortion);
#ifndef NDEBUG
            for (int x = 0; x < distortion.count; ++x) {
                spdlog::get("illixr")->debug("distortion  = %d : %f : index %d", distortion.count,
                            distortion.data.f[x], x);
            }
#endif

            ACameraMetadata_const_entry rotation;
            ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_POSE_ROTATION, &rotation);
#ifndef NDEBUG
            for (int x = 0; x < rotation.count; ++x) {
                spdlog::get("illixr")->debug("rotation total = %d : %f : index %d", rotation.count,
                            rotation.data.f[x], x);
            }
#endif

            ACameraMetadata_const_entry translation;
            ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_POSE_TRANSLATION, &translation);
#ifndef NDEBUG
            for (int x = 0; x < translation.count; ++x) {
                spdlog::get("illixr")->debug("translation total = %d : %f : index %d", translation.count,
                                             translation.data.f[x], x);
            }
#endif
            break;
        }


    }
#ifndef NDEBUG
    spdlog::get("illixr")->debug("back camera id %s", backId.c_str());
#endif

    ACameraManager_deleteCameraIdList(cameraIds);
    return backId;
}

void android_data::init_cam() {
#ifndef NDEBUG
    spdlog::get("illixr")->debug("INIT CAM");
#endif
    camera_manager_ = ACameraManager_create();
    auto id = get_back_facing_cam_id();
    ACameraManager_openCamera(camera_manager_, id.c_str(), &camera_device_callbacks_,
                              &camera_device_);

    ACameraDevice_createCaptureRequest(camera_device_, TEMPLATE_PREVIEW, &request_);
    ACaptureSessionOutputContainer_create(&outputs_);
    image_reader_ = create_jpeg_reader();
    image_window_ = create_surface(image_reader_);
    ANativeWindow_acquire(image_window_);
    ACameraOutputTarget_create(image_window_, &image_target_);
    ACaptureRequest_addTarget(request_, image_target_);

    ACaptureSessionOutput_create(image_window_, &image_output_);
    ACaptureSessionOutputContainer_add(outputs_, image_output_);
    // Create the session
    ACameraDevice_createCaptureSession(camera_device_, outputs_, &session_state_callbacks_,
                                       &capture_session_);
#ifndef NDEBUG
    camera_status_t status = ACameraCaptureSession_setRepeatingRequest(capture_session_,
                                                                       &capture_callbacks_, 1,
                                                                       &request_, nullptr);
    spdlog::get("illixr")->debug("ACameraCaptureSession_setRepeatingRequest status = %d", static_cast<int>(status));
#endif
}

/// For `threadloop` style plugins, do not override the start() method unless you know what you're doing!
/// _p_one_iteration() is called in a thread created by threadloop::start()
void android_data::_p_one_iteration() {
    //ANDROID_LOG("Android_cam started ..");
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    if (!img_ready_)
        return;
    if (!clock_->is_started()) {
        return;
    }
    if (imu_ts_ == 0)
        return;
    auto start = std::chrono::high_resolution_clock::now();


    double ts = std::chrono::system_clock::now().time_since_epoch().count();//current_ts;
    ullong cam_time = static_cast<ullong>(ts * 1000);
    if (!first_imu_time_) {
        first_imu_time_ = cam_time;//imu_ts;
        first_real_time_imu_ = clock_->now();
    }
    //ullong last_imu_time = 0;
    //double last_cam_time = 0;
    lock_.lock();
    Eigen::Vector3d cur_gyro = gyro_;
    Eigen::Vector3d cur_accel = accel_;
    counter_++;
//        myfile << std::to_string((uint64_t)ts*1000) + "," + std::to_string(gyro[0]) + "," + std::to_string(gyro[1]) + "," +
//                  std::to_string(gyro[2]) + "," + std::to_string(accel[0]) + ","  + std::to_string(accel[1]) + "," + std::to_string(accel[2])  + "\n";
    //last_imu_time = imu_ts;
    imu_time_ = 0;
    lock_.unlock();

    cv::Mat ir_left;
    cv::Mat ir_right;
    if (counter_ % 10 == 0) {
        mtx_.lock();
        ir_left = last_image_;
        ir_right = last_image_;
//            uint64_t name = (uint64_t)ts*1000;
//            bool check = cv::imwrite(
//                    "/sdcard/Android/data/com.example.native_activity/cam0/"+ std::to_string(name)+".png", last_image);
//            ANDROID_LOG("ANDROID CAM CHECK %d", check);
//            camfile << std::to_string((uint64_t)ts*1000) << "," << std::to_string(name) + ".png" <<std::endl;
        //last_cam_time = cam_proc_time;
        cam_proc_time_ = 0;
        mtx_.unlock();
    }

//        time_point cam_time_point{*first_real_time_imu_ + std::chrono::nanoseconds(last_imu_time - *first_imu_time_)};

    time_point curr_time = clock_->now();
    //ANDROID_LOG("TIME = %lf and cam_time %llu",duration_2_double(std::chrono::nanoseconds(cam_time - *_m_first_cam_time)), cam_time);
//        _m_cam.put(_m_cam.allocate<cam_type>({cam_time_point, ir_left, ir_right}));
    imu_.put(imu_.allocate<data_format::imu_type>(
            data_format::imu_type{time_point{curr_time},
                                      cur_gyro,
                                      cur_accel}));
    cam_.put(cam_.allocate<data_format::binocular_cam_type>(
            data_format::binocular_cam_type{time_point{curr_time},
                                            ir_left.clone(),
                                            ir_right.clone()}));
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
#ifndef NDEBUG
    spdlog::get("illixr")->debug("duration: %f", duration_to_double(duration));
    spdlog::get("illixr")->debug("AG: %f, %f, %f, %f, %f, %f", cur_accel[0], cur_accel[1], cur_accel[2], cur_gyro[0],
                cur_gyro[1], cur_gyro[2]);
#endif
//        sl->write_duration("imu_cam", duration_2_double(duration) + last_imu_time + last_cam_time);
}


PLUGIN_MAIN(android_data);

