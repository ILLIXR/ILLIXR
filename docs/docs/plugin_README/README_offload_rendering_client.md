# offload_rendering_client

## Summary

`offload_rendering_client` receives encoded frames from the network, and uses a mix of NPPI and FFMPEG, or NVDEC to decode the
frames before updating the buffer pool. Setting the environment variable ``ILLIXR_USE_DEPTH_IMAGES`` to a non-zero value
indicates that depth images are being received, and should thus also be decoded.

Note that there is a known color shift issue (to be fixed), where the decoded frame's colors are slightly different from
the original frame (likely due to the many conversions between YUV and RGBA).

Please refer to the README in the [`network backends`][L11] for setting the server and client IP address and port number.

!!! note "Android Builds"

    If you are using an Android device for the offload rendering client, you must use NVENC encoding on the server. See
    the server [documentation][L10] page for details. The Android decoder supports AV1 and HEVC formats, which must also
    match the encoder settings in the server. The encoder is chosen by setting the appropriate CMake arguments
    in `app/build.gradle` in the `externalNativeBuild` section. The Android code has been tested on a Quest 3 headset and
    may contain some code that is device specific.


[L10]:  README_offload_rendering_server.md

[L11]:  README_network_backends.md
