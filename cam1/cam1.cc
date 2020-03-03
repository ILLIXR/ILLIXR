#include <chrono>
#include <thread>
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

using namespace ILLIXR;

class cam1 : public component {
public:
	cam1(std::unique_ptr<writer<camera_frame>>&& output)
		: _m_output{std::move(output)}
	{
		_p_compute_one_iteration();
	}

	virtual void _p_compute_one_iteration() override {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(50ms);
		auto buf = new camera_frame;
		buf->pixel[0] = 1;
		_m_output->put(buf);
	}

	virtual ~cam1() override { }

private:
	std::unique_ptr<writer<camera_frame>> _m_output;
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto camera_ev = sb->publish<camera_frame>("camera");

	return new cam1 {std::move(camera_ev)};
}
