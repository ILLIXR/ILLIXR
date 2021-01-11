## Monado Integration Dataflow

The integration for [_Monado_][21] in terms of dataflow can be divided into two parts,
    getting pose from ILLIXR, and sending user rendered [_Frame_][28] to ILLIXR.

In Monado, ILLIXR looks like an [_HMD_][23] for Monado, while in ILLIXR,
    Monado looks like a user application as [`gldemo`][20].
After ILLIXR is initialized from Monado, and Monado is registered as a [_Plugin_][25] for ILLIXR,
    most recent [_Pose_][22] information is easy to get from [_Switchboard_][24].

The compositor side of Monado integration with ILLIXR is implemented more subtly.
Original Monado [_Compositor_][26] mostly does [_Distortion Correction_][29]
    and [_Aberration Correction_][30] in a [_Vulkan_][26] back-end compositor.
It also has two client compositors (one for [_OpenGL_][27] applications and another
    for Vulkan application) which pass frame data to the back-end compositor.
ILLIXR integration intercepts the frame at GL client compositor and sends it to Switchboard
    of ILLIXR, which is then used by [`timewarp_gl` component][20].

In order to get a OpenGL frame and use it without copying pixels, ILLIXR needs to get the user
    application GL context.
It is done at OpenXR session creation time, where ILLIXR is initialized.
Note that logically ILLIXR is initialized during OpenXR instance creation,
    or running at the background all the time.
But, since ILLIXR only supports single session at this time,
    and requires a user application gl context upon initialization,
    ILLIXR is initialized at session creation time.

The current ILLIXR integration for Monado is a temporary solution and has some drawbacks caused
    by the concurrent and continued development from both the Monado and ILLIXR projects.
The integration:

1.  Does not use the pose that user application declares to use at rendering
        (using the OpenXR specification).
    This is due to incongruencies with Monado's internal interfaces and representations.
    The pose difference used by [timewarp][20] is computed using the most recent query
        for a pose update.

1.  Cannot submit frame with depth buffer.

1.  Cannot have poses that make use of [OpenXR Spaces][1].
    Raw pose data is instead retrieved from the application's [_SLAM_][32] algorithms.

1.  Does not support controller action.

1.  Only supports GL user-space applications.

1.  User-space applications cannot acquire more than one [_Swap Chain_][31] buffer for each eye
        during the the processing of a frame.

1.  Must initialize ILLIXR during the session initilization.


[//]: # (- References -)

[1]:    https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#spaces

[//]: # (- Internal -)

[20]:   illixr_plugins.md
[21]:   glossary.md#monado
[22]:   glossary.md#pose
[23]:   glossary.md#head-mounted-display
[24]:   glossary.md#switchboard
[25]:   glossary.md#plugin
[26]:   glossary.md#vulkan
[27]:   glossary.md#opengl
[28]:   glossary.md#framebuffer
[29]:   glossary.md#distortion-correction
[30]:   glossary.md#chromatic-abberation-correction
[31]:   glossary.md#swap-chain
[32]:   glossary.md#simultaneous-localization-and-mapping
