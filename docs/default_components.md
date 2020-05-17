# Default Components

Almost all of the XR-related functionality is defined in components that can be overridden. This are
the componets we include by default.

- `offline_imu_cam`: Reads IMU data and images from files on disk, emulating a real sensor on the headset (with emulated timing).
- `ground_truth_slam`: Reads the ground-truth from the same dataset to compare our output against.
- `open_vins`: Runs [OpenVINS][1] on the input, and outputs a the headset's pose.
- `gldemo`: Renders a static scene given the pose from `open_vins`.
- `timewarp_gl`: [Asynchronous reprojection][2]
- `debugview`: Renders a frame for debug information.

[1]: https://docs.openvins.com/
[2]: https://en.wikipedia.org/wiki/Asynchronous_reprojection
