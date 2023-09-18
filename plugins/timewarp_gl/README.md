# timewarp_gl

## Summary

`timewarp_gl` is an OpenGL-based asynchronous rotational reprojection plugin intended for use in the ILLIXR architecture. This plugin implements a rotational reprojection algorithm (i.e. does not reproject position, only rotation). 

## Switchboard connection

`timewarp_gl` subscribes to and publishes to several Switchboard plugs and Phonebook resources.

- The plugin grabs the GL context from Phonebook. This is necessary to share the eyebuffers with the rendering application (or `gldemo`). As this is an OpenGL-based reprojection plugin, it relies on OpenGL resources for the eyebuffers.
- `timewarp_gl` subscribes to the most recent pose published by the system. Currently, as our pose-prediction system is still under development, this is not technically accurate; the final, intended functionality is that the timewarp plugin will sample a pose prediction algorithm through an RPC-like mechanism. As we complete our work on our pose prediction system, this plugin will be modified to use this mechanism.
- `timewarp_gl` also subscribes to the most recent frame published by the system. This frame data also includes the pose that was used to render the frame; this is how the timewarp algorithm calcuates the "diff" to be used to reproject the frame.

## Notes

The rotational reprojection algorithm implemented in this plugin is a re-implementation of the algorithm used by the late Jan Paul van Waveren. His invaluable, priceless work in the area of AR/VR has made our project possible. View his codebase [here.](https://github.com/KhronosGroup/Vulkan-Samples-Deprecated/tree/master/samples/apps/atw)

## Known Issues

As noted above, this plugin currently samples `slow_pose`. This will be changed to sample a `fast_pose` topic through an RPC mechanism. In addition, JMP Van Waveren's algorithm includes a method for warping between two reprojection matrices based on the actual progress of the display controller's "scanline"; this is simply commented out in our code, but can be re-enabled when our pose prediction system comes online.

## Contributions

Contributions are welcome; please raise an issue first, though, as many issues are known and may be a part of our existing internal backlog.