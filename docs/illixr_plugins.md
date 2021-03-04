# ILLIXR plugins

## Default Plugins

-   [`offline_imu_cam`][2]:
    Reads [_IMU_][36] data and images from files on disk, emulating a real sensor on the headset
        (feeds the application input measurements with timing similar to an actual IMU).

    Topic details:

    -   *Publishes* `imu_cam_type` on `imu_cam` topic

-   [`ground_truth_slam`][3]:
    Reads the [_ground truth_][34] from the same dataset as the `offline_imu_cam` plugin.
    This data can be compared against the measurements from `offline_imu_cam` for accuracy.
    Timing information is taken from the `offline_imu_cam` measurements/data.

    Topic details:

    -   *Publishes* `pose_type` on `true_pose` topic
    -   Asynchronously *reads* `imu_cam_type` on `imu_cam` topic

-   [`kimera_vio`][10]:
    Runs Kimera-VIO ([upstream][1]) on the input, and outputs the headset's pose.
    In practice, this publishes a fairly [_slow pose_][37], so [_IMU_][36] integration
        and pose prediction is required to infer a [_fast pose_][37].

    Topic details:

    -   *Publishes* `pose_type` on `slow_pose` topic
    -   *Publishes* `imu_integrator_input` on `imu_integrator_input` topic
    -   Synchronously *reads*/*subscribes* to `imu_cam_type` on `imu_cam` topic

-   [`gtsam_integrator`][12]:
    Integrates over all [_IMU_][36] samples since the last published SLAM pose to provide a
        [_fast pose_][37] every time a new IMU sample arrives using
        the GTSAM library ([upstream][11]).

    Topic details:

    -   *Publishes* `imu_raw_type` on `imu_raw` topic
    -   Synchronously *reads/subscribes* to `imu_cam_type` on `imu_cam` topic
    -   Asynchronously *reads* `imu_integrator_input` on `imu_integrator_input` topic

-   [`pose_prediction`][17]:
    Uses the latest IMU value to predict a pose for a future point in time.
    This implements the `pose_prediction` service (defined in `common`), so it can be called directly from other plugins.

    Topic details:

    -   Asynchronously *reads* `pose_type` on `slow_pose` topic, but it is only used in a fallback
    -   Asynchronously *reads* `imu_raw` on `imu_raw` topic
    -   Asynchronously *reads* `pose_type` on `true_pose` topic, but it is only used if the client asks for the true pose.
    -   Asynchronously *reads* `time_type` on `vsync_estimate` topic.
        This tells `pose_predict` what time to estimate for.

-   [`gldemo`][5]:
    Renders a static scene (into left and right [_eye buffers_][34]) given the pose
        from `pose_prediction`.

    Topic details:

    -   *Publishes* `rendered_frame` on `eyebuffer` topic
    -   Asynchronously *reads* `time_type` on `vsync_estimate` topic

-   [`timewarp_gl`][6]:
    [Asynchronous reprojection][34] of the [_eye buffers_][34]. Timewarp ends just after vsync,
        so it can deduce when the next vsync will be.

    Topic details:

    -   *Calls* `pose_prediction`
    -   Asynchronously *reads* `rendered_frame` on `eyebuffer` topic
    -   *Publishes* `time_type` on `mtp` topic, which is an estimate of the _nominal_ motion-to-photon latency
    -   *Publishes* `time_type` on `warp_frame_age` topic
    -   *Publishes* `type_type` on `vsync_estimate`
    -   *Publishes* `hologram_input` on `hologram_in` topic
    -   *Publishes* `texture_pose` on `texture_pose` topic if `ILLIXR_OFFLOAD_ENABLE` is set in the env.

-   [`debugview`][7]: Renders a frame for debug information.

    Topic details:

    -   *Calls* `pose_prediction`
    -   Asynchronously *reads* `imu_raw` on `imu_raw` topic

-   [`audio_pipeline`][8]:
    Launches a thread for [Binaural][19] recording and one for binaural playback.
    This is not yet routed to the speakers or microphone, but it still does a compute workload representative of a real system.
    By default this is disabled.

    Topic details:

    -   *Calls* `pose_prediction`

Below this point, we will use Switchboard terminology.
Read the [API documentation on _Switchboard_][32] for more information.

<img
    src="../dataflow.png"
    alt ="ILLIXR dataflow graph, showing switchboard communication"
    style="width: 400px;"
/>

-   In the above figure, rectangles are plugins, cylinders are topics
        (the graph is bipartitioned between these two groups).

-   Solid arrows from plugins to topics represent publishing.

-   Solid arrows from topics to plugins represent synchronous reading.
    They take some action for _every_ event which gets published on the topic.

-   Solid arrows from topics to plugins represent asynchronous reading.
    They need to know just the _latest_ event on their topic.

-   Imagine the topic as a trough filing with events from its publisher,
        being drained by its synchronous readers (AKA subscribers),
        while asynchronous readres just skim from the top.

See [Writing Your Plugin][30] to extend ILLIXR.


## Other Supported Plugins
ILLIXR supports additional plugins to replace some of the default plugins.

-   [`hologram`][9]:
    Adapts the eyebuffer for use on a holographic display.
    By default this is disabled, since it requires an NVIDIA GPU.

    Topic details:

    -   Asynchronously *reads* `hologram_input` on `hologram_in` topic.
        Hologram is too slow to run for every input, so it is modeled as an async reader which can drop inputs.
    -   *Publishes* `hologram_output` on `hologram_out` topic

-   [`open_vins`][4]:
    This is an alternate SLAM ([upstream][18]) that uses a MSCKF
        (Multi-State Constrained Kalman Filter) to determine poses via camera/IMU.

    Topic details:

    -   Same interface as `Kimera-VIO`

-   [`rk4_integrator`][16]:
    Integrates over all IMU samples since the last published SLAM pose to
        provide a fast-pose everytime a new IMU sample arrives using RK4 integration.

    Topic details:

    -   Same interface as `gtsam_integrator`

-   [`pose_lookup`][20]:
    Implements the `pose_predict` service, but uses ground-truth from the dataset.
    It can look "into the future" and see what the pose will be, exactly, at a certain time.

    Topic details:

    -   Asynchronously *reads* `time_type` on `vsync_estimate`. This tells `pose_lookup` what time to lookup.

-   [`offload_data`][21]:
    Writes warped frames to disk.

    Topic details:

    -   Asynchronously *reads* `texture_image` on `texture_image`

-   [`zed`][22]:
    Reads images and IMU measurements from the [ZED Mini][24].
    Unlike `offline_imu_cam`, `zed` additionally has RGB and depth data.
    Note that this uses two threads, one for the camera and one for the IMU.

    Topic details:

    -   *Publishes* `imu_cam_type` on `imu_cam`
    -   *Publishes* `rgb_depth_type` on `rgb_depth`

-   [`realsense`][23]:
    Reads images and IMU measurements from the [Intel Realsense][25].

    Topic details:

    -   Same interface as `zed`.

See [Building ILLIXR][31] for more information on adding plugins to a config file.


[//]: # (- References -)

[1]:    https://github.com/MIT-SPARK/Kimera-VIO
[2]:    https://github.com/ILLIXR/ILLIXR/tree/master/offline_imu_cam
[3]:    https://github.com/ILLIXR/ILLIXR/tree/master/ground_truth_slam
[4]:    https://github.com/ILLIXR/open_vins
[5]:    https://github.com/ILLIXR/ILLIXR/tree/master/gldemo
[6]:    https://github.com/ILLIXR/ILLIXR/tree/master/timewarp_gl
[7]:    https://github.com/ILLIXR/ILLIXR/tree/master/debugview
[8]:    https://github.com/ILLIXR/audio_pipeline/tree/illixr-integration
[9]:    https://github.com/ILLIXR/HOTlab/tree/illixr-integration
[10]:   https://github.com/ILLIXR/Kimera-VIO
[11]:   https://gtsam.org/
[12]:   https://github.com/ILLIXR/ILLIXR/tree/master/gtsam_integrator
[16]:   https://github.com/ILLIXR/ILLIXR/tree/master/rk4_integrator
[17]:   https://github.com/ILLIXR/ILLIXR/tree/master/pose_prediction
[18]:   https://docs.openvins.com
[19]:   https://en.wikipedia.org/wiki/Binaural_recording
[20]:   https://github.com/ILLIXR/ILLIXR/tree/master/pose_lookup
[21]:   https://github.com/ILLIXR/ILLIXR/tree/master/offload_data
[22]:   https://github.com/ILLIXR/ILLIXR/tree/master/zed
[23]:   https://github.com/ILLIXR/ILLIXR/tree/master/realsense
[24]:   https://www.stereolabs.com/zed-mini/
[25]:   https://www.intelrealsense.com/depth-camera-d435/

[//]: # (- Internal -)

[30]:   writing_your_plugin.md
[31]:   building_illixr.md
[32]:   api/html/classILLIXR_1_1switchboard.html
[33]:   glossary.md#ground-truth
[34]:   glossary.md#eye-buffers
[35]:   glossary.md#asynchronous-reprojection
[36]:   glossary.md#inertial-measurement-unit
[37]:   glossary.md#pose
