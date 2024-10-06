#pragma once

#include "illixr/common/data_format.hpp"
#include "illixr/common/phonebook.hpp"
#include "illixr/common/relative_clock.hpp"
#include "illixr/common/threadloop.hpp"
#include "dataset_loader.hpp"

#include <chrono> // for std::chrono::nanoseconds
#include <map>
#include <memory> // for std::shared_ptr
#include <string>
#include <thread>  // for std::this_thread::sleep_for
#include <utility> // for std::move

using namespace ILLIXR;

class GroundTruthPublisher : public threadloop {
public:
    GroundTruthPublisher(std::string name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , m_ground_truth_publisher{sb->get_writer<ground_truth_type>("ground truth")}
        , m_dataset_loader{std::shared_ptr<DatasetLoader>(DatasetLoader::getInstance())}
        , m_data{m_dataset_loader->getGroundTruthData()}
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
        std::chrono::nanoseconds time_since_start = m_rtc->now().time_since_epoch();

        std::chrono::nanoseconds upper_bound_time = time_since_start + dataset_first_time;

        std::chrono::nanoseconds lower_bound_time = upper_bound_time - error_cushion;

        for (m_data_iterator = m_data.lower_bound(lower_bound_time); m_data_iterator != m_data.upper_bound(upper_bound_time);
             ++m_data_iterator) {
            GroundTruthData datum = m_data_iterator->second;

            time_point expected_real_time_given_dataset_time(m_data_iterator->first - dataset_first_time);

            m_ground_truth_publisher.put(m_ground_truth_publisher.allocate<ground_truth_type>(
                ground_truth_type{expected_real_time_given_dataset_time, datum.data}));
        }
    }

private:
    const std::shared_ptr<switchboard>                             sb;
    switchboard::writer<ground_truth_type>                         m_ground_truth_publisher;
    const std::shared_ptr<DatasetLoader>                           m_dataset_loader;
    const std::multimap<std::chrono::nanoseconds, GroundTruthData> m_data;

    std::multimap<std::chrono::nanoseconds, GroundTruthData>::const_iterator m_data_iterator;

    std::chrono::nanoseconds       dataset_first_time;
    std::shared_ptr<RelativeClock> m_rtc;

    const std::chrono::nanoseconds error_cushion{250};
};
