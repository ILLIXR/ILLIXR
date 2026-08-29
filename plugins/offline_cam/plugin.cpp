#include "plugin.hpp"

#include "illixr/data_loading.hpp"
#include "illixr/iterators/csv_iterator.hpp"

#include <chrono>
#include <regex>
#include <thread>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// combine two maps into one
std::map<ullong, sensor_types> make_map(const std::map<ullong, LAZY_TYPE>& cam0, const std::map<ullong, LAZY_TYPE>& cam1) {
    std::map<ullong, sensor_types> data;
    for (auto& it : cam0) {
        data[it.first].cam0 = it.second;
    }
    for (auto& it : cam1) {
        data[it.first].cam1 = it.second;
    }
    return data;
}

inline std::map<ullong, LAZY_TYPE> read_data(std::ifstream& gt_file, const std::string& file_name) {
    std::map<ullong, LAZY_TYPE> data;
    auto                        name = std::regex_replace(file_name, std::regex("\\.csv"), "/");
    for (csv_iterator row{gt_file, 1}; row != csv_iterator{}; ++row) {
        ullong t = std::stoull(row[0]);
#ifdef __ANDROID__
        data[t] = new lazy_load_image(name + row[1]);
#else
        data[t] = lazy_load_image{name + row[1]};
#endif
    }
    return data;
}

[[maybe_unused]] offline_cam::offline_cam(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , cam_publisher_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , sensor_data_{make_map(load_data<LAZY_TYPE>("cam0", "offline_cam", &read_data, switchboard_),
                            load_data<LAZY_TYPE>("cam1", "offline_cam", &read_data, switchboard_))}
    , dataset_first_time_{sensor_data_.cbegin()->first}
    , last_timestamp_{0}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , next_row_{sensor_data_.cbegin()} {
    spdlogger(switchboard_->get_env_char("OFFLINE_CAM_LOG_LEVEL"));
}

ILLIXR::threadloop::skip_option offline_cam::_p_should_skip() {
    if (next_row_ == sensor_data_.end()) {
        return skip_option::stop;
    }

    const auto target_time =
        std::chrono::nanoseconds{next_row_->first - dataset_first_time_};
    std::this_thread::sleep_for(target_time - clock_->now().time_since_epoch());
    return skip_option::run;
}

void offline_cam::_p_one_iteration() {
    assert(next_row_ != sensor_data_.end());
    auto current_row = next_row_++;

    if (last_timestamp_ != current_row->first) {
        last_timestamp_ = current_row->first;

#ifdef __ANDROID__
        auto img0 = current_row->second.cam0->load();
        auto img1 = current_row->second.cam1->load();
#else
        auto img0 = current_row->second.cam0.load();
        auto img1 = current_row->second.cam1.load();
#endif

        time_point expected_real_time_given_dataset_time(
            std::chrono::duration<long long, std::nano>{current_row->first - dataset_first_time_});
        cam_publisher_.put(cam_publisher_.allocate<binocular_cam_type>(binocular_cam_type{
            expected_real_time_given_dataset_time,
            img0,
            img1,
        }));
    }
}

PLUGIN_MAIN(offline_cam)
