#include <mutex>
#include "common/phonebook.hpp"
#include "common/pose_prediction.hpp"
#include "common/data_format.hpp"

using namespace ILLIXR;

class pose_prediction_impl : public pose_prediction {
public:
    pose_prediction_impl(phonebook* pb_)
    	: pb{pb_}
		, sb{pb->lookup_impl<switchboard>()}
		, _m_pose{sb->subscribe_latest<pose_type>("slow_pose")}
        , _m_true_pose{sb->subscribe_latest<pose_type>("true_pose")}
    { }

	virtual void start() override {
		pb->register_impl<pose_prediction>(this);
	}


    // In the future this service will be pose predict which will predict a pose some t in the future
    virtual pose_type* get_fast_pose() override {
        auto latest_pose = _m_pose->get_latest_ro();
        return correct_pose(latest_pose);
    }

    virtual pose_type* get_fast_true_pose() override {
        auto true_pose = _m_true_pose->get_latest_ro();
        if (true_pose == NULL) {
            return NULL;
        }
        return correct_pose(true_pose);
    }

	virtual void set_offset(const Eigen::Quaternionf& raw_o_times_offset) override {
		std::lock_guard<std::mutex> lock {offset_mutex};
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

	Eigen::Quaternionf apply_offset(const Eigen::Quaternionf& orientation) {
		std::lock_guard<std::mutex> lock {offset_mutex};
		return orientation * offset;
	}

private:
    phonebook* const pb;
    switchboard* const sb;
    std::unique_ptr<reader_latest<pose_type>> _m_pose;
    std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
	Eigen::Quaternionf offset {Eigen::Quaternionf::Identity()};
	std::mutex offset_mutex;
    
    pose_type* correct_pose(pose_type const* pose) {
        pose_type* swapped_pose = new pose_type(*pose);

        // This uses the OpenVINS standard output coordinate system.
        // This is a mapping between the OV coordinate system and the OpenGL system.
        swapped_pose->position.x() = -pose->position.y();
        swapped_pose->position.y() = pose->position.z();
        swapped_pose->position.z() = -pose->position.x();

        // There is a slight issue with the orientations: basically,
        // the output orientation acts as though the "top of the head" is the
        // forward direction, and the "eye direction" is the up direction.
        // Can be offset with an initial "calibration quaternion."
		Eigen::Quaternionf raw_o (pose->orientation.w(), -pose->orientation.y(), pose->orientation.z(), -pose->orientation.x());

		swapped_pose->orientation = apply_offset(raw_o);

        return swapped_pose;
    }
};

PLUGIN_MAIN(pose_prediction_impl);
