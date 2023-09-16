# gldemo

## Summary

The `gldemo` plugin serves as a stand-in for an actual application when ILLIXR is run as a standalone application without an actual OpenXR application. `gldemo` will subscribe to several Switchboard plugs, render a simple, hard-coded 3D scene (in fact, the same 3D scene that is included in the `debugview` plugin) and publish the results to the Switchboard API. `gldemo` is intended to be as lightweight as possible, serving as a baseline debug "dummy application". During development, it is useful to have some content being published to the HMD display without needing to use the full OpenXR interface; `gldemo` fills this requirement. As an important note, `gldemo` does not render stereoscopically; the two eye renders are rendered from the same position. This may be updated to render stereoscopically in the future, but is not seen as a critical feature as this is generally intended as a debugging tool.

## Switchboard connection

`gldemo` subscribes to and publishes to several Switchboard plugs. Most notably, `gldemo` subscribes to the `fast_pose` plug, which (ideally) represents the most recent extrapolated pose. This connection represents an area of active development in ILLIXR, as we are replacing the pose subscription with an RPC-like proper pose prediction system. As of the time of writing, `fast_pose` is functionally identical to the `slow_pose` published by the SLAM system, but this will change when proper pose extrapolation is implemented. `gldemo` also pulls the correct graphics context from Phonebook.

`gldemo` publishes the rendered eyebuffers to the Switchboard system as well, using whichever eyebuffer format has been selected with the `USE_ALT_EYE_FORMAT` compile-time macro. The alternative eye format is more similar to the format used by Monado/OpenXR, and is more fully explained by the code comments.

## Notes

`gldemo` does not pretend to be an OpenXR application; it does not use the OpenXR API, nor does it follow typical OpenXR patterns. It hooks directly into the Switchboard system and is intended as a debug/visualization tool. For more accurate and representative testing, consider running ILLIXR with an actual OpenXR application.

## Known Issues

As noted above, `gldemo` does not actually render stereoscopically, and the two eye buffers are rendered from the same eye location. (This is not to say that the two eye buffers are not rendered separately; they are actually two separate drawcalls.) In addition, the quality of the pose used by `gldemo` is dependent on the upstream pose, which is currently not extrapolated/predicted and is subject to change.

## Contributions

Contributions are welcome; please raise an issue first, though, as many issues are known and are a part of our existing internal backlog.