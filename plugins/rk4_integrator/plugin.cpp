// This entire IMU integrator has been ported almost as-is from the original OpenVINS integrator, which
// can be found here: https://github.com/rpng/open_vins/blob/master/ov_msckf/src/state/Propagator.cpp

#include "plugin.hpp"

#include "illixr/runge-kutta.hpp"

#include <chrono>
#include <eigen3/Eigen/Dense>
#include <iomanip>
#include <memory>
#include <vector>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

constexpr duration IMU_SAMPLE_LIFETIME{std::chrono::seconds{5}};

[[maybe_unused]] rk4_integrator::rk4_integrator(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , imu_integrator_input_{switchboard_->get_reader<imu_integrator_input>("imu_integrator_input")}
    , imu_raw_{switchboard_->get_writer<imu_raw_type>("imu_raw")} {
    switchboard_->schedule<imu_type>(id_, "imu", [&](const switchboard::ptr<const imu_type>& datum, size_t) {
        callback(datum);
    });
}

void rk4_integrator::callback(const switchboard::ptr<const imu_type>& datum) {
    imu_vec_.emplace_back(datum->time, datum->angular_v, datum->linear_a);

    clean_imu_vec(datum->time);
    propagate_imu_values(datum->time);

    RAC_ERRNO_MSG("rk4_integrator");
}

// Clean IMU values older than IMU_SAMPLE_LIFETIME seconds
void rk4_integrator::clean_imu_vec(time_point timestamp) {
    auto it0 = imu_vec_.begin();
    while (it0 != imu_vec_.end()) {
        if (timestamp - it0->time < IMU_SAMPLE_LIFETIME) {
            break;
        }
        it0 = imu_vec_.erase(it0);
    }
}

// Timestamp we are propagating the biases to (new IMU reading time)
void rk4_integrator::propagate_imu_values(time_point real_time) {
    auto input_values = imu_integrator_input_.get_ro_nullable();
    if (input_values == nullptr) {
        return;
    }

    if (!has_last_offset_) {
        /// TODO: Should be set and tested at the end of this function to avoid staleness from VIO.
        last_imu_offset_ = input_values->t_offset;
        has_last_offset_ = true;
    }

    proper_quaterniond          curr_quat = {input_values->quat.w(), input_values->quat.x(), input_values->quat.y(),
                                             input_values->quat.z()};
    Eigen::Matrix<double, 3, 1> curr_pos  = input_values->position;
    Eigen::Matrix<double, 3, 1> curr_vel  = input_values->velocity;

    // Uncomment this for some helpful prints
    // total_imu_++;
    // if (input_values->last_cam_integration_time > last_cam_time_) {
    // 	cam_count_++;
    // 	last_cam_time_ = input_values->last_cam_integration_time;
    // 	std::cout << "Num IMUs received since last cam: " << counter_ << " Diff between new cam and latest IMU: "
    // 			  << timestamp - last_cam_time_ << " Expected IMUs received VS Actual: " << cam_count_*10 << ", " <<
    // total_imu_
    // << std::endl; 	counter_ = 0;
    // }
    // counter_++;

    // Get what our IMU-camera offset should be (t_imu = t_cam + calib_dt)
    duration t_off_new = input_values->t_offset;

    // This is the last CAM time
    time_point time0 = input_values->last_cam_integration_time + last_imu_offset_;
    time_point time1 = real_time + t_off_new;

    std::vector<imu_type>       prop_data = select_imu_readings(imu_vec_, time0, time1);
    Eigen::Matrix<double, 3, 1> w_hat;
    Eigen::Matrix<double, 3, 1> a_hat;
    Eigen::Matrix<double, 3, 1> w_hat2;
    Eigen::Matrix<double, 3, 1> a_hat2;

    // Loop through all IMU messages, and use them to move the state forward in time
    // This uses the zero'th order quat, and then constant acceleration discrete
    if (prop_data.size() > 1) {
        spdlog::get("illixr")->debug(std::to_string(real_time.time_since_epoch().count()) + " Integrating over " + std::to_string(prop_data.size()) + " values");
        for (size_t i = 0; i < prop_data.size() - 1; i++) {
            // Time elapsed over interval
            double dt = duration_to_double(prop_data[i + 1].time - prop_data[i].time);

            // Corrected imu measurements
            w_hat  = prop_data[i].angular_v - input_values->bias_gyro;
            a_hat  = prop_data[i].linear_a - input_values->bias_acc;
            w_hat2 = prop_data[i + 1].angular_v - input_values->bias_gyro;
            a_hat2 = prop_data[i + 1].linear_a - input_values->bias_acc;

            // Compute the new state mean value
            state_plus sp =
                ::ILLIXR::predict_mean_rk4(dt, state_plus(curr_quat, curr_vel, curr_pos), w_hat, a_hat, w_hat2, a_hat2);

            curr_quat = sp.orientation;
            curr_pos  = sp.position;
            curr_vel  = sp.velocity;
        }
    }
    spdlog::get("illixr")->debug("  Pos " + std::to_string(curr_pos.x()) + ", " + std::to_string(curr_pos.y()) + ", " + std::to_string(curr_pos.z()));
    spdlog::get("illixr")->debug("  Quat " + std::to_string(curr_quat.w()) + ", " + std::to_string(curr_quat.x()) + ", " + std::to_string(curr_quat.y()) + ", " + std::to_string(curr_quat.z()));
    imu_raw_.put(imu_raw_.allocate(w_hat, a_hat, w_hat2, a_hat2, curr_pos, curr_vel, curr_quat, real_time));
}

// Select IMU readings based on timestamp similar to how OpenVINS selects IMU values to propagate
std::vector<imu_type> rk4_integrator::select_imu_readings(const std::vector<imu_type>& imu_data, time_point time_begin,
                                                          time_point time_end) {
    std::vector<imu_type> prop_data;
    if (imu_data.size() < 2) {
        return prop_data;
    }

    for (size_t i = 0; i < imu_data.size() - 1; i++) {
        // If time_begin comes inbetween two IMUs (A and B), interpolate A forward to time_begin
        if (imu_data[i + 1].time > time_begin && imu_data[i].time < time_begin) {
            imu_type data = interpolate_imu(imu_data[i], imu_data[i + 1], time_begin);
            prop_data.push_back(data);
            continue;
        }

        // IMU is within time_begin and time_end
        if (imu_data[i].time >= time_begin && imu_data[i + 1].time <= time_end) {
            prop_data.push_back(imu_data[i]);
            continue;
        }

        // IMU is past time_end
        if (imu_data[i + 1].time > time_end) {
            imu_type data = interpolate_imu(imu_data[i], imu_data[i + 1], time_end);
            prop_data.push_back(data);
            break;
        }
    }

    // Loop through and ensure we do not have zero dt values
    // This would cause the noise covariance to be Infinity
    for (int i = 0; i < int(prop_data.size()) - 1; i++) {
        if (std::chrono::abs(prop_data[i + 1].time - prop_data[i].time) < std::chrono::nanoseconds{1}) {
            prop_data.erase(prop_data.begin() + i);
            i--; // i can be negative, so use type int
        }
    }

    return prop_data;
}

// For when an integration time ever falls inbetween two imu measurements (modeled after OpenVINS)
imu_type rk4_integrator::interpolate_imu(const imu_type& imu_1, const imu_type& imu_2, time_point timestamp) {
    double lambda = duration_to_double(timestamp - imu_1.time) / duration_to_double(imu_2.time - imu_1.time);
    return imu_type{timestamp, (1 - lambda) * imu_1.linear_a + lambda * imu_2.linear_a,
                    (1 - lambda) * imu_1.angular_v + lambda * imu_2.angular_v};
}

PLUGIN_MAIN(rk4_integrator)
