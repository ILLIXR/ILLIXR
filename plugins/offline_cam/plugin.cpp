#include "plugin.hpp"

#include "illixr/data_loading.hpp"

#include <chrono>
#include <regex>
#include <thread>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// combine two maps into one
std::map<ullong, sensor_types> make_map(const std::map<ullong, lazy_load_image>& cam0,
                                        const std::map<ullong, lazy_load_image>& cam1) {
    std::map<ullong, sensor_types> data;
    for (auto& it : cam0) {
        data[it.first].cam0 = it.second;
    }
    for (auto& it : cam1) {
        data[it.first].cam1 = it.second;
    }
    return data;
}

inline std::map<ullong, lazy_load_image> read_data(std::ifstream& gt_file, const std::string& file_name) {
    std::map<ullong, lazy_load_image> data;
    auto                              name = std::regex_replace(file_name, std::regex("\\.csv"), "/");
    for (csv_iterator row{gt_file, 1}; row != csv_iterator{}; ++row) {
        ullong t = std::stoull(row[0]);
        data[t]  = lazy_load_image{name + row[1]};
    }
    return data;
}

[[maybe_unused]] offline_cam::offline_cam(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , switchboard_{phonebook_->lookup_impl<switchboard>()}
    , cam_publisher_{switchboard_->get_writer<binocular_cam_type>("cam")}
    , sensor_data_{make_map(load_data<lazy_load_image>("cam0", "offline_cam", &read_data, switchboard_),
                            load_data<lazy_load_image>("cam1", "offline_cam", &read_data, switchboard_))}
    , dataset_first_time_{sensor_data_.cbegin()->first}
    , last_timestamp_{0}
    , clock_{phonebook_->lookup_impl<relative_clock>()}
    , next_row_{sensor_data_.cbegin()} {
    spdlogger(switchboard_->get_env_char("OFFLINE_CAM_LOG_LEVEL"));
}

ILLIXR::threadloop::skip_option offline_cam::_p_should_skip() {
    if (true) {
        return skip_option::run;
    } else {
        return skip_option::stop;
    }
}

void offline_cam::_p_one_iteration() {
    duration time_since_start = clock_->now().time_since_epoch();
    // duration begin            = time_since_start;
    ullong lookup_time = std::chrono::nanoseconds{time_since_start}.count() + dataset_first_time_;
    std::map<ullong, sensor_types>::const_iterator nearest_row;

    // "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
    auto after_nearest_row = sensor_data_.upper_bound(lookup_time);

    if (after_nearest_row == sensor_data_.cend()) {
#ifndef NDEBUG
        spdlog::get(name_)->warn("Running out of the dataset! Time {} ({} + {}) after last datum {}", lookup_time,
                                 clock_->now().time_since_epoch().count(), dataset_first_time_, sensor_data_.rbegin()->first);
#endif
        // Handling the last camera images. There's no more rows after the nearest_row, so we set after_nearest_row
        // to be nearest_row to avoiding sleeping at the end.
        nearest_row       = std::prev(after_nearest_row, 1);
        after_nearest_row = nearest_row;
        // We are running out of the dataset and the loop will stop next time.
        internal_stop();
    } else if (after_nearest_row == sensor_data_.cbegin()) {
        // Should not happen because lookup_time is bigger than dataset_first_time_
#ifndef NDEBUG
        spdlog::get(name_)->warn("Time {} ({} + {}) before first datum {}", lookup_time,
                                 clock_->now().time_since_epoch().count(), dataset_first_time_, sensor_data_.cbegin()->first);
#endif
    } else {
        // Most recent
        nearest_row = std::prev(after_nearest_row, 1);
    }

    if (last_timestamp_ != nearest_row->first) {
        last_timestamp_ = nearest_row->first;

        auto img0 = nearest_row->second.cam0.load();
        auto img1 = nearest_row->second.cam1.load();

        time_point expected_real_time_given_dataset_time(
            std::chrono::duration<long long, std::nano>{nearest_row->first - dataset_first_time_});
        cam_publisher_.put(cam_publisher_.allocate<binocular_cam_type>(binocular_cam_type{
            expected_real_time_given_dataset_time,
            img0,
            img1,
        }));
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(after_nearest_row->first - dataset_first_time_ -
                                                         clock_->now().time_since_epoch().count() - 2));
}

PLUGIN_MAIN(offline_cam)
