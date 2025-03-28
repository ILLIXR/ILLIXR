# Glossary of ILLIXR Terminology

A collection of ILLIXR and ILLIXR-adjacent terms and their definitions can be found
on this page your reference.

## General

#### Asynchronous Reprojection

The processing of rendered video for motion interpolation.
Asynchronous reprojection improves the perception of the rendered video to the [_HMD_][G16]
when rendering misses it target [_frame rate_][G17].

Asynchronous reprojection is implemented in the [`timewarpgl`][P18].

See the [Wikipedia article][E23].

#### Chromatic Aberration Correction

The processing of visual anomalies in images where colors are diffracted due to imperfect optics
or other perturbing factors.

For more information, see the [Wikipedia article][E39].

#### Compositor

A window manager that establishes a [_framebuffer_][G17] for each window of a graphical system.
A compositor merges information across its windows to construct a unified framebuffer.

For more information, see the [Wikipedia article][E35].

#### Distortion Correction

The processing of visual anomalies in images where rectilinear features have been warped.

For more information, see the [Wikipedia artice][E38].

#### Eye Tracking

The process of measuring the eye movement of a user (who is possibly also wearing an [_HMD_][G16]).

For more information, see the [Wikipedia article][E30].

#### Event Stream

A communication interface supporting writes, synchronous reads, and asynchronous reads.
For synchronous reads, every value written to the stream is visible to consumers.
For asynchronous reads, only the latest values written are guaranteed to be visible to consumers.

#### Framebuffer

A region of memory used to hold graphical information to be output to a display or graphics device.

- **Depth Buffer**:
  A framebuffer representing the depth information of a 3D scene.
  Depth information is useful for applications such as graphics and [_SLAM_][G18].

- **Eye Buffer**:
  A framebuffer dedicated for display through an [_HMD_][G16] lens to be perceived by a user's eye.

- **Frame**:
  A single frame (image) to be output to a display at a certain instant of time based on the
  system's _frame rate_.

- **Frame Rate**:
  The interval period between complete (as defined by the output resolution) frame updates
  and refreshes.
  In many systems, the target frame rate is determined by a fixed vertical sync (_VSYNC_) period.

For more information, see the [Wikipedia article][E27].

#### Ground Truth

The most accurate source of measurement available for a data set.
Typically, ground truth measurements are provided for the evaluation of sensor data where the sensor
or other data source is not as accurate or reliable as the source for the ground truth.

- ***Ground Truth Poses***:
  A collection of poses used to evaluate the accuracy of pose generation and prediction algorithms.

- ***Ground Truth Images***:
  A collection of images used to evaluate the accuracy of visual processing algorithms,
  like [_SLAM_][G18] and [_VIO_][G19].

See the [ILLIXR Plugins][P18] page for information about sensors implemented in ILLIXR.

#### Head-mounted Display

A display device worn on the head and face for use with VR and XR applications.
Also known as an _HMD_.

For more information, see the [Wikipedia article][E26].

#### Inertial Measurement Unit

A device that reports its orientation in space and any forces applied it.
Also known as an _IMU_.

An IMU is implemented in the [`offline_imu`][P18].

For more information, see the [Wikipedia article][E24].

#### Plugin

A modular component that can be detected and enabled for use by an ILLIXR application.
A plugin can be internal or external to the [ILLIXR project][E14].
Each plugin is compiled and launched dynamically at runtime based on the command line options given or
ILLIXR [_profile_][G10] file being used.
ILLIXR also implements a [_Monado_][G12] runtime [_translation Plugin_][P19].

For a list of supported plugins and their details, see the [ILLIXR Plugins][P18] page.
For instructions for how to modify or write your own plugins, see the [Modifying a Plugin][I10]
and [Writing Your Plugin][I11] pages.

See the [_Plugin_ API documentation][A13].

#### Profile

A _profile_ describes the environment to be used for the build system and running ILLIXR. _Profiles_ are defined
in [YAML][G13] files. There are several provided in the `profiles` directory in the repository. A _profile_ file defines
what [plugins][G14] are to be used, as well as additional information specific to where it is being used.

- ***As input to CMake***:
  If a _profile_ file is given to cmake via the `-DYAML_FILE=` directive then the listed [plugins][G14] will be built.
- ***As input to the ILLIXR binary***
  If a _profile_ file is given on the ILLIXR binary via the `--yaml=` command line option, then any
  listed [plugins][G14]
  will be loaded and any other command line options given in the _profile_ file will be used. See [Running ILLIXR][I15]
  for details.

The same _profile_ file can be given to both cmake and the ILLIXR binary (you may need to change the `data:` entry), as
any unrecognized options are ignored by both systems. See [Profile file format][I16] for details on the _profile_ file
format.

#### Pose

The combination of orientation and position of an object, used for computer vision
and robotics applications.
ILLIXR applications make use of poses to track the [user's HMD][G16] within the virtual environment.
Internally, ILLIXR has multiple classifications of poses which are used for various purposes.

- ***Slow Pose***:
  A _slow pose_ is a visual-inertial based pose estimate at low frequency (e.g. 30 Hz). It can be from OpenVINS or ORB_SLAM3.

- ***Fast Pose***:
  A _fast pose_ is a pose estimate from IMU integration at high frequency (e.g. hundreds of Hz), but with limited accuracy.

- ***True Pose***:
  A _true pose_ is a ground truth pose, usually from datasets.

- ***Pose Prediction***:
  To improve the user's perceived latency, pose prediction leverages historical and current system information such as poses and sensor inputs to pre-compute the user's future pose.
  This pre-computation enables downstream components to begin processing earlier, reducing end-to-end latency.

Pose Prediction is implemented in [`pose_prediction`][P18].

For more information, see the [Wikipedia article][E28].

#### Runtime

The ILLIXR system runtime is responsible for the dynamic orchestration of ILLIXR
device resources,
system resources,
and
client applications.

The runtime implementation is located in `<ILLIXR_INSTALL_DIR>/bin`.
See the [Getting Started][I14] and [Monado Overview][P14] pages for details about the ILLIXR runtime.

#### Service

These are modular components that are initialized like a [_plugin_][G14], but provide callable functions. Services are
not directly triggered by ILLIXR's main loop, or by publication to a topic.

#### Simultaneous Localization and Mapping

The computational process of creating a map of an unknown environment, and finding one's location
within that space.
Also known as _SLAM_.

For more information, see the [Wikipedia article][E25].

#### Swap Chain

A set of virtual [_framebuffers_][G17] to be output to a display.
Only one framebuffer in a swap chain is displayed at a time, enabling the
other virtual framebuffers to be concurrently modified in memory.

For more information, see the [Wikipedia article][E29].

#### Visual Inertial Odometry

The process of computing a [_pose estimate_][G11] from incoming visual information and measurements
from the [_IMU_][G19].
Also known as _VIO_.
Often used in combination with [_SLAM_][G18] techniques.

See the [Wikipedia article][E40].

## Components

#### Phonebook

An ILLIXR service directory used to introspectively interface plugins and their data.
The implementation resides in `ILLIXR/runtime/`.

See the [_Phonebook_ API documentation][A11].

#### Spindle

An ILLIXR component responsible for launching and managing plugin threads.
The implementation resides in `ILLIXR/runtime/`.

See the [_Spindle_ API documentation][A12].

#### Switchboard

An ILLIXR event stream manager that maintains data pipelines between plugins.
The implementation resides in `ILLIXR/runtime/`.

See the [_Switchboard_ API documentation][A14].

## Technologies

#### Docker

A platform and containerization framework for deploying applications under virtualization.
ILLIXR uses Docker to deploy and test code in a continuous integration and deployment pipeline.

For more information, see the [Docker overview and getting started page][E16].

#### Godot

An open source game development engine.
ILLIXR applications targeting the [_OpenXR_][G15] use Godot to access the engine's integration
with the OpenXR standard via [_Monado_][G12].

For more information, visit the [official Godot site][E22].

#### Monado

An open source, modular implementation of the OpenXR standard for [GNU/Linux][E13].

See the ILLIXR [Monado Overview][P14] and [Monado Dataflow][P15] pages for details about our
runtime integration using Monado.

For more information, visit the [official Monado development site][E12].

#### OpenCV

An open source computer vision library. Many of ILLIXR's components utilize these powerful algorithms and
the [cv::Mat][E41] class serves as the basis format for images inside ILLIXR.

For more information, visit the [official OpenCV site][E42].

#### OpenGL

A cross-platform graphics API that allows developers to create portable graphics applications
easily.
Also known as _GL_.

- **GL Context**:
  A data structure storing the state of an OpenGL application instance.
  Within a GL context resides [_framebuffer_][G17] data.
  It is not thread safe to share contexts without appropriate synchronization.

- **GLFW**:
  An open source implementation of OpenGL.
  Supports Windows, macOS and, Linux ([_X11_][E32] and Wayland).
  See the [GLFW development site][E36].

For more information, see the [official OpenGL page from the Khronos Group][E34].

#### OpenXR

An open standard for Augmented and Virtual Reality.
ILLIXR components target the OpenXR standard and interact with the ILLIXR device
via the Application Interface.

For more information, visit the [official site from the Khronos Group][E11].

#### QEMU-KVM

An open source virtualization tool and machine emulator.
See the instructions for running [ILLIXR under Virtualization][I13].

For more information, see the [official QEMU page][E21].

#### SQLite

A SQL database engine implementation in C designed to be lightweight and easy to use.
The ILLIXR project allows user to record application statistics to a local database
for efficient processing.
See the [Logging and Metrics page][I12] for usage details.

For more information, see the [SQLite development site][E31].

#### Ubuntu

An open source [GNU/Linux][E13] operating system and distribution.
ILLIXR currently supports the _Long Term Support (LTS)_ versions of Ubuntu:
20.04 LTS (Focal)
and 11.04 (Jammy)

For more information, visit the [official Ubuntu site][E39].

#### Vulkan

A cross-platform graphics API that allows developers to efficiently target
low-level hardware features.

For more information, see the [official Vulkan page from the Khronos Group][E33].

#### Xvfb

A virtual framebuffer for the [X11 Window System][E32].
ILLIXR uses Xvfb to enable running the graphical ILLIXR application without requiring the user
to have a graphical environment configured at application launch.

For more information, see the [Xfvb man page][E15].

#### YAML

A markup language and data serialization standard designed to be user-friendly.
We make use of the [yaml-cpp][E19] libraries to read our [_profile_][G10] files.

For more information, visit the [official YAML page][E17].


[//]: # (- glossary -)

[G10]:   glossary.md#profile

[G11]:   glossary.md#pose

[G12]:   glossary.md#monado

[G13]:   glossary.md#yaml

[G14]:   glossary.md#plugin

[G15]:   glossary.md#openxr

[G16]:   glossary.md#head-mounted-display

[G17]:   glossary.md#framebuffer

[G18]:   glossary.md#simultaneous-localization-and-mapping

[G19]:   glossary.md#visual-inertial-odometry


[//]: # (- plugins -)

[P14]:   plugin_README/monado_illixr_runtime_overview.md

[P15]:   plugin_README/monado_integration_dataflow.md

[P18]:   illixr_plugins.md

[P19]:   plugin_README/monado_illixr_runtime_overview.md#translation-plugin


[//]: # (- internal -)

[I10]:   working_with/modifying_a_plugin.md

[I11]:   working_with/writing_your_plugin.md

[I12]:   working_with/logging_and_metrics.md

[I13]:   working_with/virtualization.md

[I14]:   getting_started.md

[I15]:   getting_started.md#running-illixr

[I16]:   getting_started.md#profile-file-format


[//]: # (- api -)

[A11]:   api/classILLIXR_1_1phonebook.md

[A12]:   api/classILLIXR_1_1threadloop.md

[A13]:   api/classILLIXR_1_1plugin.md

[A14]:   api/classILLIXR_1_1switchboard.md


[//]: # (- external -)

[E11]:    https://www.khronos.org/openxr

[E12]:    https://monado.dev

[E13]:    https://www.gnu.org/gnu/linux-and-gnu.en.html

[E14]:    https://github.com/ILLIXR/ILLIXR

[E15]:    https://www.x.org/releases/X11R7.7/doc/man/man1/Xvfb.1.xhtml

[E16]:    https://docs.docker.com/get-started/overview

[E17]:    https://yaml.org

[E18]:    https://json-schema.org

[E19]:    https://github.com/jbeder/yaml-cpp

[E21]:   https://www.qemu.org

[E22]:   https://godotengine.org

[E23]:   https://en.wikipedia.org/wiki/Asynchronous_reprojection

[E24]:   https://en.wikipedia.org/wiki/Inertial_measurement_unit

[E25]:   https://en.wikipedia.org/wiki/Simultaneous_localization_and_mapping

[E26]:   https://en.wikipedia.org/wiki/Head-mounted_display

[E27]:   https://en.wikipedia.org/wiki/Framebuffer

[E28]:   https://en.wikipedia.org/wiki/Pose_(computer_vision)

[E29]:   https://en.wikipedia.org/wiki/Swap_chain

[E30]:   https://en.wikipedia.org/wiki/Eye_tracking

[E31]:   https://www.sqlite.org/index.html

[E32]:   https://en.wikipedia.org/wiki/X_Window_System

[E33]:   https://www.khronos.org/vulkan

[E34]:   https://www.opengl.org

[E35]:   https://en.wikipedia.org/wiki/Compositing_window_manager

[E36]:   https://www.glfw.org

[E37]:   https://en.wikipedia.org/wiki/Distortion_(optics)

[E38]:   https://en.wikipedia.org/wiki/Chromatic_aberration

[E39]:   https://ubuntu.com

[E40]:   https://en.wikipedia.org/wiki/Visual_odometry

[E41]:   https://docs.opencv.org/4.x/d3/d63/classcv_1_1Mat.html

[E42]:   https://opencv.org/
