#include "plugin.hpp"
#include "data_format.hpp"

using namespace ILLIXR;

class pose_prediction : public plugin {
public:
	pose_prediction(std::string name_, phonebook* pb_)
		: plugin{name_, pb_} { }

    virtual pose_type* get_fast_pose() = 0;
    virtual pose_type* get_fast_true_pose() = 0;
};
