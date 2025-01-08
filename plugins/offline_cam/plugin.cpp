#include "illixr/data_loading.hpp"
#include "illixr/opencv_data_types.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/relative_clock.hpp"
#include "illixr/threadloop.hpp"

#include <chrono>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>
#include <regex>
#include <shared_mutex>
#include <thread>

using namespace ILLIXR;

/*
 * Uncommenting this preprocessor macro makes the offline_cam load each data from the disk as it is needed.
 * Otherwise, we load all of them at the beginning, hold them in memory, and drop them in the queue as needed.
 * Lazy loading has an artificial negative impact on performance which is absent from an online-sensor system.
 * Eager loading deteriorates the startup time and uses more memory.
 */
// #define LAZY

class lazy_load_image {
public:
    lazy_load_image() { }

    lazy_load_image(std::string path)
        : _m_path(std::move(path)) {
#ifndef LAZY
        _m_mat = cv::imread(_m_path, cv::IMREAD_GRAYSCALE);
#endif
    }

    [[nodiscard]] cv::Mat load() const {
#ifdef LAZY
        cv::Mat _m_mat = cv::imread(_m_path, cv::IMREAD_GRAYSCALE);
    #error "Linux scheduler cannot interrupt IO work, so lazy-loading is unadvisable."
#endif
        assert(!_m_mat.empty());
        return _m_mat;
    }

private:
    std::string _m_path;
    cv::Mat     _m_mat;
};

typedef struct {
    lazy_load_image cam0;
    lazy_load_image cam1;
} sensor_types;

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
    for (CSVIterator row{gt_file, 1}; row != CSVIterator{}; ++row) {
        ullong t = std::stoull(row[0]);
        data[t]  = {name + row[1]};
    }
    return data;
}

class offline_cam : public threadloop {
public:
    offline_cam(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , sb{pb->lookup_impl<switchboard>()}
        , _m_cam_publisher{sb->get_writer<cam_type>("cam")}
        , _m_sensor_data{make_map(load_data<lazy_load_image>("cam0", "offline_cam", &read_data, sb),
                                  load_data<lazy_load_image>("cam1", "offline_cam", &read_data, sb))}
        , dataset_first_time{_m_sensor_data.cbegin()->first}
        , last_ts{0}
        , _m_rtc{pb->lookup_impl<RelativeClock>()}
        , next_row{_m_sensor_data.cbegin()} {
        spdlogger(sb->get_env_char("OFFLINE_CAM_LOG_LEVEL"));
    }

    skip_option _p_should_skip() override {
        if (true) {
            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    void _p_one_iteration() override {
        duration time_since_start = _m_rtc->now().time_since_epoch();
        // duration begin            = time_since_start;
        ullong lookup_time = std::chrono::nanoseconds{time_since_start}.count() + dataset_first_time;
        std::map<ullong, sensor_types>::const_iterator nearest_row;

        // "std::map::upper_bound" returns an iterator to the first pair whose key is GREATER than the argument.
        auto after_nearest_row = _m_sensor_data.upper_bound(lookup_time);

        if (after_nearest_row == _m_sensor_data.cend()) {
#ifndef NDEBUG
            spdlog::get(name)->warn("Running out of the dataset! Time {} ({} + {}) after last datum {}", lookup_time,
                                    _m_rtc->now().time_since_epoch().count(), dataset_first_time,
                                    _m_sensor_data.rbegin()->first);
#endif
            // Handling the last camera images. There's no more rows after the nearest_row, so we set after_nearest_row
            // to be nearest_row to avoiding sleeping at the end.
            nearest_row       = std::prev(after_nearest_row, 1);
            after_nearest_row = nearest_row;
            // We are running out of the dataset and the loop will stop next time.
            internal_stop();
        } else if (after_nearest_row == _m_sensor_data.cbegin()) {
            // Should not happen because lookup_time is bigger than dataset_first_time
#ifndef NDEBUG
            spdlog::get(name)->warn("Time {} ({} + {}) before first datum {}", lookup_time,
                                    _m_rtc->now().time_since_epoch().count(), dataset_first_time,
                                    _m_sensor_data.cbegin()->first);
#endif
        } else {
            // Most recent
            nearest_row = std::prev(after_nearest_row, 1);
        }

        if (last_ts != nearest_row->first) {
            last_ts = nearest_row->first;

            auto img0 = nearest_row->second.cam0.load();
            auto img1 = nearest_row->second.cam1.load();

            time_point expected_real_time_given_dataset_time(
                std::chrono::duration<long, std::nano>{nearest_row->first - dataset_first_time});
            _m_cam_publisher.put(_m_cam_publisher.allocate<cam_type>(cam_type{
                expected_real_time_given_dataset_time,
                img0,
                img1,
            }));
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(after_nearest_row->first - dataset_first_time -
                                                             _m_rtc->now().time_since_epoch().count() - 2));
    }

private:
    const std::shared_ptr<switchboard>             sb;
    switchboard::writer<cam_type>                  _m_cam_publisher;
    const std::map<ullong, sensor_types>           _m_sensor_data;
    ullong                                         dataset_first_time;
    ullong                                         last_ts;
    std::shared_ptr<RelativeClock>                 _m_rtc;
    std::map<ullong, sensor_types>::const_iterator next_row;
};

PLUGIN_MAIN(offline_cam)
