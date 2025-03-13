# native_renderer
## Summary
`native_renderer` utilizes `vkdemo` and `timewarp_vk` to construct a full rendering pipeline. `vkdemo` is used to render the scene, and `timewarp_vk` is used to perform rotational reprojection. This plugin creates the necessary Vulkan resources and targets for `vkdemo` and `timewarp_vk` to render to, and then composites the results into a single image. The resulting image is presented using the Vulkan swapchain provided by `display_vk`.

## Environment Variables

**NATIVE_RENDERER_LOG_LEVEL**: logging level for this plugin, values can be "trace", "debug", "info", "warning", "error", "critical", or "off"
