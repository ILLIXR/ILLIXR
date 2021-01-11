# Glossary of ILLIXR Terminology

A collection of ILLIXR and ILLIXR-adjacent terms and their definitions can be found
    on this page your reference.


## General


#### Runtime

The ILLIXR system runtime is responsible for the dynamic orchestration of ILLIXR
    device resources,
    system resources,
    and
    client applications.

The runtime implementation is located in `ILLIXR/runtime/`.
See the [Building ILLIXR][54] and [Monado Overiew][44] pages for details about the ILLIXR runtime.

#### Plugin

A modular component that can be detected and enabled for use by an ILLIXR application.
A plugin can be internal or external to the [ILLIXR project][4].
Each plugin is compiled and launched dynamically at runtime based on the
    ILLIXR [_Configuration_][20] used.
ILLIXR also implements a [_Monado_][38] runtime [_Translation Plugin_][55].

For a list of supported plugins and their details, see the [ILLIXR Plugins][48].
For instructions for how to modify or write your own plugins, see the [Modifying a Plugin][40]
    and [Writing Your Plugin][41] pages.

See the [_Plugin_ API documentation][33].

#### Config(uration)

A file describing the key information required to launch an ILLIXR application.
Configurations for ILLIXR are implemented as [_YAML_][46] files.
Each configuration comprises an _Action_, a _Profile_, and a list of [_Plugins_][47] as
    defined by our configuration specification _Schema_.

-   **Action** *(Previously Loader)*:
    An _Action_ encapsulates a task for [_Runner_][2].
    
    *   `native`:
        The default application launch configuration.
        Does not use our [_Monado_][38] runtime integration.
        Defined in `ILLIXR/configs/native.yaml`.

    *   `native-lookup`:
        Same as `native`, but using a [_Ground Truth_][36] lookup from a file for
            the [_Pose_][37] instead of computing it.
        Defined in `ILLIXR/configs/native-lookup.yaml`.

    *   `headless`:
        Same as `native`, but using [_Xvfb_][39] to run without a graphical environment.
        Defined in `ILLIXR/configs/headless.yaml`.

    *   `ci`:
        Same as `headless`, but using [_Docker_][42] virtualization and debug-enabled compilation.
        Defined in `ILLIXR/configs/ci.yaml`.

    *   `monado`:
        Similar to `native`, but uses our [_Monado_][38] runtime integration.
        Defined in `ILLIXR/configs/monado.yaml`.

    *   `clean`:
        A meta-task that fetches all supported plugins and then cleans up builds across
            the entire ILLIXR project.
        Defined in `ILLIXR/configs/clean.yaml` and supported by `ILLIXR/clean.sh`.

    *   `docs`:
        A meta-task that generates and populates the documention subdirectories in the project.

-   **Profile**:
    A _Profile_ captures the compilation mode used by [_Runner_][35].
 
    *   `opt`:
        Sets [_Runner_][35] to compile the ILLIXR application and plugins with optimizations.

    *  `dbg`:
        Sets [_Runner_][35] to compile the ILLIXR application and plugins without optimizations,
            while enabling debug logic and debug logging.

-   **Schema**:
    A _Schema_ captures the specification describing the allowable structure of
        a configuration file.
    Our schema is implemented using the [json-schema specification][8].
    Defined in `ILLIXR/runner/config_schema.yaml`.

For more details about the structure of a configuration, see the [Building ILLIXR page][49].

#### Framebuffer

A region of memory used to hold graphical information to be output to a display or graphics device.

-   **Frame**:
    ***TODO***

-   **Depth Buffer**:
    ***TODO***

-   **Eye Buffer**:
    ***TODO***

For more information, see the [Wikipedia article][17].

#### Swap Chain

A set of virtual [_Framebuffers_][52] to be output to a display.
Only one Framebuffer in a Swap Chain is displayed at a time, enabling the
    other virtual framebuffers to be concurrently modified in memory.

For more information, see the [Wikipedia article][19].

#### Compositor

A window manager that establishes a [_Framebuffer_][52] for each window of a graphical system.
ILLIXR uses compositors to ... ***TODO***

For more information, see the [Wikipedia article][25].

#### Head-mounted Display

A display device worn on the head and face for use with VR and XR applications.
Also known as a _HMD_.
ILLIXR implements a [_Monado_][38] HMD to ... ***TODO***

For more information, see the [Wikipedia article][16].

#### Eye Tracking

The process of measuring the eye movement of a user (who is possibly also wearing a [_HMD_][51]).

For more information, see the [Wikipedia article][20].

#### Event Stream

A ... ***TODO***

#### Pose

The combination of orientation and position of an object, used for computer vision
    and robotics applications.
ILLIXR applications make use of poses to track the [user's HMD][51] within the virtual environment.
Internally, ILLIXR has multiple classifications of poses which are used for various purposes.

-   ***Slow Pose***:
    A _Slow Pose_ is a ... ***TODO***

-   ***Fast Pose***:
    A _Fast Pose_ is a ... ***TODO***

-   ***True Pose***:
    A _True Pose_ is a ... ***TODO***

For more information, see the [Wikipedia article][18].

#### Ground Truth

A ... ***TODO***

#### Pose Prediction

... ***TODO***

Pose Prediction is implemented in the [`pose_prediction` ILLIXR plugin][48].

#### Inertial Measurement Unit

A device that reports its orientation in space and any forces applied it.
Also known as an _IMU_.

An IMU is implemented in the [`offline_imu_cam` ILLIXR plugin][48].

For more information, see the [Wikipedia article][14].

#### Simultaneous Localization and Mapping

_Simultaneous Localization and Mapping (SLAM)_ is the computation process of creating a map
    a map of an unknown environment, and finding one's location within that space.
ILLIXR applications use SLAM ... ***TODO***

An IMU is implemented in the [`offline_imu_cam` ILLIXR plugin][48].

For more information, see the [Wikipedia article][15].

#### Asynchronous Reprojection

A ... ***TODO***

Asynchronous reprojection is implemented in the [`timewarpgl` ILLIXR plugin][48].

See the [Wikipedia article][13].

#### Distortion Correction

The processing of visual anomalies in images where rectilinear features have been warped.

For more information, see the [Wikipedia artice][28].

#### Chromatic Abberation Correction

The processing of visual anomalies in images where colors are diffracted due to imperfect optics
    or other perturbing factors.

For more information, see the [Wikipedia article][29].


## Components


#### Runner

An ILLIXR tool responsible for
    preparing the environment,
    downloading required assets & code,
    compiling each plugin,
    and
    launching the ILLIXR application.
The implementation resides in `ILLIXR/runner/`, and can be launched with
    the appropriate environment setup via `ILLIXR/runner.sh`.

-   **Action** *(Previously Loader)*:
    See [_Configuration_][30].

#### Spindle

An ILLIXR component responsible for launching and managing plugin threads.
The implementation resides in `ILLIXR/runtime/`.

See the [_Spindle_ API documentation][32].

#### Phonebook

An ILLIXR service directory used to introspectively interface plugins and their data.
The implementation resides in `ILLIXR/runtime/`.

See the [_Phonebook_ API documentation][31].

#### Switchboard

An ILLIXR event stream manager that maintains data pipelines between plugins.
The implementation resides in `ILLIXR/runtime/`.

See the [_Switchboard_ API documentation][34].


## Technologies


#### OpenXR

An open standard for Augmented and Virtual Reality.
ILLIXR components target the OpenXR standard and interact with the ILLIXR device
    via the Application Interface.

For more information, visit the [official site from the Khronos Group][1].

#### Monado

An open source, modular implementation of the OpenXR standard for [GNU/Linux][3].

See the ILLIXR [Monado Overview][44] and [Monado Dataflow][45] pages for details about our
    runtime integration using Monado.

For more information, visit the [official Monado development site][2].

#### Godot

An open source game development engine.
ILLIXR applications targeting the [_OpenXR_][50] use Godot ... ***TODO***

For more information, visit the [official Godot site][12].

#### Xvfb

A virtual framebuffer for the [X11 Window Sytem][22].
ILLIXR uses Xvfb to enable running the graphical ILLIXR application without requiring the user
    to have a graphical environment configured at application launch.

For more information, see the [Xfvb man page][5].

#### Docker

A platform and containerization framework for deploying applications under virtualization.
ILLIXR uses Docker to deploy and test code in a continuous integration and deployment pipeline.

For more information, see the [Docker overview and getting started page][6].

#### QEMU-KVM

An open source virtulization tool and machine emulator.
See the instructions for running [ILLIXR under Virtualization][43].

For more information, see the [official QEMU page][11].

#### YAML

A markup language and data serilization standard designed to be user friendly.
We make use of the [PyYAML][9] and [pyyaml-include][10] libraries to implement
    our [_Configuration_][30] implementation.

For more information, visit the [official YAML page][7].

#### SQLite

A SQL database engine implementation in C designed to be lightweight and easy to use.
The ILLIXR project allows user to records application statistics to a local database
    for efficient processing.
See the [Logging and Metrics page][53] for usage details.

For more information, see the [SQLite development site][21].

#### Vulkan

A cross-platform graphics API that allows developers to efficiently target
    low-level hardware features.
ILLIXR uses Vulkan to ... ***TODO***

For more information, see the [official Vulkan page from the Khronos Group][23].

#### OpenGL

A cross-platform graphics API that allows developers to create graphics applications
    easily and portably.
Also known as _GL_.
ILLIXR uses OpenGL to ... ***TODO***

-   **GL Context**:
    ***TODO***

-   **[GLFW][24]**:
    An open source implementation of OpenGL.
    ***TODO***

For more information, see the [official OpenGL page from the Khronos Group][24].

#### Ubuntu

An open source [GNU/Linux][3] operating system and distribution.
ILLIXR currently supports the _Long Term Support (LTS)_ versions of Ubuntu:
    18.04 LTS (Bionic)
    and
    20.04 LTS (Focal).

For more information, visit the [official Ubuntu site][29].


[//]: # (- References -)

[1]:    https://www.khronos.org/openxr
[2]:    https://monado.dev
[3]:    https://www.gnu.org/gnu/linux-and-gnu.en.html
[4]:    https://github.com/ILLIXR/ILLIXR
[5]:    https://www.x.org/releases/X11R7.7/doc/man/man1/Xvfb.1.xhtml
[6]:    https://docs.docker.com/get-started/overview
[7]:    https://yaml.org
[8]:    https://json-schema.org
[9]:    https://github.com/yaml/pyyaml
[10]:   https://pypi.org/project/pyyaml-include
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

[//]: # (- Internal -)

[30]:   glossary.md#configuration
[31]:   api/html/classILLIXR_1_1phonebook.html
[32]:   api/html/classILLIXR_1_1threadloop.html
[33]:   api/html/classILLIXR_1_1plugin.html
[34]:   api/html/classILLIXR_1_1switchboard.html
[35]:   glossary.md#runner
[36]:   glossary.md#ground-truth
[37]:   glossary.md#pose
[38]:   glossary.md#monado
[39]:   glossary.md#xvfb
[40]:   modifying_a_plugin.md
[41]:   writing_your_plugin.md
[42]:   glossary.md#docker
[43]:   qemu.md
[44]:   monado_illixr_runtime_overview.md
[45]:   monado_integration_dataflow.md
[46]:   glossary.md#yaml
[47]:   glossary.md#plugin
[48]:   illixr_plugins.md
[49]:   building_illixr.md#configuration
[50]:   glossary.md#openxr
[51]:   glossary.md#head-mounted-display
[52]:   glossary.md#framebuffer
[53]:   logging_and_metrics.md
[54]:   building_illixr.md
[55]:   monado_illixr_runtime_overview.md#translation-plugin
