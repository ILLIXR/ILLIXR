#include <chrono>
#include <thread>
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

using namespace ILLIXR;

class slam1 : public component {
public:
	/* Provide handles to slam1 */
	slam1(std::unique_ptr<writer<pose>>&& pose)
		: _m_pose{std::move(pose)}
		, state{0}
	{ }

	void compute(const camera_frame* frame) {
		if (frame) {
			state += frame->pixel[0];
		}

		/* Instead of allocating a new buffer with malloc/new, the
		   topic can optionally recycle old buffers (completing the
		   swap-chain). Unfortunately, this doesn't work yet.
		pose* buf = std::any_cast<pose*>(_m_pose->allocate());
		*/

		std::cout << "Slam" << std::endl;

		// RT will delete this memory when it gets replaced with a newer value.
		pose* buf = new pose;
		buf->data[0] = state;
		buf->data[1] = 0;
		buf->data[2] = 0;

		/* Publish this buffer to the topic. */
		_m_pose->put(buf);
	}

	virtual void _p_start() override {
		/* All of my work is already scheduled synchronously. Nohting to do here. */
	}

	virtual void _p_stop() override { }

	virtual ~slam1() override {
		/*
		  This developer is responsible for killing their processes
		  and deallocating their resources here.
		*/
	}

private:
	std::unique_ptr<writer<pose>> _m_pose;
	int state;
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto pose_ev = sb->publish<pose>("pose");
	auto this_slam1 = new slam1{std::move(pose_ev)};
	sb->schedule<camera_frame>("camera", [this_slam1](const void* frame_untyped) {
		this_slam1->compute(reinterpret_cast<const camera_frame*>(frame_untyped));
	});

	return this_slam1;
}
