---
- author.astro-friedel
---
Deployed four new plugins: hand_tracking, webcam, hand_tracking.viewer, and zed.data_injection, reworked the zed plugin, and a zed_capture executable.
  - hand_tracking: detects and reports the positions of joints and fingertips; if depth information is available, or distances can be calculated by parallax, the reported coordinates are real-world in millimeters, otherwise they are in pixel coordinates
  - webcam: captures images from a webcam and publishes them to the `webcam` topic 
  - hand_tracking.viewer: gui based interface for visualizing the output of the hand_tracker in real time
  - zed.data_injection: reads in data from disk and publishes it as if it were the zed plugin, good for debugging and testing
  - zed: now publishes information about the camera (pixels, field of view, etc.) which is required for some modes of depth sensing; also now publishes the pose corresponding to the current images; these are in addition to the images and IMU information it has always published
  - zed_capture: a standalone executable used to capture the images and poses from the zed camera and write them to disk (currently does not capture depth information); the products of this executable are intended to be used by the zed.data_injection plugin 
