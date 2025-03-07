# ILLIXR plugins

This page details the structure of ILLIXR's [_plugins_][41] and how they interact with each other.


<div id="plugin-audio-pipeline"></div>
-   [`audio_pipeline`][8]:
    Launches a thread for [binaural][19] recording and one for binaural playback.
    Audio output is not yet routed to the system's speakers or microphone,
    but the plugin's compute workload is still representative of a real system.
    By default, this plugin is enabled (see `native` [_configuration_][40]).

    Topic details:

    -   *Calls* [`pose_prediction`][57]

<div id="plugins-debugview"></div>
-   [`debugview`][7]:
    Renders incoming [_frames_][34] from the graphics pipeline for debugging live executions of the application.

    Topic details:

    -   *Calls* [`pose_prediction`][57]
    -   Asynchronously *reads* [`fast_pose_type`][60] from `imu_raw` topic. ([_IMU_][36] biases are unused).
    -   Asynchronously *reads* [`pose_type`][61] from `slow_pose` topic.
    -   Asynchronously *reads* [`rgb_depth_type`][62] from `rgb_depth` topic.
    -   Asynchronously *reads* buffered [`binocular_cam_type`][63] from `cam` topic.

<div id="plugins-depthai"></div>
-   [`depthai`][28]:
    INFO NEEDED

    Topic details:

    -  *Publishes* [`imu_type`][64] to `imu` topic
    -  *Publishes* [`binocular_cam_type`][63] to `cam` topic`
    -  *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic

<div id="plugins-gldemo"></div>
-   [`gldemo`][5]:
    Renders a static scene (into left and right [_eye buffers_][34]) given the [_pose_][37]
    from [`pose_prediction`][57].

    Topic details:

    -   *Calls* [`pose_prediction`][57]
    -   *Publishes* `rendered_frame` to `eyebuffer` topic.
    -   *Publishes* `image_handle` to `image_handle` topic.
    -   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

<div id="plugins-ground-truth-slam"></div>
-   [`ground_truth_slam`][3]:
    Reads the [_ground truth_][34] from the same dataset as the `offline_imu` plugin.
    Ground truth data can be compared against the measurements from `offline_imu` for accuracy.
    Timing information is taken from the `offline_imu` measurements/data.

    Topic details:

    -   *Publishes* [`pose_type`][61] to `true_pose` topic.
    -   *Publishes* `Eigen::Vector3f` to `ground_truth_offset` topic.
    -   Asynchronously *reads* [`imu_type`][64] from `imu` topic.

<div id="plugins-gtsam-integrator"></div>
-   [`gtsam_integrator`][12]:
    Integrates over all [_IMU_][36] samples since the last published [_SLAM_][39] pose to provide a
    [_fast pose_][37] every time a new IMU sample arrives using the GTSAM library ([upstream][11]).

    Topic details:

    -   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.
    -   Synchronously *reads* [`imu_type`][64] from `imu` topic.
    -   Asynchronously *reads* [`imu_integrator_input`][66] to `imu_integrator_input` topic.

<div id="plugins-hand-tracking"></div>
-   [`hand_tracking`][45]:
    Detects and identifies hands in an image, CPU based calculations. The output from this plugin can be used to track hand movements and recognize hand gestures.

    Topic details:

    -   Synchronously *reads* one of [`monocular_cam_type`][67] from `webcam` topic, [`binocular_cam_type`][63] from `cam` topic, or [`cam_type_zed`][70] from `cam_zed` topic. This is selectable at run time via an environment variable.
    -   Asynchronously *reads* [`camera_data`][68] from `cam_data` topic, only once as values are static
    -   If reading from `webcam`
        - Asynchronously *reads* [`pose_type`][61] from `pose` topic
        - Asynchronously *reads* one of [`depth_type`][69] from `depth` topic or [`rgb_depth_type`][62] from `rgb_depth` topic, depending on which is available
    -   If reading from `cam`
        - Asynchronously *reads* [`pose_type`][61] from `pose` topic
        - Asynchronously *reads* one of [`depth_type`][69] from `depth` topic or [`rgb_depth_type`][62] from `rgb_depth` topic, if either is available, but not required
    -   If reading from `cam_zed`, no additional data are required.
    -   *Publishes* [`ht_frame`][71] to `ht` topic.

<div id="plugins-hand-tracking-gpu"></div>
-   [`hand_tracking_gpu`][45]:
    Detects and identifies hands in an image, GPU based calculations. The output from this plugin can be used to track hand movements and recognize hand gestures. This plugin is currently experimental.

    Topic details:

    -   Synchronously *reads* one of [`monocular_cam_type`][67] from `webcam` topic, [`binocular_cam_type`][63] from `cam` topic, or [`cam_type_zed`][70] from `cam_zed` topic. This is selectable at run time via an environment variable.
    -   Asynchronously *reads* [`camera_data`][68] from `cam_data` topic, only once as values are static
    -   If reading from `webcam`
        - Asynchronously *reads* [`pose_type`][61] from `pose` topic
        - Asynchronously *reads* one of [`depth_type`][69] from `depth` topic or [`rgb_depth_type`][62] from `rgb_depth` topic, depending on which is available
    -   If reading from `cam`
        - Asynchronously *reads* [`pose_type`][61] from `pose` topic
        - Asynchronously *reads* one of [`depth_type`][69] from `depth` topic or [`rgb_depth_type`][62] from `rgb_depth` topic, if either is available, but not required
    -   If reading from `cam_zed`, no additional data are required.
    -   *Publishes* [`ht_frame`][71] to `ht` topic.

<div id="plugins-hand-tracking-viewer"></div>
-   [`hand_tracking.viewer`][43]:
    Reads the output of the `hand_tracking` plugin and displays the results on the screen. This is most useful for debugging. The capabilities of this plugin will be merged into the `debugview` plugin in the future.

    Topic details:

    -   Synchronously *reads* [`ht_frame`][71] from `ht` topic.

<div id="plugins-lighthouse"></div>
-   [`lighthouse`][46]:
    INFO NEEDED

    Topic details:

    -   *Publishes* [`pose_type`][61] to `slow_pose` topic.
    -   *Publishes* [`fast_pose_type`][60] to `fast_pose` topic. 


<div id="plugins-native-renderer"></div>
-   [`native_renderer][47]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`pose_prediction`][57]
    -   *Calls* [`vulkan::display_provider`][30]
    -   *Calls* [`vulkan::timewarp`][30]
    -   *Calls* [`vulkan::app`][30]
    -   Synchronously *reads* `time_point` from `vsync_estimate` topic.

<div id="plugins-offline-cam"></div>
-   [`offline_cam`][2]:
    Reads camera images from files on disk, emulating real cameras on the [_headset_][38]
    (feeds the application input measurements with timing similar to an actual camera).

    Topic details:

    -   *Publishes* [`binocular_cam_type`][63] to `cam` topic.

<div id="plugins-offline-imu"></div>
-   [`offline_imu`][1]:
    Reads [_IMU_][36] data files on disk, emulating a real sensor on the [_headset_][38]
    (feeds the application input measurements with timing similar to an actual IMU).

    Topic details:

    -   *Publishes* [`imu_type`][64] to `imu` topic.

<div id="plugins-offload-data"></div>
-   [`offload_data`][21]:
    Writes [_frames_][34] and [_poses_][37] output from the [_asynchronous reprojection_][35] plugin to disk for analysis.

    Topic details:

    -   Synchronously *reads* `texture_pose` to `texture_pose` topic.

<div id="plugins-offload-rendering-client"></div>
-   [`offload_rendering_client`][48]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`vulkan::display_provider`][30]
    -   *Calls* [`pose_prediction`][32]
    -   Asynchronously *reads* `compressed_frame` from `compressed_frames` topic.
    -   *Publishes* [`fast_pose_type`][60] to `render_pose` topic.

<div id="plugins-offload-rendering-client-jetson"></div>
-   [`offload_rendering_client_jetson`][49]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`vulkan::display_provider`][30]
    -   *Calls* [`pose_prediction`][32]
    -   Asynchronously *reads* `compresswed_frame` from `compressed_frames` topic.
    -   *Publishes* [`fast_pose_type`][60] to `render_pose` topic.

<div id="plugins-offload-rendering-server"></div>
-   [`offload_rendering_server`][50]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`vulkan::display_provider`][30]
    -   Asynchronously *reads* [`fast_pose_type`][60] from `render_pose_` topic.
    -   *Publishes* `compressed_frame` to `compressed_frames` topic.

<div id="plugins-offload-vio"></div>
-   [`offload_vio`][51]:
    INFO NEEDED

    Topic details:
    
    - `offload_vio.device_rx`
      - Asynchronously *reads* a string from topic `vio_pose`.
      - *Publishes* [`pose_type`][61] to `slow_pose` topic.
      - *Publishes* [`imu_integrator_input`][66] to `imu_integrator_input` topic.
    - `offload_vio.device_tx`
      - Asynchronously *reads* [`binocular_cam_type`][63] from `cam topic`
      - *Publishes* a string to `compressed_imu_cam` topic
    - `offload_vio.server_rx`
      - Asynchronously *reads* a string from `compressed_imu_cam` topic
      - *Publishes* [`imu_type`][64] to `imu` topic.
      - *Publishes* [`binocular_cam_type`][63] to `cam` topic.
    - `offload_vio.server_tx`
      - Asynchronously *reads* [`imu_integrator_input`][66] from `imu_integrator_input` topic.
      - *Publishes* a string to `vio_pose` topic.

<div id="plugins-openni"></div>
-   [`openni`][52]:
    INFO NEEDED

    Topic details:

    -   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic. 

<div id="plugins-openvins"></div>
-   [`open_vins`][4]:
    An alternate [_SLAM_][39] ([upstream][18]) implementation that uses a MSCKF
    (Multi-State Constrained Kalman Filter) to determine poses via camera/[_IMU_][36].

    Topic details:

    -   *Publishes* [`pose_type`][61] on `slow_pose` topic.
    -   *Publishes* [`imu_integrator_input`][66] on `imu_integrator_input` topic.
    -   Synchronously *reads*/*subscribes* to [`imu_type`][64] on `imu` topic.

<div id="plugins-openwarp-vk"></div>
-   [`openwarp_vk`][53]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`vulkan::timewarp`][30]
    -   *Calls* [`pose_prediction`][32]

<div id="plugins-orb-slam3"></div>
-   [`orb_slam3`][54]:
    INFO NEEDED

    Topic details:

    -   Asynchronously *reads* [`binocular_cam_type`][63] from `cam` topic.
    -   *Publishes* [`pose_type`][61] to `slow_pose` topic.
    -   *Publishes* [`imu_integrator_input`][66] to `imu_integrator_input` topic.

<div id="plugins-passthrough-integrator"></div>
-   [`passthrough_integrator][29]:
    INFO NEEDED

    Topic details:

    -   Asynchronously *reads* [`imu_integrator_input`][66] from `imu_integrator_input` topic.
    -   Synchronously *reads* [`imu_type`][64] from `imu` topic.
    -   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.

<div id="plugins-realsense"></div>
-   [`realsense`][23]:
    Reads images and [_IMU_][36] measurements from the [Intel Realsense][25].

    Topic details:

    -   *Publishes* [`imu_type`][64] to `imu` topic.
    -   *Publishes* [`binocular_cam_type`][63] to `cam` topic.
    -   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic.

<div id="plugins-record-imu-cam"></div>
-   [`record_imu_cam`][55]:
    Writes [`imu_type`][64] and [`binocular_cam_type`][63] data to disk.

    Topic details:

    -   Asynchronously *reads* [`binocular_cam_type`][63] from `cam` topic.
    -   Synchronously *reads* [`imu_type`][64] from `imu` topic.

<div id="plugins-record-rgb-depth"></div>
-   [`record_rgb_depth`][26]:
    Writes [`rgb_depth_type`][62] data to disk.

    Topic details:

    -   Synchronously *reads* [`rgb_depth_type`][62] from `rgb_depth` topic.

<div id="plugins-rk4-integrator"></div>
-   [`rk4_integrator`][16]:
    Integrates over all [_IMU_][36] samples since the last published [_SLAM_][39] [_pose_][37] to
    provide a [_fast pose_][37] every time a new IMU sample arrives using RK4 integration.

    Topic details:

    -   Asynchronously *reads* [`imu_integrator_input`][66] from `imu_integrator_input` topic.
    -   Synchronously *reads* [`imu_type`][64] from `imu` topic.
    -   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.

<div id="plugins-tcp-network-backend"></div>
-   [`tcp_network_backend`][27]:
    INFO NEEDED

<div id="plugins-timewarp"></div>
-   [`timewarp_gl`][6]:
    [Asynchronous reprojection][35] of the [_eye buffers_][34].
    The timewarp ends just after [_vsync_][34], so it can deduce when the next vsync will be.

    Topic details:

    -   *Calls* [`pose_prediction`][57]
    -   *Publishes* `hologram_input` to `hologram_in` topic.
    -   If using Monado
        - Asynchronously *reads* `rendered_frame` on `eyebuffer` topic, if using Monado.
        - *Publishes* `time_point` to `vsync_estimate` topic.
        - *Publishes* `texture_pose` to `texture_pose` topic if `ILLIXR_OFFLOAD_ENABLE` is set in the env.
    -   If *not* using Monado
        - *Publishes*  `signal_to_quad` to `signal_quad` topic.

<div id="plugins-timewarp_vk"></div>
-   [`timewarp_vk`][10]
    [Asynchronous reprojection][35] of the [_eye buffers_][34].
    The timewarp ends just after [_vsync_][34], so it can deduce when the next vsync will be.

    Topic details:

    -   *Calls* [`vulkan::timewarp`][30]
    -   *Calls* [`pose_prediction`][57]
    -   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

<div id="plugins-webcam"></div>
-   [`webcam`][44]:
    Uses a webcam to capture images for input into the `hand_tracking` plugin. This plugin is useful for debugging and is not meant to be used in a production pipeline.

    Topic details:

    -   *Publishes* [`monocular_cam_type`][67] to `webcam` topic.

<div id="plugins-zed"></div>
-   [`zed`][22]:
    Reads images and [_IMU_][36] measurements from the [ZED Mini][24].
    Unlike `offline_imu`, `zed` additionally has RGB and [_depth_][34] data.
    Note that this plugin implements two threads: one for the camera, and one for the IMU.

    Topic details:

    -   *Publishes* [`imu_type`][64] to `imu` topic.
    -   *Publishes* [`binocular_cam_type`][63] to `cam` topic.
    -   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic.
    -   *Publishes* [`camera_data`][68] to `cam_data` topic.
    -   *Publishes* [`cam_type_zed`][70] on `cam_zed` topic.

<div id="plugins-zed-data-injection"></div>
-   [`zed.data_injection`][31]:
    Reads images and pose information from disk and publishes them to ILLIXR.

    Topic details:

    -   *Publishes* [`binocular_cam_type`][63] to `cam` topic
    -   *Publishes* [`pose_type`][61] to `pose` topic.
    -   *Publishes* [`camera_data`][68] to `cam_data` topic.
    
Below this point, we will use Switchboard terminology.
Read the [API documentation on _Switchboard_][32] for more information.

<img
    src="../images/dataflow.dot.png"
    alt ="ILLIXR dataflow graph, showing switchboard communication"
    style="width: 400px;"
/>

-   In the above figure, ovals are plugins.

-   Solid arrows from plugins to topics represent publishing.

-   Solid arrows from topics to plugins represent synchronous reading.
    Some action is taken for _every_ event which gets published on the topic.

-   Dashed arrows from topics to plugins represent asynchronous reading.
    Plugin readers only need the _latest_ event on their topic.

-   Imagine the topic as a trough filling with events from its publisher.
    Synchronous readers (AKA subscribers) drain the trough,
        while asynchronous readers just skim fresh events off the top of the trough.

See [Writing Your Plugin][30] to extend ILLIXR.



## Plugin Interdependencies

Some plugins require other plugins to be loaded in order to work. The table below gives a listing of the plugin interdependencies.

| Plugin          | Requires        | Provided by plugin                      |
|:----------------|:----------------|:----------------------------------------|
| debugview       | pose_prediction | faux_pose, pose_lookup, pose_prediction |
| gldemo          | pose_prediction | faux_pose, pose_lookup, pose_prediction |
| native_renderer | app             | vkdemo                                  |
|                 | display_sink    | display_vk                              |
|                 | pose_prediction | faux_pose, pose_lookup, pose_prediction |
|                 | timewarp        | timewarp_vk                             |
| timewarp_gl     | pose_prediction | faux_pose, pose_lookup, pose_prediction |
| timewarp_vk     | display_sink    | display_vk                              |
|                 | pose_prediction | faux_pose, pose_lookup, pose_prediction |
| vkdemo          | display_sink    | display_vk                              |

See [Getting Started][31] for more information on adding plugins to a [_profile_][40] file.

[//]: # (- References -)

[1]:    https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offline_imu
[2]:    https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offline_cam
[3]:    https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ground_truth_slam
[4]:    https://github.com/ILLIXR/open_vins
[8]:    https://github.com/ILLIXR/audio_pipeline
[9]:    https://github.com/ILLIXR/HOTlab/tree/illixr-integration
[11]:   https://gtsam.org/
[12]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/gtsam_integrator
[16]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/rk4_integrator
[18]:   https://docs.openvins.com
[19]:   https://en.wikipedia.org/wiki/Binaural_recording
[21]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_data
[22]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/zed
[23]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/realsense
[24]:   https://www.stereolabs.com/zed-mini
[25]:   https://www.intelrealsense.com/depth-camera-d435
[26]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/record_rgb_depth
[27]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/tcp_network_backend
[28]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/depthai
[29]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/passthrough_integrator
[30]:   https://github.com/ILLIXR/ILLIXR/tree/master/include/illixr/vk
[31]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/zed/data_injection
[32]:   https://github.com/ILLIXR/ILLIXR/tree/master/services/pose_prediction

[//]: # (- Internal -)

[5]:    plugin_README/README_gldemo.md
[6]:    plugin_README/README_timewarp_gl.md
[7]:    plugin_README/README_debugview.md
[10]:   plugin_README/README_timewarp_vk.md
[30]:   writing_your_plugin.md
[31]:   getting_started.md
[32]:   api/html/classILLIXR_1_1switchboard.html
[33]:   glossary.md#ground-truth
[34]:   glossary.md#framebuffer
[35]:   glossary.md#asynchronous-reprojection
[36]:   glossary.md#inertial-measurement-unit
[37]:   glossary.md#pose
[38]:   glossary.md#head-mounted-display
[39]:   glossary.md#simultaneous-localization-and-mapping
[40]:   glossary.md#profile
[41]:   glossary.md#plugin
[43]:   plugin_README/README_hand_tracking.md#viewer
[44]:   plugin_README/README_webcam.md
[45]:   plugin_README/README_hand_tracking.md
[46]:   plugin_README/README_lighthouse.md
[47]:   plugin_README/README_native_renderer.md
[48]:   plugin_README/README_offload_rendering_client.md
[49]:   plugin_README/README_offload_rendering_client_jetson.md
[50]:   plugin_README/README_offload_rendering_server.md
[51]:   plugin_README/offload_vio.md
[52]:   plugin_README/README_openni.md
[53]:   plugin_README/README_openwarp.md
[54]:   plugin_README/README_ORB_SLAM3.md
[55]:   plugin_README/README_record_imu_cam.md
[56]:   plugin_README/README_vkdemo.md
[57]:   illixr_services.md#service-pose-prediction

[60]:   api/html/structILLIXR_1_1data__format_1_1fast__pose__type.html
[61]:   api/html/structILLIXR_1_1data__format_1_1pose__type.html
[62]:   api/html/structILLIXR_1_1data__format_1_1rgb__depth__type.html
[63]:   api/html/structILLIXR_1_1data__format_1_1binocular__cam__type.html
[64]:   api/html/structILLIXR_1_1data__format_1_1imu__type.html
[65]:   api/html/structILLIXR_1_1data__format_1_1imu__raw__type.html
[66]:   api/html/structILLIXR_1_1data__format_1_1imu__integrator__input.html
[67]:   api/html/structILLIXR_1_1data__format_1_1monocular__cam__type.html
[68]:   api/html/structILLIXR_1_1data__format_1_1camera__data.html
[69]:   api/html/structILLIXR_1_1data__format_1_1depth__type.html
[70]:   api/html/structILLIXR_1_1data__format_1_1cam__type__zed.html
[71]:   api/html/structILLIXR_1_1data__format_1_1ht_1_1ht__frame.html
