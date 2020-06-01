#include "phonebook.hpp"
#include "data_format.hpp"

using namespace ILLIXR;

class pose_prediction : public phonebook::service {
public:
    virtual pose_type get_fast_pose() const = 0;
    virtual pose_type get_fast_true_pose() const = 0;
};
