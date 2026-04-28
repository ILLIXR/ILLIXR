#pragma once

#define DOUBLE_INCLUDE
#include "illixr/data_format/latency_data.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"

#undef DOUBLE_INCLUDE

#include <atomic>
#include <optional>

namespace ILLIXR {
class  network_latency_pong_rx : public threadloop {
public:
    [[maybe_unused]] network_latency_pong_rx(const std::string& name, phonebook* pb);

protected:
    skip_option _p_should_skip() override { return skip_option::run; }

    void _p_one_iteration() override;

private:
    void process_pong(const switchboard::ptr<const data_format::latency_pong>& pong);

    static uint64_t get_timestamp_ns();

    const std::shared_ptr<switchboard> switchboard_;

    switchboard::buffered_reader<data_format::latency_pong> pong_reader_;
    switchboard::writer<data_format::network_latency_result> result_writer_;

    std::optional<uint64_t> last_received_seq_;  ///< Empty until first pong received

    std::atomic<double> smoothed_clock_offset_ms_ = 0.0;
    std::atomic<double> smoothed_rtt_ms_          = 0.0;
    bool   clock_offset_initialized_ = false;
};

}
