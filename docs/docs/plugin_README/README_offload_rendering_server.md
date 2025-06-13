# offload_rendering_server

## Summary

`offload_rendering_server` encodes frames using FFMPEG and transmits them to the client with the [network_backend][P10]
from [_switchboard_][G10]. In addition to the frame, some additional information is also transmitted, e.g., the pose
used for the rendered frame so that the client can reprojected the decoded frame accordingly.

Relevant environment variables include:
  - ``ILLIXR_USE_DEPTH_IMAGES`` set to non-zero will encode/transmit depth images.
  - ``ILLIXR_OFFLOAD_RENDERING_COLOR_BITRATE`` sets the color encoding bitrate.
  - ``ILLIXR_OFFLOAD_RENDERING_DEPTH_BITRATE`` sets the depth encoding bitrate, if depth encoding is enabled.
  - ``ILLIXR_OFFLOAD_RENDERING_FRAMERATE`` sets the encoding framerate.
  - ``ILLIXR_OFFLOAD_RENDERING_NALU_ONLY`` set to non-zero indicates a Jetson client.

!!! note

    Note that at the moment, the ``offload_rendering_server`` only supports Monado + OpenXR apps, and does not offload the native demos.

[//]: # (- glossary -)

[G10]:  ../glossary.md#switchboard


[//]: # (- plugins -)

[P10]:  ../illixr_plugins.md#tcp_network_backend
