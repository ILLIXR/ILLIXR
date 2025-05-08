# Monado Integration Overview

ILLIXR's [_plugins_][G13] provide XR services, and the [_runtime_][G14] ties them together. However, we don't want to
force developers to write _their whole application_ specifically for ILLIXR. As such, we want to implement a common
interface XR runtimes, such as [_OpenXR_][G11], so one application can work on several runtimes (including ours). In
order to support OpenXR, we modified [_Monado_][G10], an existing, open-source implementation of the standard.

-   When running ILLIXR without Monado, the ILLIXR runtime is the entry-point.
    Phonebook and switchboard are initialized and plugins are loaded, among which is the vkdemo app.

- When running from Monado, however, as mandated by OpenXR specifications, the application is the entry point. As a
  result, the ILLIXR runtime system is loaded at a later point as a shared library. This page documents the changes to
  the ILLIXR runtime when an OpenXR application is used.

## OpenXR Application Launch

As specified by [_OpenXR_][G11], the OpenXR application initializes the OpenXR runtime by reading a configuration JSON
file pointed to by an environment variable and loads the OpenXR runtime, which is Monado in this case, as a shared
library into its address space. Consult the [OpenXR specifications][E10] and the [OpenXR-SDK][E11] from Khronos Group for
more details.

## Monado Device Probe and ILLIXR Initialization

During initialization, [_Monado_][G10] asks all drivers to probe for and initialize [_HMDs_][G15] and controllers,
internally known as `xdev`s. Our ILLIXR driver will always respond to Monado with one discovered HMD that will be used
to capture OpenXR queries and events from Monado's state tracker. The driver obtains the path to the ILLIXR runtime
`.so` file and a list of plugins from environment variables.

After probing is finished, the application will start to create an OpenXR session. At some point in this process, the
application will send its rendering context to the runtime, which we capture and send to the ILLIXR driver. At this
moment, all necessary data is ready and ILLIXR will be launched.

## ILLIXR Runtime Launch

When used with [_Monado_][G10], the ILLIXR [_Runtime_][G14] is compiled into a shared library instead of an executable.
The library exports its two major functionalities: initializing the [_switchboard_][G16] and [_phonebook_][G17], and
loading [_plugins_][G13].

The driver starts to load the runtime by loading the shared library into the current
    (application's) address space and calls the Switchboard and Phonebook initialization.
Then, it calls the plugin loading for each ILLIXR plugin
    (except [`vkdemo`][28], which is replaced by the OpenXR app).
Finally, it calls a special plugin loading which takes a function address instead of a file path
    to load a Translation Plugin into ILLIXR as the application.
If the plugin implements a long-running computation, it may block the main ILLIXR thread
    which drives the entire application.
To remedy this, a plugin should implement long-running processing in its own thread.
This way, the driver will be able to reacquire control and return to Monado
    and the application efficiently.


## Translation Plugin

When the application and all [ILLIXR plugins][P10] are up and running, the translation plugin handles the connection
between [_Monado_][G10] and ILLIXR. It might be confusing to see that this plugin is part of the ILLIXR driver which is
part of Monado while at the same time also part of ILLIXR as a plugin. However, Monado and ILLIXR are running in
different threads in the same address space. The translation plugin is the interface of these two parallel systems.

The translation plugin handles two types of events at the moment: [_pose_][G20] requests and [_frame_][G19] submissions.
From the view of Monado, the translation plugin is the destination of all requests: from the application, to Monado's
state trackers, to the xdev interface who is responsible for servicing the request. From the view of ILLIXR, the
translation plugin behaves the same as the [`vkdemo` application][P10]: reading pose and submitting frames.

For implementation details regarding the representation of poses and frames in Monado and in ILLIXR, please see
ILLIXR's [Monado Integration Dataflow][10].


[//]: # (- glossary -)

[G10]:   ../glossary.md#monado

[G11]:   ../glossary.md#openxr

[G13]:   ../glossary.md#plugin

[G14]:   ../glossary.md#runtime

[G15]:   ../glossary.md#head-mounted-display

[G16]:   ../glossary.md#switchboard

[G17]:   ../glossary.md#phonebook

[G19]:   ../glossary.md#framebuffer

[G20]:   ../glossary.md#pose


[//]: # (- plugins -)

[P10]:   ../illixr_services.md#vkdemo


[//]: # (- internal -)

[10]:   monado_integration_dataflow.md


[//]: # (- external -)

[E10]:   https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html

[E11]:   https://github.com/KhronosGroup/OpenXR-SDK
