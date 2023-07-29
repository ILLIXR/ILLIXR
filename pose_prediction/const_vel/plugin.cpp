#include "common/plugin.hpp"

#include "common/data_format.hpp"
#include "common/matrix.hpp"
#include "common/phonebook.hpp"
#include "common/plugin.hpp"
#include "common/pose_prediction.hpp"

#include <eigen3/Eigen/Dense>
#include <shared_mutex>

using namespace ILLIXR;

class const_vel_predictor_impl : public pose_prediction {
public:
    const_vel_predictor_impl(const phonebook* const pb)
        : sb{pb->lookup_impl<switchboard>()}
        , _m_clock{pb->lookup_impl<RelativeClock>()}
        , _m_slow_pose{sb->get_reader<pose_type>("slow_pose")}
        , _m_imu_raw{sb->get_reader<imu_raw_type>("imu_raw")}
        , _m_true_pose{sb->get_reader<pose_type>("true_pose")}
        , _m_ground_truth_offset{sb->get_reader<switchboard::event_wrapper<Eigen::Vector3f>>("ground_truth_offset")}
        , _m_vsync_estimate{sb->get_reader<switchboard::event_wrapper<time_point>>("vsync_estimate")} { }

    // No parameter get_fast_pose() predicts to the next vsync estimate, if one exists, otherwise predicts to 'now'
    virtual fast_pose_type get_fast_pose() const override {
        switchboard::ptr<const switchboard::event_wrapper<time_point>> vsync_estimate = _m_vsync_estimate.get_ro_nullable();

        if (vsync_estimate == nullptr) {
            return get_fast_pose(_m_clock->now());
        } else {
            return get_fast_pose(*vsync_estimate);
        }
    }

    virtual pose_type get_true_pose() const override {
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
    virtual fast_pose_type get_fast_pose(time_point future_timestamp) const override {
        switchboard::ptr<const pose_type> slow_pose = _m_slow_pose.get_ro_nullable();
        if (slow_pose == nullptr) {
            // No slow pose, return identity pose
            return fast_pose_type{
                .pose                  = correct_pose(pose_type{}),
                .predict_computed_time = _m_clock->now(),
                .predict_target_time   = future_timestamp,
            };
        }

        switchboard::ptr<const imu_raw_type> imu_raw = _m_imu_raw.get_ro_nullable();
        if (imu_raw == nullptr) {
#ifndef NDEBUG
            printf("Fast pose is slow pose!\n");
#endif
            // No imu_raw, return slow_pose
            return fast_pose_type{
                .pose                  = correct_pose(*slow_pose),
                .predict_computed_time = _m_clock->now(),
                .predict_target_time   = future_timestamp,
            };
        }

        // Both slow_pose and imu_raw are available, do pose prediction
        double                                              dt = duration2double(future_timestamp - imu_raw->imu_time);
        std::pair<Eigen::Matrix<double, 13, 1>, time_point> predictor_result = predict_const_vel(dt);
        auto                                                state_plus       = predictor_result.first;

        // predictor_imu_time is the most recent IMU sample that was used to compute the prediction.
        auto predictor_imu_time = predictor_result.second;

        pose_type predicted_pose =
            correct_pose({predictor_imu_time,
                          Eigen::Vector3f{static_cast<float>(state_plus(4)), static_cast<float>(state_plus(5)),
                                          static_cast<float>(state_plus(6))},
                          Eigen::Quaternionf{static_cast<float>(state_plus(3)), static_cast<float>(state_plus(0)),
                                             static_cast<float>(state_plus(1)), static_cast<float>(state_plus(2))}});

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
        // - the prediction compute time (time when this prediction was computed, i.e., now)
        // - the prediction target (the time that was requested for this pose.)
        return fast_pose_type{
            .pose = predicted_pose, .predict_computed_time = _m_clock->now(), .predict_target_time = future_timestamp};
    }

    virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
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

    virtual bool fast_pose_reliable() const override {
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

    virtual bool true_pose_reliable() const override {
        /*
          We do not have a ground truth available in all cases, such
          as when reading live data.
         */
        return bool(_m_true_pose.get_ro_nullable());
    }

    virtual Eigen::Quaternionf get_offset() override {
        return offset;
    }

    // Correct the orientation of the pose due to the lopsided IMU in the
    // current Dataset we are using (EuRoC)
    virtual pose_type correct_pose(const pose_type pose) const override {
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

    // Based on https://github.com/rpng/open_vins/issues/92
    // Returns a pair of the predictor state and the time associated with the
    // most recent imu reading used to perform this prediction.
    std::pair<Eigen::Matrix<double, 13, 1>, time_point> predict_const_vel(double dt) const {
        const auto imu_raw = _m_imu_raw.get_ro();

        // Current state
        const auto curr_time = imu_raw->imu_time;
        // TODO: should this be the last w or the estimated w in the VIO/integrator state?
        const auto curr_w    = 0.5 * (imu_raw->w_hat + imu_raw->w_hat2);
        const auto curr_quat = imu_raw->quat;
        const auto curr_R = quat_2_Rot(Eigen::Matrix<double, 4, 1>{curr_quat.x(), curr_quat.y(), curr_quat.z(), curr_quat.w()});
        const auto curr_pos = imu_raw->pos;
        const auto curr_vel = imu_raw->vel;

        // Compute new quaternion

        // Option 1: PG's code from issue 92. Same as Propagator.cpp
        const auto future_q = rot_2_quat(exp_so3(-curr_w * dt) * curr_R);

        // Option 2: PG's formula from issue 92. Same as Trawny eq. 96.
        // const auto future_q = rot_2_quat(exp_so3(0.5 * Omega(curr_w) * dt) * curr_R);

        // Compute new position
        const auto delta_pos  = curr_vel * dt;
        const auto future_pos = curr_pos + delta_pos;

        // Assemble state
        Eigen::Matrix<double, 13, 1> state_plus = Eigen::Matrix<double, 13, 1>::Zero();
        state_plus.block(0, 0, 4, 1)            = future_q;
        state_plus.block(4, 0, 3, 1)            = future_pos;
        state_plus.block(7, 0, 3, 1)            = curr_vel;

        return {state_plus, curr_time};
    }
};

class const_vel_predictor_plugin : public plugin {
public:
    const_vel_predictor_plugin(const std::string& name, phonebook* pb)
        : plugin{name, pb} {
        pb->register_impl<pose_prediction>(
            std::static_pointer_cast<pose_prediction>(std::make_shared<const_vel_predictor_impl>(pb)));
    }
};

PLUGIN_MAIN(const_vel_predictor_plugin);
