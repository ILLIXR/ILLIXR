# rgb_passthrough

## Summary

The `rgb_passthrough` plugin serves as the AR replacement for vkdemo by using a digital passthrough. `rgb_passthrough` subscribes to several Switchboard plugins, gets the rgb images from camera, and copies them to VkImage. This is currently used through native_renderer which calls the timewarp on top of the camera RGB image. In future, it might be a good idea to decouple native_renderer for VR, while create a `passthrough_renderer` for AR/MR. This future passthhrough renderer should be able to receive app render outputs, composit them on RGB, and feed to reprojection (Time/Space warp). In the future we would also like to use OpenWarp instead of timewarp, since most industry headsets use 6DOF reprojection.

## Phonebook Service
`rgb_passthrough` is registered as a service in phonebook, conforming to the `app` render pass interface. Perhaps, in the future, this should be implemented on the lines of native_renderer. Four functions are exposed:
* `setup(VkRenderPass render_pass, uint32_t subpass)` initializes the required staging buffers and memory allocations required to copy camera images (`cv::Mat`) to `VkImage`. 
* `update_uniforms(const pose_type render_pose)` For rendering apps, it is used to update the pose. But here, we don't render. We only use this to get the latest camera frames from switchboard and flip them since openCV and Vulkan have oppsite coordinate system for Y axis/rows.
* `record_command_buffer(VkCommandBuffer commandBuffer, int eye)` Not implemented (Used in VR rendering apps)
* `record_command_buffer(VkCommandBuffer commandBuffer, VkImage image , int eye)` records the commands into a given command buffer that would perform the copying of camera image to the VkImage received in this function. This works only for one eye, and receives the eye number as a param. 0 is left eye and 1 is right. To achieve stereoscopic passthrough, the function is called twice, once for each eye.
* `get_app_type()` Additional information used in native_renderer. Since native_renderer deals with both VR apps, and passthrough. It must be able to identify which app is running and decide buffer sizes and render_pass vs. no_render_pass accordingly.
* `virtual void destroy()` cleans up the resources allocated by `rgb_passthrough`. Currently, this part is not yet implemented.