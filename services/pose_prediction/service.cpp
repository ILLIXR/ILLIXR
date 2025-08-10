#include "service.hpp"

#include "illixr/runge-kutta.hpp"

#include <eigen3/Eigen/Dense>
#include <filesystem>
#include <shared_mutex>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

pose_prediction_impl::pose_prediction_impl(const phonebook* const pb)
    : switchboard_{pb->lookup_impl<switchboard>()}
    , clock_{pb->lookup_impl<relative_clock>()}
    , slow_pose_{switchboard_->get_reader<pose_type>("slow_pose")}
    , imu_raw_{switchboard_->get_reader<imu_raw_type>("imu_raw")}
    , true_pose_{switchboard_->get_reader<pose_type>("true_pose")}
    , ground_truth_offset_{switchboard_->get_reader<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
    , vsync_estimate_{switchboard_->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")}
    , using_lighthouse_{switchboard_->get_env_bool("ILLIXR_LIGHTHOUSE")} { 
        if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
            // setup_pose_reader();
            setup_fake_poses();
        }
    }
fast_pose_type pose_prediction_impl::get_fake_render_pose() {
    // If we are comparing images, return pose from the pose reader
    // if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
    //     if (!render_poses_.empty()) {
    //         fast_pose_type render_pose = render_poses_.front();
    //         render_poses_.erase(render_poses_.begin());
    //         return render_pose;
    //     } else {
    //         spdlog::get("illixr")->warn("[POSEPREDICTION] No render poses available, returning zero pose.");
    //         return fast_pose_type{correct_pose(pose_type{}), clock_->now(), clock_->now()};
    //     }
    // }
    if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
        return fake_render_pose_;
    } else {
        return get_fast_pose();
    }
}

fast_pose_type pose_prediction_impl::get_fake_warp_pose() {
    // If we are comparing images, return pose from the pose reader
    // if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
    //     if (!warp_poses_.empty()) {
    //         fast_pose_type warp_pose = warp_poses_.front();
    //         warp_poses_.erase(warp_poses_.begin());
    //         return warp_pose;
    //     } else {
    //         spdlog::get("illixr")->warn("[POSEPREDICTION] No warp poses available, returning zero pose.");
    //         return fast_pose_type{correct_pose(pose_type{}), clock_->now(), clock_->now()};
    //     }
    // }
    if (switchboard_->get_env_bool("ILLIXR_COMPARE_IMAGES")) {
        return fake_warp_pose_;
    } else {
        return get_fast_pose();
    }
}

// No parameter get_fast_pose() should just predict to the next vsync
// However, we don't have vsync estimation yet.
// So we will predict to `now()`, as a temporary approximation
fast_pose_type pose_prediction_impl::get_fast_pose() const {

    switchboard::ptr<const switchboard::event_wrapper<time_point>> vsync_estimate = vsync_estimate_.get_ro_nullable();

    if (vsync_estimate == nullptr) {
        // Even if no vsync is available, we should at least make a reasonable guess
        return get_fast_pose(clock_->now());
    } else {
        time_point vsync_time = vsync_estimate->operator time_point();
        // spdlog::get("illixr")->info("[POSEPREDICTION] FAST POSE IS PREDICTED TO VSYNC {}!", vsync_time.time_since_epoch().count());
        return get_fast_pose(vsync_time);
    }
}

pose_type pose_prediction_impl::get_true_pose() const {
    switchboard::ptr<const pose_type>                                   pose_ptr   = true_pose_.get_ro_nullable();
    switchboard::ptr<const switchboard::event_wrapper<Eigen::Vector3f>> offset_ptr = ground_truth_offset_.get_ro_nullable();

    pose_type offset_pose;

    // Subtract offset if valid pose and offset, otherwise use zero pose.
    // Checking that pose and offset are both valid is safer than just
    // checking one or the other because it assumes nothing about the
    // ordering of writes on the producer's end or about the producer
    // actually writing to both streams.
    if (pose_ptr != nullptr && offset_ptr != nullptr) {
        offset_pose = *pose_ptr;
        offset_pose.position -= **offset_ptr;
    } else {
        offset_pose.imu_time = clock_->now();
        offset_pose.position    = Eigen::Vector3f{0, 0, 0};
        offset_pose.orientation = Eigen::Quaternionf{1, 0, 0, 0};
    }

    return correct_pose(offset_pose);
}

// future_time: An absolute timepoint in the future
fast_pose_type pose_prediction_impl::get_fast_pose(time_point future_timestamp) const {
    switchboard::ptr<const pose_type> slow_pose = slow_pose_.get_ro_nullable();
    if (slow_pose == nullptr) {
        // No slow pose, return 0
        return fast_pose_type{
            correct_pose(pose_type{}),
            clock_->now(),
            future_timestamp,
        };
    }

    switchboard::ptr<const imu_raw_type> imu_raw = imu_raw_.get_ro_nullable();
    if (imu_raw == nullptr) {
        if (!using_lighthouse_)
            spdlog::get("illixr")->debug("[POSEPREDICTION] FAST POSE IS SLOW POSE!");

        // No imu_raw, return slow_pose
        return fast_pose_type{
            correct_pose(*slow_pose),
            clock_->now(),
            future_timestamp,
        };
    }

    // slow_pose and imu_raw, do pose prediction

    double     dt      = duration_to_double(future_timestamp - imu_raw->imu_time);
    state_plus state_p = ::ILLIXR::predict_mean_rk4(dt, state_plus(imu_raw->quat, imu_raw->vel, imu_raw->pos), imu_raw->w_hat,
                                                    imu_raw->a_hat, imu_raw->w_hat2, imu_raw->a_hat2);

    // predictor_imu_time is the most recent IMU sample that was used to compute the prediction.
    auto predictor_imu_time = imu_raw->imu_time;

    pose_type predicted_pose =
        correct_pose({imu_raw->cam_time, predictor_imu_time, state_p.position.cast<float>(), state_p.orientation.cast<float>()});

    // Make the first valid fast pose be straight ahead.
    if (first_time_) {
        std::unique_lock lock{offset_mutex_};
        // check again, now that we have mutual exclusion
        if (first_time_) {
            first_time_ = false;
            offset_     = predicted_pose.orientation.inverse();
        }
    }

    // Several timestamps are logged:
    //       - the prediction compute time (time when this prediction was computed, i.e., now)
    //       - the prediction target (the time that was requested for this pose.)
    return fast_pose_type{predicted_pose, clock_->now(), future_timestamp};
}

void pose_prediction_impl::set_offset(const Eigen::Quaternionf& raw_o_times_offset) {
    std::unique_lock   lock{offset_mutex_};
    Eigen::Quaternionf raw_o = raw_o_times_offset * offset_.inverse();
    offset_                  = raw_o.inverse();
    /*
      Now, `raw_o` is maps to the identity quaternion.
      Proof:
      apply_offset(raw_o)
          = raw_o * offset_
          = raw_o * raw_o.inverse()
          = Identity.
     */
}

Eigen::Quaternionf pose_prediction_impl::apply_offset(const Eigen::Quaternionf& orientation) const {
    std::shared_lock lock{offset_mutex_};
    return orientation * offset_;
}

bool pose_prediction_impl::fast_pose_reliable() const {
    if (using_lighthouse_)
        return true;

    return slow_pose_.get_ro_nullable() && imu_raw_.get_ro_nullable();
    /*
      SLAM takes some time to initialize, so initially fast_pose
      is unreliable.

  In such cases, we might return a fast_pose based only on the
  IMU data (currently, we just return a zero-pose)., and mark
  it as "unreliable"

  This way, there always a pose coming out of pose_prediction,
  representing our best guess at that time, and we indicate
  how reliable that guess is here.

 */
}

bool pose_prediction_impl::true_pose_reliable() const {
    // return true_pose_.valid();
    /*
      We do not have a "ground truth" available in all cases, such
      as when reading live data.
     */
    return bool(true_pose_.get_ro_nullable());
}

Eigen::Quaternionf pose_prediction_impl::get_offset() {
    return offset_;
}

// Correct the orientation of the pose due to the lopsided IMU in the
// current Dataset we are using (EuRoC)
pose_type pose_prediction_impl::correct_pose(const pose_type& pose) const {
    if (using_lighthouse_) // The lighthouse plugin should already apply the correct pose.
        return pose;

    pose_type swapped_pose;

    // Make any changes to the axes direction below
    // This is a mapping between the coordinate system of the current
    // SLAM (OpenVINS) we are using and the OpenGL system.
    swapped_pose.position.x() = -pose.position.y();
    swapped_pose.position.y() = pose.position.z();
    swapped_pose.position.z() = -pose.position.x();

    // Make any changes to orientation of the output below
    // For the dataset were currently using (EuRoC), the output orientation acts as though
    // the "top of the head" is the forward direction, and the "eye direction" is the up direction.
    Eigen::Quaternionf raw_o(pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

    swapped_pose.orientation = apply_offset(raw_o);
    swapped_pose.imu_time = pose.imu_time;
    swapped_pose.cam_time = pose.cam_time;

    return swapped_pose;
}

class pose_prediction_plugin : public plugin {
public:
    [[maybe_unused]] pose_prediction_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb} {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(std::make_shared<pose_prediction_impl>(pb)));
    }
};

PLUGIN_MAIN(pose_prediction_plugin)
