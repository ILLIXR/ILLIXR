# ILLIXR plugins

This page details the structure of ILLIXR's [_plugins_][41] and how they interact with each other.

## audio_pipeline

Launches a thread for [binaural][19] recording and one for binaural playback.
Audio output is not yet routed to the system's speakers or microphone,
but the plugin's compute workload is still representative of a real system.
By default, this plugin is enabled (see `native` [_configuration_][40]).

Topic details:

- *Calls* [`pose_prediction`][57]

&nbsp;&nbsp;[**Details**][D1]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C1]

## debugview

Renders incoming [_frames_][34] from the graphics pipeline for debugging live executions of the application.

Topic details:

- *Calls* [`pose_prediction`][57]
- Asynchronously *reads* [`fast_pose_type`][60] from `imu_raw` topic. ([_IMU_][36] biases are unused).
- Asynchronously *reads* [`pose_type`][61] from `slow_pose` topic.
- Asynchronously *reads* [`rgb_depth_type`][62] from `rgb_depth` topic.
- Asynchronously *reads* buffered [`binocular_cam_type`][63] from `cam` topic.

&nbsp;&nbsp;[**Details**][D2]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C2]

## depthai

Enables access to the DepthAI library.

Topic details:

-  *Publishes* [`imu_type`][64] to `imu` topic
-  *Publishes* [`binocular_cam_type`][63] to `cam` topic`
-  *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C3]

## gldemo [^1]

Renders a static scene (into left and right [_eye buffers_][34]) given the [_pose_][37]
from [`pose_prediction`][57].

Topic details:

-   *Calls* [`pose_prediction`][57]
-   *Publishes* `rendered_frame` to `eyebuffer` topic.
-   *Publishes* `image_handle` to `image_handle` topic.
-   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][D4]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C4]

## ground_truth_slam

Reads the [_ground truth_][34] from the same dataset as the `offline_imu` plugin.
Ground truth data can be compared against the head tracking results (e.g. from VIO, IMU integrator, or pose predictor) for accuracy.
Timing information is taken from the `offline_imu` measurements/data.

Topic details:

-   *Publishes* [`pose_type`][61] to `true_pose` topic.
-   *Publishes* `Eigen::Vector3f` to `ground_truth_offset` topic.
-   Asynchronously *reads* [`imu_type`][64] from `imu` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C5]

## gtsam_integrator

Integrates over all [_IMU_][36] samples since the last published [_SLAM_][39] pose to provide a
[_fast pose_][37] every time a new IMU sample arrives using the GTSAM library ([upstream][11]).

Topic details:

-   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.
-   Synchronously *reads* [`imu_type`][64] from `imu` topic.
-   Asynchronously *reads* [`imu_integrator_input`][66] to `imu_integrator_input` topic.

&nbsp;&nbsp;**Details** [**Code**][C6]

## hand_tracking

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

&nbsp;&nbsp;[**Details**][D7]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C7]

## hand_tracking_gpu

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

&nbsp;&nbsp;[**Details**][D7]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C7]

## hand_tracking.viewer

Reads the output of the `hand_tracking` plugin and displays the results on the screen. This is most useful for debugging. The capabilities of this plugin will be merged into the `debugview` plugin in the future.

Topic details:

-   Synchronously *reads* [`ht_frame`][71] from `ht` topic.

&nbsp;&nbsp;[**Details**][D8]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C8]

## lighthouse

Enables lighthouse tracking using the [libsurvive library](https://github.com/collabora/libsurvive)

Topic details:

-   *Publishes* [`pose_type`][61] to `slow_pose` topic.
-   *Publishes* [`fast_pose_type`][60] to `fast_pose` topic. 

&nbsp;&nbsp;[**Details**][D9]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C9]

## native_renderer

Constructs a full rendering pipeline utilizing several ILLIXR components.

Topic details:

-   *Calls* [`pose_prediction`][57]
-   *Calls* [`vulkan::display_provider`][30]
-   *Calls* [`vulkan::timewarp`][30]
-   *Calls* [`vulkan::app`][30]
-   Synchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][D10]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C10]

## offline_cam

Reads camera images from files on disk, emulating real cameras on the [_headset_][38]
(feeds the application input measurements with timing similar to an actual camera).

Topic details:

-   *Publishes* [`binocular_cam_type`][63] to `cam` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C11]

## offline_imu

Reads [_IMU_][36] data files on disk, emulating a real sensor on the [_headset_][38]
(feeds the application input measurements with timing similar to an actual IMU).

Topic details:

-   *Publishes* [`imu_type`][64] to `imu` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C12]

## offload_data

Writes [_frames_][34] and [_poses_][37] output from the [_asynchronous reprojection_][35] plugin to disk for analysis.

Topic details:

-   Synchronously *reads* `texture_pose` to `texture_pose` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C13]

## offload_rendering_client

Receives encoded frames from the network, sent by [offload_rendering_server](#offload_rendering_server)

Topic details:

-   *Calls* [`vulkan::display_provider`][30]
-   *Calls* [`pose_prediction`][32]
-   Asynchronously *reads* `compressed_frame` from `compressed_frames` topic.
-   *Publishes* [`fast_pose_type`][60] to `render_pose` topic.

&nbsp;&nbsp;[**Details**][D14]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C14]

## offload_rendering_client_jetson

Receives encoded frames from the network, sent by [offload_rendering_server](#offload_rendering_server), but specialized to run on a Jetson.

Topic details:

-   *Calls* [`vulkan::display_provider`][30]
-   *Calls* [`pose_prediction`][32]
-   Asynchronously *reads* `compresswed_frame` from `compressed_frames` topic.
-   *Publishes* [`fast_pose_type`][60] to `render_pose` topic.

&nbsp;&nbsp;[**Details**][D15]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C15]

## offload_rendering_server

Encodes and transmits frames to one of the offload_rendering_clients. 

Topic details:

-   *Calls* [`vulkan::display_provider`][30]
-   Asynchronously *reads* [`fast_pose_type`][60] from `render_pose_` topic.
-   *Publishes* `compressed_frame` to `compressed_frames` topic.

&nbsp;&nbsp;[**Details**][D16]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C16]

## offload_vio

Four plugins which work in unison to allow head tracking (VIO) to be rendered remotely. 

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

&nbsp;&nbsp;[**Details**][D17]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C17]

## openni

Enables an interface to the Openni algorithms.

Topic details:

-   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic. 

&nbsp;&nbsp;[**Details**][D18]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C18]

## open_vins

An alternate [_SLAM_][39] ([upstream][18]) implementation that uses a MSCKF
(Multi-State Constrained Kalman Filter) to determine poses via camera/[_IMU_][36].

Topic details:

-   *Publishes* [`pose_type`][61] on `slow_pose` topic.
-   *Publishes* [`imu_integrator_input`][66] on `imu_integrator_input` topic.
-   Synchronously *reads*/*subscribes* to [`imu_type`][64] on `imu` topic.

&nbsp;&nbsp;[**Details**][D19]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C19]

## openwarp_vk

Provides a Vulkan-based reprojection service.

Topic details:

-   *Calls* [`vulkan::timewarp`][30]
-   *Calls* [`pose_prediction`][32]

&nbsp;&nbsp;[**Details**][D20]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C20]

## orb_slam3

Utilizes the ORB_SLAM3 library to enable real-time head tracking (VIO).

Topic details:

-   Asynchronously *reads* [`binocular_cam_type`][63] from `cam` topic.
-   *Publishes* [`pose_type`][61] to `slow_pose` topic.
-   *Publishes* [`imu_integrator_input`][66] to `imu_integrator_input` topic.

&nbsp;&nbsp;[**Details**][D21]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C21]

## passthrough_integrator

Provides IMU integration.

Topic details:

-   Asynchronously *reads* [`imu_integrator_input`][66] from `imu_integrator_input` topic.
-   Synchronously *reads* [`imu_type`][64] from `imu` topic.
-   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C22]

## realsense

Reads images and [_IMU_][36] measurements from the [Intel Realsense][25].

Topic details:

-   *Publishes* [`imu_type`][64] to `imu` topic.
-   *Publishes* [`binocular_cam_type`][63] to `cam` topic.
-   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C23]

## record_imu_cam

Writes [`imu_type`][64] and [`binocular_cam_type`][63] data to disk.

Topic details:

-   Asynchronously *reads* [`binocular_cam_type`][63] from `cam` topic.
-   Synchronously *reads* [`imu_type`][64] from `imu` topic.

&nbsp;&nbsp;[**Details**][D24]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C24]

## record_rgb_depth

Writes [`rgb_depth_type`][62] data to disk.

Topic details:

-   Synchronously *reads* [`rgb_depth_type`][62] from `rgb_depth` topic.

**Details** [**Code**][C25]

## rk4_integrator

Integrates over all [_IMU_][36] samples since the last published [_SLAM_][39] [_pose_][37] to
provide a [_fast pose_][37] every time a new IMU sample arrives using RK4 integration.

Topic details:

-   Asynchronously *reads* [`imu_integrator_input`][66] from `imu_integrator_input` topic.
-   Synchronously *reads* [`imu_type`][64] from `imu` topic.
-   *Publishes* [`imu_raw_type`][65] to `imu_raw` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C26]

## tcp_network_backend

Provides network communications over TCP.

**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C27]

## timewarp_gl [^1]

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

&nbsp;&nbsp;[**Details**][D28]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C28]

## timewarp_vk

[Asynchronous reprojection][35] of the [_eye buffers_][34].
The timewarp ends just after [_vsync_][34], so it can deduce when the next vsync will be.

Topic details:

-   *Calls* [`vulkan::timewarp`][30]
-   *Calls* [`pose_prediction`][57]
-   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][D29]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C29]

## webcam

Uses a webcam to capture images for input into the `hand_tracking` plugin. This plugin is useful for debugging and is not meant to be used in a production pipeline.

Topic details:

-   *Publishes* [`monocular_cam_type`][67] to `webcam` topic.

&nbsp;&nbsp;[**Details**][D30]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C30]

## zed

Reads images and [_IMU_][36] measurements from the [ZED Mini][24].
Unlike `offline_imu`, `zed` additionally has RGB and [_depth_][34] data.

!!! note
    
    This plugin implements two threads: one for the camera, and one for the IMU.

Topic details:

-   *Publishes* [`imu_type`][64] to `imu` topic.
-   *Publishes* [`binocular_cam_type`][63] to `cam` topic.
-   *Publishes* [`rgb_depth_type`][62] to `rgb_depth` topic.
-   *Publishes* [`camera_data`][68] to `cam_data` topic.
-   *Publishes* [`cam_type_zed`][70] on `cam_zed` topic.

&nbsp;&nbsp;[**Details**][D31]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C31]

## zed.data_injection

Reads images and pose information from disk and publishes them to ILLIXR.

Topic details:

-   *Publishes* [`binocular_cam_type`][63] to `cam` topic
-   *Publishes* [`pose_type`][61] to `pose` topic.
-   *Publishes* [`camera_data`][68] to `cam_data` topic.

&nbsp;&nbsp;[**Details**][D32]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C32]

Below this point, we will use Switchboard terminology.
Read the [API documentation on _Switchboard_][32] for more information.

{! include-markdown "dataflow.md" !}

See [Writing Your Plugin][30] to extend ILLIXR.

## Plugin Interdependencies

Some plugins require other plugins to be loaded in order to work. The table below gives a listing of the plugin
interdependencies.

| Plugin          | Requires        | Provided by plugin                      |
|:----------------|:----------------|:----------------------------------------|
| debugview       | pose_prediction | fauxpose, pose_lookup, pose_prediction |
| gldemo          | pose_prediction | fauxpose, pose_lookup, pose_prediction |
| native_renderer | app             | vkdemo                                  |
|                 | display_sink    | display_vk                              |
|                 | pose_prediction | fauxpose, pose_lookup, pose_prediction |
|                 | timewarp        | timewarp_vk                             |
| timewarp_gl     | pose_prediction | fauxpose, pose_lookup, pose_prediction |
| timewarp_vk     | display_sink    | display_vk                              |
|                 | pose_prediction | fauxpose, pose_lookup, pose_prediction |
| vkdemo          | display_sink    | display_vk                              |

See [Getting Started][31] for more information on adding plugins to a [_profile_][40] file.


[^1]: ILLIXR has switched to a Vulkan back end, thus OpenGL based plugins may not work on every system.


[//]: # (- References -)

[D1]:   https://github.com/ILLIXR/audio_pipeline/blob/master/README.md

[D2]:   plugin_README/README_debugview.md

[D4]:   plugin_README/README_gldemo.md

[D7]:   plugin_README/README_hand_tracking.md

[D8]:   plugin_README/README_hand_tracking.md#viewer

[D9]:   plugin_README/README_lighthouse.md

[D10]:  plugin_README/README_native_renderer.md

[D14]:  plugin_README/README_offload_rendering_client.md

[D15]:  plugin_README/README_offload_rendering_client_jetson.md

[D16]:  plugin_README/README_offload_rendering_server.md

[D17]:  plugin_README/README_offload_vio.md

[D18]:  plugin_README/README_openni.md

[D19]:  https://github.com/ILLIXR/open_vins/blob/master/ReadMe.md

[D20]:  plugin_README/README_openwarp_vk.md

[D20]:  plugin_README/README_orb_slam3.md

[D24]:  plugin_README/README_record_imu_cam.md

[D28]:  plugin_README/README_timewarp_gl.md

[D29]:  plugin_README/README_timewarp_vk.md

[D30]:  plugin_README/README_webcam.md

[D31]:  plugin_README/README_zed.md

[D32]:  plugin_README/README_zed_data_injection.md


[9]:    https://github.com/ILLIXR/HOTlab/tree/illixr-integration

[11]:   https://gtsam.org/

[18]:   https://docs.openvins.com

[19]:   https://en.wikipedia.org/wiki/Binaural_recording

[24]:   https://www.stereolabs.com/zed-mini

[25]:   https://www.intelrealsense.com/depth-camera-d435

[30]:   https://github.com/ILLIXR/ILLIXR/tree/master/include/illixr/vk

[32]:   https://github.com/ILLIXR/ILLIXR/tree/master/services/pose_prediction


[//]: # (- Code -)

[C1]:   https://github.com/ILLIXR/audio_pipeline

[C2]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/debugview

[C3]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/depthai

[C4]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/gldemo

[C5]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ground_truth_slam

[C6]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/gtsam_integrator

[C7]:   https://github.com/ILLIXR/hand_tracking

[C8]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/hand_tracking/viewer

[C9]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/lighthouse

[C10]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/native_renderer

[C11]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offline_cam

[C12]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offline_imu

[C13]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_data

[C14]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_rendering_client

[C15]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_rendering_client_jetson

[C16]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_rendering_server

[C17]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/offload_vio

[C18]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/openni

[C19]:  https://github.com/ILLIXR/open_vins

[C20]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/openwarp_vk

[C21]:  https://github.com/ILLIXR/ORB_SLAM3

[C22]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/passthrough_integrator

[C23]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/realsense

[C24]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/record_imu_cam

[C25]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/record_rgb_depth

[C26]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/rk4_integrator

[C27]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/tcp_network_backend

[C28]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/timewarp_gl

[C29]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/timewarp_vk

[C30]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/webcam

[C31]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/zed

[C32]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/zed/data_injection

[//]: # (- Internal -)

[30]:   working_with/writing_your_plugin.md

[31]:   getting_started.md

[32]:   api/classILLIXR_1_1switchboard.md

[33]:   glossary.md#ground-truth

[34]:   glossary.md#framebuffer

[35]:   glossary.md#asynchronous-reprojection

[36]:   glossary.md#inertial-measurement-unit

[37]:   glossary.md#pose

[38]:   glossary.md#head-mounted-display

[39]:   glossary.md#simultaneous-localization-and-mapping

[40]:   glossary.md#profile

[41]:   glossary.md#plugin

[57]:   illixr_services.md#pose_prediction

[60]:   api/structILLIXR_1_1data__format_1_1fast__pose__type.md

[61]:   api/structILLIXR_1_1data__format_1_1pose__type.md

[62]:   api/structILLIXR_1_1data__format_1_1rgb__depth__type.md

[63]:   api/structILLIXR_1_1data__format_1_1binocular__cam__type.md

[64]:   api/structILLIXR_1_1data__format_1_1imu__type.md

[65]:   api/structILLIXR_1_1data__format_1_1imu__raw__type.md

[66]:   api/structILLIXR_1_1data__format_1_1imu__integrator__input.md

[67]:   api/structILLIXR_1_1data__format_1_1monocular__cam__type.md

[68]:   api/structILLIXR_1_1data__format_1_1camera__data.md

[69]:   api/structILLIXR_1_1data__format_1_1depth__type.md

[70]:   api/structILLIXR_1_1data__format_1_1cam__type__zed.md

[71]:   api/structILLIXR_1_1data__format_1_1ht_1_1ht__frame.md
