#include "plugin.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

[[maybe_unused]] network_latency_pong_rx::network_latency_pong_rx(const std::string& name_, phonebook* pb_)
        : threadloop{name_, pb_}
        , switchboard_{pb_->lookup_impl<switchboard>()}
        , pong_reader_{switchboard_->get_buffered_reader<latency_pong>("latency_pong")}
        , result_writer_{switchboard_->get_writer<network_latency_result>("network_latency")}
        , last_received_seq_{std::nullopt} {
    spdlog::get("illixr")->info("[network_latency_pong_rx] Initialized");
}

void network_latency_pong_rx::_p_one_iteration() {
    // Block waiting for a pong - this is safe since tx is a separate plugin
    auto pong = pong_reader_.dequeue();
    if (!pong) {
        return;
    }

    // Skip if we've already processed this sequence number (duplicate detection)
    if (last_received_seq_.has_value() && pong->sequence_number <= last_received_seq_.value()) {
        spdlog::get("illixr")->trace("[network_latency_pong_rx] Skipping duplicate seq={}",
                                     pong->sequence_number);
        return;
    }

    process_pong(pong);
    last_received_seq_ = pong->sequence_number;
}

void network_latency_pong_rx::process_pong(const switchboard::ptr<const latency_pong>& pong) {
    uint64_t receive_time = get_timestamp_ns();

    // Calculate round-trip time (always accurate)
    auto rtt_ns = static_cast<double>(receive_time - pong->client_timestamp_ns);
    double rtt_ms = rtt_ns / 1'000'000.0;

    // Calculate directional latencies (only accurate with synchronized clocks)
    auto c2s_ns = static_cast<double>(pong->server_timestamp_ns) - static_cast<double>(pong->client_timestamp_ns);
    double c2s_ms = c2s_ns / 1'000'000.0;

    auto s2c_ns = static_cast<double>(receive_time) - static_cast<double>(pong->server_timestamp_ns);
    double s2c_ms = s2c_ns / 1'000'000.0;

    // Estimate clock offset assuming symmetric latency
    double clock_offset_ms = c2s_ms - (rtt_ms / 2.0);

    spdlog::get("illixr")->warn("Rx time: {}ns",
                                receive_time);

    spdlog::get("illixr")->warn("  Client time: {}ns", pong->client_timestamp_ns);
    spdlog::get("illixr")->warn("  RTT: {:.3f}ms", rtt_ms);
    spdlog::get("illixr")->warn("  Latency: {:.3f}ms", c2s_ms);
    spdlog::get("illixr")->warn("  Server time: {}ns", pong->server_timestamp_ns);
    spdlog::get("illixr")->warn("  Clock offset: {:.3f}ms", clock_offset_ms);

    //spdlog::get("illixr")->warn("Rx time: {}ns  Client time: {}ns  RTT: {.3f}ms  Latency: {.3f}ms  Server time: {}ns  Clock offset: {.3f}ms",
    //                             receive_time,
    //                             pong->client_timestamp_ns,
    //                             rtt_ms,
    //                             c2s_ms,
    //                             pong->server_timestamp_ns,
    //                             clock_offset_ms);

    // Reject samples where RTT is implausibly large (> 500ms on a local
    // network indicates a clock jump or network anomaly)
    if (rtt_ms > 500.0 || rtt_ms < 0.0) {
        spdlog::get("illixr")->warn(
                "[network_latency_pong_rx] Rejecting implausible RTT={:.3f}ms",
                rtt_ms);
        return;
    }

    // Reject offset samples that differ from the current smoothed value
    // by more than 10ms, which indicates a clock adjustment mid-session
    if (clock_offset_initialized_ &&
        std::abs(clock_offset_ms - smoothed_clock_offset_ms_) > 10.0) {
        spdlog::get("illixr")->warn(
                "[network_latency_pong_rx] Rejecting outlier offset={:.3f}ms "
                "(smoothed={:.3f}ms)",
                clock_offset_ms, smoothed_clock_offset_ms_.load());
        return;
    }

    // Maintain a rolling average of clock offset over recent samples to
    // smooth out network jitter. Weight recent samples more heavily.
    constexpr double alpha = 0.1; // EMA smoothing factor
    if (!clock_offset_initialized_) {
        smoothed_clock_offset_ms_ = clock_offset_ms;
        smoothed_rtt_ms_          = rtt_ms;
        clock_offset_initialized_ = true;
    } else {
        smoothed_clock_offset_ms_ = alpha * clock_offset_ms +
                                    (1.0 - alpha) * smoothed_clock_offset_ms_;
        smoothed_rtt_ms_          = alpha * rtt_ms +
                                    (1.0 - alpha) * smoothed_rtt_ms_;
    }
    // Publish the latency result
    result_writer_.put(
            result_writer_.allocate<network_latency_result>(network_latency_result{pong->sequence_number,
                                                                                   rtt_ms,
                                                                                   c2s_ms,
                                                                                   s2c_ms,
                                                                                   clock_offset_ms,
                                                                                   smoothed_clock_offset_ms_,
                                                                                   smoothed_rtt_ms_,
                                                                                   receive_time}));

    spdlog::get("illixr")->info(
            "[network_latency_pong_rx] seq={}: RTT={:.3f}ms (smooth={:.3f}ms) "
            "offset={:.3f}ms (smooth={:.3f}ms)",
            pong->sequence_number, rtt_ms, smoothed_rtt_ms_.load(),
            clock_offset_ms, smoothed_clock_offset_ms_.load());
}

uint64_t network_latency_pong_rx::get_timestamp_ns() {
    auto now = std::chrono::system_clock::now();
    auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(ns.count());
}
