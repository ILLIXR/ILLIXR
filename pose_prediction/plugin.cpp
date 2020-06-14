#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"
#include "common/plugin.hpp"

using namespace ILLIXR;

class pose_prediction_impl : public pose_prediction {
public:
    pose_prediction_impl(/* non-const */ phonebook* pb)
		: sb{pb->lookup_impl<switchboard>()}
		, _m_slow_pose{sb->subscribe_latest<pose_type>("slow_pose")}
        , _m_true_pose{sb->subscribe_latest<pose_type>("true_pose")}
    { }

    virtual pose_type get_fast_pose() const override {
		const pose_type* pose_ptr = _m_slow_pose->get_latest_ro();
		return correct_pose(
			pose_ptr ? *pose_ptr : pose_type{}
		);
    }

    virtual pose_type get_true_pose() const override {
		const pose_type* pose_ptr = _m_true_pose->get_latest_ro();
		return correct_pose(
			pose_ptr ? *pose_ptr : pose_type{}
		);
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
	const std::shared_ptr<switchboard> sb;
    std::unique_ptr<reader_latest<pose_type>> _m_slow_pose;
	std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
    
    pose_type correct_pose(const pose_type pose) const {
        pose_type swapped_pose {pose};

        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose.position.x() = -pose.position.y();
        swapped_pose.position.y() = pose.position.z();
        swapped_pose.position.z() = -pose.position.x();

		
        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
        // Can be offset with an initial "calibration quaternion."
        swapped_pose.orientation.w() = pose.orientation.w();
        swapped_pose.orientation.x() = -pose.orientation.y();
        swapped_pose.orientation.y() = pose.orientation.z();
        swapped_pose.orientation.z() = -pose.orientation.x();

        return swapped_pose;
    }
};

class pose_prediction_plugin : public plugin {
public:
    pose_prediction_plugin(phonebook* pb_)
    	: pb{pb_}
	{
		pb->register_impl<pose_prediction>(
			std::static_pointer_cast<pose_prediction>(
				std::make_shared<pose_prediction_impl>(pb)
			)
		);
	}

private:
    phonebook* const pb;
	const std::shared_ptr<switchboard> sb;
};

PLUGIN_MAIN(pose_prediction_plugin);
