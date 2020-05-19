## Monado Integration Dataflow

The integration for monado in terms of dataflow can be divided into two parts, getting pose from
illixr, and sending user rendered frame to illixr.

In monado, illixr looks like an HMD for monado, while in illixr, monado looks like a user
application as `gldemo`. After illixr is initialized from monado, and monado is registered as a
plugin for illixr, most recent pose information is easy to get from switchboard.

The compositor side of monado integration with illixr is done in more subtle way. Original monado
compositor mostly does distortion correction and aberration correction in a vulkan back-end
compositor. It also has two client compositor, one for opengl app, the other for vulkan app, which
pass frame data to the back-end compositor. Illixr integration intercepts the frame at gl client
compositor and sends it to switchboard of illixr, which is then used by `timewarp_gl` component.

In order to get a opengl frame and use it without copying pixels, illixr needs to get the user
application gl context. It is done at OpenXR session creation time, where illixr is
initialized. Note that logically illixr is initialized during OpenXR instance creation, or running
at the background all the time. But, since illixr only supports single session at this time, and
requires a user application gl context upon initialization, illixr is initialized at session
creation time.

Current illixr integration for monado is a temporary solution and has some downsides because of the
progress from both monado and illixr.

1. It does not use the pose that user application declares to use at rendering (OpenXR
   specification) because of monado internal interfaces. The pose difference used by timewarp comes
   from the last pose query call.

2. It cannot submit frame with depth buffer.

3. The pose from illixr does not vary according to the OpenXR space. It is a raw pose data from slam
   algorithms.

4. No controller action support at all.

5. It only supports gl user applications.

6. User application cannot acquire more than one swapchain image (for each eye) in one frame.

7. Illixr is initialized during session creation time.
