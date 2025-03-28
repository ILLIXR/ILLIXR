# Lighthouse
## Summary
The ``lighthouse`` plugin supports lighthouse tracking using the [libsurvive library](https://github.com/collabora/libsurvive). To use this plugin, libsurvive should first be built, installed, and calibrated according to [their documentation](https://github.com/collabora/libsurvive/blob/master/README.md). Running ILLIXR's CMake to build the ``lighthouse`` plugin should automatically build and install ``libsurvive``, but the steps to calibrate the lighthouses should still be followed.

## Note

If the ``lighthouse`` plugin is being used, make sure to set the environment variable ``ILLIXR_LIGHTHOUSE`` to true, as the ``pose_prediction`` relies on it.
