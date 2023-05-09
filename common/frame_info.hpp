#pragma once
#include "sqlite_wrapper.hpp"

#include <string>

namespace ILLIXR {

static const sqlite::schema site_info_schema{{
    {"plugin", sqlite::type_TEXT},
    {"topic_name", sqlite::type_TEXT},
    {"serial_no", sqlite::type_INTEGER},
    {"custom_time", sqlite::type_INTEGER},
}};

class FrameInfo {
private:
    std::string                           plugin;
    std::string                           topic;
    int64_t                               serial_no;
    std::chrono::steady_clock::time_point time;

public:
    FrameInfo(std::string plugin_ = std::string{}, std::string topic_ = std::string{}, int64_t serial_no_ = 0,
              std::chrono::steady_clock::time_point time_ = std::chrono::steady_clock::time_point{})
        : plugin{plugin_}
        , topic{std::move(topic_)}
        , serial_no{serial_no_}
        , time{time_} { }

    void serialize(std::vector<sqlite::value>& row) const {
        row.emplace_back(plugin);
        row.emplace_back(std::string_view{topic});
        row.emplace_back(serial_no);
        if (time != std::chrono::steady_clock::time_point{}) {
            static std::optional<std::chrono::nanoseconds> process_start;
            if (!process_start) {
                process_start = cpu_timer::get_process().get_start();
            }
            row.emplace_back(int64_t((time.time_since_epoch() - *process_start).count()));
        } else {
            row.emplace_back(int64_t(0));
        }
    }
};

static const FrameInfo default_frame_info;
} // namespace ILLIXR
