# debugview

## Summary

`debugview` is a simple debugging view for the ILLIXR system. By subscribing to several Switchboard endpoints, many important datapoints can be observed to help debug issues with the runtime. The debug view shows the current calculated SLAM pose and the (optional) ground truth pose read from the ground truth dataset as 3D headset models drawn on a backdrop scene. In addition, camera and IMU data is also visible, with the stereoscopic onboard camera views shown in one of the windows. Various offsets can be applied to the generated poses to aid in viewing. Dear ImGUI is used for displaying data and providing an interactive interface.

## Switchboard connection

- `debugview` subscribes to the slow-pose, which represents the latest pose published by the SLAM/IMU system.
- In the future, `debugview` will also query for the most up-to-date predicted pose through an RPC-like query system. This is not currently in our release version; but is in development and will be added soon. The "fast pose" referenced in this plugin just samples the `slow_pose` instead.
- `debugview` also is synchronously dependent on the `imu_cam` topic, as the stereoscopic camera views are displayed in the debug window. In ILLIXR, "synchronous dependencies" are implemented as scheduled "handlers" that are executed from a shared thread pool; this handler is executed every time a fresh "packet" is available from the `imu_cam` topic. Given that this is a separate thread from the main graphics thread, this `imu_cam` packet must be saved/cached for later, when the grahpics thread renders the next frame. This is a good example to follow when writing your own multithreaded components that use both synchronous and asynchronous dependencies, and need to share information across dependency boundaries.

## Notes

`debugview` will be under heavy development in the near-term. Pull requests may or may not be accepted, due to the frequency of rapid internal changes.

## Known Issues

Currently, the poses returned from the SLAM system have an incorrect initial rotation, as the poses are returned in an incorrect frame of reference. Thus, the headsets may appear to be tilted incorrectly; the button "calculate new orientation offset" will reset the orientation, such that the current orientation is considered the "neutral" orientation. In a future update, the correct frame of reference/transform matrix will be applied and the pose will accurately reflect the actual head position.

## Contributions

Contributions are welcome; please raise an issue first, though, as many issues are known and may be a part of our existing internal backlog.