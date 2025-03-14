### Webcam

The `webcam` plugin opens the attached webcam, captures images at 30 frames per second, and publishes them on the `webcam`
topic as `monocular_cam_type`. The images are in cv::Mat format with color order RGB. Depending on your needs, you may 
need to flip the images over the vertical axis. This plugin is intended mostly for testing and debugging other plugins.

```C++
    cv::flip(image, image, 1);
```
