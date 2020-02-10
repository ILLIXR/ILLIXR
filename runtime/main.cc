#include <memory>
#include <random>
#include <chrono>
#include <future>
#include <thread>
#include "common/common.hh"
#include "concurrent_utils.hh"
#include "dynamic_lib.hh"

using namespace ILLIXR;

class slow_pose_producer {
public:
	slow_pose_producer(
		std::shared_ptr<abstract_slam> slam,
		std::shared_ptr<abstract_cam> cam,
		std::shared_ptr<abstract_imu> imu)
		: _m_slam{slam}
		, _m_cam{cam}
		, _m_imu{imu}
	{ }

	virtual void main_loop() {
		_m_slam->feed_cam_frame_nonbl(_m_cam->produce_blocking());
		_m_slam->feed_accel_nonbl(_m_imu->produce_nonbl());
	}
	virtual const pose& produce() {
		return _m_slam->produce_nonbl();
	}
private:
	std::shared_ptr<abstract_slam> _m_slam;
	std::shared_ptr<abstract_cam> _m_cam;
	std::shared_ptr<abstract_imu> _m_imu;
};

#define create_from_dynamic_factory(abstract_type, lib) \
	std::shared_ptr<abstract_type>{ \
		lib.get<abstract_type*(*)()>("make_" #abstract_type)() \
	};

int main(int argc, char** argv) {
	// this would be set by a config file
	dynamic_lib slam_lib = dynamic_lib::create(std::string{argv[1]});
	dynamic_lib cam_lib = dynamic_lib::create(std::string{argv[2]});
	dynamic_lib imu_lib = dynamic_lib::create(std::string{argv[3]});

	/* dynamiclaly load the .so-lib provider and call its factory. I
	   use pointers for the polymorphism. */
	std::shared_ptr<abstract_slam> slam = create_from_dynamic_factory(abstract_slam, slam_lib);
	std::shared_ptr<abstract_cam> cam = create_from_dynamic_factory(abstract_cam, cam_lib);
	std::shared_ptr<abstract_imu> imu = create_from_dynamic_factory(abstract_imu, imu_lib);

	slow_pose_producer slow_pose {slam, cam, imu};

	/* This part models an app that calls for a slow_pose
	   now-and-then. */
	std::async(std::launch::async, [&](){
		std::default_random_engine generator;
		std::uniform_int_distribution<int> distribution{500, 2000};
		while (true) {
			int delay = distribution(generator);
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			const pose& cur_pose = slow_pose.produce();
			std::cout << "pose = "
					  << cur_pose.data[0] << ", "
					  << cur_pose.data[1] << ", "
					  << cur_pose.data[2] << std::endl;
		}
	});

	return 0;
}
