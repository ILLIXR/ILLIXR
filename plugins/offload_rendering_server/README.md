# offload_rendering_server

## Summary
`offload_rendering_server` encodes frames using FFMPEG and transmits them to the client with the ``network_backend`` from ``switchboard``. In addition to the frame, some additional information is also transmitted, e.g., the pose used for the rendered frame so that the client can reprojected the decoded frame accordingly.