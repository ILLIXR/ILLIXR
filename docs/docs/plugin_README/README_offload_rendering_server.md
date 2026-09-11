# offload_rendering_server

## Summary

`offload_rendering_server` encodes frames using FFMPEG (on Linux, this is also the default) and NVENC (on Windows and Linux)
and transmits them to the client with the [network_backend][P10]
from [_switchboard_][G10]. In addition to the frame, some additional information is also transmitted, e.g., the pose
used for the rendered frame so that the client can reprojected the decoded frame accordingly.

Relevant environment variables include:
  - ``ILLIXR_USE_DEPTH_IMAGES`` set to non-zero will encode/transmit depth images.
  - ``ILLIXR_OFFLOAD_RENDERING_BITRATE`` sets the encoding bitrate.
  - ``ILLIXR_OFFLOAD_RENDERING_FRAMERATE`` sets the encoding framerate.
  - ``ILLIXR_OFFLOAD_RENDERING_NALU_ONLY`` set to non-zero indicates a Jetson client.

Please refer to the README in [`network backends`][L10] for setting the server and client IP address and port number.

### NVENC Encoding

NVENC encoding uses the onboard encoders of NVIDIA GPU cards. It is intended to be used when the offloading client is an Android device. ILLIXR uses either AV1 or HEVC encoding with this method.
The GeForce RTX 4090 and 5090 cards are known to work with both encodings, while the GeForce RTX 3090 card only supports HEVC encoding.
Using the NVENC encoder requires a few additional components installed, that must be manually done. It requires an NVIDIA GPU
running CUDA 12.8+ or 13.0+. The following need to be installed on your machine:

- CUDA toolkit installed: this can be downloaded from [here](https://developer.nvidia.com/cuda-downloads)
- NVIDIA video codec SDK: this can be downloaded from [here](https://developer.nvidia.com/nvidia-video-codec-sdk/download); this SDK is just some headers, so when you unzip the archive, note the location of the files.

!!! note

    Please be sure to match the version of the SDK's with the CUDA version and NVIDIA driver versions. For the NVIDIA SDK there is a system requirements section on the page stating the minimum driver version. Older version of the NVIDIA SDK can be found under the "Additional Resources" section on the page.

To enable NVENC encoding can be enabled with the `-DUSE_NVENC_ENCODER=ON` CMake argument. The different encoding schemes
can be selected with the `-DUSE_AV1_ENCODING` and `-DUSE_HEVC_ENCODING` CMake arguments, setting one to `ON` and the other `OFF`.

Since this is intended to be used with Android based clients with an OpenXR runtime it is recommended to also use the
`-DUSE_OPENXR_DEFINITIONS=ON` CMake argument.

!!! note

    Note that at the moment, the ``offload_rendering_server`` only supports Monado + OpenXR apps, and does not offload the native demos.

## Windows builds

The Windows version of the ``offload_rendering_server`` has only been tested with an Android client.

[//]: # (- glossary -)

[G10]:  ../glossary.md#switchboard


[//]: # (- plugins -)

[P10]:  ../illixr_plugins.md#tcp_network_backend

[L10]:  README_network_backends.md
