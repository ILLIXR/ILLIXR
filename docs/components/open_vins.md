# OpenVINS

We chose [OpenVINS](https://github.com/rpng/open_vins/) as the VIO (Visual Inertial Odometry) and SLAM (Simultaneus Localization and Mapping) component for the ILLIXR benchmark because of its performance and also its modularity.

## Brief Algorithmic Overview
The core of OpenVINS is an MSCKF (Multi-state Constrained Kalman Filter) based VIO system. The current version of OpenVINS doesn't contain loop closure to correct slight drift over time but this feature will be added in the future.
OpenVINS works with both monocular and stereo camera but also requires an IMU (Inertial Measurement Unit) in order to perform accurate pose estimation. The VIO system works by tracking points between the current and previous camera frames and computes the rotation and translation of the camera based off of those points. OpenVINS then also integrates all of the IMU readings that came between the two camera readings and integrates them forward to estimate the pose based off of the IMU. Finally using the features detected in the images and the estimated camera and IMU poses, OpenVINS uses its MSCKF to estimate a more accurate pose.

[This paper has more detail on the OpenVINS system and its performance on various datasets.](http://udel.edu/~pgeneva/downloads/papers/c10.pdf)
