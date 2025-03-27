# offload_rendering_server

## Summary
`offload_rendering_server` encodes frames using FFMPEG and transmits them to the client with the ``network_backend`` from ``switchboard``. In addition to the frame, some additional information is also transmitted, e.g., the pose used for the rendered frame so that the client can reprojected the decoded frame accordingly. 

Relevant environment variables include:

- `ILLIXR_USE_DEPTH_IMAGES` set to true will encode/transmit depth images.
- `ILLIXR_OFFLOAD_RENDERING_BITRATE` sets the encoding bitrate.
- `ILLIXR_OFFLOAD_RENDERING_FRAMERATE` sets the encoding framerate.
- `ILLIXR_OFFLOAD_RENDERING_NALU_ONLY` set to true indicates a Jetson client.

## Note

Note that at the moment, the ``offload_rendering_server`` only supports Monado + OpenXR apps, and does not offload the native demos.
