# Building ILLIXR

The ILLIXR application is configured and built via CMake. The CMake system checks for required dependencies,
builds each requested plugin, builds the main ILLIXR binary, and (optionally) installs these components. 

ILLIXR currently only builds on Linux systems, and has been tested on the following configurations:

- Ubuntu
  - 20.04
  - 22.04
- Fedora
  - 37
  - 38
- CentOS
  - stream9

Other versions of these operating systems may work, but will likely require some manual installation of dependencies.
For other Linux distributions (e.g. RHEL) it will require significant manual installation of dependencies as many
are not available in the distribution repos. Full installation instructions are available in the
[Plugin Selector](install/setup.html). The instructions below are a generalized version.

## Dependencies

There are two levels of dependencies in ILLIXR: those that are required for any type of build, and those that are
required only for specific plugins. 

See the [Plugin Selector](install/setup.html) for a listing on dependencies for each plugin, and their installation instructions.
The following dependencies are required for any build:

Ubuntu
```bash
sudo apt-get install libglew-dev libglu1-mesa-dev libsqlite3-dev libx11-dev libgl-dev pkg-config libopencv-dev libeigen3-dev
```
Fedora
```bash
sudo dnf install glew-devel mesa-libGLU-devel sqlite-devel libX11-devel mesa-libGL-devel pkgconf-pkg-config opencv-devel eigen3-devel
```
CentOS
```bash
sudo yum install glew-devel mesa-libGLU-devel sqlite-devel libX11-devel mesa-libGL-devel pkgconf-pkg-config vtk-devel eigen3-devel
```

## Configuration

Configuring ILLIXR is straight forward, but there are many options

```bash
cd ILLIXR
mkdir build
cd build
cmake .. <OPTIONS>
```
You can use the [Plugin Selector](install/setup.html) to generate the CMake options for the
particular plugins you want to use. To install ILLIXR into a non-standard location (e.g. in a user's home directory) 
specify `-DCMAKE_INSTALL_PREFIX=<INSTALL PATH>`, where INSTALL PATH is the desired install location. Use `-DCMAKE_BUILD_TYPE=<>`
to specify the build type (choices are `Debug`, `Release`, and `RelWithDebInfo`).

An alternate to specifying the plugins as command line arguments is to create a [_YAML_][2] file which specifies the
plugins to build. Using `-DYAML_FILE=<FILE_NAME>` as the command line argument specifying the [_YAML_][2] file to use.
The format for the [_YAML_][2] file is:
```yaml
group: none
plugins: timewarp_gl,gldemo,ground_truth_slam
data: http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip
```
Where group is one of `all`, `ci`, `monado`, `native`, or `none`; plugins is a group of comma separated plugin names 
(case sensitive, no spaces); data is the location of the data to download (if any). The current list of plugins is

- audio_pipeline
- debugview
- depthai
- gldemo
- ground_truth_slam
- gtsam_integrator
- kimera_vio
- monado
- offline_cam
- offline_imu
- offload_data
- offload_vio
- openxr_app
- orb_slam
- passthrough_integrator
- pose_lookup
- pose_prediction
- realsense
- record_imu_cam
- rk4_integrator
- timewarp_gl

The current groups are defined as

- all: All current plugins
- ci: audio_pipeline, gldemo, ground_truth_slam, gtsam_integrator, kimera_vio, offline_cam, offline_imu, pose_prediction, timewarp_gl
- monado: audio_pipeline, gtsam_integrator, kimera_vio, monado, offline_cam, offline_imu, openxr_app, pose_prediction, timewarp_gl
- native: audio_pipeline, debugview, gldemo, ground_truth_slam, gtsam_integrator, kimera_vio, offline_cam, offline_imu, offload_data, pose_prediction, timewarp_gl
- none: only plugins which are individually specified in the `plugins` entry are used

The CMake process will also create a [_YAML_][2] file call `illixr.yaml` which can be used as input to the binary.
## Building ILLIXR

To compile ILLIXR just run
```bash
make -jX
make install
```
X is the number of cores to build with concurrently and the install step is optional (may require `sudo`). Depending on
how many plugins and dependencies are being built, and the number of cores being used, the build time can
vary from a few minutes to over half an hour or more.

## Running ILLIXR

To run the ILLIXR binary just call `main.<>.exe` with any of the following command line arguments. (the `<>` indicate
an infix specifying the build type, for `Debug` use `dbg`, for `Release` use `opt`, for `RelWithDebInfo` use `optdbg`)

- --duration=<>, the duration to run for in seconds (default is 60)
- --data=<>, the data file to use
- --demo_data=<>, the demo data to use
- --enable_offload, ??
- --enable_alignment, ??
- --enable_verbose_errors, give more information about errors
- --enable_pre_sleep, ??
- -p,--plugins=<>, comma separated list of plugins to use (case sensitive, all lowercase, no spaces)
- -g,--group=<>, the group of plugins to use.
- -y,--yaml<>, the [_YAML_][2] file to use which specifies some or all of the above arguments (e.g. the generated `illixr.yaml`)

An example of a [_YAML_][2] config file is
```yaml
group: none
plugins: timewarp_gl,gldemo,ground_truth_slam
data: http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip
enable_offload: true
enable_verbose_errors: false
```

Regarding parameters for the binary, the following priority will be used:
1. If the parameter is specified on the command line it will have the highest priority
2. If the parameter has a value in the yaml file this value will only be used if it is not specified on the command line (second priority)
3. If the parameter has a value as an environment variable this value will only be used if it is not specified on the command line nor yaml file



## Rationale

-  The current system can use config files to control everything from the build to running ILLIXR, inkeeping with the [DRY principle][1].
  However, for maximum flexibility control can also be done at the command line level as well.


## Philosophy

-   Each plugin should not have to know or care how the others are compiled.
    In the future, they may even be distributed separately, just as SOs.
    Therefore, each plugin needs its own build system.

-   Despite this per-plugin flexibility, building the 'default' set of ILLIXR plugins
        should be extremely easy.

-   It should be easy to build in parallel.

-   Re-running `make` (and optionally `cmake` first) will only rebuild those parts of the code with have changed.


[//]: # (- References -)

[1]:    https://en.wikipedia.org/wiki/Don%27t_repeat_yourself

[//]: # (- Internal -)

[2]:   glossary.md#yaml
