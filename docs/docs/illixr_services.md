<div id="service-fauxpose"></div>  
- [`fauxpose`][42]:
    An alternate tracking implementation that simply generates "fast_pose"
    data from a simple mathematical algorithm (circular movement).  The intent
    is for use when debugging other plugins and the developer wants a known
    pose trajectory without having to configure actual tracking.

    Topic details:
    -   *Provides* [`fast_pose_type`][60]
    -   Asynchronously *reads* `time_type` from `vsync_estimate` topic.

<div id="service-pose-prediction"></div>
-   [`pose_prediction`][17]:
    Uses the latest [_IMU_][36] value to predict a [_pose_][37] for a future point in time.
    Implements the `pose_prediction` service,
    so poses can be served directly to other plugins.

    Topic details:

    -   Asynchronously *reads* [`pose_type`][61] on `slow_pose` topic,
        but it is only used as a fallback.
    -   Asynchronously *reads* `imu_raw` on `imu_raw` topic.
    -   Asynchronously *reads* [`pose_type`][61] on `true_pose` topic,
        but it is only used if the client asks for the true pose.
    -   Asynchronously *reads* `time_type` on `vsync_estimate` topic.
        This tells `pose_predict` what time to estimate for.

<div id="service-pose-lookup"></div>
-   [`pose_lookup`][20]:
    Implements the `pose_predict` service, but uses [_ground truth_][33] from the dataset.
    The plugin peeks "into the future" to determine what the exact [_pose_][37] will be at a certain time.

    Topic details:

    -   Asynchronously *reads* `time_point` on `vsync_estimate` topic.
        This tells `pose_lookup` what time to lookup.

<div id="service-vkdemo"></div>
-   [`vkdemo`][56]:
    INFO NEEDED

    Topic details:

    -   *Calls* [`pose_prediction`][57]
    -   *Calls* [`vulkan::display_provider`][30]



[42]:	plugin_README/README_fauxpose.md
[17]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/pose_prediction
[36]:   glossary.md#inertial-measurement-unit
[37]:   glossary.md#pose
[20]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/pose_lookup
[33]:   glossary.md#ground-truth
[37]:   glossary.md#pose
[57]:   illixr_services.md#service-pose-prediction
[61]:   api/html/structILLIXR_1_1data__format_1_1pose__type.html
[60]:   api/html/structILLIXR_1_1data__format_1_1fast__pose__type.html
[30]:   https://github.com/ILLIXR/ILLIXR/tree/master/include/illixr/vk
