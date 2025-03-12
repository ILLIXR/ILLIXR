# vkdemo

## Summary

The `vkdemo` plugin serves as a stand-in for an actual application when ILLIXR is run as a standalone application without an actual OpenXR application. `vkdemo` will subscribe to several Switchboard plugs, render a simple, hard-coded 3D scene (in fact, the same 3D scene that is included in the `debugview` plugin) and publish the results to the Switchboard API. `vkdemo` is intended to be as lightweight as possible, serving as a baseline debug "dummy application". During development, it is useful to have some content being published to the HMD display without needing to use the full OpenXR interface; `vkdemo` fills this requirement.

## Phonebook Service
`vkdemo` is registered as a service in phonebook, conforming to the `app` render pass interface. Three functions are exposed:
* `setup(VkRenderPass render_pass, uint32_t subpass)` initializes the required Vulkan pipeline and resources given a specific render pass and subpass, to which `vkdemo` binds to
* `update_uniforms(const pose_type render_pose)` updates the uniform buffer with the given pose, which is used to render the scene. This must be called before `record_command_buffer` is called.
* `record_command_buffer(VkCommandBuffer commandBuffer, int eye)` records the commands into a given command buffer that would perform the rendering for one eye, for which 0 is left and 1 is right. To achieve stereoscopic rendering, the function is called twice, once for each eye.
* `virtual void destroy()` cleans up the resources allocated by `vkdemo`. Currently, this part is not yet implemented.