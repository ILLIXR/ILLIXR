#include "oxr_relay.hpp"
const int log_interval = 300; // pose logging interval in frames
#include <chrono>
#include <spdlog/spdlog.h>

using namespace ILLIXR;
using namespace ILLIXR::data_format;

#define TIME_CUTOFF 3.0

oxr_relay::oxr_relay(const std::string& name, phonebook* pb)
        : threadloop{name, pb}
        , switchboard_{phonebook_->lookup_impl<switchboard>()}
        , clock_{phonebook_->lookup_impl<relative_clock>()}
        , combined_pose_writer_{switchboard_->get_network_writer<data_format::pose::combined_pose>("combined_pose", {.serialization_method=network::topic_config::BOOST, .transport_method=network::topic_config::UDP})}
        , latency_reader_{switchboard_->get_reader<network_latency_result>("network_latency")} {
}

void oxr_relay::destroy() {
    // Destroy hand interaction action set and spaces first
    destroy_hand_interaction();
    // Destroy hand trackers first
    destroy_hand_tracking();
}

oxr_relay::~oxr_relay() {
    destroy();
}

void oxr_relay::initialize(XrInstance instance, XrSession session, XrSpace local, XrSpace view) {
    instance_ = instance;
    session_ = session;
    local_space_ = local;
    view_space_ = view;

    // Initialize hand tracking after session is created
    if (init_hand_tracking()) {
        spdlog::get("illixr")->info("OXR hand tracking initialized successfully");
    } else {
        spdlog::get("illixr")->warn("OXR hand tracking not available");
    }
    // Initialize hand interaction (requires session and instance to be ready)
    if (init_hand_interaction()) {
        spdlog::get("illixr")->info("OXR hand interaction initialized successfully");
    } else {
        spdlog::get("illixr")->warn("OXR hand interaction not available");
    }

    initialized_ = true;
    spdlog::get("illixr")->info("oxr_relay: initialized");
}

threadloop::skip_option oxr_relay::_p_should_skip() {
    if (!initialized_) {
        return skip_option::skip_and_yield;
    }
    if (predicted_time_.load() == 0)
        return skip_option::skip_and_spin;
    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    return skip_option::run;
}

void oxr_relay::_p_one_iteration() {
    auto iter_start = std::chrono::steady_clock::now();
    auto log_iter_time = [&](const char* exit_point) {
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - iter_start).count() / 1000.0;
        if (ms > TIME_CUTOFF)
            spdlog::get("illixr")->warn("[oxr_timing] _p_one_iteration took {:.2f}ms ({})", ms, exit_point);
    };
    pose::head_pose_type   head_pose;

    // Periodically recompute the monotonic-to-system offset to guard
    // against NTP clock adjustments invalidating the startup calibration.
    // CLOCK_MONOTONIC is never adjusted by NTP so it is always a stable
    // reference; only the system_clock side can jump.
    auto now = std::chrono::steady_clock::now();
    if (!time_offsets_calibrated_ ||
        now - last_offset_calibration_ > std::chrono::seconds(30)) {
        calibrate_time_offsets();
        last_offset_calibration_ = now;
    }

    // Verify the XrTime == CLOCK_BOOTTIME assumption once on first iteration.
    // If the difference is large (> 1s) something is wrong and we log a warning.
    if (!xr_time_verified_) {
        struct timespec ts_boot;
        clock_gettime(CLOCK_BOOTTIME, &ts_boot);
        int64_t boot_ns   = ts_boot.tv_sec * 1'000'000'000LL + ts_boot.tv_nsec;
        int64_t xr_ns     = static_cast<int64_t>(predicted_time_.load());
        int64_t diff_ms   = std::abs(boot_ns - xr_ns) / 1'000'000;

        if (diff_ms > 1000) {
            spdlog::get("illixr")->warn(
                "oxr_relay: XrTime ({}) differs from CLOCK_BOOTTIME ({}) "
                "by {}ms — xr_to_monotonic_offset assumption may be wrong",
                xr_ns, boot_ns, diff_ms);
        } else {
            //spdlog::get("illixr")->info(
            //    "oxr_relay: XrTime vs CLOCK_BOOTTIME diff={}ms — "
            //    "assumption confirmed", diff_ms);
        }
        xr_time_verified_ = true;
    }
    // store the predicted time so it is consistent across add data gathering
    XrTime pose_time = predicted_time_.load();

    if (XR_SUCCEEDED(get_head_pose(pose_time,
                                   &head_pose))) {
        if (memcmp(&head_pose, &last_pose_, sizeof(XrPosef)) == 0) {
            log_iter_time("pose unchanged");
            return;
        }
        current_head_pose_ = head_pose;
        last_pose_ = head_pose;
        if (head_pose.relation_flags & pose::XRT_SPACE_RELATION_ORIENTATION_VALID_BIT &&
            head_pose.relation_flags & pose::XRT_SPACE_RELATION_POSITION_VALID_BIT) {
            //spdlog::get("illixr")->debug("pose valid, writing {}, {}, {}, {}", headPose.orientation.w, headPose.orientation.x, headPose.orientation.y, headPose.orientation.z);
        } else {
            spdlog::get("illixr")->debug("Head pose not tracked");
        }
    } else {
        spdlog::get("illixr")->debug("Update Failed");
    }

    // Update hand tracking at the predicted display time
    update_hand_tracking(pose_time);
    // Update hand interaction poses at the predicted display time
    update_hand_interaction(pose_time);

    push_poses(pose_time);
    log_iter_time("normal");
}

bool oxr_relay::init_hand_tracking() {
    if (!hand_tracking_supported_) {
        spdlog::get("illixr")->debug("Hand tracking not supported, skipping initialization");
        return false;
    }

    // Get function pointers for hand tracking
    OXR(xrGetInstanceProcAddr(instance_, "xrCreateHandTrackerEXT",
                              (PFN_xrVoidFunction*)&xr_create_hand_tracker_))
    OXR(xrGetInstanceProcAddr(instance_, "xrDestroyHandTrackerEXT",
                              (PFN_xrVoidFunction*)&xr_destroy_hand_tracker_))
    OXR(xrGetInstanceProcAddr(instance_, "xrLocateHandJointsEXT",
                              (PFN_xrVoidFunction*)&xr_locate_hand_joints_))

    if (!xr_create_hand_tracker_ || !xr_destroy_hand_tracker_ || !xr_locate_hand_joints_) {
        spdlog::get("illixr")->error("Failed to get hand tracking function pointers");
        hand_tracking_supported_ = false;
        return false;
    }

    // Create left hand tracker
    XrHandTrackerCreateInfoEXT create_info = {XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
    create_info.hand = XR_HAND_LEFT_EXT;
    create_info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;

    XrResult result = xr_create_hand_tracker_(session_, &create_info, &left_hand_tracker_);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->error("Failed to create left hand tracker: {}", static_cast<int>(result));
        return false;
    }

    // Create right hand tracker
    create_info.hand = XR_HAND_RIGHT_EXT;
    result = xr_create_hand_tracker_(session_, &create_info, &right_hand_tracker_);
    if (XR_FAILED(result)) {
        spdlog::get("illixr")->error("Failed to create right hand tracker: {}", static_cast<int>(result));
        xr_destroy_hand_tracker_(left_hand_tracker_);
        left_hand_tracker_ = XR_NULL_HANDLE;
        return false;
    }

    spdlog::get("illixr")->info("Hand trackers created successfully");
    return true;
}

void oxr_relay::destroy_hand_tracking() {
    if (xr_destroy_hand_tracker_) {
        if (left_hand_tracker_ != XR_NULL_HANDLE) {
            xr_destroy_hand_tracker_(left_hand_tracker_);
            left_hand_tracker_ = XR_NULL_HANDLE;
        }
        if (right_hand_tracker_ != XR_NULL_HANDLE) {
            xr_destroy_hand_tracker_(right_hand_tracker_);
            right_hand_tracker_ = XR_NULL_HANDLE;
        }
    }
}

void oxr_relay::update_hand_tracking(XrTime predicted_time) {
    if (!hand_tracking_supported_ || !xr_locate_hand_joints_) {
        return;
    }

    current_hand_poses_.sensor_time = clock_->now();

    // Process both hands
    XrHandTrackerEXT trackers[2] = {left_hand_tracker_, right_hand_tracker_};
    pose::hand_joint_poses* hand_states[2] = {&current_hand_poses_[pose::LEFT], &current_hand_poses_[pose::RIGHT]};
    const char* hand_names[2] = {"left", "right"};

    for (int hand_idx = 0; hand_idx < 2; hand_idx++) {
        if (trackers[hand_idx] == XR_NULL_HANDLE) {
            spdlog::get("illixr")->debug("Hand {}: No tracker handle", hand_names[hand_idx]);
            continue;
        }

        // Prepare joint locations array
        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joint_locations{};
        // Prepare velocity array (optional but useful)
        std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> joint_velocities{};

        // Set up velocity container (this one has type/next)
        XrHandJointVelocitiesEXT velocities = {XR_TYPE_HAND_JOINT_VELOCITIES_EXT};
        velocities.next = nullptr;
        velocities.jointCount = XR_HAND_JOINT_COUNT_EXT;
        velocities.jointVelocities = joint_velocities.data();

        // Set up locations container with velocity chain (this one has type/next)
        XrHandJointLocationsEXT locations = {XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        locations.next = &velocities;
        locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locations.jointLocations = joint_locations.data();
        locations.isActive = XR_FALSE;  // Initialize to false

        // Locate info
        XrHandJointsLocateInfoEXT locate_info = {XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        locate_info.baseSpace = local_space_;
        locate_info.time = predicted_time;

        auto t0 = std::chrono::steady_clock::now();
        XrResult result = xr_locate_hand_joints_(trackers[hand_idx], &locate_info, &locations);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
        if (ms > TIME_CUTOFF)
            spdlog::get("illixr")->warn("[oxr_timing] xrLocateHandJointsEXT ({}) took {:.2f}ms",
                                        hand_names[hand_idx], ms);

        // Throttle per-hand logging to once every 300 frames (~5 s at 60 Hz)
        static uint64_t ht_log_counter[2] = {0, 0};
        const bool ht_should_log = (++ht_log_counter[hand_idx] % log_interval) == 1;

        if (ht_should_log) {
            spdlog::get("illixr")->debug("[hand_tracking] {} xrLocateHandJointsEXT result={} isActive={}",
                                         hand_names[hand_idx],
                                         static_cast<int>(result),
                                         locations.isActive ? "TRUE" : "FALSE");
        }

        if (XR_SUCCEEDED(result) && locations.isActive) {
            // Calculate confidence based on how many joints are tracked

            // Convert all joints
            hand_states[hand_idx]->update(joint_locations, joint_velocities);

            if (ht_should_log) {
                const auto& wrist     = hand_states[hand_idx]->joints[XR_HAND_JOINT_WRIST_EXT];
                const auto& index_tip = hand_states[hand_idx]->joints[XR_HAND_JOINT_INDEX_TIP_EXT];
                spdlog::get("illixr")->debug(
                    "[hand_tracking] {} ACTIVE | active={} | "
                    "wrist=({:.3f},{:.3f},{:.3f}) flags=0x{:02x} | "
                    "index_tip=({:.3f},{:.3f},{:.3f}) flags=0x{:02x}",
                    hand_names[hand_idx],
                    hand_states[hand_idx]->is_active,
                    wrist.relation.pose.position.x, wrist.relation.pose.position.y, wrist.relation.pose.position.z,
                    static_cast<unsigned>(wrist.relation.relation_flags),
                    index_tip.relation.pose.position.x, index_tip.relation.pose.position.y, index_tip.relation.pose.position.z,
                    static_cast<unsigned>(index_tip.relation.relation_flags));
            }
        } else {
            hand_states[hand_idx]->is_active   = false;

            if (XR_FAILED(result)) {
                spdlog::get("illixr")->warn("[hand_tracking] {} xrLocateHandJointsEXT FAILED result={}",
                                            hand_names[hand_idx], static_cast<int>(result));
            } else if (ht_should_log) {
                spdlog::get("illixr")->debug("[hand_tracking] {} isActive=FALSE (controllers may be active)",
                                             hand_names[hand_idx]);
            }
        }
    }

    if (current_hand_poses_.has_hands()) {
        //spdlog::get("illixr")->debug("[hand_tracking] publishing: left={} right={}",
        //                             current_hand_poses_.hands[pose::LEFT].is_active  ? "tracked" : "not tracked",
        //                             current_hand_poses_.hands[pose::RIGHT].is_active ? "tracked" : "not tracked");

    } else {
        // Only log occasionally to avoid spam
        static uint64_t no_tracking_count = 0;
        if (++no_tracking_count % log_interval == 1) {
            spdlog::get("illixr")->info("[hand_tracking] not publishing — has_hands()=false "
                                        "(left tracked={} right tracked={}) count={}",
                                        current_hand_poses_.hands[pose::LEFT].is_active,
                                        current_hand_poses_.hands[pose::RIGHT].is_active,
                                        no_tracking_count);
        }
    }
}

XrResult oxr_relay::get_head_pose(XrTime time, pose::head_pose_type* out_pose) {
    // Chain XrSpaceVelocity into the location query so the runtime fills both
    // pose and velocity in a single call, guaranteeing they are time-coherent.
    XrSpaceVelocity velocity = {XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    location.next = &velocity;
    auto t0 = std::chrono::steady_clock::now();
    OXR(xrLocateSpace(view_space_, local_space_, time, &location))
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    if (ms > TIME_CUTOFF)
        spdlog::get("illixr")->warn("[oxr_timing] xrLocateSpace (head) took {:.2f}ms", ms);
    out_pose->pose = location.pose;
    out_pose->linear_velocity = velocity.linearVelocity;
    out_pose->angular_velocity = velocity.angularVelocity;
    out_pose->set_flags(location.locationFlags, velocity.velocityFlags);
    return XR_SUCCESS;
}

void oxr_relay::push_poses(XrTime predicted_time) {
     // Throttle detailed logging to once every 300 calls (~5 s at 60 Hz)
    static uint64_t log_push_counter = 0;
    const bool should_log = (++log_push_counter % log_interval) == 1;
    if (!current_head_pose_.valid()) {
        //spdlog::get("illixr")->debug("  not valid");
        return;
    }

    pose::fast_head_pose_type current_pose;
    current_pose.pose = current_head_pose_;

    // Hand joint tracking
    if (current_hand_poses_.has_hands())
        current_hand_poses_.sensor_time = clock_->now();

    if (should_log) {
        if (current_hand_poses_.has_hands()) {
            const char*      hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand hand_enums[] = {pose::LEFT, pose::RIGHT};
            for (int h = 0; h < 2; h++) {
                const auto& hd = current_hand_poses_.hands.at(hand_enums[h]);
                if (!hd.is_active) {
                    spdlog::get("illixr")->debug("[hand_tracking] {} not tracked", hand_names[h]);
                    continue;
                }
                const auto& wrist = hd.joints[pose::WRIST];
                const auto& index_tip = hd.joints[pose::INDEX_TIP];
                spdlog::get("illixr")->debug(
                        "[hand_tracking] {} | active={} | "
                        "wrist=({:.3f},{:.3f},{:.3f}) flags=0x{:02x} | "
                        "index_tip=({:.3f},{:.3f},{:.3f}) flags=0x{:02x}",
                        hand_names[h], hd.is_active,
                        wrist.relation.pose.position.x, wrist.relation.pose.position.y, wrist.relation.pose.position.z,
                        static_cast<unsigned>(wrist.relation.relation_flags),
                        index_tip.relation.pose.position.x, index_tip.relation.pose.position.y,
                        index_tip.relation.pose.position.z,
                        static_cast<unsigned>(index_tip.relation.relation_flags));
            }
        } else {
            spdlog::get("illixr")->debug("[hand_tracking] data available but no hands tracked");
        }
        // Palm poses
        if (current_palm_poses_.is_valid()) {
            const char*      hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand hand_enums[] = {pose::LEFT, pose::RIGHT};
            for (int h = 0; h < 2; h++) {
                const auto& pd = current_palm_poses_.hands.at(hand_enums[h]);
                if (!pd.is_valid()) {
                    spdlog::get("illixr")->debug("[palm_pose] {} not valid", hand_names[h]);
                    continue;
                }
                spdlog::get("illixr")->debug(
                        "[palm_pose] {} | "
                        "pos=({:.3f},{:.3f},{:.3f}) | "
                        "ori=({:.3f},{:.3f},{:.3f},{:.3f})",
                        hand_names[h],
                        pd.pose.position.x,    pd.pose.position.y,    pd.pose.position.z,
                        pd.pose.orientation.x, pd.pose.orientation.y,
                        pd.pose.orientation.z, pd.pose.orientation.w);
            }
        } else {
            spdlog::get("illixr")->debug("[palm_pose] no data on switchboard");
        }

        // Hand interactions
        if (current_hand_interactions_.is_valid()) {
            const char*                    hand_names[] = {"LEFT", "RIGHT"};
            const pose::hand               hand_enums[] = {pose::LEFT, pose::RIGHT};
            const char*                    type_names[] = {"AIM", "GRIP", "PINCH", "POKE"};
            const pose::interaction_pose_type type_enums[] = {
                    pose::AIM, pose::GRIP, pose::PINCH, pose::POKE
            };
            for (int h = 0; h < 2; h++) {
                const auto& hip = current_hand_interactions_.hands.at(hand_enums[h]);
                for (int t = 0; t < pose::NUM_INTERACTION_POSES; t++) {
                    const auto& ip = hip.at(type_enums[t]);
                    if (!ip.valid()) continue;
                    if (type_enums[t] == pose::POKE) {
                        spdlog::get("illixr")->debug(
                                "[hand_interaction] {} {} | pos=({:.3f},{:.3f},{:.3f})",
                                hand_names[h], type_names[t],
                                ip.pose.position.x, ip.pose.position.y, ip.pose.position.z);
                    } else {
                        spdlog::get("illixr")->debug(
                                "[hand_interaction] {} {} | pos=({:.3f},{:.3f},{:.3f}) | value={:.3f} ready={}",
                                hand_names[h], type_names[t],
                                ip.pose.position.x, ip.pose.position.y, ip.pose.position.z,
                                ip.value, ip.ready ? "true" : "false");
                    }
                }
            }
        } else {
            spdlog::get("illixr")->debug("[hand_interaction] no data on switchboard");
        }
    }

    auto now = time_point{std::chrono::duration<long, std::nano>{std::chrono::high_resolution_clock::now().time_since_epoch()}};
    current_pose.predict_target_time   = predicted_time;
    current_pose.predict_computed_time = now;

    uint64_t pose_id = ++counter_;

    // Store pose in history map so oxr_interface can correlate incoming
    // frames back to the original pose measurement via pose_id.
    {
        std::lock_guard<std::mutex> lock(pose_history_mutex_);
        pose_history_entry entry{};
        entry.pose           = current_head_pose_;
        entry.generated_time = now;
        entry.xr_time        = predicted_time;
        pose_history_[pose_id] = entry;

        // Prune oldest entries to keep the map bounded
        while (pose_history_.size() > MAX_POSE_HISTORY) {
            pose_history_.erase(pose_history_.begin());
        }
    }

    // ----------------------------------------------------------------
    // Populate time conversion fields so pose_relay on the server can
    // convert this XrTime into Monado time.
    //
    //   pose_xr_time_ns             = predictedDisplayTime (headset XrTime)
    //   xr_to_monotonic_offset_ns   = CLOCK_MONOTONIC_ns - XrTime
    //   monotonic_to_system_offset_ns = system_clock_ns - CLOCK_MONOTONIC_ns
    //   smoothed_clock_offset_ns    = server system_clock - headset system_clock
    //   smoothed_rtt_ns             = smoothed round trip time
    // ----------------------------------------------------------------

    // Read the latest network latency data if available
    auto latency_data = latency_reader_.get_ro_nullable();
    double smoothed_offset = 0.;
    double smoothed_rtt = 0.;
    if (latency_data != nullptr) {
        smoothed_offset = latency_data->smoothed_clock_offset_ms * 1'000'000.0;
        smoothed_rtt = latency_data->smoothed_rtt_ms * 1'000'000.0;
    }
    //spdlog::get("illixr")->debug("[oxr_relay] pushing pose {}", pose_id);
    combined_pose_writer_.put(std::make_shared<pose::combined_pose>(current_pose, current_hand_poses_, current_palm_poses_,
                                                                    current_hand_interactions_, pose_id,
                                                                    predicted_time, xr_to_monotonic_offset_ns_,
                                                                    monotonic_to_system_offset_ns_, smoothed_offset,
                                                                    smoothed_rtt));
}

bool oxr_relay::init_hand_interaction() {
    if (!hand_interaction_supported_) {
        spdlog::get("illixr")->debug("Hand interaction extension not supported, skipping initialization");
        return false;
    }

    //  Sub-action paths
    const char* hand_path_strings[2] = {"/user/hand/left", "/user/hand/right"};
    for (int h = 0; h < 2; h++) {
        OXR(xrStringToPath(instance_, hand_path_strings[h], &hand_subaction_paths_[h]))
    }
    XrPath both_subaction_paths[2] = {hand_subaction_paths_[0], hand_subaction_paths_[1]};

    //  Action set
    XrActionSetCreateInfo action_set_info = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strncpy(action_set_info.actionSetName,            "hand_interaction",  XR_MAX_ACTION_SET_NAME_SIZE);
    strncpy(action_set_info.localizedActionSetName,   "Hand Interaction",  XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
    action_set_info.priority = 0;
    OXR(xrCreateActionSet(instance_, &action_set_info, &hand_interaction_action_set_))

    //  Pose actions (one per interaction_pose_type, shared across both hands)
    const char* pose_action_names[pose::NUM_INTERACTION_POSES]      = {"aim_pose",  "grip_pose",  "pinch_pose",  "poke_pose"};
    const char* pose_action_localized[pose::NUM_INTERACTION_POSES]  = {"Aim Pose",  "Grip Pose",  "Pinch Pose",  "Poke Pose"};

    for (int i = 0; i < pose::NUM_INTERACTION_POSES; i++) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strncpy(ai.actionName,           pose_action_names[i],     XR_MAX_ACTION_NAME_SIZE);
        strncpy(ai.localizedActionName,  pose_action_localized[i], XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
        ai.actionType            = XR_ACTION_TYPE_POSE_INPUT;
        ai.countSubactionPaths   = 2;
        ai.subactionPaths        = both_subaction_paths;
        OXR(xrCreateAction(hand_interaction_action_set_, &ai, &interaction_pose_actions_[i]))
    }

    //  Float value actions (AIM=0, GRIP=1, PINCH=2; POKE has none)
    const char* value_action_names[3]     = {"aim_activate_value", "grasp_value",  "pinch_value"};
    const char* value_action_localized[3] = {"Aim Activate Value", "Grasp Value",  "Pinch Value"};

    for (int i = 0; i < 3; i++) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strncpy(ai.actionName,           value_action_names[i],     XR_MAX_ACTION_NAME_SIZE);
        strncpy(ai.localizedActionName,  value_action_localized[i], XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
        ai.actionType            = XR_ACTION_TYPE_FLOAT_INPUT;
        ai.countSubactionPaths   = 2;
        ai.subactionPaths        = both_subaction_paths;
        OXR(xrCreateAction(hand_interaction_action_set_, &ai, &interaction_value_actions_[i]))
    }

    //  Boolean ready actions (AIM=0, GRIP=1, PINCH=2)
    const char* ready_action_names[3]     = {"aim_activate_ready", "grasp_ready",  "pinch_ready"};
    const char* ready_action_localized[3] = {"Aim Activate Ready", "Grasp Ready",  "Pinch Ready"};

    for (int i = 0; i < 3; i++) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strncpy(ai.actionName,           ready_action_names[i],     XR_MAX_ACTION_NAME_SIZE);
        strncpy(ai.localizedActionName,  ready_action_localized[i], XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
        ai.actionType            = XR_ACTION_TYPE_BOOLEAN_INPUT;
        ai.countSubactionPaths   = 2;
        ai.subactionPaths        = both_subaction_paths;
        OXR(xrCreateAction(hand_interaction_action_set_, &ai, &interaction_ready_actions_[i]))
    }

    //  Palm pose action (XR_EXT_palm_pose)
    // Created in this same action set so it shares the single xrAttachSessionActionSets
    // call that is only allowed once per session.
    {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strncpy(ai.actionName,          "palm_pose",  XR_MAX_ACTION_NAME_SIZE);
        strncpy(ai.localizedActionName, "Palm Pose",  XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
        ai.actionType          = XR_ACTION_TYPE_POSE_INPUT;
        ai.countSubactionPaths = 2;
        ai.subactionPaths      = both_subaction_paths;
        OXR(xrCreateAction(hand_interaction_action_set_, &ai, &palm_pose_action_))
    }

    //  Suggest bindings
    // Each profile gets its own xrSuggestInteractionProfileBindings call because
    // the spec requires a separate call per profile.  We suggest:
    //   • /interaction_profiles/ext/hand_interaction_ext  — bare-hand interaction poses
    //   • /interaction_profiles/khr/simple_controller     — generic fallback for any controller
    //
    // Keeping to these two vendor-neutral profiles means the code makes no assumptions
    // about the physical hardware.  The runtime will automatically select whichever
    // profile matches the active device.
    //
    // Palm pose (/input/palm_ext/pose from XR_EXT_palm_pose) is bound ONLY on the
    // simple_controller profile.  It is NOT a valid binding path on hand_interaction_ext
    // — that profile only exposes aim/grip/pinch/poke poses and their gesture scalars.
    // When bare hands are active the runtime will derive the palm pose from the hand
    // skeleton internally; when a controller is active it will be read from the
    // simple_controller binding.

    const char* pose_suffixes[pose::NUM_INTERACTION_POSES] = {
            "/input/aim/pose",
            "/input/grip/pose",
            "/input/pinch_ext/pose",
            "/input/poke_ext/pose"
    };
    const char* value_suffixes[3] = {
            "/input/aim_activate_ext/value",
            "/input/grasp_ext/value",
            "/input/pinch_ext/value"
    };
    const char* ready_suffixes[3] = {
            "/input/aim_activate_ext/ready_ext",
            "/input/grasp_ext/ready_ext",
            "/input/pinch_ext/ready_ext"
    };

    //  Profile 1: XR_EXT_hand_interaction
    {
        XrPath profile_path;
        OXR(xrStringToPath(instance_, "/interaction_profiles/ext/hand_interaction_ext", &profile_path))

        std::vector<XrActionSuggestedBinding> bindings;
        for (auto & hand_path_string : hand_path_strings) {
            for (int i = 0; i < pose::NUM_INTERACTION_POSES; i++) {
                XrPath path;
                std::string s = std::string(hand_path_string) + pose_suffixes[i];
                OXR(xrStringToPath(instance_, s.c_str(), &path))
                bindings.push_back({interaction_pose_actions_[i], path});
            }
            for (int i = 0; i < 3; i++) {
                XrPath path;
                std::string s = std::string(hand_path_string) + value_suffixes[i];
                OXR(xrStringToPath(instance_, s.c_str(), &path))
                bindings.push_back({interaction_value_actions_[i], path});
            }
            for (int i = 0; i < 3; i++) {
                XrPath path;
                std::string s = std::string(hand_path_string) + ready_suffixes[i];
                OXR(xrStringToPath(instance_, s.c_str(), &path))
                bindings.push_back({interaction_ready_actions_[i], path});
            }
            // NOTE: palm_pose_action_ is NOT bound here.  /input/palm_ext/pose is
            // defined by XR_EXT_palm_pose and is only valid on controller interaction
            // profiles, not on hand_interaction_ext.  It is bound in Profile 2 below.
        }

        XrInteractionProfileSuggestedBinding suggested = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile     = profile_path;
        suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggested.suggestedBindings      = bindings.data();
        OXR(xrSuggestInteractionProfileBindings(instance_, &suggested))
        spdlog::get("illixr")->info("Suggested {} bindings for hand_interaction_ext profile", bindings.size());
    }

    //  Profile 2: KHR simple controller (generic fallback)
    {
        XrPath profile_path;
        OXR(xrStringToPath(instance_, "/interaction_profiles/khr/simple_controller", &profile_path))

        std::vector<XrActionSuggestedBinding> bindings;
        for (auto & hand_path_string : hand_path_strings) {
            // Only aim and grip are defined on the simple_controller profile
            for (int i : {(int)pose::AIM, (int)pose::GRIP}) {
                XrPath path;
                std::string s = std::string(hand_path_string) + pose_suffixes[i];
                OXR(xrStringToPath(instance_, s.c_str(), &path))
                bindings.push_back({interaction_pose_actions_[i], path});
            }
            {
                XrPath path;
                std::string s = std::string(hand_path_string) + "/input/palm_ext/pose";
                OXR(xrStringToPath(instance_, s.c_str(), &path))
                bindings.push_back({palm_pose_action_, path});
            }
        }

        XrInteractionProfileSuggestedBinding suggested = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile     = profile_path;
        suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggested.suggestedBindings      = bindings.data();
        XrResult r = xrSuggestInteractionProfileBindings(instance_, &suggested);
        if (XR_FAILED(r)) {
            spdlog::get("illixr")->warn("Could not suggest simple_controller bindings ({})",
                                        static_cast<int>(r));
        } else {
            spdlog::get("illixr")->info("Suggested {} bindings for simple_controller profile", bindings.size());
        }
    }

    //  Attach action set to session
    XrSessionActionSetsAttachInfo attach_info = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach_info.countActionSets = 1;
    attach_info.actionSets      = &hand_interaction_action_set_;
    XrResult attach_result = xrAttachSessionActionSets(session_, &attach_info);
    if (XR_FAILED(attach_result)) {
        spdlog::get("illixr")->error("Failed to attach hand interaction action set: {}",
                                     static_cast<int>(attach_result));
        hand_interaction_supported_ = false;
        return false;
    }

    //  Create per-hand action spaces
    XrPosef identity = {};
    identity.orientation.w = 1.0f;

    for (int h = 0; h < 2; h++) {
        for (int i = 0; i < pose::NUM_INTERACTION_POSES; i++) {
            XrActionSpaceCreateInfo space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
            space_info.action              = interaction_pose_actions_[i];
            space_info.subactionPath       = hand_subaction_paths_[h];
            space_info.poseInActionSpace   = identity;
            OXR(xrCreateActionSpace(session_, &space_info, &interaction_pose_spaces_[h][i]))
        }

        // Palm pose action space — one per hand
        XrActionSpaceCreateInfo palm_space_info = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        palm_space_info.action            = palm_pose_action_;
        palm_space_info.subactionPath     = hand_subaction_paths_[h];
        palm_space_info.poseInActionSpace = identity;
        OXR(xrCreateActionSpace(session_, &palm_space_info, &palm_pose_spaces_[h]))
    }

    spdlog::get("illixr")->info("Hand interaction action set and action spaces created");
    return true;
}

void oxr_relay::destroy_hand_interaction() {
    for (int h = 0; h < 2; h++) {
        for (int p = 0; p < pose::NUM_INTERACTION_POSES; p++) {
            if (interaction_pose_spaces_[h][p] != XR_NULL_HANDLE) {
                xrDestroySpace(interaction_pose_spaces_[h][p]);
                interaction_pose_spaces_[h][p] = XR_NULL_HANDLE;
            }
        }
        if (palm_pose_spaces_[h] != XR_NULL_HANDLE) {
            xrDestroySpace(palm_pose_spaces_[h]);
            palm_pose_spaces_[h] = XR_NULL_HANDLE;
        }
    }
    if (hand_interaction_action_set_ != XR_NULL_HANDLE) {
        xrDestroyActionSet(hand_interaction_action_set_);
        hand_interaction_action_set_ = XR_NULL_HANDLE;
    }
}

void oxr_relay::update_hand_interaction(XrTime predicted_time) {
    if (!hand_interaction_supported_ || hand_interaction_action_set_ == XR_NULL_HANDLE) {
        spdlog::get("illixr")->debug("NO handle");
        return;
    }

    //  Sync actions
    XrActiveActionSet active_set = {};
    active_set.actionSet     = hand_interaction_action_set_;
    active_set.subactionPath = XR_NULL_PATH; // all sub-action paths

    XrActionsSyncInfo sync_info = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets      = &active_set;

    auto t0 = std::chrono::steady_clock::now();
    XrResult sync_result = xrSyncActions(session_, &sync_info);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
    if (ms > TIME_CUTOFF)
        spdlog::get("illixr")->warn("[oxr_timing] xrSyncActions took {:.2f}ms", ms);
    if (sync_result == XR_SESSION_NOT_FOCUSED) {
        spdlog::get("illixr")->debug("update_hand_interaction: session not focused, skipping");
        return;
    }
    if (XR_FAILED(sync_result)) {
        spdlog::get("illixr")->warn("xrSyncActions failed: {}", static_cast<int>(sync_result));
        return;
    }

    //  Collect poses
    current_hand_interactions_.sensor_time = clock_->now();

    const pose::hand hand_enum[2] = {pose::LEFT, pose::RIGHT};
    const char*                       type_names[] = {"AIM", "GRIP", "PINCH", "POKE"};
    const char*                       hand_names[] = {"LEFT", "RIGHT"};

    for (int h = 0; h < 2; h++) {
        pose::hand_interaction_poses& hand_poses = current_hand_interactions_.hands[hand_enum[h]];

        for (int p = 0; p < pose::NUM_INTERACTION_POSES; p++) {
            //spdlog::get("illixr")->debug("Checking {} {}", hand_names[h], type_names[p]);
            auto pose_type = static_cast<pose::interaction_pose_type>(p);
            pose::hand_interaction_pose& interaction_pose = hand_poses[pose_type];
            float f_state = 0.;
            bool b_state = false;

            // Check if this pose action is active for this hand
            XrActionStateGetInfo get_info = {XR_TYPE_ACTION_STATE_GET_INFO};
            get_info.action        = interaction_pose_actions_[p];
            get_info.subactionPath = hand_subaction_paths_[h];

            XrActionStatePose pose_state = {XR_TYPE_ACTION_STATE_POSE};
            if (XR_FAILED(xrGetActionStatePose(session_, &get_info, &pose_state))) {
                spdlog::get("illixr")->debug("   {} {} get action state failed", hand_names[h], type_names[p]);
                continue;
            }

            if (!pose_state.isActive) {
                spdlog::get("illixr")->debug("  Not active");
                continue;
            }

            // Fetch value/ready FIRST for AIM/GRIP/PINCH — for PINCH this gates
            // whether the space will have valid location flags.
            //bool gesture_active = true;  // assume true for POKE which has no value/ready
            if (p < 3) {                 // AIM=0, GRIP=1, PINCH=2
                XrActionStateGetInfo float_info = {XR_TYPE_ACTION_STATE_GET_INFO};
                float_info.action        = interaction_value_actions_[p];
                float_info.subactionPath = hand_subaction_paths_[h];

                XrActionStateFloat float_state = {XR_TYPE_ACTION_STATE_FLOAT};
                if (XR_SUCCEEDED(xrGetActionStateFloat(session_, &float_info, &float_state)) &&
                    float_state.isActive) {
                    f_state = float_state.currentState;
                }

                XrActionStateGetInfo bool_info = {XR_TYPE_ACTION_STATE_GET_INFO};
                bool_info.action        = interaction_ready_actions_[p];
                bool_info.subactionPath = hand_subaction_paths_[h];

                XrActionStateBoolean bool_state = {XR_TYPE_ACTION_STATE_BOOLEAN};
                if (XR_SUCCEEDED(xrGetActionStateBoolean(session_, &bool_info, &bool_state)) &&
                    bool_state.isActive) {
                    b_state = (bool_state.currentState == XR_TRUE);
                }

                // For PINCH specifically: the pose space is only valid when the
                // gesture is occurring.  Querying it with zero value will always
                // yield locationFlags=0, which is correct per spec but useless.
                if (pose_type == pose::PINCH && f_state <= 0.0f) {
                    //spdlog::get("illixr")->debug("  {} PINCH value={:.3f}, skipping space locate",
                    //                             hand_names[h], interaction_pose.value);
                    interaction_pose.value = f_state;
                    interaction_pose.ready = b_state;
                    continue;
                } else if (pose_type == pose::PINCH) {
                    //spdlog::get("illixr")->debug("  {} PINCH value={:.3f}", hand_names[h], interaction_pose.value);
                }
            }


            //spdlog::get("illixr")->debug("  {} is active", type_names[p]);
            // Locate the action space relative to the local tracking space
            XrSpaceLocation space_loc = {XR_TYPE_SPACE_LOCATION};
            auto t3 = std::chrono::steady_clock::now();
            XrResult locate_result = xrLocateSpace(
                    interaction_pose_spaces_[h][p],
                    local_space_,
                    predicted_time,
                    &space_loc
            );
            auto t4 = std::chrono::steady_clock::now();
            auto ms1 = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() / 1000.0;
            if (ms1 > TIME_CUTOFF)
                spdlog::get("illixr")->warn("[oxr_timing] xrLocateSpace (interaction {}/{}) took {:.2f}ms",
                                            hand_names[h], type_names[p], ms1);
            const bool pos_valid = (space_loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)    != 0u;
            const bool ori_valid = (space_loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0u;
            if (XR_SUCCEEDED(locate_result) && pos_valid && ori_valid) {
                //spdlog::get("illixr")->debug("  {} {} succeeded", hand_names[h], type_names[p]);
                interaction_pose.update(space_loc, f_state, b_state, predicted_time);
            } else {
                //spdlog::get("illixr")->debug("  {} {} locate failed: result={} flags={}",
                //                             hand_names[h], type_names[p],
                //                             static_cast<int>(locate_result),
                //                             static_cast<int>(space_loc.locationFlags));
            }
        }
    }

    if (current_hand_interactions_.is_valid()) {
        // Throttle logging to once every 300 frames
        static uint64_t hi_log_counter = 0;
        if ((++hi_log_counter % 300) == 1) {
            //spdlog::get("illixr")->debug("update_hand_interaction: logging");

            const pose::hand                  hand_enums[] = {pose::LEFT, pose::RIGHT};
            const pose::interaction_pose_type type_enums[] = {
                    pose::AIM, pose::GRIP, pose::PINCH, pose::POKE
            };
            for (auto hand : hand_enums) {
                const auto& hip = current_hand_interactions_.hands.at(hand);
                for (auto type_enum : type_enums) {
                    const auto& ip = hip.at(type_enum);
                    if (!ip.valid()) {
                        //spdlog::get("illixr")->debug("[hand_interaction] {} {} not valid", hand_names[h], type_names[t]);
                        continue;
                    }
                    //if (type_enums[t] == pose::POKE) {
                        //spdlog::get("illixr")->debug(
                        //    "[hand_interactionX] {} {} | pos=({:.3f},{:.3f},{:.3f})",
                        //    hand_names[h], type_names[t],
                        //    ip.position.x(), ip.position.y(), ip.position.z());
                    //} else {
                        //spdlog::get("illixr")->debug(
                        //    "[hand_interactionX] {} {} | pos=({:.3f},{:.3f},{:.3f}) | value={:.3f} ready={}",
                        //    hand_names[h], type_names[t],
                        //    ip.position.x(), ip.position.y(), ip.position.z(),
                        //    ip.value, ip.ready ? "true" : "false");
                    //}
                }
            }
        }
    }

    //  Palm poses via XR_EXT_palm_pose
    // Located here (after xrSyncActions) so the action-system state is current.
    // The palm pose action was bound against multiple interaction profiles in
    // init_hand_interaction(), so the runtime resolves it from whichever input
    // source is active (bare hands or controllers).
    current_palm_poses_.sensor_time = clock_->now();

    for (int h = 0; h < 2; h++) {
        if (palm_pose_spaces_[h] == XR_NULL_HANDLE) continue;

        XrActionStateGetInfo get_info = {XR_TYPE_ACTION_STATE_GET_INFO};
        get_info.action        = palm_pose_action_;
        get_info.subactionPath = hand_subaction_paths_[h];

        XrActionStatePose pose_state = {XR_TYPE_ACTION_STATE_POSE};
        if (XR_FAILED(xrGetActionStatePose(session_, &get_info, &pose_state)) ||
            !pose_state.isActive) {
            current_palm_poses_.hands[(h == 0) ? pose::LEFT : pose::RIGHT].relation_flags = 0;
            continue;
        }

        XrSpaceVelocity velocity = {XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
        location.next = &velocity;
        XrResult r = xrLocateSpace(palm_pose_spaces_[h], local_space_, predicted_time, &location);

        const bool pos_valid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)    != 0u;
        const bool ori_valid = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0u;

        if (XR_SUCCEEDED(r) && pos_valid && ori_valid) {
            pose::hand palm_hand = (h == 0) ? pose::LEFT : pose::RIGHT;
            current_palm_poses_.hands[palm_hand].update(location, velocity);
        } else {
            current_palm_poses_.hands[(h == 0) ? pose::LEFT : pose::RIGHT].relation_flags = 0;
        }
    }

    if (current_palm_poses_.is_valid()) {
        // Throttle logging to once every 300 frames
        static uint64_t pp_log_counter = 0;
        if ((++pp_log_counter % log_interval) == 1) {
            const pose::hand hand_enums[] = {pose::LEFT, pose::RIGHT};
            for (int h = 0; h < 2; h++) {
                const auto& pd = current_palm_poses_.hands.at(hand_enums[h]);
                if (!pd.is_valid()) {
                    spdlog::get("illixr")->debug("[palm_pose] {} not valid", hand_names[h]);
                    continue;
                }
            }
        }
    }
}

void oxr_relay::calibrate_time_offsets() {
    // On Quest/Android, XrTime is nanoseconds since device boot,
    // equivalent to CLOCK_BOOTTIME. During an active XR session the
    // device does not suspend, so CLOCK_BOOTTIME == CLOCK_MONOTONIC.
    // We therefore approximate xr_to_monotonic_offset as zero and
    // compute monotonic_to_system_offset directly.
    //
    // Verification: sample XrTime from predicted_time_ alongside
    // CLOCK_MONOTONIC to confirm they are in the same timebase.
    xr_to_monotonic_offset_ns_ = 0;

    // Compute monotonic_to_system_offset_ns_:
    //   system_clock_ns = CLOCK_MONOTONIC_ns + monotonic_to_system_offset_ns_
    constexpr int NUM_SAMPLES = 20;
    int64_t best_offset = 0;
    int64_t best_rtt    = INT64_MAX;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        auto before_sys = std::chrono::system_clock::now();
        struct timespec ts_mono;
        clock_gettime(CLOCK_MONOTONIC, &ts_mono);
        auto after_sys = std::chrono::system_clock::now();

        int64_t before_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                before_sys.time_since_epoch()).count();
        int64_t after_ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                after_sys.time_since_epoch()).count();
        int64_t rtt       = after_ns - before_ns;
        int64_t mid_ns    = before_ns + rtt / 2;
        int64_t mono_ns   = ts_mono.tv_sec * 1'000'000'000LL + ts_mono.tv_nsec;
        int64_t offset    = mid_ns - mono_ns;

        if (rtt < best_rtt) {
            best_rtt    = rtt;
            best_offset = offset;
        }
    }

    monotonic_to_system_offset_ns_ = best_offset;
    time_offsets_calibrated_       = true;

    /*spdlog::get("illixr")->info(
            "oxr_relay: calibrate_time_offsets: "
            "xr_to_monotonic={:.6f}s (assumed 0, Quest has no conversion extension) "
            "monotonic_to_system={:.6f}s",
            xr_to_monotonic_offset_ns_     / 1'000'000'000.0,
            monotonic_to_system_offset_ns_ / 1'000'000'000.0);
            */
}

bool oxr_relay::get_pose_history(uint64_t id, pose_history_entry& out_entry) const {
    std::lock_guard<std::mutex> lock(pose_history_mutex_);
    auto it = pose_history_.find(id);
    if (it == pose_history_.end()) {
        return false;
    }
    out_entry = it->second;
    return true;
}