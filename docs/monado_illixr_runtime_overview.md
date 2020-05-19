# Monado Integration Overview

ILLIXR's plugins provide XR services, and the runtime ties them together. However, we don't want to
force developers to write _their wohle application_ specifically for ILLIXR. As such, we want to
implement a common interface XR runtimes, such as [OpenXR][1], so one application can work on
several runtimes (including ours). In order to support OpenXR, we modified [Monado][2], an existing,
open-source implementation.

- When running ILLIXR without Monado, the Illixr runtime is the entry-point. Phonebook and
  switchboard are initialized and plugins are loaded, among which is the gldemo app.

- When running from Monado, however, as mandated by OpenXR specifications, the application is the
  entry point. As a result, the Illixr runtime system is loaded at a later point as a shared
  library. This article documents the changes to the Illixr runtime when an OpenXR application is
  used.

## 1. App launches and brings up Monado

As specified by OpenXR, the OpenXR application initializes the OpenXR runtime by reading a
configuration JSON file pointed to by an environment variable and loads the OpenXR runtime, which is
Monado in this case, as a shared library into its address space. Consult the OpenXR specifications
and the OpenXR-SDK from Khronous Group for more details.

## 2.  Monado probes HMD devices and Illixr Initializes

During initialization, Monado asks all drivers to probe for and initialize HMDs and controllers,
internally known as `xdev`s.  We have an Illixr driver, which will always respond to Monado with one
discovered HMD that will be used to capture OpenXR queries and events from Monado's state
tracker. The Illixr driver obtains the path to the Illixr runtime so file and a list of plugins from
environment variables.

After probing is finished, the application will start to create an OpenXR session. At some point in
this process, the application will send its rendering context to the runtime, which we capture and
send to the Illixr driver. At this moment, all necessary data is ready and Illixr will be launched.

## 3. Illixr Runtime Launch

When used with Monado, the Illixr runtime is compiled into a shared library instead of an
executable. The library exports its two major functionalities: initializing switchboard and
phonebook, and load plugins.

The drivers starts to load the runtime by loading the shared library into the current (the app's)
address space and calls the switchboard and phonebook initialization. Then, it calls the plugin
loading for each Illixr plugin (except gldemo, which is replaced by the OpenXR app). Finally, it
calls a special plugin loading which takes a function address instead of a file path to load a
translation plugin into Illixr as the application. The translation plugin will be in the next
section. Each plugin should either not block or start its own thread, so the driver will be able to
reacquire control and return to Monado and the app shortly.

## 4. The translation plugin

When the app and all Illixr plugins are up and running, the translation plugin handles the
connection between Monado and Illixr. It might be confusing to see that this plugin is part of the
Illixr driver which is part of Monado while at the same time also part of Illixr as a plugin. But
Monado and Illixr are running in different threads in the same address space. The translation plugin
is the intersection of these two parallel systems serving as a bridge between the two.

The translation plugin handles two types of events at the moment: pose requests and frame
submissions. From the view of Monado, it is the destination of all requests: from the application,
to Monado's state trackers, to the xdev interface who is responsible for servicing the request. From
the view of Illixr, it behaves the same as the gldemo app: reading pose and submitting frames.

For implementation details regarding the representation of poses and frames in Monado and in Illixr,
please see Monado Integration Dataflow.

[1]: https://www.khronos.org/openxr/
[2]: https://monado.dev/
