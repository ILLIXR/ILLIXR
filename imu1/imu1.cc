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
		start_time = std::chrono::system_clock::now();
		_p_compute_one_iteration();
	}

	virtual void _p_compute_one_iteration() override {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(5ms);
		std::chrono::duration<float> this_time = std::chrono::system_clock::now() - start_time;
		auto buf = new imu_type {
			std::chrono::system_clock::now(),
			{0.5, 0.5, 0.0},
			{0.0, 0.0, 0},
		};
		_m_output->put(buf);
	}

	virtual ~imu1() override { }

private:
	std::unique_ptr<writer<imu_type>> _m_output;
	std::chrono::time_point<std::chrono::system_clock> start_time;
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto imu_ev = sb->publish<imu_type>("imu");

	return new imu1 {std::move(imu_ev)};
}
