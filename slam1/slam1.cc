#include <chrono>
#include <thread>
#include <cmath>
#include <ctime>
#include "common/plugin.hpp"
#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include "common/threadloop.hpp"

using namespace ILLIXR;

class slam1 : public threadloop {
public:
	/* Provide handles to slam1 */
	slam1(phonebook* pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_camera{sb->subscribe_latest<camera_frame>("camera")}
		, _m_pose{sb->publish<pose_type>("fast_pose")}
		, state{0}
	{
		start_time = std::chrono::system_clock::now();
		pose_type* new_pose = new pose_type;
		_m_pose->put(new_pose);
	}

	virtual void _p_one_iteration() override {
		using namespace std::chrono_literals;

		// Until we have pose prediction, we'll run our magical
		// make-believe SLAM system at an unreasonably high
		// frequency, like 120hz!
		std::this_thread::sleep_for(8.3ms);

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
		pose_type* new_pose = new pose_type;
		std::chrono::duration<float> this_time = std::chrono::system_clock::now() - start_time;
		new_pose->orientation = generateDummyOrientation(this_time.count());
		new_pose->position.x()= cosf(this_time.count()) * 0.2f;
		new_pose->position.y() = cosf(this_time.count() + 0.2f) * 0.2f + 0.3f;
		new_pose->position.z() = sinf(this_time.count()) * 0.2f;
		new_pose->time = std::chrono::system_clock::now();

		/* Publish this buffer to the topic. */
		_m_pose->put(new_pose);
	}

	virtual ~slam1() override {
		/*
		  This developer is responsible for killing their processes
		  and deallocating their resources here.
		*/
	}

private:
	switchboard* const sb;
	std::unique_ptr<reader_latest<camera_frame>> _m_camera;
	std::unique_ptr<writer<pose_type>> _m_pose;

	std::chrono::time_point<std::chrono::system_clock> start_time;
	
	int state;

	Eigen::Quaternionf generateDummyOrientation(float time){
		float rollIsPitch = 0.3f * sinf( time * 2.5f ) * 0.0f;
		float yawIsRoll = 0;
		float pitchIsYaw = 0.3f * cosf( time * 2.5f ) * 0.5f - 0.5f;

		//https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles#Euler_Angles_to_Quaternion_Conversion


		double cy = cos(yawIsRoll * 0.5);
		double sy = sin(yawIsRoll * 0.5);
		double cp = cos(pitchIsYaw * 0.5);
		double sp = sin(pitchIsYaw * 0.5);
		double cr = cos(rollIsPitch * 0.5);
		double sr = sin(rollIsPitch * 0.5);

		Eigen::Quaternionf q;
		q.w() = cy * cp * cr + sy * sp * sr;
		q.x() = cy * cp * sr - sy * sp * cr;
		q.y() = sy * cp * sr + cy * sp * cr;
		q.z() = sy * cp * cr - cy * sp * sr;
		
		return q;

	}
};

PLUGIN_MAIN(slam1);
