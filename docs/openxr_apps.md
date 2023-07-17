# OpenXR

ILLIXR aims to be OpenXR-compatible, which means that OpenXR apps should be able to run on top of ILLIXR. There are many ways that OpenXR apps can be built; they can be programmed manually using the OpenXR SDK or built through a game engine that supports OpenXR on Linux.

## Running OpenXR Apps

When using ILLIXR's integration with Monado, a list of applications can be provided through ``openxr_app.yaml``. In ``opt`` mode, these applcations will be run automatically after waiting 5 seconds for the Monado service to launch. In ``dbg`` mode, this list is ignored and users must manually launch the application from a separate terminal.

### Manually Launching Applications

When running an OpenXR application through the command-line, you can force the application to look for a specific runtime with the ``XR_RUNTIME_JSON`` environment variable. This should point to a specific JSON file, which in the case of ILLIXR, can be found at ``{path_to_monado}/build/openxr_monado-dev.json``. 

To run this application on ILLIXR, first launch the Monado service in one terminal with: 
```
./runner.sh configs/monado.yaml
```
Then after the service has been launched properly, the OpenXR application (e.g. ``./openxr_app``) can be launched with:
```
XR_RUNTIME_JSON={path_to_monado}/build/openxr_monado-dev.json ./openxr_app
```

Note that in this case, the OpenXR application itself can also be launched with ``gdb``.


### Defining Applications in Config

Although the OpenXR specification does not explicitly require support for multiple OpenXR proocesses, but since Monado does provide this support, we currently allow the launching of multiple applications. There are two main ways to run an OpenXR app:

1. If there is a build system in place to build the app, an application can be defined as follows:
```
app:
  src_path:
    git_repo : https://gitlab.freedesktop.org/monado/demos/openxr-simple-example
    version  : master
  bin_subpath: build/openxr-example
```
This provides a link to path to Git repo (alterenatively, a local directory can be specified through ``src_path``), and ``bin_subpath`` defines where the binary will be built.
2. If the application binary is already available, an application can just be linked to directly with:
```
app: {path_to_app_binary}
```

To provided multiple applications, multiple ``app``s can be defined in the ``openxr_app.yaml`` file.


## Building OpenXR Apps from Game Engines

### Godot Game Engine

Godot 3.4 with the OpenXR plugin has been tested with ILLIXR. To build an app through Godot 3.4, follow the steps below:
1. You may either clone Godot with ``git clone --branch 3.4 https://github.com/godotengine/godot`` and build it locally with the provided build instructions, or you may install Godot 3.4.
2. Clone the Godot OpenXR plugin with ``git clone --reecursive https://github.com/GodotVR/godot_openxr`` and follow the build instructions to build it locally.
3. For a set of sample Godot apps, clone the following repository with ``git clone https://github.com/ILLIXR/OpenXR-Apps`.
4. For a given Godot project:
  - Go to the app you want to run (e.g. OpenXR-Apps/sponza) and delete the contents of /addons
  - Go to the OpenXR plugin you built in step 2 and copy the contents of godot_openxr/demo/addons into the addons folder in sponza
5. Start your Godot binary:
  - Click import on the right hand side and find the project.godot file in the sponza directory
  - Once the game scene is loaded in the godot editor, go to project->export->linux (runnable). Set the export path to ./Sponza_VR.x86_64 and under the “options” tab, for both the Debug and Release, point it to the Godot executable that you used to open Godot
    - If you built Godot locally, you may be prompted with a message regarding missing templates. In that case, you should also be provided the option to install the missing templates.
  - Export project

Godot 4 also now supports OpenXR natively, although it has not yet been tested with ILLIXR.


### Unreal Engine 4/5

Both Unreal Engine 4 and 5 have native support for OpenXR on Linux. Need to add more information about exporting here.


## Notes

Depending on how you exit the service, there may be some cases where the Monado service does not clean up properly. In this case, the service may fail to launch on a later run. if the service logs that ``/run/user/1000/monado_comp_ipc`` is already in use, you may manually delete the file with:
```
sudo rm -rf /run/user/1000/monado_comp_ipc
```