#pragma once
#ifndef USING_OPENXR
#    define USING_OPENXR
#endif

#define DOUBLE_INCLUDE
#include "illixr/data_format/latency_data.hpp"
#include "illixr/data_format/openxr_view_frame.hpp"
#include "illixr/data_format/poses/combined_pose.hpp"
#include "illixr/data_format/quest_controller.hpp"
#include "illixr/data_format/serialization/openxr_view_frame.hpp"
#include "illixr/data_format/serialization/quest_controller.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/threadloop.hpp"
#undef DOUBLE_INCLUDE

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <openxr/openxr.h>

#define OXR_CheckErrors(cmd, pfunc)                                                                                     \
    do {                                                                                                                \
        XrResult res = cmd;                                                                                             \
        if (XR_FAILED(res)) {                                                                                           \
            spdlog::get("illixr")->error("OpenXR error {} at {}:{} from {}", static_cast<int>(res), __FILE__, __LINE__, \
                                         #pfunc);                                                                       \
            throw std::runtime_error("Call failed");                                                                    \
        }                                                                                                               \
    } while (0)
#define OXR(func) OXR_CheckErrors(func, #func);

namespace ILLIXR {

class oxr_interface;

/// Entry in the headset pose history map, used by oxr_interface to
/// correlate received frames back to the original pose measurement.
struct pose_history_entry {
    data_format::pose::xrt_space_relation pose;           ///< Head pose
    time_point                            generated_time; ///< Wall-clock time the pose was generated
    XrTime                                xr_time;        ///< predictedDisplayTime this pose was located for
};

class oxr_relay : public threadloop {
public:
    oxr_relay(const std::string& name, phonebook* pb);

    ~oxr_relay() override;

    /// Look up a pose history entry by combined_pose id.
    /// Returns true and populates out_entry if found, false otherwise.
    /// Thread-safe.
    bool get_pose_history(uint64_t id, pose_history_entry& out_entry) const;

    /** Publish a coherent Quest controller + stereo-view sample for Boba. */
    void publish_boba_input(XrTime predicted_time, XrDuration predicted_period, XrBool32 should_render,
                            XrViewStateFlags view_flags, const XrView views[2], const XrViewConfigurationView view_configs[2]);

protected:
    skip_option _p_should_skip() override;

    void _p_one_iteration() override;

private:
    friend oxr_interface;

    void initialize(XrInstance instance, XrSession session, XrSpace local, XrSpace view);

    void destroy();

    void update_time(XrTime time) {
        predicted_time_.store(time);
    }

    // Pose tracking
    XrResult get_head_pose(XrTime time, data_format::pose::head_pose_type* out_pose);
    // ==================== Time offset calibration ====================

    /**
     * @brief Compute the offset between XrTime and CLOCK_MONOTONIC once
     *        at session initialization using xrConvertTimespecTimeToTimeKHR.
     *
     * Populates xr_to_monotonic_offset_ns_ such that:
     *   CLOCK_MONOTONIC_ns = XrTime + xr_to_monotonic_offset_ns_
     *
     * Also computes monotonic_to_system_offset_ns_ such that:
     *   system_clock_ns = CLOCK_MONOTONIC_ns + monotonic_to_system_offset_ns_
     *
     * Both offsets are sent in every combined_pose so the server can
     * convert headset XrTime into Monado time.
     */
    void calibrate_time_offsets();
    // ==================== Hand Tracking ====================

    /**
     * @brief Initialize the OpenXR hand tracking extension.
     *
     * Queries for XR_EXT_hand_tracking support and creates hand trackers
     * for both left and right hands.
     *
     * @return true if hand tracking was successfully initialized
     */
    bool init_hand_tracking();

    /**
     * @brief Clean up hand tracking resources.
     */
    void destroy_hand_tracking();

    /**
     * @brief Update hand tracking and palm pose data for the current frame.
     *
     * Locates hand joints at the predicted display time, publishes joint tracking
     * data to "hand_tracking" and extracts palm poses to publish on "palm_poses".
     *
     * @param predicted_time The predicted display time for this frame
     */
    void update_hand_tracking(XrTime predicted_time);

    // ==================== Hand Interaction ====================

    /**
     * @brief Initialize the XR_EXT_hand_interaction action set.
     *
     * Creates actions for aim / grip / pinch / poke poses (and their associated
     * gesture-strength values and readiness flags), suggests bindings against the
     * hand-interaction interaction profile, attaches the action set to the session,
     * and creates the per-hand action spaces used each frame.
     *
     * Must be called after create_session() and, if hand tracking is used,
     * after init_hand_tracking().
     *
     * @return true if the action set was fully initialized
     */
    bool init_hand_interaction();

    /**
     * @brief Destroy action spaces and the hand interaction action set.
     */
    void destroy_hand_interaction();

    /**
     * @brief Sync actions and publish hand interaction poses for the current frame.
     *
     * Calls xrSyncActions, then for every hand × interaction-pose-type combination:
     * locates the action space, and (for AIM / GRIP / PINCH) reads the
     * gesture-strength value and readiness boolean.  Publishes a
     * hand_interaction_poses_pair to "hand_interaction" when at least one pose
     * is valid.
     *
     * @param predicted_time The predicted display time for this frame
     */
    void update_hand_interaction(XrTime predicted_time);

    bool        create_controller_actions();
    bool        suggest_controller_bindings();
    bool        sync_actions();
    void        refresh_controller_profiles();
    bool        query_controller_hand(std::size_t hand_index, XrTime sample_time, data_format::quest_hand_controller* hand);
    bool        query_controller_pose(XrAction action, XrSpace space, XrPath hand_path, XrTime sample_time,
                                      data_format::quest_controller_pose* pose);
    bool        query_controller_boolean(XrAction action, XrPath hand_path, data_format::quest_controller_button* button);
    bool        query_controller_float(XrAction action, XrPath hand_path, float threshold,
                                       data_format::quest_controller_button* button);
    bool        query_controller_axis(XrAction action, XrPath hand_path, data_format::quest_controller_axis2d* axis);
    static void merge_controller_button(data_format::quest_controller_button*       destination,
                                        const data_format::quest_controller_button& source);

    /**
     * @brief Send the latest pose to the server.
     *
     * Populates a combined_pose with the head pose, hand tracking data,
     * and all time conversion fields needed by pose_relay on the server
     * to convert the headset XrTime into Monado time.
     *
     * @param predicted_time The predictedDisplayTime from xrWaitFrame
     */
    void push_poses(XrTime predicted_time);

    // ================== Member Variables ====================
    const std::shared_ptr<switchboard>    switchboard_;
    const std::shared_ptr<relative_clock> clock_;

    /**
     * @brief Combined pose + hand tracking writer for network transmission.
     *
     * Sends combined_pose to the offload rendering server, which contains
     * the head pose, hand tracking data, and time conversion fields.
     */
    switchboard::network_writer<data_format::pose::combined_pose>    combined_pose_writer_;
    switchboard::network_writer<data_format::quest_controller_input> quest_controller_writer_;
    switchboard::network_writer<data_format::openxr_view_frame>      openxr_view_writer_;

    /// Reader for network latency results published by network_latency_pong_rx.
    /// Used to populate smoothed_clock_offset_ns and smoothed_rtt_ns in
    /// each combined_pose so the server has up-to-date network timing data.
    switchboard::reader<data_format::network_latency_result> latency_reader_;

    data_format::pose::head_pose_type              current_head_pose_;
    data_format::pose::hand_joint_poses_pair       current_hand_poses_;
    data_format::pose::hand_interaction_poses_pair current_hand_interactions_;
    data_format::pose::palm_poses_pair             current_palm_poses_;

    XrInstance          instance_       = XR_NULL_HANDLE;
    XrSession           session_        = XR_NULL_HANDLE;
    XrSpace             local_space_    = XR_NULL_HANDLE;
    XrSpace             view_space_     = XR_NULL_HANDLE;
    std::atomic<XrTime> predicted_time_ = 0;

    /// Left hand tracker handle
    XrHandTrackerEXT left_hand_tracker_{XR_NULL_HANDLE};

    /// Right hand tracker handle
    XrHandTrackerEXT right_hand_tracker_{XR_NULL_HANDLE};

    /// Function pointer for xrCreateHandTrackerEXT
    PFN_xrCreateHandTrackerEXT xr_create_hand_tracker_{nullptr};

    /// Function pointer for xrDestroyHandTrackerEXT
    PFN_xrDestroyHandTrackerEXT xr_destroy_hand_tracker_{nullptr};

    /// Function pointer for xrLocateHandJointsEXT
    PFN_xrLocateHandJointsEXT xr_locate_hand_joints_{nullptr};

    // ==================== Hand Interaction State ====================

    /// Whether XR_EXT_hand_interaction extension is supported and initialized
    bool hand_interaction_supported_{false};

    // ==================== Hand Tracking State ====================

    /// Whether XR_EXT_hand_tracking extension is supported
    bool hand_tracking_supported_{false};

    /// Single action set that owns all hand-interaction actions AND the palm pose action.
    /// Combining them avoids needing a second xrAttachSessionActionSets call, which is
    /// only permitted once per session.
    XrActionSet hand_interaction_action_set_{XR_NULL_HANDLE};

    /**
     * @brief sub-action paths for left (index 0) and right (index 1) hands.
     *
     * Used when creating actions with sub-action paths and when querying
     * per-hand action state.
     */
    XrPath hand_subaction_paths_[2]{XR_NULL_PATH, XR_NULL_PATH};

    /**
     * @brief One pose action per interaction_pose_type (AIM=0 … POKE=3).
     *
     * Each action was created with both left and right sub-action paths so that
     * a single action covers both hands; the per-hand state is retrieved by
     * passing the appropriate sub-action path to xrGetActionStatePose /
     * xrLocateSpace.
     */
    XrAction interaction_pose_actions_[data_format::pose::NUM_INTERACTION_POSES]{};

    /**
     * @brief Action spaces indexed as [hand][interaction_pose_type].
     *
     * Created in init_hand_interaction() via xrCreateActionSpace with the
     * corresponding pose action and sub-action path.  Used every frame inside
     * update_hand_interaction() to locate each pose.
     */
    XrSpace interaction_pose_spaces_[2][data_format::pose::NUM_INTERACTION_POSES]{};

    /**
     * @brief Float (gesture-strength) actions for AIM (0), GRIP (1), and PINCH (2).
     *
     * POKE has no value binding in XR_EXT_hand_interaction and is therefore not
     * represented here.
     */
    XrAction interaction_value_actions_[3]{};

    /**
     * @brief Boolean (gesture-ready) actions for AIM (0), GRIP (1), and PINCH (2).
     *
     * As with value actions, POKE has no ready binding.
     */
    XrAction interaction_ready_actions_[3]{};

    /**
     * @brief Palm pose action from XR_EXT_palm_pose, shared across both hands via
     * sub-action paths.
     *
     * Bindings are suggested against every relevant interaction profile so that a
     * palm pose is available regardless of whether the user is using bare hands or
     * holding controllers.
     */
    XrAction palm_pose_action_{XR_NULL_HANDLE};

    /**
     * @brief Action spaces for the palm pose, indexed by hand (LEFT=0, RIGHT=1).
     *
     * Created in init_hand_interaction() once the action and session are ready.
     */
    XrSpace palm_pose_spaces_[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};

    // Quest Touch inputs share hand_interaction_action_set_ so the session has
    // exactly one xrAttachSessionActionSets call.
    XrAction                                             controller_trigger_click_action_{XR_NULL_HANDLE};
    XrAction                                             controller_trigger_value_action_{XR_NULL_HANDLE};
    XrAction                                             controller_squeeze_value_action_{XR_NULL_HANDLE};
    XrAction                                             controller_primary_click_action_{XR_NULL_HANDLE};
    XrAction                                             controller_secondary_click_action_{XR_NULL_HANDLE};
    XrAction                                             controller_thumbstick_click_action_{XR_NULL_HANDLE};
    XrAction                                             controller_thumbstick_axis_action_{XR_NULL_HANDLE};
    std::array<data_format::quest_controller_profile, 2> controller_profiles_{data_format::quest_controller_profile::none,
                                                                              data_format::quest_controller_profile::none};
    bool                                                 controller_actions_initialized_{false};
    std::mutex                                           actions_mutex_;
    std::atomic<std::uint64_t>                           boba_input_sequence_{0};

    bool                                  initialized_{false};
    bool                                  xr_time_verified_{false};
    bool                                  time_offsets_calibrated_{false};
    std::chrono::steady_clock::time_point last_offset_calibration_{};
    XrTime                                monotonic_to_system_offset_ns_{0};
    XrTime                                xr_to_monotonic_offset_ns_{0};
    uint64_t                              counter_{0};
    data_format::pose::head_pose_type     last_pose_{};

    // Map from combined_pose id to the pose that was sent with that id.
    /// Bounded to 240 entries (~2 seconds at 120 Hz) to avoid unbounded growth.
    /// Accessible by oxr_interface for frame correlation logging.
    std::map<uint64_t, pose_history_entry> pose_history_;
    mutable std::mutex                     pose_history_mutex_;
    static constexpr size_t                MAX_POSE_HISTORY = 240;
};

} // namespace ILLIXR
