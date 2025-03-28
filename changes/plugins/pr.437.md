---
- author.qinjunj
- author.Jebbly
- author.shg8
---
Deployed the following plugins:
  - tcp_network_backend: a backend for Switchboard using TCP to transmit topics between two connected ILLIXR instances
  - offload_rendering_server: encodes OpenXR frames with FFMPEG and transmits them across the Switchboard network backend
  - offload_rendering_client: decodes frames transmitted from an offload_rendering_server instance and adds decoded frames to a buffer pool
  - openwarp_vk: an implementation of [OpenWarp](https://github.com/Zee2/openwarp), 6-DoF asynchronous reprojection, in ILLIXR
  - lighthouse: uses [libsurvive](https://github.com/collabora/libsurvive) to connect with Valve Index lighthouses for head tracking
native_renderer has been reworked so that external applications can run asynchronously with reprojection.
