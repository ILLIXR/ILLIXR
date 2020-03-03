#include <chrono>
#include <thread>
#include "common/component.hh"
#include "common/switchboard.hh"
#include "common/data_format.hh"

using namespace ILLIXR;

class slam1 : public component {
public:
	/* Provide handles to slam1 */
	slam1(std::unique_ptr<reader_latest<camera_frame>>&& camera,
		  std::unique_ptr<writer<pose>>&& pose)
		: _m_camera{std::move(camera)}
		, _m_pose{std::move(pose)}
		, state{0}
	{ }

	virtual void _p_compute_one_iteration() override {
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(100ms);

		/* The component can read the latest value from its
		   subscription. */
		auto frame = _m_camera->get_latest_ro();
		if (frame) {
			state += frame->pixel[0];
		}

		/* Instead of allocating a new buffer with malloc/new, the
		   topic can optionally recycle old buffers (completing the
		   swap-chain). Unfortunately, this doesn't work yet.
		pose* buf = std::any_cast<pose*>(_m_pose->allocate());
		*/
		
		// RT will delete this memory when it gets replaced with a newer value.
		pose* buf = new pose;
		buf->data[0] = state;
		buf->data[1] = 0;
		buf->data[2] = 0;

		/* Publish this buffer to the topic. */
		_m_pose->put(buf);
	}

	virtual ~slam1() override {
		/*
		  This developer is responsible for killing their processes
		  and deallocating their resources here.
		*/
	}

private:
	std::unique_ptr<reader_latest<camera_frame>> _m_camera;
	std::unique_ptr<writer<pose>> _m_pose;
	int state;
};

extern "C" component* create_component(switchboard* sb) {
	/* First, we declare intent to read/write topics. Switchboard
	   returns handles to those topics. */
	auto camera_ev = sb->subscribe_latest<camera_frame>("camera");
	auto pose_ev = sb->publish<pose>("pose");

	return new slam1 {std::move(camera_ev), std::move(pose_ev)};
}
