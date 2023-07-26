#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/data_format.hpp"
#include "common/phonebook.hpp"
#include "common/relative_clock.hpp"
#include "common/threadloop.hpp"

#include "include/dataset_loader.hpp"

using namespace ILLIXR;

class ImagePublisher : public threadloop {
public:
    ImagePublisher(std::string name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard{pb->lookup_impl<switchboard>()}
        , m_image_publisher{sb->get_writer<image_type>("image")}
        , m_dataset_loader{std::shared_ptr<DatasetLoader>(DatasetLoader::getInstance())}
        , m_data{std::move(m_dataset_loader->getImageData())}
        , m_data_iterator{m_data.cbegin()}
        , dataset_first_time{m_data_iterator->first}
        , m_rtc{pb->lookup_impl<RelativeClock>()} { }

    virtual skip_option _p_should_skip() override {
        if (m_data_iterator != m_data.end()) {
            std::chrono::nanoseconds dataset_now = m_data_iterator->first;

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

        std::chrono::nanoseconds lower_bound_time = upper_bound_time - 250ns;

        for (auto it = m_data.lower_bound(lower_bound_time); it != m_data.upper_bound(upper_bound_time); ++it) {
            ImageData datum = it->second;

            time_point expected_real_time_given_dataset_time(it->first - dataset_first_time);
            
            
            m_image_publisher.put(m_image_publisher.allocate<image_type>(image_type{
                expected_real_time_given_dataset_time,
                datum.loadImage(),
                datum.getChannel()
            }));
        }
    }

private:   
    const std::shared_ptr<switchboard> sb;
    const switchboard::writer<image_type> m_image_publisher;
    const std::shared_ptr<DatasetLoader> m_dataset_loader;
    const std::multimap<std::chrono::nanoseconds, ImageData> m_data;

    std::multimap<std::chrono::nanoseconds, ImageData>::const_iterator m_data_iterator;
    
    std::chrono::nanoseconds dataset_first_time;
    std::shared_ptr<RelativeClock> m_rtc;
};
