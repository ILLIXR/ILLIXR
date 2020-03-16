#include <vector>
#include <chrono>
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
    pose_prediction(std::unique_ptr<reader_latest<pose_sample>>&& pose_plug,
	  std::unique_ptr<reader_latest<imu_sample>>&& imu_plug,
      std::unique_ptr<writer<pose_sample>>&& predicted_pose_plug)
	: _m_slow_pose{std::move(pose_plug)}
	, _m_imu{std::move(imu_plug)}
    , _m_fast_pose{std::move(predicted_pose_plug)} {

        std::chrono::time_point<std::chrono::system_clock> current_time = std::chrono::system_clock::now();
        pose_sample _latest_pose = {pose_t{quaternion_t{0, 0, 0, 1}, vector3_t{0, 0, 0}}, current_time};
        _last_slow_pose_update_time = current_time;

        _filter = new kalman_filter{current_time};
        _latest_vel = std::vector<float>{0, 0, 0};
        _latest_acc = std::vector<float>{0, 0, 0};
        _latest_gyro = std::vector<float>{0, 0, 0};
    }

    // Overridden method from the component interface. This specifies one interation of the main loop 
    virtual void _p_compute_one_iteration() override { 
		auto pose_sample_nullable = _m_slow_pose->get_latest_ro();
		std::cout << "_p_compute_one_iteration" << std::endl;
		assert(!pose_sample_nullable);
	    const pose_sample fresh_pose = *pose_sample_nullable;

        // If the SB has a new slow pose value from SLAM
        if (fresh_pose.sample_time > _last_slow_pose_update_time) {
            _update_slow_pose(fresh_pose);
        }

        const imu_sample fresh_imu_measurement = *(_m_imu->get_latest_ro());

        // If queried IMU is fresher than the pose mesaurement and the local IMU copy we have, update it and the pose
        if (fresh_imu_measurement.sample_time > _latest_pose.sample_time) {
            _update_fast_pose(fresh_imu_measurement);
        }
    }

    /*
    This developer is responsible for killing their processes
    and deallocating their resources here.
    */
    virtual ~pose_prediction() override {}

private:
    // Switchboard plugs for writing / reading data.
    std::unique_ptr<reader_latest<pose_sample>> _m_slow_pose;
    std::unique_ptr<reader_latest<imu_sample>> _m_imu;
    std::unique_ptr<writer<pose_sample>> _m_fast_pose;

    pose_sample _latest_pose;
    std::chrono::time_point<std::chrono::system_clock> _last_slow_pose_update_time;

    kalman_filter* _filter;
    std::vector<float> _latest_vel;
    std::vector<float> _latest_acc;
    std::vector<float> _latest_gyro;

    // Helper that will push to the SB if a newer pose is pulled from the SB/SLAM
    void _update_slow_pose(const pose_sample fresh_pose) {
        float time_difference = std::chrono::duration_cast<std::chrono::milliseconds>
                (fresh_pose.sample_time - _latest_pose.sample_time).count();

        // Update the velocity with the pose position difference over time. This is done because
        // Accelerometer readings have lots of noise and will lead to bad dead reckoning
        _latest_vel[0] = (fresh_pose.pose.position.x - _latest_pose.pose.position.x) / time_difference;
        _latest_vel[1] = (fresh_pose.pose.position.y - _latest_pose.pose.position.y) / time_difference;
        _latest_vel[2] = (fresh_pose.pose.position.z - _latest_pose.pose.position.z) / time_difference;

        _latest_pose = fresh_pose;
        _last_slow_pose_update_time = fresh_pose.sample_time;
        _m_fast_pose->put(&_latest_pose);
    }

    // Helper that updates the pose with new IMU readings and pushed to the SB
    void _update_fast_pose(const imu_sample fresh_imu_measurement) {
        _latest_acc = std::vector<float>{
            fresh_imu_measurement.measurement.ax, 
            fresh_imu_measurement.measurement.ay, 
            fresh_imu_measurement.measurement.az
        };
        _latest_gyro = _filter->predict_values(fresh_imu_measurement);

        float time_difference = std::chrono::duration_cast<std::chrono::milliseconds>
        (fresh_imu_measurement.sample_time - _latest_pose.sample_time).count();

        _latest_pose = {_calculate_pose(time_difference), fresh_imu_measurement.sample_time};
        _m_fast_pose->put(&_latest_pose);
    }

    // Helper that does all the math to calculate the poses
    pose_t _calculate_pose(float time_delta) {
        Eigen::Quaternion orientation_quaternion = Eigen::Quaternion(
                _latest_pose.pose.orientation.w,
                _latest_pose.pose.orientation.x,
                _latest_pose.pose.orientation.y,
                _latest_pose.pose.orientation.z); 

        // Convert the quaternion to euler angles and calculate the new rotation
        Eigen::Vector3f orientation_euler = orientation_quaternion.toRotationMatrix().eulerAngles(0, 1, 2);
        orientation_euler(0) += _latest_gyro[0] * time_delta;
        orientation_euler(1) += _latest_gyro[1] * time_delta;
        orientation_euler(2) += _latest_gyro[2] * time_delta;

        // Convert euler angles back into a quaternion
        orientation_quaternion = Eigen::AngleAxisf(orientation_euler(0), Eigen::Vector3f::UnitX())
                * Eigen::AngleAxisf(orientation_euler(1), Eigen::Vector3f::UnitY())
                * Eigen::AngleAxisf(orientation_euler(2), Eigen::Vector3f::UnitZ());

        Eigen::Vector4f coeffs = orientation_quaternion.coeffs();                
        quaternion_t predicted_orientation = {
            coeffs(1),
            coeffs(2),
            coeffs(3),
            coeffs(0),
        };

        // Calculate the new pose transform by .5*a*t^2 + v*t + d
        vector3_t predicted_position = {
            static_cast<float>(0.5 * _latest_acc[0] * pow(time_delta, 2) + _latest_vel[0] * time_delta + _latest_pose.pose.position.x),
            static_cast<float>(0.5 * _latest_acc[1] * pow(time_delta, 2) + _latest_vel[1] * time_delta + _latest_pose.pose.position.y),
            static_cast<float>(0.5 * _latest_acc[2] * pow(time_delta, 2) + _latest_vel[2] * time_delta + _latest_pose.pose.position.z),
        };

        return pose_t{predicted_orientation, predicted_position};
    }
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
    auto slow_pose_plug = sb->subscribe_latest<pose_sample>("slow_pose");
	auto imu_plug = sb->subscribe_latest<imu_sample>("imu");
	auto fast_pose_plug = sb->publish<pose_sample>("fast_pose");

	/* This ensures pose is available at startup. */
	auto nullable_pose = slow_pose_plug->get_latest_ro();
	assert(nullable_pose);
	std::cout << "create_component" << std::endl;
	fast_pose_plug->put(nullable_pose);

	return new pose_prediction {std::move(slow_pose_plug), std::move(imu_plug), std::move(fast_pose_plug)};
}
