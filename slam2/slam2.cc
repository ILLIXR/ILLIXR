#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <signal.h>
#include "common/common.hh"

using namespace ILLIXR;

class slam2 : public abstract_slam {
public:
	slam2() {
		_m_current_pose.data[0] = 0;
		_m_current_pose.data[1] = 0;
		_m_current_pose.data[2] = 0;
		_m_thread = std::thread{&slam2::main_loop, this};
	}
	void main_loop() {
		while (!_m_terminate.load()) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(200ms);
			_m_current_pose.data[0] -= 1;
		}
	}
private:
	pose _m_current_pose;
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};

public:
	/* compatibility interface */
	virtual void feed_cam_frame_nonbl(cam_frame&) override { }
	virtual void feed_accel_nonbl(accel&) override { }
	virtual ~slam2() override {
		_m_terminate.store(true);
		_m_thread.join();
	}
	virtual pose& produce_nonbl() override {
		return _m_current_pose;
	}
};

ILLIXR_make_dynamic_factory(abstract_slam, slam2)
