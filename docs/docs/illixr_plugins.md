# ILLIXR plugins

This page details the structure of ILLIXR's [_plugins_][G18] and how they interact with each other.

## ada

Ada’s distributed design relies on **four communication plugins** that coordinate data transfer between the **device** and **server** for remote scene provisioning.

<<<<<<< Updated upstream
-   `ada.device_rx`
    - Asynchronously *reads* a string from topic `ada_processed`
    - *Publishes* [`mesh_type`][A23] to `compressed_scene` topic.
    - *Publishes* [`vb_type`][A24] to `VB_update_lists` topic.
-   `ada.device_tx`
    - Synchronously *reads* [`scene_recon_type`][A25] from `ScanNet_Data` topic.
    - *Publishes* a string to `ada_data` topic.
-   `ada.server_rx`
    - Asynchronously *reads* a string from topic `ada_data`
    - *Publishes* [`scene_recon_type`][A25] to `ScanNet_Data` topic.
-   `ada.server_tx`
    - Synchronously *reads* [`vb_type`][A24] from `unique_VB_list` topic.
    - Synchronously *reads* [`mesh_type`][A23] from `compressed_scene` topic.
    - *Publishes* a string to `ada_processed` topic.
=======
- `ada.device_rx`
  - *Purpose:* receives processed data from the server to the device. 
  - Asynchronously *reads* a string from the topic `ada_processed`, which contains protobuf packets sent by the server.  
  - *Publishes* [`mesh_type`][A23] to `compressed_scene` topic.  forwards compressed mesh chunks to `ada.mesh_decompression_grey`  
  - *Publishes* [`vb_type`][A24] to `VB_update_lists` topic. forwards the unique voxel block list (UVBL) to `ada.scene_management`
- `ada.device_tx`
  - *Purpose:* sends encoded depth images (MSB and LSB) along with non-encoded pose information from the device to the server.  
  - *Features:* the LSB encoding bitrate is configurable for bandwidth–reconstruction accuracy trade-offs. 
  - Synchronously *reads* [`scene_recon_type`][A25] from `ScanNet_Data` topic, which provides dataset input (via `ada.offline_scannet`).  
  - *Publishes* encoded depth data as a string to the `ada_data` topic.
- `ada.server_rx`
  - *Purpose:* receives encoded depth images and poses (not encoded) from the device, decodes them, and feeds them to the reconstruction module (`ada.infiniTAM`).  
  - Asynchronously *reads* a string from topic `ada_data`
  - *Publishes* [`scene_recon_type`][A25] to `ScanNet_Data` topic. provides decoded depth frames and corresponding pose inforamtion to downstream reconstruction.
- `ada.server_tx`
  - *Purpose:* sends processed scene data (meshes and Unique Voxel Block Lists(UVBL)) from the server back to the device. 
  - Synchronously *reads* [`vb_type`][A24] from `unique_VB_list` topic (output of `ada.infiniTAM`). 
  - Synchronously *reads* [`mesh_type`][A23] from `compressed_scene` topic (output of `ada.infiniTAM`).
  - *Publishes* a string to `ada_processed` topic, each string is either a protobuf for vb_type topic or a protobuf for mesh_type topic.
>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C33]

## add.infinitam
Topic details:
<<<<<<< Updated upstream

-   Synchronously *reads* [`scene_recon_type`][A25] from `ScanNet_Data` topic.
-   *Publishes* [`mesh_type`][A23] to `requested_scene` topic.
-   *Publishes* [`vb_type`][A24] to `unique_VB_list` topic.
=======
-  *Purpose:* performs **scene reconstruction** using incoming depth and pose data from the device, followed by **on-demand or proactive scene extraction**.  
  During extraction, it generates both the **updated partial mesh** and the **Unique Voxel Block List (UVBL)**, which are sent downstream for compression and scene management.  
- *Features:* extraction frequency is configurable to balance latency and compute cost.  
- Synchronously *reads* [`scene_recon_type`][A25] from `ScanNet_Data` topic.
- *Publishes* [`mesh_type`][A23] to `requested_scene` topic. Extracted mesh chunks for compression
- *Publishes* [`vb_type`][A24] to `unique_VB_list` topic. This is the metadata for identifying updated voxel regions
>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C34]

## ada.mesh_compression

Topic details:
<<<<<<< Updated upstream

-   Synchronously *reads* [`mesh_type`][A23] from `requested_scene` topic.
-   *Publishes* [`mesh_type`][A23] to `compressed_scene` topic.
=======
- *Purpose:* compresses mesh chunks from `ada.infinitam` using a **customized version of Google Draco**.  
- *Features:* compression parallelism can be tuned for different latency–power trade-offs.
- Synchronously *reads* [`mesh_type`][A23] from `requested_scene` topic.
- *Publishes* [`mesh_type`][A23] to `compressed_scene` topic. compressed mesh chunks ready for transmission to the device. Voxel block information has been attached to each encoded face. 
>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C35]

## ada.mesh_decompression_grey

Topic details:
<<<<<<< Updated upstream

-   Synchronously *reads* [`mesh_type`][A23] from `compressed_scene` topic.
-   *Publishes* [`draco_type`][A26] to `decoded_inactive_scene` topic.
=======
- *Purpose:* decompress the mesh chunks received from the server and performs a portion of scene management that can be parallelized. 
- *Features:* decompression parallelism can be tuned for different latency–power trade-offs. 
- Synchronously *reads* [`mesh_type`][A23] from `compressed_scene` topic.
- *Publishes* [`draco_type`][A26] to `decoded_inactive_scene` topic. decoded mesh data sent to `ada.scene_management`.  

>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C36]

## ada.offline_scannet

Topic details:
<<<<<<< Updated upstream

-   *Publishes* [`scene_recon_type`][A25] to `ScanNet_Data` topic.
=======
- *Purpose:* loads the **ScanNet dataset** for offline or reproducible experiments.
- *Publishes* [`scene_recon_type`][A25] to `ScanNet_Data` topic.
>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C37]

## ada.scene_management

Topic details:
<<<<<<< Updated upstream

-   Synchronously *reads* [`draco_type`][A26] from `decoded_inactive_scene` topic.
-   Synchronously *reads* [`vb_type`][A24] from `VB_update_lists` topic.
=======
- *Purpose:* integrates incremental scene updates into a **maintained global mesh**, merging new geometry and removing outdated regions for consistency.  
- Synchronously *reads* [`draco_type`][A26] from `decoded_inactive_scene` topic.
- Synchronously *reads* [`vb_type`][A24] from `VB_update_lists` topic.
>>>>>>> Stashed changes

&nbsp;&nbsp;[**Details**][P33]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C38]

## audio_pipeline

Launches a thread for [binaural][E12]: recording and one for binaural playback.
Audio output is not yet routed to the system's speakers or microphone,
but the plugin's compute workload is still representative of a real system.
By default, this plugin is enabled (see `native` [_configuration_][G17]).

Topic details:

- *Calls* [`pose_prediction`][S10]

&nbsp;&nbsp;[**Details**][P10]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C1]

## debugview

Renders incoming [_frames_][G11] from the graphics pipeline for debugging live executions of the application.

Topic details:

- *Calls* [`pose_prediction`][S10]
- Asynchronously *reads* [`fast_pose_type`][A11] from `imu_raw` topic. ([_IMU_][G13] biases are unused).
- Asynchronously *reads* [`pose_type`][A12] from `slow_pose` topic.
- Asynchronously *reads* [`rgb_depth_type`][A13] from `rgb_depth` topic.
- Asynchronously *reads* buffered [`binocular_cam_type`][A14] from `cam` topic.

&nbsp;&nbsp;[**Details**][P11]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C2]

## depthai

Enables access to the DepthAI library.

Topic details:

-  *Publishes* [`imu_type`][A15] to `imu` topic
-  *Publishes* [`binocular_cam_type`][A14] to `cam` topic`
-  *Publishes* [`rgb_depth_type`][A13] to `rgb_depth` topic

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C3]

## gldemo [^1]

Renders a static scene (into left and right [_eye buffers_][G11]) given the [_pose_][G14]
from [`pose_prediction`][S10].

Topic details:

-   *Calls* [`pose_prediction`][S10]
-   *Publishes* `rendered_frame` to `eyebuffer` topic.
-   *Publishes* `image_handle` to `image_handle` topic.
-   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][P12]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C4]

## ground_truth_slam

Reads the [_ground truth_][G10] from the same dataset as the `offline_imu` plugin.
Ground truth data can be compared against the head tracking results (e.g. from VIO, IMU integrator, or pose predictor) for accuracy.
Timing information is taken from the `offline_imu` measurements/data.

Topic details:

-   *Publishes* [`pose_type`][A12] to `true_pose` topic.
-   *Publishes* `Eigen::Vector3f` to `ground_truth_offset` topic.
-   Asynchronously *reads* [`imu_type`][A15] from `imu` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C5]

## gtsam_integrator

Integrates over all [_IMU_][G13] samples since the last published visual-inertial [_pose_][G14] to provide a
[_fast pose_][G14] every time a new IMU sample arrives using the GTSAM library ([upstream][E10]).

Topic details:

-   *Publishes* [`imu_raw_type`][A16] to `imu_raw` topic.
-   Synchronously *reads* [`imu_type`][A15] from `imu` topic.
-   Asynchronously *reads* [`imu_integrator_input`][A17] to `imu_integrator_input` topic.

&nbsp;&nbsp;**Details** [**Code**][C6]

## hand_tracking

Detects and identifies hands in an image, CPU based calculations. The output from this plugin can be used to track hand movements and recognize hand gestures.

Topic details:

-   Synchronously *reads* one of [`monocular_cam_type`][A18] from `webcam` topic, [`binocular_cam_type`][A14] from `cam` topic, or [`cam_type_zed`][A21] from `cam_zed` topic. This is selectable at run time via an environment variable.
-   Asynchronously *reads* [`camera_data`][A19] from `cam_data` topic, only once as values are static
-   If reading from `webcam`
    - Asynchronously *reads* [`pose_type`][A12] from `pose` topic
    - Asynchronously *reads* one of [`depth_type`][A20] from `depth` topic or [`rgb_depth_type`][A13] from `rgb_depth` topic, depending on which is available
-   If reading from `cam`
    - Asynchronously *reads* [`pose_type`][A12] from `pose` topic
    - Asynchronously *reads* one of [`depth_type`][A20] from `depth` topic or [`rgb_depth_type`][A13] from `rgb_depth` topic, if either is available, but not required
-   If reading from `cam_zed`, no additional data are required.
-   *Publishes* [`ht_frame`][A22] to `ht` topic.

&nbsp;&nbsp;[**Details**][P13]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C7]

## hand_tracking_gpu

Detects and identifies hands in an image, GPU based calculations. The output from this plugin can be used to track hand movements and recognize hand gestures. This plugin is currently experimental.

Topic details:

-   Synchronously *reads* one of [`monocular_cam_type`][A18] from `webcam` topic, [`binocular_cam_type`][A14] from `cam` topic, or [`cam_type_zed`][A21] from `cam_zed` topic. This is selectable at run time via an environment variable.
-   Asynchronously *reads* [`camera_data`][A19] from `cam_data` topic, only once as values are static
-   If reading from `webcam`
    - Asynchronously *reads* [`pose_type`][A12] from `pose` topic
    - Asynchronously *reads* one of [`depth_type`][A20] from `depth` topic or [`rgb_depth_type`][A13] from `rgb_depth` topic, depending on which is available
-   If reading from `cam`
    - Asynchronously *reads* [`pose_type`][A12] from `pose` topic
    - Asynchronously *reads* one of [`depth_type`][A20] from `depth` topic or [`rgb_depth_type`][A13] from `rgb_depth` topic, if either is available, but not required
-   If reading from `cam_zed`, no additional data are required.
-   *Publishes* [`ht_frame`][A22] to `ht` topic.

&nbsp;&nbsp;[**Details**][P13]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C7]

## hand_tracking.viewer

Reads the output of the `hand_tracking` plugin and displays the results on the screen. This is most useful for debugging. The capabilities of this plugin will be merged into the `debugview` plugin in the future.

Topic details:

-   Synchronously *reads* [`ht_frame`][A22] from `ht` topic.

&nbsp;&nbsp;[**Details**][P14]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C8]

## lighthouse

Enables lighthouse tracking using the [libsurvive library](https://github.com/collabora/libsurvive)

Topic details:

-   *Publishes* [`pose_type`][A12] to `slow_pose` topic.
-   *Publishes* [`fast_pose_type`][A11] to `fast_pose` topic. 

&nbsp;&nbsp;[**Details**][P15]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C9]

## native_renderer

Constructs a full rendering pipeline utilizing several ILLIXR components.

Topic details:

-   *Calls* [`pose_prediction`][S10]
-   *Calls* [`vulkan::display_provider`][E15]
-   *Calls* [`vulkan::timewarp`][E15]
-   *Calls* [`vulkan::app`][E15]
-   Synchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][P21]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C10]

## offline_cam

Reads camera images from files on disk, emulating real cameras on the [_headset_][G15]
(feeds the application input measurements with timing similar to an actual camera).

Topic details:

-   *Publishes* [`binocular_cam_type`][A14] to `cam` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C11]

## offline_imu

Reads [_IMU_][G13] data files on disk, emulating a real sensor on the [_headset_][G15]
(feeds the application input measurements with timing similar to an actual IMU).

Topic details:

-   *Publishes* [`imu_type`][A15] to `imu` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C12]

## offload_data

Writes [_frames_][G11] and [_poses_][G14] output from the [_asynchronous reprojection_][G12] plugin to disk for analysis.

Topic details:

-   Synchronously *reads* `texture_pose` to `texture_pose` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C13]

## offload_rendering_client

Receives encoded frames from the network, sent by [offload_rendering_server](#offload_rendering_server)

Topic details:

-   *Calls* [`vulkan::display_provider`][E15]
-   *Calls* [`pose_prediction`][E16]
-   Asynchronously *reads* `compressed_frame` from `compressed_frames` topic.
-   *Publishes* [`fast_pose_type`][A11] to `render_pose` topic.

&nbsp;&nbsp;[**Details**][P22]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C14]

## offload_rendering_server

Encodes and transmits frames to one of the offload_rendering_clients. 

Topic details:

-   *Calls* [`vulkan::display_provider`][E15]
-   Asynchronously *reads* [`fast_pose_type`][A11] from `render_pose_` topic.
-   *Publishes* `compressed_frame` to `compressed_frames` topic.

&nbsp;&nbsp;[**Details**][P16]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C16]

## offload_vio

Four plugins which work in unison to allow head tracking (VIO) to be rendered remotely. 

Topic details:

-   `offload_vio.device_rx`
    - Asynchronously *reads* a string from topic `vio_pose`.
    - Synchronously *reads* [`imu_type`][A15] from `imu` topic
    - *Publishes* [`pose_type`][A12] to `slow_pose` topic.
    - *Publishes* [`imu_integrator_input`][A17] to `imu_integrator_input` topic.
-   `offload_vio.device_tx`
    - Asynchronously *reads* [`binocular_cam_type`][A14] from `cam topic`
    - *Publishes* a string to `compressed_imu_cam` topic
-   `offload_vio.server_rx`
    - Asynchronously *reads* a string from `compressed_imu_cam` topic
    - *Publishes* [`imu_type`][A15] to `imu` topic.
    - *Publishes* [`binocular_cam_type`][A14] to `cam` topic.
-   `offload_vio.server_tx`
    - Asynchronously *reads* [`imu_integrator_input`][A17] from `imu_integrator_input` topic.
    - Synchronously *reads* [`pose_type`][A12] from `slow_pose` topic from `open_vins`
    - *Publishes* a string to `vio_pose` topic.

&nbsp;&nbsp;[**Details**][P17]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C17]

## openni

Enables an interface to the Openni algorithms.

Topic details:

-   *Publishes* [`rgb_depth_type`][A13] to `rgb_depth` topic. 

&nbsp;&nbsp;[**Details**][P18]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C18]

## open_vins

An alternate head tracking ([upstream][E11]) implementation that uses a MSCKF
(Multi-State Constrained Kalman Filter) to determine poses via camera/[_IMU_][G13].

Topic details:

-   *Publishes* [`pose_type`][A12] on `slow_pose` topic.
-   *Publishes* [`imu_integrator_input`][A17] on `imu_integrator_input` topic.
-   Synchronously *reads*/*subscribes* to [`imu_type`][A15] on `imu` topic.

&nbsp;&nbsp;[**Details**][P19]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C19]

## openwarp_vk

Provides a Vulkan-based reprojection service.

Topic details:

-   *Calls* [`vulkan::timewarp`][E15]
-   *Calls* [`pose_prediction`][E16]

&nbsp;&nbsp;[**Details**][P20]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C20]

## orb_slam3

Utilizes the ORB_SLAM3 library to enable real-time head tracking.

Topic details:

-   Asynchronously *reads* [`binocular_cam_type`][A14] from `cam` topic.
-   Synchronously *reads*/*subscribes* to [`imu_type`][A15] on `imu` topic.
-   *Publishes* [`pose_type`][A12] to `slow_pose` topic.
-   *Publishes* [`imu_integrator_input`][A17] to `imu_integrator_input` topic.

&nbsp;&nbsp;[**Details**][P21]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C21]

## passthrough_integrator

Provides IMU integration.

Topic details:

-   Asynchronously *reads* [`imu_integrator_input`][A17] from `imu_integrator_input` topic.
-   Synchronously *reads* [`imu_type`][A15] from `imu` topic.
-   *Publishes* [`imu_raw_type`][A16] to `imu_raw` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C22]

## realsense

Reads images and [_IMU_][G13] measurements from the [Intel Realsense][E14].

Topic details:

-   *Publishes* [`imu_type`][A15] to `imu` topic.
-   *Publishes* [`binocular_cam_type`][A14] to `cam` topic.
-   *Publishes* [`rgb_depth_type`][A13] to `rgb_depth` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C23]

## record_imu_cam

Writes [`imu_type`][A15] and [`binocular_cam_type`][A14] data to disk.

Topic details:

-   Asynchronously *reads* [`binocular_cam_type`][A14] from `cam` topic.
-   Synchronously *reads* [`imu_type`][A15] from `imu` topic.

&nbsp;&nbsp;[**Details**][P24]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C24]

## record_rgb_depth

Writes [`rgb_depth_type`][A13] data to disk.

Topic details:

-   Synchronously *reads* [`rgb_depth_type`][A13] from `rgb_depth` topic.

**Details** [**Code**][C25]

## rk4_integrator

Integrates over all [_IMU_][G13] samples since the last published visual-inertial [_pose_][G14] to
provide a [_fast pose_][G14] every time a new IMU sample arrives using RK4 integration.

Topic details:

-   Asynchronously *reads* [`imu_integrator_input`][A17] from `imu_integrator_input` topic.
-   Synchronously *reads* [`imu_type`][A15] from `imu` topic.
-   *Publishes* [`imu_raw_type`][A16] to `imu_raw` topic.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C26]

## tcp_network_backend

Provides network communications over TCP.

**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C27]

## timewarp_gl [^1]

[Asynchronous reprojection][G12] of the [_eye buffers_][G11].
The timewarp ends right before [_vsync_][G11], so it can deduce when the next vsync will be.

Topic details:

-   *Calls* [`pose_prediction`][S10]
-   *Publishes* `hologram_input` to `hologram_in` topic.
-   If using Monado
    - Asynchronously *reads* `rendered_frame` on `eyebuffer` topic, if using Monado.
    - *Publishes* `time_point` to `vsync_estimate` topic.
    - *Publishes* `texture_pose` to `texture_pose` topic if `ILLIXR_OFFLOAD_ENABLE` is set in the env.
-   If *not* using Monado
    - *Publishes*  `signal_to_quad` to `signal_quad` topic.

&nbsp;&nbsp;[**Details**][P28]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C28]

## timewarp_vk

[Asynchronous reprojection][G12] of the [_eye buffers_][G11].
The timewarp ends right before [_vsync_][G11], so it can deduce when the next vsync will be.

Topic details:

-   *Calls* [`vulkan::timewarp`][E15]
-   *Calls* [`pose_prediction`][S10]
-   Asynchronously *reads* `time_point` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][P29]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C29]

## webcam

Uses a webcam to capture images for input into the `hand_tracking` plugin. This plugin is useful for debugging and is not meant to be used in a production pipeline.

Topic details:

-   *Publishes* [`monocular_cam_type`][A18] to `webcam` topic.

&nbsp;&nbsp;[**Details**][P30]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C30]

## zed

Reads images and [_IMU_][G13] measurements from the [ZED Mini][E13].
Unlike `offline_imu`, `zed` additionally has RGB and [_depth_][G11] data.

!!! note
    
    This plugin implements two threads: one for the camera, and one for the IMU.

Topic details:

-   *Publishes* [`imu_type`][A15] to `imu` topic.
-   *Publishes* [`binocular_cam_type`][A14] to `cam` topic.
-   *Publishes* [`rgb_depth_type`][A13] to `rgb_depth` topic.
-   *Publishes* [`camera_data`][A19] to `cam_data` topic.
-   *Publishes* [`cam_type_zed`][A21] on `cam_zed` topic.

&nbsp;&nbsp;[**Details**][P31]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C31]

## zed.data_injection

Reads images and pose information from disk and publishes them to ILLIXR.

Topic details:

-   *Publishes* [`binocular_cam_type`][A14] to `cam` topic
-   *Publishes* [`pose_type`][A12] to `pose` topic.
-   *Publishes* [`camera_data`][A19] to `cam_data` topic.

&nbsp;&nbsp;[**Details**][P32]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C32]

Below this point, we will use Switchboard terminology.
Read the [API documentation on _Switchboard_][A10] for more information.

{! include-markdown "dataflow.md" !}

See [Writing Your Plugin][I10] to extend ILLIXR.

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

See [Getting Started][I11] for more information on adding plugins to a [_profile_][G17] file.


[^1]: ILLIXR has switched to a Vulkan back end, thus OpenGL based plugins may not work on every system.


[//]: # (- References -)

[P10]:   https://github.com/ILLIXR/audio_pipeline/blob/master/README.md

[P11]:   plugin_README/README_debugview.md

[P12]:   plugin_README/README_gldemo.md

[P13]:   plugin_README/README_hand_tracking.md

[P14]:   plugin_README/README_hand_tracking.md#viewer

[P15]:   plugin_README/README_lighthouse.md

[P16]:  plugin_README/README_offload_rendering_server.md

[P17]:  plugin_README/README_offload_vio.md

[P18]:  plugin_README/README_openni.md

[P19]:  https://github.com/ILLIXR/open_vins/blob/master/ReadMe.md

[P20]:  plugin_README/README_openwarp_vk.md

[P21]:  plugin_README/README_native_renderer.md

[P22]:  plugin_README/README_offload_rendering_client.md

[P24]:  plugin_README/README_record_imu_cam.md

[P28]:  plugin_README/README_timewarp_gl.md

[P29]:  plugin_README/README_timewarp_vk.md

[P30]:  plugin_README/README_webcam.md

[P31]:  plugin_README/README_zed.md

[P32]:  plugin_README/README_zed_data_injection.md

[P33]:   plugin_README/README_ada.md

[S10]:   illixr_services.md#pose_prediction


[//]: # (- external -)

[E10]:   https://gtsam.org/

[E11]:   https://docs.openvins.com

[E12]:   https://en.wikipedia.org/wiki/Binaural_recording

[E13]:   https://www.stereolabs.com/zed-mini

[E14]:   https://www.intelrealsense.com/depth-camera-d435

[E15]:   https://github.com/ILLIXR/ILLIXR/tree/master/include/illixr/vk

[E16]:   https://github.com/ILLIXR/ILLIXR/tree/master/services/pose_prediction


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

[C33]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada

[C34]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada/infinitam

[C35]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada/mesh_compression

[C36]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada/mesh_decompression_grey

[C37]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada/offline_scannet

[C38]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/ada/scene_management


[//]: # (- Internal -)

[I10]:   working_with/writing_your_plugin.md

[I11]:   getting_started.md

[G10]:   glossary.md#ground-truth

[G11]:   glossary.md#framebuffer

[G12]:   glossary.md#asynchronous-reprojection

[G13]:   glossary.md#inertial-measurement-unit

[G14]:   glossary.md#pose

[G15]:   glossary.md#head-mounted-display

[G16]:   glossary.md#simultaneous-localization-and-mapping

[G17]:   glossary.md#profile

[G18]:   glossary.md#plugin

[//]: # (- api -)

[A10]:   api/classILLIXR_1_1switchboard.md

[A11]:   api/structILLIXR_1_1data__format_1_1fast__pose__type.md

[A12]:   api/structILLIXR_1_1data__format_1_1pose__type.md

[A13]:   api/structILLIXR_1_1data__format_1_1rgb__depth__type.md

[A14]:   api/structILLIXR_1_1data__format_1_1binocular__cam__type.md

[A15]:   api/structILLIXR_1_1data__format_1_1imu__type.md

[A16]:   api/structILLIXR_1_1data__format_1_1imu__raw__type.md

[A17]:   api/structILLIXR_1_1data__format_1_1imu__integrator__input.md

[A18]:   api/structILLIXR_1_1data__format_1_1monocular__cam__type.md

[A19]:   api/structILLIXR_1_1data__format_1_1camera__data.md

[A20]:   api/structILLIXR_1_1data__format_1_1depth__type.md

[A21]:   api/structILLIXR_1_1data__format_1_1cam__type__zed.md

[A22]:   api/structILLIXR_1_1data__format_1_1ht_1_1ht__frame.md

[A23]:   api/structILLIXR_1_1data__format_1_1mesh__type.md

[A24]:   api/structILLIXR_1_1data__format_1_1vb__type.md

[A25]:   api/structILLIXR_1_1data__format_1_1scene__recon__type.md

[A26]:   api/structILLIXR_1_1data__format_1_1draco__type.md
