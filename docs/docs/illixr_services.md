# ILLIXR Services

This page details the structure of ILLIXR's [_services_][58] and how they interact with each other.


## fauxpose

An alternate tracking implementation that simply generates "fast_pose"
data from a simple mathematical algorithm (circular movement).  The intent
is for use when debugging other plugins and the developer wants a known
pose trajectory without having to configure actual tracking.

Topic details:
-   *Provides* [`fast_pose_type`][60]
-   Asynchronously *reads* `time_type` from `vsync_estimate` topic.

&nbsp;&nbsp;[**Details**][D42]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C42]

## pose_prediction

Uses the latest [_IMU_][36] value to predict a [_pose_][37] for a future point in time.
Implements the `pose_prediction` service,
so poses can be served directly to other plugins.

Topic details:

-   Asynchronously *reads* [`pose_type`][61] on `slow_pose` topic, but it is only used as a fallback.
-   Asynchronously *reads* `imu_raw` on `imu_raw` topic.
-   Asynchronously *reads* [`pose_type`][61] on `true_pose` topic, but it is only used if the client asks for the true pose.
-   Asynchronously *reads* `time_type` on `vsync_estimate` topic. This tells `pose_predict` what time to estimate for.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C44]

## pose_lookup

Implements the `pose_predict` service, but uses [_ground truth_][33] from the dataset.
The plugin peeks "into the future" to determine what the exact [_pose_][37] will be at a certain time.

Topic details:

-   Asynchronously *reads* `time_point` on `vsync_estimate` topic. This tells `pose_lookup` what time to lookup.

&nbsp;&nbsp;**Details**&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C43]

## vkdemo

INFO NEEDED

Topic details:

-   *Calls* [`pose_prediction`][57]
-   *Calls* [`vulkan::display_provider`][30]

&nbsp;&nbsp;[**Details**][D45]&nbsp;&nbsp;&nbsp;&nbsp;[**Code**][C45]

{! include-markdown "dataflow.md" !}

[D42]:	plugin_README/README_fauxpose.md
[D45]:	plugin_README/README_vkdemo.md


[C42]:  https://github.com/ILLIXR/ILLIXR/tree/master/plugins/fauxpose
[C43]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/pose_lookup
[C44]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/pose_prediction
[C45]:   https://github.com/ILLIXR/ILLIXR/tree/master/plugins/vkdemo


[36]:   glossary.md#inertial-measurement-unit
[37]:   glossary.md#pose
[33]:   glossary.md#ground-truth
[37]:   glossary.md#pose
[57]:   illixr_services.md#pose_prediction
[58]:   glossary.md#service
[61]:   api/structILLIXR_1_1data__format_1_1pose__type.md
[60]:   api/structILLIXR_1_1data__format_1_1fast__pose__type.md
[30]:   https://github.com/ILLIXR/ILLIXR/tree/master/include/illixr/vk
