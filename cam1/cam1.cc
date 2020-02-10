#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include "common/common.hh"

using namespace ILLIXR;

class cam1 : public abstract_cam {
private:
	cam_frame current_frame;
public:
	virtual cam_frame& produce_blocking() override {
		return current_frame;
	}
};

ILLIXR_make_dynamic_factory(abstract_cam, cam1)
