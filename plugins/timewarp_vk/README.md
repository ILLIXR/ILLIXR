# timewarp_vk

## Summary

`timewarp_vk` is an Vulkan-based rotational reprojection service intended for use in the ILLIXR architecture. This plugin implements a rotational reprojection algorithm (i.e. does not reproject position, only rotation). 

## Phonebook Service

`timewarp_vk` is registered as a service in phonebook, conforming to the `timewarp` render pass interface. Three functions are exposed:

* `setup(VkRenderPass render_pass, uint32_t subpass)` initializes the required Vulkan pipeline and resources given a specific render pass and subpass, to which `timewarp_vk` binds to
* `update_uniforms(const pose_type render_pose)` calculates the reprojection matrix given the current pose and the pose used to render the frame, and updates the uniform buffer with the reprojection matrix. This must be called before `record_command_buffer` is called
* `record_command_buffer(VkCommandBuffer commandBuffer, int left)` records the commands into a given command buffer that would perform the reprojection for one eye, for which 1 is left and 0 is right

## Notes

The rotational reprojection algorithm implemented in this plugin is a re-implementation of the algorithm used by the late Jan Paul van Waveren. His invaluable, priceless work in the area of AR/VR has made our project possible. View his codebase [here.](https://github.com/KhronosGroup/Vulkan-Samples-Deprecated/tree/master/samples/apps/atw)