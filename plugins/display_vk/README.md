# display_vk
## Summary
`display_vk` implements the service interface `display_sink`, which sets up a windowing backend (using GLFW in this case), initializes Vulkan, and creates a swapchain. If available, Vulkan validation layers are enabled. This plugin is only required when running the native target. For Monado, the `display_sink` service is provided by the Monado compositor.