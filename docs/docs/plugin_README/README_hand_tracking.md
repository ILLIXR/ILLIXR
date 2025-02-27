# hand_tracking

## Summary

`hand_tracking` and `hand_tracking_gpu` are plugins which detects hands in an image. The plugins integrate the Mediapipe hand landmark detection[1] algorithm into the ILLIXR framework. The only difference between `hand_tracking` and `hand_tracking_gpu` are where the data are processed: CPU vs GPU. Their operation and interface are identical.

## Switchboard connection

The `hand_tracking` plugin subscribes to the webcam to get the input images to process. Future development will allow this plugin to subscribe to other input types dynamically, depending on the users' needs. The plugin utilizes the following data structures

  - **rect**: representation of a rectangle
    - **x_center**: x-coordinate of the rectangle center
    - **y_center**: y-coordinate of the rectangle center
    - **width**: width of the rectangle
    - **height**: height of the rectangle
    - **rotation**: rotation angle of the rectangle in radians
    - **normalized**: boolean indicating the units; `true` indicates normalized units [0..1] of the input image, `false` indicates pixel units
    - **valid**: boolean indicating whther the object is valid
  - **point**: representation of a 3D point
    - **x**: x-coordinate
    - **y**: y-coordinate
    - **z**: z-coordinate (not an absolute distance, but a measure of the point's depth relative to other points)
    - **normalized**: boolean indicating the units; `true` indicates normalized units [0..1] of the input image, `false` indicates pixel units
    - **valid**: boolean indicating whther the object is valid

NOTE:
All coordinates in these data are normalized to the input image size
    
The plugin published an `ht_frame` which contains the following data

  - **detections**: the raw information produced by the mediapipe code, x and y coordinates are normalized to the input image size, and z has no meaning
    - **left_palm**: rect which encloses the left palm, if detected
    - **right_palm**: rect which encloses the right palm, if detected
    - **left_hand**: rect which encloses the entire left hand, if detected
    - **right_hand**: rect which encloses the entire right hand, if detected
    - **left_confidence**: float indicating the detection confidence of the left hand [0..1]
    - **right_confidence**: float indicating the detection confidence of the right hand [0..1]
    - **left_hand_points**: vector of the 21 point objects, one for each hand landmark, from the left hand, if detected
    - **right_hand_points**:vector of the 21 point objects, one for each hand landmark, from the right hand, if detected 
    - **img**: cv::Mat in `CV_8UC4` format (RGBA), representing the detection results
  - **hand_positions**: map of detected points for each hand, if depth cannot be determined then the value for that axis will have no meaning (axis will depend on the coordinate reference frame), coordinate origin is defined by the user at startup
  - **hand_velocities**: map of velocities for each detected point for each hand, requires that depth is known or calculated, and the last iteration of the code produced valid results, the units **unit** per second
  - **offset_pose**: a pose, that when removed from each point, will give coordinates relative to the camera
  - **reference**: the coordinate reference space (e.g. left hand y up)
  - **unit**: the units of the coordinate system

NOTE:
The **detections** may be removed or re-worked in future releases 

Each vector of hand points contains 21 items which refencence the following (from https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker)

![hand_landmark_map](../images/hand_landmark_reference.png)

The `landmark_points` `enum` can be used to reference the individual points in each vector

```C++
ht.left_hand_points[THUMB_TIP]
```

will get the `point` for the tip of the left thumb.

## Helper plugins

There are two additional plugins which are designed to aid in debugging the `hand_tracking` plugin.

### Viewer

The `hand_tracking.viewer` plugin subscribes to the output of the `hand_tracking` plugin and displays the results, both graphically and in tabular format.

### Webcam

The `webcam` plugin can feed single frame images to the hand tracking plugin.

[//]: # (- References -)
[1]: https://ai.google.dev/edge/mediapipe/solutions/vision/hand_landmarker
