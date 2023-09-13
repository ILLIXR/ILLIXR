# Monado Integration Overview

ILLIXR's [_Plugins_][23] provide XR services, and the [_Runtime_][24] ties them together.
However, we don't want to force developers to write _their whole application_ specifically
    for ILLIXR.
As such, we want to implement a common interface XR runtimes, such as [OpenXR][21],
    so one application can work on several runtimes (including ours).
In order to support OpenXR, we modified [Monado][20], an existing, open-source implementation
    of the standard.

-   When running ILLIXR without Monado, the ILLIXR runtime is the entry-point.
    Phonebook and switchboard are initialized and plugins are loaded, among which is the gldemo app.

-   When running from Monado, however, as mandated by OpenXR specifications,
        the application is the entry point.
    As a result, the ILLIXR runtime system is loaded at a later point as a shared library.
    This page documents the changes to the ILLIXR runtime when an OpenXR application is used.


## OpenXR Application Launch

As specified by [_OpenXR_][21], the OpenXR application initializes the OpenXR runtime by reading a
    configuration JSON file pointed to by an environment variable and loads the OpenXR runtime,
    which is Monado in this case, as a shared library into its address space.
Consult the OpenXR specifications and the OpenXR-SDK from Khronos Group for more details.


## Monado Device Probe and ILLIXR Initialization

During initialization, [_Monado_][20] asks all drivers to probe for and initialize [_HMDs_][25]
    and controllers, internally known as `xdev`s.
Our ILLIXR driver will always respond to Monado with one discovered HMD that
    will be used to capture OpenXR queries and events from Monado's state tracker.
The driver obtains the path to the ILLIXR runtime `.so` file and a list of plugins from
    environment variables.

After probing is finished, the application will start to create an OpenXR session.
At some point in this process, the application will send its rendering context to the runtime,
    which we capture and send to the ILLIXR driver.
At this moment, all necessary data is ready and ILLIXR will be launched.


## ILLIXR Runtime Launch

When used with [_Monado_][20], the ILLIXR [_Runtime_][24] is compiled into
    a shared library instead of an executable.
The library exports its two major functionalities:
    initializing [_Switchboard_][26] and [_Phonebook_][27],
    and
    loading [_Plugins_][23].

The driver starts to load the runtime by loading the shared library into the current
    (application's) address space and calls the Switchboard and Phonebook initialization.
Then, it calls the plugin loading for each ILLIXR plugin
    (except [`gldemo`][28], which is replaced by the OpenXR app).
Finally, it calls a special plugin loading which takes a function address instead of a file path
    to load a [_Translation Plugin_][30] into ILLIXR as the application.
If the plugin implements a long running computation, it may block the main ILLIXR thread
    which drives the entire application.
To remedy this, a plugin should implement long running processing in its own thread.
This way, the driver will be able to reacquire control and return to Monado
    and the application efficiently.


## Translation Plugin

When the application and all [ILLIXR plugins][28] are up and running,
    the translation plugin handles the connection between [_Monado_][20] and ILLIXR.
It might be confusing to see that this plugin is part of the ILLIXR driver which is part of
    Monado while at the same time also part of ILLIXR as a plugin.
However, Monado and ILLIXR are running in different threads in the same address space.
The translation plugin is the interface of these two parallel systems.

The translation plugin handles two types of events at the moment:
    [_pose_][29] requests and [_frame_][29] submissions.
From the view of Monado, the translation plugin is the destination of all requests:
    from the application,
    to Monado's state trackers,
    to the xdev interface who is responsible for servicing the request.
From the view of ILLIXR, the translation plugin behaves the same as the [`gldemo` application][28]:
    reading pose and submitting frames.

For implementation details regarding the representation of poses and frames in Monado
    and in ILLIXR, please see ILLIXR's [Monado Integration Dataflow][22].


[//]: # (- Internal -)

[20]:   glossary.md#monado
[21]:   glossary.md#openxr
[22]:   monado_integration_dataflow.md
[23]:   glossary.md#plugin
[24]:   glossary.md#runtime
[25]:   glossary.md#head-mounted-display
[26]:   glossary.md#switchboard
[27]:   glossary.md#phonebook
[28]:   illixr_plugins.md
[29]:   glossary.md#framebuffer
[30]:   glossary.md#translation-plugin
