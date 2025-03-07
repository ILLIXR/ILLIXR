# ILLIXR v3.2.0 (Sept 19, 2023)

## Release Notes

* The biggest change is the complete rewriting of the build system. It now runs via CMake, making plugin management and dependency checking much more streamlined.
* ILLIXR added a new graphics pipeline using the Vulkan API, implemented in the new plugins vkdemo, timewarp_vk, dislpay_vk, and nativer_renderer. For details, see PR https://github.com/ILLIXR/ILLIXR/pull/396 by @shg8 .
* ILLIXR has updated integration to support Monado's out-of-process compositor, which now allows users to run more OpenXR apps (Unreal Engine, Vulkan-based apps, etc) with direct mode.
* There are numerous updates to plugins as well as a few new ones (see details below)
* OpenVINS is now the default SLAM plugin.
* Kimera-VIO is no longer available for use with ILLIXR

## What's Changed

* The offload_vio plugins were updated to a TCP implementation with separate IMU and Camera types; and the H264 codec was added by @qinjunj in https://github.com/ILLIXR/ILLIXR/pull/409 and https://github.com/ILLIXR/ILLIXR/pull/363
* The timewarp_gl plugin was modified to render into two textures/framebuffers and a Vulkan-OpenGL interop was introduced by @Jebbly in https://github.com/ILLIXR/ILLIXR/pull/404
* A new openni plugin was written by @hungdche in https://github.com/ILLIXR/ILLIXR/pull/370
* A new fauxpose plugin was written by @wsherman64 in https://github.com/ILLIXR/ILLIXR/pull/389
* A shaking issue with the zed plugin was fixed by @qinjunj in https://github.com/ILLIXR/ILLIXR/pull/394
* The logging infrastructure was overhauled to use spdlog by @mvanmoer in https://github.com/ILLIXR/ILLIXR/pull/391
* The documentation was overhauled to make it more clear and descriptive as well as provide an interactive tool to select plugins and get their dependencies and customized build commands by @hungdche and @astro-friedel in https://github.com/ILLIXR/ILLIXR/pull/361 and https://github.com/ILLIXR/ILLIXR/pull/384
* The ILLIXR codebase was converted to a more modular build system (CMake) by @astro-friedel in https://github.com/ILLIXR/ILLIXR/pull/384

## Issues

The following issues have been addressed and closed by this release

https://github.com/ILLIXR/ILLIXR/issues/42   Evaluate Ninja/CMake/Bazel for plugin build system
https://github.com/ILLIXR/ILLIXR/issues/44   Add human-readable logging infrastructure
https://github.com/ILLIXR/ILLIXR/issues/50   Separate IMU/camera Switchboard topics
https://github.com/ILLIXR/ILLIXR/issues/181  Fix plugin documentation
https://github.com/ILLIXR/ILLIXR/issues/188  Separate out Qemu setup from main setup
https://github.com/ILLIXR/ILLIXR/issues/205  Switch languages for the runner's config's schema
https://github.com/ILLIXR/ILLIXR/issues/213  Preserve partial progress in our Makefile
https://github.com/ILLIXR/ILLIXR/issues/231  Refactor configurations to reduce redundancy
https://github.com/ILLIXR/ILLIXR/issues/313  Add libgtsam.so symlink in gtsam install script
https://github.com/ILLIXR/ILLIXR/issues/358  Update Documentations
https://github.com/ILLIXR/ILLIXR/issues/360  Memory leak in offline_imu_cam plugin
https://github.com/ILLIXR/ILLIXR/issues/367  Installation issue on Ubuntu 22.04
https://github.com/ILLIXR/ILLIXR/issues/398  Required GPU specifications
https://github.com/ILLIXR/ILLIXR/issues/399  Reduce dependency on OpenCV
https://github.com/ILLIXR/ILLIXR/issues/400  Header/include cleanup is needed

**Full Changelog**: https://github.com/ILLIXR/ILLIXR/compare/v3.1.0...v3.2.0

---

# ILLIXR v3.1.0 (May 8 2022)

## Release Notes
* Added a new core runtime service called `RelativeClock` to standardize timestamping and duration calculation in the system
* Added a set of new plugins under `offload_vio/` that enable the offloading of VIO from the device to a remote server
* Added a new plugin `record_imu_cam` that enables data collection with a real camera for later offline use
* Bug fixes to `realsense`

## What's Changed
* Add IMU and Cam Recorder by @hungdche in https://github.com/ILLIXR/ILLIXR/pull/327
* Add Offloading VIO Plugins to ILLIXR by @JeffreyZh4ng in https://github.com/ILLIXR/ILLIXR/pull/331
* Fix RealSense plugin by @mhuzai in https://github.com/ILLIXR/ILLIXR/pull/337
* Standardize time in ILLIXR by @charmoniumQ in https://github.com/ILLIXR/ILLIXR/pull/250


**Full Changelog**: https://github.com/ILLIXR/ILLIXR/compare/v3.0.0...v3.1.0

---

# ILLIXR v3.0.0 (Apr 8, 2022)

## Release Notes

- Bug fixes to install script, `pose_lookup`, and `dynamic_lib`
- Documentation updates
- New documentation for using Switchboard standalone

While this is technically a patch, we are bumping the version to 3.0.0 because Switchboard had an API change in v2.1.11 and we forgot to bump up the major version then.

## What's Changed
* Don't use sudo by @charmoniumQ in https://github.com/ILLIXR/ILLIXR/pull/322
* Revert boost install script by @mhuzai in https://github.com/ILLIXR/ILLIXR/pull/323
* Fix docs by @charmoniumQ in https://github.com/ILLIXR/ILLIXR/pull/319
* Fixed `pose_lookup` and `dynamic_lib` by @e3m3 in https://github.com/ILLIXR/ILLIXR/pull/304
* Added documentation for using Switchboard and Phonebook externally by @charmoniumQ in https://github.com/ILLIXR/ILLIXR/pull/320
* Fix and add documentation by @mhuzai in https://github.com/ILLIXR/ILLIXR/pull/330


**Full Changelog**: https://github.com/ILLIXR/ILLIXR/compare/v2.1.11...v3.0.0

---

ILLIXR v2.1.11 (Dec 27, 2021)

### Updated plugins
- realsense:
    * Expanded supported cameras to include D4XX and T26X.
- qemu:
    * Updated script to pull and run Ubuntu 18.04.6.
- timewarp_gl:
    * Fixed memory leak by moving the destructor to ‘xlib_gl_extended_window’.
    * Added option to disable timewarping without disabling the plugin.
    * Suppressed per frame prints.
- ground_truth_slam:
    * Promoted instance-member to local variable.
- gtsam_integrator:
    * Fixed to dump all sqlite files once ILLIXR shuts down.
- gldemo:
    * Suppressed per frame prints.

### New plugins
- depthai:
    * Added support for DepthAI and OAK-D cameras.
- offload_data:
    * Added support to synchronously collect rendered images and poses on-the-fly.

### Build and CI infrastructure
- Updated dependency install scripts
    * Fixed gtsam,and opengv install script.
    * Enabled libboost in deps.sh.
    * Updated dependencies for Ubuntu 21.
    * Updated build and dependency installation script with DepthAI.
    * Canonicalized install script structure (fetch -> build -> install).
- Added Eigen install script.
- Added package dependencies install support for IntelRealSense for Ubuntu 18 LTS.
- Fixed libgtsam symlink.
- Fixed ZED compilation.
- Fixed compilation with OpenVINS.
- Migrated to tag-based checkouts for out-of-repo plugins.
- Other minor modifications.

### Documentation and guidelines
- README.md
    * Fixed internal links.
    * Other minor modifications.
- Documentation
    * Fixed a number of outdated information, broken links,typos, and unclear descriptions.
    * Fixed documentation figures and topic descriptions.
    * Fixed description of OpenXR Spaces for Monado Integration.
    * Updated contributing guidelines with step-by-step instructions.
    * Updated instructions on writing customized plugins.
    * Added explicit definitions for adding cmd and environment variables.
    * Removed unused code and documentation.
- License
    * Addressed missing licence information for GTSAM, Kimera-VIO and moodycamel::ConqurrentQueue.

### Improved runtime
- Added  errno checking.
- Added verbose errors flag.
- Added additional debugging info/support.
- Added a lock-based queue for the debugging process.
- Added KIMERA_ROOT to default monado config.
- Added git tags for mondao_integration launch configuration.
- Added stoplight to synchronize thread starts/stops.
- Added explicit definitions for adding cmd and environment variables.
- Enabled non-ILLIXR specific files to be potentially reused.
- Cleared errno in XOpenDisplay.
- Updated ILLIXR runtime for new Monado integration.
- Changed Switchboard to use ref-counting to patch memory leak.
- Changed the tqdm bar to show units (bytes/kbytes/…).
- Fixed ‘clean’ configuration (clean.yaml).
- Fixed ‘headless’ configuration command argument.
- Fixed the issue that running ILLIXR on dbg mode cannot exit cleanly.
- Fixed GL performance warning to be no longer fatal.
- Inlined global helper function.

---

ILLIXR v2.1.10 (Mar 16, 2021)

- Updated plugins
    - New: `pose_prediction`, `pose_lookup`, `kimera-vio`, `realsense`, `gtsam_integrator`, audio decoding
    - Removed (Git) submodules
    - Fixed ground truth for plugins
    - Fixed OpenVins Motion-to-Photon
    - New global camera shutter
    - New object and interpupilary distance (IPD) rendering
    - Improved hologram performance
    - Using Euroc dataset
- New build and CI infrastructure
    - Fixed GTSAM build
    - Fixed Kimera-VIO build
    - New parallel building for Runner
    - New source (Git) and dataset fetching for Runner
    - New headless support (`xvfb`)
    - New virtualization support (`qemu`)
    - New CI workflows for master branch and pull requests
    - Updated dependency installations for Docker
    - Updated configurations (e.g., `native-lookup`, `ci`)
    - Fixed Monado integration
    - New instrumented OpenCV
- New documentation and guidelines
    - New plugins documentation
    - New Runner documentation
    - New contributing guidelines
- Improved runtime
    - New Switchboard API
    - Updated synchronization for readers and writers
    - New threaded plugins
    - New timing instrumentation
    - New logging infrastructure (SQLite support)

---

ILLIXR v2.1.0 (Sept. 4, 2020)

- Added support for ZED Mini
- Improved build system
- Streamlined Monado integration
- Added logging interface (implementation pending)
- Improved core runtime (switchboard, phonebook, and sleeping mechanism)
- Updated documentation

---

ILLIXR v2.0.1 (May 19, 2020)

Tweaked documentation.

---

ILLIXR v2.0.0

- Added runtime framework (spindle, switchboard, phonebook, logger)
- Added OpenXR support
- Integrated components into runtime framework
- Enabled end-to-end system
- Added docs

---

ILLIXR v1.2.0

- Added eye tracking
- Improved audio pipeline build process
- Removed legacy code from hologram
- Updated README.md
- Updated CONTRIBUTORS

---

ILLIXR v1.0.0

v1 of ILLIXR contains only standalone components. See the v2 release for the runtime and component integration.

Initial release of ILLIXR with SLAM, scene reconstruction, audio recording and playback, lens distortion correction, chromatic aberration correction, timewarp, and computational holography.
