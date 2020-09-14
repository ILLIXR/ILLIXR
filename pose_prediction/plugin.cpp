#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

using namespace ILLIXR;

class pose_prediction_impl : public pose_prediction {
public:
    pose_prediction_impl(const phonebook* const pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_slow_pose{sb->subscribe_latest<pose_type>("slow_pose")}
        , _m_true_pose{sb->subscribe_latest<pose_type>("true_pose")}
    { }

    virtual pose_type get_fast_pose() const override {
		const pose_type* pose_ptr = _m_slow_pose->get_latest_ro();

		// Make the first valid fast pose be straight ahead.
		if (first_time && pose_ptr) {
			std::lock_guard<std::recursive_mutex> lock {offset_mutex};
			// check again, now that we have mutual exclusion
			if (first_time) {
				auto pose = correct_pose(*pose_ptr);
				const_cast<pose_prediction_impl&>(*this).set_offset(pose.orientation);
				const_cast<pose_prediction_impl&>(*this).first_time = false;
			}
		}

		return correct_pose(
			pose_ptr ? *pose_ptr : pose_type{
				.time = std::chrono::system_clock::now(),
				.position = Eigen::Vector3f{0, 0, 0},
				.orientation = Eigen::Quaternionf{1, 0, 0, 0},
			}
		);
    }

    virtual pose_type get_true_pose() const override {
		const pose_type* pose_ptr = _m_true_pose->get_latest_ro();
		return correct_pose(
			pose_ptr ? *pose_ptr : pose_type{
				.time = std::chrono::system_clock::now(),
				.position = Eigen::Vector3f{0, 0, 0},
				.orientation = Eigen::Quaternionf{1, 0, 0, 0},
			}
		);
    }

	virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
		std::lock_guard<std::recursive_mutex> lock {offset_mutex};
		Eigen::Quaternionf raw_o = raw_o_times_offset * offset.inverse();
		offset = raw_o.inverse();
		/*
		  Now, `raw_o` is maps to the identity quaternion.

		  Proof:
		  apply_offset(raw_o)
		      = raw_o * offset
		      = raw_o * raw_o.inverse()
		      = Identity.
		 */
	}

	Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) const {
		std::lock_guard<std::recursive_mutex> lock {offset_mutex};
		return orientation * offset;
	}


	virtual bool fast_pose_reliable() const override {
		//return _m_slow_pose.valid();
		/*
		  SLAM takes some time to initialize, so initially fast_pose
		  is unreliable.

		  In such cases, we might return a fast_pose based only on the
		  IMU data (currently, we just return a zero-pose)., and mark
		  it as "unreliable"

		  This way, there always a pose coming out of pose_prediction,
		  representing our best guess at that time, and we indicate
		  how reliable that guess is here.

		 */
		return true;
	}

	virtual bool true_pose_reliable() const override {
		//return _m_true_pose.valid();
		/*
		  We do not have a "ground truth" available in all cases, such
		  as when reading live data.
		 */
		return true;
	}

private:
	std::atomic<bool> first_time{true};
	const std::shared_ptr<switchboard> sb;
    std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
	std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
	Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	mutable std::recursive_mutex offset_mutex;
    
    pose_type correct_pose(const pose_type pose) const {
        pose_type swapped_pose;

        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose.position.x() = -pose.position.y();
        swapped_pose.position.y() = pose.position.z();
        swapped_pose.position.z() = -pose.position.x();

		
        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
		Eigen::Quaternionf raw_o (pose.orientation.w(), -pose.orientation.y(), pose.orientation.z(), -pose.orientation.x());

		swapped_pose.orientation = apply_offset(raw_o);

        return swapped_pose;
    }
};

class pose_prediction_plugin : public plugin {
public:
    pose_prediction_plugin(const std::string& name, phonebook* pb)
    	: plugin{name, pb}
	{
		pb->register_impl<pose_prediction>(
			std::static_pointer_cast<pose_prediction>(
				std::make_shared<pose_prediction_impl>(pb)
			)
		);
	}
};

PLUGIN_MAIN(pose_prediction_plugin);
