#pragma once

#include "data_loading.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/threadloop.hpp"

namespace ILLIXR {
class offline_cam : public threadloop {
public:
    [[maybe_unused]] offline_cam(const std::string& name, phonebook* pb);
    skip_option _p_should_skip() override;
    void        _p_one_iteration() override;

private:
    const std::shared_ptr<switchboard>             switchboard_;
    switchboard::writer<cam_type>                  cam_publisher_;
    const std::map<ullong, sensor_types>           sensor_data_;
    ullong                                         dataset_first_time_;
    ullong                                         last_timestamp_;
    std::shared_ptr<relative_clock>                clock_;
    std::map<ullong, sensor_types>::const_iterator next_row_;
};
} // namespace ILLIXR