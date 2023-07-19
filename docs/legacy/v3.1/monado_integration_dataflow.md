## Monado Integration Dataflow

The dataflow for the ILLIXR [_Monado_][21] integration comprises two steps:
1.  getting pose data from ILLIXR,
    and
1.  sending a user rendered [_frame_][28] back to ILLIXR.

In Monado, ILLIXR is recognized as an [_HMD_][23] for Monado, while in ILLIXR,
    Monado looks like a user application (such as [`gldemo`][20]).
After ILLIXR is initialized from Monado, and Monado is registered as a [_plugin_][25] for ILLIXR,
    the most recent [_pose_][22] information can be easily obtained via [_Switchboard_][24].

The [_compositor_][26] side of Monado integration with ILLIXR is implemented more subtly.
The original Monado compositor primarily performs [_distortion correction_][29]
    and [_aberration correction_][30] in a [_Vulkan_][26] back-end compositor.
The compositor also has two client compositors (one for [_OpenGL_][27] applications and another
    for Vulkan applications) which pass frame data to the back-end compositor.
ILLIXR integration intercepts the frame at GL client compositor and sends it to Switchboard
    of ILLIXR, which is then used by [`timewarp_gl` component][20].

To get an OpenGL frame and use it without copying pixels, ILLIXR needs to get the user
    application GL context.
This is done at OpenXR session creation time, where ILLIXR is initialized.
Note that, logically, ILLIXR is initialized during OpenXR instance creation,
    or is otherwise running in the background all the time.
Currently, ILLIXR is initialized at session creation time, since ILLIXR only supports single
    OpenXR session, and requires a user application GL context upon initialization,

The current ILLIXR integration for Monado is a temporary solution and has some drawbacks caused
    by the concurrent and continued development from both the Monado and ILLIXR projects.
The integration:

1.  Does not use the pose that user application declares to use at rendering
        (using the OpenXR specification).
    This is due to incongruencies with Monado's internal interfaces and representations.
    The pose difference used by [timewarp][20] is computed using the most recent query
        for a pose update.

1.  Cannot submit frame data with a depth buffer.

1.  Cannot have poses that make use of [OpenXR Spaces][1].
    Raw pose data is instead retrieved from the application's [_SLAM_][32] algorithms.

1.  Does not support controller action.

1.  Only supports GL user-space applications.

1.  User-space applications cannot acquire more than one [_swap chain_][31] buffer for each eye
        during the the processing of a frame.

1.  Must initialize ILLIXR during the session initialization.


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
