# openxr_interface

## Summary

The `openxr_interface` plugin is designed to work in conjunction with [`offload_rendering_client`][1] on Android systems.
The plugin connects to the Android devices own OpenXR runtime. It then reads the current head pose, hand poses, and hand interactions
from the device and publishes them to the network at a rate of 120Hz. Additionally, it also retrieves decoded frames from the `offload_rendering_client` and submits them to the OpenXR swapchain for display.



!!! note

    This plugin was designed to work on, and has only been tested on, the Quest 3. The only Quest 3 specific feature being used is spacewarp, so to run on other Android devices with OpenXR runtimes you can just comment that out. The spacewarp interface has not been heavily tested as it can be very difficult to force the conditions where spacewarp will run on the Quest 3. You may need to change the cadence of the pose retrieval and frames to better match your device.









[1]: README_offload_rendering_client.md
