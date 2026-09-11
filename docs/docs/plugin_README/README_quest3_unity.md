# Quest 3 Unity Plugin

This plugin captures RGB and depth images from the Quest 3 headset. These images are encoded using HEVC and sent over the
network to the server. The following environment variables can be used to control the behavior of the plugin:

- `ILLIXR_CAPTURE_FPS`: The frame rate of the RGB capture. Default is 2.
- `ILLIXR_ENCODER_BITRATE_BPS`: The bit rate of the encoder. Default is 5'000'000.
- `ILLIXR_CAPTURE_MAX_DEPTH`: The maximum depth value to use. Default os 0.0, indicating infinite max depth.

## RGB images

The RGB images are captured directly from the left eye camera using standard Android camera APIs. The pose of the camera
is also captured and attached to the image before transmission.

## Depth images

The depth images cannot be directly accessed from the Quest 3. Instead, they are retrieved using OpenXR calls through
the Unity app's OpenXR interface. This part of the plugin relies on C# code in the [SemanticXR][L10] repository.

[L10]:  https://github.com/ILLIXR/SemanticXR/blob/illixr/integration
