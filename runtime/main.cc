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

	void main_loop() {
		_m_slam->feed_cam_frame_nonbl(_m_cam->produce_blocking());
		_m_slam->feed_accel_nonbl(_m_imu->produce_nonbl());
	}
	const pose& produce() {
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
	dynamic_lib slam2_lib = dynamic_lib::create(std::string{argv[4]});

	/* I enter a lexical scope after creating the dynamic_libs,
	   because I need to be sure that they are destroyed AFTER the
	   objects which come from them. */
	{
		/* dynamiclaly load the .so-lib provider and call its factory. I
		   use pointers for the polymorphism. */
		std::shared_ptr<abstract_slam> slam = create_from_dynamic_factory(abstract_slam, slam_lib);
		std::shared_ptr<abstract_cam> cam = create_from_dynamic_factory(abstract_cam, cam_lib);
		std::shared_ptr<abstract_imu> imu = create_from_dynamic_factory(abstract_imu, imu_lib);

		auto slow_pose = std::make_unique<slow_pose_producer>(slam, cam, imu);

		std::async(std::launch::async, [&](){
			std::default_random_engine generator;
			std::uniform_int_distribution<int> distribution{200, 600};

			std::cout << "Model an XR app by calling for a pose sporadically."
					  << std::endl;

			for (int i = 0; i < 4; ++i) {
				int delay = distribution(generator);
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				const pose& cur_pose = slow_pose->produce();
				std::cout << "pose = "
						  << cur_pose.data[0] << ", "
						  << cur_pose.data[1] << ", "
						  << cur_pose.data[2] << std::endl;
			}

			std::cout << "Hot swap slam1 for slam2 (should see negative drift now)."
					  << std::endl;

			std::shared_ptr<abstract_slam> slam2 = create_from_dynamic_factory(abstract_slam, slam2_lib);
			// auto new_slow_pose = std::make_unique<slow_pose_producer>(slam2, cam, imu);
			// slow_pose.swap(new_slow_pose);
			slow_pose.reset(new slow_pose_producer{slam2, cam, imu});

			for (int i = 0; i < 4; ++i) {
				int delay = distribution(generator);
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
				const pose& cur_pose = slow_pose->produce();
				std::cout << "pose = "
						  << cur_pose.data[0] << ", "
						  << cur_pose.data[1] << ", "
						  << cur_pose.data[2] << std::endl;
			}
		});
	}

	return 0;
}
