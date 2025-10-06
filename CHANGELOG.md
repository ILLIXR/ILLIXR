# ILLIXR v4.1 (2025-10-06)

- Release Notes:
    This release of ILLIXR introduces a set of nine plugins for our Ada system, a distributed, power-aware, real-time scene provider for XR.
- Plugins
    - This is the first open-source release of **Ada**, implemented on top of the [ILLIXR](https://illixr.org) testbed. Ada provides a reproducible implementation of our ISMAR/TVCG 2025 [paper](https://rsim.cs.illinois.edu/Pubs/25-TVCG-Ada.pdf) results. Its features include:
        - Full implementation of **Ada’s distributed scene provisioning pipeline**:
            - Efficient depth encoding with MSB (lossless) + LSB (tunable bitrate)
            - Decoupled volumetric fusion and scene extraction
            - Extraction-aware scene management
            - Real-time mesh compression with parallel encoding
        - **Device-side and server-side plugin set** for ILLIXR
        - Support for ScanNet dataset and InfiniTAM-based reconstruction
        - Example prepared ScanNet sequence for quick testing
      <br>By [@jianxiapyh](https://github.com/jianxiapyh) in PR [#462](https://github.com/ILLIXR/ILLIXR/pull/462)
- Infrastructure
    - Some of the new plugins have dependencies which require the [Clang](https://clang.llvm.org/) compiler in order to be built. To avoid library conflicts, we recommend using the Clang compiler to build the entire project.

---

# ILLIXR v4.0 (2025-03-28)

- Release Notes:
    This release of ILLIXR brings many new features and capabilities. A good potion of the code base has been refactored/reorganized to both standardize the code and group similar parts together. There were a total of 16 pull requests merged which introduced 10 new plugins, overhauled several others, and addressed 13 issues. The new plugins include remote rendering, hand tracking, and ORB_SLAM. Vulkan is now the default display back-end, and due to this, some of the OpenGL based plugins may not work on every system. See below for more detailed notes on the changes.
- Plugins
    - Introduces a new ORB_SLAM3 based plugin. By [@hungdche](https://github.com/hungdche) in PR [#407](https://github.com/ILLIXR/ILLIXR/pull/407)
    - Deployed four new plugins: hand_tracking, webcam, hand_tracking.viewer, and zed.data_injection, reworked the zed plugin, and a zed_capture executable.
        - hand_tracking: detects and reports the positions of joints and fingertips; if depth information is available, or distances can be calculated by parallax, the reported coordinates are real-world in millimeters, otherwise they are in pixel coordinates
        - webcam: captures images from a webcam and publishes them to the `webcam` topic
        - hand_tracking.viewer: gui based interface for visualizing the output of the hand_tracker in real time
        - zed.data_injection: reads in data from disk and publishes it as if it were the zed plugin, good for debugging and testing
        - zed: now publishes information about the camera (pixels, field of view, etc.) which is required for some modes of depth sensing; also now publishes the pose corresponding to the current images; these are in addition to the images and IMU information it has always published
        - zed_capture: a standalone executable used to capture the images and poses from the zed camera and write them to disk (currently does not capture depth information); the products of this executable are intended to be used by the zed.data_injection plugin
      By [@astro-friedel](https://github.com/astro-friedel) in PR [#428](https://github.com/ILLIXR/ILLIXR/pull/428)
    - The hand tracking plugin now can be used as an OpenXR API Layer, where it will intercept calls to get hand tracking data. It has been tested with Monado, but should be generally compatible. Adheres to OpenXR-1.0 standards. By [@astro-friedel](https://github.com/astro-friedel) in PR [#436](https://github.com/ILLIXR/ILLIXR/pull/436)
    - Deployed the following plugins:
        - tcp_network_backend: a backend for Switchboard using TCP to transmit topics between two connected ILLIXR instances
        - offload_rendering_server: encodes OpenXR frames with FFMPEG and transmits them across the Switchboard network backend
        - offload_rendering_client: decodes frames transmitted from an offload_rendering_server instance and adds decoded frames to a buffer pool
        - openwarp_vk: an implementation of [OpenWarp](https://github.com/Zee2/openwarp), 6-DoF asynchronous reprojection, in ILLIXR
        - lighthouse: uses [libsurvive](https://github.com/collabora/libsurvive) to connect with Valve Index lighthouses for head tracking native_renderer has been reworked so that external applications can run asynchronously with reprojection.
      By [@qinjunj](https://github.com/qinjunj), [@Jebbly](https://github.com/Jebbly), [@shg8](https://github.com/shg8) in PR [#437](https://github.com/ILLIXR/ILLIXR/pull/437)
- Infrastructure
    - File loading is used by several plugins, each with their own code. The file loading code has been centralized into `data_loading.hpp`. By [@astro-friedel](https://github.com/astro-friedel) in PR [#416](https://github.com/ILLIXR/ILLIXR/pull/416)
    - Fixed an issue where plugins needed to be specified in the correct order, or they would throw an exception. By [@astro-friedel](https://github.com/astro-friedel) in PR [#417](https://github.com/ILLIXR/ILLIXR/pull/417)
    - Fixed an issue when changing a profile name and re-running CMake. By [@astro-friedel](https://github.com/astro-friedel) in PR [#418](https://github.com/ILLIXR/ILLIXR/pull/418)
    - The Runge-Kutta code was replicated several times in the code base. This code has now been centralized and rewritten to make it compatible with the current ILLIXR license. By [@astro-friedel](https://github.com/astro-friedel) in PR [#421](https://github.com/ILLIXR/ILLIXR/pull/421)
    - Updated the tcp socket code so that it can be distributed under the current ILLIXR license. By [@williamsentosa95](https://github.com/williamsentosa95) in PR [#424](https://github.com/ILLIXR/ILLIXR/pull/424)
    - The hand tracking plugin introduced a large number of **breaking** and non-breaking changes to the base structures used by ILLIXR.
        - **breaking**: the `cam_type` structures have been completely overhauled
            - all now inherit from a `cam_base_type` base struct, allowing for the `cam_base_type` to be used as a function argument type, letting a single function handle several different types of image input
            - all `cam_base_type` based structs are now defined in the common headers (under include/illixr) rather than in some plugins
            - there are now the following types: `binocular_cam_type`, `monocular_cam_type`, `rgb_depth_type`, `depth_type`, and `cam_type_zed`
        - **breaking**: the header `data_format.hpp` was getting too large and complex; it has been broken up into multiple headers under the `data_format` directory, with all struts and functions inside the `ILLIXR::data_format` namespace
            - camera_data.hpp: structures used to describe the physical characteristics of a camera (# pixels, field of view, etc.)
            - coordinate.hpp: enums for labelling the coordinate reference axes (e.g. LEFT_HANDED_Y_UP, RIGHT_HANDED_Y_UP, RIGHT_HANDED_Z_UP_X_FWD)
            - frame.hpp: the `rendered_frame` struct
            - hand_tracking_data: structs for holding positional information of points of a hand
            - imu.hpp: all imu related structs
            - misc.hpp: definitions that didn't fit with anything else
            - opencv_data_types.hpp: most `cam_base_type` structs
            - point.hpp: various structs for holding information about a point in 3D space
            - pose.hpp: all pose related structs
            - shape.hpp: structs related to shapes (e.g. rectangle)
            - template.hpp: common templated functions
            - unit.hpp: enums and functions related to units of measure
            - zed_cam.hpp: the `cam_type_zed` struct; this is kept separate from the other `cam_base_type` due to potential dependencies on ZED headers, which not all systems have
        - The pose structures were expanded to hold additional information used by the hand_tracking code, these additions do not affect current uses of these structures
      By [@astro-friedel](https://github.com/astro-friedel) in PR [#428](https://github.com/ILLIXR/ILLIXR/pull/428), [#440](https://github.com/ILLIXR/ILLIXR/pull/440)
    - Environment variable handling has been introduced to the `switchboard`. Calls to `std::getenv` have been replaced with calls to `swithchboard->get_env`, `switchbaord->get_env_char`, or `switchbaord->get_env_bool` and calls to `std::setenv` have been replaced with calls to `switchboard->set_env`. This change allows the switchboard to act as a broker for all environment variables. Additionally, environment variables can now be specified on the command line or in a yaml file, and will be made available to all plugins via the switchboard. By [@astro-friedel](https://github.com/astro-friedel) in PR [#430](https://github.com/ILLIXR/ILLIXR/pull/430)
- Misc
    - **Breaking**: Policies for contributing to the ILLIXR code base, as well as a general coding style guide have been introduced. The entire code base was overhauled to bring it into compliance with the style guide. By [@astro-friedel](https://github.com/astro-friedel) in PR [#419](https://github.com/ILLIXR/ILLIXR/pull/419)
    - General documentation updates for ILLIXR. By [@astro-friedel](https://github.com/astro-friedel) in PR [#422](https://github.com/ILLIXR/ILLIXR/pull/422)
    - Updated CMakeLists.txt to cleanly handle newer FindBoost capabilities, updated tags on some external packages, cleaned up CMake output, added some `include` lines required by newer gcc compiler versions, and added `all.yaml` profile for testing that the code fully builds. By [@astro-friedel](https://github.com/astro-friedel) in PR [#431](https://github.com/ILLIXR/ILLIXR/pull/431)
    - Updated several external packages ILLIXR builds from source to newer versions. Official support for CentOS, Fedora, and Ubuntu 20 and below has been dropped, support for Ubuntu 24 has been added. By    [@astro-friedel](https://github.com/astro-friedel) in PR [#439](https://github.com/ILLIXR/ILLIXR/pull/439)
    - Added automated release note generation by [@astro-friedel](https://github.com/astro-friedel) in PR [#441](https://github.com/ILLIXR/ILLIXR/pull/441)
    - Major documentation updates, including up-to-date plugin listing and descriptions and more internal links to data types. By [@astro-friedel](https://github.com/astro-friedel) in PR [#447](https://github.com/ILLIXR/ILLIXR/pull/447)
- Issues
  The following issues have been addressed and closed by this release:
    - [#151](https://github.com/ILLIXR/ILLIXR/issues/151) Openwarp by PR [#437](https://github.com/ILLIXR/ILLIXR/pull/437)
    - [#245](https://github.com/ILLIXR/ILLIXR/issues/245) Automate changelog generation by PR [#441](https://github.com/ILLIXR/ILLIXR/pull/441)
    - [#368](https://github.com/ILLIXR/ILLIXR/issues/368) Integrate TCP implementation for offloading VIO by PR [#437](https://github.com/ILLIXR/ILLIXR/pull/437)
    - [#371](https://github.com/ILLIXR/ILLIXR/issues/371) Bringing Configurable Parameters to Current iteration of ILLIXR by PR [#430](https://github.com/ILLIXR/ILLIXR/pull/430)
    - [#401](https://github.com/ILLIXR/ILLIXR/issues/401) Reduce or remove the use of environment variables to pass information between main and plugins by PR [#430](https://github.com/ILLIXR/ILLIXR/pull/430)
    - [#408](https://github.com/ILLIXR/ILLIXR/issues/408) Dependency handling for plugin loader by PR [#417](https://github.com/ILLIXR/ILLIXR/pull/417)
    - [#410](https://github.com/ILLIXR/ILLIXR/issues/410) Re-running CMake with different profile file results in old profile file being used by PR [#418](https://github.com/ILLIXR/ILLIXR/pull/418)
    - [#412](https://github.com/ILLIXR/ILLIXR/issues/412) Enable Headless Rendering in Vulkan by PR [#437](https://github.com/ILLIXR/ILLIXR/pull/437)
    - [#415](https://github.com/ILLIXR/ILLIXR/issues/415) Generate Guidelines and Procedures by PR [#419](https://github.com/ILLIXR/ILLIXR/pull/419)
    - [#427](https://github.com/ILLIXR/ILLIXR/issues/427) Broken Fedora 38 (only?) build by PR [#439](https://github.com/ILLIXR/ILLIXR/pull/439)
    - [#443](https://github.com/ILLIXR/ILLIXR/issues/443) Add CMake to the dependencies by PR [#422](https://github.com/ILLIXR/ILLIXR/pull/422)
    - [#89](https://github.com/ILLIXR/ILLIXR/issues/89) Integrate ORB-SLAM3 by PR [#407](https://github.com/ILLIXR/ILLIXR/pull/407)
    - [#96](https://github.com/ILLIXR/ILLIXR/issues/96) Push mkdocs and doxygen on merge to master by PR [#441](https://github.com/ILLIXR/ILLIXR/pull/441)


# ILLIXR v3.2.0 (Sept 19, 2023)

## Release Notes

* The biggest change is the complete rewriting of the build system. It now runs via CMake, making plugin management and dependency checking much more streamlined.
* ILLIXR added a new graphics pipeline using the Vulkan API, implemented in the new plugins vkdemo, timewarp_vk, dislpay_vk, and nativer_renderer. For details, see PR [#396](https://github.com/ILLIXR/ILLIXR/pull/396) by [@shg8](https://github.com/shg8).
* ILLIXR has updated integration to support Monado's out-of-process compositor, which now allows users to run more OpenXR apps (Unreal Engine, Vulkan-based apps, etc) with direct mode.
* There are numerous updates to plugins as well as a few new ones (see details below)
* OpenVINS is now the default SLAM plugin.
* Kimera-VIO is no longer available for use with ILLIXR

## What's Changed

* The offload_vio plugins were updated to a TCP implementation with separate IMU and Camera types; and the H264 codec was added by [@qinjunj](https://github.com/qinjunj) in [#409](https://github.com/ILLIXR/ILLIXR/pull/409) and [#363](https://github.com/ILLIXR/ILLIXR/pull/363)
* The timewarp_gl plugin was modified to render into two textures/framebuffers and a Vulkan-OpenGL interop was introduced by [@Jebbly](https://github.com/Jebbly) in [#404](https://github.com/ILLIXR/ILLIXR/pull/404)
* A new openni plugin was written by [@hungdche](https://github.com/hungdche) in [#370](https://github.com/ILLIXR/ILLIXR/pull/370)
* A new fauxpose plugin was written by [@wsherman64](https://github.com/wsherman64) in [#389](https://github.com/ILLIXR/ILLIXR/pull/389)
* A shaking issue with the zed plugin was fixed by [@qinjunj](https://github.com/qinjunj) in [#394](https://github.com/ILLIXR/ILLIXR/pull/394)
* The logging infrastructure was overhauled to use spdlog by [@mvanmoer](https://github.com/mvanmoer) in [#391](https://github.com/ILLIXR/ILLIXR/pull/391)
* The documentation was overhauled to make it more clear and descriptive as well as provide an interactive tool to select plugins and get their dependencies and customized build commands by [@hungdche](https://github.com/hungdche) and [@astro-friedel](https://github.com/astro-friedel) in [#361](https://github.com/ILLIXR/ILLIXR/pull/361) and [#384](https://github.com/ILLIXR/ILLIXR/pull/384)
* The ILLIXR codebase was converted to a more modular build system (CMake) by [@astro-friedel](https://github.com/astro-friedel) in [#384](https://github.com/ILLIXR/ILLIXR/pull/384)

## Issues

The following issues have been addressed and closed by this release

- [#42](https://github.com/ILLIXR/ILLIXR/issues/42)   Evaluate Ninja/CMake/Bazel for plugin build system
- [#44](https://github.com/ILLIXR/ILLIXR/issues/44)   Add human-readable logging infrastructure
- [#50](https://github.com/ILLIXR/ILLIXR/issues/50)   Separate IMU/camera Switchboard topics
- [#181](https://github.com/ILLIXR/ILLIXR/issues/181)  Fix plugin documentation
- [#188](https://github.com/ILLIXR/ILLIXR/issues/188)  Separate out Qemu setup from main setup
- [#205](https://github.com/ILLIXR/ILLIXR/issues/205)  Switch languages for the runner's config's schema
- [#213](https://github.com/ILLIXR/ILLIXR/issues/213)  Preserve partial progress in our Makefile
- [#231](https://github.com/ILLIXR/ILLIXR/issues/231)  Refactor configurations to reduce redundancy
- [#313](https://github.com/ILLIXR/ILLIXR/issues/313)  Add libgtsam.so symlink in gtsam install script
- [#358](https://github.com/ILLIXR/ILLIXR/issues/358)  Update Documentations
- [#360](https://github.com/ILLIXR/ILLIXR/issues/360)  Memory leak in offline_imu_cam plugin
- [#367](https://github.com/ILLIXR/ILLIXR/issues/367)  Installation issue on Ubuntu 22.04
- [#398](https://github.com/ILLIXR/ILLIXR/issues/398)  Required GPU specifications
- [#399](https://github.com/ILLIXR/ILLIXR/issues/399)  Reduce dependency on OpenCV
- [#400](https://github.com/ILLIXR/ILLIXR/issues/400)  Header/include cleanup is needed

**Full Changelog**: [https://github.com/ILLIXR/ILLIXR/compare/v3.1.0...v3.2.0](https://github.com/ILLIXR/ILLIXR/compare/v3.1.0...v3.2.0)

---

# ILLIXR v3.1.0 (May 8 2022)

## Release Notes
* Added a new core runtime service called `RelativeClock` to standardize timestamping and duration calculation in the system
* Added a set of new plugins under `offload_vio/` that enable the offloading of VIO from the device to a remote server
* Added a new plugin `record_imu_cam` that enables data collection with a real camera for later offline use
* Bug fixes to `realsense`

## What's Changed
* Add IMU and Cam Recorder by [@hungdche](https://github.com/hungdche) in [#327](https://github.com/ILLIXR/ILLIXR/pull/327)
* Add Offloading VIO Plugins to ILLIXR by [@JeffreyZh4ng](https://github.com/JeffreyZh4ng) in [#331](https://github.com/ILLIXR/ILLIXR/pull/331)
* Fix RealSense plugin by [@mhuzai](https://github.com/mhuzai) in [#337](https://github.com/ILLIXR/ILLIXR/pull/337)
* Standardize time in ILLIXR by [@charmoniumQ](https://github.com/charmoniumQ) in [#250](https://github.com/ILLIXR/ILLIXR/pull/250)


**Full Changelog**: [https://github.com/ILLIXR/ILLIXR/compare/v3.0.0...v3.1.0](https://github.com/ILLIXR/ILLIXR/compare/v3.0.0...v3.1.0)

---

# ILLIXR v3.0.0 (Apr 8, 2022)

## Release Notes

- Bug fixes to install script, `pose_lookup`, and `dynamic_lib`
- Documentation updates
- New documentation for using Switchboard standalone

While this is technically a patch, we are bumping the version to 3.0.0 because Switchboard had an API change in v2.1.11 and we forgot to bump up the major version then.

## What's Changed
* Don't use sudo by [@charmoniumQ](https://github.com/charmoniumQ) in [#322](https://github.com/ILLIXR/ILLIXR/pull/322)
* Revert boost install script by [@mhuzai](https://github.com/mhuzai) in [#323](https://github.com/ILLIXR/ILLIXR/pull/323)
* Fix docs by [@charmoniumQ](https://github.com/charmoniumQ) in [#319](https://github.com/ILLIXR/ILLIXR/pull/319)
* Fixed `pose_lookup` and `dynamic_lib` by [@e3m3](https://github.com/e3m3) in [#304](https://github.com/ILLIXR/ILLIXR/pull/304)
* Added documentation for using Switchboard and Phonebook externally by [@charmoniumQ](https://github.com/charmoniumQ) in [#320](https://github.com/ILLIXR/ILLIXR/pull/320)
* Fix and add documentation by [@mhuzai](https://github.com/mhuzai) in [#330](https://github.com/ILLIXR/ILLIXR/pull/330)


**Full Changelog**: [https://github.com/ILLIXR/ILLIXR/compare/v2.1.11...v3.0.0](https://github.com/ILLIXR/ILLIXR/compare/v2.1.11...v3.0.0)

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
