# Glossary of ILLIXR Terminology

A collection of ILLIXR and ILLIXR-adjacent terms and their definitions can be found
    on this page your reference.


## General

#### Asynchronous Reprojection

The processing of rendered video for motion interpolation.
Asynchronous reprojection improves the perception of the rendered video to the [_HMD_][71]
when rendering misses it target [_frame rate_][72].

Asynchronous reprojection is implemented in the [`timewarpgl` ILLIXR plugin][68].

See the [Wikipedia article][13].

#### Chromatic Abberation Correction

The processing of visual anomalies in images where colors are diffracted due to imperfect optics
or other perturbing factors.

For more information, see the [Wikipedia article][29].

#### Compositor

A window manager that establishes a [_framebuffer_][72] for each window of a graphical system.
A compositor merges information across its windows to construct a unified framebuffer.

For more information, see the [Wikipedia article][25].

#### Distortion Correction

The processing of visual anomalies in images where rectilinear features have been warped.

For more information, see the [Wikipedia artice][28].

#### Eye Tracking

The process of measuring the eye movement of a user (who is possibly also wearing a [_HMD_][71]).

For more information, see the [Wikipedia article][20].

#### Event Stream

A communication interface supporting writes, sychronous reads, and asynchronous reads.
For synchronous reads, every value written to the stream is visible to consumers.
For asynchronous reads, only the latest values written are guaranteed to be visible to consumers.


#### Framebuffer

A region of memory used to hold graphical information to be output to a display or graphics device.

-   **Depth Buffer**:
    A framebuffer representing the depth information of a 3D scene.
    Depth information is useful for applications such as graphics and [_SLAM_][76].

-   **Eye Buffer**:
    A framebuffer dedicated for display through a [_HMD_][71] lens to be perceived by a user's eye.

-   **Frame**:
    A single frame (image) to be output to a display at a certain instant of time based on the
    system's _frame rate_.

-   **Frame Rate**:
    The interval period between complete (as defined by the output resolution) frame updates
    and refreshes.
    In many systems, the target frame rate is determined by a fixed vertical sync (_VSYNC_) period.

For more information, see the [Wikipedia article][17].

#### Ground Truth

The most accurate source of measurement available for a data set.
Typically, ground truth measurements are provided for the evaluation of sensor data where the sensor
or other data source is not as accurate or reliable as the source for the ground truth.

-   ***Ground Truth Poses***:
    A collection of poses used to evaluate the accuracy of pose generation and prediction algorithms.

-   ***Ground Truth Images***:
    A collection of images used to evaluate the accuracy of visual processing algorithms,
    like [_SLAM_][76] and [_VIO_][77].

See the [ILLIXR Plugins][68] page for information about sensors implemented in ILLIXR.

#### Head-mounted Display

A display device worn on the head and face for use with VR and XR applications.
Also known as a _HMD_.

For more information, see the [Wikipedia article][16].

#### Inertial Measurement Unit

A device that reports its orientation in space and any forces applied it.
Also known as an _IMU_.

An IMU is implemented in the [`offline_imu` ILLIXR plugin][68].

For more information, see the [Wikipedia article][14].

#### Plugin

A modular component that can be detected and enabled for use by an ILLIXR application.
A plugin can be internal or external to the [ILLIXR project][4].
Each plugin is compiled and launched dynamically at runtime based on the command line options given or
    ILLIXR [_profile_][50] file being used.
ILLIXR also implements a [_Monado_][58] runtime [_translation Plugin_][75].

For a list of supported plugins and their details, see the [ILLIXR Plugins][68] page.
For instructions for how to modify or write your own plugins, see the [Modifying a Plugin][60]
    and [Writing Your Plugin][61] pages.

See the [_Plugin_ API documentation][53].

#### Profile

A _profile_ describes the environment to be used for the build system and running ILLIXR. _Profiles_ are defined in [YAML][66] files. There are several provided in the `profiles` directory in the repository. A _profile_ file defines what [plugins][67] are to be used, as well as additional information specific to where it is being used.

-  ***As input to CMake***:
   If a _profile_ file is given to cmake via the `-DYAML_FILE=` directive then the listed [plugins][67] will be built.
-  ***As input to the ILLIXR binary***
   If a _profile_ file is given on the ILLIXR binary via the `--yaml=` command line option, then any listed [plugins][67] will be loaded and any other command line options given in the _profile_ file will be used. See [Running ILLIXR][78] for details.

The same _profile_ file can be given to both cmake and the ILLIXR binary (you may need to change the `data:` entry), as any unrecognized options are ignored by both systems. See [Profile file format][80] for details on the _profile_ file format.

#### Pose

The combination of orientation and position of an object, used for computer vision
and robotics applications.
ILLIXR applications make use of poses to track the [user's HMD][71] within the virtual environment.
Internally, ILLIXR has multiple classifications of poses which are used for various purposes.

-   ***Slow Pose***:
    A _slow pose_ is a ... ***TODO***

-   ***Fast Pose***:
    A _fast pose_ is a ... ***TODO***

-   ***True Pose***:
    A _true pose_ is a ... ***TODO***
    _Depracated_ starting ILLIXR release `v2.X.X`.

-   ***Pose Prediction***:
    To improve the user's perception latency experience the time between, _pose prediction_
    uses history and current system information to pre-compute the user's next pose
    Pre-computing the next pose allows for components downstream from the pose output
    in the event stream dataflow graph to begin computation.

Pose Prediction is implemented in the [`pose_prediction` ILLIXR plugin][68].

For more information, see the [Wikipedia article][18].

#### Runtime

The ILLIXR system runtime is responsible for the dynamic orchestration of ILLIXR
device resources,
system resources,
and
client applications.

The runtime implementation is located in `<ILLIXR_INSTALL_DIR>/bin`.
See the [Getting Started][74] and [Monado Overiew][64] pages for details about the ILLIXR runtime.

#### Swap Chain

A set of virtual [_framebuffers_][72] to be output to a display.
Only one framebuffer in a swap chain is displayed at a time, enabling the
    other virtual framebuffers to be concurrently modified in memory.

For more information, see the [Wikipedia article][19].

#### Simultaneous Localization and Mapping

The computational process of creating a map of an unknown environment, and finding one's location
    within that space.
Also known as _SLAM_.

For more information, see the [Wikipedia article][15].

#### Visual Interial Odometry

The process of computing a [_pose estimate_][57] from incoming visual information and measurements
    from the [_IMU_][77].
Also known as _VIO_.
Often used in combination with [_SLAM_][76] techniques.

See the [Wikipedia article][30].


## Components


#### Phonebook

An ILLIXR service directory used to introspectively interface plugins and their data.
The implementation resides in `ILLIXR/runtime/`.

See the [_Phonebook_ API documentation][51].

#### Spindle

An ILLIXR component responsible for launching and managing plugin threads.
The implementation resides in `ILLIXR/runtime/`.

See the [_Spindle_ API documentation][52].

#### Switchboard

An ILLIXR event stream manager that maintains data pipelines between plugins.
The implementation resides in `ILLIXR/runtime/`.

See the [_Switchboard_ API documentation][54].


## Technologies


#### Docker

A platform and containerization framework for deploying applications under virtualization.
ILLIXR uses Docker to deploy and test code in a continuous integration and deployment pipeline.

For more information, see the [Docker overview and getting started page][6].

#### Godot

An open source game development engine.
ILLIXR applications targeting the [_OpenXR_][70] use Godot to access the engine's integration
with the OpenXR standard via [_Monado_][58].

For more information, visit the [official Godot site][12].

#### Monado

An open source, modular implementation of the OpenXR standard for [GNU/Linux][3].

See the ILLIXR [Monado Overview][64] and [Monado Dataflow][65] pages for details about our
runtime integration using Monado.

For more information, visit the [official Monado development site][2].

#### OpenGL

A cross-platform graphics API that allows developers to create graphics applications
easily and portably.
Also known as _GL_.

-   **GL Context**:
    A data structure storing the state of an OpenGL application instance.
    Within a GL context resides [_framebuffer_][72] data.
    It is not thread safe to share contexts without appropriate synchronization.

-   **GLFW**:
    An open source implementation of OpenGL.
    Supports Windows, MacOS and, Linux ([_X11_][22] and Wayland).
    See the [GLFW development site][26].

For more information, see the [official OpenGL page from the Khronos Group][24].

#### OpenXR

An open standard for Augmented and Virtual Reality.
ILLIXR components target the OpenXR standard and interact with the ILLIXR device
    via the Application Interface.

For more information, visit the [official site from the Khronos Group][1].

#### QEMU-KVM

An open source virtulization tool and machine emulator.
See the instructions for running [ILLIXR under Virtualization][63].

For more information, see the [official QEMU page][11].

#### SQLite

A SQL database engine implementation in C designed to be lightweight and easy to use.
The ILLIXR project allows user to records application statistics to a local database
for efficient processing.
See the [Logging and Metrics page][73] for usage details.

For more information, see the [SQLite development site][21].

#### Ubuntu

An open source [GNU/Linux][3] operating system and distribution.
ILLIXR currently supports the _Long Term Support (LTS)_ versions of Ubuntu:
20.04 LTS (Focal)
and 11.04 (Jammy)

For more information, visit the [official Ubuntu site][29].
#### Vulkan

A cross-platform graphics API that allows developers to efficiently target
low-level hardware features.

For more information, see the [official Vulkan page from the Khronos Group][23].

#### Xvfb

A virtual framebuffer for the [X11 Window Sytem][22].
ILLIXR uses Xvfb to enable running the graphical ILLIXR application without requiring the user
    to have a graphical environment configured at application launch.

For more information, see the [Xfvb man page][5].

#### YAML

A markup language and data serialization standard designed to be user-friendly.
We make use of the [yaml-cpp][9] libraries to read
    our [_profile_][50] files.

For more information, visit the [official YAML page][7].



[//]: # (- References -)

[1]:    https://www.khronos.org/openxr
[2]:    https://monado.dev
[3]:    https://www.gnu.org/gnu/linux-and-gnu.en.html
[4]:    https://github.com/ILLIXR/ILLIXR
[5]:    https://www.x.org/releases/X11R7.7/doc/man/man1/Xvfb.1.xhtml
[6]:    https://docs.docker.com/get-started/overview
[7]:    https://yaml.org
[8]:    https://json-schema.org
[9]:    https://github.com/jbeder/yaml-cpp
[11]:   https://www.qemu.org
[12]:   https://godotengine.org
[13]:   https://en.wikipedia.org/wiki/Asynchronous_reprojection
[14]:   https://en.wikipedia.org/wiki/Inertial_measurement_unit
[15]:   https://en.wikipedia.org/wiki/Simultaneous_localization_and_mapping
[16]:   https://en.wikipedia.org/wiki/Head-mounted_display
[17]:   https://en.wikipedia.org/wiki/Framebuffer
[18]:   https://en.wikipedia.org/wiki/Pose_(computer_vision)
[19]:   https://en.wikipedia.org/wiki/Swap_chain
[20]:   https://en.wikipedia.org/wiki/Eye_tracking
[21]:   https://www.sqlite.org/index.html
[22]:   https://en.wikipedia.org/wiki/X_Window_System
[23]:   https://www.khronos.org/vulkan
[24]:   https://www.opengl.org
[25]:   https://en.wikipedia.org/wiki/Compositing_window_manager
[26]:   https://www.glfw.org
[27]:   https://en.wikipedia.org/wiki/Distortion_(optics)
[28]:   https://en.wikipedia.org/wiki/Chromatic_aberration
[29]:   https://ubuntu.com
[30]:   https://en.wikipedia.org/wiki/Visual_odometry

[//]: # (- Internal -)

[50]:   glossary.md#profile
[51]:   api/html/classILLIXR_1_1phonebook.html
[52]:   api/html/classILLIXR_1_1threadloop.html
[53]:   api/html/classILLIXR_1_1plugin.html
[54]:   api/html/classILLIXR_1_1switchboard.html
[56]:   glossary.md#ground-truth
[57]:   glossary.md#pose
[58]:   glossary.md#monado
[59]:   glossary.md#xvfb
[60]:   modifying_a_plugin.md
[61]:   writing_your_plugin.md
[62]:   glossary.md#docker
[63]:   virtualization.md
[64]:   plugin_README/monado_illixr_runtime_overview.md
[65]:   plugin_README/monado_integration_dataflow.md
[66]:   glossary.md#yaml
[67]:   glossary.md#plugin
[68]:   illixr_plugins.md
[70]:   glossary.md#openxr
[71]:   glossary.md#head-mounted-display
[72]:   glossary.md#framebuffer
[73]:   logging_and_metrics.md
[74]:   getting_started.md
[75]:   plugin_README/monado_illixr_runtime_overview.md#translation-plugin
[76]:   glossary.md#simultaneous-localization-and-mapping
[77]:   glossary.md#visual-interial-odometry
[78]:   getting_started.md#running-illixr
[80]:   getting_started.md#profile-file-format