#include "pose_relay.hpp"

#include <chrono>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <inttypes.h>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

// Helper functions

#ifndef USING_OPENXR
// Convert ILLIXR head pose to xrt_space_relation
static xrt_space_relation build_relation_from_pose(const data_format::pose::fast_head_pose_type& p) {
    xrt_space_relation relation = {};

    relation.pose.orientation.x = p.pose.orientation.x();
    relation.pose.orientation.y = p.pose.orientation.y();
    relation.pose.orientation.z = p.pose.orientation.z();
    relation.pose.orientation.w = p.pose.orientation.w();
    relation.pose.position.x    = p.pose.position.x();
    relation.pose.position.y    = p.pose.position.y();
    relation.pose.position.z    = p.pose.position.z();

    auto flags = static_cast<xrt_space_relation_flags>(
        XRT_SPACE_RELATION_ORIENTATION_VALID_BIT | XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT |
        XRT_SPACE_RELATION_POSITION_VALID_BIT | XRT_SPACE_RELATION_POSITION_TRACKED_BIT);

    if (p.pose.linear_velocity_valid) {
        relation.linear_velocity.x = p.pose.linear_velocity.x();
        relation.linear_velocity.y = p.pose.linear_velocity.y();
        relation.linear_velocity.z = p.pose.linear_velocity.z();
        flags = static_cast<xrt_space_relation_flags>(flags | XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT);
    }
    if (p.pose.angular_velocity_valid) {
        relation.angular_velocity.x = p.pose.angular_velocity.x();
        relation.angular_velocity.y = p.pose.angular_velocity.y();
        relation.angular_velocity.z = p.pose.angular_velocity.z();
        flags = static_cast<xrt_space_relation_flags>(flags | XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT);
    }

    relation.relation_flags = flags;
    return relation;
}
#endif // !USING_OPENXR
// Smooth velocities using a sliding window average over the last N poses.
// Writes the averaged velocity back into rel in-place.
static void filter_velocity(xrt_space_relation& rel, velocity_filter& state, int window_size, float deadband_lin,
                            float deadband_ang) {
    // Push new samples into the circular buffer
    int slot = state.head % velocity_filter::MAX_WINDOW;

    if (rel.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
        state.linear_samples[slot] = {rel.linear_velocity.x, rel.linear_velocity.y, rel.linear_velocity.z};
    } else {
        state.linear_samples[slot] = Eigen::Vector3f::Zero();
    }

    if (rel.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
        state.angular_samples[slot] = {rel.angular_velocity.x, rel.angular_velocity.y, rel.angular_velocity.z};
    } else {
        state.angular_samples[slot] = Eigen::Vector3f::Zero();
    }

    state.head  = (state.head + 1) % velocity_filter::MAX_WINDOW;
    state.count = std::min(state.count + 1, velocity_filter::MAX_WINDOW);

    // Compute mean over the window
    int             n       = std::min(state.count, window_size);
    Eigen::Vector3f lin_sum = Eigen::Vector3f::Zero();
    Eigen::Vector3f ang_sum = Eigen::Vector3f::Zero();
    int             start   = (slot - n + 1 + velocity_filter::MAX_WINDOW) % velocity_filter::MAX_WINDOW;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % velocity_filter::MAX_WINDOW;
        lin_sum += state.linear_samples[idx];
        ang_sum += state.angular_samples[idx];
    }
    Eigen::Vector3f lin_avg = lin_sum / static_cast<float>(n);
    Eigen::Vector3f ang_avg = ang_sum / static_cast<float>(n);

    // Apply deadband to the averaged result
    if (lin_avg.norm() < deadband_lin) {
        lin_avg = Eigen::Vector3f::Zero();
    }
    if (ang_avg.norm() < deadband_ang) {
        ang_avg = Eigen::Vector3f::Zero();
    }

    // Write back
    if (rel.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
        rel.linear_velocity.x = lin_avg.x();
        rel.linear_velocity.y = lin_avg.y();
        rel.linear_velocity.z = lin_avg.z();
    }
    if (rel.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
        rel.angular_velocity.x = ang_avg.x();
        rel.angular_velocity.y = ang_avg.y();
        rel.angular_velocity.z = ang_avg.z();
    }
}

// Extrapolate the given pose to the given timestamp
static xrt_space_relation extrapolate_pose(const xrt_space_relation& relation, double dt) {
    xrt_space_relation result = relation;

    // Extrapolate position using linear velocity if available
    if (relation.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
        result.pose.position.x += relation.linear_velocity.x * static_cast<float>(dt);
        result.pose.position.y += relation.linear_velocity.y * static_cast<float>(dt);
        result.pose.position.z += relation.linear_velocity.z * static_cast<float>(dt);
    }

    // Extrapolate orientation by integrating angular velocity
    if (relation.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
        Eigen::Vector3f ang_vel{relation.angular_velocity.x, relation.angular_velocity.y, relation.angular_velocity.z};
        float           angle = ang_vel.norm() * static_cast<float>(dt);
        if (angle > 1e-6f) {
            Eigen::Quaternionf delta{Eigen::AngleAxisf{angle, ang_vel.normalized()}};
            Eigen::Quaternionf base{relation.pose.orientation.w, relation.pose.orientation.x, relation.pose.orientation.y,
                                    relation.pose.orientation.z};
            Eigen::Quaternionf predicted = (delta * base).normalized();
            result.pose.orientation.x    = predicted.x();
            result.pose.orientation.y    = predicted.y();
            result.pose.orientation.z    = predicted.z();
            result.pose.orientation.w    = predicted.w();
        }
    }

    return result;
}

pose_relay::pose_relay(const std::string& name, phonebook* pb)
    : threadloop{name, pb}
    , log_{spdlogger("debug")}
    , switchboard_{pb->lookup_impl<switchboard>()}
#ifdef USING_OPENXR
    , combined_pose_{switchboard_->get_reader<pose::combined_pose>("combined_pose")}
    , hand_tracking_writer_{switchboard_->get_writer<pose::hand_joint_poses_pair>("hand_poses")}
    , hand_interaction_writer_{switchboard_->get_writer<pose::hand_interaction_poses_pair>("hand_interactions")}
    , palm_pose_writer_{switchboard_->get_writer<pose::palm_poses_pair>("palm_poses")} {
#else
    , render_pose_{switchboard_->get_reader<pose::fast_head_pose_type>("render_pose")} {
#endif
#ifdef USING_OPENXR
    use_hand_tracking_     = switchboard_->get_env_bool("ILLIXR_USE_HAND_TRACKING", "true");
    use_palm_poses_        = switchboard_->get_env_bool("ILLIXR_USE_PALM_POSES", "false");
    use_hand_interactions_ = switchboard_->get_env_bool("ILLIXR_USE_HAND_INTERACTIONS", "false");

    log_->info(use_hand_tracking_ ? "Hand tracking forwarding enabled" : "Hand tracking forwarding disabled");
    log_->info(use_palm_poses_ ? "Palm pose forwarding enabled" : "Palm pose forwarding disabled");
    log_->info(use_hand_interactions_ ? "Hand interaction forwarding enabled" : "Hand interaction forwarding disabled");
#endif
    do_pose_prediction_   = switchboard_->get_env_bool("ILLIXR_ENABLE_POSE_PREDICTION");
    velocity_window_size_ = static_cast<int>(switchboard_->get_env_double("ILLIXR_VELOCITY_WINDOW", 8.));
    velocity_window_size_ = std::clamp(velocity_window_size_, 1, velocity_filter::MAX_WINDOW);
    log_->info("[pose_relay] velocity window size = {}", velocity_window_size_);

    velocity_deadband_lin_ = static_cast<float>(switchboard_->get_env_double("ILLIXR_VELOCITY_DEADBAND_LIN", 0.05));
    velocity_deadband_ang_ = static_cast<float>(switchboard_->get_env_double("ILLIXR_VELOCITY_DEADBAND_ANG", 0.03));
    log_->info("[pose_relay] velocity deadband lin={:.3f} m/s ang={:.3f} rad/s", velocity_deadband_lin_,
               velocity_deadband_ang_);
}

threadloop::skip_option pose_relay::_p_should_skip() {
    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    return skip_option::run;
}

bool pose_relay::fast_pose_reliable() const {
#ifdef USING_OPENXR
    return combined_pose_.get_ro_nullable() != nullptr;
#else
    return render_pose_.get_ro_nullable() != nullptr;
#endif
}

void pose_relay::_p_one_iteration() {
#ifdef USING_OPENXR
    // Try to get the latest pose_with_hands data
    auto pose_data = combined_pose_.get_ro_nullable();
    if (pose_data == nullptr) {
        // log_->debug("[pose_relay]  no new pose");
        return;
    }
    if (pose_data->id == last_pose_id_) {
        return;
    }
    last_pose_id_             = pose_data->id;
    auto clock_now            = std::chrono::steady_clock::now();
    pose_time_[last_pose_id_] = clock_now;
    if (!monado_offset_calibrated_ || clock_now - last_offset_calibration_ > std::chrono::seconds(30)) {
        calibrate_monado_time_offset();
        last_offset_calibration_ = clock_now;
    }

    // ----------------------------------------------------------------
    // Convert pose timestamp from headset XrTime to Monado XrTime.
    //
    // All offsets needed for the conversion are carried in combined_pose,
    // populated by oxr_relay on the headset:
    //
    //   pose_xr_time_ns             — headset XrTime (CLOCK_BOOTTIME ns)
    //   xr_to_monotonic_offset_ns   — headset: CLOCK_MONOTONIC - XrTime
    //   monotonic_to_system_offset_ns — headset: system_clock - CLOCK_MONOTONIC
    //   smoothed_clock_offset_ns    — network: server system_clock - headset system_clock
    //
    // Chain:
    //   headset XrTime
    //   + xr_to_monotonic_offset_ns       → headset CLOCK_MONOTONIC
    //   + monotonic_to_system_offset_ns   → headset system_clock (Unix epoch ns)
    //   + smoothed_clock_offset_ns        → server system_clock (Unix epoch ns)
    //   - windows_epoch_offset_ns_        → Monado time (os_monotonic_get_ns)
    // ----------------------------------------------------------------
    int64_t monado_time_ns = pose_data->pose_xr_time_ns + pose_data->xr_to_monotonic_offset_ns +
        pose_data->monotonic_to_system_offset_ns + static_cast<int64_t>(pose_data->smoothed_clock_offset_ns) -
        windows_epoch_offset_ns_;

    // Cache smoothed_rtt_ns for use in get_pose() without requiring
    // a reader access from within the mutex
    smoothed_rtt_ns_.store(pose_data->smoothed_rtt_ns);

    // Store in bounded pose history, oldest to newest
    constexpr size_t MAX_POSE_HISTORY = 10;
    {
        std::lock_guard<std::mutex> lock(pose_mutex_);
        pose_point                  pp;
        pp.time = static_cast<XrTime>(monado_time_ns);
        pp.id   = pose_data->id;
#    ifdef USING_OPENXR
        pp.pose = pose_data->head_pose.pose;
#    else
        pp.pose = build_relation_from_pose(pose_data->head_pose);
#    endif
        // Compute dt from the previous pose for the filter time constant
        double filter_dt = 0.0;
        if (!current_poses_.empty()) {
            filter_dt = static_cast<double>(pp.time - current_poses_.back().time) * 1e-9;
        }

        filter_velocity(pp.pose, velocity_filter_, velocity_window_size_, velocity_deadband_lin_, velocity_deadband_ang_);
        // Zero velocities that are below the deadband — these represent
        // micro-motion from breathing/tremor, not intentional head movement.
        if (pp.pose.relation_flags & XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT) {
            Eigen::Vector3f lv{pp.pose.linear_velocity.x, pp.pose.linear_velocity.y, pp.pose.linear_velocity.z};
            if (lv.norm() < velocity_deadband_lin_) {
                pp.pose.linear_velocity.x = 0.f;
                pp.pose.linear_velocity.y = 0.f;
                pp.pose.linear_velocity.z = 0.f;
            }
        }
        if (pp.pose.relation_flags & XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT) {
            Eigen::Vector3f av{pp.pose.angular_velocity.x, pp.pose.angular_velocity.y, pp.pose.angular_velocity.z};
            if (av.norm() < velocity_deadband_ang_) {
                pp.pose.angular_velocity.x = 0.f;
                pp.pose.angular_velocity.y = 0.f;
                pp.pose.angular_velocity.z = 0.f;
            }
        }

        current_poses_.push_back(pp);
        if (current_poses_.size() > MAX_POSE_HISTORY) {
            current_poses_.erase(current_poses_.begin());
        }

        // Prune pose_map_ entries older than the oldest stored pose
        if (!current_poses_.empty()) {
            auto oldest = current_poses_.front().time;
            for (auto it = pose_map_.begin(); it != pose_map_.end();) {
                if (it->first < oldest) {
                    it = pose_map_.erase(it);
                } else {
                    break;
                }
            }
        }
    }

    // Forward hand tracking data to Monado (only for new data)
    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};

    // Throttle debug logging to once every 300 new frames (~5 s at 60 Hz)
    static uint64_t log_frame_counter = 0;
    const bool      should_log        = (++log_frame_counter % 300000) == 1;

    if (use_hand_tracking_ && pose_data->valid_data & pose::HANDS_TRACKED) {
        // Create a copy of hand tracking data to publish
        auto hand_data = pose_data->hand_poses;

        // Update timestamp to local time
        hand_data.sensor_time = now;

        // Publish to switchboard for Monado's ILLIXR driver
        hand_tracking_writer_.put(std::make_shared<pose::hand_joint_poses_pair>(hand_data));

        if (should_log) {
            const char*      hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand hand_enums[] = {pose::LEFT, pose::RIGHT};
            for (int h = 0; h < 2; h++) {
                const auto& hd = hand_data.hands.at(hand_enums[h]);
                if (!hd.is_active) {
                    log_->debug("[hand_tracking] {} not tracked", hand_names[h]);
                    continue;
                }
                // Log wrist (joint 1) and index tip (joint 10) as representative joints
                const auto& wrist     = hd.values.hand_joint_set_default[pose::WRIST];
                const auto& index_tip = hd.values.hand_joint_set_default[pose::INDEX_TIP];
                log_->debug("[hand_tracking] {} | active={} | "
                            "wrist=({:.3f},{:.3f},{:.3f}) flags=0x{:02x} | "
                            "index_tip=({:.3f},{:.3f},{:.3f}) flags=0x{:02x}",
                            hand_names[h], hd.is_active, wrist.relation.pose.position.x, wrist.relation.pose.position.y,
                            wrist.relation.pose.position.z, static_cast<unsigned>(wrist.relation.relation_flags),
                            index_tip.relation.pose.position.x, index_tip.relation.pose.position.y,
                            index_tip.relation.pose.position.z, static_cast<unsigned>(index_tip.relation.relation_flags));
            }
        }
    }

    if (use_palm_poses_ && pose_data->valid_data & pose::PALMS_TRACKED) {
        // Create a copy of palm pose data to publish
        auto palm_data        = pose_data->palm_poses;
        palm_data.sensor_time = now;

        palm_pose_writer_.put(std::make_shared<pose::palm_poses_pair>(palm_data));

        if (should_log) {
            const char*      hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand hand_enums[] = {pose::LEFT, pose::RIGHT};
            for (int h = 0; h < 2; h++) {
                const auto& pd = palm_data.hands.at(hand_enums[h]);
                if (pd.relation_flags == 0) {
                    log_->debug("[palm_pose] {} not valid", hand_names[h]);
                    continue;
                }
                log_->debug("[palm_pose] {} | "
                            "pos=({:.3f},{:.3f},{:.3f}) | "
                            "ori=({:.3f},{:.3f},{:.3f},{:.3f})",
                            hand_names[h], pd.pose.position.x, pd.pose.position.y, pd.pose.position.z, pd.pose.orientation.x,
                            pd.pose.orientation.y, pd.pose.orientation.z, pd.pose.orientation.w);
            }
        }
    }

    if (use_hand_interactions_ && pose_data->valid_data & pose::INTERACTIONS_TRACKED) {
        // Create a copy of hand interaction data to publish
        auto hand_interaction_data        = pose_data->hand_interactions;
        hand_interaction_data.sensor_time = now;

        hand_interaction_writer_.put(std::make_shared<pose::hand_interaction_poses_pair>(hand_interaction_data));

        if (should_log) {
            const char*                       hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand                  hand_enums[] = {pose::LEFT, pose::RIGHT};
            const char*                       type_names[] = {"AIM", "GRIP", "PINCH", "POKE"};
            const pose::interaction_pose_type type_enums[] = {pose::AIM, pose::GRIP, pose::PINCH, pose::POKE};
            for (int h = 0; h < 2; h++) {
                const auto& hip = hand_interaction_data.hands.at(hand_enums[h]);
                for (int t = 0; t < pose::NUM_INTERACTION_POSES; t++) {
                    const auto& ip = hip.at(type_enums[t]);
                    if (!ip.valid()) {
                        spdlog::get("illixr")->debug("NOT VALID");
                        continue;
                    }
                    // POKE has no value/ready binding; log it pose-only
                    if (type_enums[t] == pose::POKE) {
                        log_->debug("[hand_interaction] {} {} | "
                                    "pos=({:.3f},{:.3f},{:.3f})",
                                    hand_names[h], type_names[t], ip.pose.position.x, ip.pose.position.y, ip.pose.position.z);
                    } else {
                        log_->debug("[hand_interaction] {} {} | "
                                    "pos=({:.3f},{:.3f},{:.3f}) | "
                                    "value={:.3f} ready={}",
                                    hand_names[h], type_names[t], ip.pose.position.x, ip.pose.position.y, ip.pose.position.z,
                                    ip.value, ip.ready ? "true" : "false");
                    }
                }
            }
        }
    }
#else
    auto pose = render_pose_.get_ro_nullable();
    if (pose == nullptr) {
        return;
    }
    current_pose_ = *pose;
#endif
}

#ifdef USING_OPENXR
xrt_space_relation pose_relay::get_pose() const {
    return get_pose(os_monotonic_get_ns());
}

xrt_space_relation pose_relay::get_pose(XrTime future_time) const {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    if (current_poses_.empty()) {
        // No pose data yet — return identity
        xrt_space_relation relation = {};
        relation.pose.orientation.w = 1.0f;
        relation.relation_flags     = XRT_SPACE_RELATION_BITMASK_NONE;
        return relation;
    }

    // Check cache first
    auto cached = pose_map_.find(future_time);
    if (cached != pose_map_.end()) {
        return cached->second.pose;
    }

    // ----------------------------------------------------------------
    // Step 1: Find the most recent pose whose timestamp is at or before
    //         future_time. Iterate from newest to oldest.
    // ----------------------------------------------------------------
    const pose_point* best = &current_poses_.front();
    for (auto it = current_poses_.rbegin(); it != current_poses_.rend(); ++it) {
        if (it->time <= future_time) {
            best = &(*it);
            break;
        }
    }

    // ----------------------------------------------------------------
    // Step 2: Add full pipeline latency to future_time to get the
    //         target display time. Full RTT is used (not half) because
    //         the pose travels to the server and the rendered frame
    //         travels back — both legs of the round trip contribute.
    //
    //   target = future_time
    //          + smoothed_rtt          (full round trip, from combined_pose)
    //          + encode_latency
    //          + decode_latency
    // ----------------------------------------------------------------
    int64_t pipeline_ns = static_cast<int64_t>(smoothed_rtt_ns_.load() + ENCODE_LATENCY_NS + DECODE_LATENCY_NS + 30);

    XrTime target_time = future_time + static_cast<XrTime>(pipeline_ns);

    // ----------------------------------------------------------------
    // Step 3: Compute dt and extrapolate
    // ----------------------------------------------------------------
    double dt = static_cast<double>(target_time - best->time) * 1e-9;

    if (dt < 0.0 || dt > 0.2) {
        static uint64_t warn_counter = 0;
        if (++warn_counter % 100 == 1) {
            log_->warn("[pose_relay] get_pose: dt={:.3f}ms out of range "
                       "(future_time={:.3f}ms best_pose_time={:.3f}ms "
                       "pipeline={:.3f}ms)",
                       dt * 1000.0, future_time / 1'000'000.0, best->time / 1'000'000.0, pipeline_ns / 1'000'000.0);
        }
        // Cache and return the best pose without extrapolation
        pose_map_[future_time] = {best->id, best->pose};
        return best->pose;
    }
    if (do_pose_prediction_) {
        xrt_space_relation p_pose = extrapolate_pose(best->pose, dt);
        pose_map_[future_time]    = {best->id, p_pose};
        return p_pose;
    }
    return best->pose;
}

#else

data_format::pose::fast_head_pose_type pose_relay::get_pose(time_point future_time) const {
    if (current_poses_.empty()) {
        return {};
    }
    return current_poses_.back().pose;
}

data_format::pose::fast_head_pose_type pose_relay::get_pose() const {
    if (current_poses_.empty()) {
        return {};
    }
    return current_poses_.back().pose;
}

#endif

void pose_relay::calibrate_monado_time_offset() {
    // Take several samples and keep the one with the smallest round-trip
    // to minimise scheduling jitter.
    //
    // Computes windows_epoch_offset_ns_ such that:
    //   system_clock_ns = os_monotonic_get_ns() + windows_epoch_offset_ns_
    //
    // On Windows: os_monotonic_get_ns() is QPC-based (boot-relative),
    //             system_clock is Unix epoch. Their difference is the
    //             Unix epoch timestamp of Windows boot.
    // On Linux:   os_monotonic_get_ns() is CLOCK_MONOTONIC,
    //             system_clock is CLOCK_REALTIME. Their difference is
    //             the offset between the two clocks.
    constexpr int NUM_SAMPLES = 20;
    int64_t       best_offset = 0;
    int64_t       best_rtt    = INT64_MAX;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        auto    before  = std::chrono::system_clock::now();
        int64_t mono_ns = static_cast<int64_t>(os_monotonic_get_ns());
        auto    after   = std::chrono::system_clock::now();

        int64_t before_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(before.time_since_epoch()).count();
        int64_t after_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(after.time_since_epoch()).count();
        int64_t rtt       = after_ns - before_ns;
        int64_t mid_ns    = before_ns + rtt / 2;

        // system_clock_ns - os_monotonic_ns = epoch offset
        int64_t offset = mid_ns - mono_ns;

        if (rtt < best_rtt) {
            best_rtt    = rtt;
            best_offset = offset;
        }
    }

    // Store: windows_unix_epoch_ns = qpc_ns + windows_epoch_offset_ns_
    windows_epoch_offset_ns_  = best_offset;
    monado_offset_calibrated_ = true;
}

uint64_t pose_relay::get_pose_id_for_time(XrTime at_time) const {
    std::lock_guard<std::mutex> lock(pose_mutex_);
    auto                        it = pose_map_.find(at_time);
    if (it != pose_map_.end()) {
        return it->second.id;
    }
    // Not found in cache — search current_poses_ for the closest match
    // as a fallback for cases where get_pose was not called for this time
    if (current_poses_.empty()) {
        return 0;
    }
    const pose_point* best = &current_poses_.front();
    for (auto rit = current_poses_.rbegin(); rit != current_poses_.rend(); ++rit) {
        if (rit->time <= at_time) {
            best = &(*rit);
            break;
        }
    }
    return best->id;
}

uint64_t pose_relay::find_pose_id_by_orientation(const Eigen::Quaternionf& q) const {
    std::lock_guard<std::mutex> lock(pose_mutex_);

    uint64_t best_id  = 0;
    float    best_dot = -1.f;

    // Search pose_map_ first since it contains extrapolated poses that
    // are closer to what Unity actually rendered with
    for (const auto& entry : pose_map_) {
        const auto&        rel = entry.second.pose;
        Eigen::Quaternionf entry_q{rel.pose.orientation.w, rel.pose.orientation.x, rel.pose.orientation.y,
                                   rel.pose.orientation.z};
        float              dot = std::abs(q.dot(entry_q));
        if (dot > best_dot) {
            best_dot = dot;
            best_id  = entry.second.id;
        }
    }

    // Fall back to current_poses_ if pose_map_ had no good match
    if (best_dot < 0.99999f) {
        for (const auto& pp : current_poses_) {
            Eigen::Quaternionf pp_q{pp.pose.pose.orientation.w, pp.pose.pose.orientation.x, pp.pose.pose.orientation.y,
                                    pp.pose.pose.orientation.z};
            float              dot = std::abs(q.dot(pp_q));
            if (dot > best_dot) {
                best_dot = dot;
                best_id  = pp.id;
            }
        }
    }

    if (best_dot < 0.99999f) {
        // No sufficiently close match found
        spdlog::get("illixr")->debug("No matching pose");
        return 0;
    }

    return best_id;
}

std::chrono::steady_clock::time_point pose_relay::get_pose_time(uint64_t id) const {
    return pose_time_.at(id);
}