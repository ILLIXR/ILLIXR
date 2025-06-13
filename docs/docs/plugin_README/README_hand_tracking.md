# Hand Tracking

## Summary

`hand_tracking` and `hand_tracking_gpu` are plugins which detects hands in an image. The plugins integrate the Mediapipe
hand landmark detection[E11] algorithm into the ILLIXR framework. The only difference between `hand_tracking` and
`hand_tracking_gpu` are where the data are processed: CPU vs GPU. Their operation and interface are identical. (
Currently the GPU version is a work in progress.)

## Switchboard connection

The `hand_tracking` plugin subscribes to the webcam to get the input images to process. Future development will allow
this plugin to subscribe to other input types dynamically, depending on the users' needs. The plugin utilizes the
following data structures

- **rect**: representation of a rectangle
    - **x_center**: x-coordinate of the rectangle center
    - **y_center**: y-coordinate of the rectangle center
    - **width**: width of the rectangle
    - **height**: height of the rectangle
    - **rotation**: rotation angle of the rectangle in radians
    - **normalized**: boolean indicating the units; `true` indicates normalized units [0..1] of the input image, `false`
      indicates pixel units
    - **valid**: boolean indicating whther the object is valid
- **point**: representation of a 3D point
    - **x**: x-coordinate
    - **y**: y-coordinate
    - **z**: z-coordinate (not an absolute distance, but a measure of the point's depth relative to other points)
    - **normalized**: boolean indicating the units; `true` indicates normalized units [0..1] of the input image, `false`
      indicates pixel units
    - **valid**: boolean indicating whther the object is valid

!!! note

    All coordinates in these data are normalized to the input image size

The plugin published an `ht_frame` which contains the following data

- **detections**: the raw information produced by the mediapipe code, x and y coordinates are normalized to the input
  image size, and z has no meaning
    - **left_palm**: rect which encloses the left palm, if detected
    - **right_palm**: rect which encloses the right palm, if detected
    - **left_hand**: rect which encloses the entire left hand, if detected
    - **right_hand**: rect which encloses the entire right hand, if detected
    - **left_confidence**: float indicating the detection confidence of the left hand [0..1]
    - **right_confidence**: float indicating the detection confidence of the right hand [0..1]
    - **left_hand_points**: vector of the 21 point objects, one for each hand landmark, from the left hand, if detected
    - **right_hand_points**: vector of the 21 point objects, one for each hand landmark, from the right hand, if detected
    - **img**: cv::Mat in `CV_8UC4` format (RGBA), representing the detection results
- **hand_positions**: map of detected points for each hand, if depth cannot be determined then the value for that axis
  will have no meaning (axis will depend on the coordinate reference frame), coordinate origin is defined by the user at
  startup
- **hand_velocities**: map of velocities for each detected point for each hand, requires that depth is known or
  calculated, and the last iteration of the code produced valid results, the units **unit** per second
- **offset_pose**: a pose, that when removed from each point, will give coordinates relative to the camera
- **reference**: the coordinate reference space (e.g. left hand y up)
- **unit**: the units of the coordinate system

!!! info

    The **detections** may be removed or re-worked in future releases 

Each vector of hand points contains 21 items which reference the following (
from https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker)

![hand_landmark_map](../images/hand_landmark_reference.png)

The `landmark_points` `enum` can be used to reference the individual points in each vector

``` Cpp
ht.left_hand_points[THUMB_TIP]
```

will get the `point` for the tip of the left thumb.

## Environment Variables

The hand tracking utilizes the following environment/yaml file variables to control its processing:

- **HT_INPUT**: the type of images to be fed to the plugin. Values are
    - **zed**
    - **cam** (for typical cam_type/binocular images)
    - **webcam** (single image)
- **HT_INPUT_TYPE**: descriptor of what image(s) to use. Values are
    - **LEFT** - only use the left eye image from an input pair
    - **SINGLE** - same as LEFT
    - **RIGHT** - only use the right eye image from an input pair
    - **MULTI** - use both input images
    - **BOTH** - same as MULTI
    - **RGB** - only a single input image
- **WCF_ORIGIN**: the origin pose of the world coordinate system as a string of three, four, or seven numbers. The
  numbers should be comma separated with no spaces.
    - **x,y,z** - three coordinate version, representing the position of the origin pose (quaternion will be 1,0,0,0)
    - **w,wx,wy,wz** - four coordinate version, representing the quaternion of the origin pose (position will be 0,0,0)
    - **x,y,z,w,wx,wy,wz** - seven coordinate version, representing the full origin pose

## Helper plugins

There are two additional plugins which are designed to aid in debugging the `hand_tracking` plugin.

### Viewer

The `hand_tracking.viewer` plugin subscribes to the output of the `hand_tracking` plugin and displays the results, both
graphically and in tabular format.

### Webcam

The `webcam` plugin can feed single frame images to the hand tracking plugin.

## OpenXR

The hand tracking plugin can be built with an OpenXR interface. To build the interface add `-DBUILD_OXR_INTERFACE=ON` to
your cmake command line. The interface itself is in libopenxr_illixr_ht.so and is designed to be an [API Layer][E12]. It
installs a json file in the user's `.local` directory and is automatically detected by libopenxr_loader.so To use the
layer you will need both an OpenXR application and runtime. This code is known to be compatible with the Monado runtime,
and should be compatible with others. Currently, the hand tracking must receive that data from ILLIXR, but as an API
Layer the resulting calculations can be retrieved via OpenXR API calls.

## API

<<<<<<< HEAD
The hand tracking API can be found [here][3].

[//]: # (- References -)
[1]: https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker
[2]: https://registry.khronos.org/OpenXR/specs/1.0/loader.html#openxr-api-layers
[3]: https://illixr.github.io/hand_tracking/
=======
The hand tracking API can be found [here][E13].

[//]: # (- external -)

[E11]: https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker

[E12]: https://registry.khronos.org/OpenXR/specs/1.0/loader.html#openxr-api-layers

[E13]: https://illixr.github.io/hand_tracking/
>>>>>>> master
