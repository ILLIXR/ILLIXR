#include "phonebook.hpp"
#include "data_format.hpp"

using namespace ILLIXR;

class pose_prediction : public phonebook::service {
public:
	virtual fast_pose_type get_fast_pose() const = 0;
	virtual pose_type get_true_pose() const = 0;
	virtual fast_pose_type get_fast_pose(time_point future_time) const = 0;
	virtual bool fast_pose_reliable() const = 0;
	virtual bool true_pose_reliable() const = 0;
	virtual void set_offset(const Eigen::Quaternionf& orientation) = 0;
	virtual Eigen::Quaternionf get_offset() = 0;
    virtual pose_type correct_pose(const pose_type pose) const = 0;
	virtual ~pose_prediction() { }
};
