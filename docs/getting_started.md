# Getting Started

## ILLIXR standalone

The ILLIXR runtime can be built "standalone", to run without Monado. This mode does not support
OpenXR, but it is faster for development. These instructions have been tested with Ubuntu 18.10.

1. Clone the repository.

        git clone --recursive https://github.com/charmoniumQ/illixr-prototype
        # TODO: update this link when we move repositories


2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init --recursive

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds a specific
   version of OpenCV from source.

        ./install_deps.sh

4. Build and run ILLIXR standalone.

        make run.dbg

## ILLIXR with Monado (supports OpenXR)

Monado only supports Ubuntu 18.10, because of a low-level driver issue.

1. Clone and build ILLIXR (same steps as standalone, except the make target is `all.dbg.so`).

        git clone --recursive https://github.com/charmoniumQ/illixr-prototype
        git submodule update --init --recursive
        ./install_deps.sh
        make all.dbg.so

2. Clone and build Monado.

        git clone https://github.com/ILLIXR/monado_integration.git
        cd monado_integration
        mkdir build && cd build
        cmake .. -DBUILD_WITH_LIBUDEV=0 -DBUILD_WITH_LIBUVC=0 -DBUILD_WITH_LIBUSB=0 -DBUILD_WITH_LIBUDEV=0 -DBUILD_WITH_NS=0 -DBUILD_WITH_PSMV=0 -DBUILD_WITH_PSVR=0 -DBUILD_WITH_OPENHMD=0 -DBUILD_WITH_VIVE=0  -DILLIXR_PATH=../../ILLIXR -G "Unix Makefiles"
        # replace ../ILLIXR with the path to ILLIXR
        make -j$(nproc)

3. Clone and build our application example.

        git clone https://gitlab.freedesktop.org/monado/demos/openxr-simple-example
        cd openxr-simple-example
        mkdir build && cd build
        cmake ..
        make -j$(nproc)

4. Set environment variables and run.

        export XR_RUNTIME_JSON=../monado_integration/build/openxr_monado-dev.json
        # replace ../monado_integration with the path to the previous repo
        export ILLIXR_PATH=../ILLIXR/runtime/plugin.dbg.so
        export ILLIXR_COMP=../ILLIXR/ground_truth_slam/plugin.dbg.so:../ILLIXR/offline_imu_cam/plugin.dbg.so:../ILLIXR/open_vins/plugin.dbg.so:../ILLIXR/pose_prediction/plugin.dbg.so:../ILLIXR/timewarp_gl/plugin.dbg.so:../ILLIXR/debugview/plugin.dbg.so:../ILLIXR/audio_pipeline/plugin.dbg.so
        # replace ../ILLIXR with the path to ILLIXR

## Next steps

 The source code is divided into the following directories:
- `runtime`: create a runnable binary that loads every plugin.
    * This contains Spindle, which is responsible for loading plugins.

- `common`: resources one might use in each plugin. Most plugins symlink this directory into theirs.
    * Contains the interface for Switchboard, which maintains event-streams (implementation is in `runtime`).
    * Contains the interface for Phonebook, which is a service-directory (implementation is in `runtime`).

- a directory for each plugin. Almost all of the XR functionality is implemented in plugins. See
  [Default Components][1] for more details.

Try browsing the source of plugins.  If you edit any of the source files, this make commend will
detect and rebuild the respective binary. If you want to add your own, see [Writing Your Plugin][2].

[1]: default_plugins.md
[2]: writing_your_plugin.md
