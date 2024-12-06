#pragma once

#include "emitter.hpp"
#include "illixr/common/data_format.hpp"
#include "illixr/common/phonebook.hpp"
#include "illixr/common/relative_clock.hpp"
#include "illixr/common/threadloop.hpp"

#include <chrono>  // for std::chrono::nanoseconds
#include <memory>  // for std::shared_ptr
#include <thread>  // for std::this_thread::sleep_for
#include <utility> // for std::move

using namespace ILLIXR;

class Publisher : public threadloop {
public:
    Publisher(std::string name, phonebook* pb)
        : threadloop{name, pb}
        , sb{pb->lookup_impl<switchboard>()}
        , data_emitter{sb->get_writer<ground_truth_type>("image"), sb->get_writer<ground_truth_type>("imu"),
                       sb->get_writer<ground_truth_type>("pose"), sb->get_writer<ground_truth_type>("ground truth")}
        , m_rtc{pb->lookup_impl<RelativeClock>()} { }

    virtual skip_option _p_should_skip() override {
        if (!data_emitter.empty()) {
            // how much time should this thread sleep for?
            std::chrono::nanoseconds sleep_time = data_emitter.sleep_for();

            // sleep for the calculated amount of time
            std::this_thread::sleep_for(sleep_time);

            return skip_option::run;
        } else {
            return skip_option::stop;
        }
    }

    virtual void _p_one_iteration() override {
        std::chrono::nanoseconds time_since_start = m_rtc->now().time_since_epoch();

        data_emitter.emit(time_since_start);
    }

private:
    const std::shared_ptr<switchboard> sb;

    DataEmitter                    data_emitter;
    std::shared_ptr<RelativeClock> m_rtc;
}
