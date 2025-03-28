# Monado Integration Dataflow

The dataflow for the ILLIXR [_Monado_][G11] integration comprises two steps:

1. getting [_pose_][G12] data from ILLIXR,
2. sending a user rendered [_frame_][G18] back to ILLIXR.

In Monado, ILLIXR is recognized as an [_HMD_][G13] for Monado, while in ILLIXR, Monado looks like a user application (
such as [`vkdemo`][P12]). After ILLIXR is initialized from Monado, and Monado is registered as a [_plugin_][G15] for
ILLIXR, the most recent [_pose_][G12] information can be easily obtained via the [_switchboard_][G14].

The [_compositor_][G16] side of Monado integration with ILLIXR is implemented more subtly. The original Monado
compositor primarily performs [_distortion correction_][G19] and [_aberration correction_][G20] in a [_Vulkan_][G16]
back-end compositor. The compositor also has one, Vulkan based,  client compositor which passes frame data to the back-end compositor.


The current ILLIXR integration for Monado is a temporary solution and has some drawbacks caused by the concurrent and
continued development from both the Monado and ILLIXR projects. The integration:

1. Does not use the pose that user application declares to use at rendering (using the OpenXR specification). This is
   due to incongruence with Monado's internal interfaces and representations. The pose difference used by 
   [`timewarp_vk`][P11] is computed using the most recent query for a pose update.

2. Cannot submit frame data with a depth buffer.

3. Cannot have poses that make use of [OpenXR Spaces][E10]. Raw pose data is instead retrieved from the application's 
   [_SLAM_][G22] algorithms.

4. Does not support controller action.

5. User-space applications cannot acquire more than one [_swap chain_][G21] buffer for each eye
        during the processing of a frame.

6. Must initialize ILLIXR during the session initialization.

[//]: # (- glossary -)

[G11]:   ../glossary.md#monado

[G12]:   ../glossary.md#pose

[G13]:   ../glossary.md#head-mounted-display

[G14]:   ../glossary.md#switchboard

[G15]:   ../glossary.md#plugin

[G16]:   ../glossary.md#vulkan

[G17]:   ../glossary.md#opengl

[G18]:   ../glossary.md#framebuffer

[G19]:   ../glossary.md#distortion-correction

[G20]:   ../glossary.md#chromatic-aberration-correction

[G21]:   ../glossary.md#swap-chain

[G22]:   ../glossary.md#simultaneous-localization-and-mapping


[//]: # (- plugins -)

[P11]:   ../illixr_plugins.md#timewarp_vk

[P12]:   ../illixr_services.md#vkdemo


[//]: # (- external -)

[E10]:    https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#spaces
