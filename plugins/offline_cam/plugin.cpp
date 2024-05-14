#include "data_loading.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/threadloop.hpp"

#include <chrono>
#include <shared_mutex>
#include <thread>

using namespace ILLIXR;

class offline_cam : public threadloop {
public:
    [[maybe_unused]] offline_cam(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , cam_publisher_{switchboard_->get_writer<cam_type>("cam")}
        , sensor_data_{load_data()}
        , dataset_first_time_{sensor_data_.cbegin()->first}
        , last_timestamp_{0}
        , clock_{phonebook_->lookup_impl<relative_clock>()}
        , next_row_{sensor_data_.cbegin()} {
        spdlogger(std::getenv("OFFLINE_CAM_LOG_LEVEL"));
    }

    skip_option _p_should_skip() override {
        if (true) {
            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    void _p_one_iteration() override {
        duration time_since_start = clock_->now().time_since_epoch();
        // duration begin            = time_since_start;
        ullong lookup_time = std::chrono::nanoseconds{time_since_start}.count() + dataset_first_time_;
        std::map<ullong, sensor_types>::const_iterator nearest_row;

        // "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
        auto after_nearest_row = sensor_data_.upper_bound(lookup_time);

        if (after_nearest_row == sensor_data_.cend()) {
#ifndef NDEBUG
            spdlog::get(name_)->warn("Running out of the dataset! Time {} ({} + {}) after last datum {}", lookup_time,
                                    clock_->now().time_since_epoch().count(), dataset_first_time_,
                                    sensor_data_.rbegin()->first);
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
                                    clock_->now().time_since_epoch().count(), dataset_first_time_,
                                    sensor_data_.cbegin()->first);
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
                std::chrono::duration<long, std::nano>{nearest_row->first - dataset_first_time_});
            cam_publisher_.put(cam_publisher_.allocate<cam_type>(cam_type{
                expected_real_time_given_dataset_time,
                img0,
                img1,
            }));
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(after_nearest_row->first - dataset_first_time_ -
                                                             clock_->now().time_since_epoch().count() - 2));
    }

private:
    const std::shared_ptr<switchboard>             switchboard_;
    switchboard::writer<cam_type>                  cam_publisher_;
    const std::map<ullong, sensor_types>           sensor_data_;
    ullong                                         dataset_first_time_;
    ullong                                         last_timestamp_;
    std::shared_ptr<relative_clock>                 clock_;
    std::map<ullong, sensor_types>::const_iterator next_row_;
};

PLUGIN_MAIN(offline_cam)
