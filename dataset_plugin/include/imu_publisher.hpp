#pragma once

#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/relative_clock.hpp"
#include "common/threadloop.hpp"
#include "include/dataset_loader.hpp"

#include <chrono> // for std::chrono::nanoseconds
#include <map>
#include <memory> // for std::shared_ptr
#include <string>
#include <thread>  // for std::this_thread::sleep_for
#include <utility> // for std::move

using namespace ILLIXR;

class IMUPublisher : public threadloop {
public:
    IMUPublisher(std::string name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard{pb->lookup_impl<switchboard>()}
        , m_imu_publisher{sb->get_writer<imu_type>("imu")}
        , m_dataset_loader{std::shared_ptr<DatasetLoader>(DatasetLoader::getInstance())}
        , m_data{std::move(m_dataset_loader->getIMUData())}
        , m_data_iterator{m_data.cbegin()}
        , dataset_first_time{m_data_iterator->first}
        , m_rtc{pb->lookup_impl<RelativeClock>()} { }

    virtual skip_option _p_should_skip() override {
        if (m_data_iterator != m_data.end()) {
            std::chrono::nanoseconds dataset_now = m_data_iterator->first;

            // the time difference needs to be casted to `time_point` because `m_rtc` returns that type.
            // the explicit type of the `sleep_time` variable will trigger a typecast of the final calculated expression.
            std::chrono::nanoseconds sleep_time = time_point{dataset_now - dataset_first_time} - m_rtc->now();

            std::this_thread::sleep_for(sleep_time);

            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    virtual void _p_one_iteration() override {
        std::chrono::nanoseconds time_since_start = _m_rtc->now().time_since_epoch();

        std::chrono::nanoseconds upper_bound_time = time_since_start.count() + dataset_first_time;

        std::chrono::nanoseconds lower_bound_time = upper_bound_time - error_cushion;

        for (m_data_iterator = m_data.lower_bound(lower_bound_time); it != m_data.upper_bound(upper_bound_time);
             ++m_data_iterator) {
            IMUData datum = it->second;

            time_point expected_real_time_given_dataset_time(it->first - dataset_first_time);

            m_imu_publisher.put(m_imu_publisher.allocate<imu_type>(expected_real_time_given_dataset_time, datum.angular_v,
                                                                   datum.linear_a, datum.channel));
        }
    }

private:
    const std::shared_ptr<switchboard>                     sb;
    const switchboard::writer<imu_type>                    m_imu_publisher;
    const std::shared_ptr<DatasetLoader>                   m_dataset_loader;
    const std::multimap<std::chrono::nanoseconds, IMUData> m_data;

    std::multimap<std::chrono::nanoseconds, IMUData>::const_iterator m_data_iterator;

    std::chrono::nanoseconds       dataset_first_time;
    std::shared_ptr<RelativeClock> m_rtc;

    const std::chrono::nanoseconds error_cushion(250);
};
