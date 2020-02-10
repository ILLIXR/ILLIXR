#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include "common/common.hh"

using namespace ILLIXR;

class slam1 : public abstract_slam {
public:
	slam1() {
		/* You can do anything (processes, threads, network). Most
		   SLAMs already manage their own mainloop. That would go
		   here. */
		current_pose.data[0] = 0;
		current_pose.data[1] = 0;
		current_pose.data[2] = 0;
		thread = std::thread{&slam1::main_loop, this};
	}
	void main_loop() {
		while (true) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(200ms);
			current_pose.data[0] += 1;

			/*
			  1. look at cam_frame (already fed in)
			  2. look at accel (already fed in)
			  3. do computation (could go to network here)
			  4. write to current_pose
			*/
		}
	}
private:
	pose current_pose;
	std::thread thread;

public:
	/* compatibility interface */
	virtual void feed_cam_frame_nonbl(cam_frame&) override { }
	virtual void feed_accel_nonbl(accel&) override { }
	virtual ~slam1() override {
		/*
		  This developer is responsible for killing their processes
		  and deallocating their resources here.
		*/
	}
	virtual pose& produce_nonbl() override {
		return current_pose;
	}
};

ILLIXR_make_dynamic_factory(abstract_slam, slam1)
