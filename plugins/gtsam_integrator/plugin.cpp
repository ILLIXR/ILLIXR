#include "plugin.hpp"

#include <gtsam/base/Vector.h>
#include <gtsam/navigation/AHRSFactor.h>
#include <gtsam/navigation/ImuBias.h>
#include <iomanip>
#include <memory>
#include <thread>
#include <utility>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// IMU sample time to live in seconds
constexpr duration IMU_TTL{std::chrono::seconds{5}};

using ImuBias = gtsam::imuBias::ConstantBias;

[[maybe_unused]] gtsam_integrator::gtsam_integrator(const std::string& name, phonebook* pb)
    : plugin{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , imu_integrator_input_{switchboard_->get_reader<imu_integrator_input>("imu_integrator_input")}
    , imu_raw_{switchboard_->get_writer<imu_raw_type>("imu_raw")} {
    spdlogger(switchboard_->get_env_char("GTSAM_INTEGRATOR_LOG_LEVEL"));
    switchboard_->schedule<imu_type>(id_, "imu", [&](const switchboard::ptr<const imu_type>& datum, size_t) {
        callback(datum);
    });
    const double frequency  = 200.;
    const double min_cutoff = 10.;
    const double beta       = 1.;
    const double d_cutoff   = 10.;

    for (int i = 0; i < 8; ++i) {
        filters_.emplace_back(frequency, Eigen::Array<double, 3, 1>{min_cutoff, min_cutoff, min_cutoff},
                              Eigen::Array<double, 3, 1>{beta, beta, beta},
                              Eigen::Array<double, 3, 1>{d_cutoff, d_cutoff, d_cutoff}, Eigen::Array<double, 3, 1>::Zero(),
                              Eigen::Array<double, 3, 1>::Ones(), [](auto& in) {
                                  return in.abs();
                              });
    }
}

void gtsam_integrator::callback(const switchboard::ptr<const imu_type>& datum) {
    imu_vector_.emplace_back(datum->time, datum->angular_v.cast<double>(), datum->linear_a.cast<double>());

    clean_imu_vec(datum->time);
    propagate_imu_values(datum->time);

    RAC_ERRNO_MSG("gtsam_integrator");
}

gtsam_integrator::pim_object::pim_object(const imu_int_t& imu_int_input)
    : imu_bias_{imu_int_input.bias_acc, imu_int_input.bias_gyro}
    , pim_{nullptr} {
    pim_t::Params params{imu_int_input.params.n_gravity};
    params.setGyroscopeCovariance(std::pow(imu_int_input.params.gyro_noise, 2.0) * Eigen::Matrix3d::Identity());
    params.setAccelerometerCovariance(std::pow(imu_int_input.params.acc_noise, 2.0) * Eigen::Matrix3d::Identity());
    params.setIntegrationCovariance(std::pow(imu_int_input.params.imu_integration_sigma, 2.0) * Eigen::Matrix3d::Identity());
    params.setBiasAccCovariance(std::pow(imu_int_input.params.acc_walk, 2.0) * Eigen::Matrix3d::Identity());
    params.setBiasOmegaCovariance(std::pow(imu_int_input.params.gyro_walk, 2.0) * Eigen::Matrix3d::Identity());

    pim_ = new pim_t{boost::make_shared<pim_t::Params>(std::move(params)), imu_bias_};
    reset_integration_and_set_bias(imu_int_input);
}

gtsam_integrator::pim_object::~pim_object() {
    assert(pim_ != nullptr && "pim_ should not be null");
    /// Note: Deliberately leak pim_ => Removes SEGV read during destruction
    /// delete pim_;
}

void gtsam_integrator::pim_object::reset_integration_and_set_bias(const imu_int_t& imu_int_input) noexcept {
    assert(pim_ != nullptr && "pim_ should not be null");

    imu_bias_ = bias_t{imu_int_input.bias_acc, imu_int_input.bias_gyro};
    pim_->resetIntegrationAndSetBias(imu_bias_);

    nav_state_lkf_ = nav_t{gtsam::Pose3{gtsam::Rot3{imu_int_input.quat}, imu_int_input.position}, imu_int_input.velocity};
}

void gtsam_integrator::pim_object::integrate_measurement(const imu_t& imu_input, const imu_t& imu_input_next) noexcept {
    assert(pim_ != nullptr && "pim_ shuold not be null");

    const gtsam::Vector3 measured_acc{imu_input.linear_a};
    const gtsam::Vector3 measured_omega{imu_input.angular_v};

    duration delta_t = imu_input_next.time - imu_input.time;

    pim_->integrateMeasurement(measured_acc, measured_omega, duration_to_double(delta_t));
}

[[nodiscard]] bias_t gtsam_integrator::pim_object::bias_hat() const noexcept {
    assert(pim_ != nullptr && "pim_ shuold not be null");
    return pim_->biasHat();
}

[[nodiscard]] nav_t gtsam_integrator::pim_object::predict() const noexcept {
    assert(pim_ != nullptr && "pim_ should not be null");
    return pim_->predict(nav_state_lkf_, imu_bias_);
}

// Remove IMU values older than 'IMU_TTL' from the imu buffer
void gtsam_integrator::clean_imu_vec(time_point timestamp) {
    auto imu_iterator = imu_vector_.begin();

    // Since the vector is ordered oldest to latest, keep deleting until you
    // hit a value less than 'IMU_TTL' seconds old
    while (imu_iterator != imu_vector_.end()) {
        if (timestamp - imu_iterator->time < IMU_TTL) {
            break;
        }

        imu_iterator = imu_vector_.erase(imu_iterator);
    }
}

// Timestamp we are propagating the biases to (new IMU reading time)
void gtsam_integrator::propagate_imu_values(time_point real_time) {
    auto input_values = imu_integrator_input_.get_ro_nullable();
    if (input_values == nullptr) {
        return;
    }

#ifndef NDEBUG
    if (input_values->last_cam_integration_time > last_cam_time_) {
        spdlog::get(name_)->debug("New slow pose has arrived!");
        last_cam_time_ = input_values->last_cam_integration_time;
    }
#endif

    if (pim_obj_ == nullptr) {
        /// We don't have a pim_object -> make and set given the current input
        pim_obj_ = std::make_unique<pim_object>(*input_values);

        /// Setting 'last_imu_offset_' here to stay consistent with previous integrator version.
        /// TODO: Should be set and tested at the end of this function to avoid staleness from VIO.
        last_imu_offset_ = input_values->t_offset;
    } else {
        /// We already have a pim_object -> set the values given the current input
        pim_obj_->reset_integration_and_set_bias(*input_values);
    }

    assert(pim_obj_ != nullptr && "pim_obj_ should not be null");

    // TODO last_imu_offset_ is 0, t_offset only take effects when it's negative.
    // However, why would we want to integrate to a past time point rather than the current time point?
    time_point time_begin = input_values->last_cam_integration_time + last_imu_offset_;
    time_point time_end   = real_time;

    const std::vector<imu_type> prop_data = select_imu_readings(imu_vector_, time_begin, time_end);

    /// Need to integrate over a sliding window of 2 imu_type values.
    /// If the container of data is smaller than 2 elements, return early.
    if (prop_data.size() < 2) {
        return;
    }

    ImuBias prev_bias = pim_obj_->bias_hat();
    ImuBias bias      = pim_obj_->bias_hat();

    spdlog::get(name_)->debug("Integrating over {} IMU samples", prop_data.size());

    for (std::size_t i = 0; i < prop_data.size() - 1; i++) {
        pim_obj_->integrate_measurement(prop_data[i], prop_data[i + 1]);

        prev_bias = bias;
        bias      = pim_obj_->bias_hat();
    }

    gtsam::NavState navstate_k = pim_obj_->predict();
    gtsam::Pose3    out_pose   = navstate_k.pose();

    spdlog::get(name_)->debug("Base Position (x, y, z) = {}, {}, {}", input_values->position(0), input_values->position(1),
                              input_values->position(2));
    spdlog::get(name_)->debug("New Position (x, y, z) = {}, {}, {}", out_pose.x(), out_pose.y(), out_pose.z());

    auto                        seconds_since_epoch = std::chrono::duration<double>(real_time.time_since_epoch()).count();
    auto                        original_quaternion = out_pose.rotation().toQuaternion();
    Eigen::Matrix<double, 3, 1> rotation_angles  = original_quaternion.toRotationMatrix().eulerAngles(0, 1, 2).cast<double>();
    Eigen::Matrix<double, 3, 1> filtered_sins    = filters_[6](rotation_angles.array().sin(), seconds_since_epoch);
    Eigen::Matrix<double, 3, 1> filtered_cosines = filters_[7](rotation_angles.array().cos(), seconds_since_epoch);
    Eigen::Matrix<double, 3, 1> filtered_angles{atan2(filtered_sins[0], filtered_cosines[0]),
                                                atan2(filtered_sins[1], filtered_cosines[1]),
                                                atan2(filtered_sins[2], filtered_cosines[2])};

    if (has_prev_ &&
        (abs(rotation_angles[0] - prev_euler_angles_[0]) > M_PI / 2 ||
         abs(rotation_angles[1] - prev_euler_angles_[1]) > M_PI / 2 ||
         abs(rotation_angles[2] - prev_euler_angles_[2]) > M_PI / 2)) {
        filters_[6].clear();
        filters_[7].clear();
        filtered_sins    = filters_[6](rotation_angles.array().sin(), seconds_since_epoch);
        filtered_cosines = filters_[7](rotation_angles.array().cos(), seconds_since_epoch);
        filtered_angles  = {atan2(filtered_sins[0], filtered_cosines[0]), atan2(filtered_sins[1], filtered_cosines[1]),
                            atan2(filtered_sins[2], filtered_cosines[2])};
    } else {
        has_prev_ = true;
    }

    prev_euler_angles_ = std::move(rotation_angles);

    [[maybe_unused]] auto new_quaternion = Eigen::AngleAxisd(filtered_angles(0, 0), Eigen::Vector3d::UnitX()) *
        Eigen::AngleAxisd(filtered_angles(1, 0), Eigen::Vector3d::UnitY()) *
        Eigen::AngleAxisd(filtered_angles(2, 0), Eigen::Vector3d::UnitZ());

    Eigen::MatrixWrapper<Eigen::Array<double, 3, 1, 0, 3, 1>> filtered_pos{
        filters_[4](out_pose.translation().array(), seconds_since_epoch).matrix()};

    imu_raw_.put(imu_raw_.allocate<imu_raw_type>(
        imu_raw_type{prev_bias.gyroscope(), prev_bias.accelerometer(), bias.gyroscope(), bias.accelerometer(),
                     filtered_pos,                                                    /// Position
                     filters_[5](navstate_k.velocity().array(), seconds_since_epoch), /// Velocity
                     new_quaternion,                                                  /// Eigen Quat
                     real_time}));
}

// Select IMU readings based on timestamp similar to how OpenVINS selects IMU values to propagate
std::vector<imu_type> gtsam_integrator::select_imu_readings(const std::vector<imu_type>& imu_data, const time_point time_begin,
                                                            const time_point time_end) {
    std::vector<imu_type> prop_data;
    if (imu_data.size() < 2) {
        return prop_data;
    }

    for (std::size_t i = 0; i < imu_data.size() - 1; i++) {
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

    // Loop through and ensure we do not have a zero dt values
    // This would cause the noise covariance to be Infinity
    for (int i = 0; i < int(prop_data.size()) - 1; i++) {
        // I need prop_data.size() - 1 to be signed, because it might equal -1.
        if (std::chrono::abs(prop_data[i + 1].time - prop_data[i].time) < std::chrono::nanoseconds{1}) {
            prop_data.erase(prop_data.begin() + i);
            i--;
        }
    }

    return prop_data;
}

// For when an integration time ever falls inbetween two imu measurements (modeled after OpenVINS)
imu_type gtsam_integrator::interpolate_imu(const imu_type& imu_1, const imu_type& imu_2, time_point timestamp) {
    double lambda = duration_to_double(timestamp - imu_1.time) / duration_to_double(imu_2.time - imu_1.time);
    return imu_type{timestamp, (1 - lambda) * imu_1.linear_a + lambda * imu_2.linear_a,
                    (1 - lambda) * imu_1.angular_v + lambda * imu_2.angular_v};
}

PLUGIN_MAIN(gtsam_integrator)
