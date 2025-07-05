#include "zed_camera_thread.hpp"

#include "include/zed_opencv.hpp"

using namespace ILLIXR;
using namespace ILLIXR::data_format;

void transform_zed_pose(sl::Transform& from_pose, sl::Transform& to_pose, float ty) {
    sl::Transform transform_;
    transform_.setIdentity();
    transform_.ty = ty;
    to_pose       = sl::Transform::inverse(transform_) * from_pose * transform_;
}

zed_camera_thread::zed_camera_thread(const std::string& name_, phonebook* pb_, std::shared_ptr<zed_camera> zed_cam)
    : threadloop{name_, pb_}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , cam_{switchboard_->get_writer<cam_type_zed>("cam_zed")}
    , zed_cam_{std::move(zed_cam)}
    , image_size_{zed_cam_->getCameraInformation().camera_configuration.resolution} {
    // runtime_parameters.sensing_mode = SENSING_MODE::STANDARD;
    // Image setup
    imageL_zed_.alloc(image_size_.width, image_size_.height, sl::MAT_TYPE::U8_C4, sl::MEM::CPU);
    imageR_zed_.alloc(image_size_.width, image_size_.height, sl::MAT_TYPE::U8_C4, sl::MEM::CPU);
    rgb_zed_.alloc(image_size_.width, image_size_.height, sl::MAT_TYPE::U8_C4, sl::MEM::CPU);
    depth_zed_.alloc(image_size_.width, image_size_.height, sl::MAT_TYPE::F32_C1, sl::MEM::CPU);
    confidence_zed_.alloc(image_size_.width, image_size_.height, sl::MAT_TYPE::F32_C1, sl::MEM::CPU);

    imageL_ocv_     = slMat_to_cvMat(imageL_zed_);
    imageR_ocv_     = slMat_to_cvMat(imageR_zed_);
    rgb_ocv_        = slMat_to_cvMat(rgb_zed_);
    depth_ocv_      = slMat_to_cvMat(depth_zed_);
    confidence_ocv_ = slMat_to_cvMat(confidence_zed_);
}

threadloop::skip_option zed_camera_thread::_p_should_skip() {
    if (zed_cam_->grab(runtime_parameters_) == sl::ERROR_CODE::SUCCESS) {
        return skip_option::run;
    } else {
        return skip_option::skip_and_spin;
    }
}

void zed_camera_thread::stop() {
    zed_cam_->close();
    threadloop::stop();
}

void zed_camera_thread::_p_one_iteration() {
    RAC_ERRNO_MSG("zed at start of _p_one_iteration");

    // Time as ullong (nanoseconds)
    // ullong cam_time = static_cast<ullong>(zedm->getTimestamp(TIME_REFERENCE::IMAGE).getNanoseconds());

    // Retrieve images
    zed_cam_->retrieveImage(imageL_zed_, sl::VIEW::LEFT, sl::MEM::CPU, image_size_);
    zed_cam_->retrieveImage(imageR_zed_, sl::VIEW::RIGHT, sl::MEM::CPU, image_size_);
    zed_cam_->retrieveMeasure(depth_zed_, sl::MEASURE::DEPTH, sl::MEM::CPU, image_size_);
    zed_cam_->retrieveImage(rgb_zed_, sl::VIEW::LEFT, sl::MEM::CPU, image_size_);
    zed_cam_->retrieveMeasure(confidence_zed_, sl::MEASURE::CONFIDENCE);

    multi_pose_map poses;
    if (zed_cam_->grab() == sl::ERROR_CODE::SUCCESS) {
        sl::Pose zed_pose_left;
        // Get the pose of the camera relative to the world frame
        sl::POSITIONAL_TRACKING_STATE state = zed_cam_->getPosition(zed_pose_left);
        // if (state != sl::POSITIONAL_TRACKING_STATE::OK)
        //     throw std::runtime_error("Tracking failed");
        sl::Pose zed_pose_right{zed_pose_left};
        transform_zed_pose(zed_pose_left.pose_data, zed_pose_right.pose_data, zed_cam_->getBaseline());
        pose_type left_eye_pose{
            time_point(clock_duration_(zed_pose_left.timestamp.getNanoseconds())),
            time_point(clock_duration_(zed_pose_left.timestamp.getNanoseconds())),
            {zed_pose_left.getTranslation().tx, zed_pose_left.getTranslation().ty, zed_pose_left.getTranslation().tz},
            {zed_pose_left.getOrientation().w, zed_pose_left.getOrientation().x, zed_pose_left.getOrientation().y,
             zed_pose_left.getOrientation().z},
            units::UNITS};
        pose_type right_eye_pose{
            time_point(clock_duration_(zed_pose_right.timestamp.getNanoseconds())),
            time_point(clock_duration_(zed_pose_right.timestamp.getNanoseconds())),
            {zed_pose_right.getTranslation().tx, zed_pose_right.getTranslation().ty, zed_pose_right.getTranslation().tz},
            {zed_pose_right.getOrientation().w, zed_pose_right.getOrientation().x, zed_pose_right.getOrientation().y,
             zed_pose_right.getOrientation().z},
            units::UNITS};
        poses = {{units::LEFT_EYE, left_eye_pose}, {units::RIGHT_EYE, right_eye_pose}};
    }

    clock_duration_ ts = clock_duration_(zed_cam_->getTimestamp(sl::TIME_REFERENCE::IMAGE).getNanoseconds());
    cam_.put(cam_.allocate<cam_type_zed>({time_point{ts}, imageL_ocv_.clone(), imageR_ocv_.clone(), rgb_ocv_.clone(),
                                          depth_ocv_.clone(), confidence_ocv_.clone(), ++serial_no_, poses}));

    RAC_ERRNO_MSG("zed_cam at end of _p_one_iteration");
}
