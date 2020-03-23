#include <chrono>
#include <thread>
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

using namespace ILLIXR;

class imu1 : public component {
public:
	imu1(std::unique_ptr<writer<imu_type>>&& output)
		: _m_output{std::move(output)}
	{
		_p_compute_one_iteration();
	}

	virtual void _p_compute_one_iteration() override {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(5ms);
		auto buf = new imu_type {
			std::chrono::system_clock::now(),
			{0, 0, 0},
			{0, 0, 1},
		};
		_m_output->put(buf);
		std::cout << "IMU" << std::endl;
	}

	virtual ~imu1() override { }

private:
	std::unique_ptr<writer<imu_type>> _m_output;
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto imu_ev = sb->publish<imu_type>("imu");

	return new imu1 {std::move(imu_ev)};
}
