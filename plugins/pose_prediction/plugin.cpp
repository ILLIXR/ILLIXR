#include "illixr/plugin.hpp"

#include "illixr/data_format.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/pose_prediction.hpp"
#include "illixr/runge-kutta.hpp"

#include <eigen3/Eigen/Dense>
#include <filesystem>
#include <shared_mutex>

using namespace ILLIXR;

class pose_prediction_impl : public pose_prediction {
public:
    explicit pose_prediction_impl(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_slow_pose{sb->get_reader<pose_type>("slow_pose")}
        , _m_imu_raw{sb->get_reader<imu_raw_type>("imu_raw")}
        , _m_true_pose{sb->get_reader<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_reader<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_vsync_estimate{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} { }

    // No parameter get_fast_pose() should just predict to the next vsync
    // However, we don't have vsync estimation yet.
    // So we will predict to `now()`, as a temporary approximation
    fast_pose_type get_fast_pose() const override {
        switchboard::ptr<const switchboard::event_wrapper<time_point>> vsync_estimate = _m_vsync_estimate.get_ro_nullable();

        if (vsync_estimate == nullptr) {
            return get_fast_pose(_m_clock->now());
        } else {
            return get_fast_pose(*vsync_estimate);
        }
    }

    pose_type get_true_pose() const override {
        switchboard::ptr<const pose_type>                                   pose_ptr = _m_true_pose.get_ro_nullable();
        switchboard::ptr<const switchboard::event_wrapper<Eigen::Vector3f>> offset_ptr =
            _m_ground_truth_offset.get_ro_nullable();

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
            offset_pose.sensor_time = _m_clock->now();
            offset_pose.position    = Eigen::Vector3f{0, 0, 0};
            offset_pose.orientation = Eigen::Quaternionf{1, 0, 0, 0};
        }

        return correct_pose(offset_pose);
    }

    // future_time: An absolute timepoint in the future
    fast_pose_type get_fast_pose(time_point future_timestamp) const override {
        switchboard::ptr<const pose_type> slow_pose = _m_slow_pose.get_ro_nullable();
        if (slow_pose == nullptr) {
            // No slow pose, return 0
            return fast_pose_type{
                correct_pose(pose_type{}),
                _m_clock->now(),
                future_timestamp,
            };
        }

        switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get_ro_nullable();
        if (imu_raw == nullptr) {
#ifndef NDEBUG
            spdlog::get("illixr")->debug("[POSEPREDICTION] FAST POSE IS SLOW POSE!");
#endif
            // No imu_raw, return slow_pose
            return fast_pose_type{
                .pose                  = correct_pose(*slow_pose),
                .predict_computed_time = _m_clock->now(),
                .predict_target_time   = future_timestamp,
            };
        }

        // slow_pose and imu_raw, do pose prediction

        double    dt         = duration2double(future_timestamp - imu_raw->imu_time);
        StatePlus state_plus = predict_mean_rk4(dt, StatePlus(imu_raw->quat, imu_raw->vel, imu_raw->pos), imu_raw->w_hat, imu_raw->a_hat,
                                                imu_raw->w_hat2, imu_raw->a_hat2);

        // predictor_imu_time is the most recent IMU sample that was used to compute the prediction.
        auto predictor_imu_time = imu_raw->imu_time;

        pose_type predicted_pose =
            correct_pose({predictor_imu_time, state_plus.position.cast<float>(), state_plus.orientation.cast<float>()});

        // Make the first valid fast pose be straight ahead.
        if (first_time) {
            std::unique_lock lock{offset_mutex};
            // check again, now that we have mutual exclusion
            if (first_time) {
                first_time = false;
                offset     = predicted_pose.orientation.inverse();
            }
        }

        // Several timestamps are logged:
        //       - the prediction compute time (time when this prediction was computed, i.e., now)
        //       - the prediction target (the time that was requested for this pose.)
        return fast_pose_type{
            .pose = predicted_pose, .predict_computed_time = _m_clock->now(), .predict_target_time = future_timestamp};
    }

    void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
        std::unique_lock   lock{offset_mutex};
        Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
        offset                   = raw_o.inverse();
        /*
          Now, `raw_o` is maps to the identity quaternion.
          Proof:
          apply_offset(raw_o)
              = raw_o * offset
              = raw_o * raw_o.inverse()
              = Identity.
         */
    }

    Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
        std::shared_lock lock{offset_mutex};
        return orientation * offset;
    }

    bool fast_pose_reliable() const override {
        return _m_slow_pose.get_ro_nullable() && _m_imu_raw.get_ro_nullable();
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

    bool true_pose_reliable() const override {
        // return _m_true_pose.valid();
        /*
          We do not have a "ground truth" available in all cases, such
          as when reading live data.
         */
        return bool(_m_true_pose.get_ro_nullable());
    }

    Eigen::Quaternionf get_offset() override {
        return offset;
    }

    // Correct the orientation of the pose due to the lopsided IMU in the
    // current Dataset we are using (EuRoC)
    pose_type correct_pose(const pose_type& pose) const override {
        pose_type swapped_pose;

        // Make any changes to the axes direction below
        // This is a mapping between the coordinate system of the current
        // SLAM (OpenVINS) we are using and the OpenGL system.
        swapped_pose.position.x() = -pose.position.y();
        swapped_pose.position.y() = pose.position.z();
        swapped_pose.position.z() = -pose.position.x();

        // Make any chanes to orientation of the output below
        // For the dataset were currently using (EuRoC), the output orientation acts as though
        // the "top of the head" is the forward direction, and the "eye direction" is the up direction.
        Eigen::Quaternionf raw_o(pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

        swapped_pose.orientation = apply_offset(raw_o);
        swapped_pose.sensor_time = pose.sensor_time;

        return swapped_pose;
    }

private:
    mutable std::atomic<bool>                                        first_time{true};
    const std::shared_ptr<switchboard>                               sb;
    const std::shared_ptr<const RelativeClock>                       _m_clock;
    switchboard::reader<pose_type>                                   _m_slow_pose;
    switchboard::reader<imu_raw_type>                                _m_imu_raw;
    switchboard::reader<pose_type>                                   _m_true_pose;
    switchboard::reader<switchboard::event_wrapper<Eigen::Vector3f>> _m_ground_truth_offset;
    switchboard::reader<switchboard::event_wrapper<time_point>>      _m_vsync_estimate;
    mutable Eigen::Quaternionf                                       offset{Eigen::Quaternionf::Identity()};
    mutable std::shared_mutex                                        offset_mutex;

    /**
     * @brief Normalizes a quaternion to make sure it is unit norm
     * @param q_t Quaternion to normalized
     * @return Normalized quaterion
     */
    static inline Eigen::Matrix<double, 4, 1> quatnorm(Eigen::Matrix<double, 4, 1> q_t) {
        if (q_t(3, 0) < 0) {
            q_t *= -1;
        }
        return q_t / q_t.norm();
    }
};

class pose_prediction_plugin : public plugin {
public:
    pose_prediction_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb} {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(std::make_shared<pose_prediction_impl>(pb)));
    }
};

PLUGIN_MAIN(pose_prediction_plugin);
