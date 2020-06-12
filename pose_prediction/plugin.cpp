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
    {
		std::lock_guard<std::mutex> lock {zero_mutex};
		zero = Eigen::Quaternionf::Identity();
	}

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

	virtual void set_zero(const Eigen::Quaternionf& orientation) override {
		std::lock_guard<std::mutex> lock {zero_mutex};
		zero = (orientation * zero.inverse()).inverse();
	}

private:
    phonebook* const pb;
    switchboard* const sb;
    std::unique_ptr<reader_latest<pose_type>> _m_pose;
    std::unique_ptr<reader_latest<pose_type>> _m_true_pose;
	Eigen::Quaternionf zero;
	std::mutex zero_mutex;
    
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
        swapped_pose->orientation.w() = pose->orientation.w();
        swapped_pose->orientation.x() = -pose->orientation.y();
        swapped_pose->orientation.y() = pose->orientation.z();
        swapped_pose->orientation.z() = -pose->orientation.x();

		std::lock_guard<std::mutex> lock {zero_mutex};
		swapped_pose->orientation = swapped_pose->orientation * zero;

        return swapped_pose;
    }
};

PLUGIN_MAIN(pose_prediction_impl);
