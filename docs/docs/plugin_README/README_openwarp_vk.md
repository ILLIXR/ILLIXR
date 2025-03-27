# openwarp_vk

## Summary

`openwarp_vk` is a Vulkan-based translational reprojection service intended for use in the ILLIXR architecture.

## Phonebook Service

`openwarp_vk` is registered as a service in phonebook, conforming to the `timewarp` render pass interface. Three
functions are exposed:

* `setup(VkRenderPass render_pass, uint32_t subpass)` initializes the required Vulkan pipeline and resources given a
  specific render pass and subpass, to which `openwarp_vk` binds to
* `update_uniforms(const pose_type render_pose)` calculates the reprojection matrix given the current pose and the pose
  used to render the frame, and updates the uniform buffer with the reprojection matrix. This must be called before
  `record_command_buffer` is called
* `record_command_buffer(VkCommandBuffer commandBuffer, int left)` records the commands into a given command buffer that
  would perform the reprojection for one eye, for which 1 is left and 0 is right

!!! note

    Note that at the moment, OpenWarp assumes that a reverse depth buffer is being used (as in Unreal Engine, Godot, and our native demo). If you're using an application that uses forward depth, the projection matrices and Vulkan pipeline configuration should be updated accordingly.
