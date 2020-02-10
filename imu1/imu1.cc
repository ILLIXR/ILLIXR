#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include "common/common.hh"

using namespace ILLIXR;

class imu1 : public abstract_imu {
private:
	accel current_frame;
public:
	virtual accel& produce_nonbl() override {
		return current_frame;
	}
};

ILLIXR_make_dynamic_factory(abstract_imu, imu1)
