---
- author.astro-friedel
- author.Madhuparna04
- pr.485
- pr.486
- pr.487
- pr.489
- pr.491
- pr.492
- pr.493
- pr.494
---
This work introduces Android support to the main ILLIXR repo. This work is based on previous work by [Madhuparna04](https://github.com/Madhuparna04/illixr-native-activity).

Adding Android support to the services plugins. Two new, Android-only, plugins were added to manage the graphics interface.

This work merges Android-specific code into the gldemo plugin.

This work updates the offline_can and offline_imu plugins to work on Android devices.

This work updates the tcp_network_backend and udp_network_backend plugins to work on Adnroid devices.

This work updates the rk4_integrator and timewarp_gl plugins to work on Android devices.

This work updates the offload_rendering_client plugin to work on Android systems. It supports HEVC and AV1 encoding schemes, but not H264 (this is due to hardware limitations on our test platform)


