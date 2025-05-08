# zed_capture

The `zed_capture` binary is used to generate test data for injection into ILLIXR. The binary is a stand-alone feature and
does not interact with the rest of ILLIXR. It takes the following command line arguments.

| Argument       | Description                                                                                                                   | Default                    |
|----------------|-------------------------------------------------------------------------------------------------------------------------------|----------------------------|
| -d, --duration | The duration to run for in seconds                                                                                            | 10                         |
| -f, --fps      | Frames per second                                                                                                             | 30                         |
| --wc           | The origin of the world coordinate system in relation to the camera. Must be 7 comma separated values x, y, z, w, wx, wy, wz. | 0., 0., 0., 1., 0., 0., 0. |
| -p --path      | The root path to write the data to.                                                                                           | Current working directory  |

`zed_capture` will write out the left and right camera images and the current pose at each frame. Note that it may not
work at the requested fps due to overheads of writing files. This will be addressed in future updates. Note that the
depth information is also not captured currently as [_OpenCV_][G10] does not properly write out cv::Mat objects with float formats.
This will also be addressed in future updates.

Data are written to <path>/'fps' + <fps_value> + 'dur' + <duration_value> (e.g. <path>/fps30_dur10 will contain data 
taken at 30 frames per second with a total run time of 10 seconds, or 300 frames, it will always contain 300 frames
regardless of any slow down due to overheads). This format is compatible with the zed.data_injection plugin.


[//]: # (- glossary -)

[G10]:  ../glossary.md#opencv
