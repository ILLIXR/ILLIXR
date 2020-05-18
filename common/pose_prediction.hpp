#include "plugin.hpp"
#include "data_format.hpp"

using namespace ILLIXR;

class pose_prediction : public plugin {
public:
    virtual pose_type* get_fast_pose() = 0;
    virtual pose_type* get_fast_true_pose() = 0;
};
