#include <vector>
#include <chrono>
#include <math.h>
#include "kalman.hh"
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

using namespace ILLIXR;

class pose_prediction : public component {
public:
    // Public constructor, create_component passes Switchboard handles ("plugs")
    // to this constructor. In turn, the constructor fills in the private
    // references to the switchboard plugs, so the component can read the
    // data whenever it needs to.
    pose_prediction(std::unique_ptr<reader_latest<pose_type>>&& pose_plug,
	  std::unique_ptr<reader_latest<imu_type>>&& imu_plug,
      std::unique_ptr<writer<pose_type>>&& predicted_pose_plug)
	: _m_slow_pose{std::move(pose_plug)}
	, _m_imu{std::move(imu_plug)}
    , _m_fast_pose{std::move(predicted_pose_plug)} {

        std::chrono::time_point<std::chrono::system_clock> current_time = std::chrono::system_clock::now();
        pose_type init_pose = pose_type{
            current_time, 
            Eigen::Vector3f{0.878612, 2.14247, 0.947262}, 
            Eigen::Quaternionf{0.060514, -0.828459, -0.058956, -0.553641}
        };

        _filter = new kalman_filter();
        _previous_slow_pose = init_pose;
        _current_fast_pose = init_pose;
        _fast_linear_vel = Eigen::Vector3f(0, 0, 0);
        _slam_received = false;
    }

    // Overridden method from the component interface. This specifies one interation of the main loop 
    virtual void _p_compute_one_iteration() override {

        // If the SB has a new slow pose value from SLAM
        auto pose_sample = _m_slow_pose->get_latest_ro();
        if (pose_sample != NULL && pose_sample->time > _previous_slow_pose.time) {
            _update_slow_pose(*pose_sample);

            // If this is the first time receiving a slow pose, init the filter biases and initial state.
            if (!_slam_received) {
                _slam_received = true;
                _filter->init_prediction(*pose_sample);
            }
        }

        const imu_type* fresh_imu_measurement = _m_imu->get_latest_ro();
		assert(fresh_imu_measurement != NULL);

        // If queried IMU is fresher than the pose mesaurement and the local IMU copy we have, update it and the pose
        if (fresh_imu_measurement->time > _current_fast_pose.time) {
            if (_slam_received) {
                _update_fast_pose(*fresh_imu_measurement);
            } else {
                _filter->add_bias(*fresh_imu_measurement);
            }
        }
    }

    /*
    This developer is responsible for killing their processes
    and deallocating their resources here.
    */
    virtual ~pose_prediction() override {}

private:
    // Switchboard plugs for writing / reading data.
    std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
    std::unique_ptr<reader_latest<imu_type>> _m_imu;
    std::unique_ptr<writer<pose_type>> _m_fast_pose;

    kalman_filter* _filter;
    pose_type _previous_slow_pose;
    pose_type _current_fast_pose;
    Eigen::Vector3f _fast_linear_vel;
    bool _slam_received;

    // Helper that will push to the SB if a newer pose is pulled from the SB/SLAM
    void _update_slow_pose(const pose_type fresh_pose) {
        float time_difference = static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>
												   (fresh_pose.time - _previous_slow_pose.time).count()) / 1000000000.0f;

        // Ground truth values do not start from 0, 0, 0. They start from the offset specified by these variables
        pose_type *temp_pose = new pose_type(fresh_pose);
        temp_pose->position[0] += 0.878612;
        temp_pose->position[1] += 2.14247;
        temp_pose->position[2] += 0.947262;

        // Update the velocity with the pose position difference over time. This is done because
        // Accelerometer readings have lots of noise and will lead to bad dead reckoning
        _fast_linear_vel[0] = (temp_pose->position[0] - _previous_slow_pose.position[0]) / time_difference;
        _fast_linear_vel[1] = (temp_pose->position[1] - _previous_slow_pose.position[1]) / time_difference;
        _fast_linear_vel[2] = (temp_pose->position[2] - _previous_slow_pose.position[2]) / time_difference;

        _previous_slow_pose = *temp_pose;
        _current_fast_pose = *temp_pose;
        _filter->update_estimates(temp_pose->orientation);

        Eigen::Vector3f orientation_euler = temp_pose->orientation.toRotationMatrix().eulerAngles(0, 1, 2);
        std::cout << "New pose recieved from SLAM! " << time_difference << std::endl;
        std::cout << "New Velocity: " << _fast_linear_vel[0] << ", " << _fast_linear_vel[1] << ", " << _fast_linear_vel[2] << std::endl;
        std::cout << "New Position " << temp_pose->position[0] << ", " << temp_pose->position[1] << ", " << temp_pose->position[2] << std::endl;
        std::cout << "New Orientation Euler: " << orientation_euler[0] << ", " << orientation_euler[1] << ", " << orientation_euler[2] << std::endl;
        std::cout << "New Orientation Quat: " << temp_pose->orientation.w() << ", " << temp_pose->orientation.x() << ", " << temp_pose->orientation.y() << ", " << temp_pose->orientation.z() << std::endl << std::endl;

        _m_fast_pose->put(new pose_type(fresh_pose));
    }

    // Helper that updates the pose with new IMU readings and pushed to the SB
    void _update_fast_pose(const imu_type fresh_imu_measurement) {
        float time_difference = static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>
                                (fresh_imu_measurement.time - _current_fast_pose.time).count()) / 1000000000.0f;

        Eigen::Vector3f rotated_accel = _current_fast_pose.orientation.inverse() * fresh_imu_measurement.linear_a;
        // rotated_accel[2] -= 9.81;

        // Yes this is intentional, the IMU on the drone for this dataset is installed sideways so that X is up, Y is left, and Z is front
        _fast_linear_vel[0] += rotated_accel[0] * time_difference;
        _fast_linear_vel[1] += rotated_accel[1] * time_difference;
        _fast_linear_vel[2] += rotated_accel[2] * time_difference;

        std::cout << "IMU Values: " << time_difference << ", " << rotated_accel[0] << ", " << rotated_accel[1] << ", " << rotated_accel[2] << std::endl;
        std::cout << "IMU Velocity: " << _fast_linear_vel[0] << ", " << _fast_linear_vel[1] << ", " << _fast_linear_vel[2] << std::endl;
        std::cout << "Rotational Vel: " << fresh_imu_measurement.angular_v[0] << ", " << fresh_imu_measurement.angular_v[1] << ", " << fresh_imu_measurement.angular_v[2] << std::endl;

        // Calculate the new pose transform by .5*a*t^2 + v*t + d
        Eigen::Vector3f predicted_position = {
            static_cast<float>(0.5 * rotated_accel[0] * pow(time_difference, 2) + _fast_linear_vel[0] * time_difference + _current_fast_pose.position[0]),
            static_cast<float>(0.5 * rotated_accel[1] * pow(time_difference, 2) + _fast_linear_vel[1] * time_difference + _current_fast_pose.position[1]),
            static_cast<float>(0.5 * rotated_accel[2] * pow(time_difference, 2) + _fast_linear_vel[2] * time_difference + _current_fast_pose.position[2]),
        };

        // imu_type clean_imu_measurement = fresh_imu_measurement;
        // clean_imu_measurement.linear_a = rotated_accel;

        Eigen::Vector3f orientation_euler = _filter->predict_values(fresh_imu_measurement, rotated_accel, time_difference);
        Eigen::Quaternionf predicted_orientation = Eigen::AngleAxisf(orientation_euler(0), Eigen::Vector3f::UnitX())
                * Eigen::AngleAxisf(orientation_euler(2), Eigen::Vector3f::UnitY())
                * Eigen::AngleAxisf(orientation_euler(1), Eigen::Vector3f::UnitZ());

        _current_fast_pose = pose_type{fresh_imu_measurement.time, predicted_position, predicted_orientation};

        assert(isfinite(_current_fast_pose.orientation.w()));
        assert(isfinite(_current_fast_pose.orientation.x()));
        assert(isfinite(_current_fast_pose.orientation.y()));
        assert(isfinite(_current_fast_pose.orientation.z()));
        assert(isfinite(_current_fast_pose.position[0]));
        assert(isfinite(_current_fast_pose.position[1]));
        assert(isfinite(_current_fast_pose.position[2]));

        std::cout << "Predicted Orientation Euler: " << orientation_euler[0] << ", " << orientation_euler[1] << ", " << orientation_euler[2] << std::endl;
        std::cout << "Predicted Orientation Quat: " << predicted_orientation.w() << ", " << predicted_orientation.x() << ", " << predicted_orientation.y() << ", " << predicted_orientation.z() << std::endl;
		std::cout << "Predicted Position " << _current_fast_pose.position[0] << ", " << _current_fast_pose.position[1] << ", " << _current_fast_pose.position[2] << std::endl << std::endl;
		
		_m_fast_pose->put(new pose_type(_current_fast_pose));
    }
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
    auto slow_pose_plug = sb->subscribe_latest<pose_type>("slow_pose");
	auto imu_plug = sb->subscribe_latest<imu_type>("imu0");
	auto fast_pose_plug = sb->publish<pose_type>("fast_pose");

	/* This ensures pose is available at startup. */
	auto nullable_pose = slow_pose_plug->get_latest_ro();
	assert(nullable_pose != NULL);
	fast_pose_plug->put(nullable_pose);

	return new pose_prediction{std::move(slow_pose_plug), std::move(imu_plug), std::move(fast_pose_plug)};
}